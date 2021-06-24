/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <kernel.h>

#include <drivers/display.h>
#include <lvgl.h>

#define MODULE display
#include "module_state_event.h"
#include "potmeter_data_event.h"
#include "hx711_data_event.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_PRESS_DISPLAY_LOG_LEVEL);

#define THREAD_STACK_SIZE 4096
#define THREAD_PRIORITY K_PRIO_PREEMPT(K_LOWEST_APPLICATION_THREAD_PRIO)

static K_THREAD_STACK_DEFINE(thread_stack, THREAD_STACK_SIZE);
static struct k_thread thread;

K_SEM_DEFINE(display_update_sem, 1, 1);

static void display_update_timer_func(struct k_timer *dummy);
K_TIMER_DEFINE(display_update_timer, display_update_timer_func, NULL);

static const struct device *display_dev;

// static volatile uint32_t potmeter_update;
static atomic_t weight_set_point;
static atomic_t weight_measurement;

static void display_update_timer_func(struct k_timer *dummy)
{
	k_sem_give(&display_update_sem);
}

static void display_thread_func(void)
{
	char str[128];

	int displayed_set_point = -1;
	int displayed_measurement = -1;

	display_dev = device_get_binding(CONFIG_LVGL_DISPLAY_DEV_NAME);
	__ASSERT_NO_MSG(display_dev != NULL);
	// uint32_t display_value = 0;

	int err;
	
	err = display_set_orientation(display_dev, DISPLAY_ORIENTATION_ROTATED_180);
	if (err) {
		LOG_ERR("display_set_orientation: %d", err);
	}

	lv_obj_t *label;
	static lv_style_t font_style;
	static lv_style_t angle_style;

	lv_style_set_text_font(&font_style, LV_STATE_DEFAULT, &lv_font_montserrat_24);
	lv_style_set_transform_angle(&angle_style, LV_STATE_DEFAULT, 90);

	label = lv_label_create(lv_scr_act(), NULL);
	lv_obj_align(label, NULL, LV_ALIGN_IN_TOP_LEFT, 40, 120);
	lv_obj_add_style(label, LV_OBJ_PART_MAIN, &font_style);
	lv_obj_add_style(label, LV_OBJ_PART_MAIN, &angle_style);
	// snprintf(str, sizeof(str), "%d.%d kg", display_value / 10, display_value - (display_value / 10) * 10);

	lv_label_set_text(label, str);

	lv_task_handler();
	display_blanking_off(display_dev);

	k_timer_start(
		&display_update_timer,
		K_USEC((1000000 / CONFIG_DISPLAY_REFRESH_RATE)),
		K_USEC((1000000 / CONFIG_DISPLAY_REFRESH_RATE)));

	while (true) {

		k_sem_take(&display_update_sem, K_FOREVER);

		if (displayed_set_point == atomic_get(&weight_set_point) &&
			displayed_measurement == atomic_get(&weight_measurement)) {
			continue;
		}

		displayed_set_point = atomic_get(&weight_set_point);
		displayed_measurement = atomic_get(&weight_measurement);

		snprintf(str, sizeof(str), "Set: %d.%d kg\nMeas: %d.%d kg\n",
			displayed_set_point / 10, displayed_set_point - (displayed_set_point / 10) * 10,
			displayed_measurement / 1000, displayed_measurement - (displayed_measurement / 1000) * 1000);
		lv_label_set_text(label, str);

		lv_task_handler();
		display_blanking_off(display_dev);
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
					(k_thread_entry_t)display_thread_func,
					NULL, NULL, NULL,
					THREAD_PRIORITY, 0, K_NO_WAIT);
			k_thread_name_set(&thread, MODULE_NAME "_thread");
		}

		return false;
	}

	if (is_potmeter_data_event(eh)) {
		const struct potmeter_data_event *event =
			cast_potmeter_data_event(eh);

		atomic_set(&weight_set_point, event->value);

		return false;
	}

	if (is_hx711_data_event(eh)) {
		const struct hx711_data_event *event =
			cast_hx711_data_event(eh);

		atomic_set(&weight_measurement, event->value);

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
