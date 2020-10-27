/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <kernel.h>

#include <drivers/adc.h>
#include <drivers/gpio.h>

#define MODULE potmeter_handler
#include "module_state_event.h"
#include "potmeter_ctrl_event.h"
#include "potmeter_data_event.h"

#include "potmeter_pin_config.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_PRESS_POTMETER_LOG_LEVEL);

static const struct device * adc_dev;

static void potmeter_sample_timer_func(struct k_timer *dummy);
K_TIMER_DEFINE(potmeter_sample_timer, potmeter_sample_timer_func, NULL);

static void potmeter_sample_func(struct k_work *work);
K_WORK_DEFINE(potmeter_sample_work, potmeter_sample_func);

static void potmeter_vdd_set(bool on)
{
	const struct device *gpio_dev = device_get_binding(port_map[potmeter_vdd.port]);

	__ASSERT_NO_MSG(gpio_dev != NULL);

	gpio_pin_set(gpio_dev, potmeter_vdd.pin, on ? 1 : 0);
}

static void configure_gpio(void)
{
	const struct device *gpio_dev = device_get_binding(port_map[potmeter_vdd.port]);
	int err;

	if (!gpio_dev) {
		LOG_ERR("Cannot get GPIO device");
		return;
	}

	err = gpio_pin_configure(gpio_dev, potmeter_vdd.pin, GPIO_OUTPUT_ACTIVE);
	if (err) {
		LOG_ERR("Failed to init gpio: %d", err);
		return;
	}

	potmeter_vdd_set(false);

	// LOG_INF("Set %s:%d high", CONFIG_POTMETER_VDD_GPIO_DEV, CONFIG_POTMETER_VDD_GPIO_PIN);
}

static void configure_adc(void)
{
	int16_t adc_meas;
	int err;

	adc_dev = device_get_binding("ADC_0");

	if (!adc_dev) {
		LOG_ERR("Cannot get ADC device");
		return;
	}

	struct adc_channel_cfg channel_cfg = {
		.gain = ADC_GAIN_1_6,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME_DEFAULT,
		.channel_id = 0,
		.differential = 0,
		.input_positive = potmeter_ain,
	};

	struct adc_sequence sequence = {
		.options = NULL,
		.channels = BIT(0),
		.buffer = &adc_meas,
		.buffer_size = sizeof(adc_meas),
		.resolution = 10,
		.oversampling = 0,
		.calibrate = true,
	};

	err = adc_channel_setup(adc_dev, &channel_cfg);
	if (err) {
		LOG_ERR("Failed to setup channel: %d", err);
		return;
	}

	err = adc_read(adc_dev, &sequence);
	if (err) {
		LOG_ERR("Failed initial read: %d", err);
		return;
	}
}

static int16_t adc_sample_get(const struct device * adc_dev)
{
	static int16_t adc_meas[1];
	// static int32_t adc_meas_mv;
	int err;

	struct adc_sequence sequence = {
		.options = NULL,
		.channels = BIT(0),
		.buffer = adc_meas,
		.buffer_size = sizeof(adc_meas),
		.resolution = 10,
		.oversampling = 0,
		.calibrate = false,
	};

	potmeter_vdd_set(true);

	err = adc_read(adc_dev, &sequence);
	if (err) {
		LOG_ERR("Failed read read: %d", err);
		return 0;
	}

	potmeter_vdd_set(false);

	return adc_meas[0];
}

static uint32_t potmeter_step_get(int16_t meas_raw)
{
	static struct {
		int16_t vals[8];
		int idx;
	} rolling_avg;
	const int32_t step_size = (CONFIG_POTMETER_VDD_VOLTAGE / CONFIG_POTMETER_STEP_COUNT);
	int32_t meas_mv;
	int32_t sum;
	int err;

	rolling_avg.vals[rolling_avg.idx++] = meas_raw;
	if (rolling_avg.idx >= ARRAY_SIZE(rolling_avg.vals)) {
		rolling_avg.idx = 0;
	}
	sum = 0;
	for (int i = 0; i < ARRAY_SIZE(rolling_avg.vals); ++i) {
		sum += rolling_avg.vals[i];
	}

	meas_mv = sum / ARRAY_SIZE(rolling_avg.vals);

	err = adc_raw_to_millivolts(600, ADC_GAIN_1_6, 10, &meas_mv);
	if (err) {
		LOG_ERR("Conversion failed: %d", err);
		return 0;
	}

	if (meas_mv < 0) {
		meas_mv = 0;
	}

	return (meas_mv / step_size);
}

static void potmeter_sample_func(struct k_work *work)
{
	static uint32_t meas_step_prev;
	uint32_t meas_step;
	int16_t meas_raw;

	meas_raw = adc_sample_get(adc_dev);
	meas_step = potmeter_step_get(meas_raw);
	if (meas_step == meas_step_prev) {
		return;
	}
	// LOG_INF("Step: %d", potmeter_step_get(meas_mv));

	struct potmeter_data_event *event;
	event = new_potmeter_data_event();
	event->value = meas_step;
	EVENT_SUBMIT(event);

	meas_step_prev = meas_step;
}

static void potmeter_sample_timer_func(struct k_timer *dummy)
{
	k_work_submit(&potmeter_sample_work);
}

static bool event_handler(const struct event_header *eh)
{
	if (is_module_state_event(eh)) {
		const struct module_state_event *event =
			cast_module_state_event(eh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			configure_gpio();
			configure_adc();
		}

		return false;
	}

	if (is_potmeter_ctrl_event(eh)) {
		const struct potmeter_ctrl_event *event =
			cast_potmeter_ctrl_event(eh);

		switch (event->cmd) {
		case POTMETER_CMD_ENABLE:
			k_timer_start(&potmeter_sample_timer,
				K_MSEC((1000 / CONFIG_POTMETER_SAMPLE_RATE)),
				K_MSEC((1000 / CONFIG_POTMETER_SAMPLE_RATE)));
			break;
		case POTMETER_CMD_DISABLE:
			k_timer_stop(&potmeter_sample_timer);
			break;

		default:
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
EVENT_SUBSCRIBE(MODULE, potmeter_ctrl_event);
