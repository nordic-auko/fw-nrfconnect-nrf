#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

zephyr_library_sources_ifdef(CONFIG_NPM10XX_ADC npm10xx.c)
zephyr_library_sources_ifdef(CONFIG_NPM13XX_CHARGER npm13xx.c)
zephyr_library_sources_ifdef(CONFIG_NPM2100_VBAT npm2100.c)
