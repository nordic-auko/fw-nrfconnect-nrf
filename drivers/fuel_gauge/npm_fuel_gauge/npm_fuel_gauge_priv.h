/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#ifndef __NPM_FUEL_GAUGE_PRIV_H__
#define __NPM_FUEL_GAUGE_PRIV_H__

#include <nrf_fuel_gauge.h>

enum npm_fuel_gauge_temperature_measurement {
	NPM_FUEL_GAUGE_TEMP_NTC,
	NPM_FUEL_GAUGE_TEMP_DIE,
	NPM_FUEL_GAUGE_TEMP_STATIC
};

enum npm_fuel_gauge_idle_flags {
	NPM_FUEL_GAUGE_IDLE_REQUESTED = 0,
	NPM_FUEL_GAUGE_IDLE,
};

struct npm_fuel_gauge_pmic_capability_flags {
	uint32_t vbus: 1;
	uint32_t charger: 1;
	uint32_t current_measurement: 1;
	uint32_t resistance_tracking: 1;
};

struct npm_fuel_gauge_measurement {
	bool valid;

	float voltage;
	float current;
	float temperature;
	float t_delta;

	float soc;
	float soh;
	float tte;
	float ttf;
	uint16_t charge_cycle_count;
};

struct npm_fuel_gauge_data {
	K_KERNEL_STACK_MEMBER(thread_stack, CONFIG_NPM_FUEL_GAUGE_THREAD_STACK_SIZE);
	struct k_thread thread;
	sys_slist_t user_callbacks;
	struct k_mutex data_mutex;
	struct k_mutex fuel_gauge_mutex;

	struct k_sem sem;
	struct k_timer timer;
	atomic_t initialized;
	atomic_t idle_flags;
	k_timeout_t processing_interval;
	struct npm_fuel_gauge_measurement measurement;
	float idle_current;
	float cc_current;
	float termination_current;

	enum nrf_fuel_gauge_charge_state charge_state;
	union {
		const struct battery_model *model;
		const struct battery_model_primary *model_primary;
	};
};

struct npm_fuel_gauge_config {
	struct npm_fuel_gauge_data *data;
	bool is_primary_cell;
	bool charger_runtime_updates;
	const char *model_name;
	int static_temperature;
	enum npm_fuel_gauge_temperature_measurement temperature_measurement;
	const struct device *sensor_dev;
	const struct device *charger_dev;
	const struct pmic_hw_abstraction *dev_specific;
};

typedef int (*npm_fuel_gauge_sensor_sensor_read_fn)(
	const struct device *dev,
	enum npm_fuel_gauge_temperature_measurement temperature_measurement, float *voltage,
	float *current, float *temp);

typedef int (*npm_fuel_gauge_update_param_fn)(const struct device *dev,
					      struct nrf_fuel_gauge_config_parameters *config);

typedef int (*npm_fuel_gauge_chg_state_get_fn)(const struct device *dev,
					       enum nrf_fuel_gauge_charge_state *chg_state);

typedef int (*npm_fuel_gauge_chg_current_get_fn)(const struct device *dev, float *cc_current,
						 float *termination_current);

typedef int (*npm_fuel_gauge_vbus_get_fn)(const struct device *dev, bool *vbus_present);

struct pmic_hw_abstraction {
	struct npm_fuel_gauge_pmic_capability_flags capability_flags;

	npm_fuel_gauge_sensor_sensor_read_fn sensor_read;
	npm_fuel_gauge_update_param_fn update_params;
	npm_fuel_gauge_chg_state_get_fn chg_state_get;
	npm_fuel_gauge_chg_current_get_fn chg_current_get;
	npm_fuel_gauge_vbus_get_fn vbus_get;
};

static inline int
npm_fuel_gauge_sensor_read(const struct device *dev,
			   enum npm_fuel_gauge_temperature_measurement temperature_measurement,
			   float *voltage, float *current, float *temp)
{
	const struct npm_fuel_gauge_config *config = dev->config;

	if (config->dev_specific->sensor_read) {
		return config->dev_specific->sensor_read(
			config->sensor_dev, temperature_measurement, voltage, current, temp);
	}

	return -ENOTSUP;
}

static inline int npm_fuel_gauge_update_params(const struct device *dev,
					       struct nrf_fuel_gauge_config_parameters *params)
{
	const struct npm_fuel_gauge_config *config = dev->config;

	if (config->dev_specific->update_params) {
		return config->dev_specific->update_params(dev, params);
	}

	return -ENOTSUP;
}

static inline int npm_fuel_gauge_chg_state_get(const struct device *dev,
					       enum nrf_fuel_gauge_charge_state *chg_state)
{
	const struct npm_fuel_gauge_config *config = dev->config;

	if (config->dev_specific->capability_flags.charger == 0) {
		return -ENOTSUP;
	}

	return config->dev_specific->chg_state_get(config->charger_dev, chg_state);
}

static inline int npm_fuel_gauge_chg_current_get(const struct device *dev, float *cc_current,
						 float *termination_current)
{
	const struct npm_fuel_gauge_config *config = dev->config;

	if (config->dev_specific->capability_flags.charger == 0) {
		return -ENOTSUP;
	}

	return config->dev_specific->chg_current_get(config->charger_dev, cc_current,
						     termination_current);
}

static inline int npm_fuel_gauge_vbus_get(const struct device *dev, bool *vbus_present)
{
	const struct npm_fuel_gauge_config *config = dev->config;

	*vbus_present = false;

	if (config->dev_specific->capability_flags.vbus == 0) {
		return 0;
	}

	return config->dev_specific->vbus_get(config->charger_dev, vbus_present);
}

#define DECLARE_PMIC_HW_ABSTRACTION(_dev_name)                                                     \
	const struct pmic_hw_abstraction _dev_name##_pmic_hw_abstraction

#endif /* __NPM_FUEL_GAUGE_PRIV_H__ */
