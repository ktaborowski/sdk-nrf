/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/kernel.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <common/bt_str.h>

#include <zephyr/settings/settings.h>

#include <dk_buttons_and_leds.h>

#include <nfc_t4t_lib.h>
#include <nfc/t4t/ndef_file.h>
#include <nfc/ndef/msg.h>
#include <nfc/ndef/le_oob_rec.h>

#define LED_STATUS_BLE DK_LED1
#define LED_STATUS_NFC DK_LED2
#define BUTTON_BOND_REMOVE DK_BTN4_MSK

#define DEVICE_NAME	CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define NDEF_FILE_BUF_SIZE 256
#define BLE_SC_DATA_SIZE   16

static struct bt_le_oob oob_local;
static uint8_t ndef_file_buf[NDEF_FILE_BUF_SIZE];

static struct k_work adv_work;

static const struct bt_data ad[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

/* NFC */

static int nfc_ndef_le_oob_encode(uint8_t *file_buf, size_t buf_size)
{
	int err;
	struct nfc_ndef_le_oob_rec_payload_desc rec_payload;
	uint8_t tk_value[NFC_NDEF_LE_OOB_REC_TK_LEN] = {0};

	err = bt_le_oob_get_local(BT_ID_DEFAULT, &oob_local);
	if (err) {
		printk("NFC: bt_le_oob_get_local failed %d\n", err);
		return err;
	}

	memset(&rec_payload, 0, sizeof(rec_payload));
	rec_payload.addr = &oob_local.addr;
	rec_payload.local_name = bt_get_name();
	rec_payload.le_role = NFC_NDEF_LE_OOB_REC_LE_ROLE(NFC_NDEF_LE_OOB_REC_LE_ROLE_PERIPH_ONLY);
	rec_payload.appearance = NFC_NDEF_LE_OOB_REC_APPEARANCE(CONFIG_BT_DEVICE_APPEARANCE);
	rec_payload.flags = NFC_NDEF_LE_OOB_REC_FLAGS(BT_LE_AD_NO_BREDR);
	rec_payload.le_sc_data = &oob_local.le_sc_data;
	rec_payload.tk_value = tk_value;

	printk("NFC: addr: %s\n", bt_addr_le_str(rec_payload.addr));
	printk("NFC: local_name: %s\n", rec_payload.local_name);
	printk("NFC: le_role: %d\n", *rec_payload.le_role);
	printk("NFC: appearance: %d\n", *rec_payload.appearance);
	printk("NFC: flags: %d\n", *rec_payload.flags);
	printk("NFC: le_sc_data confirm (last 4 bytes): %02X %02X %02X %02X\n",
	       rec_payload.le_sc_data->c[BLE_SC_DATA_SIZE - 4],
	       rec_payload.le_sc_data->c[BLE_SC_DATA_SIZE - 3],
	       rec_payload.le_sc_data->c[BLE_SC_DATA_SIZE - 2],
	       rec_payload.le_sc_data->c[BLE_SC_DATA_SIZE - 1]);
	printk("NFC: le_sc_data random (last 4 bytes): %02X %02X %02X %02X\n",
	       rec_payload.le_sc_data->r[BLE_SC_DATA_SIZE - 4],
	       rec_payload.le_sc_data->r[BLE_SC_DATA_SIZE - 3],
	       rec_payload.le_sc_data->r[BLE_SC_DATA_SIZE - 2],
	       rec_payload.le_sc_data->r[BLE_SC_DATA_SIZE - 1]);

	NFC_NDEF_MSG_DEF(nfc_le_oob_msg, 1);
	NFC_NDEF_LE_OOB_RECORD_DESC_DEF(nfc_le_oob_rec, 0, &rec_payload);

	err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_le_oob_msg),
				      &NFC_NDEF_LE_OOB_RECORD_DESC(nfc_le_oob_rec));
	if (err) {
		printk("NFC: ndef_msg_record_add failed %d\n", err);
		return err;
	}

	uint32_t msg_len = nfc_t4t_ndef_file_msg_size_get(buf_size);
	uint8_t *msg_buf = nfc_t4t_ndef_file_msg_get(file_buf);

	err = nfc_ndef_msg_encode(&NFC_NDEF_MSG(nfc_le_oob_msg), msg_buf, &msg_len);
	if (err) {
		printk("NFC: ndef_msg_encode failed %d\n", err);
		return err;
	}

	err = nfc_t4t_ndef_file_encode(file_buf, &msg_len);
	if (err) {
		printk("NFC: ndef_file_encode failed %d\n", err);
		return err;
	}
	return 0;
}

static void nfc_callback(void *context, nfc_t4t_event_t event, const uint8_t *data,
			 size_t data_length, uint32_t flags)
{
	ARG_UNUSED(context);
	ARG_UNUSED(data);
	ARG_UNUSED(data_length);
	ARG_UNUSED(flags);

	switch (event) {
	case NFC_T4T_EVENT_FIELD_ON:
		dk_set_led_on(LED_STATUS_NFC);
		break;
	case NFC_T4T_EVENT_FIELD_OFF:
		dk_set_led_off(LED_STATUS_NFC);
		break;
	case NFC_T4T_EVENT_NDEF_READ:
		printk("NFC: NDEF read\n");
		k_work_submit(&adv_work);
		break;
	default:
		break;
	}
}

static int nfc_init(void)
{
	int err;

	err = nfc_t4t_setup(nfc_callback, NULL);
	if (err) {
		printk("NFC: nfc_t4t_setup failed %d\n", err);
		return err;
	}

	err = nfc_ndef_le_oob_encode(ndef_file_buf, sizeof(ndef_file_buf));
	if (err) {
		printk("NFC: ndef encode failed %d\n", err);
		return err;
	}

	err = nfc_t4t_ndef_rwpayload_set(ndef_file_buf, sizeof(ndef_file_buf));
	if (err) {
		printk("NFC: nfc_t4t_ndef_rwpayload_set failed %d\n", err);
		return err;
	}

	err = nfc_t4t_emulation_start();
	if (err) {
		printk("NFC: nfc_t4t_emulation_start failed %d\n", err);
		return err;
	}

	printk("NFC initialized\n");

	return 0;
}

/* Bluetooth LE */

static void adv_work_handler(struct k_work *work)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);

	if (err && err != -EALREADY) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}
	printk("Advertising started\n");
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed 0x%02x %s\n", err, bt_hci_err_to_str(err));
		return;
	}

	printk("Connected\n");
	dk_set_led_on(LED_STATUS_BLE);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected, reason 0x%02x %s\n", reason, bt_hci_err_to_str(reason));
	dk_set_led_off(LED_STATUS_BLE);
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	if (!err) {
		printk("Security changed: level %u\n", level);
	} else {
		printk("Security failed: level %u err %d %s\n", level, err,
		       bt_security_err_to_str(err));
	}
}

static void auth_oob_data_request(struct bt_conn *conn, struct bt_conn_oob_info *info)
{
	int err;

	if (info->type == BT_CONN_OOB_LE_SC) {
		err = bt_le_oob_set_sc_data(conn, &oob_local.le_sc_data, NULL);
		if (err) {
			printk("Auth failed: LESC OOB set error: %d\n", err);
		}

		printk("Auth success\n");
		return;
	}

	printk("Auth failed: Unsupported OOB type %u\n", info->type);
}

static void auth_cancel(struct bt_conn *conn)
{
	printk("Pairing cancelled\n");
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	printk("Pairing completed, bonded: %d\n", bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	printk("Pairing failed, reason %d %s\n", reason, bt_security_err_to_str(reason));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.cancel = auth_cancel,
	.oob_data_request = auth_oob_data_request,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

static int bluetooth_init(void)
{
	int err;

	err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (err) {
		printk("Failed to register authorization callbacks (err %d)\n", err);
		return err;
	}

	err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
	if (err) {
		printk("Failed to register authorization info callbacks (err %d)\n", err);
	}

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return err;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		err = settings_load();
		if (err) {
			printk("Cannot load settings (err %d)\n", err);
		}
	}

	k_work_init(&adv_work, adv_work_handler);

	printk("Bluetooth initialized\n");

	return 0;
}

/* Buttons */
void button_changed(uint32_t button_state, uint32_t has_changed)
{
	int err;
	uint32_t buttons = button_state & has_changed;

	if (buttons & BUTTON_BOND_REMOVE) {
		err = bt_unpair(BT_ID_DEFAULT, NULL);
		if (err) {
			printk("Bond remove failed err: %d\n", err);
		} else {
			printk("All bond removed\n");
		}
	}

}

int main(void)
{
	int err;

	printk("Starting Bluetooth NFC Pairing Reference sample\n");

	err = dk_leds_init();
	if (err) {
		printk("LED init failed (err %d)\n", err);
	}

	err = dk_buttons_init(button_changed);
	if (err) {
		printk("Buttons init failed (err %d)\n", err);
	}

	err = bluetooth_init();
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
	}

	err = nfc_init();
	if (err) {
		printk("NFC init failed (err %d)\n", err);
	}

	return 0;
}