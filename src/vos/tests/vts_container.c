/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of dsm
 *
 * vos/tests/vts_container.c
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
#include <vos_internal.h>

#include <daos_srv/vos.h>

#define VCT_CONTAINERS	100

struct vc_test_args {
	char			*fname;
	uuid_t			pool_uuid;
	daos_handle_t		poh;
	int			seq_cnt[VCT_CONTAINERS];
	int			ops_seq[VCT_CONTAINERS][5];
	daos_handle_t		coh[VCT_CONTAINERS];
	struct d_uuid		uuid[VCT_CONTAINERS];
	bool			anchor_flag;
};

static void
co_ops_run(void **state)
{
	int			ret = 0, i, j;
	struct vc_test_args	*arg = *state;
	vos_cont_info_t		cinfo;

	for (i = 0; i < VCT_CONTAINERS; i++) {
		for (j = 0; j < arg->seq_cnt[i]; j++) {
			switch (arg->ops_seq[i][j]) {
			case CREAT:
				uuid_generate(arg->uuid[i].uuid);
				ret = vos_cont_create(arg->poh,
						    arg->uuid[i].uuid);
				break;
			case OPEN:
				ret = vos_cont_open(arg->poh,
						    arg->uuid[i].uuid,
						    &arg->coh[i]);
				break;
			case CLOSE:
				ret = vos_cont_close(arg->coh[i]);
				break;
			case QUERY:
				ret = vos_cont_query(arg->coh[i], &cinfo);
				assert_int_equal(cinfo.ci_nobjs, 0);
				assert_int_equal(cinfo.ci_used, 0);
				break;
			case DESTROY:
				ret = vos_cont_destroy(arg->poh,
						     arg->uuid[i].uuid);
				uuid_clear(arg->uuid[i].uuid);
				if (!uuid_is_null(arg->uuid[i].uuid))
					printf("UUID clear did not work\n");
				break;
			default:
				fail_msg("Unknown Ops!\n");
				break;
			}
			assert_int_equal(ret, 0);
		}
	}
	D_PRINT("Finished all create and discards\n");
}

static int
co_allocate_params(int ops, struct vc_test_args *test_args)
{
	int			i;

	for (i = 0; i < VCT_CONTAINERS; i++) {
		test_args->seq_cnt[i] = ops;
		test_args->coh[i] = DAOS_HDL_INVAL;
		uuid_clear(test_args->uuid[i].uuid);
	}

	return 0;
}


static int
co_unit_teardown(void **state)
{
	struct vc_test_args	*arg = *state;
	int			i, ret = 0;

	for (i = 0; i < VCT_CONTAINERS; i++) {
		if (!uuid_is_null(arg->uuid[i].uuid)) {
			ret = vos_cont_destroy(arg->poh, arg->uuid[i].uuid);
			assert_int_equal(ret, 0);
			uuid_clear(arg->uuid[i].uuid);
		}
	}

	return ret;
}

static int
co_ref_count_setup(void **state)
{
	struct vc_test_args	*arg = *state;
	int			i, ret = 0;

	ret = vos_cont_create(arg->poh, arg->uuid[0].uuid);
	assert_int_equal(ret, 0);

	for (i = 0; i < VCT_CONTAINERS; i++) {
		ret = vos_cont_open(arg->poh, arg->uuid[0].uuid,
				    &arg->coh[i]);
		assert_int_equal(ret, 0);
	}

	return 0;
}


static void
co_ref_count_test(void **state)
{
	struct vc_test_args	*arg = *state;
	int			i, ret;

	ret = vos_cont_destroy(arg->poh, arg->uuid[0].uuid);
	assert_int_equal(ret, -DER_BUSY);

	for (i = 0; i < VCT_CONTAINERS; i++) {
		ret = vos_cont_close(arg->coh[i]);
		assert_int_equal(ret, 0);
	}

	ret = vos_cont_destroy(arg->poh, arg->uuid[0].uuid);
	assert_int_equal(ret, 0);
}

static int
setup(void **state)
{
	struct vc_test_args	*test_arg = NULL;
	int			ret = 0;

	D_ALLOC(test_arg, sizeof(struct vc_test_args));
	assert_ptr_not_equal(test_arg, NULL);

	uuid_generate_time_safe(test_arg->pool_uuid);
	vts_pool_fallocate(&test_arg->fname);
	ret = vos_pool_create(test_arg->fname, test_arg->pool_uuid, 0, 0);
	assert_int_equal(ret, 0);
	ret = vos_pool_open(test_arg->fname, test_arg->pool_uuid, false,
			    &test_arg->poh);
	assert_int_equal(ret, 0);
	*state = test_arg;
	return 0;
}

static int
teardown(void **state)
{
	int			ret = 0;
	struct vc_test_args	*test_arg = *state;

	if (test_arg == NULL) {
		print_message("state not set, likely due to group-setup"
			      " issue\n");
		return 0;
	}

	ret = vos_pool_close(test_arg->poh);
	assert_int_equal(ret, 0);

	D_ASSERT(test_arg->fname != NULL);
	ret = vos_pool_destroy(test_arg->fname, test_arg->pool_uuid);
	assert_int_equal(ret, 0);

	if (vts_file_exists(test_arg->fname))
		remove(test_arg->fname);
	D_FREE(test_arg->fname);

	D_FREE(test_arg);
	return 0;
}

static int
co_create_tests(void **state)
{
	struct vc_test_args	*arg = *state;
	int			i;

	co_allocate_params(1, arg);
	for (i = 0; i < VCT_CONTAINERS; i++)
		arg->ops_seq[i][0] = CREAT;

	return 0;
}

static int
co_iter_tests_setup(void **state)
{
	co_create_tests(state);
	co_ops_run(state);
	return 0;
}

static int
co_uuid_iter_test(struct vc_test_args *arg)
{
	vos_iter_param_t	param;
	daos_handle_t		ih;
	int			nr = 0;
	int			rc = 0;

	memset(&param, 0, sizeof(param));
	param.ip_hdl = arg->poh;

	rc = vos_iter_prepare(VOS_ITER_COUUID, &param, &ih, NULL);
	if (rc != 0) {
		print_error("Failed to prepare co iterator\n");
		return rc;
	}

	rc = vos_iter_probe(ih, NULL);
	if (rc != 0) {
		print_error("Failed to set iterator cursor: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

	while (1) {
		vos_iter_entry_t	ent;
		daos_anchor_t		anchor;

		rc = vos_iter_fetch(ih, &ent, NULL);
		if (rc == -DER_NONEXIST) {
			print_message("Finishing obj iteration\n");
			break;
		}

		if (rc != 0) {
			print_error("Failed to fetch co uuid: "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}

		if (!uuid_is_null(ent.ie_couuid)) {
			D_DEBUG(DB_TRACE,
				"COUUID:"DF_UUID"\n", DP_UUID(ent.ie_couuid));
			nr++;
		}


		rc = vos_iter_next(ih);
		if (rc == -DER_NONEXIST)
			break;

		if (rc != 0) {
			print_error("Failed to move cursor: "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}

		if (!arg->anchor_flag)
			continue;

		rc = vos_iter_fetch(ih, &ent, &anchor);
		if (rc != 0) {
			assert_true(rc != -DER_NONEXIST);
			print_error("Failed to fetch anchor: "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}

		rc = vos_iter_probe(ih, &anchor);
		if (rc != 0) {
			assert_true(rc != -DER_NONEXIST);
			print_error("Failed to probe anchor: "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}
	}
out:
	print_message("Enumerated %d, total: %d\n", nr, VCT_CONTAINERS);
	assert_int_equal(nr, VCT_CONTAINERS);
	vos_iter_finish(ih);
	return rc;
}

static void
co_iter_test(void **state)
{
	struct vc_test_args	*arg = *state;
	int			rc = 0;

	arg->anchor_flag = false;

	rc = co_uuid_iter_test(arg);
	assert_true(rc == 0 || rc == -DER_NONEXIST);
}

static void
co_iter_test_with_anchor(void **state)
{
	struct vc_test_args	*arg = *state;
	int			rc = 0;

	arg->anchor_flag = true;

	rc = co_uuid_iter_test(arg);
	assert_true(rc == 0 || rc == -DER_NONEXIST);
}

static int
co_tests(void **state)
{
	struct vc_test_args	*arg = *state;
	int			i;

	co_allocate_params(5, arg);
	for (i = 0; i < VCT_CONTAINERS; i++) {
		arg->ops_seq[i][0] = CREAT;
		arg->ops_seq[i][1] = OPEN;
		arg->ops_seq[i][2] = QUERY;
		arg->ops_seq[i][3] = CLOSE;
		arg->ops_seq[i][4] = DESTROY;
	}
	return 0;
}

static const struct CMUnitTest vos_co_tests[] = {
	{ "VOS100: container create test", co_ops_run, co_create_tests,
		co_unit_teardown},
	{ "VOS101: container all APIs", co_ops_run, co_tests,
		co_unit_teardown},
	{ "VOS102: container uuid iter test", co_iter_test, co_iter_tests_setup,
		co_unit_teardown},
	{ "VOS103: container uuid iter test with anchor",
		co_iter_test_with_anchor, co_iter_tests_setup,
		co_unit_teardown},
	{ "VOS104: container handle ref count tests", co_ref_count_test,
		co_ref_count_setup, NULL},
};

int
run_co_test(const char *cfg)
{
	char	test_name[DTS_CFG_MAX];

	dts_create_config(test_name, "VOS container tests %s", cfg);
	return cmocka_run_group_tests_name(test_name,
					   vos_co_tests,
					   setup, teardown);
}
