/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdlib.h>
#include <math.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/pm/device.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(NPM_FUEL_GAUGE, CONFIG_FUEL_GAUGE_LOG_LEVEL);

#include <drivers/fuel_gauge/npm_fuel_gauge.h>
#include "npm_fuel_gauge_priv.h"

#include <nrf_fuel_gauge.h>

#define SUPPORTED_PMIC_LIST(X, n)                                                                  \
	X(npm1012, nordic_npm10xx_adc, n)                                                          \
	X(npm1300, nordic_npm1300_charger, n)                                                      \
	X(npm1304, nordic_npm1304_charger, n)                                                      \
	X(npm2100, nordic_npm2100_vbat, n)

struct nrf_fuel_gauge_pmic_param {
	float a_param;
	float b_param;
	float c_param;
	float d_param;
};

static void timer_expiry_fn(struct k_timer *timer)
{
	struct npm_fuel_gauge_data *data = CONTAINER_OF(timer, struct npm_fuel_gauge_data, timer);

	k_sem_give(&data->sem);
}

static bool load_battery_model_from_section(const struct npm_fuel_gauge_config *config,
					    struct npm_fuel_gauge_data *data, bool is_primary_cell)
{
	if (config->model_name == NULL) {
		return false;
	}

	if (is_primary_cell) {
		STRUCT_SECTION_FOREACH(section_battery_models, model)
		{
			if (model->primary && strcmp(config->model_name, model->name) == 0) {
				data->model_primary = model->model;
				LOG_DBG("Loaded primary battery model: %s", config->model_name);
				return true;
			}
		}
	} else {
		STRUCT_SECTION_FOREACH(section_battery_models, model)
		{
			if (!model->primary && strcmp(config->model_name, model->name) == 0) {
				data->model = model->model;
				LOG_DBG("Loaded secondary battery model: %s", config->model_name);
				return true;
			}
		}
	}

	LOG_ERR("Failed to load battery model: %s", config->model_name);

	return false;
}

static int secondary_cell_ext_state_update(const struct device *dev)
{
	const struct npm_fuel_gauge_config *config = dev->config;
	struct npm_fuel_gauge_data *data = config->data;
	enum nrf_fuel_gauge_charge_state charge_state;
	union nrf_fuel_gauge_ext_state_info_data state_info;
	bool vbus_present;
	int err;

	err = npm_fuel_gauge_chg_state_get(dev, &charge_state);
	if (err < 0) {
		return err;
	}

	/* Charge update should only be applied upon state change */
	if (charge_state != data->charge_state) {
		err = nrf_fuel_gauge_ext_state_update(
			NRF_FUEL_GAUGE_EXT_STATE_INFO_CHARGE_STATE_CHANGE,
			&(union nrf_fuel_gauge_ext_state_info_data){.charge_state = charge_state});
		if (err < 0) {
			return err;
		}

		LOG_DBG("charge state change: %d -> %d", data->charge_state, charge_state);
		data->charge_state = charge_state;
	}

	err = npm_fuel_gauge_vbus_get(dev, &vbus_present);
	if (err < 0) {
		return err;
	}

	/* VBUS connection state can be updated regardless of charge state */
	err = nrf_fuel_gauge_ext_state_update(
		vbus_present ? NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_CONNECTED
			     : NRF_FUEL_GAUGE_EXT_STATE_INFO_VBUS_DISCONNECTED,
		NULL);
	if (err < 0) {
		return err;
	}
	LOG_DBG("vbus state: %s", vbus_present ? "connected" : "disconnected");

	if (config->charger_runtime_updates || isnan(data->cc_current)) {
		err = npm_fuel_gauge_chg_current_get(dev, &data->cc_current,
						     &data->termination_current);
		if (err < 0) {
			return err;
		}

		state_info.charge_current_limit = data->cc_current;
		err = nrf_fuel_gauge_ext_state_update(
			NRF_FUEL_GAUGE_EXT_STATE_INFO_CHARGE_CURRENT_LIMIT, &state_info);
		if (err < 0) {
			data->cc_current = NAN;
			return err;
		}

		state_info.charge_term_current = data->termination_current;
		err = nrf_fuel_gauge_ext_state_update(NRF_FUEL_GAUGE_EXT_STATE_INFO_TERM_CURRENT,
						      &state_info);
		if (err < 0) {
			data->cc_current = NAN;
			return err;
		}
	}

	return 0;
}

static int fuel_gauge_lock_func(const void *mutex, bool lock)
{
	if (lock) {
		return k_mutex_lock((struct k_mutex *)mutex, K_FOREVER);
	} else {
		return k_mutex_unlock((struct k_mutex *)mutex);
	}
}

static void npm_fuel_gauge_thread(void *arg1, void *arg2, void *arg3)
{
	struct npm_fuel_gauge_data *data = arg1;
	const struct npm_fuel_gauge_config *config = arg2;
	const struct device *dev = arg3;

	struct npm_fuel_gauge_measurement measurement;
	int64_t ref_uptime;
	int err;

	if (!device_is_ready(config->sensor_dev) ||
	    (!config->is_primary_cell && !device_is_ready(config->charger_dev))) {
		LOG_ERR("Sensor or charger device not ready.");
		return;
	}

	LOG_DBG("Starting fuel gauge thread for model: %s", config->model_name);

	if (load_battery_model_from_section(config, data, config->is_primary_cell)) {
		/* Battery model provided via dts option: start right away */
		k_timer_start(&data->timer, data->processing_interval, data->processing_interval);
		k_sem_give(&data->sem);
	}

	ref_uptime = k_uptime_get();

	while (1) {
		k_sem_take(&data->sem, K_FOREVER);

		measurement.valid = false;

		err = npm_fuel_gauge_sensor_read(dev, config->temperature_measurement,
						 &measurement.voltage, &measurement.current,
						 &measurement.temperature);
		if (err < 0) {
			continue;
		}

		if (config->temperature_measurement == NPM_FUEL_GAUGE_TEMP_STATIC) {
			measurement.temperature = (float)config->static_temperature;
		}

		/* Zephyr current direction is opposite to nRF fuel gauge */
		measurement.current = -measurement.current;

		if (atomic_set(&data->initialized, true) == false) {
			struct nrf_fuel_gauge_config_parameters opt_params;
			struct nrf_fuel_gauge_init_parameters parameters = {
				.v0 = measurement.voltage,
				.i0 = measurement.current,
				.t0 = measurement.temperature,
				.opt_params = &opt_params,
				.state = NULL,
				.lock_func = fuel_gauge_lock_func,
				.lock_context = &data->fuel_gauge_mutex,
			};

			if (config->is_primary_cell) {
				parameters.model_primary = data->model_primary;
			} else {
				parameters.model = data->model;
			}

			err = nrf_fuel_gauge_config_params_default_get(&opt_params);
			if (err < 0) {
				atomic_set(&data->initialized, false);
				continue;
			}

			err = npm_fuel_gauge_update_params(dev, &opt_params);
			if (err < 0) {
				atomic_set(&data->initialized, false);
				continue;
			}

			err = nrf_fuel_gauge_init(&parameters, NULL);
			if (err < 0) {
				atomic_set(&data->initialized, false);
				continue;
			}

			ref_uptime = k_uptime_get();
			data->cc_current = NAN;
			data->termination_current = NAN;

			LOG_DBG("Initialized");
		}

		if (!config->is_primary_cell) {
			err = secondary_cell_ext_state_update(dev);
			if (err < 0) {
				continue;
			}
		}

		measurement.t_delta = ((float)k_uptime_delta(&ref_uptime)) / 1000.f;

		err = nrf_fuel_gauge_process(measurement.voltage, measurement.current,
					     measurement.temperature, measurement.t_delta,
					     &measurement.soc, NULL);
		if (err < 0) {
			continue;
		}
		LOG_DBG("Processed measurement: v=%f,i=%f,t=%f,soc=%f", (double)measurement.voltage,
			(double)measurement.current, (double)measurement.temperature,
			(double)measurement.soc);

		measurement.soh = NAN;
		measurement.tte = NAN;
		measurement.ttf = NAN;
		measurement.charge_cycle_count = 0;
		measurement.valid = true;

		err = k_mutex_lock(&data->data_mutex, K_FOREVER);
		if (err < 0) {
			continue;
		}
		data->measurement = measurement;
		(void)k_mutex_unlock(&data->data_mutex);

		if (atomic_test_bit(&data->idle_flags, NPM_FUEL_GAUGE_IDLE_REQUESTED)) {
			err = 0;

			if (atomic_get(&data->initialized)) {
				LOG_DBG("Entering idle state");
				err = nrf_fuel_gauge_idle_set(measurement.voltage,
							      measurement.temperature,
							      data->idle_current);
			}

			if (err) {
				LOG_ERR("nrf_fuel_gauge_idle_set: %d", err);
				/* Ensure timer is still running for idle retry */
				k_timer_start(&data->timer, data->processing_interval,
					      data->processing_interval);
				continue;
			}

			k_timer_stop(&data->timer);

			if (!atomic_test_bit(&data->idle_flags, NPM_FUEL_GAUGE_IDLE_REQUESTED)) {
				/* Idle requested cleared, resume polling */
				k_timer_start(&data->timer, data->processing_interval,
					      data->processing_interval);
			} else {
				atomic_set_bit(&data->idle_flags, NPM_FUEL_GAUGE_IDLE);
			}
		}
	}
}

int npm_fuel_gauge_model_set(const struct device *dev, const void *model)
{
	const struct npm_fuel_gauge_config *config;
	struct npm_fuel_gauge_data *data;

	if (dev == NULL || model == NULL) {
		return -EINVAL;
	}

	config = dev->config;
	data = config->data;

	if (config->is_primary_cell) {
		data->model_primary = model;
	} else {
		data->model = model;
	}

	if (atomic_set(&data->initialized, false) == false) {
		/* Battery model is set for the first time: start processing */
		k_timer_start(&data->timer, data->processing_interval, data->processing_interval);
	}

	k_sem_give(&data->sem);

	return 0;
}

static int npm_fuel_gauge_init(const struct device *dev)
{
	const struct npm_fuel_gauge_config *config = dev->config;
	struct npm_fuel_gauge_data *data = config->data;
	int ret;

	if (config->dev_specific == NULL) {
		return -ENOTSUP;
	}

	k_timer_init(&data->timer, timer_expiry_fn, NULL);

	ret = k_sem_init(&data->sem, 0, 1);
	if (ret < 0) {
		return ret;
	}

	ret = k_mutex_init(&data->data_mutex);
	if (ret < 0) {
		return ret;
	}

	ret = k_mutex_init(&data->fuel_gauge_mutex);
	if (ret < 0) {
		return ret;
	}

	k_thread_create(&data->thread, data->thread_stack, CONFIG_NPM_FUEL_GAUGE_THREAD_STACK_SIZE,
			npm_fuel_gauge_thread, (void *)data, (void *)config, (void *)dev,
			K_PRIO_COOP(CONFIG_NPM_FUEL_GAUGE_THREAD_PRIORITY), 0, K_NO_WAIT);
	k_thread_name_set(&data->thread, "npm_fuel_gauge_thread");

	return 0;
}

static int npm_fuel_gauge_get_property(const struct device *dev, fuel_gauge_prop_t prop,
				       union fuel_gauge_prop_val *val)
{
	const struct npm_fuel_gauge_config *config = dev->config;
	struct npm_fuel_gauge_data *data = config->data;
	struct npm_fuel_gauge_measurement measurement;
	int ret;

	if (dev == NULL || val == NULL) {
		return -EINVAL;
	}

	switch (prop) {
	case FUEL_GAUGE_VOLTAGE_UV:
	case FUEL_GAUGE_TEMPERATURE_DK:
	case FUEL_GAUGE_CURRENT_UA:
	case FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE_PCT:
		break;
	default:
		return -ENOTSUP;
	}

	ret = k_mutex_lock(&data->data_mutex, K_FOREVER);
	if (ret < 0) {
		return ret;
	}
	measurement = data->measurement;
	(void)k_mutex_unlock(&data->data_mutex);

	if (!measurement.valid) {
		return -EIO;
	}

	switch (prop) {
	case FUEL_GAUGE_VOLTAGE_UV:
		val->voltage_uv = lroundf(measurement.voltage * 1000000.f);
		break;
	case FUEL_GAUGE_TEMPERATURE_DK:
		val->temperature_dk = lroundf((measurement.temperature + 273.15f) * 10.f);
		break;
	case FUEL_GAUGE_CURRENT_UA:
		if (isnan(measurement.current)) {
			return -ENOTSUP;
		}
		val->current_ua = -lroundf(measurement.current * 1000000.f);
		break;
	case FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE_PCT:
		val->relative_state_of_charge_pct = lroundf(measurement.soc);
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int npm_fuel_gauge_set_property(const struct device *dev, fuel_gauge_prop_t prop,
				       union fuel_gauge_prop_val val)
{
	const struct npm_fuel_gauge_config *config = dev->config;
	struct npm_fuel_gauge_data *data = config->data;

	switch (prop) {
	case FUEL_GAUGE_AVG_CURRENT_UA:
		data->idle_current = ((float)val.avg_current_ua) / 1000000.f;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static DEVICE_API(fuel_gauge, npm_fuel_gauge_driver_api) = {
	.get_property = npm_fuel_gauge_get_property,
	.set_property = npm_fuel_gauge_set_property,
};

#ifdef CONFIG_PM_DEVICE
/**
 * @brief Power Management action for nPM Fuel Gauge
 *
 * @param dev Device pointer
 * @param action Power management action
 * @return int 0 on success, negative error code otherwise
 */
static int npm_fuel_gauge_pm_action(const struct device *dev, enum pm_device_action action)
{
	const struct npm_fuel_gauge_config *config = dev->config;
	struct npm_fuel_gauge_data *data = config->data;
	int ret = 0;
	bool vbus_present;

	LOG_DBG("PM action %d", action);

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		if (!atomic_test_bit(&data->idle_flags, NPM_FUEL_GAUGE_IDLE_REQUESTED)) {
			return -EALREADY;
		}

		atomic_set(&data->idle_flags, 0);

		if ((config->is_primary_cell && data->model_primary != NULL) ||
		    (!config->is_primary_cell && data->model != NULL)) {
			/* Do not start polling unless there is a valid model*/
			k_timer_start(&data->timer, data->processing_interval,
				      data->processing_interval);
		}
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		if (atomic_test_bit(&data->idle_flags, NPM_FUEL_GAUGE_IDLE_REQUESTED)) {
			return -EALREADY;
		}

		if (config->is_primary_cell) {
			vbus_present = false;
		} else {
			ret = npm_fuel_gauge_vbus_get(dev, &vbus_present);
			if (ret < 0) {
				return ret;
			}
		}

		if (vbus_present) {
			ret = -EBUSY;
		} else {
			atomic_set_bit(&data->idle_flags, NPM_FUEL_GAUGE_IDLE_REQUESTED);
		}
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	return ret;
}
#endif

#ifdef CONFIG_PM_DEVICE
#define NPM_FUEL_GAUGE_PM_DEFINE(inst) PM_DEVICE_DT_INST_DEFINE(inst, npm_fuel_gauge_pm_action);
#else
#define NPM_FUEL_GAUGE_PM_DEFINE(inst)
#endif

#define PMIC_DEV_FN_DECLARE(_name, _compat, n)                                                     \
	COND_CODE_1(DT_NODE_HAS_COMPAT(n, _compat),                                                \
		    (extern const struct pmic_hw_abstraction _name##_pmic_hw_abstraction), ())
#define PMIC_DEV_FN(_name, _compat, n)                                                             \
	COND_CODE_1(DT_NODE_HAS_COMPAT(n, _compat), (&_name##_pmic_hw_abstraction), ())

#define NPM_FUEL_GAUGE_DEFINE(n, primary_cell)                                                     \
	struct only_one_instance_currently_supported {                                             \
	};                                                                                         \
	SUPPORTED_PMIC_LIST(PMIC_DEV_FN_DECLARE, DT_INST_PHANDLE(n, sensor_device));               \
	static struct npm_fuel_gauge_data npm_fuel_gauge_data_##n = {                              \
		.processing_interval = K_MSEC(DT_INST_PROP(n, processing_interval)),               \
		.charge_state = NRF_FUEL_GAUGE_CHARGE_STATE_IDLE,                                  \
		.idle_current = ((float)DT_INST_PROP(n, idle_current_ua)) / 1000000.f,             \
		.cc_current = NAN,                                                                 \
		.termination_current = NAN,                                                        \
	};                                                                                         \
	static const struct npm_fuel_gauge_config npm_fuel_gauge_config_##n = {                    \
		.data = &npm_fuel_gauge_data_##n,                                                  \
		.is_primary_cell = primary_cell,                                                   \
		.charger_runtime_updates = DT_INST_PROP_OR(n, runtime_charger_updates, false),     \
		.temperature_measurement = DT_INST_ENUM_IDX(n, temperature_measurement),           \
		.static_temperature = DT_INST_PROP(n, static_temperature),                         \
		.model_name = DT_INST_PROP_OR(n, battery_model, NULL),                             \
		.sensor_dev = DEVICE_DT_GET_OR_NULL(DT_INST_PHANDLE(n, sensor_device)),            \
		.charger_dev = DEVICE_DT_GET_OR_NULL(DT_INST_PHANDLE(n, charger_device)),          \
		.dev_specific =                                                                    \
			SUPPORTED_PMIC_LIST(PMIC_DEV_FN, DT_INST_PHANDLE(n, sensor_device)),       \
	};                                                                                         \
                                                                                                   \
	NPM_FUEL_GAUGE_PM_DEFINE(n);                                                               \
	DEVICE_DT_INST_DEFINE(n, npm_fuel_gauge_init, PM_DEVICE_DT_INST_GET(n), NULL,              \
			      &npm_fuel_gauge_config_##n, POST_KERNEL,                             \
			      CONFIG_FUEL_GAUGE_INIT_PRIORITY, &npm_fuel_gauge_driver_api);

#define NPM_FUEL_GAUGE_PRIMARY_DEFINE(n)                                                           \
	BUILD_ASSERT(IS_ENABLED(CONFIG_NRF_FUEL_GAUGE_VARIANT_PRIMARY_CELL), "Variant mismatch");  \
	NPM_FUEL_GAUGE_DEFINE(n, true)

#define NPM_FUEL_GAUGE_SECONDARY_DEFINE(n)                                                         \
	BUILD_ASSERT(IS_ENABLED(CONFIG_NRF_FUEL_GAUGE_VARIANT_SECONDARY_CELL),                     \
		     "Variant mismatch");                                                          \
	NPM_FUEL_GAUGE_DEFINE(n, false)

#define DT_DRV_COMPAT nordic_npm_fuel_gauge_primary
DT_INST_FOREACH_STATUS_OKAY(NPM_FUEL_GAUGE_PRIMARY_DEFINE)

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT nordic_npm_fuel_gauge_secondary
DT_INST_FOREACH_STATUS_OKAY(NPM_FUEL_GAUGE_SECONDARY_DEFINE)
