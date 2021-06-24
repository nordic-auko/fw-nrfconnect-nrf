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

#define HX711_SCALING_FACTOR 0.022810635f

enum hx711_sample_state {
	HX711_SAMPLE_STATE_CH_A_GAIN_128, /* 25 pulses */
	HX711_SAMPLE_STATE_CH_B_GAIN_32,  /* 26 pulses */
	HX711_SAMPLE_STATE_CH_A_GAIN_64,  /* 27 pulses */
	HX711_SAMPLE_STATE_INIT,
};

static enum hx711_sample_state sample_state_sequence[] = {
#if IS_ENABLED(CONFIG_PRESS_HX711_SENSOR_CHN_A_ENABLE) && IS_ENABLED(CONFIG_PRESS_HX711_SENSOR_CHN_A_GAIN_128)
	HX711_SAMPLE_STATE_CH_A_GAIN_128,
#endif
#if IS_ENABLED(CONFIG_PRESS_HX711_SENSOR_CHN_A_ENABLE) && IS_ENABLED(CONFIG_PRESS_HX711_SENSOR_CHN_A_GAIN_64)
	HX711_SAMPLE_STATE_CH_A_GAIN_64,
#endif
#if IS_ENABLED(CONFIG_PRESS_HX711_SENSOR_CHN_B_ENABLE)
	HX711_SAMPLE_STATE_CH_B_GAIN_32,
#endif
};

static enum hx711_sample_state sample_state;
static enum hx711_state state;
static uint32_t sample_state_counter;

static int32_t avg_filter_vals[CONFIG_PRESS_HX711_AVG_LENGTH];
static int avg_filter_idx;

static const struct device * spi_dev;
static const struct spi_config spi_config = {
	.frequency = DT_PROP(DT_NODELABEL(spi0), clock_frequency) * 2,
	.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPHA,
	.slave = 0,
	.cs = NULL,
};

static uint8_t spi_tx_buf_25bit[] = {0x00, 0x01, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
static uint8_t spi_tx_buf_26bit[] = {0x00, 0x05, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
static uint8_t spi_tx_buf_27bit[] = {0x00, 0x15, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
static uint8_t spi_rx_buf[sizeof(spi_tx_buf_27bit)];

static int32_t tare_weight;
static int64_t tare_sum;
static uint32_t tare_count;

static struct spi_buf rx_buf = {
	.buf = spi_rx_buf,
	.len = sizeof(spi_rx_buf),
};

static void hx711_read_timeout(struct k_timer *dummy);
static void hx711_tare_timeout(struct k_timer *dummy);
K_TIMER_DEFINE(hx711_poll_timer, hx711_read_timeout, NULL);
K_TIMER_DEFINE(hx711_tare_timer, hx711_tare_timeout, NULL);

static void hx711_read_work(struct k_work *work);
K_WORK_DEFINE(hx711_poll_work, hx711_read_work);

static void hx711_tare_work_handler(struct k_work *work);
K_WORK_DEFINE(hx711_tare_work, hx711_tare_work_handler);

static bool hx711_value_get(void * tx_buf, size_t tx_buf_len, uint32_t *buf)
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
		return false;
	}

	if (spi_rx_buf[0] & BIT(7)) {
		LOG_INF("Invalid: %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X", spi_rx_buf[0], spi_rx_buf[1], spi_rx_buf[2], spi_rx_buf[3], spi_rx_buf[4], spi_rx_buf[5], spi_rx_buf[6], spi_rx_buf[7]);
		return false;
	}

	uint32_t out, in1, in2;

	in1 = (sys_get_be32(spi_rx_buf));
	in2 = (sys_get_be32(&spi_rx_buf[4]));
	out = 0;


	for (int i = 0; i < 32; i += 2) {
		if (in1 & BIT(i)) {
			out |= BIT(i/2 + 16);
		}
	}
	for (int i = 0; i < 32; i += 2) {
		if (in2 & BIT(i)) {
			out |= BIT(i/2);
		}
	}

	if (buf) {
		*buf = out;
	}
	
	return true;
}

static int32_t get_filtered_measurement(int32_t new_measurement)
{
	int64_t sum = 0;

	avg_filter_vals[avg_filter_idx++] = new_measurement;
	if (avg_filter_idx >= ARRAY_SIZE(avg_filter_vals)) {
		avg_filter_idx = 0;
	}

	for (int i = 0; i < ARRAY_SIZE(avg_filter_vals); ++i)
	{
		sum += avg_filter_vals[i];
	}

	return (sum / ARRAY_SIZE(avg_filter_vals));
}

static int32_t get_scaled_measurement(int32_t measurement)
{
	float meas_f = (float) measurement;

	meas_f *= HX711_SCALING_FACTOR;

	return (int32_t) meas_f;
}

static void hx711_read_work(struct k_work *work)
{
	static int32_t prev_scaled_value;
	enum hx711_sample_state next_state;
	enum hx711_channel channel;
	int32_t raw_value;
	int32_t scaled_value;
	void * tx_buf;

	next_state = sample_state_sequence[(++sample_state_counter) % ARRAY_SIZE(sample_state_sequence)];

	switch (next_state) {
	case HX711_SAMPLE_STATE_CH_A_GAIN_128:
		tx_buf = spi_tx_buf_25bit;
		break;
	case HX711_SAMPLE_STATE_CH_B_GAIN_32:
		tx_buf = spi_tx_buf_26bit;
		break;
	case HX711_SAMPLE_STATE_CH_A_GAIN_64:
		tx_buf = spi_tx_buf_27bit;
		break;
	default:
		return;
		break;
	}

	bool valid;

	valid = hx711_value_get(tx_buf, sizeof(spi_tx_buf_25bit), &raw_value);
	if (!valid) {
		return;
	}

	switch (sample_state) {
	case HX711_SAMPLE_STATE_CH_A_GAIN_128:
		scaled_value = raw_value;
		channel = HX711_CHANNEL_A;
		break;
	case HX711_SAMPLE_STATE_CH_B_GAIN_32:
		scaled_value = raw_value * 4;
		channel = HX711_CHANNEL_B;
		break;
	case HX711_SAMPLE_STATE_CH_A_GAIN_64:
		scaled_value = raw_value * 2;
		channel = HX711_CHANNEL_A;
		break;
	default:
		return;
		break;
	}

	sample_state = next_state;

	if (state == HX711_STATE_FIND_TARE_WEIGHT) {
		tare_count += 1;
		tare_sum += scaled_value;
		// LOG_INF("Tare sum: %lld", tare_sum);
	} else if (prev_scaled_value != scaled_value) {
		struct hx711_data_event *event;
		int32_t meas;

		meas = get_filtered_measurement(scaled_value - tare_weight);
		meas = get_scaled_measurement(meas);

		event = new_hx711_data_event();
		event->channel = channel;
		// LOG_INF("Scaled value: %d", scaled_value);		
		event->value = meas;
		EVENT_SUBMIT(event);
	}

	prev_scaled_value = scaled_value;
}

static void hx711_tare_work_handler(struct k_work *work)
{
	state = HX711_STATE_RUNNING;

	tare_weight = tare_sum / tare_count;
	// LOG_INF("Tare weight: %lld / %d = %d", tare_sum, tare_count, tare_weight);
}

static void hx711_read_timeout(struct k_timer *dummy)
{
	k_work_submit(&hx711_poll_work);
}

static void hx711_tare_timeout(struct k_timer *dummy)
{
	k_work_submit(&hx711_tare_work);
}

static bool event_handler(const struct event_header *eh)
{
	if (is_module_state_event(eh)) {
		const struct module_state_event *event =
			cast_module_state_event(eh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			void *tx_buf;
			spi_dev = device_get_binding(CONFIG_PRESS_HX711_SENSOR_SPI_DEV);
			sample_state = HX711_SAMPLE_STATE_INIT;
			if (!spi_dev) {
				LOG_ERR("SPI device not found");
			}

			sample_state = sample_state_sequence[0];

			switch (sample_state) {
			case HX711_SAMPLE_STATE_CH_A_GAIN_128:
				tx_buf = spi_tx_buf_25bit;
				break;
			case HX711_SAMPLE_STATE_CH_B_GAIN_32:
				tx_buf = spi_tx_buf_26bit;
				break;
			case HX711_SAMPLE_STATE_CH_A_GAIN_64:
				tx_buf = spi_tx_buf_27bit;
				break;
			default:
				break;
			}

			(void) hx711_value_get(tx_buf, sizeof(spi_tx_buf_25bit), NULL);
			__ASSERT_NO_MSG(spi_dev != NULL);

			state = HX711_STATE_READY;

			struct hx711_state_event *state_event;
			state_event = new_hx711_state_event();
			state_event->state = state;
			EVENT_SUBMIT(state_event);
		}

		return false;
	}

	if (is_hx711_ctrl_event(eh)) {
		const struct hx711_ctrl_event *event =
			cast_hx711_ctrl_event(eh);

		switch (event->cmd) {
		case HX711_CMD_ENABLE:
			state = HX711_STATE_RUNNING;
			k_timer_start(&hx711_poll_timer, K_MSEC(120), K_MSEC(120));
			break;
		case HX711_CMD_DISABLE:
			state = HX711_STATE_STOPPED;
			k_timer_stop(&hx711_poll_timer);
			break;
		case HX711_CMD_SET_TARE_WEIGHT:
			state = HX711_STATE_FIND_TARE_WEIGHT;
			tare_sum = 0;
			tare_count = 0;
			tare_weight = 0;
			k_timer_start(&hx711_poll_timer, K_MSEC(150), K_MSEC(150));
			k_timer_start(&hx711_tare_timer, K_SECONDS(CONFIG_PRESS_HX711_TARE_DURATION), K_NO_WAIT);
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
