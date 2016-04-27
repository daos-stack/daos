/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2015 Intel Corporation.
 */
/**
 * This file is part of daos_sr
 *
 * dsr/tests/other.c
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#include <getopt.h>
#include <daos/common.h>

static int
sort_cmp(void *array, int a, int b)
{
	int	*int_arr = array;

	if (int_arr[a] > int_arr[b])
		return 1;
	if (int_arr[a] < int_arr[b])
		return -1;
	return 0;
}

static void
sort_swap(void *array, int a, int b)
{
	int	*int_arr = array;
	int	 tmp;

	tmp = int_arr[a];
	int_arr[a] = int_arr[b];
	int_arr[b] = tmp;
}

static daos_sort_ops_t sort_ops = {
	.so_cmp		= sort_cmp,
	.so_swap	= sort_swap,
};

static struct option opts[] = {
	{ "sort",		required_argument,	NULL,   's'},
	{  NULL,		0,			NULL,	 0 }
};

static int
comb_sort_test(int num)
{
	int		*arr;
	int		 i;
	struct timeval	 tv;

	arr = calloc(num, sizeof(int));
	if (arr == NULL)
		return -ENOMEM;

	gettimeofday(&tv, NULL);
	srand(tv.tv_usec);

	for (i = 0; i < num; i++) {
		arr[i] = rand() % (4 * num);
		D_PRINT("%d ", arr[i]);
	}
	D_PRINT("\n");

	daos_array_sort(arr, num, false, &sort_ops);

	D_PRINT("Sorted:\n");
	for (i = 0; i < num; i++)
		D_PRINT("%d ", arr[i]);

	D_PRINT("\n");
	free(arr);
	return 0;
}

int
main(int argc, char **argv)
{
	int	opc;
	int	rc = 0;

	while ((opc = getopt_long(argc, argv, "s:", opts, NULL)) != -1) {
		int	num;

		switch (opc) {
		default:
			return -EINVAL;
		case 's':
			num = strtoul(optarg, NULL, 0);
			if (num <= 0)
				return -EINVAL;

			rc = comb_sort_test(num);
			break;
		}
	}
	return rc;
}
