/*
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. Hybrid Logical Clock Tracker (HLCT)
 * implementation. An HLCT tracks the highest HLC timestamp the process has
 * heard of. It never generates any new HLC timestamps.
 */

#include "crt_internal.h"
#include <gurt/atomic.h>

static ATOMIC uint64_t crt_hlct;

uint64_t crt_hlct_get(void)
{
	return crt_hlct;
}

void crt_hlct_sync(uint64_t msg)
{
	uint64_t hlct, hlct_new;

	do {
		hlct = crt_hlct;
		if (hlct >= msg)
			break;
		hlct_new = msg;
	} while (!atomic_compare_exchange(&crt_hlct, hlct, hlct_new));
}
