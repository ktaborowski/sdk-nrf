/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>

#include <dk_buttons_and_leds.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#define CONNECTABLE_ADV_BUTTON DK_BTN1_MSK
#define SYSTEM_OFF_DELAY       1

#define CONNECTABLE_ADV_TIMEOUT     CONFIG_BT_POWER_PROFILING_CONNECTABLE_ADV_DURATION
#define CONNECTABLE_ADV_INTERVAL_MIN CONFIG_BT_POWER_PROFILING_CONNECTABLE_ADV_INTERVAL_MIN
#define CONNECTABLE_ADV_INTERVAL_MAX CONFIG_BT_POWER_PROFILING_CONNECTABLE_ADV_INTERVAL_MAX

static struct bt_le_ext_adv *adv_set;
static struct bt_conn *device_conn;

static const struct bt_data connectable_ad_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const struct bt_le_adv_param *connectable_ad_params =
	BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN,
			CONNECTABLE_ADV_INTERVAL_MIN,
			CONNECTABLE_ADV_INTERVAL_MAX,
			NULL);

static void system_off_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(system_off_work, system_off_work_handler);

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	if (conn_err) {
		return;
	}
	device_conn = conn;
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	device_conn = NULL;
	k_work_schedule(&system_off_work, K_SECONDS(SYSTEM_OFF_DELAY));
}

BT_CONN_CB_DEFINE(connection_cb) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void system_off(void)
{
#if !IS_ENABLED(CONFIG_SOC_SERIES_NRF54H)
	sys_poweroff();
#endif
}

static void system_off_work_handler(struct k_work *work)
{
	system_off();
}

static void advertising_terminated(struct bt_le_ext_adv *adv,
				  struct bt_le_ext_adv_sent_info *info)
{
	if (!device_conn) {
		k_work_schedule(&system_off_work, K_SECONDS(SYSTEM_OFF_DELAY));
	}
}

static const struct bt_le_ext_adv_cb adv_callbacks = {
	.sent = advertising_terminated
};

static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	if (!(button_state & has_changed & CONNECTABLE_ADV_BUTTON)) {
		return;
	}

	struct bt_le_ext_adv_start_param start_param = {
		.timeout = CONNECTABLE_ADV_TIMEOUT,
		.num_events = 0
	};
	int err;

	k_work_cancel_delayable(&system_off_work);

	(void)bt_le_ext_adv_stop(adv_set);
	err = bt_le_ext_adv_update_param(adv_set, connectable_ad_params);
	if (err) {
		return;
	}

	err = bt_le_ext_adv_set_data(adv_set, connectable_ad_data,
				     ARRAY_SIZE(connectable_ad_data), NULL, 0);
	if (err) {
		return;
	}

	err = bt_le_ext_adv_start(adv_set, &start_param);
	(void)err;
}

int main(void)
{
	int err;

	err = dk_buttons_init(button_handler);
	if (err) {
		return 0;
	}

	err = bt_enable(NULL);
	if (err) {
		return 0;
	}

	err = bt_le_ext_adv_create(connectable_ad_params, &adv_callbacks, &adv_set);
	if (err) {
		return 0;
	}

	/* No button pressed at boot: schedule power off. */
	k_work_schedule(&system_off_work, K_SECONDS(SYSTEM_OFF_DELAY));

	/* Run until power off (no main loop; work runs in workqueue). */
	k_sleep(K_FOREVER);
	return 0;
}
