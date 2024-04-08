/**
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
 * Sort an array (can be partial array if \a start is non-zero)
 *
 * It always returns zero if \a unique is false, which means array can
 * have multiple elements with the same key.
 * It returns error if \a unique is true, and there are more than one
 * elements have the same key.
 */
int
daos_array_sort_adv(void *array, unsigned int start, unsigned int len, bool unique,
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
		for (i = start, j = start + gap; j < start + len; i++, j++) {
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

int
daos_array_sort(void *array, unsigned int len, bool unique,
		daos_sort_ops_t *ops)
{
	return daos_array_sort_adv(array, 0, len, unique, ops);
}

enum {
	/* find the element whose key is equal to provided key */
	FIND_OPC_EQ,
	/* find the element whose key is less than or equal to provided key
	 */
	FIND_OPC_LE,
	/* find the element whose key is greater than or equal to provided key
	 */
	FIND_OPC_GE
};

/**
 * Binary search in a sorted array.
 *
 * It returns index of the found element, and -1 if key is nonexistent in the
 * array.
 * If there are multiple elements have the same key, it returns the first
 * appearance.
 * By specifying start_off and len it can search a partial range of the array.
 */
static int
array_bin_search(void *array, unsigned int start_off, unsigned int len, uint64_t key, int opc,
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

		rc = ops->so_cmp_key(array, start_off + cur, key);
		if (rc == 0)
			break;

		if (rc < 0)
			start = cur + 1;
		else
			end = cur - 1;
	}
	if (rc < 0) {
		/* array[cur]::key is smaller than @key */
		switch (opc) {
		case FIND_OPC_EQ:
			return -1; /* not found */
		case FIND_OPC_LE:
			return start_off + cur;
		case FIND_OPC_GE:
			return (cur == len - 1) ? -1 : start_off + cur + 1;
		}
	} else if (rc > 0) {
		/* array[cur]::key is larger than @key */
		switch (opc) {
		case FIND_OPC_EQ:
			return -1; /* not found */
		case FIND_OPC_LE:
			if (cur == 0)
				return -1;
			return start_off + cur - 1;
		case FIND_OPC_GE:
			return start_off + cur;
		}
	}

	for (; cur > 0; cur--) {
		rc = ops->so_cmp_key(array, start_off + cur - 1, key);
		if (rc != 0)
			break;
	}
	return start_off + cur;
}

int
daos_array_find(void *array, unsigned int len, uint64_t key,
		daos_sort_ops_t *ops)
{
	return array_bin_search(array, 0, len, key, FIND_OPC_EQ, ops);
}

int
daos_array_find_adv(void *array, unsigned int start, unsigned int len, uint64_t key,
		daos_sort_ops_t *ops)
{
	return array_bin_search(array, start, len, key, FIND_OPC_EQ, ops);
}

/* return the element whose key is less than or equal to @key */
int
daos_array_find_le(void *array, unsigned int len, uint64_t key,
		   daos_sort_ops_t *ops)
{
	return array_bin_search(array, 0, len, key, FIND_OPC_LE, ops);
}

int
daos_array_find_le_adv(void *array, unsigned int start, unsigned int len, uint64_t key,
		   daos_sort_ops_t *ops)
{
	return array_bin_search(array, start, len, key, FIND_OPC_LE, ops);
}

/* return the element whose key is greater than or equal to @key */
int
daos_array_find_ge(void *array, unsigned int len, uint64_t key,
		   daos_sort_ops_t *ops)
{
	return array_bin_search(array, 0, len, key, FIND_OPC_GE, ops);
}

int
daos_array_find_ge_adv(void *array, unsigned int start, unsigned int len, uint64_t key,
		   daos_sort_ops_t *ops)
{
	return array_bin_search(array, start, len, key, FIND_OPC_GE, ops);
}

void
daos_array_shuffle(void *array, unsigned int len, daos_sort_ops_t *ops)
{
	int		n;
	int		i;

	for (n = len; n > 0; n--) {
		i = d_rand() % n;
		if (i != n - 1)
			ops->so_swap(array, i, n - 1);
	}
}
