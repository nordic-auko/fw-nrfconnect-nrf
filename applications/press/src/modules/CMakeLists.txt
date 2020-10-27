#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/potmeter_handler.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/hx711_handler.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tb6612fng_handler.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/display_handler.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/controller.c)
