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

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled_cb,
};

int main(void)
{
	int err;

	printk("Starting Bluetooth LE peripheral\n");

	err = dk_leds_init();
	if (err) {
		printk("LED init failed (err %d)\n", err);
		return 0;
	}

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		err = settings_load();
		if (err) {
			printk("Cannot load settings (err %d)\n", err);
		}
	}

	advertising_init();
	advertising_start();

	for (;;) {
		k_sleep(K_SECONDS(1));
	}
}
