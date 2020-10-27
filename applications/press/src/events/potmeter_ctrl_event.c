/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <stdio.h>
#include <assert.h>

#include "potmeter_ctrl_event.h"

static const char * const cmd_name[] = {
#define X(_cmd) STRINGIFY(_cmd),
	POTMETER_CMD_LIST
#undef X
};

static int log_potmeter_ctrl_event(const struct event_header *eh, char *buf,
				  size_t buf_len)
{
	const struct potmeter_ctrl_event *event = cast_potmeter_ctrl_event(eh);

	return snprintf(
		buf,
		buf_len,
		"%s", cmd_name[event->cmd]);
}

EVENT_TYPE_DEFINE(potmeter_ctrl_event,
		  IS_ENABLED(CONFIG_PRESS_LOG_POTMETER_CTRL_EVENT),
		  log_potmeter_ctrl_event,
		  NULL);
