/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <nfc/tnep/poller.h>
#include <logging/log.h>
#include <nfc/ndef/tnep_rec.h>
#include <nfc/ndef/msg_parser.h>
#include "protocol_timer.h"
#include <errno.h>
#include "../../../samples/nfc/tnep_poller/build/zephyr/include/generated/autoconf.h"/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

LOG_MODULE_REGISTER(nfc_tnep_poller);

static size_t nfc_tnep_rx_buffer_len;
static u8_t *nfc_tnep_rx_buffer;

static u8_t nfc_tnep_tx_buffer[NFC_TNEP_MSG_MAX_SIZE];
NFC_NDEF_MSG_DEF(tnep_tx_msg, NFC_TNEP_MSG_MAX_RECORDS);

static size_t tnep_svcs_amount;
static struct nfc_tnep_service *tnep_svcs;
static volatile struct nfc_tnep_service *active_svc;

static nfc_write_t tnep_nfc_write;

enum nfc_tnep_sig_id {
	NFC_TNEP_SIG_SVC,
	NFC_TNEP_SIG_MSG_TX,
	NFC_TNEP_SIG_TIME,
	NFC_TNEP_SIG_MAX_NR
};

#define NFC_TNEP_SIG_SVC_DESELECT 0xFE

static struct k_poll_signal nfc_tnep_sig_svc;
static struct k_poll_signal nfc_tnep_sig_msg_tx;
static struct k_poll_event nfc_tnep_events[NFC_TNEP_SIG_MAX_NR];

#define TNEP_SIG_INIT(sig_name, sig_nr) do {				\
	k_poll_signal_init(&sig_name);					\
	k_poll_event_init(&nfc_tnep_events[sig_nr],			\
			K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,	\
			&sig_name);					\
} while (0)

enum tnep_state_name {
	TNEP_STATE_DISABLED,
	TNEP_STATE_SERVICE_READY,
	TNEP_STATE_SERVICE_SELECTED,
};

static enum tnep_state_name current_state = TNEP_STATE_DISABLED;

struct tnep_state {
	enum tnep_state_name name;
	void (*process)(enum nfc_tnep_sig_id);
};

static void tnep_error_check(int error_code)
{
	if (!error_code) {
		return;
	}

	LOG_DBG("TNEP error: %d", error_code);

	if (active_svc) {
		active_svc->callbacks->error_detected(error_code);
	}
}

int decode_service(u8_t *bin_rec, u32_t bin_rec_len,
		   struct nfc_ndef_tnep_svc_param *svc)
{
	if (!bin_rec || !svc) {
		return -EINVAL;
	}

	if (bin_rec_len < bin_rec[1] + 7) {
		return -ENOSR;
	}

	svc->tnep_version = *bin_rec++;

	svc->svc_name_uri_length = *bin_rec++;

	memcpy(svc->svc_name_uri, bin_rec, svc->svc_name_uri_length);
	bin_rec += svc->svc_name_uri_length;

	svc->communication_mode = *bin_rec++;

	svc->min_waiting_time = *bin_rec++;

	svc->max_waiting_time_ext = *bin_rec++;

	memcpy(&svc->max_message_size, bin_rec, 2);

	return 0;
}

static bool ndef_check_rec_type(const struct nfc_ndef_record_desc *record,
				const u8_t *type_field,
				const u32_t type_field_length)
{
	if (record->type_length != type_field_length) {
		return false;
	}

	int cmp_res = memcmp(record->type, type_field, type_field_length);

	return (cmp_res == 0);
}

static int tnep_rx_msg_svc_params_update(void)
{
	int err;

	/* Parse rx buffer to NDEF message */
	u8_t desc_buf[NFC_NDEF_PARSER_REQIRED_MEMO_SIZE_CALC(
			NFC_TNEP_MSG_MAX_RECORDS)];
	size_t desc_buf_len = sizeof(desc_buf);

	err = nfc_ndef_msg_parse(desc_buf, &desc_buf_len, nfc_tnep_rx_buffer,
				 &nfc_tnep_rx_buffer_len);
	if (err) {
		return -EINVAL;
	}

	const struct nfc_ndef_msg_desc *msg_p;

	msg_p = (struct nfc_ndef_msg_desc *) desc_buf;

	/* Search for Service Parameter Record */
	for (size_t i = 0; i < (msg_p->record_count); i++) {
		bool is_service_param_rec = ndef_check_rec_type(
				msg_p->record[i],
				nfc_ndef_tnep_rec_type_svc_param,
				NFC_NDEF_TNEP_REC_TYPE_LEN);

		if (is_service_param_rec) {
			/* Add service to services list */
			u8_t bin_rec[NFC_TNEP_RECORD_MAX_SZIE];

			u32_t bin_rec_len = sizeof(bin_rec);

			memset(bin_rec, 0x00, bin_rec_len);

			err = msg_p->record[i]->payload_constructor(
					msg_p->record[i]->payload_descriptor,
					bin_rec, &bin_rec_len);

			if (!bin_rec_len) {
				LOG_DBG("Service Parameter Record with 0 length payload");
				err = -EIO;
			}

			if (err) {
				LOG_DBG("Couldn't read Service Parameter Record");
				return err;
			}

			struct nfc_ndef_tnep_svc_param svc_param;
			u8_t svc_name[64];
			memset(svc_name, 0, sizeof(svc_name));

			svc_param.svc_name_uri = svc_name;

			decode_service(bin_rec, bin_rec_len, &svc_param);

			/* Check if it is such service and update it's parameters */
			for (int j = 0; j < tnep_svcs_amount; j++) {
				int uri_diff =
						strcmp(svc_param.svc_name_uri,
						       tnep_svcs[j].parameters->svc_name_uri);

				tnep_svcs[j].available = false;

				if (!uri_diff) {
					tnep_svcs[j].available = true;

					tnep_svcs[j].parameters->communication_mode =
							svc_param.communication_mode;
					tnep_svcs[j].parameters->min_waiting_time =
							svc_param.min_waiting_time;
					tnep_svcs[j].parameters->max_waiting_time_ext =
							svc_param.max_waiting_time_ext;
				}
			}
		}
	}

	return err;
}

static int tnep_rx_msg_status_get(u8_t *status)
{
	int err = -ENOENT;

	*status = NFC_TNEP_STATUS_PROTOCOL_ERROR;

	/* Parse rx buffer to NDEF message */
	u8_t desc_buf[NFC_NDEF_PARSER_REQIRED_MEMO_SIZE_CALC(
			NFC_TNEP_MSG_MAX_RECORDS)];
	size_t desc_buf_len = sizeof(desc_buf);

	err = nfc_ndef_msg_parse(desc_buf, &desc_buf_len, nfc_tnep_rx_buffer,
				 &nfc_tnep_rx_buffer_len);
	if (err) {
		return -EINVAL;
	}

	const struct nfc_ndef_msg_desc *msg_p;

	msg_p = (struct nfc_ndef_msg_desc *) desc_buf;

	/* Search for TNEP Status Record */
	for (size_t i = 0; i < (msg_p->record_count); i++) {
		bool is_status_rec = ndef_check_rec_type(
				msg_p->record[i], nfc_ndef_tnep_rec_type_status,
				NFC_NDEF_TNEP_REC_TYPE_LEN);

		if (is_status_rec) {
			/* Return status value */
			u8_t bin_rec[NFC_TNEP_RECORD_MAX_SZIE];

			u32_t bin_rec_len = sizeof(bin_rec);

			memset(bin_rec, 0x00, bin_rec_len);

			err = msg_p->record[i]->payload_constructor(
					msg_p->record[i]->payload_descriptor,
					bin_rec, &bin_rec_len);

			if (!bin_rec_len) {
				LOG_DBG("TNEP Status Record with 0 length payload");
				err = -EIO;
			}

			if (err) {
				LOG_DBG("Couldn't read TENP Status Record");
				return err;
			}

			/* Decode status */
			*status = bin_rec[0];

			err = 0;
		}
	}

	return err;
}

static int tnep_msg_select_svc(volatile struct nfc_tnep_service *svc)
{
	int err = 0;
	u8_t *uri;
	u8_t uri_len = 0;

	if (svc) {
		uri = svc->parameters->svc_name_uri;
		uri_len = svc->parameters->svc_name_uri_length;
	}

	NFC_TNEP_SERIVCE_SELECT_RECORD_DESC_DEF(svc_rec, uri_len, uri);

	err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(tnep_tx_msg),
				      &NFC_NDEF_TNEP_RECORD_DESC(svc_rec));

	if (err) {
		LOG_DBG("Can't add new record to tx msg. err %d", err);
		return err;
	}

	size_t len = NFC_TNEP_MSG_MAX_SIZE;
	err = nfc_ndef_msg_encode(&NFC_NDEF_MSG(tnep_tx_msg),
				  nfc_tnep_tx_buffer, &len);

	if (err) {
		LOG_DBG("Can't encode tx msg. err %d", err);
		return err;
	}

	err = tnep_nfc_write(nfc_tnep_tx_buffer, len);

	nfc_ndef_msg_clear(&NFC_NDEF_MSG(tnep_tx_msg));

	return err;
}

static void tnep_sm_disabled(enum nfc_tnep_sig_id signal_id)
{
	LOG_DBG("TNEP Disabled");
}

static void tnep_sm_service_ready(enum nfc_tnep_sig_id signal_id)
{
	LOG_DBG("TNEP Service Ready");

	int err = 0;

	switch (signal_id) {

	case NFC_TNEP_SIG_SVC:
		tnep_rx_msg_svc_params_update();

		const int svc_nr = nfc_tnep_sig_svc.result;

		if (svc_nr == NFC_TNEP_SIG_SVC_DESELECT) {
			LOG_DBG("Already in Service Ready State");
			break;
		}

		if (svc_nr < tnep_svcs_amount && tnep_svcs[svc_nr].available) {
			active_svc = &tnep_svcs[svc_nr];
		} else {
			LOG_DBG("No such service in the INIT Message. Id %d",
				svc_nr);
			err = -ENOENT;
		}

		tnep_msg_select_svc(active_svc);

		if (active_svc) {
			/* Restart timer with new parameters*/
			u8_t n =
					NFC_TNEP_MAX_EXEC_NO(
							active_svc->parameters->max_waiting_time_ext);
			size_t t_wait =
					NFC_TNEP_MIN_WAIT_TIME(
							active_svc->parameters->min_waiting_time);

			nfc_tnep_timer_stop();

			nfc_tnep_timer_init(t_wait, n);

			nfc_tnep_timer_start();
		}

		break;
	case NFC_TNEP_SIG_TIME:
		if (!active_svc) {
			LOG_DBG("Service timer signal while no service selected");
			err = -EACCES;
			break;
		}

		u8_t svc_status = NFC_TNEP_STATUS_PROTOCOL_ERROR;

		err = tnep_rx_msg_status_get(&svc_status);

		if (err) {
			LOG_DBG("TNEP Status Record read err %d", err);
			break;
		}

		if (svc_status == NFC_TNEP_STATUS_SUCCESS) {
			active_svc->callbacks->selected();

			current_state = TNEP_STATE_SERVICE_SELECTED;

			nfc_tnep_timer_stop();
		} else {
			active_svc->callbacks->error_detected(svc_status);
		}
		break;
	case NFC_TNEP_SIG_MSG_TX:
		LOG_DBG("Record will be send after service selection");
		k_poll_signal_raise(&nfc_tnep_sig_msg_tx, 1);
		break;
	default:
		err = -ENOTSUP;
		LOG_DBG("ERR: Service Ready: Unknown Signal Id %d", signal_id);
	}

	tnep_error_check(err);
}

static void tnep_sm_service_selected(enum nfc_tnep_sig_id signal_id)
{
	LOG_DBG("TNEP Service Selected");

	int err = 0;

	switch (signal_id) {
	case NFC_TNEP_SIG_SVC: {
		const int svc_nr = nfc_tnep_sig_svc.result;

		if (svc_nr != NFC_TNEP_SIG_SVC_DESELECT) {
			LOG_DBG("Only deselection is allowed in Service Selected state");
			break;
		}

		active_svc = NULL;

		tnep_msg_select_svc(active_svc);

		nfc_tnep_timer_stop();

		current_state = TNEP_STATE_SERVICE_READY;

		break;
	}
	case NFC_TNEP_SIG_MSG_TX: {
		u32_t len = sizeof(nfc_tnep_tx_buffer);
		err = nfc_ndef_msg_encode(&NFC_NDEF_MSG(tnep_tx_msg),
					  nfc_tnep_tx_buffer, &len);

		if (err) {
			LOG_DBG("Couldn't encode tx message. Err %d", err);
			break;
		}

		err = tnep_nfc_write(nfc_tnep_tx_buffer, len);

		if (err) {
			LOG_DBG("Couldn't write. Err %d", err);
			break;
		}

		nfc_ndef_msg_clear(&NFC_NDEF_MSG(tnep_tx_msg));

		nfc_tnep_timer_start();

		break;
	}
	case NFC_TNEP_SIG_TIME: {
		int buffer_change = memcmp(nfc_tnep_rx_buffer,
					   nfc_tnep_tx_buffer,
					   nfc_tnep_rx_buffer_len);

		if (buffer_change) {
			active_svc->callbacks->message_received();

			u8_t svc_status = NFC_TNEP_STATUS_PROTOCOL_ERROR;

			err = tnep_rx_msg_status_get(&svc_status);

			if (!err && (svc_status == NFC_TNEP_STATUS_SUCCESS)) {
				k_poll_signal_raise(&nfc_tnep_sig_svc,
				NFC_TNEP_SIG_SVC_DESELECT);
			}
		} else {
			if (nfc_tnep_sig_timer.result
					== NFC_TNEP_TMER_SIGNAL_TIMER_STOP) {
				k_poll_signal_raise(&nfc_tnep_sig_svc,
				NFC_TNEP_SIG_SVC_DESELECT);
			}
		}

		break;
	}
	default:
		err = -ENOTSUP;
		LOG_DBG("ERR: Service Selected: Unknown Event %d", signal_id);
	}

	tnep_error_check(err);
}

static const struct tnep_state tnep_state_machine[] = {
		{
				TNEP_STATE_DISABLED,
				tnep_sm_disabled },
		{
				TNEP_STATE_SERVICE_READY,
				tnep_sm_service_ready },
		{
				TNEP_STATE_SERVICE_SELECTED,
				tnep_sm_service_selected }, };

int nfc_tnep_rw_rx_msg_buffer_register(u8_t *rx_buffer, size_t len)
{
	if (!rx_buffer || !len) {
		LOG_DBG("Invalid buffer");
		return -EINVAL;
	}

	nfc_tnep_rx_buffer_len = len;
	nfc_tnep_rx_buffer = rx_buffer;

	return 0;
}

int nfc_tnep_rw_init(struct nfc_tnep_service *services, size_t services_amount,
		     nfc_write_t nfc_write)
{
	int err = 0;

	if (current_state != TNEP_STATE_DISABLED) {
		LOG_DBG("TNEP already running");
		return -ENOTSUP;
	}

	if (!services_amount || !services) {
		return -EINVAL;
	}

	tnep_svcs = services;
	tnep_svcs_amount = services_amount;

	if (!nfc_write) {
		LOG_DBG("No write function");
		return -EIO;
	}

	tnep_nfc_write = nfc_write;

	TNEP_SIG_INIT(nfc_tnep_sig_timer, NFC_TNEP_SIG_TIME);
	TNEP_SIG_INIT(nfc_tnep_sig_svc, NFC_TNEP_SIG_SVC);
	TNEP_SIG_INIT(nfc_tnep_sig_msg_tx, NFC_TNEP_SIG_MSG_TX);

	current_state = TNEP_STATE_SERVICE_READY;

	return err;
}

void nfc_tnep_rw_uninit(void)
{
	for (int j = 0; j < tnep_svcs_amount; j++) {
		tnep_svcs[j].available = false;
	}

	active_svc = NULL;
	tnep_nfc_write = NULL;
	nfc_tnep_rx_buffer_len = 0;
	nfc_tnep_rx_buffer = NULL;

	current_state = TNEP_STATE_DISABLED;
}

int nfc_tnep_rw_svc_select(u32_t svc_nr)
{
	if (active_svc) {
		LOG_DBG("deselect service before selecting new one");
		return -EACCES;
	}

	if (svc_nr > tnep_svcs_amount) {
		return -EINVAL;
	}

	k_poll_signal_raise(&nfc_tnep_sig_svc, svc_nr);

	return 0;
}

void nfc_tnep_rw_svc_deselect(void)
{
	k_poll_signal_raise(&nfc_tnep_sig_svc, NFC_TNEP_SIG_SVC_DESELECT);
}

int nfc_tnep_rw_tx_msg_data(struct nfc_ndef_record_desc *record)
{
	int err = 0;

	err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(tnep_tx_msg), record);

	if (err) {
		LOG_DBG("Couldn't add tx record. Err %d", err);
		return err;
	}

	k_poll_signal_raise(&nfc_tnep_sig_msg_tx, 1);

	return 0;
}

int nfc_tnep_rw_process(void)
{
	int err;

	err = k_poll(nfc_tnep_events, NFC_TNEP_SIG_MAX_NR, K_MSEC(100));

	if (err) {
		return err;
	}

	/* Check for signals */
	for (size_t sig_nr = 0; sig_nr < NFC_TNEP_SIG_MAX_NR; sig_nr++) {

		if (nfc_tnep_events[sig_nr].state == K_POLL_STATE_SIGNALED) {
			/* Clear signal */
			nfc_tnep_events[sig_nr].signal->signaled = 0;
			nfc_tnep_events[sig_nr].state = K_POLL_STATE_NOT_READY;

			/* Run TNEP State Machine */
			tnep_state_machine[current_state].process(sig_nr);
		}
	}

	return err;
}
