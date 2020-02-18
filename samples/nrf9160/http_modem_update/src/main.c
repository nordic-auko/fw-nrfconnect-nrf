/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */
#include <zephyr.h>
#include <drivers/gpio.h>
#include <drivers/flash.h>
#include <bsd.h>
#include <lte_lc.h>
#include <at_cmd.h>
#include <at_notif.h>
#include <net/bsdlib.h>
#include <net/fota_download.h>
#include <dfu/mcuboot.h>
#include "dfu_target_flash.h"

#include "mfu.h"

#define LED_PORT	DT_ALIAS_LED0_GPIOS_CONTROLLER

static struct		device *gpiob;
static struct		gpio_callback gpio_cb;
static struct k_work	fota_work;

static u32_t update_begin;
static u32_t update_end;

/**@brief Recoverable BSD library error. */
void bsd_recoverable_error_handler(uint32_t err)
{
	printk("bsdlib recoverable error: %u\n", err);
}

/**@brief Start transfer of the file. */
static void app_dfu_transfer_start(struct k_work *unused)
{
	int retval;

	mfu_update_verify_stream_init();

	retval = fota_download_start(CONFIG_DOWNLOAD_HOST,
				     CONFIG_DOWNLOAD_FILE);
	if (retval != 0) {
		/* Re-enable button callback */
		gpio_pin_enable_callback(gpiob, DT_ALIAS_SW0_GPIOS_PIN);

		printk("fota_download_start() failed, err %d\n",
			retval);
	}
}

/**@brief Turn on LED0 and LED1 if CONFIG_APPLICATION_VERSION
 * is 2 and LED0 otherwise.
 */
static int led_app_version(void)
{
	struct device *dev;

	dev = device_get_binding(LED_PORT);
	if (dev == 0) {
		printk("Nordic nRF GPIO driver was not found!\n");
		return 1;
	}

	gpio_pin_configure(dev, DT_ALIAS_LED0_GPIOS_PIN, GPIO_DIR_OUT);
	gpio_pin_write(dev, DT_ALIAS_LED0_GPIOS_PIN, 1);

#if CONFIG_APPLICATION_VERSION == 2
	gpio_pin_configure(dev, DT_ALIAS_LED1_GPIOS_PIN, GPIO_DIR_OUT);
	gpio_pin_write(dev, DT_ALIAS_LED1_GPIOS_PIN, 1);
#endif
	return 0;
}

void dfu_button_pressed(struct device *gpiob, struct gpio_callback *cb,
			u32_t pins)
{
	k_work_submit(&fota_work);
	gpio_pin_disable_callback(gpiob, DT_ALIAS_SW0_GPIOS_PIN);

	update_begin = k_cycle_get_32();
}

static int dfu_button_init(void)
{
	int err;

	gpiob = device_get_binding(DT_ALIAS_SW0_GPIOS_CONTROLLER);
	if (gpiob == 0) {
		printk("Nordic nRF GPIO driver was not found!\n");
		return 1;
	}
	err = gpio_pin_configure(gpiob, DT_ALIAS_SW0_GPIOS_PIN,
				 GPIO_DIR_IN | GPIO_INT | GPIO_PUD_PULL_UP |
					 GPIO_INT_EDGE | GPIO_INT_ACTIVE_LOW);
	if (err == 0) {
		gpio_init_callback(&gpio_cb, dfu_button_pressed,
			BIT(DT_ALIAS_SW0_GPIOS_PIN));
		err = gpio_add_callback(gpiob, &gpio_cb);
	}
	if (err == 0) {
		err = gpio_pin_enable_callback(gpiob, DT_ALIAS_SW0_GPIOS_PIN);
	}
	if (err != 0) {
		printk("Unable to configure SW0 GPIO pin!\n");
		return 1;
	}
	return 0;
}


void fota_dl_handler(const struct fota_download_evt *evt)
{
	u32_t time_diff;
	int err;

	switch (evt->id) {
	case FOTA_DOWNLOAD_EVT_ERROR:
		printk("Received error from fota_download\n");
		gpio_pin_enable_callback(gpiob, DT_ALIAS_SW0_GPIOS_PIN);
		break;
	case FOTA_DOWNLOAD_EVT_PROGRESS:
		// printk("FOTA_DOWNLOAD_EVT_PROGRESS: %d\n", evt->offset);
		err = mfu_update_verify_stream_process(evt->offset);
		if (err) {
			printk("ERROR: mfu_update_verify_stream_process: %d\n", err);
		}
		break;
	case FOTA_DOWNLOAD_EVT_FINISHED:
		/* Re-enable button callback */
		printk("FOTA DOWNLOAD FINISHED\n");
		gpio_pin_enable_callback(gpiob, DT_ALIAS_SW0_GPIOS_PIN);

		update_end = k_cycle_get_32();
		time_diff = update_begin < update_end ?
			(update_end - update_begin) :
			(UINT32_MAX - update_begin + update_end);
		printk("Download took %d ms\n",
			(time_diff * 1000) / CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC);

		err = mfu_update_verify_stream_finalize();
		if (err) {
			printk("ERROR: mfu_update_verify_stream_finalize: %d\n", err);
		} else {
			err = mfu_update_available_set();
			if (err) {
				printk("mfu_update_available_set: %d\n", err);
			}
		}

		break;

	default:
		break;
	}
}

/**@brief Configures modem to provide LTE link.
 *
 * Blocks until link is successfully established.
 */
static void modem_configure(void)
{
#if defined(CONFIG_LTE_LINK_CONTROL)
	BUILD_ASSERT_MSG(!IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT),
			"This sample does not support auto init and connect");
	int err;

	err = at_notif_init();
	__ASSERT(err == 0, "AT Notify could not be initialized.");
	err = at_cmd_init();
	__ASSERT(err == 0, "AT CMD could not be established.");
	printk("LTE Link Connecting ...\n");
	err = lte_lc_init_and_connect();
	__ASSERT(err == 0, "LTE link could not be established.");
	// if (err) {
	// 	printk("LTE link could not be established.\n");
	// }
	printk("LTE Link Connected!\n");
#endif
}

static int application_init(void)
{
	int err;

	k_work_init(&fota_work, app_dfu_transfer_start);

	err = dfu_button_init();
	if (err != 0) {
		return err;
	}

	err = led_app_version();
	if (err != 0) {
		return err;
	}

	err = fota_download_init(fota_dl_handler);
	if (err != 0) {
		return err;
	}

	return 0;
}

#define FLASH_DEVICE  DT_INST_0_ADESTO_AT45D_LABEL
#define FLASH_DEVICE_ADDR_OFFSET 0
// #define FLASH_DEVICE_ADDR_OFFSET (FLASH_DEVICE_SIZE_BYTES / 2)
#define FLASH_DEVICE_SIZE_BYTES (DT_INST_0_ADESTO_AT45D_SIZE / 8)

// Addr offset 0 contains full update
// Addr offset FLASH_DEVICE_SIZE_BYTES / 2 contains small test file

#define FLASH_DEVICE_FULL_TEST 0

#if FLASH_DEVICE_FULL_TEST
static void serial_flash_test(void)
{
	printk("DataFlash test app on %s\n", CONFIG_BOARD);
	static u8_t write_buf[512];
	static u8_t read_buf[512];

	struct device *flash_dev;
	int i;
	int err;

	flash_dev = device_get_binding(FLASH_DEVICE);
	if (!flash_dev) {
		printk("Device %s not found!\n", FLASH_DEVICE);
		return;
	}

	for (i = 0; i < sizeof(write_buf); ++i) {
		write_buf[i] = (u8_t)i;
	}

	const struct flash_pages_layout *layout;
	const struct flash_driver_api *api = flash_dev->driver_api;
	size_t layout_size;
	api->page_layout(flash_dev, &layout, &layout_size);

	if (!layout) {
		printk("layout is NULL\n");
		return;
	}

	if (layout->pages_size != sizeof(write_buf)) {
		printk("ERROR: Page size mismatch\n");
		return;
	}

	printk("Erasing the entire flash... ");

	u32_t duration;
	u32_t timestamp_start, timestamp_end;
	u32_t time_diff;

	if (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC != 32768) {
		printk("CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC: %d\n", CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC);
		return;
	}

	timestamp_start = k_cycle_get_32();

	for (int j = 0; j < ((layout->pages_count * 512) / DT_INST_0_ADESTO_AT45D_BLOCK_SIZE); ++j) {
		err = flash_erase(flash_dev, j * DT_INST_0_ADESTO_AT45D_BLOCK_SIZE, DT_INST_0_ADESTO_AT45D_BLOCK_SIZE);
		if (err != 0) {
			printk("FAILED to erase block %d\n", j);
		}
	}

	timestamp_end = k_cycle_get_32();
	time_diff = timestamp_start < timestamp_end ?
		(timestamp_end - timestamp_start) :
		(UINT32_MAX - timestamp_start + timestamp_end);

	printk("Erasing took %d ticks... \n", time_diff);

	printk("Writing the entire flash...\n");

	u32_t byte_count;

	byte_count = 0;
	timestamp_start = k_cycle_get_32();
	for (int j = 0; j < layout->pages_count; ++j) {
		err = flash_write(flash_dev, j * 512,
				write_buf,  sizeof(write_buf));
		if (err != 0) {
			printk("FAILED\n");
		} else {
			byte_count += sizeof(write_buf);
		}
	}

	timestamp_end = k_cycle_get_32();
	time_diff = timestamp_start < timestamp_end ?
		(timestamp_end - timestamp_start) :
		(UINT32_MAX - timestamp_start + timestamp_end);
	printk("Writing %d bytes took %d ticks... \n", byte_count, time_diff);

	printk("Reading the entire flash...\n");

	byte_count = 0;
	duration = 0;
	for (int j = 0; j < layout->pages_count; ++j) {
		timestamp_start = k_cycle_get_32();
		err = flash_read(flash_dev, j * 512,
			 read_buf, sizeof(read_buf));
		if (err != 0) {
			printk("FAILED\n");
		} else {
			byte_count += sizeof(read_buf);
		}
		timestamp_end = k_cycle_get_32();
		time_diff = timestamp_start < timestamp_end ?
		(timestamp_end - timestamp_start) :
		(UINT32_MAX - timestamp_start + timestamp_end);
		duration += time_diff;

		for (int k = 0; k < sizeof(read_buf); ++k) {
			if (read_buf[k] != write_buf[k]) {
				printk("\nERROR at offset %d: "
					"expected %02X, read %02X\n",
					j * 512 + k, write_buf[k], read_buf[k]);
			}
		}
	}
	printk("Read %d bytes in %d ticks...\n", byte_count, duration);
}
#endif

void main(void)
{
	int err;

#if FLASH_DEVICE_FULL_TEST
	serial_flash_test();
#endif

	struct device *flash_dev;

	flash_dev = device_get_binding(FLASH_DEVICE);

	static u8_t dfu_flash_buf[512];

	err = dfu_target_flash_cfg(FLASH_DEVICE, FLASH_DEVICE_ADDR_OFFSET, FLASH_DEVICE_SIZE_BYTES,
			 dfu_flash_buf, sizeof(dfu_flash_buf));
	if (err) {
		printk("dfu_target_flash_cfg: %d\n", err);
	}


	err = mfu_init(flash_dev, FLASH_DEVICE_ADDR_OFFSET);
	if (err) {
		printk("mfu_init(): %d\n", err);
	}

#if 0
	bool update_available = mfu_update_available_get();

	if (update_available) {
		printk("Update marked as available\n");
		err = mfu_update_apply();
		if (err) {
			printk("mfu_update_apply: %d\n", err);
		}
	} else {
		printk("No update marked as available\n");
	}
#else
	err = mfu_update_verify_integrity();
	if (err) {
		printk("Integrity check NOT OK\n");
	} else {
		printk("Integrity check OK\n");
		err = mfu_update_apply();
		if (err) {
			printk("mfu_update_apply: %d\n", err);
		}
	}
#endif

	while(true)
	{
		
	}

	printk("Initializing bsdlib\n");
	err = bsdlib_init();
	switch (err) {
	case MODEM_DFU_RESULT_OK:
		printk("Modem firmware update successful!\n");
		printk("Modem will run the new firmware after reboot\n");
		k_thread_suspend(k_current_get());
		break;
	case MODEM_DFU_RESULT_UUID_ERROR:
	case MODEM_DFU_RESULT_AUTH_ERROR:
		printk("Modem firmware update failed\n");
		printk("Modem will run non-updated firmware on reboot.\n");
		break;
	case MODEM_DFU_RESULT_HARDWARE_ERROR:
	case MODEM_DFU_RESULT_INTERNAL_ERROR:
		printk("Modem firmware update failed\n");
		printk("Fatal error.\n");
		break;
	case -1:
		printk("Could not initialize bsdlib.\n");
		printk("Fatal error.\n");
		return;
	default:
		break;
	}
	printk("Initialized bsdlib\n");

	modem_configure();

	boot_write_img_confirmed();

	err = application_init();
	if (err != 0) {
		return;
	}

	printk("Press Button 1 to start the FOTA download\n");
}
