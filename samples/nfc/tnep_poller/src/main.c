/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <stdbool.h>
#include <string.h>
#include <misc/util.h>
#include <nfc/tnep/poller.h>
#include <nfc/ndef/nfc_ndef_msg.h>
#include <nfc/ndef/msg_parser.h>
#include <nfc/ndef/nfc_text_rec.h>
#include <logging/log.h>
#include <logging/log_ctrl.h>

LOG_MODULE_REGISTER(main);

char reqest_msg[] = "Request";
char response_msg[] = "Response";

static u8_t training_uri_one[] = "svc:one";
static u8_t training_uri_two[] = "svc:two";
static u8_t en_code[] = "en";

static u8_t data_field[1024];
static size_t data_field_len = sizeof(data_field);

NFC_TNEP_SERIVCE_SELECT_RECORD_DESC_DEF(deselect_service, 0, NULL);
NFC_NDEF_TEXT_RECORD_DESC_DEF(svc_one_rec, UTF_8, en_code, sizeof(en_code),
			      reqest_msg, sizeof(reqest_msg));
NFC_NDEF_TEXT_RECORD_DESC_DEF(tag_one_rec, UTF_8, en_code, sizeof(en_code),
			      response_msg, sizeof(response_msg));

u8_t svc_one_sel(void)
{
	LOG_INF("%s", __func__);

	return 0;
}
void svc_one_desel(void)
{
	LOG_INF("%s", __func__);

}
void svc_one_new_msg(void)
{
	LOG_INF("%s", __func__);
}
void svc_timeout(void)
{
	LOG_INF("%s", __func__);
}
void svc_error(int err_code)
{
	LOG_INF("%s. code %d", __func__, err_code);
}

NFC_TNEP_SERVICE_DEF(service_1, training_uri_one, ARRAY_SIZE(training_uri_one),
		     NFC_TNEP_COMM_MODE_SINGLE_RESPONSE, 40, 2, svc_one_sel,
		     svc_one_desel, svc_one_new_msg, svc_timeout, svc_error);
NFC_TNEP_SERIVCE_PARAM_RECORD_DESC_DEF(test_svc, 0x10, sizeof(training_uri_two),
				       training_uri_two, 0, 10, 3, 1024);
NFC_TNEP_STATUS_RECORD_DESC_DEF(status_success, NFC_TNEP_STATUS_SUCCESS);

struct nfc_tnep_service training_services[] = {
				NFC_TNEP_SERVICE(service_1),
		};

void check_service_message(int value)
{

	int err = 0;

	NFC_NDEF_MSG_DEF(my_service_msg, 1);

	switch (value) {
	case 0:
		/* Do nothing */
		break;
	case 1:
		LOG_DBG("Not supported");
		break;
	case 2:
		LOG_DBG("Not supported");
		break;
	case 3:
		LOG_DBG("TNEP Poller prepare INIT message");

		nfc_ndef_msg_clear(&NFC_NDEF_MSG(my_service_msg));

		err = nfc_ndef_msg_record_add(
				&NFC_NDEF_MSG(my_service_msg),
				&NFC_NDEF_TNEP_RECORD_DESC(service_1));

		if (err < 0) {
			LOG_DBG("Cannot add record!\n");
		}

		memset(data_field, 0x00, sizeof(data_field));

		data_field_len = sizeof(data_field);

		err = nfc_ndef_msg_encode(&NFC_NDEF_MSG(my_service_msg),
					  data_field, &data_field_len);
		if (err < 0) {
			LOG_DBG("Cannot encode message!\n");
		}

		/* NLEN field detection */
		if (IS_ENABLED(CONFIG_NFC_NDEF_MSG_WITH_NLEN)) {
			u32_t buffer_start = 0;

			buffer_start += NLEN_FIELD_SIZE;

			memcpy(data_field, &data_field[buffer_start],
			       data_field_len);
		}

		break;
	case 4:
		LOG_DBG("TNEP Poller Select Service 0");

		err = nfc_tnep_rw_svc_select(0);

		break;
	case 5:
		LOG_DBG("TNEP Poller Deselect Service");

		nfc_tnep_rw_svc_deselect();

		break;
	case 6:
		LOG_DBG("TNEP Status Success");

		nfc_ndef_msg_clear(&NFC_NDEF_MSG(my_service_msg));

		err = nfc_ndef_msg_record_add(
				&NFC_NDEF_MSG(my_service_msg),
				&NFC_NDEF_TNEP_RECORD_DESC(status_success));

		if (err < 0) {
			LOG_DBG("Cannot add record!\n");
		}

		memset(data_field, 0x00, sizeof(data_field));

		data_field_len = sizeof(data_field);

		err = nfc_ndef_msg_encode(&NFC_NDEF_MSG(my_service_msg),
					  data_field, &data_field_len);
		if (err < 0) {
			LOG_DBG("Cannot encode message!\n");
		}

		/* NLEN field detection */
		if (IS_ENABLED(CONFIG_NFC_NDEF_MSG_WITH_NLEN)) {
			u32_t buffer_start = 0;

			buffer_start += NLEN_FIELD_SIZE;

			memcpy(data_field, &data_field[buffer_start],
			       data_field_len);
		}

		break;
	case 7:
		LOG_DBG("TNEP Poller write message");
		nfc_tnep_rw_tx_msg_data(
				&NFC_NDEF_TEXT_RECORD_DESC(svc_one_rec));
		break;
	case 8:
		LOG_DBG("The Tag Response");

		nfc_ndef_msg_clear(&NFC_NDEF_MSG(my_service_msg));

		err = nfc_ndef_msg_record_add(
				&NFC_NDEF_MSG(my_service_msg),
				&NFC_NDEF_TEXT_RECORD_DESC(tag_one_rec));

		if (err < 0) {
			LOG_DBG("Cannot add record!\n");
		}

		memset(data_field, 0x00, sizeof(data_field));

		data_field_len = sizeof(data_field);

		err = nfc_ndef_msg_encode(&NFC_NDEF_MSG(my_service_msg),
					  data_field, &data_field_len);
		if (err < 0) {
			LOG_DBG("Cannot encode message!\n");
		}

		/* NLEN field detection */
		if (IS_ENABLED(CONFIG_NFC_NDEF_MSG_WITH_NLEN)) {
			u32_t buffer_start = 0;

			buffer_start += NLEN_FIELD_SIZE;

			memcpy(data_field, &data_field[buffer_start],
			       data_field_len);
		}

		break;
	default:
		LOG_DBG("%s Invalid argument %d", __func__, value);
		return;
	}
}

int mock_write(u8_t *buffer, size_t len)
{
	LOG_DBG("Mock write buffer %x, length %d", (u32_t)buffer, len);

	return 0;
}

int memcpy_write(u8_t *buffer, size_t len)
{
	LOG_DBG("Memcpy write");

	memset(data_field, 0, sizeof(data_field));

	void *res = memcpy(data_field, buffer, len);

	return ((u32_t) res - (u32_t) data_field);
}

/**
 * @brief   Function for application main entry.
 */
int main(void)
{
	LOG_INF("TNEP poller sample. Dev only");

	int err;

	log_init();

	/* TNEP init */
	nfc_tnep_rw_rx_msg_buffer_register(data_field, data_field_len);

	nfc_tnep_rw_init(training_services, ARRAY_SIZE(training_services),
			 memcpy_write);

	volatile int x = 0;

	/* loop */
	for (;;) {

		check_service_message(x);

		err = nfc_tnep_rw_process();

		if (!log_process(true)) {
			/* Do nothing */
		}
	}

	return 0;
}
/** @} */
