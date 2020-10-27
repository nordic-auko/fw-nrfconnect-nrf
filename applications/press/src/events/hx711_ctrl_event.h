/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef _HX711_CTRL_EVENT_H_
#define _HX711_CTRL_EVENT_H_

/**
 * @brief HX711 control event
 * @defgroup hx711_ctrl_event HX711 control event
 * @{
 */

#include <string.h>
#include <toolchain/common.h>

#include "event_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HX711_CMD_LIST \
	X(HX711_CMD_ENABLE) \
	X(HX711_CMD_DISABLE)

enum hx711_cmd {
#define X(_cmd) _cmd,
	HX711_CMD_LIST
#undef X
};

struct hx711_ctrl_event {
	struct event_header header;

	enum hx711_cmd cmd;
};

EVENT_TYPE_DECLARE(hx711_ctrl_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _HX711_CTRL_EVENT_H_ */
