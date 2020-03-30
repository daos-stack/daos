/* Copyright (C) 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 * 4. All publications or advertising materials mentioning features or use of
 *    this software are asked, but not required, to acknowledge that it was
 *    developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

/** Get local physical time */
static inline uint64_t crt_hlc_localtime_get(void)
{
	struct timespec now;
	uint64_t	pt;
	int		rc;

	rc = clock_gettime(CLOCK_REALTIME_COARSE, &now);
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

uint64_t crt_hlc_get_msg(uint64_t msg)
{
	uint64_t pt = crt_hlc_localtime_get();
	uint64_t hlc, ret, ml = msg & ~CRT_HLC_MASK;

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

	return ret;
}

uint64_t crt_hlc2sec(uint64_t hlc)
{
	return (hlc & ~CRT_HLC_MASK) / NSEC_PER_SEC;
}
