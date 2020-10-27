#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

target_sources(app PRIVATE
		     ${CMAKE_CURRENT_SOURCE_DIR}/module_state_event.c)

target_sources(app PRIVATE
		     ${CMAKE_CURRENT_SOURCE_DIR}/potmeter_ctrl_event.c)

target_sources(app PRIVATE
		     ${CMAKE_CURRENT_SOURCE_DIR}/potmeter_data_event.c)

target_sources(app PRIVATE
		     ${CMAKE_CURRENT_SOURCE_DIR}/hx711_ctrl_event.c)

target_sources(app PRIVATE
		     ${CMAKE_CURRENT_SOURCE_DIR}/hx711_data_event.c)

target_sources(app PRIVATE
		     ${CMAKE_CURRENT_SOURCE_DIR}/tb6612fng_ctrl_event.c)
