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
 * This file is part of dsm
 *
 * vos/tests/vts_pool.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venaktesan@intel.com>
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <vts_common.h>

#include <daos_srv/vos.h>

struct vp_test_args {
	char		**fname;
	int		nfiles;
	int		*seq_cnt;
	enum ops_type	**ops_seq;
	bool		*fcreate;
	daos_handle_t	*poh;
	uuid_t		*uuid;
};

static inline void
pool_set_param(enum ops_type seq[], int cnt, bool flag, bool *cflag,
	       int *seq_cnt, enum ops_type **ops)
{
	*seq_cnt = cnt;
	memcpy(*ops, seq, cnt * sizeof(enum ops_type));
	*cflag = flag;
}

static void
pool_ops_run(void **state)
{
	int			ret = 0, i, j;
	struct vp_test_args	*arg = *state;
	vos_pool_info_t		pinfo;


	for (j = 0; j < arg->nfiles; j++) {
		for (i = 0; i < arg->seq_cnt[j]; i++) {
			switch (arg->ops_seq[j][i]) {
			case CREAT:
				uuid_generate(arg->uuid[j]);
				if (arg->fcreate[j]) {
					ret = pool_fallocate(&arg->fname[j]);
					assert_int_equal(ret, 0);
					ret = vos_pool_create(arg->fname[j],
							      arg->uuid[j],
							      0, &arg->poh[j],
							      NULL);
				} else {
					ret = alloc_gen_fname(&arg->fname[j]);
					assert_int_equal(ret, 0);
					ret = vos_pool_create(arg->fname[j],
							      arg->uuid[j],
							      VPOOL_SIZE,
							      &arg->poh[j],
							      NULL);
				}
				break;
			case OPEN:
				ret = vos_pool_open(arg->fname[j],
						    arg->uuid[j],
						    &arg->poh[j],
						    NULL);
				break;
			case CLOSE:
				ret = vos_pool_close(arg->poh[j], NULL);
			break;
			case DESTROY:
				ret = vos_pool_destroy(arg->poh[j], NULL);
				break;
			case QUERY:
				ret = vos_pool_query(arg->poh[j], &pinfo,
						     NULL);
				assert_int_equal(ret, 0);
				assert_int_equal(pinfo.pif_ncos, 0);
				assert_int_equal(pinfo.pif_nobjs, 0);
				assert_false(pinfo.pif_size != VPOOL_SIZE);
				assert_false(pinfo.pif_avail !=
					     (VPOOL_SIZE - 80));
				break;
			default:
				fail_msg("Shoudln't be here Unkown ops?\n");
				break;
			}
			if (arg->ops_seq[j][i] != QUERY)
				assert_int_equal(ret, 0);
		}
	}
}

static int pool_allocate_params(int nfiles, int ops,
			   struct vp_test_args *test_args)
{
	int i;

	test_args->nfiles = nfiles;
	test_args->fname = (char **)malloc(nfiles * sizeof(char *));
	assert_ptr_not_equal(test_args->fname, NULL);
	test_args->seq_cnt = (int *)malloc(nfiles * sizeof(int));
	assert_ptr_not_equal(test_args->seq_cnt, NULL);
	test_args->ops_seq = (enum ops_type **)malloc
		(nfiles * sizeof(enum ops_type *));
	assert_ptr_not_equal(test_args->ops_seq, NULL);
	for (i = 0; i < nfiles; i++) {
		test_args->ops_seq[i] = (enum ops_type *)malloc
			(ops * sizeof(enum ops_type));
		assert_ptr_not_equal(test_args->ops_seq[i], NULL);
	}
	test_args->fcreate = (bool *)malloc(nfiles * sizeof(bool));
	assert_ptr_not_equal(test_args->fcreate, NULL);
	test_args->poh = (daos_handle_t *)malloc(nfiles *
						 sizeof(daos_handle_t));
	assert_ptr_not_equal(test_args->poh, NULL);
	test_args->uuid = (uuid_t *)malloc(nfiles * sizeof(uuid_t));
	assert_ptr_not_equal(test_args->uuid, NULL);
	return 0;
}

static int
setup(void **state)
{
	struct vp_test_args *test_arg = NULL;

	test_arg = malloc(sizeof(struct vp_test_args));
	assert_ptr_not_equal(test_arg, NULL);
	*state = test_arg;

	return 0;
}

static int
teardown(void **state)
{
	struct vp_test_args	*arg = *state;

	free(arg);
	return 0;
}

/**
 * Common teardown for all unit tests
 */
static int
pool_unit_teardown(void **state)
{
	struct vp_test_args	*arg = *state;
	int			i;

	for (i = 0; i < arg->nfiles; i++) {
		if (file_exists(arg->fname[i]))
			remove(arg->fname[i]);
		if (arg->fname[i])
			free(arg->fname[i]);
		if (arg->ops_seq[i])
			free(arg->ops_seq[i]);
	}

	if (arg->fname)
		free(arg->fname);
	if (arg->seq_cnt)
		free(arg->seq_cnt);
	if (arg->ops_seq)
		free(arg->ops_seq);
	if (arg->fcreate)
		free(arg->fcreate);
	if (arg->poh)
		free(arg->poh);
	if (arg->uuid)
		free(arg->uuid);

	return 0;
}

/**
 * Setups for different unit Tests
 */
static inline void
create_pools_test_construct(struct vp_test_args **arr,
			    bool create_type)
{
	struct vp_test_args	*arg   = *arr;
	enum ops_type		tmp[]  = {CREAT};
	int			i, nfiles;

	/** Create number of files as CPUs */
	nfiles = sysconf(_SC_NPROCESSORS_ONLN);
	pool_allocate_params(nfiles, 1, arg);
	arg->nfiles = nfiles;
	print_message("Pool construct test with %d files\n",
		      nfiles);
	for (i = 0; i < nfiles; i++)
		pool_set_param(tmp, 1, create_type, &arg->fcreate[i],
			  &arg->seq_cnt[i], &arg->ops_seq[i]);

}

static int
pool_create_empty(void **state)
{
	struct vp_test_args	*arg   = *state;

	create_pools_test_construct(&arg, false);
	return 0;
}

static int
pool_create_exists(void **state)
{
	struct vp_test_args *arg = *state;

	/** This test fallocates */
	create_pools_test_construct(&arg, true);
	return 0;
}


static int
pool_open(void **state)
{
	struct vp_test_args *arg = *state;
	enum ops_type tmp[] = {CREAT, CLOSE, OPEN};

	pool_allocate_params(1, 1, arg);
	pool_set_param(tmp, 3, true, &arg->fcreate[0],
		  &arg->seq_cnt[0], &arg->ops_seq[0]);
	return 0;
}

static int
pool_close(void **state)
{
	struct vp_test_args *arg = *state;
	enum ops_type tmp[] = {CREAT, CLOSE};

	pool_allocate_params(1, 2, arg);
	pool_set_param(tmp, 2, true, &arg->fcreate[0],
		  &arg->seq_cnt[0], &arg->ops_seq[0]);

	return 0;
}

static int
pool_open_close(void **state)
{
	struct vp_test_args *arg = *state;
	enum ops_type tmp[] = {CREAT, CLOSE, OPEN, CLOSE};

	pool_allocate_params(1, 4, arg);
	pool_set_param(tmp, 4, true, &arg->fcreate[0],
		  &arg->seq_cnt[0], &arg->ops_seq[0]);

	return 0;
}

static int
pool_destroy(void **state)
{
	struct vp_test_args *arg = *state;
	enum ops_type tmp[] = {CREAT, DESTROY};

	pool_allocate_params(1, 2, arg);
	pool_set_param(tmp, 2, true, &arg->fcreate[0],
		  &arg->seq_cnt[0], &arg->ops_seq[0]);
	return 0;
}

static int
pool_destroy_after_open(void **state)
{
	struct vp_test_args *arg = *state;
	enum ops_type tmp[] = {CREAT, CLOSE, OPEN, DESTROY};

	pool_allocate_params(1, 4, arg);
	pool_set_param(tmp, 4, true, &arg->fcreate[0],
		  &arg->seq_cnt[0], &arg->ops_seq[0]);
	return 0;
}

static int
pool_query(void **state)
{
	struct vp_test_args *arg = *state;
	enum ops_type tmp[] = {CREAT, QUERY};

	pool_allocate_params(1, 2, arg);
	pool_set_param(tmp, 2, true, &arg->fcreate[0],
		  &arg->seq_cnt[0], &arg->ops_seq[0]);
	return 0;
}

static int
pool_query_after_open(void **state)
{
	struct vp_test_args *arg = *state;
	enum ops_type tmp[] = {CREAT, CLOSE, OPEN, QUERY};

	pool_allocate_params(1, 4, arg);
	pool_set_param(tmp, 4, true, &arg->fcreate[0],
		  &arg->seq_cnt[0], &arg->ops_seq[0]);
	return 0;
}

static int
pool_all_empty_file(void **state)
{
	struct vp_test_args *arg = *state;
	enum ops_type tmp[] = {CREAT, CLOSE, OPEN, QUERY, DESTROY};

	pool_allocate_params(1, 5, arg);
	pool_set_param(tmp, 5, false, &arg->fcreate[0],
		  &arg->seq_cnt[0], &arg->ops_seq[0]);
	return 0;

}

static int
pool_all(void **state)
{
	struct vp_test_args *arg = *state;
	enum ops_type tmp[] = {CREAT, CLOSE, OPEN, QUERY, DESTROY};

	pool_allocate_params(1, 5, arg);
	pool_set_param(tmp, 5, true, &arg->fcreate[0],
		  &arg->seq_cnt[0], &arg->ops_seq[0]);
	return 0;
}

static const struct CMUnitTest pool_tests[] = {
	{ "VOS1: Create Pool with existing files (File Count no:of cpus)",
		pool_ops_run, pool_create_exists, pool_unit_teardown},
	{ "VOS2: Create Pool with empty files (File Count no:of cpus)",
		pool_ops_run, pool_create_empty, pool_unit_teardown},
	{ "VOS3: Pool Open", pool_ops_run,
		pool_open, pool_unit_teardown},
	{ "VOS4: Pool Close", pool_ops_run,
		pool_close, pool_unit_teardown},
	{ "VOS5: Pool Destroy", pool_ops_run,
		pool_destroy, pool_unit_teardown},
	{ "VOS6: Pool Query", pool_ops_run,
		pool_query, pool_unit_teardown},
	{ "VOS7: Pool Close after open", pool_ops_run,
		pool_open_close, pool_unit_teardown},
	{ "VOS8: Pool Destroy after open", pool_ops_run,
		pool_destroy_after_open, pool_unit_teardown},
	{ "VOS9: Pool Query after open", pool_ops_run,
		pool_query_after_open, pool_unit_teardown},
	{ "VOS10: Pool all APIs empty file handle", pool_ops_run,
		pool_all_empty_file, pool_unit_teardown},
	{ "VOS11: Pool all APIs with existing file", pool_ops_run,
		pool_all, pool_unit_teardown},
};


int
run_pool_test(void)
{
	return cmocka_run_group_tests_name("VOS Pool tests", pool_tests,
					   setup, teardown);
}
