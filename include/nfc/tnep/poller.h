/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef NFC_TNEP_POLLER_H__
#define NFC_TNEP_POLLER_H__

/**
 * @file
 *
 * @defgroup nfc_tnep
 * @{
 * @ingroup nfc_api
 *
 * @brief Tag NDEF Exchange Protocol (TNEP) API.
 * The Reader/Writer device.
 *
 */

#include <nfc/tnep/base.h>

typedef int (*nfc_write_t)(u8_t*, size_t);

int nfc_tnep_rw_rx_msg_buffer_register(u8_t *rx_buffer, size_t len);

int nfc_tnep_rw_init(struct nfc_tnep_service *services, size_t services_amount,
		  nfc_write_t nfc_write);

void nfc_tnep_rw_uninit(void);

int nfc_tnep_rw_svc_select(u32_t svc_nr);

void nfc_tnep_rw_svc_deselect(void);

int nfc_tnep_rw_tx_msg_data(struct nfc_ndef_record_desc *record);

int nfc_tnep_rw_process(void);

/**
 * @}
 */

#endif /** NFC_TNEP_POLLER_H__ */
