/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef NFC_TNEP_TAG_H__
#define NFC_TNEP_TAG_H__

/**
 * @file
 *
 * @defgroup nfc_tnep
 * @{
 * @ingroup nfc_api
 *
 * @brief Tag NDEF Exchange Protocol (TNEP) API.
 * The NFC Tag Device. *
 */

#include <nfc/tnep/base.h>

#define NFC_TNEP_EVENTS_NUMBER 3
#define NFC_TNEP_MSG_ADD_REC_TIMEOUT 100

/**
 * @brief  Register TNEP message buffer.
 *
 * @param tx_buffer Pointer to NDEF message buffer.
 * @param len Length of NDEF message buffer.
 *
 * @retval -EINVAL When buffer not exist.
 * @retval 0 When success.
 */
int nfc_tnep_tx_msg_buffer_register(u8_t *tx_buffer, size_t len);

/**
 * @brief Start communication using TNEP.
 *
 * @param services Pointer to the first service information structure.
 * @param services_amount Number of services in application.
 *
 * @retval 0 Success.
 * @retval -ENOTSUP TNEP already started.
 * @retval -EINVAL Invalid argument.
 * @retval -EIO No output NDEF message buffer registered.
 */
int nfc_tnep_init(struct nfc_tnep_service *services, size_t services_amount);

/**
 * @brief Stop TNEP communication
 */
void nfc_tnep_uninit(void);

/**
 * @brief Waiting for a signals to execute protocol logic.
 *
 * @retval error code. 0 if success.
 */
int nfc_tnep_process(void);

/**
 * @brief  Indicate about new TNEP message available in buffer.
 *
 * @details The NFC Tag Device concludes that the NDEF Write Procedure
 * is finished when, after a write command of the NDEF Write Procedure,
 * an NDEF message is available in the data area. If, after the write command
 * of the NDEF Write Procedure, no NDEF message is available in the data area,
 * the NFC Tag Device concludes that the actual NDEF Write Procedure
 * is ongoing.
 *
 * @param rx_buffer Pointer to NDEF message buffer.
 * @param len Length of NDEF message buffer.
 */
void nfc_tnep_rx_msg_indicate(u8_t *rx_buffer, size_t len);

/**
 * @brief Add application data record to next message.
 *
 * @param record Pointer to application data record.
 * @param is_last_app_record Set true if this is there are no more application
 * data to transmit until next NDEF Write event occur.
 *
 * @retval error code. 0 if success.
 */
int nfc_tnep_tx_msg_app_data(struct nfc_ndef_record_desc *record);

/**
 * If the NDEF application on the NFC Tag Device has finished,
 * and therefore the NFC Tag Device has no more application data
 * available for the Reader/Writer, then the NFC Tag Device SHALL
 * provide a Status message containing a single record that is
 * a TNEP Status record indicating success.
 */
void nfc_tnep_tx_msg_no_app_data(void);

/**
 * @}
 */

#endif /** NFC_TNEP_TAG_H__ */

