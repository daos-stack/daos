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
