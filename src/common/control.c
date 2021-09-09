/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file implements functions shared with the control-plane.
 */
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

/*
 * Disable DPDK telemetry to avoid socket file clashes and quiet DPDK
 * logging by setting specific facility masks.
 */
const char *
dpdk_cli_override_opts = "--log-level=lib.eal:4 "
			 "--log-level=lib.malloc:4 "
			 "--log-level=lib.ring:4 "
			 "--log-level=lib.mempool:4 "
			 "--log-level=lib.timer:4 "
			 "--log-level=pmd:4 "
			 "--log-level=lib.hash:4 "
			 "--log-level=lib.lpm:4 "
			 "--log-level=lib.kni:4 "
			 "--log-level=lib.acl:4 "
			 "--log-level=lib.power:4 "
			 "--log-level=lib.meter:4 "
			 "--log-level=lib.sched:4 "
			 "--log-level=lib.port:4 "
			 "--log-level=lib.table:4 "
			 "--log-level=lib.pipeline:4 "
			 "--log-level=lib.mbuf:4 "
			 "--log-level=lib.cryptodev:4 "
			 "--log-level=lib.efd:4 "
			 "--log-level=lib.eventdev:4 "
			 "--log-level=lib.gso:4 "
			 "--log-level=user1:4 "
			 "--log-level=user2:4 "
			 "--log-level=user3:4 "
			 "--log-level=user4:4 "
			 "--log-level=user5:4 "
			 "--log-level=user6:4 "
			 "--log-level=user7:4 "
			 "--log-level=user8:4 "
			 "--no-telemetry";

int
copy_ascii(char *dst, size_t dst_sz, const void *src, size_t src_sz)
{
	const uint8_t	*str = src;
	int		 i, len = src_sz;

	assert(dst != NULL);
	assert(src != NULL);

	/* Trim trailing spaces */
	while (len > 0 && str[len - 1] == ' ')
		len--;

	if (len >= dst_sz)
		return -1;

	for (i = 0; i < len; i++, str++) {
		if (*str >= 0x20 && *str <= 0x7E)
			dst[i] = (char)*str;
		else
			dst[i] = '.';
	}
	dst[len] = '\0';

	return 0;
}
