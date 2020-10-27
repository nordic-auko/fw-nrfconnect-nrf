/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <kernel.h>

#define MODULE controller
#include "module_state_event.h"
#include "hx711_ctrl_event.h"
#include "tb6612fng_ctrl_event.h"
#include "potmeter_ctrl_event.h"
#include "potmeter_data_event.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_PRESS_CONTROLLER_LOG_LEVEL);

static bool event_handler(const struct event_header *eh)
{
	if (is_module_state_event(eh)) {
		const struct module_state_event *event =
			cast_module_state_event(eh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			struct potmeter_ctrl_event *potmeter_event;
			potmeter_event = new_potmeter_ctrl_event();
			potmeter_event->cmd = POTMETER_CMD_ENABLE;
			EVENT_SUBMIT(potmeter_event);

			struct hx711_ctrl_event *hx711_event;
			hx711_event = new_hx711_ctrl_event();
			hx711_event->cmd = HX711_CMD_ENABLE;
			EVENT_SUBMIT(hx711_event);
		}

		return false;
	}

	if (is_potmeter_data_event(eh)) {
		const struct potmeter_data_event *event =
			cast_potmeter_data_event(eh);

		struct tb6612fng_ctrl_event *tb6612fng_event;
		tb6612fng_event = new_tb6612fng_ctrl_event();
		tb6612fng_event->mode = TB6612FNG_MODE_CW;
		tb6612fng_event->duty_cycle = event->value <= 100 ? event->value : 100;
		EVENT_SUBMIT(tb6612fng_event);

		return false;
	}


	/* If event is unhandled, unsubscribe. */
	__ASSERT_NO_MSG(false);

	return false;
}

EVENT_LISTENER(MODULE, event_handler);
EVENT_SUBSCRIBE(MODULE, module_state_event);
EVENT_SUBSCRIBE(MODULE, potmeter_data_event);
