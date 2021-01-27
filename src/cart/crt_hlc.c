/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. Hybrid Logical Clock (HLC) implementation.
 */
#include "crt_internal.h"
#include <gurt/common.h>	/* for NSEC_PER_SEC */
#include <gurt/atomic.h>
#include <time.h>

#define CRT_HLC_MASK 0xFFFFULL

static ATOMIC uint64_t crt_hlc;

/** See crt_hlc_epsilon_set's API doc */
static uint64_t crt_hlc_epsilon = 1000 * 1000 * 1000;

/** Get local physical time */
static inline uint64_t crt_hlc_localtime_get(void)
{
	struct timespec now;
	uint64_t	pt;
	int		rc;

	rc = clock_gettime(CLOCK_REALTIME, &now);
	pt = rc ? crt_hlc : (now.tv_sec * NSEC_PER_SEC + now.tv_nsec);

	/**
	 * Return the most significant 48 bits of time.
	 * In case of error of retrieving a system time use previous time.
	 */
	return pt & ~CRT_HLC_MASK;
}

uint64_t crt_hlc_get(void)
{
	uint64_t pt = crt_hlc_localtime_get();
	uint64_t hlc, ret;

	do {
		hlc = crt_hlc;
		ret = (hlc & ~CRT_HLC_MASK) < pt ? pt : (hlc + 1);
	} while (!atomic_compare_exchange(&crt_hlc, hlc, ret));

	return ret;
}

int crt_hlc_get_msg(uint64_t msg, uint64_t *hlc_out, uint64_t *offset)
{
	uint64_t pt = crt_hlc_localtime_get();
	uint64_t hlc, ret, ml = msg & ~CRT_HLC_MASK;
	uint64_t off;

	off = ml > pt ? ml - pt : 0;

	if (offset != NULL)
		*offset = off;

	if (off > crt_hlc_epsilon)
		return -DER_HLC_SYNC;

	do {
		hlc = crt_hlc;
		if ((hlc & ~CRT_HLC_MASK) < ml)
			ret = ml < pt ? pt : (msg + 1);
		else if ((hlc & ~CRT_HLC_MASK) < pt)
			ret = pt;
		else if (pt <= ml)
			ret = (hlc < msg ? msg : hlc) + 1;
		else
			ret = hlc + 1;
	} while (!atomic_compare_exchange(&crt_hlc, hlc, ret));

	if (hlc_out != NULL)
		*hlc_out = ret;
	return 0;
}

uint64_t crt_hlc2sec(uint64_t hlc)
{
	return (hlc & ~CRT_HLC_MASK) / NSEC_PER_SEC;
}

void crt_hlc_epsilon_set(uint64_t epsilon)
{
	crt_hlc_epsilon = (epsilon + CRT_HLC_MASK) & ~CRT_HLC_MASK;
	D_INFO("set maximum system clock offset to "DF_U64" ns\n",
	       crt_hlc_epsilon);
}

uint64_t crt_hlc_epsilon_get(void)
{
	return crt_hlc_epsilon;
}

uint64_t crt_hlc_epsilon_get_bound(uint64_t hlc)
{
	return (hlc + crt_hlc_epsilon) | CRT_HLC_MASK;
}
