/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#define DT_DRV_COMPAT nordic_npm6001

#include <string.h>

#include <zephyr/types.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>

#include <lib_npm6001.h>

struct npm6001_config {
	struct i2c_dt_spec i2c_spec;
	const struct gpio_dt_spec gpio_n_int;
	uint16_t default_voltages[6];
};

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) > 0,
		 "No compatible nPM6001 instances found");

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) <= 1,
		 "Only one nPM6001 instance currently supported");

static int npm6001_init(const struct device *dev);

#define NPM6001_CFG_IRQ(inst)				\
	.gpio_n_int = GPIO_DT_SPEC_INST_GET(inst, irq_gpios)

#define NPM6001_DRIVER_INIT(inst)				    \
	static const struct npm6001_config drv_config_##inst = {	    \
		.i2c_spec = I2C_DT_SPEC_INST_GET(inst),		    \
		.default_voltages[LIB_NPM6001_BUCK0] = DT_INST_PROP(inst, buck0_voltage),		    \
		.default_voltages[LIB_NPM6001_BUCK1] = DT_INST_PROP(inst, buck1_voltage),		    \
		.default_voltages[LIB_NPM6001_BUCK2] = DT_INST_PROP(inst, buck2_voltage),		    \
		.default_voltages[LIB_NPM6001_BUCK3] = DT_INST_PROP(inst, buck3_voltage),		    \
		.default_voltages[LIB_NPM6001_LDO0] = DT_INST_PROP(inst, ldo0_voltage),		    \
		.default_voltages[LIB_NPM6001_LDO1] = DT_INST_PROP(inst, ldo1_voltage),		    \
		COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, irq_gpios), \
				(NPM6001_CFG_IRQ(inst)), ())		    \
	};							    \
	DEVICE_DT_INST_DEFINE(inst,				    \
				  &npm6001_init,			    \
				  NULL,				    \
				  NULL,			    \
				  &drv_config_##inst,		    \
				  POST_KERNEL,			    \
				  CONFIG_PMIC_NPM6001_INIT_PRIORITY,	    \
				  NULL);

DT_INST_FOREACH_STATUS_OKAY(NPM6001_DRIVER_INIT)

static int lib_npm6001_platform_init(void)
{
	if (device_is_ready(drv_config_0.i2c_spec.bus)) {
		return 0;
	} else {
		return -ENODEV;
	}
}

static int lib_npm6001_twi_read(uint8_t *buf, uint8_t len, uint8_t reg_addr)
{
	if (IS_ENABLED(CONFIG_TEST)) {
		memset(buf, 0, len);
		return 0;
	}

	return i2c_write_read_dt(&drv_config_0.i2c_spec,
		&reg_addr, sizeof(reg_addr),
		buf, len);
}

static int lib_npm6001_twi_write(const uint8_t *buf, uint8_t len, uint8_t reg_addr)
{
	struct i2c_msg msgs[] = {
		{.buf = &reg_addr, .len = sizeof(reg_addr), .flags = I2C_MSG_WRITE},
		{.buf = (uint8_t *)buf, .len = len, .flags = I2C_MSG_WRITE | I2C_MSG_STOP},
	};

	if (IS_ENABLED(CONFIG_TEST)) {
		return 0;
	}

	return i2c_transfer_dt(&drv_config_0.i2c_spec, &msgs[0], ARRAY_SIZE(msgs));
}

static int npm6001_init(const struct device *dev)
{
	const struct lib_npm6001_platform ncs_hw_funcs = {
		.lib_npm6001_platform_init = lib_npm6001_platform_init,
		.lib_npm6001_twi_read = lib_npm6001_twi_read,
		.lib_npm6001_twi_write = lib_npm6001_twi_write,
	};
	enum lib_npm6001_vreg reg_remainder[] = {
		LIB_NPM6001_BUCK1,
		LIB_NPM6001_BUCK2,
		LIB_NPM6001_BUCK3,
		LIB_NPM6001_LDO0,
	};
	const struct npm6001_config *const config = dev->config;
	int err;

	err = lib_npm6001_init(&ncs_hw_funcs);
	if (err) {
		return err;
	}

	/* BUCK0 is default on, and cannot be turned off. */
	err = lib_npm6001_vreg_voltage_set(
		LIB_NPM6001_BUCK0, config->default_voltages[LIB_NPM6001_BUCK0]);
	if (err) {
		return err;
	}

	/* LDO1 does not have configurable voltage */
	__ASSERT((config->default_voltages[LIB_NPM6001_LDO1] == 0) ||
			 (config->default_voltages[LIB_NPM6001_LDO1] == LIB_NPM6001_LDO1_MAXV),
			 "LDO1 only supports 1800 mV");
	if (config->default_voltages[LIB_NPM6001_LDO1] == 0) {
		err = lib_npm6001_vreg_disable(LIB_NPM6001_LDO1);
	} else {
		err = lib_npm6001_vreg_enable(LIB_NPM6001_LDO1);
	}
	if (err) {
		return err;
	}

	/* Configure remaining regulators */
	for (int i = 0; i < ARRAY_SIZE(reg_remainder); ++i) {
		if (config->default_voltages[reg_remainder[i]] == 0) {
			err = lib_npm6001_vreg_disable(reg_remainder[i]);
			if (err) {
				return err;
			}
		} else {
			err = lib_npm6001_vreg_voltage_set(
				reg_remainder[i], config->default_voltages[reg_remainder[i]]);
			if (err) {
				return err;
			}
			err = lib_npm6001_vreg_enable(reg_remainder[i]);
			if (err) {
				return err;
			}
		}
	}

	return 0;
}
