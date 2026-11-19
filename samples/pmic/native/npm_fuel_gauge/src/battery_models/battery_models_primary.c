/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <drivers/fuel_gauge/npm_fuel_gauge.h>

#include <nrf_fuel_gauge.h>

DECLARE_BATTERY_MODEL_PRIMARY(aa_alkaline) = {
#include <battery_models/primary_cell/AA_Alkaline.inc>
};

DECLARE_BATTERY_MODEL_PRIMARY(aaa_alkaline) = {
#include <battery_models/primary_cell/AAA_Alkaline.inc>
};

DECLARE_BATTERY_MODEL_PRIMARY(twosaa_alkaline) = {
#include <battery_models/primary_cell/2SAA_Alkaline.inc>
};

DECLARE_BATTERY_MODEL_PRIMARY(twosaaa_alkaline) = {
#include <battery_models/primary_cell/2SAAA_Alkaline.inc>
};

DECLARE_BATTERY_MODEL_PRIMARY(cr2032) = {
#include <battery_models/primary_cell/CR2032.inc>
};

DECLARE_BATTERY_MODEL_PRIMARY(lr44) = {
#include <battery_models/primary_cell/LR44.inc>
};
