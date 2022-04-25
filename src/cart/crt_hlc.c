/*
 * (C) Copyright 2019-2022 Intel Corporation.
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

/**
 * HLC timestamp unit (given in the HLC timestamp value for 1 ns) (i.e.,
 * 1/16 ns, offering a 36-year range)
 */
#define CRT_HLC_NSEC 16ULL

/**
 * HLC start time (given in the Unix time for 2021-01-01 00:00:00 +0000 UTC in
 * seconds) (i.e., together with CRT_HLC_NSEC, offering a range of [2021, 2057])
 */
#define CRT_HLC_START_SEC 1609459200ULL

/** Mask for the 18 logical bits */
#define CRT_HLC_MASK 0x3FFFFULL

static ATOMIC uint64_t crt_hlc;

/** See crt_hlc_epsilon_set's API doc */
static uint64_t crt_hlc_epsilon = 1ULL * NSEC_PER_SEC * CRT_HLC_NSEC;

/** Get local physical time */
static inline uint64_t crt_hlc_localtime_get(void)
{
	struct timespec now;
	uint64_t	pt;
	int		rc;

	rc = clock_gettime(CLOCK_REALTIME, &now);
	D_ASSERTF(rc == 0, "clock_gettime: %d\n", errno);
	D_ASSERT(now.tv_sec > CRT_HLC_START_SEC);
	pt = ((now.tv_sec - CRT_HLC_START_SEC) * NSEC_PER_SEC + now.tv_nsec) *
	     CRT_HLC_NSEC;

	/** Return the most significant 46 bits of time. */
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

uint64_t crt_hlc2nsec(uint64_t hlc)
{
	return hlc / CRT_HLC_NSEC;
}

uint64_t crt_nsec2hlc(uint64_t nsec)
{
	return nsec * CRT_HLC_NSEC;
}

uint64_t crt_hlc2unixnsec(uint64_t hlc)
{
	return hlc / CRT_HLC_NSEC + CRT_HLC_START_SEC * NSEC_PER_SEC;
}

int crt_hlc2timespec(uint64_t hlc, struct timespec *ts)
{
	uint64_t nsec;

	if (ts == NULL)
		return -DER_INVAL;

	nsec = crt_hlc2nsec(hlc);
	ts->tv_sec = nsec / NSEC_PER_SEC + CRT_HLC_START_SEC;
	ts->tv_nsec = nsec % NSEC_PER_SEC;
	return 0;
}

uint64_t crt_unixnsec2hlc(uint64_t unixnsec)
{
	uint64_t start = CRT_HLC_START_SEC * NSEC_PER_SEC;

	/*
	 * If the time represented by unixnsec is before the time represented
	 * by CRT_HLC_START_SEC, or after the maximum time representable, then
	 * the conversion is impossible.
	 */
	if (unixnsec < start || unixnsec - start > (uint64_t)-1 / CRT_HLC_NSEC)
		return 0;

	return (unixnsec - start) * CRT_HLC_NSEC;
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
