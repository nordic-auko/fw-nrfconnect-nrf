/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef _POTMETER_DATA_EVENT_H_
#define _POTMETER_DATA_EVENT_H_

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

struct potmeter_data_event {
	struct event_header header;

	uint32_t value;
};

EVENT_TYPE_DECLARE(potmeter_data_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _POTMETER_DATA_EVENT_H_ */
