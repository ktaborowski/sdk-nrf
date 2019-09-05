#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_NFC_TNEP_TAG tag.c)
zephyr_library_sources_ifdef(CONFIG_NFC_TNEP_POLLER poller.c)

if (CONFIG_NFC_TNEP_TAG OR
    CONFIG_NFC_TNEP_POLLER)
        zephyr_sources(protocol_timer.c)
endif()
