/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <zephyr/shell/shell.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <bluetooth/gatt_dm.h>

/* Connection lookup context */
struct conn_lookup_ctx {
	struct bt_conn *conn;
	uint8_t target_idx;
	uint8_t current_idx;
};

/* Discovery state */
static struct {
	bool discovery_in_progress;
	bool is_discover_all; /* true if discover-all, false if discover-uuid */
	struct bt_conn *current_conn;
	struct bt_gatt_dm *current_dm;
} discovery_state;

/* Callback context for connection lookup */
static void conn_lookup_by_index(struct bt_conn *conn, void *user_data)
{
	struct conn_lookup_ctx *ctx = user_data;
	struct bt_conn_info info;

	if (bt_conn_get_info(conn, &info) != 0) {
		return;
	}

	if (info.state != BT_CONN_STATE_CONNECTED) {
		return;
	}

	if (ctx->current_idx == ctx->target_idx) {
		ctx->conn = bt_conn_ref(conn);
	}

	ctx->current_idx++;
}

/* Get connection by index (0-based) */
static struct bt_conn *get_conn_by_index(uint8_t idx)
{
	struct conn_lookup_ctx ctx = {
		.conn = NULL,
		.target_idx = idx,
		.current_idx = 0,
	};

	bt_conn_foreach(BT_CONN_TYPE_LE, conn_lookup_by_index, &ctx);

	return ctx.conn;
}

/* Get default connection from BT shell */
static struct bt_conn *get_default_conn(void)
{
	struct conn_lookup_ctx ctx = {
		.conn = NULL,
		.target_idx = 0,
		.current_idx = 0,
	};

	/* Get first connected LE connection */
	bt_conn_foreach(BT_CONN_TYPE_LE, conn_lookup_by_index, &ctx);

	return ctx.conn;
}

/* Discovery completion callback */
static void discovery_completed_cb(struct bt_gatt_dm *dm, void *context)
{
	char uuid_str[37];
	const struct bt_gatt_dm_attr *gatt_service_attr;
	const struct bt_gatt_service_val *gatt_service;
	size_t attr_count;

	gatt_service_attr = bt_gatt_dm_service_get(dm);
	if (!gatt_service_attr) {
		shell_error((const struct shell *)context, "Failed to get service");
		return;
	}

	gatt_service = bt_gatt_dm_attr_service_val(gatt_service_attr);
	if (!gatt_service) {
		shell_error((const struct shell *)context, "Failed to get service value");
		return;
	}

	attr_count = bt_gatt_dm_attr_cnt(dm);
	bt_uuid_to_str(gatt_service->uuid, uuid_str, sizeof(uuid_str));

	shell_print((const struct shell *)context, "Discovery completed:");
	shell_print((const struct shell *)context, "  Service UUID: %s", uuid_str);
	shell_print((const struct shell *)context, "  Attribute count: %zu", attr_count);

	bt_gatt_dm_data_print(dm);
	
	/* Store DM instance for release/continue commands */
	discovery_state.current_dm = dm;
	
	shell_print((const struct shell *)context, "Use 'gatt-dm continue' to find next service or 'gatt-dm release' to finish");
}

static void discovery_service_not_found_cb(struct bt_conn *conn, void *context)
{
	ARG_UNUSED(conn);

	shell_print((const struct shell *)context, "No more services found");
	discovery_state.discovery_in_progress = false;
	discovery_state.is_discover_all = false;
	if (discovery_state.current_conn) {
		bt_conn_unref(discovery_state.current_conn);
		discovery_state.current_conn = NULL;
	}
	discovery_state.current_dm = NULL;
}

static void discovery_error_cb(struct bt_conn *conn, int err, void *context)
{
	ARG_UNUSED(conn);

	shell_error((const struct shell *)context, "Discovery failed: %d", err);
	discovery_state.discovery_in_progress = false;
	discovery_state.is_discover_all = false;
	if (discovery_state.current_conn) {
		bt_conn_unref(discovery_state.current_conn);
		discovery_state.current_conn = NULL;
	}
	discovery_state.current_dm = NULL;
}

static struct bt_gatt_dm_cb discovery_cb = {
	.completed = discovery_completed_cb,
	.service_not_found = discovery_service_not_found_cb,
	.error_found = discovery_error_cb,
};

/* Shell command: discover-all */
static int cmd_gatt_dm_discover_all(const struct shell *sh, size_t argc, char **argv)
{
	struct bt_conn *conn = NULL;
	int err;
	uint8_t conn_idx = 0;

	if (discovery_state.discovery_in_progress) {
		shell_error(sh, "Discovery already in progress");
		return -EALREADY;
	}

	/* Get connection */
	if (argc > 1) {
		conn_idx = strtoul(argv[1], NULL, 10);
		conn = get_conn_by_index(conn_idx);
		if (!conn) {
			shell_error(sh, "Connection index %u not found", conn_idx);
			return -ENOENT;
		}
	} else {
		conn = get_default_conn();
		if (!conn) {
			shell_error(sh, "No connection available. Connect first or specify connection index");
			return -ENOENT;
		}
	}

	if (argc > 1) {
		shell_print(sh, "Starting discovery on connection %u", conn_idx);
	} else {
		shell_print(sh, "Starting discovery on default connection");
	}

	err = bt_gatt_dm_start(conn, NULL, &discovery_cb, (void *)sh);
	if (err) {
		shell_error(sh, "Failed to start discovery: %d", err);
		bt_conn_unref(conn);
		return err;
	}

	discovery_state.discovery_in_progress = true;
	discovery_state.is_discover_all = true;
	discovery_state.current_conn = conn;
	discovery_state.current_dm = NULL;

	return 0;
}

/* Shell command: discover-uuid */
static int cmd_gatt_dm_discover_uuid(const struct shell *sh, size_t argc, char **argv)
{
	struct bt_conn *conn = NULL;
	struct bt_uuid_16 uuid16;
	struct bt_uuid *uuid = NULL;
	int err;
	uint8_t conn_idx = 0;
	unsigned long uuid_val;
	char *endptr;

	if (discovery_state.discovery_in_progress) {
		shell_error(sh, "Discovery already in progress");
		return -EALREADY;
	}

	if (argc < 2) {
		shell_error(sh, "UUID required");
		shell_help(sh);
		return -EINVAL;
	}

	/* Parse UUID */
	if (strncmp(argv[1], "0x", 2) == 0 || strncmp(argv[1], "0X", 2) == 0) {
		uuid_val = strtoul(argv[1], &endptr, 16);
	} else {
		uuid_val = strtoul(argv[1], &endptr, 16);
	}

	if (*endptr != '\0' || uuid_val == 0 || uuid_val > UINT16_MAX) {
		shell_error(sh, "Invalid UUID format. Use 16-bit UUID (e.g., 0x180F)");
		return -EINVAL;
	}

	/* Initialize UUID structure */
	uuid16.uuid.type = BT_UUID_TYPE_16;
	uuid16.val = (uint16_t)uuid_val;
	uuid = (struct bt_uuid *)&uuid16;

	/* Get connection */
	if (argc > 2) {
		conn_idx = strtoul(argv[2], NULL, 10);
		conn = get_conn_by_index(conn_idx);
		if (!conn) {
			shell_error(sh, "Connection index %u not found", conn_idx);
			return -ENOENT;
		}
	} else {
		conn = get_default_conn();
		if (!conn) {
			shell_error(sh, "No connection available. Connect first or specify connection index");
			return -ENOENT;
		}
	}

	if (argc > 2) {
		shell_print(sh, "Starting discovery for UUID 0x%04X on connection %u",
			    uuid16.val, conn_idx);
	} else {
		shell_print(sh, "Starting discovery for UUID 0x%04X on default connection",
			    uuid16.val);
	}

	err = bt_gatt_dm_start(conn, uuid, &discovery_cb, (void *)sh);
	if (err) {
		shell_error(sh, "Failed to start discovery: %d", err);
		bt_conn_unref(conn);
		return err;
	}

	discovery_state.discovery_in_progress = true;
	discovery_state.is_discover_all = false;
	discovery_state.current_conn = conn;
	discovery_state.current_dm = NULL;

	return 0;
}

/* Shell command: continue */
static int cmd_gatt_dm_continue(const struct shell *sh, size_t argc, char **argv)
{
	int err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!discovery_state.discovery_in_progress || !discovery_state.current_dm) {
		shell_error(sh, "No discovery in progress or discovery data not available. Start discovery first");
		return -EINVAL;
	}

	if (!discovery_state.is_discover_all) {
		shell_error(sh, "Continue only works with 'discover-all', not 'discover-uuid'");
		return -EINVAL;
	}

	/* Release previous discovery data before continuing */
	err = bt_gatt_dm_data_release(discovery_state.current_dm);
	if (err && err != -EALREADY) {
		shell_error(sh, "Failed to release discovery data: %d", err);
		return err;
	}

	shell_print(sh, "Continuing discovery...");

	err = bt_gatt_dm_continue(discovery_state.current_dm, (void *)sh);
	if (err) {
		shell_error(sh, "Failed to continue discovery: %d", err);
		return err;
	}

	discovery_state.current_dm = NULL;

	return 0;
}

/* Shell command: release */
static int cmd_gatt_dm_release(const struct shell *sh, size_t argc, char **argv)
{
	int err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!discovery_state.discovery_in_progress) {
		shell_error(sh, "No discovery in progress");
		return -EINVAL;
	}

	if (discovery_state.current_dm) {
		err = bt_gatt_dm_data_release(discovery_state.current_dm);
		if (err && err != -EALREADY) {
			shell_error(sh, "Failed to release discovery data: %d", err);
			return err;
		}
		discovery_state.current_dm = NULL;
	}

	discovery_state.discovery_in_progress = false;

	if (discovery_state.current_conn) {
		bt_conn_unref(discovery_state.current_conn);
		discovery_state.current_conn = NULL;
	}

	shell_print(sh, "Discovery data released");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(gatt_dm_cmds,
	SHELL_CMD_ARG(discover-all, NULL,
		      "[conn_idx]",
		      cmd_gatt_dm_discover_all, 1, 1),
	SHELL_CMD_ARG(discover-uuid, NULL,
		      "<uuid> [conn_idx]",
		      cmd_gatt_dm_discover_uuid, 2, 1),
	SHELL_CMD_ARG(continue, NULL,
		      NULL,
		      cmd_gatt_dm_continue, 1, 0),
	SHELL_CMD_ARG(release, NULL,
		      NULL,
		      cmd_gatt_dm_release, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(gatt_dm, &gatt_dm_cmds, "GATT Discovery Manager commands", NULL);
