#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Apply low-power netcore (ipc_radio) configuration when building with BLE
# on nRF5340: disable unused peripherals, reduce RAM, minimize assert/log.
#

if(SB_CONFIG_NETCORE_IPC_RADIO AND SB_CONFIG_BOARD_NRF5340DK)
  set(_netcore_conf "${CMAKE_CURRENT_LIST_DIR}/sysbuild/ipc_radio/boards/nrf5340dk_nrf5340_cpunet.conf")
  set(_netcore_overlay "${CMAKE_CURRENT_LIST_DIR}/sysbuild/ipc_radio/boards/nrf5340dk_nrf5340_cpunet.overlay")

  if(EXISTS "${_netcore_conf}")
    set(ipc_radio_EXTRA_CONF_FILE "${ipc_radio_EXTRA_CONF_FILE};${_netcore_conf}" CACHE INTERNAL "netcore low-power conf")
  endif()
  if(EXISTS "${_netcore_overlay}")
    set(ipc_radio_EXTRA_DTC_OVERLAY_FILE "${ipc_radio_EXTRA_DTC_OVERLAY_FILE};${_netcore_overlay}" CACHE INTERNAL "netcore low-power overlay")
  endif()
endif()
