/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/charger.h>
#include <zephyr/drivers/charger/npm10xx.h>

#include "../npm_fuel_gauge_priv.h"

static int npm10xx_sensor_read(const struct device *dev,
			       enum npm_fuel_gauge_temperature_measurement temperature_measurement,
			       float *voltage, float *current, float *temp)
{
	struct sensor_value value;
	int ret;

	ret = sensor_sample_fetch(dev);
	if (ret < 0) {
		return ret;
	}

	ret = sensor_channel_get(dev, SENSOR_CHAN_GAUGE_VOLTAGE, &value);
	if (ret < 0) {
		return ret;
	}
	*voltage = sensor_value_to_float(&value);

	ret = sensor_channel_get(dev, SENSOR_CHAN_GAUGE_AVG_CURRENT, &value);
	if (ret < 0) {
		return ret;
	}
	*current = sensor_value_to_float(&value);

	switch (temperature_measurement) {
	case NPM_FUEL_GAUGE_TEMP_STATIC:
		return 0;
	case NPM_FUEL_GAUGE_TEMP_NTC:
		ret = sensor_channel_get(dev, SENSOR_CHAN_GAUGE_TEMP, &value);
		break;
	case NPM_FUEL_GAUGE_TEMP_DIE:
		/* The default ADC fetch only updates battery measurements. */
		ret = sensor_sample_fetch_chan(dev, SENSOR_CHAN_DIE_TEMP);
		if (ret < 0) {
			return ret;
		}
		ret = sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &value);
		break;
	default:
		return -EINVAL;
	}
	if (ret < 0) {
		return ret;
	}
	*temp = sensor_value_to_float(&value);

	return 0;
}

static int npm10xx_update_params(const struct device *dev,
				 struct nrf_fuel_gauge_config_parameters *config)
{
	/* The ADC driver already applies the nPM10xx current calibration. */
	return 0;
}

static int npm10xx_chg_state_get(const struct device *dev,
				 enum nrf_fuel_gauge_charge_state *chg_state)
{
	union charger_propval value;
	int err;

	err = charger_get_prop(dev, CHARGER_PROP_STATUS, &value);
	if (err < 0) {
		return err;
	}
	switch (value.status) {
	case CHARGER_STATUS_FULL:
		*chg_state = NRF_FUEL_GAUGE_CHARGE_STATE_COMPLETE;
		return 0;
	case CHARGER_STATUS_NOT_CHARGING:
	case CHARGER_STATUS_DISCHARGING:
		*chg_state = NRF_FUEL_GAUGE_CHARGE_STATE_IDLE;
		return 0;
	case CHARGER_STATUS_CHARGING:
		break;
	default:
		return -ENOTSUP;
	}

	err = charger_get_prop(dev, CHARGER_PROP_CHARGE_TYPE, &value);
	if (err < 0) {
		return err;
	}
	switch (value.charge_type) {
	case CHARGER_CHARGE_TYPE_TRICKLE:
		*chg_state = NRF_FUEL_GAUGE_CHARGE_STATE_TRICKLE;
		return 0;
	case CHARGER_CHARGE_TYPE_STANDARD:
		*chg_state = NRF_FUEL_GAUGE_CHARGE_STATE_CV;
		return 0;
	case CHARGER_CHARGE_TYPE_LONGLIFE:
		*chg_state = NRF_FUEL_GAUGE_CHARGE_STATE_CC_LIMITED;
		return 0;
	case CHARGER_CHARGE_TYPE_FAST:
		*chg_state = NRF_FUEL_GAUGE_CHARGE_STATE_CC;
		break;
	default:
		return -ENOTSUP;
	}

	err = charger_get_prop(dev, CHARGER_PROP_HEALTH, &value);
	if (err < 0) {
		return err;
	}
	if (value.health == CHARGER_HEALTH_OVERHEAT || value.health == CHARGER_HEALTH_COOL ||
	    value.health == CHARGER_HEALTH_WARM) {
		*chg_state = NRF_FUEL_GAUGE_CHARGE_STATE_CC_LIMITED;
	}

	return 0;
}

static int npm10xx_chg_current_get(const struct device *dev, float *cc_current,
				   float *termination_current)
{
	static const float termination_factors[] = {
		[NPM10XX_CHARGER_ITERM_12P5] = 0.125f,	  [NPM10XX_CHARGER_ITERM_100P] = 1.f,
		[NPM10XX_CHARGER_ITERM_50P] = 0.5f,	  [NPM10XX_CHARGER_ITERM_25P] = 0.25f,
		[NPM10XX_CHARGER_ITERM_6P25] = 0.0625f,	  [NPM10XX_CHARGER_ITERM_3P125] = 0.03125f,
		[NPM10XX_CHARGER_ITERM_1P56] = 0.015625f, [NPM10XX_CHARGER_ITERM_0P78] = 0.0078125f,
	};
	union charger_propval icc_propval;
	union charger_propval iterm_propval;
	int err;

	err = charger_get_prop(dev, CHARGER_PROP_CONSTANT_CHARGE_CURRENT_UA, &icc_propval);
	if (err < 0) {
		return err;
	}

	err = charger_get_prop(dev, NPM10XX_CHARGER_PROP_TERM_CURRENT, &iterm_propval);
	if (err < 0) {
		return err;
	}
	if (iterm_propval.custom_uint >= ARRAY_SIZE(termination_factors)) {
		return -EINVAL;
	}

	*cc_current = (float)icc_propval.const_charge_current_ua / 1000000.f;
	*termination_current = *cc_current * termination_factors[iterm_propval.custom_uint];

	return 0;
}

static int npm10xx_vbus_get(const struct device *dev, bool *vbus_present)
{
	union charger_propval value;
	int ret;

	ret = charger_get_prop(dev, CHARGER_PROP_ONLINE, &value);
	if (ret < 0) {
		return ret;
	}
	*vbus_present = value.online != CHARGER_ONLINE_OFFLINE;

	return 0;
}

DECLARE_PMIC_HW_ABSTRACTION(npm1012) = {
	.capability_flags =
		{
			.vbus = 1,
			.charger = 1,
			.current_measurement = 1,
			.resistance_tracking = 1,
		},
	.sensor_read = npm10xx_sensor_read,
	.update_params = npm10xx_update_params,
	.chg_state_get = npm10xx_chg_state_get,
	.chg_current_get = npm10xx_chg_current_get,
	.vbus_get = npm10xx_vbus_get,
};
