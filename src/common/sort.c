/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of daos
 *
 * common/sort.c
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#define D_LOGFAC	DD_FAC(common)

#include <daos/common.h>

/**
 * Combsort for an array.
 *
 * It always returns zero if \a unique is false, which means array can
 * have multiple elements with the same key.
 * It returns error if \a unique is true, and there are more than one
 * elements have the same key.
 */
int
daos_array_sort(void *array, unsigned int len, bool unique,
		daos_sort_ops_t *ops)
{
	bool	swapped;
	int	gap;
	int	rc;

	for (gap = len, swapped = true; gap > 1 || swapped; ) {
		int	i;
		int	j;

		gap = gap * 10 / 13;
		if (gap == 9 || gap == 10)
			gap = 11;

		if (gap < 1)
			gap = 1;

		swapped = false;
		for (i = 0, j = gap; j < len; i++, j++) {
			rc = ops->so_cmp(array, i, j);
			if (rc == 0 && unique)
				return -DER_INVAL;

			if (rc > 0) {
				ops->so_swap(array, i, j);
				swapped = true;
			}
		}
	}
	return 0;
}

/**
 * Binary search in a sorted array.
 *
 * It returns index of the found element, and -1 if key is nonexistent in the
 * array.
 * If there are multiple elements have the same key, it returns the first
 * appearance.
 */
int
daos_array_find(void *array, unsigned int len, uint64_t key,
		daos_sort_ops_t *ops)
{
	int	start;
	int	end;
	int	cur = 0;
	int	rc = 0;

	D_ASSERT(len > 0);
	D_ASSERT(ops->so_cmp_key != NULL);

	for (start = 0, end = len - 1; start <= end; ) {
		cur = (start + end) / 2;

		rc = ops->so_cmp_key(array, cur, key);
		if (rc == 0)
			break;

		if (rc < 0)
			start = cur + 1;
		else
			end = cur - 1;
	}
	if (rc != 0)
		return -1; /* not found */

	for (; cur > 0; cur--) {
		rc = ops->so_cmp_key(array, cur - 1, key);
		if (rc != 0)
			break;
	}
	return cur;
}

void
daos_array_shuffle(void *array, unsigned int len, daos_sort_ops_t *ops)
{
	int		n;
	int		i;

	for (n = len; n > 0; n--) {
		i = rand() % n;
		if (i != n - 1)
			ops->so_swap(array, i, n - 1);
	}
}
