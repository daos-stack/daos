/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2014-2022, Intel Corporation */

/*
 * util.c -- very basic utilities
 */

#include <stdlib.h>
#include <string.h>
#include <endian.h>

#include "util.h"
#include "valgrind_internal.h"


#if ANY_VG_TOOL_ENABLED
/* Initialized to true if the process is running inside Valgrind. */
unsigned _On_valgrind;
#endif

#if VG_HELGRIND_ENABLED
/* Initialized to true if the process is running inside Valgrind helgrind. */
unsigned _On_helgrind;
#endif

#if VG_DRD_ENABLED
/* Initialized to true if the process is running inside Valgrind drd. */
unsigned _On_drd;
#endif

#if VG_HELGRIND_ENABLED || VG_DRD_ENABLED
/* Initialized to true if the process is running inside Valgrind drd or hg. */
unsigned _On_drd_or_hg;
#endif

#if VG_MEMCHECK_ENABLED
/* Initialized to true if the process is running inside Valgrind memcheck. */
unsigned _On_memcheck;
#endif

#if VG_TXINFO_ENABLED
/* true if DAV API and TX-related messages has to be enabled in Valgrind log. */
int _Vg_txinfo_emit;
#endif /* VG_TXINFO_ENABLED */

/*
 * util_is_zeroed -- check if given memory range is all zero
 */
int
util_is_zeroed(const void *addr, size_t len)
{
	const char *a = addr;

	if (len == 0)
		return 1;

	if (a[0] == 0 && memcmp(a, a + 1, len - 1) == 0)
		return 1;

	return 0;
}

/*
 * util_checksum_compute -- compute Fletcher64-like checksum
 *
 * csump points to where the checksum lives, so that location
 * is treated as zeros while calculating the checksum. The
 * checksummed data is assumed to be in little endian order.
 */
uint64_t
util_checksum_compute(void *addr, size_t len, uint64_t *csump, size_t skip_off)
{
	if (len % 4 != 0)
		abort();

	uint32_t *p32 = addr;
	uint32_t *p32end = (uint32_t *)((char *)addr + len);
	uint32_t *skip;
	uint32_t lo32 = 0;
	uint32_t hi32 = 0;

	if (skip_off)
		skip = (uint32_t *)((char *)addr + skip_off);
	else
		skip = (uint32_t *)((char *)addr + len);

	while (p32 < p32end)
		if (p32 == (uint32_t *)csump || p32 >= skip) {
			/* lo32 += 0; treat first 32-bits as zero */
			p32++;
			hi32 += lo32;
			/* lo32 += 0; treat second 32-bits as zero */
			p32++;
			hi32 += lo32;
		} else {
			lo32 += le32toh(*p32);
			++p32;
			hi32 += lo32;
		}

	return (uint64_t)hi32 << 32 | lo32;
}

/*
 * util_checksum -- compute Fletcher64-like checksum
 *
 * csump points to where the checksum lives, so that location
 * is treated as zeros while calculating the checksum.
 * If insert is true, the calculated checksum is inserted into
 * the range at *csump.  Otherwise the calculated checksum is
 * checked against *csump and the result returned (true means
 * the range checksummed correctly).
 */
int
util_checksum(void *addr, size_t len, uint64_t *csump,
		int insert, size_t skip_off)
{
	uint64_t csum = util_checksum_compute(addr, len, csump, skip_off);

	if (insert) {
		*csump = htole64(csum);
		return 1;
	}

	return *csump == htole64(csum);
}

/*
 * util_checksum_seq -- compute sequential Fletcher64-like checksum
 *
 * Merges checksum from the old buffer with checksum for current buffer.
 */
uint64_t
util_checksum_seq(const void *addr, size_t len, uint64_t csum)
{
	if (len % 4 != 0)
		abort();
	const uint32_t *p32 = addr;
	const uint32_t *p32end = (const uint32_t *)((const char *)addr + len);
	uint32_t lo32 = (uint32_t)csum;
	uint32_t hi32 = (uint32_t)(csum >> 32);

	while (p32 < p32end) {
		lo32 += le32toh(*p32);
		++p32;
		hi32 += lo32;
	}
	return (uint64_t)hi32 << 32 | lo32;
}

/*
 * util_init -- initialize the utils
 *
 * This is called from the library initialization code.
 */
#if ANY_VG_TOOL_ENABLED
__attribute__((constructor))
static void
_util_init(void)
{
	util_init();
}
#endif

void
util_init(void)
{
#if ANY_VG_TOOL_ENABLED
	_On_valgrind = RUNNING_ON_VALGRIND;
#endif

#if VG_MEMCHECK_ENABLED
	if (_On_valgrind) {
		unsigned tmp;
		unsigned result;
		unsigned res = VALGRIND_GET_VBITS(&tmp, &result, sizeof(tmp));

		_On_memcheck = res ? 1 : 0;
	} else {
		_On_memcheck = 0;
	}
#endif

#if VG_DRD_ENABLED
	if (_On_valgrind)
		_On_drd = DRD_GET_DRD_THREADID ? 1 : 0;
	else
		_On_drd = 0;
#endif

#if VG_HELGRIND_ENABLED
	if (_On_valgrind) {
		unsigned tmp;
		unsigned result;
		/*
		 * As of now (pmem-3.15) VALGRIND_HG_GET_ABITS is broken on
		 * the upstream version of Helgrind headers. It generates
		 * a sign-conversion error and actually returns UINT32_MAX-1
		 * when not running under Helgrind.
		 */
		long res = VALGRIND_HG_GET_ABITS(&tmp, &result, sizeof(tmp));

		_On_helgrind = res != -2 ? 1 : 0;
	} else {
		_On_helgrind = 0;
	}
#endif

#if VG_DRD_ENABLED || VG_HELGRIND_ENABLED
	_On_drd_or_hg = (unsigned)(On_helgrind + On_drd);
#endif

#if VG_TXINFO_ENABLED
	if (_On_valgrind) {
		char *txinfo_env = secure_getenv("D_DAV_VG_TXINFO");

		if (txinfo_env)
			_Vg_txinfo_emit = atoi(txinfo_env);
	} else {
		_Vg_txinfo_emit = 0;
	}
#endif
}

