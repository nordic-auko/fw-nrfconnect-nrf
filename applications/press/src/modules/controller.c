/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <kernel.h>
#include <stdlib.h>

#define MODULE controller
#include "module_state_event.h"
#include "hx711_ctrl_event.h"
#include "hx711_data_event.h"
#include "tb6612fng_ctrl_event.h"
#include "potmeter_ctrl_event.h"
#include "potmeter_data_event.h"

#define THREAD_STACK_SIZE 4096
#define THREAD_PRIORITY K_PRIO_PREEMPT(K_HIGHEST_APPLICATION_THREAD_PRIO)

#define CONTROLLER_MOTOR_DUTY_CYCLE 50

#include <logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_PRESS_CONTROLLER_LOG_LEVEL);

static K_THREAD_STACK_DEFINE(thread_stack, THREAD_STACK_SIZE);
static struct k_thread thread;

static enum {
	CONTROLLER_STATE_IDLE,   /* Stopped, or in hysteresis */
	CONTROLLER_STATE_RUNNING_RETRACT,/* Moving towards set point, clockwise */
	CONTROLLER_STATE_RUNNING_EXTEND,/* Moving towards set point, counter-clockwise */
} controller_state;

/* Weight in grams */
static atomic_t weight_set_point;
static atomic_t weight_measurement;
static atomic_t set_point_updated;
static atomic_t measurement_updated;

static atomic_t zero_set;
static atomic_t finished;

static void motor_stop(void)
{
	struct tb6612fng_ctrl_event *tb6612_event;
	tb6612_event = new_tb6612fng_ctrl_event();
	tb6612_event->mode = TB6612FNG_MODE_STANDBY;
	tb6612_event->duty_cycle = 0;
	EVENT_SUBMIT(tb6612_event);

	controller_state = CONTROLLER_STATE_IDLE;
	LOG_INF("controller_state=CONTROLLER_STATE_IDLE");
}

static void motor_retract(void)
{
	struct tb6612fng_ctrl_event *tb6612_event;
	tb6612_event = new_tb6612fng_ctrl_event();
	tb6612_event->mode = TB6612FNG_MODE_CW;
	tb6612_event->duty_cycle = CONTROLLER_MOTOR_DUTY_CYCLE;
	EVENT_SUBMIT(tb6612_event);

	controller_state = CONTROLLER_STATE_RUNNING_RETRACT;
	LOG_INF("controller_state=CONTROLLER_STATE_RUNNING_RETRACT");
}

static void motor_extend(void)
{
	struct tb6612fng_ctrl_event *tb6612_event;
	tb6612_event = new_tb6612fng_ctrl_event();
	tb6612_event->mode = TB6612FNG_MODE_CCW;
	tb6612_event->duty_cycle = CONTROLLER_MOTOR_DUTY_CYCLE;
	EVENT_SUBMIT(tb6612_event);

	controller_state = CONTROLLER_STATE_RUNNING_EXTEND;
	LOG_INF("controller_state=CONTROLLER_STATE_RUNNING_EXTEND");
}

static void controller_thread_func(void)
{
	const int sleep_delay_ms = CONFIG_CONTROLLER_CYCLE;

	while (true) {
		k_msleep(sleep_delay_ms);

		if (atomic_get(&finished)) {
			/* Retract motor for 30 seconds */
			motor_retract();
			k_msleep(30 * 1000);
			motor_stop();
			return;
		}

		if (!atomic_get(&zero_set)) {
			/* Potmeter must be set to 0 before starting */
			continue;
		}

		if (!atomic_get(&set_point_updated) && !atomic_get(&measurement_updated)) {
			continue;
		}

		int set_point = atomic_get(&weight_set_point);
		int measurement = atomic_get(&weight_measurement);
		int error = set_point - measurement;
		bool extend;

		LOG_INF("set_point=%d,measurement=%d,error=%d", set_point, measurement, error);

		if (set_point <= 20) {
			if (controller_state != CONTROLLER_STATE_IDLE) {
				motor_stop();
			}
			continue;
		}

		if (abs(error) <= CONFIG_CONTROLLER_HYSTERESIS) {
			if (controller_state != CONTROLLER_STATE_IDLE) {
				motor_stop();
			}
			continue;
		}

		if (error < 0) {
			extend = false;
		} else {
			extend = true;
		}

		switch (controller_state) {
		case CONTROLLER_STATE_IDLE:
			if (extend) {
				motor_extend();
			} else {
				motor_retract();
			}
			break;
		case CONTROLLER_STATE_RUNNING_EXTEND:
			if (!extend) {
				motor_retract();
			}
			break;
		case CONTROLLER_STATE_RUNNING_RETRACT:
			if (extend) {
				motor_extend();
			}
			break;
		default:
			__ASSERT_NO_MSG(false);
			break;
		}

		k_msleep(50);
		motor_stop();

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

		if (event->value == 0) {
			if (atomic_get(&zero_set)) {
				atomic_set(&finished, true);
			} else {
				atomic_set(&zero_set, true);
			}
		}

		atomic_set(&weight_set_point, event->value * 100);
		atomic_set(&set_point_updated, true);

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
