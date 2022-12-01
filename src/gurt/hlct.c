/*
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of GURT. Hybrid Logical Clock Tracker (HLCT)
 * implementation. An HLCT tracks the highest HLC timestamp the process has
 * heard of. It never generates any new HLC timestamps.
 */

#include <stdint.h>
#include <gurt/atomic.h>


static ATOMIC uint64_t d_hlct;

uint64_t d_hlct_get(void)
{
	return d_hlct;
}

void d_hlct_sync(uint64_t msg)
{
	uint64_t hlct, hlct_new;

	do {
		hlct = d_hlct;
		if (hlct >= msg)
			break;
		hlct_new = msg;
	} while (!atomic_compare_exchange(&d_hlct, hlct, hlct_new));
}
