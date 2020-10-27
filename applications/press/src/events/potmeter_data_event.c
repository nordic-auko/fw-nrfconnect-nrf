/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <stdio.h>
#include <assert.h>

#include "potmeter_data_event.h"

static int log_potmeter_data_event(const struct event_header *eh, char *buf,
				  size_t buf_len)
{
	const struct potmeter_data_event *event = cast_potmeter_data_event(eh);

	return snprintf(
		buf,
		buf_len,
		"%d", event->value);
}

EVENT_TYPE_DEFINE(potmeter_data_event,
		  IS_ENABLED(CONFIG_PRESS_LOG_POTMETER_DATA_EVENT),
		  log_potmeter_data_event,
		  NULL);
