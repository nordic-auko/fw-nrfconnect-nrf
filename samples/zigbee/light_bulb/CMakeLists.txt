#
# Copyright (c) 2020 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project("Zigbee light bulb with Scenes")

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/main.c
)
# NORDIC SDK APP END
