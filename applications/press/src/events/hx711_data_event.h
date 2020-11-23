/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef _HX711_DATA_EVENT_H_
#define _HX711_DATA_EVENT_H_

/**
 * @brief HX711 Data Event
 * @defgroup hx711_data_event HX711 Data Event
 * @{
 */

#include <string.h>
#include <toolchain/common.h>

#include "event_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HX711_CHANNEL_LIST \
	X(A)\
	X(B)

enum hx711_channel {
#define X(_channel) HX711_CHANNEL_##_channel,
	HX711_CHANNEL_LIST
#undef X
};

struct hx711_data_event {
	struct event_header header;

	enum hx711_channel channel;
	int32_t value;
};

EVENT_TYPE_DECLARE(hx711_data_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _HX711_DATA_EVENT_H_ */
