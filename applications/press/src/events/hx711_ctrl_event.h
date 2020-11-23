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

#define HX711_STATE_LIST \
	X(HX711_STATE_READY) \
	X(HX711_STATE_FIND_TARE_WEIGHT) \
	X(HX711_STATE_RUNNING) \
	X(HX711_STATE_STOPPED)

#define HX711_CMD_LIST \
	X(HX711_CMD_ENABLE) \
	X(HX711_CMD_DISABLE) \
	X(HX711_CMD_SET_TARE_WEIGHT) \
	X(HX711_CMD_SET_CALIBRATE_WEIGHT)

enum hx711_cmd {
#define X(_cmd) _cmd,
	HX711_CMD_LIST
#undef X
};

enum hx711_state {
#define X(_state) _state,
	HX711_STATE_LIST
#undef X
};

struct hx711_ctrl_event {
	struct event_header header;

	enum hx711_cmd cmd;
	union {
		short calibration_weight;
	};
};

struct hx711_state_event {
	struct event_header header;

	enum hx711_state state;
};

EVENT_TYPE_DECLARE(hx711_ctrl_event);
EVENT_TYPE_DECLARE(hx711_state_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _HX711_CTRL_EVENT_H_ */
