/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <stdio.h>
#include <assert.h>

#include "tb6612fng_ctrl_event.h"

static const char * const mode_name[] = {
#define X(_mode) STRINGIFY(_mode),
	TB6612FNG_MODE_LIST
#undef X
};

static int log_tb6612fng_ctrl_event(const struct event_header *eh, char *buf,
				  size_t buf_len)
{
	const struct tb6612fng_ctrl_event *event = cast_tb6612fng_ctrl_event(eh);

	return snprintf(
		buf,
		buf_len,
		"%s: %d%%", mode_name[event->mode], event->duty_cycle);
}

EVENT_TYPE_DEFINE(tb6612fng_ctrl_event,
		  IS_ENABLED(CONFIG_PRESS_LOG_TB6612FNG_CTRL_EVENT),
		  log_tb6612fng_ctrl_event,
		  NULL);
