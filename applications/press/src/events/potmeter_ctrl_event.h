/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef _POTMETER_CTRL_EVENT_H_
#define _POTMETER_CTRL_EVENT_H_

/**
 * @brief Peer Connection Event
 * @defgroup peer_conn_event Peer Connection Event
 * @{
 */

#include <string.h>
#include <toolchain/common.h>

#include "event_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POTMETER_CMD_LIST \
	X(POTMETER_CMD_ENABLE) \
	X(POTMETER_CMD_DISABLE)

enum potmeter_cmd {
#define X(_cmd) _cmd,
	POTMETER_CMD_LIST
#undef X
};

struct potmeter_ctrl_event {
	struct event_header header;

	enum potmeter_cmd cmd;
};

EVENT_TYPE_DECLARE(potmeter_ctrl_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _POTMETER_CTRL_EVENT_H_ */
