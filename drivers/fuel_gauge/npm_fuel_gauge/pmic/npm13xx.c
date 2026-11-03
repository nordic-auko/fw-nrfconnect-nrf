/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm13xx_charger.h>
#include <zephyr/drivers/mfd/npm13xx.h>

#include "../npm_fuel_gauge_priv.h"

/* nPM13xx CHARGER.BCHGCHARGESTATUS register bitmasks */
#define CHGR_STATUS_COMPLETE_MASK BIT(1)
#define CHGR_STATUS_TRICKLE_MASK  BIT(2)
#define CHGR_STATUS_CC_MASK	  BIT(3)
#define CHGR_STATUS_CV_MASK	  BIT(4)
#define CHGR_SUPPL_MODE_MASK	  BIT(7)

/* nPM13xx SYSREG.VBUSINSTATUS register bitmasks */
#define SYSREG_VBUSINSTATUS_VBUSINPRESENT_MASK	     BIT(0)
#define SYSREG_VBUSINSTATUS_VBUSINCURRLIMACTIVE_MASK BIT(1)

/* nPM13xx CHARGER.NTCSTATUS register bitmasks */
#define CHGR_BASE	      0x03U
#define CHGR_OFFSET_NTCSTATUS 0x32U

static int npm13xx_sensor_read(const struct device *dev,
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
	*voltage = (float)value.val1 + ((float)value.val2 / 1000000);

	ret = sensor_channel_get(dev, SENSOR_CHAN_GAUGE_AVG_CURRENT, &value);
	if (ret < 0) {
		return ret;
	}
	*current = (float)value.val1 + ((float)value.val2 / 1000000);

	if (temperature_measurement == NPM_FUEL_GAUGE_TEMP_STATIC) {
		return 0;
	}

	ret = sensor_channel_get(dev,
				 temperature_measurement == NPM_FUEL_GAUGE_TEMP_NTC
					 ? SENSOR_CHAN_GAUGE_TEMP
					 : SENSOR_CHAN_DIE_TEMP,
				 &value);
	if (ret < 0) {
		return ret;
	}
	*temp = (float)value.val1 + ((float)value.val2 / 1000000);

	return 0;
}

static int npm13xx_update_params(const struct device *dev,
				 struct nrf_fuel_gauge_config_parameters *config)
{
	return 0;
}

static int npm13xx_chg_state_get(const struct device *dev,
				 enum nrf_fuel_gauge_charge_state *chg_state)
{
	struct sensor_value value;
	int err;

	err = sensor_channel_get(dev, SENSOR_CHAN_NPM13XX_CHARGER_STATUS, &value);
	if (err < 0) {
		return err;
	}
	if (value.val1 & CHGR_STATUS_CV_MASK) {
		*chg_state = NRF_FUEL_GAUGE_CHARGE_STATE_CV;
	} else if (value.val1 & CHGR_STATUS_CC_MASK) {
		*chg_state = NRF_FUEL_GAUGE_CHARGE_STATE_CC;
	} else if (value.val1 & CHGR_STATUS_TRICKLE_MASK) {
		*chg_state = NRF_FUEL_GAUGE_CHARGE_STATE_TRICKLE;
	} else if (value.val1 & CHGR_STATUS_COMPLETE_MASK) {
		*chg_state = NRF_FUEL_GAUGE_CHARGE_STATE_COMPLETE;
	} else {
		*chg_state = NRF_FUEL_GAUGE_CHARGE_STATE_IDLE;
	}

	if (*chg_state != NRF_FUEL_GAUGE_CHARGE_STATE_CC) {
		return 0;
	}

	/* Check conditions that might restrict CC current:
	 * - Vbus limit
	 * - Supplement mode
	 * - Temperature restriction
	 */
	if (value.val1 & CHGR_SUPPL_MODE_MASK) {
		*chg_state = NRF_FUEL_GAUGE_CHARGE_STATE_CC_LIMITED;
		return 0;
	}

	err = sensor_channel_get(dev, SENSOR_CHAN_NPM13XX_CHARGER_VBUS_STATUS, &value);
	if (err < 0) {
		return err;
	}

	if (value.val1 & SYSREG_VBUSINSTATUS_VBUSINCURRLIMACTIVE_MASK) {
		*chg_state = NRF_FUEL_GAUGE_CHARGE_STATE_CC_LIMITED;
		return 0;
	}

	/* TODO: Check NTCSTATUS register */

	return 0;
}

static int npm13xx_chg_current_get(const struct device *dev, float *cc_current,
				   float *termination_current)
{
	struct sensor_value value;
	int err;

	err = sensor_channel_get(dev, SENSOR_CHAN_GAUGE_DESIRED_CHARGING_CURRENT, &value);
	if (err < 0) {
		return err;
	}

	*cc_current = sensor_value_to_float(&value);
	*termination_current = *cc_current / 10.f;

	return 0;
}

static int npm13xx_vbus_get(const struct device *dev, bool *vbus_present)
{
	struct sensor_value value;
	int err;

	*vbus_present = false;

	err = sensor_channel_get(dev, SENSOR_CHAN_NPM13XX_CHARGER_VBUS_STATUS, &value);
	if (err < 0) {
		return err;
	}

	*vbus_present = !!(value.val1 & SYSREG_VBUSINSTATUS_VBUSINPRESENT_MASK);

	return 0;
}

DECLARE_PMIC_HW_ABSTRACTION(npm1300) = {
	.capability_flags =
		{
			.vbus = 1,
			.charger = 1,
			.current_measurement = 1,
			.resistance_tracking = 0,
		},
	.sensor_read = npm13xx_sensor_read,
	.update_params = npm13xx_update_params,
	.chg_state_get = npm13xx_chg_state_get,
	.chg_current_get = npm13xx_chg_current_get,
	.vbus_get = npm13xx_vbus_get,
};

DECLARE_PMIC_HW_ABSTRACTION(npm1304) = {
	.capability_flags =
		{
			.vbus = 1,
			.charger = 1,
			.current_measurement = 1,
			.resistance_tracking = 0,
		},
	.sensor_read = npm13xx_sensor_read,
	.update_params = npm13xx_update_params,
	.chg_state_get = npm13xx_chg_state_get,
	.chg_current_get = npm13xx_chg_current_get,
	.vbus_get = npm13xx_vbus_get,
};
