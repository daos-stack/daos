/**
 * (C) Copyright 2016 Intel Corporation.
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
 * This file is part of vos
 * src/vos/tests/vts_chash_table.c
 * Launcher for all tests
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <omp.h>
#include <inttypes.h>
#include <daos/common.h>
#include <vos_chash_table.h>
#include <cmocka.h>

#define CHTABLE_BSIZE 10
#define CHTABLE_NKEYS 100
#define NUM_THREADS 8

struct chtable_args {
	char		*fname;
	PMEMobjpool	*pop;
};

int
compare_integers(const void *a, const void *b)
{
	if (*(uint64_t *)a == *(uint64_t *)b)
		return 0;
	else
		return -1;
}

void
print_integer_keys(const void *a)
{
	print_message("Key: "DF_U64"\t", *(uint64_t *)a);
}

void
print_integer_values(const void *a)
{
	print_message("Value: "DF_U64"\n", *(uint64_t *)a);
}

static vos_chash_ops_t my_hops = {
	.hop_key_cmp	= compare_integers,
	.hop_key_print	= print_integer_keys,
	.hop_val_print	= print_integer_values,
};

static int
test_multithreaded_ops(PMEMobjpool *pop, int bucket_size, int num_keys,
		       int num_threads)
{

	uint64_t i, *keys = NULL, *values = NULL;
	int ret = 0;
	TOID(struct vos_chash_table) hashtable;

	keys = (uint64_t *) malloc(num_keys * sizeof(uint64_t));
	assert_ptr_not_equal(keys, NULL);
	memset(keys, 0, num_keys * sizeof(uint64_t));

	values = (uint64_t *) malloc(num_keys * sizeof(uint64_t));
	assert_ptr_not_equal(values, NULL);
	memset(values, 0, num_keys * sizeof(uint64_t));

	print_message("Multithreaded test with %d threads\n", NUM_THREADS);
	vos_chash_create(pop, bucket_size, 100, true, CRC64, &hashtable,
			 &my_hops);

	#pragma omp parallel num_threads(num_threads)
	{
		#pragma omp for
		for (i = 0; i < num_keys; i++) {
			keys[i] = rand() % 100000 + 1;
			values[i] = rand() % 10;
			ret = vos_chash_insert(pop, hashtable,
					       (void *)(keys + i),
					       sizeof(keys + i),
					       (void *)(values + i),
					       sizeof(values + i));
			if (ret)
				print_error("Insert failed\n");

		}
	}

	#pragma omp parallel num_threads(num_threads)
	{
		void *value_ret = NULL;
		#pragma omp for
		for (i = 0; i < num_keys; i++) {
			ret = vos_chash_lookup(pop, hashtable,
					(void *)(keys + i),
					sizeof(keys + i),
					&value_ret);
			assert_int_equal(ret, 0);
			if (!ret &&  (value_ret != NULL))
				assert_false(values[i] !=
					     *(uint64_t *)value_ret);
		}

		if (omp_get_thread_num() == 1) {
			ret = vos_chash_remove(pop, hashtable,
					(void *)(keys + 1),
					sizeof(keys + 1));
			assert_int_equal(ret, 0);
		}
		if (omp_get_thread_num() == 4) {
			ret = vos_chash_remove(pop,
					hashtable,
					(void *)(keys + 3),
					sizeof(keys + 3));
			assert_int_equal(ret, 0);
		}
	}
	vos_chash_destroy(pop, hashtable);
	free(keys);
	free(values);

	return ret;
}

static int
test_single_thread_ops(PMEMobjpool *pop, int bucket_size, int num_keys)
{

	void *value_ret = NULL;
	uint64_t i, *keys = NULL, *values = NULL;
	int ret = 0;
	TOID(struct vos_chash_table) hashtable;


	keys = (uint64_t *) malloc(num_keys * sizeof(uint64_t));
	assert_ptr_not_equal(keys, NULL);
	memset(keys, 0, num_keys * sizeof(uint64_t));
	values = (uint64_t *) malloc(num_keys * sizeof(uint64_t));
	assert_ptr_not_equal(values, NULL);
	memset(values, 0, num_keys * sizeof(uint64_t));

	vos_chash_create(pop, bucket_size, 100, true, CRC64, &hashtable,
			 &my_hops);

	for (i = 0; i < num_keys; i++) {
		keys[i] = rand() % 100000 + 1;
		values[i] = rand() % 10;
		ret = vos_chash_insert(pop, hashtable,
				       (void *)(keys + i),
				       sizeof(keys + i),
				       (void *)(values + i),
				       sizeof(values + i));
		assert_int_equal(ret, 0);
	}

	for (i = 0; i < num_keys; i++) {
		ret = vos_chash_lookup(pop, hashtable,
				       (void *)(keys + i),
				       sizeof(keys + i),
				       &value_ret);
		assert_int_equal(ret, 0);
		if (!ret &&  (value_ret != NULL))
			assert_false(values[i] != *(uint64_t *)value_ret);
	}

	ret = vos_chash_remove(pop, hashtable, (void *)(keys + 1),
			       sizeof(keys + 1));
	assert_int_equal(ret, 0);
	ret = vos_chash_remove(pop, hashtable, (void *)(keys + 3),
			       sizeof(keys + 3));
	assert_int_equal(ret, 0);
	vos_chash_destroy(pop, hashtable);
	free(keys);
	free(values);

	return ret;
}

static int
setup(void **state)
{
	struct chtable_args	*arg = NULL;
	int			ret = 0;

	arg = malloc(sizeof(struct chtable_args));
	assert_ptr_not_equal(arg, NULL);

	ret = vts_alloc_gen_fname(&arg->fname);
	assert_int_equal(ret, 0);

	if (vts_file_exists(arg->fname))
		remove(arg->fname);
	arg->pop = pmemobj_create(arg->fname,
				  "Hashtable test", 67108864, 0666);
	assert_ptr_not_equal(arg->pop, NULL);
	*state = arg;
	return ret;
}

static int
teardown(void **state)
{
	struct chtable_args	*arg = *state;
	int			ret = 0;

	ret = remove(arg->fname);
	assert_int_equal(ret, 0);

	return ret;
}

static void
ch_single_threaded(void **state)
{
	struct chtable_args	*arg = *state;
	int			ret = 0;

	ret = test_single_thread_ops(arg->pop, CHTABLE_BSIZE, CHTABLE_NKEYS);
	assert_int_equal(ret, 0);
}

static void
ch_multi_threaded(void **state)
{
	struct chtable_args	*arg = *state;
	int			ret = 0;

	ret = test_multithreaded_ops(arg->pop, CHTABLE_BSIZE, CHTABLE_NKEYS,
				     NUM_THREADS);
	assert_int_equal(ret, 0);
}

static const struct CMUnitTest chtable_tests[] = {
	{ "VOS300: CHTABLE single threaded ops test",
	  ch_single_threaded, NULL, NULL},
	{ "VOS301: CHTABLE multi threaded ops test",
	  ch_multi_threaded, NULL, NULL},
};

int run_chtable_test(void)
{
	return cmocka_run_group_tests_name("VOS chtable tests", chtable_tests,
					   setup, teardown);
}
