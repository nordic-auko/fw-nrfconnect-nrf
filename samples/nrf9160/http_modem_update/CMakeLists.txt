#
# Copyright (c) 2019 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

cmake_minimum_required(VERSION 3.8.2)

include($ENV{ZEPHYR_BASE}/../nrf/cmake/boilerplate.cmake)
include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(http_application_update)

# NORDIC SDK APP START
target_sources(app PRIVATE src/main.c)
add_subdirectory(mfu)
zephyr_library_include_directories(
  mfu
  )

# Get direct access to the API provided by dfu_target_flash
target_include_directories(app
  PRIVATE
  ${ZEPHYR_BASE}/../nrf/subsys/dfu/include
  )
# NORDIC SDK APP END
