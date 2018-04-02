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
#define D_LOGFAC	DD_FAC(tests)

#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <vts_common.h>
#include <vos_internal.h>

#include <daos_srv/vos.h>

#define VCT_CONTAINERS	100
/*
 * Constants for cookie test
 */
#define VCT_COOKIES	100
#define VCT_EPOCHS	200

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
				assert_int_equal(ret, 0);
				assert_int_equal(cinfo.pci_nobjs, 0);
				assert_int_equal(cinfo.pci_used, 0);
				break;
			case DESTROY:
				ret = vos_cont_destroy(arg->poh,
						     arg->uuid[i].uuid);
				assert_int_equal(ret, 0);
				uuid_clear(arg->uuid[i].uuid);
				if (!uuid_is_null(arg->uuid[i].uuid))
					printf("UUID clear did not work\n");
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
	D__PRINT("Finished all create and discards\n");
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

	test_arg = malloc(sizeof(struct vc_test_args));
	assert_ptr_not_equal(test_arg, NULL);

	uuid_generate_time_safe(test_arg->pool_uuid);
	vts_pool_fallocate(&test_arg->fname);
	ret = vos_pool_create(test_arg->fname, test_arg->pool_uuid, 0);
	assert_int_equal(ret, 0);
	ret = vos_pool_open(test_arg->fname, test_arg->pool_uuid,
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

	ret = vos_pool_close(test_arg->poh);
	assert_int_equal(ret, 0);

	ret = vos_pool_destroy(test_arg->fname, test_arg->pool_uuid);
	assert_int_equal(ret, 0);

	if (vts_file_exists(test_arg->fname))
		remove(test_arg->fname);
	if (test_arg->fname)
		free(test_arg->fname);

	free(test_arg);
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
	struct vc_test_args	*arg = *state;
	int			i;

	co_allocate_params(1, arg);
	for (i = 0; i < VCT_CONTAINERS; i++)
		arg->ops_seq[i][0]  = CREAT;

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

	rc = vos_iter_prepare(VOS_ITER_COUUID, &param, &ih);
	if (rc != 0) {
		print_error("Failed to prepare co iterator\n");
		return rc;
	}

	rc = vos_iter_probe(ih, NULL);
	if (rc != 0) {
		print_error("Failed to set iterator cursor: %d\n", rc);
		goto out;
	}

	while (1) {
		vos_iter_entry_t	ent;
		daos_hash_out_t		anchor;

		rc = vos_iter_fetch(ih, &ent, NULL);
		if (rc == -DER_NONEXIST) {
			print_message("Finishing obj iteration\n");
			break;
		}

		if (rc != 0) {
			print_error("Failed to fetch co uuid: %d\n", rc);
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
			print_error("Failed to move cursor: %d\n", rc);
			goto out;
		}

		if (!arg->anchor_flag)
			continue;

		rc = vos_iter_fetch(ih, &ent, &anchor);
		if (rc != 0) {
			assert_true(rc != -DER_NONEXIST);
			print_error("Failed to fetch anchor: %d\n", rc);
			goto out;
		}

		rc = vos_iter_probe(ih, &anchor);
		if (rc != 0) {
			assert_true(rc != -DER_NONEXIST);
			print_error("Failed to probe anchor: %d\n", rc);
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


struct cookie_entry {
	struct d_ulink		ulink;
	uuid_t			cookie;
	daos_epoch_t		max_epoch;
};

void
cookie_uhash_free(struct d_ulink *uhlink)
{
	struct cookie_entry	*entry;

	entry = container_of(uhlink, struct cookie_entry, ulink);
	D__FREE_PTR(entry);
}

struct d_ulink_ops	cookie_uh_ops = {
	.uop_free	= cookie_uhash_free,
};

static void
cookie_table_test(void **state)
{
	int				ret = 0;
	int				i = 0, j = 0, k = 0;
	struct vos_cookie_table		*itab;
	struct d_uuid			*cookie_array;
	daos_epoch_t			*epochs;
	struct umem_attr		uma;
	uint64_t			epoch_ret;
	daos_handle_t			cookie_hdl;
	/* static uuid hash for verification */
	struct cookie_entry		*cookie_entries, *l_entry = NULL;
	struct d_hash_table		*uhtab = NULL;
	struct d_ulink			*l_ulink = NULL;

	D__ALLOC_PTR(itab);
	D__ALLOC(cookie_array, VCT_COOKIES * sizeof(struct d_uuid));
	D__ALLOC(cookie_entries, VCT_COOKIES * sizeof(struct cookie_entry));
	D__ALLOC(epochs, VCT_EPOCHS * sizeof(daos_epoch_t));

	ret = d_uhash_create(0, 8, &uhtab);
	if (ret != 0)
		print_error("Error creating daos Uhash  for verification\n");

	/** Generate cookies for test */
	for (i = 0; i < VCT_COOKIES; i++)
		uuid_generate_random(cookie_array[i].uuid);

	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
	ret = vos_cookie_tab_create(&uma, itab, &cookie_hdl);
	if (ret != 0)
		print_error("Failed to create cookie itab\n");

	for (i = 0; i < VCT_EPOCHS; i++) {

		j = (rand()%VCT_COOKIES) - 1;
		epochs[i] = rand()%100;

		l_ulink = d_uhash_link_lookup(uhtab, &cookie_array[j]);
		if (l_ulink != NULL) {
			l_entry = container_of(l_ulink, struct cookie_entry,
					       ulink);
			if (l_entry->max_epoch < epochs[i])
				l_entry->max_epoch = epochs[i];
		} else {
			/* Create a new cookie entry and add it to uhash */
			uuid_copy(cookie_entries[k].cookie,
				  cookie_array[j].uuid);
			cookie_entries[k].max_epoch = epochs[i];
			d_uhash_ulink_init(&cookie_entries[k].ulink,
					   &cookie_uh_ops);
			ret = d_uhash_link_insert(uhtab, &cookie_array[j],
						  &cookie_entries[k].ulink);
			if (ret != 0)
				D_ERROR("Inserting handle to UUID hash\n");
			l_entry = &cookie_entries[k];
			k++;
		}

		ret = vos_cookie_find_update(cookie_hdl, cookie_array[j].uuid,
					     epochs[i], true, &epoch_ret);
		if (ret != 0)
			print_error("find and update error\n");

		D_DEBUG(DB_TRACE, "Cookie: "DF_UUID" Epoch :%"PRIu64"\t",
			DP_UUID(cookie_array[j].uuid), epochs[i]);
		D_DEBUG(DB_TRACE, "Returned max_epoch: %"PRIu64"\n",
			epoch_ret);
		assert_true(epoch_ret == l_entry->max_epoch);

	}
	/* Cleanup allocations */
	d_uhash_destroy(uhtab);
	ret = vos_cookie_tab_destroy(cookie_hdl);
	if (ret != 0)
		D_ERROR("Cookie itab destroy error\n");
	D__FREE_PTR(itab);
	D__FREE(cookie_array, VCT_COOKIES * sizeof(struct d_uuid));
	D__FREE(cookie_entries, VCT_COOKIES * sizeof(struct cookie_entry));
	D__FREE(epochs, VCT_EPOCHS * sizeof(daos_epoch_t));
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
	{ "VOS105: cookie table tests", cookie_table_test,
		NULL, NULL},
};

int
run_co_test(void)
{
	return cmocka_run_group_tests_name("VOS container tests",
					   vos_co_tests,
					   setup, teardown);
}
