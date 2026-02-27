#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(NONE)

string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S" UTC)
target_compile_definitions(app PRIVATE BUILD_TIMESTAMP="${BUILD_TIMESTAMP}")

# NORDIC SDK APP START
target_sources(app  PRIVATE 
  src/main.c
  src/advertising.c
)
target_sources_ifdef(CONFIG_BT_PAIRING_SECURITY_ENABLED app PRIVATE src/pairing.c)
# NORDIC SDK APP END
