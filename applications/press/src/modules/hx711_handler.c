/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <kernel.h>

#include <drivers/spi.h>
#include <sys/byteorder.h>

#define MODULE hx711_sensor
#include "module_state_event.h"
#include "hx711_ctrl_event.h"
#include "hx711_data_event.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_PRESS_HX711_SENSOR_LOG_LEVEL);

#if !IS_ENABLED(CONFIG_PRESS_HX711_SENSOR_CHN_A_ENABLE) && !IS_ENABLED(CONFIG_PRESS_HX711_SENSOR_CHN_B_ENABLE)
#error No channels enabled
#endif

enum hx711_state {
	HX711_STATE_CH_A_GAIN_128, /* 25 pulses */
	HX711_STATE_CH_B_GAIN_32,  /* 26 pulses */
	HX711_STATE_CH_A_GAIN_64,  /* 27 pulses */
	HX711_STATE_INIT,
};

static enum hx711_state state_sequence[] = {
#if IS_ENABLED(CONFIG_PRESS_HX711_SENSOR_CHN_A_ENABLE) && IS_ENABLED(CONFIG_PRESS_HX711_SENSOR_CHN_A_GAIN_128)
	HX711_STATE_CH_A_GAIN_128,
#endif
#if IS_ENABLED(CONFIG_PRESS_HX711_SENSOR_CHN_A_ENABLE) && IS_ENABLED(CONFIG_PRESS_HX711_SENSOR_CHN_A_GAIN_64)
	HX711_STATE_CH_A_GAIN_64,
#endif
#if IS_ENABLED(CONFIG_PRESS_HX711_SENSOR_CHN_B_ENABLE)
	HX711_STATE_CH_B_GAIN_32,
#endif
};

static enum hx711_state state;
static uint32_t state_counter;

static const struct device * spi_dev;
static const struct spi_config spi_config = {
	.frequency = DT_PROP(DT_NODELABEL(spi0), clock_frequency) * 2,
	.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
	.slave = 0,
	.cs = NULL,
};

static uint8_t spi_tx_buf_25bit[] = {0x00, 0x01, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
static uint8_t spi_tx_buf_26bit[] = {0x00, 0x05, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
static uint8_t spi_tx_buf_27bit[] = {0x00, 0x15, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
static uint8_t spi_rx_buf[sizeof(spi_tx_buf_27bit)];

static struct spi_buf rx_buf = {
	.buf = spi_rx_buf,
	.len = sizeof(spi_rx_buf),
};

static void hx711_read_timeout(struct k_timer *dummy);
K_TIMER_DEFINE(hx711_poll_timer, hx711_read_timeout, NULL);

static void hx711_read_work(struct k_work *work);
K_WORK_DEFINE(hx711_poll_work, hx711_read_work);

static uint8_t * skip_odd_bits(const uint8_t * in_buf, uint8_t * out_buf, size_t in_size)
{
	memset(out_buf, 0, in_size / 2);

	for (int i = 0, j = 0; i < (in_size * 8); i += 2, j += 1) {
		int byte_idx = i / 8;
		int bit_idx = i - (byte_idx * 8);

		if (in_buf[byte_idx] & BIT(bit_idx)) {
			int byte_idx_out = j / 8;
			int bit_idx_out = j - (byte_idx_out * 8);

			out_buf[byte_idx_out] |= BIT(bit_idx_out);
		}
	}

	return out_buf;
}

static uint32_t hx711_value_get(void * tx_buf, size_t tx_buf_len)
{
	int err;
	struct spi_buf spi_tx_buf = {
		.buf = tx_buf,
		.len = tx_buf_len,
	};

	struct spi_buf_set tx_buffers = {
		.buffers = &spi_tx_buf,
		.count = 1,
	};

	struct spi_buf_set rx_buffers = {
		.buffers = &rx_buf,
		.count = 1,
	};

	err = spi_transceive(spi_dev, &spi_config, &tx_buffers, &rx_buffers);
	if (err) {
		LOG_ERR("spi_transceive: %d", err);
		return 0;
	}

	uint8_t trimmed_buf[sizeof(spi_rx_buf) / 2];

	skip_odd_bits(spi_rx_buf, trimmed_buf, sizeof(spi_rx_buf));

	return sys_get_be24(trimmed_buf);
}

static void hx711_read_work(struct k_work *work)
{
	static int32_t prev_scaled_value;
	enum hx711_state next_state;
	enum hx711_channel channel;
	int32_t raw_value;
	int32_t scaled_value;
	void * tx_buf;

	next_state = state_sequence[(++state_counter) % ARRAY_SIZE(state_sequence)];

	switch (next_state) {
	case HX711_STATE_CH_A_GAIN_128:
		tx_buf = spi_tx_buf_25bit;
		break;
	case HX711_STATE_CH_B_GAIN_32:
		tx_buf = spi_tx_buf_26bit;
		break;
	case HX711_STATE_CH_A_GAIN_64:
		tx_buf = spi_tx_buf_27bit;
		break;
	default:
		return;
		break;
	}

	raw_value = hx711_value_get(tx_buf, sizeof(spi_tx_buf_25bit));

	switch (state) {
	case HX711_STATE_CH_A_GAIN_128:
		scaled_value = raw_value;
		channel = HX711_CHANNEL_A;
		break;
	case HX711_STATE_CH_B_GAIN_32:
		scaled_value = raw_value * 4;
		channel = HX711_CHANNEL_B;
		break;
	case HX711_STATE_CH_A_GAIN_64:
		scaled_value = raw_value * 2;
		channel = HX711_CHANNEL_A;
		break;
	default:
		return;
		break;
	}

	state = next_state;

	if (prev_scaled_value != scaled_value) {
		struct hx711_data_event *event;
		event = new_hx711_data_event();
		event->channel = channel;
		event->value = scaled_value;
		EVENT_SUBMIT(event);
	}

	prev_scaled_value = scaled_value;
}

static void hx711_read_timeout(struct k_timer *dummy)
{
	k_work_submit(&hx711_poll_work);
}

static bool event_handler(const struct event_header *eh)
{
	if (is_module_state_event(eh)) {
		const struct module_state_event *event =
			cast_module_state_event(eh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			void *tx_buf;
			spi_dev = device_get_binding(CONFIG_PRESS_HX711_SENSOR_SPI_DEV);
			state = HX711_STATE_INIT;
			if (!spi_dev) {
				LOG_ERR("SPI device not found");
			}

			state = state_sequence[0];

			switch (state) {
			case HX711_STATE_CH_A_GAIN_128:
				tx_buf = spi_tx_buf_25bit;
				break;
			case HX711_STATE_CH_B_GAIN_32:
				tx_buf = spi_tx_buf_26bit;
				break;
			case HX711_STATE_CH_A_GAIN_64:
				tx_buf = spi_tx_buf_27bit;
				break;
			default:
				break;
			}
			(void) hx711_value_get(tx_buf, sizeof(spi_tx_buf_25bit));
			__ASSERT_NO_MSG(spi_dev != NULL);
		}

		return false;
	}

	if (is_hx711_ctrl_event(eh)) {
		const struct hx711_ctrl_event *event =
			cast_hx711_ctrl_event(eh);

		switch (event->cmd) {
		case HX711_CMD_ENABLE:
			k_timer_start(&hx711_poll_timer, K_MSEC(12), K_MSEC(12));
			break;
		case HX711_CMD_DISABLE:
			k_timer_stop(&hx711_poll_timer);
			break;
		default:
			__ASSERT_NO_MSG(false);
			break;
		}

		return false;
	}

	/* If event is unhandled, unsubscribe. */
	__ASSERT_NO_MSG(false);

	return false;
}

EVENT_LISTENER(MODULE, event_handler);
EVENT_SUBSCRIBE(MODULE, module_state_event);
EVENT_SUBSCRIBE(MODULE, hx711_ctrl_event);
