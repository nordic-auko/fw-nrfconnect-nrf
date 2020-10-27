/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include "gpio_pins.h"

#include <hal/nrf_saadc.h>

/* This structure enforces the header file is included only once in the build.
 * Violating this requirement triggers a multiple definition error at link time.
 */
const struct {} potmeter_pin_config_include_once;

static const struct gpio_pin potmeter_vdd = {
	.port = 0, .pin = 4
};

static const uint8_t potmeter_ain = NRF_SAADC_INPUT_AIN1;
