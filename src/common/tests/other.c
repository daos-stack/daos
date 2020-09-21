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
 * This file is part of daos_sr
 *
 * src/common/tests/other.c
 */
#define D_LOGFAC	DD_FAC(tests)

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
	D_FREE(arr);
	return 0;
}

int
main(int argc, char **argv)
{
	int	opc;
	int	rc = 0;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		return rc;

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

	daos_debug_fini();
	return rc;
}
