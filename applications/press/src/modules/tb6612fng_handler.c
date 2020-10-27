/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <kernel.h>

#include <drivers/gpio.h>
#include <drivers/pwm.h>

#define MODULE tb6612fng_handler
#include "module_state_event.h"
#include "tb6612fng_ctrl_event.h"

#include "tb6612fng_pin_config.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_PRESS_TB6612FNG_LOG_LEVEL);

#define PWM_PERIOD_NSEC (1000000000 / CONFIG_TB6612FNG_PWM_RATE)
#define PWM_DUTY_STEP_SIZE (PWM_PERIOD_NSEC / 100)

static const struct device *pwm_dev;

static void gpio_state_set(enum tb6612fng_pin_types pin, int value)
{
	const struct device *gpio_dev = device_get_binding(port_map[tb6612fng_pins[pin].port]);
	int err;

	err = gpio_pin_set(gpio_dev, tb6612fng_pins[pin].pin, value);
	__ASSERT_NO_MSG(err == 0);
}

static void gpio_init(void)
{
	int err;

	for (int i = 0; i < ARRAY_SIZE(tb6612fng_pins); ++i) {
		const struct device *gpio_dev = device_get_binding(port_map[tb6612fng_pins[i].port]);

		err = gpio_pin_configure(gpio_dev, tb6612fng_pins[i].pin, GPIO_OUTPUT_ACTIVE);
		if (err) {
			LOG_ERR("Failed to init gpio: %d", err);
			return;
		}
	}

	gpio_state_set(TB6612FNG_PIN_STANDBY, 0);
}

static void pwm_set_duty_cycle(uint8_t duty_cycle)
{
	uint8_t pin;
	uint32_t pulse;
	int err;

	__ASSERT_NO_MSG(duty_cycle <= 100);

	pin = tb6612fng_pins[TB6612FNG_PIN_PWM].port * 32 + tb6612fng_pins[TB6612FNG_PIN_PWM].pin;
	pulse = PWM_DUTY_STEP_SIZE * (100 - duty_cycle);

	err = pwm_pin_set_nsec(pwm_dev, pin, PWM_PERIOD_NSEC, pulse, 0);
	__ASSERT_NO_MSG(err == 0);
}

static void tb6612fng_update(enum tb6612fng_mode mode, uint8_t duty_cycle)
{
	if (duty_cycle == 0) {
		gpio_state_set(TB6612FNG_PIN_IN1, 0);
		gpio_state_set(TB6612FNG_PIN_IN2, 0);
		gpio_state_set(TB6612FNG_PIN_STANDBY, 0);
		pwm_set_duty_cycle(0);

		return;
	}

	switch (mode) {
	case TB6612FNG_MODE_CW:
		gpio_state_set(TB6612FNG_PIN_IN1, 1);
		gpio_state_set(TB6612FNG_PIN_IN2, 0);
		gpio_state_set(TB6612FNG_PIN_STANDBY, 1);
		pwm_set_duty_cycle(duty_cycle);
		break;
	case TB6612FNG_MODE_CCW:
		gpio_state_set(TB6612FNG_PIN_IN1, 0);
		gpio_state_set(TB6612FNG_PIN_IN2, 1);
		gpio_state_set(TB6612FNG_PIN_STANDBY, 1);
		pwm_set_duty_cycle(duty_cycle);
		break;
	case TB6612FNG_MODE_BRAKE:
		gpio_state_set(TB6612FNG_PIN_IN1, 1);
		gpio_state_set(TB6612FNG_PIN_IN2, 1);
		gpio_state_set(TB6612FNG_PIN_STANDBY, 1);
		pwm_set_duty_cycle(0);
		break;
	case TB6612FNG_MODE_STANDBY:
		gpio_state_set(TB6612FNG_PIN_STANDBY, 0);
		/* Fall-through */
	case TB6612FNG_MODE_STOP:
		gpio_state_set(TB6612FNG_PIN_IN1, 0);
		gpio_state_set(TB6612FNG_PIN_IN2, 0);
		pwm_set_duty_cycle(100);
		break;
	default:
		__ASSERT_NO_MSG(false);
		break;
	}
}

static void pwm_init(void)
{
	pwm_dev = device_get_binding("PWM_TB6612FNG");
	__ASSERT_NO_MSG(pwm_dev != NULL);

	pwm_set_duty_cycle(0);
}

static bool event_handler(const struct event_header *eh)
{
	if (is_module_state_event(eh)) {
		const struct module_state_event *event =
			cast_module_state_event(eh);

		if (check_state(event, MODULE_ID(main), MODULE_STATE_READY)) {
			gpio_init();
			pwm_init();
		}

		return false;
	}

	if (is_tb6612fng_ctrl_event(eh)) {
		const struct tb6612fng_ctrl_event *event =
			cast_tb6612fng_ctrl_event(eh);

		tb6612fng_update(event->mode, event->duty_cycle);

		return false;
	}

	/* If event is unhandled, unsubscribe. */
	__ASSERT_NO_MSG(false);

	return false;
}

EVENT_LISTENER(MODULE, event_handler);
EVENT_SUBSCRIBE(MODULE, module_state_event);
EVENT_SUBSCRIBE(MODULE, tb6612fng_ctrl_event);
