/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>

#include <zephyr/settings/settings.h>

#include <dk_buttons_and_leds.h>

#include "advertising.h"
#include "pairing.h"

#define CON_STATUS_LED DK_LED1

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		printk("Connection failed %s 0x%02x %s\n", addr, err,
		       bt_hci_err_to_str(err));
		return;
	}

#if defined(CONFIG_BT_PAIRING_SECURITY_ENABLED)
	/* Require pairing */
	bt_conn_set_security(conn, BT_SECURITY_L2);
#endif

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

	advertising_init();
	advertising_start();

	for (;;) {
		k_sleep(K_SECONDS(1));
	}
}
