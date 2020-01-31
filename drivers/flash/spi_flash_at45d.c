/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <drivers/flash.h>
#include <drivers/spi.h>
#include <init.h>
#include <logging/log.h>

LOG_MODULE_REGISTER(spi_flash_at45d, CONFIG_FLASH_LOG_LEVEL);

#define CMD_READ		0x01 /* Continuous Array Read (Low Power Mode) */
#define CMD_WRITE		0x02 /* Main Memory Byte/Page Program through
					Buffer 1 without Built-In Erase */
#define CMD_READ_ID		0x9F /* Manufacturer and Device ID Read */
#define CMD_READ_STATUS		0xD7 /* Status Register Read */
#define CMD_SECTOR_ERASE	0x7C /* Sector Erase */
#define CMD_BLOCK_ERASE		0x50 /* Block Erase */
#define CMD_PAGE_ERASE		0x81 /* Page Erase */

#define AT45D_SECTOR_SIZE	0x10000UL

#define DEF_BUF_SET(_name, _buf_array) \
	const struct spi_buf_set _name = { \
		.buffers = _buf_array, \
		.count   = ARRAY_SIZE(_buf_array), \
	}

struct spi_flash_at45d_data {
	struct device *spi;
	struct spi_cs_control spi_cs;
	struct k_sem lock;
};

struct spi_flash_at45d_config {
	const char *spi_bus;
	struct spi_config spi_cfg;
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	struct flash_pages_layout pages_layout;
#endif
	u32_t chip_size;
	u16_t block_size;
	u16_t page_size;
	u8_t jedec_id[3];
};

static struct spi_flash_at45d_data *get_dev_data(struct device *dev)
{
	return dev->driver_data;
}

static const struct spi_flash_at45d_config *get_dev_config(struct device *dev)
{
	return dev->config->config_info;
}

static void acquire(struct device *dev)
{
	struct spi_flash_at45d_data *data = dev->driver_data;

	k_sem_take(&data->lock, K_FOREVER);
}

static void release(struct device *dev)
{
	struct spi_flash_at45d_data *data = dev->driver_data;

	k_sem_give(&data->lock);
}

static int check_jedec_id(struct device *dev)
{
	int err;

	u8_t const *expected_id = get_dev_config(dev)->jedec_id;
	u8_t read_id[sizeof(get_dev_config(dev)->jedec_id)];
	const u8_t opcode = CMD_READ_ID;	
	const struct spi_buf tx_buf[] = {
		{
			.buf = (void *)&opcode,
			.len = sizeof(opcode),
		}
	};
	const struct spi_buf rx_buf[] = {
		{
			.len = sizeof(opcode),
		},
		{
			.buf = read_id,
			.len = sizeof(read_id),
		}
	};
	DEF_BUF_SET(tx_buf_set, tx_buf);
	DEF_BUF_SET(rx_buf_set, rx_buf);

	err = spi_transceive(get_dev_data(dev)->spi,
			     &get_dev_config(dev)->spi_cfg,
			     &tx_buf_set, &rx_buf_set);
	if (err != 0) {
		LOG_ERR("SPI transaction failed with code: %d", err);
		return -EIO;
	}

	if (memcmp(expected_id, read_id, sizeof(read_id)) != 0) {
		LOG_ERR("Wrong JEDEC ID: %02X %02X %02X, "
			"expected: %02X %02X %02X",
			read_id[0], read_id[1], read_id[2],
			expected_id[0], expected_id[1], expected_id[2]);
		return -ENODEV;
	}

	return 0;
}

/*
 * Reads 2-byte Status Register:
 * - Byte 0 to LSB
 * - Byte 1 to MSB
 * of the pointed parameter.
 */
static int read_status_register(struct device *dev, u16_t *status)
{
	int err;

	const u8_t opcode = CMD_READ_STATUS;	
	const struct spi_buf tx_buf[] = {
		{
			.buf = (void *)&opcode,
			.len = sizeof(opcode),
		}
	};
	const struct spi_buf rx_buf[] = {
		{
			.len = sizeof(opcode),
		},
		{
			.buf = status,
			.len = sizeof(u16_t),
		}
	};
	DEF_BUF_SET(tx_buf_set, tx_buf);
	DEF_BUF_SET(rx_buf_set, rx_buf);

	err = spi_transceive(get_dev_data(dev)->spi,
			     &get_dev_config(dev)->spi_cfg,
			     &tx_buf_set, &rx_buf_set);
	if (err != 0) {
		LOG_ERR("SPI transaction failed with code: %d", err);
	}

	return (err != 0) ? -EIO : 0;
}

static int wait_until_ready(struct device *dev)
{
	int err;
	u16_t status;

	do {
		err = read_status_register(dev, &status);
	} while (err == 0 && !(status & 0x80));

	return err;
}

static int configure_page_size(struct device *dev)
{
	int err;
	u16_t status;

	err = read_status_register(dev, &status);
	if (err != 0) {
		return err;
	}

	if (status & 0x01) {
		/* If the device is already configured for "power of 2" binary
		 * page size, there is nothing more to do.
		 */
		return 0;
	}

	u8_t const conf_binary_page_size[] = {
		[0] = 0x3D,
		[1] = 0x2A,
		[2] = 0x80,
		[3] = 0xA6,
	};
	const struct spi_buf tx_buf[] = {
		{
			.buf = (void *)conf_binary_page_size,
			.len = sizeof(conf_binary_page_size),
		}
	};
	DEF_BUF_SET(tx_buf_set, tx_buf);

	err = spi_write(get_dev_data(dev)->spi,
			&get_dev_config(dev)->spi_cfg,
			&tx_buf_set);
	if (err != 0) {
		LOG_ERR("SPI transaction failed with code: %d", err);
	} else {
		err = wait_until_ready(dev);
	}
	
	return (err != 0) ? -EIO : 0;
}

static bool is_valid_request(off_t addr, size_t size, size_t chip_size)
{
	return (addr >= 0 && (addr + size) <= chip_size);
}

static int spi_flash_at45d_read(struct device *dev, off_t offset,
				void *data, size_t len)
{
	if (!is_valid_request(offset, len, get_dev_config(dev)->chip_size)) {
		return -ENODEV;
	}

	int err;
	u8_t const op_and_addr[] = {
		[0] = CMD_READ,
		[1] = (offset >> 16) & 0xFF,
		[2] = (offset >> 8)  & 0xFF,
		[3] = (offset >> 0)  & 0xFF,
	};
	const struct spi_buf tx_buf[] = {
		{
			.buf = (void *)&op_and_addr,
			.len = sizeof(op_and_addr),
		}
	};
	const struct spi_buf rx_buf[] = {
		{
			.len = sizeof(op_and_addr),
		},
		{
			.buf = data,
			.len = len,
		}
	};
	DEF_BUF_SET(tx_buf_set, tx_buf);
	DEF_BUF_SET(rx_buf_set, rx_buf);

	acquire(dev);
	err = spi_transceive(get_dev_data(dev)->spi,
			     &get_dev_config(dev)->spi_cfg,
			     &tx_buf_set, &rx_buf_set);
	release(dev);

	if (err != 0) {
		LOG_ERR("SPI transaction failed with code: %d", err);
	}

	return (err != 0) ? -EIO : 0;
}

static int perform_write(struct device *dev, off_t offset,
			 const void *data, size_t len)
{
	int err;
	u8_t const op_and_addr[] = {
		[0] = CMD_WRITE,
		[1] = (offset >> 16) & 0xFF,
		[2] = (offset >> 8)  & 0xFF,
		[3] = (offset >> 0)  & 0xFF,
	};
	const struct spi_buf tx_buf[] = {
		{
			.buf = (void *)&op_and_addr,
			.len = sizeof(op_and_addr),
		},
		{
			.buf = (void *)data,
			.len = len,
		}
	};
	DEF_BUF_SET(tx_buf_set, tx_buf);

	err = spi_write(get_dev_data(dev)->spi,
			&get_dev_config(dev)->spi_cfg,
			&tx_buf_set);
	if (err != 0) {
		LOG_ERR("SPI transaction failed with code: %d", err);
	} else {
		err = wait_until_ready(dev);
	}
	
	return (err != 0) ? -EIO : 0;
}

static int spi_flash_at45d_write(struct device *dev, off_t offset,
				 const void *data, size_t len)
{
	const struct spi_flash_at45d_config *cfg = get_dev_config(dev);

	if (!is_valid_request(offset, len, get_dev_config(dev)->chip_size)) {
		return -ENODEV;
	}

	acquire(dev);

	int err = 0;
	while (len) {
		size_t chunk_len = len;
		off_t current_page_end =
			offset - (offset & (cfg->page_size-1)) + cfg->page_size;

		if (chunk_len > (current_page_end - offset)) {
			chunk_len = (current_page_end - offset);
		}

		err = perform_write(dev, offset, data, chunk_len);
		if (err != 0) {
			break;
		}

		offset += chunk_len;
		len    -= chunk_len;
	}

	release(dev);

	return err;
}

static int perform_chip_erase(struct device *dev)
{
	int err;
	u8_t const chip_erase_cmd[] = {
		[0] = 0xC7,
		[1] = 0x94,
		[2] = 0x80,
		[3] = 0x9A,
	};
	const struct spi_buf tx_buf[] = {
		{
			.buf = (void *)&chip_erase_cmd,
			.len = sizeof(chip_erase_cmd),
		}
	};
	DEF_BUF_SET(tx_buf_set, tx_buf);

	err = spi_write(get_dev_data(dev)->spi,
			&get_dev_config(dev)->spi_cfg,
			&tx_buf_set);
	if (err != 0) {
		LOG_ERR("SPI transaction failed with code: %d", err);
	} else {
		err = wait_until_ready(dev);
	}

	return (err != 0) ? -EIO : 0;
}

static bool is_erase_possible(size_t entity_size,
			      off_t offset, size_t requested_size)
{
	return (requested_size >= entity_size &&
		(offset & (entity_size - 1)) == 0);
}

static int perform_erase_op(struct device *dev, u8_t opcode, off_t offset)
{
	int err;
	u8_t const op_and_addr[] = {
		[0] = opcode,
		[1] = (offset >> 16) & 0xFF,
		[2] = (offset >> 8)  & 0xFF,
		[3] = (offset >> 0)  & 0xFF,
	};
	const struct spi_buf tx_buf[] = {
		{
			.buf = (void *)&op_and_addr,
			.len = sizeof(op_and_addr),
		}
	};
	DEF_BUF_SET(tx_buf_set, tx_buf);

	err = spi_write(get_dev_data(dev)->spi,
			&get_dev_config(dev)->spi_cfg,
			&tx_buf_set);
	if (err != 0) {
		LOG_ERR("SPI transaction failed with code: %d", err);
	} else {
		err = wait_until_ready(dev);
	}

	return (err != 0) ? -EIO : 0;
}

static int spi_flash_at45d_erase(struct device *dev, off_t offset, size_t size)
{
	const struct spi_flash_at45d_config *cfg = get_dev_config(dev);

	if (!is_valid_request(offset, size, cfg->chip_size)) {
		return -ENODEV;
	}

	acquire(dev);

	int err;
	if (size == cfg->chip_size) {
		err = perform_chip_erase(dev);
	} else {
		while (size) {
			if (is_erase_possible(AT45D_SECTOR_SIZE,
					      offset, size)) {
				err = perform_erase_op(dev, CMD_SECTOR_ERASE,
						       offset);
				offset += AT45D_SECTOR_SIZE;
				size   -= AT45D_SECTOR_SIZE;
			} else if (is_erase_possible(cfg->block_size,
						     offset, size)) {
				err = perform_erase_op(dev, CMD_BLOCK_ERASE,
						       offset);
				offset += cfg->block_size;
				size   -= cfg->block_size;
			} else if (is_erase_possible(cfg->page_size,
						     offset, size)) {
				err = perform_erase_op(dev, CMD_PAGE_ERASE,
						       offset);
				offset += cfg->page_size;
				size   -= cfg->page_size;
			} else {
				LOG_ERR("Unsupported erase request: "
					"size %zu at 0x%lx",
					size, (long)offset);
				err = -EINVAL;
			}

			if (err != 0) {
				break;
			}
		}
	}

	release(dev);

	return err;
}

static int spi_flash_at45d_write_protection(struct device *dev, bool enable)
{
	/* TODO - not supported yet. */
	return -ENOTSUP;
}

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
static void spi_flash_at45d_pages_layout(
				struct device *dev,
				const struct flash_pages_layout **layout,
				size_t *layout_size)
{
	const struct spi_flash_at45d_config *dev_cfg = get_dev_config(dev);

	*layout = &dev_cfg->pages_layout;
	*layout_size = 1;
}
#endif /* CONFIG_FLASH_PAGE_LAYOUT */

static int spi_flash_at45d_init(struct device *dev)
{
	struct spi_flash_at45d_data *dev_data = get_dev_data(dev);
	const struct spi_flash_at45d_config *dev_cfg = get_dev_config(dev);
	int err;

	dev_data->spi = device_get_binding(dev_cfg->spi_bus);
	if (!dev_data->spi) {
		LOG_ERR("Cannot find %s", dev_cfg->spi_bus);
		return -ENODEV;
	}

#ifdef DT_INST_0_ADESTO_AT45D_CS_GPIOS_CONTROLLER
	dev_data->spi_cs.gpio_dev =
		device_get_binding(DT_INST_0_ADESTO_AT45D_CS_GPIOS_CONTROLLER);
	if (!dev_data->spi_cs.gpio_dev) {
		LOG_ERR("Cannot find %s",
			DT_INST_0_ADESTO_AT45D_CS_GPIOS_CONTROLLER);
		return -ENODEV;
	}

	dev_data->spi_cs.gpio_pin = DT_INST_0_ADESTO_AT45D_CS_GPIOS_PIN;
	dev_data->spi_cs.delay = 0;
#endif /* DT_INST_0_ADESTO_AT45D_GPIOS_CONTROLLER */

	acquire(dev);

	err = check_jedec_id(dev);
	if (err == 0)
	{
		err = configure_page_size(dev);
	}

	release(dev);

	return err;
}

static const struct flash_driver_api spi_flash_at45d_api = {
	.read = spi_flash_at45d_read,
	.write = spi_flash_at45d_write,
	.erase = spi_flash_at45d_erase,
	.write_protection = spi_flash_at45d_write_protection,
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	.page_layout = spi_flash_at45d_pages_layout,
#endif
	.write_block_size = 1,
};

static struct spi_flash_at45d_data inst_0_data = {
	.lock = Z_SEM_INITIALIZER(inst_0_data.lock, 1, 1),
};

enum {
	INST_0_BYTES = (DT_INST_0_ADESTO_AT45D_SIZE / 8),
	INST_0_PAGES = INST_0_BYTES / DT_INST_0_ADESTO_AT45D_PAGE_SIZE,
};

static const struct spi_flash_at45d_config inst_0_config = {
	.spi_bus = DT_INST_0_ADESTO_AT45D_BUS_NAME,
	.spi_cfg = {
		.frequency = DT_INST_0_ADESTO_AT45D_SPI_MAX_FREQUENCY,
		.operation = SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB |
			     SPI_WORD_SET(8) | SPI_LINES_SINGLE,
		.slave = DT_INST_0_ADESTO_AT45D_BASE_ADDRESS,
		.cs = &inst_0_data.spi_cs,
	},
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	.pages_layout = {
		.pages_count = INST_0_PAGES,
		.pages_size  = DT_INST_0_ADESTO_AT45D_PAGE_SIZE,
	},
#endif	
	.chip_size  = INST_0_BYTES,
	.block_size = DT_INST_0_ADESTO_AT45D_BLOCK_SIZE,
	.page_size  = DT_INST_0_ADESTO_AT45D_PAGE_SIZE,
	.jedec_id   = DT_INST_0_ADESTO_AT45D_JEDEC_ID,
};

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
BUILD_ASSERT_MSG(
	(INST_0_PAGES * DT_INST_0_ADESTO_AT45D_PAGE_SIZE) == INST_0_BYTES,
	"Page size specified for instance 0 of adesto,at45d is not compatible "
	"with its total size");
#endif

DEVICE_AND_API_INIT(inst_0, DT_INST_0_ADESTO_AT45D_LABEL,
		    spi_flash_at45d_init, &inst_0_data, &inst_0_config,
		    POST_KERNEL, CONFIG_SPI_FLASH_AT45D_INIT_PRIORITY,
		    &spi_flash_at45d_api);
