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
 * vos/tests/vts_container.c
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

#define VCT_CONTAINERS 10

struct vc_test_args {
	char			*fname;
	int			*seq_cnt;
	enum vts_ops_type	*ops_seq[VCT_CONTAINERS];
	uuid_t			pool_uuid;
	daos_handle_t		poh;
	daos_handle_t		coh[VCT_CONTAINERS];
	uuid_t			uuid[VCT_CONTAINERS];
};

static void
co_ops_run(void **state)
{
	int			ret = 0, i, j;
	struct vc_test_args	*arg = *state;
	vos_co_info_t		cinfo;

	for (i = 0; i < VCT_CONTAINERS; i++) {
		for (j = 0; j < arg->seq_cnt[i]; j++) {
			switch (arg->ops_seq[i][j]) {
			case CREAT:
				uuid_generate(arg->uuid[i]);
				ret = vos_co_create(arg->poh, arg->uuid[i],
						    NULL);
				break;
			case OPEN:
				ret = vos_co_open(arg->poh, arg->uuid[i],
						  &arg->coh[i], NULL);
				break;
			case CLOSE:
				ret = vos_co_close(arg->coh[i], NULL);
				break;
			case QUERY:
				ret = vos_co_query(arg->coh[i], &cinfo,
						   NULL);
				assert_int_equal(ret, 0);
				assert_int_equal(cinfo.pci_nobjs, 0);
				assert_int_equal(cinfo.pci_used, 0);
				break;
			case DESTROY:
				ret = vos_co_destroy(arg->poh, arg->uuid[i],
						     NULL);
				assert_int_equal(ret, 0);
				uuid_clear(arg->uuid[i]);
				break;
			default:
				fail_msg("Unkown Ops!\n");
				break;
			}
			if (arg->ops_seq[i][j] != QUERY ||
			    arg->ops_seq[i][j] != DESTROY)
				assert_int_equal(ret, 0);
		}
	}
}

static int
co_allocate_params(int ncontainers, int ops,
		   struct vc_test_args *test_args)
{
	int			i;
	enum vts_ops_type	*tmp_seq = NULL;

	test_args->seq_cnt = (int *)malloc(ncontainers * sizeof(int));
	assert_ptr_not_equal(test_args->seq_cnt, NULL);

	for (i = 0; i < ncontainers; i++) {
		tmp_seq = (enum vts_ops_type *)malloc(ops *
						  sizeof(enum vts_ops_type));
		assert_ptr_not_equal(tmp_seq, NULL);
		memset(tmp_seq, 0,
		       sizeof(ops * sizeof(enum vts_ops_type)));
		test_args->ops_seq[i] = tmp_seq;
		tmp_seq = NULL;
	}

	memset(&test_args->coh, 0,
	       sizeof(VCT_CONTAINERS * sizeof(daos_handle_t)));
	memset(&test_args->uuid, 0,
	       sizeof(VCT_CONTAINERS * sizeof(uuid_t)));

	return 0;
}

static int
co_unit_teardown(void **state)
{
	struct vc_test_args	*arg = *state;
	int			i, ret = 0;

	for (i = 0; i < VCT_CONTAINERS; i++) {
		if (arg->ops_seq[i]) {
			free(arg->ops_seq[i]);
			arg->ops_seq[i] = NULL;
		}
		if (!uuid_is_null(arg->uuid[i])) {
			ret = vos_co_destroy(arg->poh,
					     arg->uuid[i], NULL);
			assert_int_equal(ret, 0);
			uuid_clear(arg->uuid[i]);
		}
	}
	if (arg->seq_cnt) {
		free(arg->seq_cnt);
		arg->seq_cnt = NULL;
	}
	return ret;
}

static int
setup(void **state)
{
	struct vc_test_args	*test_arg = NULL;
	int			ret = 0;

	test_arg = malloc(sizeof(struct vc_test_args));
	assert_ptr_not_equal(test_arg, NULL);

	uuid_generate_time_safe(test_arg->pool_uuid);
	vts_pool_fallocate(&test_arg->fname);
	ret = vos_pool_create(test_arg->fname, test_arg->pool_uuid,
			      0, &test_arg->poh, NULL);
	assert_int_equal(ret, 0);
	*state = test_arg;
	return 0;
}

static int
teardown(void **state)
{
	int			ret = 0;
	struct vc_test_args	*test_arg = *state;

	ret = vos_pool_destroy(test_arg->poh, NULL);
	assert_int_equal(ret, 0);

	if (vts_file_exists(test_arg->fname))
		remove(test_arg->fname);
	if (test_arg->fname)
		free(test_arg->fname);

	free(test_arg);
	return 0;
}

static inline void
co_set_param(enum vts_ops_type seq[], int cnt,
	     int *seq_cnt, enum vts_ops_type **ops)
{
	*seq_cnt = cnt;
	memcpy(*ops, seq, cnt * sizeof(enum vts_ops_type));
}

static int
co_create_tests(void **state)
{
	struct vc_test_args	*arg = *state;
	enum vts_ops_type	tmp[] = {CREAT};
	int			i;

	co_allocate_params(VCT_CONTAINERS, 1, arg);
	for (i = 0; i < VCT_CONTAINERS; i++)
		co_set_param(tmp, 1, &arg->seq_cnt[i],
			     &arg->ops_seq[i]);
	return 0;
}

static int
co_open_tests(void **state)
{
	struct vc_test_args	*arg = *state;
	enum vts_ops_type	tmp[] = {OPEN};
	uuid_t			tmp_uuid;
	int			i, ret = 0;

	uuid_generate(tmp_uuid);
	ret = vos_co_create(arg->poh, tmp_uuid,
			    NULL);
	assert_int_equal(ret, 0);

	co_allocate_params(VCT_CONTAINERS, 1, arg);
	for (i = 0; i < VCT_CONTAINERS; i++) {
		uuid_copy(arg->uuid[i], tmp_uuid);
		co_set_param(tmp, 1, &arg->seq_cnt[i],
			     &arg->ops_seq[i]);
	}
	return 0;
}

static int
co_tests(void **state)
{
	struct vc_test_args	*arg = *state;
	enum vts_ops_type	tmp[] = {CREAT, OPEN, QUERY, CLOSE, DESTROY};
	int			i;

	co_allocate_params(VCT_CONTAINERS, 5, arg);
	for (i = 0; i < VCT_CONTAINERS; i++)
		co_set_param(tmp, 5, &arg->seq_cnt[i],
			     &arg->ops_seq[i]);
	return 0;
}


static const struct CMUnitTest vos_co_tests[] = {
	{ "VOS100: container create test", co_ops_run, co_create_tests,
		co_unit_teardown},
	{ "VOS101: multiple container open handles for same container",
		co_ops_run, co_tests, co_unit_teardown},
	{ "VOS102: container all APIs", co_ops_run, co_tests,
		co_unit_teardown},
};

int
run_co_test(void)
{
	return cmocka_run_group_tests_name("VOS container tests",
					   vos_co_tests,
					   setup, teardown);
}
