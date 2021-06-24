/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <kernel.h>

#define MODULE controller
#include "module_state_event.h"
#include "hx711_ctrl_event.h"
#include "hx711_data_event.h"
#include "tb6612fng_ctrl_event.h"
#include "potmeter_ctrl_event.h"
#include "potmeter_data_event.h"

#define THREAD_STACK_SIZE 4096
#define THREAD_PRIORITY K_PRIO_PREEMPT(K_HIGHEST_APPLICATION_THREAD_PRIO)

#include <logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_PRESS_CONTROLLER_LOG_LEVEL);

static K_THREAD_STACK_DEFINE(thread_stack, THREAD_STACK_SIZE);
static struct k_thread thread;

static enum {
	CONTROLLER_STATE_IDLE,   /* Stopped, or in hysteresis */
	CONTROLLER_STATE_RUNNING,/* Moving towards set point */
} controller_state;

static atomic_t weight_set_point;
static atomic_t weight_measurement;
static atomic_t set_point_updated;
static atomic_t measurement_updated;

static void calculate_control_value(int sp, int meas)
{

}

static void controller_thread_func(void)
{
	const int sleep_delay_ms = 1000 / CONFIG_CONTROLLER_RATE;

	while (true) {
		k_msleep(sleep_delay_ms);

		if (!atomic_get(&set_point_updated) || !atomic_get(&measurement_updated)) {
			continue;
		}

		int set_point = atomic_get(&weight_set_point);
		int measurement = atomic_get(&weight_measurement);

		calculate_control_value(set_point, measurement);

		atomic_set(&set_point_updated, false);
		atomic_set(&measurement_updated, false);
	}
}

static bool event_handler(const struct event_header *eh)
{
	if (is_module_state_event(eh)) {
		const struct module_state_event *event =
			cast_module_state_event(eh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			k_thread_create(&thread, thread_stack,
					THREAD_STACK_SIZE,
					(k_thread_entry_t)controller_thread_func,
					NULL, NULL, NULL,
					THREAD_PRIORITY, 0, K_NO_WAIT);
			k_thread_name_set(&thread, MODULE_NAME "_thread");

			controller_state = CONTROLLER_STATE_IDLE;
			
			struct potmeter_ctrl_event *potmeter_event;
			potmeter_event = new_potmeter_ctrl_event();
			potmeter_event->cmd = POTMETER_CMD_ENABLE;
			EVENT_SUBMIT(potmeter_event);

			struct hx711_ctrl_event *hx711_event;
			hx711_event = new_hx711_ctrl_event();
			hx711_event->cmd = HX711_CMD_SET_TARE_WEIGHT;
			EVENT_SUBMIT(hx711_event);
		}

		return false;
	}

	if (is_potmeter_data_event(eh)) {
		const struct potmeter_data_event *event =
			cast_potmeter_data_event(eh);

		// struct tb6612fng_ctrl_event *tb6612fng_event;
		// tb6612fng_event = new_tb6612fng_ctrl_event();
		// tb6612fng_event->mode = TB6612FNG_MODE_CW;
		// tb6612fng_event->duty_cycle = event->value <= 100 ? event->value : 100;
		// EVENT_SUBMIT(tb6612fng_event);

		atomic_set(&weight_set_point, event->value);
		atomic_set(&set_point_updated, true);

		//
		// Motor controller testing
		//
		if (event->value > 40 && event->value <= 50) {
			struct tb6612fng_ctrl_event *tb6612_event;
			tb6612_event = new_tb6612fng_ctrl_event();
			tb6612_event->mode = TB6612FNG_MODE_CW;
			tb6612_event->duty_cycle = 100;
			EVENT_SUBMIT(tb6612_event);
		} else if (event->value > 50 && event->value <= 60) {
			struct tb6612fng_ctrl_event *tb6612_event;
			tb6612_event = new_tb6612fng_ctrl_event();
			tb6612_event->mode = TB6612FNG_MODE_CCW;
			tb6612_event->duty_cycle = 100;
			EVENT_SUBMIT(tb6612_event);
		} else {
			struct tb6612fng_ctrl_event *tb6612_event;
			tb6612_event = new_tb6612fng_ctrl_event();
			tb6612_event->mode = TB6612FNG_MODE_STANDBY;
			tb6612_event->duty_cycle = 0;
			EVENT_SUBMIT(tb6612_event);
		}

		return false;
	}

	if (is_hx711_data_event(eh)) {
		const struct hx711_data_event *event =
			cast_hx711_data_event(eh);

		atomic_set(&weight_measurement, event->value);
		atomic_set(&measurement_updated, true);

		return false;
	}

	/* If event is unhandled, unsubscribe. */
	__ASSERT_NO_MSG(false);

	return false;
}

EVENT_LISTENER(MODULE, event_handler);
EVENT_SUBSCRIBE(MODULE, module_state_event);
EVENT_SUBSCRIBE(MODULE, potmeter_data_event);
EVENT_SUBSCRIBE(MODULE, hx711_data_event);
