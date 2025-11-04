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
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_BT_EXT_ADV)

/* Advertising set state */
enum adv_set_state {
	ADV_SET_STATE_NOT_CREATED,
	ADV_SET_STATE_CREATED,
	ADV_SET_STATE_STARTED,
};

/* Advertising set information */
struct adv_set_info {
	struct bt_le_ext_adv *adv;
	enum adv_set_state state;
	uint8_t sid;
};

/* Advertising sets state */
static struct adv_set_info adv_sets_info[CONFIG_BT_EXT_ADV_MAX_ADV_SET];

/* Callbacks for advertising events */
#if defined(CONFIG_BT_PERIPHERAL)
static void adv_connected_cb(struct bt_le_ext_adv *adv,
			     struct bt_le_ext_adv_connected_info *info)
{
	char addr[BT_ADDR_LE_STR_LEN];
	uint8_t idx = bt_le_ext_adv_get_index(adv);

	bt_addr_le_to_str(bt_conn_get_dst(info->conn), addr, sizeof(addr));
	printk("Advertiser[%d] %p connected conn %p (%s)\n",
	       idx, adv, info->conn, addr);
}
#endif /* CONFIG_BT_PERIPHERAL */

static void adv_scanned_cb(struct bt_le_ext_adv *adv,
			   struct bt_le_ext_adv_scanned_info *info)
{
	char addr[BT_ADDR_LE_STR_LEN];
	uint8_t idx = bt_le_ext_adv_get_index(adv);

	bt_addr_le_to_str(info->addr, addr, sizeof(addr));
	printk("Advertiser[%d] scanned by %s\n", idx, addr);
}

static void adv_sent_cb(struct bt_le_ext_adv *adv,
			struct bt_le_ext_adv_sent_info *info)
{
	uint8_t idx = bt_le_ext_adv_get_index(adv);

	printk("Advertiser[%d] sent %d events\n",
	       idx, info->num_sent);
}

static const struct bt_le_ext_adv_cb adv_cb = {
#if defined(CONFIG_BT_PERIPHERAL)
	.connected = adv_connected_cb,
#endif /* CONFIG_BT_PERIPHERAL */
	.scanned = adv_scanned_cb,
	.sent = adv_sent_cb,
};

/* Get advertising set info by index */
static struct adv_set_info *get_adv_set_info(uint8_t idx)
{
	if (idx >= ARRAY_SIZE(adv_sets_info)) {
		return NULL;
	}

	return &adv_sets_info[idx];
}

/* Parse advertising options */
static int parse_adv_options(const struct shell *sh, size_t argc, char **argv,
			     struct bt_le_adv_param *param, uint8_t default_sid)
{
	bool connectable = false;
	bool scannable = false;
	bool non_connectable = false;
	int interval_min = -1;
	int interval_max = -1;

	/* Default parameters */
	param->id = BT_ID_DEFAULT;
	param->options = BT_LE_ADV_OPT_EXT_ADV;
	param->interval_min = BT_GAP_ADV_FAST_INT_MIN_2;
	param->interval_max = BT_GAP_ADV_FAST_INT_MAX_2;
	param->peer = NULL;
	param->sid = default_sid;

	for (size_t i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "connectable")) {
			connectable = true;
			param->options |= BT_LE_ADV_OPT_CONN;
		} else if (!strcmp(argv[i], "non-connectable")) {
			non_connectable = true;
		} else if (!strcmp(argv[i], "scannable")) {
			scannable = true;
			param->options |= BT_LE_ADV_OPT_SCANNABLE;
		} else if (!strcmp(argv[i], "interval-min")) {
			if (i + 1 >= argc) {
				shell_error(sh, "interval-min requires a value");
				return -EINVAL;
			}
			interval_min = strtol(argv[++i], NULL, 10);
			if (interval_min <= 0 || interval_min > 0xFFFF) {
				shell_error(sh, "Invalid interval-min: %d", interval_min);
				return -EINVAL;
			}
			param->interval_min = interval_min;
		} else if (!strcmp(argv[i], "interval-max")) {
			if (i + 1 >= argc) {
				shell_error(sh, "interval-max requires a value");
				return -EINVAL;
			}
			interval_max = strtol(argv[++i], NULL, 10);
			if (interval_max <= 0 || interval_max > 0xFFFF) {
				shell_error(sh, "Invalid interval-max: %d", interval_max);
				return -EINVAL;
			}
			param->interval_max = interval_max;
		} else if (!strcmp(argv[i], "sid")) {
			if (i + 1 >= argc) {
				shell_error(sh, "sid requires a value");
				return -EINVAL;
			}
			int sid = strtol(argv[++i], NULL, 10);
			if (sid < 0 || sid > 0xF) {
				shell_error(sh, "Invalid sid: %d (must be 0-15)", sid);
				return -EINVAL;
			}
			param->sid = sid;
		}
	}

	if (connectable && non_connectable) {
		shell_error(sh, "Cannot specify both connectable and non-connectable");
		return -EINVAL;
	}

	if (interval_max > 0 && interval_min > 0 && interval_max < interval_min) {
		shell_error(sh, "interval-max must be >= interval-min");
		return -EINVAL;
	}

	return 0;
}

/* Shell command: create */
static int cmd_multi_adv_create(const struct shell *sh, size_t argc, char **argv)
{
	struct bt_le_adv_param param;
	struct adv_set_info *set_info;
	uint8_t idx;
	int err;

	if (argc < 2) {
		shell_error(sh, "Index required");
		shell_help(sh);
		return -EINVAL;
	}

	idx = strtoul(argv[1], NULL, 10);
	if (idx >= ARRAY_SIZE(adv_sets_info)) {
		shell_error(sh, "Invalid index: %u (max: %zu)", idx,
			    ARRAY_SIZE(adv_sets_info) - 1);
		return -EINVAL;
	}

	set_info = get_adv_set_info(idx);
	if (set_info->state != ADV_SET_STATE_NOT_CREATED) {
		shell_error(sh, "Advertiser[%d] already exists. Delete it first", idx);
		return -EALREADY;
	}

	/* Parse options */
	if (argc > 2) {
		err = parse_adv_options(sh, argc - 2, &argv[2], &param, idx);
		if (err) {
			return err;
		}
	} else {
		/* Default: non-connectable, scannable */
		param.id = BT_ID_DEFAULT;
		param.options = BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_SCANNABLE;
		param.interval_min = BT_GAP_ADV_FAST_INT_MIN_2;
		param.interval_max = BT_GAP_ADV_FAST_INT_MAX_2;
		param.peer = NULL;
		param.sid = idx;
	}

	/* Create advertising set */
	err = bt_le_ext_adv_create(&param, &adv_cb, &set_info->adv);
	if (err) {
		shell_error(sh, "Failed to create advertiser[%d]: %d", idx, err);
		return err;
	}

	set_info->state = ADV_SET_STATE_CREATED;
	set_info->sid = param.sid;

	shell_print(sh, "Created advertiser[%d] (SID: %d)", idx, param.sid);

	return 0;
}

/* Shell command: data */
static int cmd_multi_adv_data(const struct shell *sh, size_t argc, char **argv)
{
	struct adv_set_info *set_info;
	uint8_t idx;
	int err;
	struct bt_data ad[BT_GAP_ADV_MAX_ADV_DATA_LEN];
	size_t ad_len = 0;
	struct bt_data sd[BT_GAP_ADV_MAX_ADV_DATA_LEN];
	size_t sd_len = 0;
	bool is_scan_response = false;

	if (argc < 3) {
		shell_error(sh, "Index and data required");
		shell_help(sh);
		return -EINVAL;
	}

	idx = strtoul(argv[1], NULL, 10);
	if (idx >= ARRAY_SIZE(adv_sets_info)) {
		shell_error(sh, "Invalid index: %u", idx);
		return -EINVAL;
	}

	set_info = get_adv_set_info(idx);
	if (set_info->state == ADV_SET_STATE_NOT_CREATED) {
		shell_error(sh, "Advertiser[%d] not created. Create it first", idx);
		return -EINVAL;
	}

	/* Parse arguments */
	for (size_t i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "scan-response")) {
			is_scan_response = true;
			continue;
		} else if (!strcmp(argv[i], "name")) {
			if (i + 1 >= argc) {
				shell_error(sh, "name requires a value");
				return -EINVAL;
			}
			const char *name = argv[++i];
			size_t name_len = strlen(name);

			if (is_scan_response) {
				if (sd_len >= ARRAY_SIZE(sd)) {
					shell_error(sh, "Too many scan response data entries");
					return -ENOMEM;
				}
				sd[sd_len].type = BT_DATA_NAME_COMPLETE;
				sd[sd_len].data_len = name_len;
				sd[sd_len].data = (const uint8_t *)name;
				sd_len++;
			} else {
				if (ad_len >= ARRAY_SIZE(ad)) {
					shell_error(sh, "Too many advertising data entries");
					return -ENOMEM;
				}
				ad[ad_len].type = BT_DATA_NAME_COMPLETE;
				ad[ad_len].data_len = name_len;
				ad[ad_len].data = (const uint8_t *)name;
				ad_len++;
			}
		} else if (!strcmp(argv[i], "flags")) {
			uint8_t flags = BT_LE_AD_NO_BREDR;

			if (is_scan_response) {
				if (sd_len >= ARRAY_SIZE(sd)) {
					shell_error(sh, "Too many scan response data entries");
					return -ENOMEM;
				}
				sd[sd_len].type = BT_DATA_FLAGS;
				sd[sd_len].data_len = 1;
				sd[sd_len].data = &flags;
				sd_len++;
			} else {
				if (ad_len >= ARRAY_SIZE(ad)) {
					shell_error(sh, "Too many advertising data entries");
					return -ENOMEM;
				}
				ad[ad_len].type = BT_DATA_FLAGS;
				ad[ad_len].data_len = 1;
				ad[ad_len].data = &flags;
				ad_len++;
			}
		} else {
			/* Try to parse as hex data */
			shell_error(sh, "Unsupported data format: %s", argv[i]);
			shell_help(sh);
			return -EINVAL;
		}
	}

	/* Set advertising data */
	err = bt_le_ext_adv_set_data(set_info->adv, ad_len > 0 ? ad : NULL, ad_len,
				      sd_len > 0 ? sd : NULL, sd_len);
	if (err) {
		shell_error(sh, "Failed to set data for advertiser[%d]: %d", idx, err);
		return err;
	}

	shell_print(sh, "Set data for advertiser[%d]", idx);

	return 0;
}

/* Shell command: start */
static int cmd_multi_adv_start(const struct shell *sh, size_t argc, char **argv)
{
	struct adv_set_info *set_info;
	struct bt_le_ext_adv_start_param start_param = {0};
	uint8_t idx;
	int err;
	int timeout = 0;
	int num_events = 0;

	if (argc < 2) {
		shell_error(sh, "Index required");
		shell_help(sh);
		return -EINVAL;
	}

	idx = strtoul(argv[1], NULL, 10);
	if (idx >= ARRAY_SIZE(adv_sets_info)) {
		shell_error(sh, "Invalid index: %u", idx);
		return -EINVAL;
	}

	set_info = get_adv_set_info(idx);
	if (set_info->state == ADV_SET_STATE_NOT_CREATED) {
		shell_error(sh, "Advertiser[%d] not created. Create it first", idx);
		return -EINVAL;
	}

	if (set_info->state == ADV_SET_STATE_STARTED) {
		shell_print(sh, "Advertiser[%d] already started", idx);
		return 0;
	}

	/* Parse optional parameters */
	for (size_t i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "timeout")) {
			if (i + 1 >= argc) {
				shell_error(sh, "timeout requires a value");
				return -EINVAL;
			}
			timeout = strtol(argv[++i], NULL, 10);
			if (timeout < 0 || timeout > 0xFFFF) {
				shell_error(sh, "Invalid timeout: %d", timeout);
				return -EINVAL;
			}
			start_param.timeout = timeout;
		} else if (!strcmp(argv[i], "num-events")) {
			if (i + 1 >= argc) {
				shell_error(sh, "num-events requires a value");
				return -EINVAL;
			}
			num_events = strtol(argv[++i], NULL, 10);
			if (num_events < 0 || num_events > 0xFF) {
				shell_error(sh, "Invalid num-events: %d", num_events);
				return -EINVAL;
			}
			start_param.num_events = num_events;
		}
	}

	err = bt_le_ext_adv_start(set_info->adv,
				  timeout > 0 || num_events > 0 ? &start_param : NULL);
	if (err) {
		shell_error(sh, "Failed to start advertiser[%d]: %d", idx, err);
		return err;
	}

	set_info->state = ADV_SET_STATE_STARTED;
	shell_print(sh, "Started advertiser[%d]", idx);

	return 0;
}

/* Shell command: stop */
static int cmd_multi_adv_stop(const struct shell *sh, size_t argc, char **argv)
{
	struct adv_set_info *set_info;
	uint8_t idx;
	int err;

	if (argc < 2) {
		shell_error(sh, "Index required");
		shell_help(sh);
		return -EINVAL;
	}

	idx = strtoul(argv[1], NULL, 10);
	if (idx >= ARRAY_SIZE(adv_sets_info)) {
		shell_error(sh, "Invalid index: %u", idx);
		return -EINVAL;
	}

	set_info = get_adv_set_info(idx);
	if (set_info->state == ADV_SET_STATE_NOT_CREATED) {
		shell_error(sh, "Advertiser[%d] not created", idx);
		return -EINVAL;
	}

	if (set_info->state != ADV_SET_STATE_STARTED) {
		shell_print(sh, "Advertiser[%d] not started", idx);
		return 0;
	}

	err = bt_le_ext_adv_stop(set_info->adv);
	if (err) {
		shell_error(sh, "Failed to stop advertiser[%d]: %d", idx, err);
		return err;
	}

	set_info->state = ADV_SET_STATE_CREATED;
	shell_print(sh, "Stopped advertiser[%d]", idx);

	return 0;
}

/* Shell command: delete */
static int cmd_multi_adv_delete(const struct shell *sh, size_t argc, char **argv)
{
	struct adv_set_info *set_info;
	uint8_t idx;
	int err;

	if (argc < 2) {
		shell_error(sh, "Index required");
		shell_help(sh);
		return -EINVAL;
	}

	idx = strtoul(argv[1], NULL, 10);
	if (idx >= ARRAY_SIZE(adv_sets_info)) {
		shell_error(sh, "Invalid index: %u", idx);
		return -EINVAL;
	}

	set_info = get_adv_set_info(idx);
	if (set_info->state == ADV_SET_STATE_NOT_CREATED) {
		shell_error(sh, "Advertiser[%d] not created", idx);
		return -EINVAL;
	}

	/* Stop if started */
	if (set_info->state == ADV_SET_STATE_STARTED) {
		bt_le_ext_adv_stop(set_info->adv);
	}

	err = bt_le_ext_adv_delete(set_info->adv);
	if (err) {
		shell_error(sh, "Failed to delete advertiser[%d]: %d", idx, err);
		return err;
	}

	set_info->adv = NULL;
	set_info->state = ADV_SET_STATE_NOT_CREATED;
	set_info->sid = 0;

	shell_print(sh, "Deleted advertiser[%d]", idx);

	return 0;
}

/* Shell command: list */
static int cmd_multi_adv_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Advertising sets:");
	shell_print(sh, "Index | State      | SID");
	shell_print(sh, "------|------------|-----");

	for (size_t i = 0; i < ARRAY_SIZE(adv_sets_info); i++) {
		const struct adv_set_info *set_info = &adv_sets_info[i];
		const char *state_str;

		switch (set_info->state) {
		case ADV_SET_STATE_NOT_CREATED:
			state_str = "not created";
			break;
		case ADV_SET_STATE_CREATED:
			state_str = "created";
			break;
		case ADV_SET_STATE_STARTED:
			state_str = "started";
			break;
		default:
			state_str = "unknown";
			break;
		}

		if (set_info->state != ADV_SET_STATE_NOT_CREATED) {
			shell_print(sh, "  %zu   | %-10s | %d", i, state_str, set_info->sid);
		}
	}

	return 0;
}

/* Shell command: start-all */
static int cmd_multi_adv_start_all(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err;
	int started = 0;
	int failed = 0;

	for (size_t i = 0; i < ARRAY_SIZE(adv_sets_info); i++) {
		struct adv_set_info *set_info = &adv_sets_info[i];

		if (set_info->state == ADV_SET_STATE_CREATED) {
			err = bt_le_ext_adv_start(set_info->adv, NULL);
			if (err) {
				shell_error(sh, "Failed to start advertiser[%zu]: %d", i, err);
				failed++;
			} else {
				set_info->state = ADV_SET_STATE_STARTED;
				started++;
			}
		}
	}

	if (started > 0) {
		shell_print(sh, "Started %d advertiser(s)", started);
	}

	if (failed > 0) {
		shell_error(sh, "Failed to start %d advertiser(s)", failed);
		return -EIO;
	}

	if (started == 0) {
		shell_print(sh, "No created advertisers to start");
	}

	return 0;
}

/* Shell command: stop-all */
static int cmd_multi_adv_stop_all(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err;
	int stopped = 0;
	int failed = 0;

	for (size_t i = 0; i < ARRAY_SIZE(adv_sets_info); i++) {
		struct adv_set_info *set_info = &adv_sets_info[i];

		if (set_info->state == ADV_SET_STATE_STARTED) {
			err = bt_le_ext_adv_stop(set_info->adv);
			if (err) {
				shell_error(sh, "Failed to stop advertiser[%zu]: %d", i, err);
				failed++;
			} else {
				set_info->state = ADV_SET_STATE_CREATED;
				stopped++;
			}
		}
	}

	if (stopped > 0) {
		shell_print(sh, "Stopped %d advertiser(s)", stopped);
	}

	if (failed > 0) {
		shell_error(sh, "Failed to stop %d advertiser(s)", failed);
		return -EIO;
	}

	if (stopped == 0) {
		shell_print(sh, "No started advertisers to stop");
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(multi_adv_cmds,
	SHELL_CMD_ARG(create, NULL,
		      "<index> [connectable|non-connectable] [scannable] "
		      "[interval-min <ms>] [interval-max <ms>] [sid <id>]",
		      cmd_multi_adv_create, 2, 10),
	SHELL_CMD_ARG(data, NULL,
		      "<index> [name <name>] [flags] [scan-response ...]",
		      cmd_multi_adv_data, 3, 10),
	SHELL_CMD_ARG(start, NULL,
		      "<index> [timeout <ms>] [num-events <count>]",
		      cmd_multi_adv_start, 2, 4),
	SHELL_CMD_ARG(stop, NULL,
		      "<index>",
		      cmd_multi_adv_stop, 2, 0),
	SHELL_CMD_ARG(delete, NULL,
		      "<index>",
		      cmd_multi_adv_delete, 2, 0),
	SHELL_CMD_ARG(list, NULL,
		      NULL,
		      cmd_multi_adv_list, 1, 0),
	SHELL_CMD_ARG(start-all, NULL,
		      NULL,
		      cmd_multi_adv_start_all, 1, 0),
	SHELL_CMD_ARG(stop-all, NULL,
		      NULL,
		      cmd_multi_adv_stop_all, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(multi_adv, &multi_adv_cmds, "Multiple advertising sets commands", NULL);

#endif /* CONFIG_BT_EXT_ADV */

