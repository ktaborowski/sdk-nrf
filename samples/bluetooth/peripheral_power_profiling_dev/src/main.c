/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>

#if IS_ENABLED(CONFIG_APP_GPIO_WAKEUP_ENABLE)
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#endif /* CONFIG_APP_GPIO_WAKEUP_ENABLE */

#define SYSTEM_OFF_DELAY 1

static void system_off_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(system_off_work, system_off_work_handler);

static void system_off(void)
{
#if IS_ENABLED(CONFIG_APP_GPIO_WAKEUP_ENABLE)
	/* Configure button 1 (sw0) as wake source from system off (level-active). */
	static const struct gpio_dt_spec sw0 = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

	if (gpio_is_ready_dt(&sw0)) {
		(void)gpio_pin_configure_dt(&sw0, GPIO_INPUT);
		(void)gpio_pin_interrupt_configure_dt(&sw0, GPIO_INT_LEVEL_ACTIVE);
	}
#endif /* CONFIG_APP_GPIO_WAKEUP_ENABLE */
	sys_poweroff();
}

static void system_off_work_handler(struct k_work *work)
{
	system_off();
}

#if IS_ENABLED(CONFIG_APP_POWER_PROFILING_BLE)

#include <dk_buttons_and_leds.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#define CONNECTABLE_ADV_BUTTON DK_BTN1_MSK
#define POWER_OFF_BUTTON       DK_BTN2_MSK

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
	uint32_t buttons = button_state & has_changed;

	if (buttons & POWER_OFF_BUTTON) {
		/* Disconnect if connected and schedule power off. */
		k_work_cancel_delayable(&system_off_work);
		if (device_conn) {
			(void)bt_conn_disconnect(device_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		}
		k_work_schedule(&system_off_work, K_SECONDS(SYSTEM_OFF_DELAY));
		return;
	}

	if (!(buttons & CONNECTABLE_ADV_BUTTON)) {
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

#endif /* CONFIG_APP_POWER_PROFILING_BLE */

int main(void)
{
#if IS_ENABLED(CONFIG_APP_POWER_PROFILING_BLE)
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
#endif

	/* No BLE or no button at boot: schedule power off. */
	k_work_schedule(&system_off_work, K_SECONDS(SYSTEM_OFF_DELAY));

	k_sleep(K_FOREVER);
	return 0;
}
