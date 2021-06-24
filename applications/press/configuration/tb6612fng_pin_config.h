/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include "gpio_pins.h"

/* This structure enforces the header file is included only once in the build.
 * Violating this requirement triggers a multiple definition error at link time.
 */
const struct {} tb6612fng_pin_config_include_once;

enum tb6612fng_pin_types {
    TB6612FNG_PIN_IN1,
    TB6612FNG_PIN_IN2,
    TB6612FNG_PIN_PWM,
    TB6612FNG_PIN_STANDBY,
};

static const struct gpio_pin tb6612fng_pins[] = {
    [TB6612FNG_PIN_PWM] = {.port = 1, .pin = 12},
    [TB6612FNG_PIN_IN2] = {.port = 1, .pin = 11},
    [TB6612FNG_PIN_IN1] = {.port = 1, .pin = 10},
    [TB6612FNG_PIN_STANDBY] = {.port = 1, .pin = 8},
};
