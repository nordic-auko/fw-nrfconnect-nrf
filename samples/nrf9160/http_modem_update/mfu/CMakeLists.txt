#
# Copyright (c) 2019 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

target_sources_ifdef(CONFIG_MFU_ENABLE
		     app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mfu.c)

target_sources_ifdef(CONFIG_MFU_ENABLE
		     app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mfu_types.c)

target_sources_ifdef(CONFIG_MFU_ENABLE
		     app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mfu_state_ncs_settings.c)

target_sources_ifdef(CONFIG_MFU_ENABLE
		     app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modem_dfu_rpc.c)
