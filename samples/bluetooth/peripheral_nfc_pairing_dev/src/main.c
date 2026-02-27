/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
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

#include "advertising.h"
#if defined(CONFIG_BT_PAIRING_SECURITY_ENABLED)
#include "pairing.h"
#endif

#define CON_STATUS_LED DK_LED1
#define NFC_FIELD_LED  DK_LED2

#define NDEF_FILE_BUF_SIZE 256

static uint8_t ndef_file_buf[NDEF_FILE_BUF_SIZE];
static struct bt_le_oob oob_local;

static void nfc_callback(void *context, nfc_t4t_event_t event,
			 const uint8_t *data, size_t data_length, uint32_t flags)
{
	ARG_UNUSED(context);
	ARG_UNUSED(data);
	ARG_UNUSED(data_length);
	ARG_UNUSED(flags);

	switch (event) {
	case NFC_T4T_EVENT_FIELD_ON:
		printk("NFC: field ON\n");
		dk_set_led_on(NFC_FIELD_LED);
		break;
	case NFC_T4T_EVENT_FIELD_OFF:
		printk("NFC: field OFF\n");
		dk_set_led_off(NFC_FIELD_LED);
		break;
	case NFC_T4T_EVENT_NDEF_READ:
		printk("NFC: NDEF read\n");
		advertising_start();
		break;
	default:
		break;
	}
}

static int nfc_ndef_le_oob_encode(uint8_t *file_buf, size_t buf_size)
{
	int err;
	struct nfc_ndef_le_oob_rec_payload_desc rec_payload;

	memset(&rec_payload, 0, sizeof(rec_payload));
	rec_payload.addr = &oob_local.addr;
	rec_payload.local_name = bt_get_name();
	rec_payload.le_role = NFC_NDEF_LE_OOB_REC_LE_ROLE(
		NFC_NDEF_LE_OOB_REC_LE_ROLE_PERIPH_ONLY);
	rec_payload.appearance = NFC_NDEF_LE_OOB_REC_APPEARANCE(
		CONFIG_BT_DEVICE_APPEARANCE);
	rec_payload.flags = NFC_NDEF_LE_OOB_REC_FLAGS(BT_LE_AD_NO_BREDR);
#if defined(CONFIG_BT_PAIRING_SECURITY_ENABLED)
	rec_payload.le_sc_data = &oob_local.le_sc_data;
	rec_payload.tk_value = (uint8_t *)pairing_get_tk();
#endif

	printk("NFC: addr: %s\n", bt_addr_le_str(rec_payload.addr));
	printk("NFC: local_name: %s\n", rec_payload.local_name);
	printk("NFC: le_role: %d\n", *rec_payload.le_role);
	printk("NFC: appearance: %d\n", *rec_payload.appearance);
	printk("NFC: flags: %d\n", *rec_payload.flags);
	printk("NFC: le_sc_data confirm: %s\n", bt_hex(rec_payload.le_sc_data->c, sizeof(rec_payload.le_sc_data->c)));
	printk("NFC: le_sc_data random: %s\n", bt_hex(rec_payload.le_sc_data->r, sizeof(rec_payload.le_sc_data->r)));

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

static void nfc_init(void)
{
	int err;

	err = nfc_t4t_setup(nfc_callback, NULL);
	if (err) {
		printk("NFC: nfc_t4t_setup failed %d\n", err);
		return;
	}

	err = nfc_ndef_le_oob_encode(ndef_file_buf, sizeof(ndef_file_buf));
	if (err) {
		printk("NFC: ndef encode failed %d\n", err);
		return;
	}

	err = nfc_t4t_ndef_rwpayload_set(ndef_file_buf, sizeof(ndef_file_buf));
	if (err) {
		printk("NFC: nfc_t4t_ndef_rwpayload_set failed %d\n", err);
		return;
	}

	err = nfc_t4t_emulation_start();
	if (err) {
		printk("NFC: nfc_t4t_emulation_start failed %d\n", err);
		return;
	}

	printk("NFC initialized\n");
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		printk("Connection failed %s 0x%02x %s\n", addr, err,
		       bt_hci_err_to_str(err));
		return;
	}

	printk("Connected %s\n", addr);
	dk_set_led_on(CON_STATUS_LED);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	dk_set_led_off(CON_STATUS_LED);
	printk("Disconnected %s, reason 0x%02x %s\n", addr, reason,
	       bt_hci_err_to_str(reason));
}

static void recycled_cb(void)
{
	advertising_start();
}

#if defined(CONFIG_BT_PAIRING_SECURITY_ENABLED)
static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		printk("Security changed: %s level %u\n", addr, level);
	} else {
		printk("Security failed: %s level %u err %d %s\n", addr, level, err,
		       bt_security_err_to_str(err));
	}
}
#endif

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled_cb,
#if defined(CONFIG_BT_PAIRING_SECURITY_ENABLED)
	.security_changed = security_changed,
#endif
};

int main(void)
{
	int err;

	printk("Starting Bluetooth LE peripheral\n");
	printk("Build time" BUILD_TIMESTAMP "\n");

	err = dk_leds_init();
	if (err) {
		printk("LED init failed (err %d)\n", err);
		return 0;
	}

#if defined(CONFIG_BT_PAIRING_SECURITY_ENABLED)
	err = pairing_register();
	if (err) {
		printk("Failed to register pairing callbacks (err %d)\n", err);
		return 0;
	}
#endif

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

#if defined(CONFIG_BT_PAIRING_SECURITY_ENABLED)
	err = settings_load();
	if (err) {
		printk("Cannot load settings (err %d)\n", err);
	}
#endif

#if defined(CONFIG_BT_PAIRING_SECURITY_ENABLED)
	err = paring_key_generate(&oob_local);
	if (err) {
		printk("Failed to generate pairing keys (err %d)\n", err);
		return 0;
	}
#else
	err = bt_le_oob_get_local(BT_ID_DEFAULT, &oob_local);
	if (err) {
		printk("Failed to get local OOB data (err %d)\n", err);
		return 0;
	}
#endif /* CONFIG_BT_PAIRING_SECURITY_ENABLED */

	advertising_init();

	nfc_init();

	for (;;) {
		k_sleep(K_SECONDS(1));
	}
}
