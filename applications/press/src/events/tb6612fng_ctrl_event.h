/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef _TB6612FNG_CTRL_EVENT_H_
#define _TB6612FNG_CTRL_EVENT_H_

/**
 * @brief TB6612FNG control event
 * @defgroup tb6612fng_ctrl_event TB6612FNG control event
 * @{
 */

#include <string.h>
#include <toolchain/common.h>

#include "event_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TB6612FNG_MODE_LIST \
	X(CW) \
	X(CCW) \
	X(BRAKE) \
	X(STOP) \
	X(STANDBY)

enum tb6612fng_mode {
#define X(_mode) TB6612FNG_MODE_##_mode,
	TB6612FNG_MODE_LIST
#undef X
};

struct tb6612fng_ctrl_event {
	struct event_header header;

	enum tb6612fng_mode mode;
	uint8_t duty_cycle;
};

EVENT_TYPE_DECLARE(tb6612fng_ctrl_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _TB6612FNG_CTRL_EVENT_H_ */
