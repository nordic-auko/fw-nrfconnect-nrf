/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <math.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include "../npm_fuel_gauge_priv.h"

static int npm2100_sensor_read(const struct device *dev,
			       enum npm_fuel_gauge_temperature_measurement temperature_measurement,
			       float *voltage, float *current, float *temp)
{
	struct sensor_value value;
	int ret;

	ret = sensor_sample_fetch(dev);
	if (ret < 0) {
		return ret;
	}

	*current = NAN;

	ret = sensor_channel_get(dev, SENSOR_CHAN_GAUGE_VOLTAGE, &value);
	if (ret < 0) {
		return ret;
	}
	*voltage = (float)value.val1 + ((float)value.val2 / 1000000);

	if (temperature_measurement == NPM_FUEL_GAUGE_TEMP_STATIC) {
		return 0;
	} else if (temperature_measurement == NPM_FUEL_GAUGE_TEMP_NTC) {
		return -EINVAL;
	}

	ret = sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &value);
	if (ret < 0) {
		return ret;
	}
	*temp = (float)value.val1 + ((float)value.val2 / 1000000);

	return 0;
}

static int npm2100_update_params(const struct device *dev,
				 struct nrf_fuel_gauge_config_parameters *config)
{
	return 0;
}

DECLARE_PMIC_HW_ABSTRACTION(npm2100) = {
	.capability_flags =
		{
			.vbus = 0,
			.charger = 0,
			.current_measurement = 0,
			.resistance_tracking = 0,
		},
	.sensor_read = npm2100_sensor_read,
	.update_params = npm2100_update_params,
};