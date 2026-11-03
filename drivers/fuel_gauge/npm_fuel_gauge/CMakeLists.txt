#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

zephyr_library_sources(npm_fuel_gauge.c)

# Iterable section in ROM containing battery models.
zephyr_linker_sources(SECTIONS section_battery_models.ld)
zephyr_iterable_section(NAME section_battery_models KVMA RAM_REGION GROUP RODATA_REGION)

add_subdirectory(pmic)
