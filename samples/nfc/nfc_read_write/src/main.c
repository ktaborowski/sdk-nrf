/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <stdbool.h>
#include <nfc_t4t_lib.h>
#include <string.h>
#include <nfc/tnep/tag.h>
#include <nfc/ndef/nfc_ndef_msg.h>
#include <nfc_t4t_lib.h>
#include <dk_buttons_and_leds.h>
#include <nfc/ndef/msg_parser.h>
#include <logging/log.h>
#include <logging/log_ctrl.h>
#include <misc/util.h>
#include <device.h>
#include <gpio.h>
#include <dk_buttons_and_leds.h>

LOG_MODULE_REGISTER(app);

u8_t msg[] = "my message";
u8_t bit_type[] = "N";
static u8_t training_uri_one[] = "urn:nfc:one"; /* uri for testing*/
static u8_t training_uri_two[] = "urn:nfc:two";
static u8_t training_uri_three[] = "urn:nfc:three";
static u8_t tag_buffer[1024];
static size_t tag_buffer_size = sizeof(tag_buffer);

NFC_TNEP_SERIVCE_SELECT_RECORD_DESC_DEF(deselect_service, 0, NULL);
NFC_NDEF_RECORD_BIN_DATA_DEF(bin_data_rec, TNF_WELL_KNOWN, NULL, 0, bit_type,
			     sizeof(bit_type), msg, sizeof(msg));

u8_t training_service_selected(void)
{
	LOG_INF("%s", __func__);
	return 0;
}
void training_service_deselected(void)
{
	LOG_INF("%s", __func__);
}
void training_service_new_message(void)
{
	LOG_INF("%s", __func__);
}
void training_service_timeout(void)
{
	LOG_INF("%s", __func__);
}
void training_service_error(int err_code)
{
	LOG_INF("%s. code %d", __func__, err_code);
}

void training_service_new_message_replay(void)
{
	LOG_INF("writing replay message");

	nfc_tnep_tx_msg_app_data(&NFC_NDEF_RECORD_BIN_DATA(bin_data_rec));
}

NFC_TNEP_SERVICE_DEF(training_1, training_uri_one, ARRAY_SIZE(training_uri_one),
		     NFC_TNEP_COMM_MODE_SINGLE_RESPONSE, 200, 4,
		     training_service_selected, training_service_deselected,
		     training_service_new_message, training_service_timeout,
		     training_service_error);

NFC_TNEP_SERVICE_DEF(training_2, training_uri_two, ARRAY_SIZE(training_uri_two),
		     NFC_TNEP_COMM_MODE_SINGLE_RESPONSE, 200, 4,
		     training_service_selected, training_service_deselected,
		     training_service_new_message_replay,
		     training_service_timeout, training_service_error);

NFC_TNEP_SERVICE_DEF(training_3, training_uri_three,
		     ARRAY_SIZE(training_uri_three),
		     NFC_TNEP_COMM_MODE_SINGLE_RESPONSE, 250, 15,
		     training_service_selected, training_service_deselected,
		     training_service_new_message, training_service_timeout,
		     training_service_error);

struct nfc_tnep_service main_services[] = {
				NFC_TNEP_SERVICE(training_1),
				NFC_TNEP_SERVICE(training_2),
				NFC_TNEP_SERVICE(training_3)
		};

static void nfc_callback(void *context, enum nfc_t4t_event event,
			 const u8_t *data, size_t data_length, u32_t flags)
{
	switch (event) {
	case NFC_T4T_EVENT_NDEF_READ:
		break;
	case NFC_T4T_EVENT_NDEF_UPDATED: {
		u8_t *ndef_msg = tag_buffer;

		tag_buffer_size = sizeof(tag_buffer);

		if (IS_ENABLED(CONFIG_NFC_NDEF_MSG_WITH_NLEN)) {
			ndef_msg += NLEN_FIELD_SIZE;
			/*ndef_msg_len -= NLEN_FIELD_SIZE;*/
		}

		nfc_tnep_rx_msg_indicate(ndef_msg, tag_buffer_size);
		break;
	}
	case NFC_T4T_EVENT_DATA_TRANSMITTED:
	case NFC_T4T_EVENT_DATA_IND:
	case NFC_T4T_EVENT_NONE:
	case NFC_T4T_EVENT_FIELD_ON:
	case NFC_T4T_EVENT_FIELD_OFF:
	default:
		/* This block intentionally left blank. */
		break;
	}
}

void check_service_message(int value)
{

	int err = -1;

	NFC_TNEP_SERIVCE_SELECT_RECORD_DESC_DEF(
			my_service_1,
			main_services[0].parameters->svc_name_uri_length,
			main_services[0].parameters->svc_name_uri);

	NFC_TNEP_SERIVCE_SELECT_RECORD_DESC_DEF(
			my_service_2,
			main_services[1].parameters->svc_name_uri_length,
			main_services[1].parameters->svc_name_uri);

	NFC_NDEF_MSG_DEF(my_service_msg, 1);

	switch (value) {
	case 1:
		err = nfc_ndef_msg_record_add(
				&NFC_NDEF_MSG(my_service_msg),
				&NFC_NDEF_TNEP_RECORD_DESC(my_service_1));
		break;
	case 2:
		err = nfc_ndef_msg_record_add(
				&NFC_NDEF_MSG(my_service_msg),
				&NFC_NDEF_TNEP_RECORD_DESC(deselect_service));
		break;
	case 3:
		err = nfc_ndef_msg_record_add(
				&NFC_NDEF_MSG(my_service_msg),
				&NFC_NDEF_RECORD_BIN_DATA(bin_data_rec));
		break;
	case 4:
		err = nfc_ndef_msg_record_add(
				&NFC_NDEF_MSG(my_service_msg),
				&NFC_NDEF_TNEP_RECORD_DESC(my_service_2));
		break;
	default:
		printk("check service message argument error\n");
		break;
	}

	if (err < 0) {
		printk("Cannot add record!\n");
	}

	memset(tag_buffer, 0x00, sizeof(tag_buffer));

	tag_buffer_size = sizeof(tag_buffer);

	err = nfc_ndef_msg_encode(&NFC_NDEF_MSG(my_service_msg), tag_buffer,
				  &tag_buffer_size);
	if (err < 0) {
		printk("Cannot encode message!\n");
	}

	/* NLEN field detection */
	if (IS_ENABLED(CONFIG_NFC_NDEF_MSG_WITH_NLEN)) {
		u32_t buffer_start = 0;

		buffer_start += NLEN_FIELD_SIZE;

		memcpy(tag_buffer, &tag_buffer[buffer_start], tag_buffer_size);
	}
}

static void button_pressed(u32_t button_state, u32_t has_changed)
{
	u32_t button = button_state & has_changed;

	if (button & DK_BTN1_MSK) {
		check_service_message(1);

		nfc_tnep_rx_msg_indicate(tag_buffer, tag_buffer_size);
	}

	if (button & DK_BTN2_MSK) {
		check_service_message(2);

		nfc_tnep_rx_msg_indicate(tag_buffer, tag_buffer_size);
	}

	if (button & DK_BTN3_MSK) {
		check_service_message(3);

		nfc_tnep_rx_msg_indicate(tag_buffer, tag_buffer_size);
	}

	if (button & DK_BTN4_MSK) {
		check_service_message(4);

		nfc_tnep_rx_msg_indicate(tag_buffer, tag_buffer_size);
	}
}

/**
 * @brief   Function for application main entry.
 */
int main(void)
{
	printk("nfc read write demo\n");

	int err;

	log_init();

	err = dk_buttons_init(button_pressed);

	if (err) {
		printk("buttons init error %d", err);
	}

	/* TNEP init */

	nfc_tnep_tx_msg_buffer_register(tag_buffer, tag_buffer_size);

	nfc_tnep_init(main_services, ARRAY_SIZE(main_services));

	nfc_tnep_tx_msg_app_data(&NFC_NDEF_RECORD_BIN_DATA(bin_data_rec));

	/* Type X Tag init */

	err = nfc_t4t_setup(nfc_callback, NULL);
	if (err < 0) {
		LOG_ERR("nfc_t4t_setup");
		return err;
	}

	err = nfc_t4t_ndef_rwpayload_set(tag_buffer, tag_buffer_size);
	if (err < 0) {
		LOG_ERR("nfc_t4t_ndef_rwpayload_set");
		return err;
	}

	err = nfc_t4t_emulation_start();
	if (err < 0) {
		LOG_ERR("nfc_t4t_emulation_start");
		return err;
	}

	/* loop */
	for (;;) {

		err = nfc_tnep_process();

		if (!log_process(true)) {
			__WFE();
		}
	}

	return 0;
}
/** @} */
