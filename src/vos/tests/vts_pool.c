/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of dsm
 *
 * vos/tests/vts_pool.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venaktesan@intel.com>
 */
#define D_LOGFAC	DD_FAC(tests)

#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "vts_common.h"

#include <vos_layout.h>
#include <daos_srv/vos.h>
#include <vos_internal.h>

#define VPOOL_TEST_WAL_SZ	(1ULL << 25) /* default_cluster_sz(): DAOS_BS_CLUSTER_SZ: 32MB */

struct vp_test_args {
	char			**fname;
	int			nfiles;
	int			*seq_cnt;
	enum vts_ops_type	**ops_seq;
	bool			*fcreate;
	daos_handle_t		*poh;
	uuid_t			*uuid;
};

static inline void
pool_set_param(enum vts_ops_type seq[], int cnt, bool flag, bool *cflag,
	       int *seq_cnt, enum vts_ops_type **ops)
{
	*seq_cnt = cnt;
	memcpy(*ops, seq, cnt * sizeof(enum vts_ops_type));
	*cflag = flag;
}

static int
pool_file_setup(void **state)
{
	struct vp_test_args	*arg = *state;
	int			ret = 0;

	D_ALLOC(arg->fname, sizeof(char *));
	assert_ptr_not_equal(arg->fname, NULL);

	D_ALLOC_ARRAY(arg->poh, 10);
	assert_ptr_not_equal(arg->poh, NULL);

	ret = vts_alloc_gen_fname(&arg->fname[0]);
	assert_int_equal(ret, 0);
	return 0;
}

static int
pool_file_destroy(void **state)
{
	struct vp_test_args	*arg = *state;
	int			ret = 0;

	if (arg->fname[0]) {
		ret = remove(arg->fname[0]);
		if (ret != 0)
			D_ERROR("Removing %s failed\n",
				arg->fname[0]);
		D_FREE(arg->fname[0]);
	}
	D_FREE(arg->fname);
	D_FREE(arg->poh);
	return 0;
}

static void
pool_ref_count_test(void **state)
{
	int			ret = 0, i;
	uuid_t			uuid;
	struct vp_test_args	*arg = *state;
	int			num = 10;

	uuid_generate(uuid);
	ret = vos_pool_create(arg->fname[0], uuid, VPOOL_256M, 0, 0, NULL);
	for (i = 0; i < num; i++) {
		ret = vos_pool_open(arg->fname[0], uuid, 0, &arg->poh[i]);
		assert_rc_equal(ret, 0);
	}
	for (i = 0; i < num - 1; i++) {
		ret = vos_pool_close(arg->poh[i]);
		assert_rc_equal(ret, 0);
	}
	ret = vos_pool_destroy(arg->fname[0], uuid);
	assert_rc_equal(ret, -DER_BUSY);
	ret = vos_pool_close(arg->poh[num - 1]);
	assert_rc_equal(ret, 0);
	ret = vos_pool_destroy(arg->fname[0], uuid);
	assert_rc_equal(ret, 0);
}

static void
pool_policy_update(void **state)
{
	int			ret = 0;
	uuid_t			uuid;
	struct vp_test_args	*arg = *state;
	struct policy_desc_t	policy_desc;
	struct vos_pool		*vp;

	uuid_generate(uuid);
	ret = vos_pool_create(arg->fname[0], uuid, VPOOL_256M, 0, 0, NULL);

	ret = vos_pool_open(arg->fname[0], uuid, 0, &arg->poh[0]);
	assert_rc_equal(ret, 0);

	policy_desc.policy = DAOS_MEDIA_POLICY_WRITE_INTENSIVITY;
	policy_desc.params[0] = 123;
	policy_desc.params[1] = 246;
	policy_desc.params[2] = 667;
	policy_desc.params[3] = 993;

	ret = vos_pool_ctl(arg->poh[0], VOS_PO_CTL_SET_POLICY, &policy_desc);
	assert_rc_equal(ret, 0);

	vp = vos_hdl2pool(arg->poh[0]);
	if (vp == NULL)
		fail_msg("Cannot get vos_pool from hdl");

	assert_int_equal(vp->vp_policy_desc.policy, policy_desc.policy);
	assert_int_equal(vp->vp_policy_desc.params[0], policy_desc.params[0]);
	assert_int_equal(vp->vp_policy_desc.params[1], policy_desc.params[1]);
	assert_int_equal(vp->vp_policy_desc.params[2], policy_desc.params[2]);
	assert_int_equal(vp->vp_policy_desc.params[3], policy_desc.params[3]);

	ret = vos_pool_close(arg->poh[0]);
	assert_rc_equal(ret, 0);
	ret = vos_pool_destroy(arg->fname[0], uuid);
	assert_rc_equal(ret, 0);
}

static void
pool_interop(void **state)
{
	struct vp_test_args	*arg = *state;
	uuid_t			uuid;
	daos_handle_t		poh;
	int			ret;

	FAULT_INJECTION_REQUIRED();

	uuid_generate(uuid);

	daos_fail_loc_set(FLC_POOL_DF_VER | DAOS_FAIL_ONCE);
	ret = vos_pool_create(arg->fname[0], uuid, VPOOL_256M, 0, 0, NULL);
	assert_rc_equal(ret, 0);

	ret = vos_pool_open(arg->fname[0], uuid, 0, &poh);
	assert_rc_equal(ret, -DER_DF_INCOMPT);

	ret = vos_pool_destroy(arg->fname[0], uuid);
	assert_rc_equal(ret, 0);
}

static void
pool_ops_run(void **state)
{
	int			ret = 0, i, j;
	struct vp_test_args	*arg = *state;
	vos_pool_info_t		pinfo;

	for (j = 0; j < arg->nfiles; j++) {
		for (i = 0; i < arg->seq_cnt[j]; i++) {
			daos_handle_t *poh = NULL;

			switch (arg->ops_seq[j][i]) {
			case CREAT_OPEN:
				poh = &arg->poh[j];
				/* fall through */
			case CREAT:
				uuid_generate(arg->uuid[j]);
				if (arg->fcreate[j]) {
					ret =
					vts_pool_fallocate(&arg->fname[j]);
					assert_int_equal(ret, 0);
					ret = vos_pool_create_ex(arg->fname[j],
							      arg->uuid[j],
							      0, 0,
							      VPOOL_TEST_WAL_SZ,
							      0, poh);
				} else {
					ret =
					vts_alloc_gen_fname(&arg->fname[j]);
					assert_int_equal(ret, 0);
					ret = vos_pool_create_ex(arg->fname[j],
							      arg->uuid[j],
							      VPOOL_256M, 0,
							      VPOOL_TEST_WAL_SZ,
							      0, poh);
				}
				break;
			case OPEN:
				ret = vos_pool_open(arg->fname[j], arg->uuid[j],
						    0, &arg->poh[j]);
				break;
			case CLOSE:
				ret = vos_pool_close(arg->poh[j]);
				break;
			case DESTROY:
				ret = vos_pool_destroy(arg->fname[j],
						       arg->uuid[j]);
				break;
			case QUERY:
				ret = vos_pool_query(arg->poh[j], &pinfo);
				assert_rc_equal(ret, 0);
				assert_int_equal(pinfo.pif_cont_nr, 0);
				assert_false(SCM_TOTAL(&pinfo.pif_space) !=
						VPOOL_256M);
				assert_false(NVME_TOTAL(&pinfo.pif_space) != 0);
				assert_false(SCM_FREE(&pinfo.pif_space) >
				     (VPOOL_256M - sizeof(struct vos_pool_df)));
				assert_false(NVME_FREE(&pinfo.pif_space) != 0);
				break;
			default:
				fail_msg("Shouldn't be here Unknown ops?\n");
				break;
			}
			if (arg->ops_seq[j][i] != QUERY)
				assert_rc_equal(ret, 0);
		}
	}
}

static int pool_allocate_params(int nfiles, int ops,
			   struct vp_test_args *test_args)
{
	int i;

	test_args->nfiles = nfiles;
	D_ALLOC_ARRAY(test_args->fname, nfiles);
	assert_ptr_not_equal(test_args->fname, NULL);

	D_ALLOC_ARRAY(test_args->seq_cnt, nfiles);
	assert_ptr_not_equal(test_args->seq_cnt, NULL);

	D_ALLOC_ARRAY(test_args->ops_seq, nfiles);
	assert_ptr_not_equal(test_args->ops_seq, NULL);

	for (i = 0; i < nfiles; i++) {
		D_ALLOC_ARRAY(test_args->ops_seq[i], ops);
		assert_ptr_not_equal(test_args->ops_seq[i], NULL);
	}

	D_ALLOC_ARRAY(test_args->fcreate, nfiles);
	assert_ptr_not_equal(test_args->fcreate, NULL);

	D_ALLOC_ARRAY(test_args->poh, nfiles);
	assert_ptr_not_equal(test_args->poh, NULL);


	D_ALLOC_ARRAY(test_args->uuid, nfiles);
	assert_ptr_not_equal(test_args->uuid, NULL);

	return 0;
}

static int
setup(void **state)
{
	struct vp_test_args *test_arg = NULL;

	D_ALLOC(test_arg, sizeof(struct vp_test_args));
	assert_ptr_not_equal(test_arg, NULL);
	*state = test_arg;

	return 0;
}

static int
teardown(void **state)
{
	struct vp_test_args	*arg = *state;

	if (arg == NULL) {
		print_message("state not set, likely due to group-setup"
			      " issue\n");
		return 0;
	}

	D_FREE(arg);
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
		int cnt =  arg->seq_cnt[i];

		/** Call vos_pool_kill() here to reclaim SMD_DEV space
		 * by deleting pool blobs if pool was not destroyed yet.
		 */
		D_ASSERT(cnt > 0 && cnt <= QUERY);
		if (arg->ops_seq[i][--cnt] != DESTROY)
			vos_pool_kill(arg->uuid[i], 0);

		if (vts_file_exists(arg->fname[i]))
			assert_int_equal(remove(arg->fname[i]), 0);
		if (arg->fname[i])
			D_FREE(arg->fname[i]);
		if (arg->ops_seq[i])
			D_FREE(arg->ops_seq[i]);
	}

	if (arg->fname)
		D_FREE(arg->fname);
	if (arg->seq_cnt)
		D_FREE(arg->seq_cnt);
	if (arg->ops_seq)
		D_FREE(arg->ops_seq);
	if (arg->fcreate)
		D_FREE(arg->fcreate);
	if (arg->poh)
		D_FREE(arg->poh);
	if (arg->uuid)
		D_FREE(arg->uuid);

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
	enum vts_ops_type	tmp[]  = {CREAT};
	int			i, nfiles;

	/** Create number of files as CPUs */
	nfiles = sysconf(_SC_NPROCESSORS_ONLN);
	/* Limit to 16 to save file space when bdev=aio */
	nfiles = min(nfiles, 16);
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

static void
pool_set_sequence(void **state, bool flag, int num_ops,
		  enum vts_ops_type seq[])
{
	struct vp_test_args	*arg = *state;

	pool_allocate_params(1, num_ops, arg);
	pool_set_param(seq, num_ops, flag, &arg->fcreate[0],
		  &arg->seq_cnt[0], &arg->ops_seq[0]);
}

static int
pool_open_close(void **state)
{
	enum vts_ops_type tmp[] = {CREAT, OPEN, CLOSE};
	int num_ops = sizeof(tmp) / sizeof(enum vts_ops_type);

	pool_set_sequence(state, true, num_ops, tmp);
	return 0;
}

static int
pool_destroy(void **state)
{
	enum vts_ops_type tmp[] = {CREAT, DESTROY};
	int num_ops = sizeof(tmp) / sizeof(enum vts_ops_type);

	pool_set_sequence(state, true, num_ops, tmp);
	return 0;
}

static int
pool_query_after_open(void **state)
{
	enum vts_ops_type tmp[] = {CREAT, OPEN, QUERY, CLOSE};
	int num_ops = sizeof(tmp) / sizeof(enum vts_ops_type);

	pool_set_sequence(state, true, num_ops, tmp);
	return 0;
}

static int
pool_all_empty_file(void **state)
{
	enum vts_ops_type tmp[] = {CREAT, OPEN, QUERY, CLOSE, DESTROY};
	int num_ops = sizeof(tmp) / sizeof(enum vts_ops_type);

	pool_set_sequence(state, false, num_ops, tmp);
	return 0;

}

static int
pool_all(void **state)
{
	enum vts_ops_type tmp[] = {CREAT, OPEN, QUERY, CLOSE, DESTROY};
	int num_ops = sizeof(tmp) / sizeof(enum vts_ops_type);

	pool_set_sequence(state, true, num_ops, tmp);
	return 0;
}

static int
pool_create_open_close(void **state)
{
	enum vts_ops_type tmp[] = {CREAT_OPEN, CLOSE};
	int num_ops = sizeof(tmp) / sizeof(enum vts_ops_type);

	pool_set_sequence(state, false, num_ops, tmp);
	return 0;
}

static void
pool_open_excl_test(void **state)
{
	int			ret = 0;
	uuid_t			uuid;
	struct vp_test_args	*arg = *state;

	uuid_generate(uuid);

	print_message("open EXCL shall fail upon existing create opener\n");
	ret = vos_pool_create(arg->fname[0], uuid, VPOOL_256M, 0, 0,
			      &arg->poh[0]);
	assert_rc_equal(ret, 0);
	ret = vos_pool_open(arg->fname[0], uuid, VOS_POF_EXCL, &arg->poh[1]);
	assert_rc_equal(ret, -DER_BUSY);
	ret = vos_pool_close(arg->poh[0]);
	assert_rc_equal(ret, 0);
	ret = vos_pool_destroy(arg->fname[0], uuid);
	assert_rc_equal(ret, 0);

	print_message("open EXCL shall fail upon existing opener\n");
	ret = vos_pool_create(arg->fname[0], uuid, VPOOL_256M, 0, 0, NULL);
	assert_rc_equal(ret, 0);
	ret = vos_pool_open(arg->fname[0], uuid, 0, &arg->poh[0]);
	assert_rc_equal(ret, 0);
	ret = vos_pool_open(arg->fname[0], uuid, VOS_POF_EXCL, &arg->poh[1]);
	assert_rc_equal(ret, -DER_BUSY);
	ret = vos_pool_close(arg->poh[0]);
	assert_rc_equal(ret, 0);
	ret = vos_pool_destroy(arg->fname[0], uuid);
	assert_rc_equal(ret, 0);

	print_message("open EXCL shall fail upon existing EXCL opener\n");
	ret = vos_pool_create(arg->fname[0], uuid, VPOOL_256M, 0, 0, NULL);
	assert_rc_equal(ret, 0);
	ret = vos_pool_open(arg->fname[0], uuid, VOS_POF_EXCL, &arg->poh[0]);
	assert_rc_equal(ret, 0);
	ret = vos_pool_open(arg->fname[0], uuid, VOS_POF_EXCL, &arg->poh[1]);
	assert_rc_equal(ret, -DER_BUSY);
	ret = vos_pool_close(arg->poh[0]);
	assert_rc_equal(ret, 0);
	ret = vos_pool_destroy(arg->fname[0], uuid);
	assert_rc_equal(ret, 0);

	print_message("open EXCL shall fail upon existing EXCL create "
		      "opener\n");
	ret = vos_pool_create(arg->fname[0], uuid, VPOOL_256M, 0, VOS_POF_EXCL,
			      &arg->poh[0]);
	assert_rc_equal(ret, 0);
	ret = vos_pool_open(arg->fname[0], uuid, VOS_POF_EXCL, &arg->poh[1]);
	assert_rc_equal(ret, -DER_BUSY);
	ret = vos_pool_close(arg->poh[0]);
	assert_rc_equal(ret, 0);
	ret = vos_pool_destroy(arg->fname[0], uuid);
	assert_rc_equal(ret, 0);

	print_message("open shall fail upon existing EXCL opener\n");
	ret = vos_pool_create(arg->fname[0], uuid, VPOOL_256M, 0, 0, NULL);
	assert_rc_equal(ret, 0);
	ret = vos_pool_open(arg->fname[0], uuid, VOS_POF_EXCL, &arg->poh[0]);
	assert_rc_equal(ret, 0);
	ret = vos_pool_open(arg->fname[0], uuid, 0, &arg->poh[1]);
	assert_rc_equal(ret, -DER_BUSY);
	ret = vos_pool_close(arg->poh[0]);
	assert_rc_equal(ret, 0);
	ret = vos_pool_destroy(arg->fname[0], uuid);
	assert_rc_equal(ret, 0);

	print_message("open shall fail upon existing EXCL create opener\n");
	ret = vos_pool_create(arg->fname[0], uuid, VPOOL_256M, 0, VOS_POF_EXCL,
			      &arg->poh[0]);
	assert_rc_equal(ret, 0);
	ret = vos_pool_open(arg->fname[0], uuid, 0, &arg->poh[1]);
	assert_rc_equal(ret, -DER_BUSY);
	ret = vos_pool_close(arg->poh[0]);
	assert_rc_equal(ret, 0);
	ret = vos_pool_destroy(arg->fname[0], uuid);
	assert_rc_equal(ret, 0);
}

static const struct CMUnitTest pool_tests[] = {
	{ "VOS1: Create Pool with existing files (File Count no:of cpus)",
		pool_ops_run, pool_create_exists, pool_unit_teardown},
	{ "VOS2: Create Pool with empty files (File Count no:of cpus)",
		pool_ops_run, pool_create_empty, pool_unit_teardown},
	{ "VOS3: Pool Destroy", pool_ops_run,
		pool_destroy, pool_unit_teardown},
	{ "VOS4: Pool DF interoperability", pool_interop,
		 pool_file_setup, pool_file_destroy},
	{ "VOS5: Pool Close after open", pool_ops_run,
		pool_open_close, pool_unit_teardown},
	{ "VOS6: Pool handle refcount", pool_ref_count_test,
		 pool_file_setup, pool_file_destroy},
	{ "VOS7: Pool Query after open", pool_ops_run,
		pool_query_after_open, pool_unit_teardown},
	{ "VOS8: Pool all APIs empty file handle", pool_ops_run,
		pool_all_empty_file, pool_unit_teardown},
	{ "VOS9: Pool all APIs with existing file", pool_ops_run,
		pool_all, pool_unit_teardown},
	{ "VOS10: Pool Close after create and open", pool_ops_run,
		pool_create_open_close, pool_unit_teardown},
	{ "VOS11: Pool exclusive open", pool_open_excl_test,
		pool_file_setup, pool_file_destroy},
	{ "VOS12: Pool policy update", pool_policy_update,
		 pool_file_setup, pool_file_destroy},

};


int
run_pool_test(const char *cfg)
{
	char	test_name[DTS_CFG_MAX];

	dts_create_config(test_name, "Pool tests %s", cfg);
	return cmocka_run_group_tests_name(test_name, pool_tests,
					   setup, teardown);
}
