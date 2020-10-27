/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <stdio.h>
#include <assert.h>

#include "hx711_data_event.h"

static const char * const channel_name[] = {
#define X(_channel) STRINGIFY(_channel),
	HX711_CHANNEL_LIST
#undef X
};

static int log_hx711_data_event(const struct event_header *eh, char *buf,
				  size_t buf_len)
{
	const struct hx711_data_event *event = cast_hx711_data_event(eh);

	return snprintf(
		buf,
		buf_len,
		"%s: 0x%08X", channel_name[event->channel], event->value);
}

EVENT_TYPE_DEFINE(hx711_data_event,
		  IS_ENABLED(CONFIG_PRESS_LOG_HX711_DATA_EVENT),
		  log_hx711_data_event,
		  NULL);
