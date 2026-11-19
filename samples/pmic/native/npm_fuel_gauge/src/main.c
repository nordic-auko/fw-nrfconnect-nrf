/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/settings/settings.h>

#include <drivers/fuel_gauge/npm_fuel_gauge.h>
#include <zephyr/drivers/fuel_gauge.h>

#include <zephyr/sys/printk.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

static void print_measurements(const struct device *fg_dev)
{
	const fuel_gauge_prop_t props[] = {
		FUEL_GAUGE_VOLTAGE_UV,
		FUEL_GAUGE_TEMPERATURE_DK,
		FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE_PCT,
#if !IS_ENABLED(CONFIG_DT_HAS_NORDIC_NPM2100_ENABLED)
		FUEL_GAUGE_CURRENT_UA,
#endif
	};
	union fuel_gauge_prop_val vals[ARRAY_SIZE(props)];
	int err;

	err = fuel_gauge_get_props(fg_dev, props, vals, ARRAY_SIZE(props));
	if (err) {
		LOG_ERR("Failed to get fuel gauge properties.");
		return;
	}

#if IS_ENABLED(CONFIG_DT_HAS_NORDIC_NPM2100_ENABLED)
	LOG_INF("v=%f, t=%f, soc=%d", (double)vals[0].voltage_uv / 1e6,
		(double)vals[1].temperature_dk / 1e1 - 273.15,
		vals[2].relative_state_of_charge_pct);
#else
	LOG_INF("v=%f, i=%f, t=%f, soc=%d", (double)vals[0].voltage_uv / 1e6,
		(double)vals[3].current_ua / 1e6, (double)vals[1].temperature_dk / 1e1 - 273.15,
		vals[2].relative_state_of_charge_pct);
#endif /* IS_ENABLED(CONFIG_DT_HAS_NORDIC_NPM2100_ENABLED) */
}

int main(void)
{
	int err;
	const struct device *fg_dev = DEVICE_DT_GET(DT_NODELABEL(npm_fuel_gauge));

	if (!device_is_ready(fg_dev)) {
		LOG_ERR("fuel gauge device not ready.");
		return 0;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		err = settings_subsys_init();
		if (err < 0) {
			LOG_ERR("Settings subsystem could not be initialized.");
		}
	}

	LOG_INF("Fuel gauge device ok");

	while (1) {
		k_sleep(K_SECONDS(1));
		print_measurements(fg_dev);
	}
}
