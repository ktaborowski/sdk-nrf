/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef ADVERTISING_H__
#define ADVERTISING_H__

/** Start connectable advertising (via work queue). */
void advertising_start(void);

/** Initialize advertising (work + first start). Call after bt_enable. */
void advertising_init(void);

#endif /* ADVERTISING_H__ */
