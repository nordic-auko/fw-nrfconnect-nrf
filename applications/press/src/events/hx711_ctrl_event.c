/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <stdio.h>
#include <assert.h>

#include "hx711_ctrl_event.h"

static const char * const cmd_name[] = {
#define X(_cmd) STRINGIFY(_cmd),
	HX711_CMD_LIST
#undef X
};

static int log_hx711_ctrl_event(const struct event_header *eh, char *buf,
				  size_t buf_len)
{
	const struct hx711_ctrl_event *event = cast_hx711_ctrl_event(eh);

	return snprintf(
		buf,
		buf_len,
		"%s", cmd_name[event->cmd]);
}

EVENT_TYPE_DEFINE(hx711_ctrl_event,
		  IS_ENABLED(CONFIG_PRESS_LOG_HX711_CTRL_EVENT),
		  log_hx711_ctrl_event,
		  NULL);
