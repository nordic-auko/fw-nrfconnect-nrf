/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file
 * @brief Header file for extended fuel gauge API of npm_fuel_gauge
 * @ingroup npm_fuel_gauge_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_FUEL_GAUGE_NPM_FUEL_GAUGE_H_
#define ZEPHYR_INCLUDE_DRIVERS_FUEL_GAUGE_NPM_FUEL_GAUGE_H_

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/sys/iterable_sections.h>

#include <nrf_fuel_gauge.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Battery model section item used by battery declaration macros.
 */
struct section_battery_models {
	bool primary;
	const char *name;
	const void *model;
};

/* @brief Section macro for declaring a primary cell battery model
 * @details Declares a battery model struct to populate with battery model data.
 *
 *			The _name parameter can be references via dts overlay.
 *
 *			Usage:
 *			  DECLARE_BATTERY_MODEL(my_model) = {
 *				  .name = {'C', 'R', '2', '0', '3', '2'},
 *				  .param_19 = 0.15f,
 *				  (...)
 *			  };
 */
#define DECLARE_BATTERY_MODEL_PRIMARY(_name)                                                       \
	extern const struct battery_model_primary primary_battery_##_name;                         \
	const STRUCT_SECTION_ITERABLE(section_battery_models, _name) = {                           \
		.primary = true,                                                                   \
		.name = STRINGIFY(_name), .model = &primary_battery_##_name,                       \
	};                                                                                         \
	const struct battery_model_primary primary_battery_##_name

/* @brief Section macro for declaring a secondary cell battery model
 * @details Declares a battery model struct to populate with battery model data.
 *
 *			The _name parameter can be references via dts overlay.
 *
 *			Usage:
 *			  DECLARE_BATTERY_MODEL(my_model) = {
 *				  .name = {'M', 'Y', ' ', 'B', 'A', 'T', 'T'},
 *				  .temps = {25.f},
 *				  (...)
 *			  };
 */
#define DECLARE_BATTERY_MODEL_SECONDARY(_name)                                                     \
	extern const struct battery_model secondary_battery_##_name;                               \
	const STRUCT_SECTION_ITERABLE(section_battery_models, _name) = {                           \
		.primary = false,                                                                  \
		.name = STRINGIFY(_name), .model = &secondary_battery_##_name,                     \
	};                                                                                         \
	const struct battery_model secondary_battery_##_name

/**
 * @brief nPM Fuel Gauge API
 * @defgroup npm_fuel_gauge_interface nPM Fuel Gauge
 * @ingroup fuel_gauge_interface_ext
 * @{
 */

/**
 * @brief Set the battery model for the nPM fuel gauge.
 *
 * @note This can be used instead of dts overlay for battery model assignment.
 *
 * @param dev Pointer to the device structure for the nPM fuel gauge.
 * @param model Pointer to the battery model to set.
 *
 * @return 0 on success, negative error code on failure.
 */
int npm_fuel_gauge_model_set(const struct device *dev, const void *model);

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* ZEPHYR_INCLUDE_DRIVERS_FUEL_GAUGE_NPM_FUEL_GAUGE_H_ */
