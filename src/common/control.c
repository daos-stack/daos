/**
 * (C) Copyright 2020-2021 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file implements functions shared with the control-plane.
 */
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Buffer to hold dynamically generated DPDK CLI options */
static char dpdk_cli_opts_buffer[2048];

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

/**
 * Build DPDK CLI options string with per-facility log levels.
 *
 * \param eal_level      Log level for EAL facility (1-8)
 * \param default_level  Default log level for other facilities (1-8)
 *
 * \return Pointer to static buffer containing DPDK CLI options string,
 *         or NULL on error.
 */
const char *
dpdk_cli_build_opts(int eal_level, int default_level)
{
	int ret;

	/* Validate log levels */
	if (eal_level < 1 || eal_level > 8 || default_level < 1 || default_level > 8) {
		return NULL;
	}

	/* Build with custom EAL level, others at default */
	ret = snprintf(dpdk_cli_opts_buffer, sizeof(dpdk_cli_opts_buffer),
		       "--log-level=lib.eal:%d "
		       "--log-level=lib.malloc:%d "
		       "--log-level=lib.ring:%d "
		       "--log-level=lib.mempool:%d "
		       "--log-level=lib.timer:%d "
		       "--log-level=pmd:%d "
		       "--log-level=lib.hash:%d "
		       "--log-level=lib.lpm:%d "
		       "--log-level=lib.kni:%d "
		       "--log-level=lib.acl:%d "
		       "--log-level=lib.power:%d "
		       "--log-level=lib.meter:%d "
		       "--log-level=lib.sched:%d "
		       "--log-level=lib.port:%d "
		       "--log-level=lib.table:%d "
		       "--log-level=lib.pipeline:%d "
		       "--log-level=lib.mbuf:%d "
		       "--log-level=lib.cryptodev:%d "
		       "--log-level=lib.efd:%d "
		       "--log-level=lib.eventdev:%d "
		       "--log-level=lib.gso:%d "
		       "--log-level=user1:%d "
		       "--log-level=user2:%d "
		       "--log-level=user3:%d "
		       "--log-level=user4:%d "
		       "--log-level=user5:%d "
		       "--log-level=user6:%d "
		       "--log-level=user7:%d "
		       "--log-level=user8:%d "
		       "--no-telemetry",
		       eal_level, default_level, default_level, default_level, default_level,
		       default_level, default_level, default_level, default_level, default_level,
		       default_level, default_level, default_level, default_level, default_level,
		       default_level, default_level, default_level, default_level, default_level,
		       default_level, default_level, default_level, default_level, default_level,
		       default_level, default_level, default_level, default_level);

	if (ret < 0 || ret >= sizeof(dpdk_cli_opts_buffer)) {
		return NULL;
	}

	return dpdk_cli_opts_buffer;
}
