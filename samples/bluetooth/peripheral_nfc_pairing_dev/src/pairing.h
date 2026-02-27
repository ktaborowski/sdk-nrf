/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef PAIRING_H__
#define PAIRING_H__

#include <zephyr/bluetooth/bluetooth.h>

int pairing_register(void);

int paring_key_generate(struct bt_le_oob *oob);

#endif /* PAIRING_H__ */
