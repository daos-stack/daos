/**
 * (C) Copyright 2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B620873.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
