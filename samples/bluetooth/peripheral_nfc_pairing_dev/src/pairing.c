/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include <common/bt_str.h>
#include <nfc/ndef/le_oob_rec.h>

#include "pairing.h"

#define AUTH_SC_FLAG 0x08

static struct bt_le_oob *oob_local_ptr;
static uint8_t tk_value[NFC_NDEF_LE_OOB_REC_TK_LEN];

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static void auth_oob_data_request(struct bt_conn *conn, struct bt_conn_oob_info *info)
{
	int err;
	struct bt_conn_info conn_info;
	char addr[BT_ADDR_LE_STR_LEN];

	printk("Auth OOB data request type: %d\n", info->type);

	if (!oob_local_ptr) {
		printk("Auth failed: No local OOB data\n");
		return;
	}

	if (info->type == BT_CONN_OOB_LE_SC) {
		err = bt_conn_get_info(conn, &conn_info);
		if (err) {
			printk("Auth failed: Failed to get connection info: %d\n", err);
			return;
		}

		if (bt_addr_le_cmp(conn_info.le.local, &oob_local_ptr->addr)) {
			bt_addr_le_to_str(conn_info.le.local, addr, sizeof(addr));
			printk("Auth failed: No OOB data available for local %s", addr);
			bt_conn_auth_cancel(conn);
			return;
		}

		if (bt_addr_le_cmp(conn_info.le.local, &oob_local_ptr->addr)) {
			printk("Auth failed: Local addr mismatch for LESC OOB\n");
			bt_conn_auth_cancel(conn);
			return;
		}
		err = bt_le_oob_set_sc_data(conn, &oob_local_ptr->le_sc_data, NULL);
		if (err) {
			printk("Auth failed: LESC OOB set error: %d\n", err);
		}
		return;
	}

#if !defined(CONFIG_BT_SMP_SC_PAIR_ONLY)
	if (info->type == BT_CONN_OOB_LE_LEGACY) {
		printk("Legacy TK value requested\n");
		err = bt_le_oob_set_legacy_tk(conn, tk_value);
		if (err) {
			printk("TK value set error: %d\n", err);
		}
		return;
	}
#endif

	printk("Auth failed: Unsupported OOB type %u\n", info->type);
}

static enum bt_security_err pairing_accept(struct bt_conn *conn,
					   const struct bt_conn_pairing_feat *feat)
{
	if (feat->oob_data_flag && (!(feat->auth_req & AUTH_SC_FLAG))) {
		bt_le_oob_set_legacy_flag(true);
	}
	return BT_SECURITY_ERR_SUCCESS;
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing completed: %s, bonded: %d\n", addr, bonded);
	bt_le_oob_set_sc_flag(false);
#if !defined(CONFIG_BT_SMP_SC_PAIR_ONLY)
	bt_le_oob_set_legacy_flag(false);
#endif
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing failed conn: %s, reason %d %s\n", addr, reason,
	       bt_security_err_to_str(reason));
	bt_le_oob_set_sc_flag(false);
#if !defined(CONFIG_BT_SMP_SC_PAIR_ONLY)
	bt_le_oob_set_legacy_flag(false);
#endif
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.cancel = auth_cancel,
	.oob_data_request = auth_oob_data_request,
	.pairing_accept = pairing_accept,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

int pairing_register(void)
{
	int err;

	err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (err) {
		return err;
	}

	err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
	if (err) {
		return err;
	}

	return 0;
}

int paring_key_generate(struct bt_le_oob *oob)
{
	int err;

	printk("Generating new pairing keys\n");

#if !defined(CONFIG_BT_SMP_SC_PAIR_ONLY)
	err = bt_rand(tk_value, sizeof(tk_value));

	if (err) {
		printk("Random TK value generation failed: %d\n", err);
	}
#endif
	err = bt_le_oob_get_local(BT_ID_DEFAULT, oob);
	oob_local_ptr = err ? NULL : oob;

	if (!err) {
		bt_le_oob_set_sc_flag(true);
	}

	return err;
}

const uint8_t *pairing_get_tk(void)
{
	return tk_value;
}