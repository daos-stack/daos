/**
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(tests)

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos/common.h>
#include <daos_srv/smd.h>
#include "../smd_internal.h"

#define SMD_STORAGE_PATH	"/mnt/daos"

uuid_t	dev_id1;
uuid_t	dev_id2;

static int
smd_ut_setup(void **state)
{
	int	rc;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc) {
		print_error("Error initializing the debug instance\n");
		return rc;
	}

	rc = smd_init(SMD_STORAGE_PATH);
	if (rc) {
		print_error("Error initializing SMD store: %d\n", rc);
		daos_debug_fini();
		return rc;
	}

	return 0;
}

static int
smd_ut_teardown(void **state)
{
	smd_fini();
	smd_store_destroy(SMD_STORAGE_PATH);
	daos_debug_fini();
	return 0;
}

static void
verify_dev(struct smd_dev_info *dev_info, uuid_t id, int dev_idx)
{
	int	i;

	assert_int_equal(uuid_compare(dev_info->sdi_id, id), 0);
	if (dev_idx == 1) {
		assert_int_equal(dev_info->sdi_state, SMD_DEV_NORMAL);
		assert_int_equal(dev_info->sdi_tgt_cnt, 3);
		for (i = 0; i < 3; i++)
			assert_int_equal(dev_info->sdi_tgts[i], i);
	} else {
		assert_int_equal(dev_info->sdi_state, SMD_DEV_FAULTY);
		assert_int_equal(dev_info->sdi_tgt_cnt, 1);
		assert_int_equal(dev_info->sdi_tgts[0], 3);
	}
}

static void
ut_device(void **state)
{
	struct smd_dev_info	*dev_info, *tmp;
	d_list_t		 dev_list;
	uuid_t			 id3;
	int			 i, dev_cnt = 0, rc;

	uuid_generate(dev_id1);
	uuid_generate(dev_id2);
	uuid_generate(id3);

	/* Assigned dev1 to target 0, 1, 2, dev2 to target 3 */
	rc = smd_dev_assign(dev_id1, 0);
	assert_int_equal(rc, 0);

	rc = smd_dev_assign(dev_id1, 0);
	assert_int_equal(rc, -DER_EXIST);

	for (i = 1; i < 3; i++) {
		rc = smd_dev_assign(dev_id1, i);
		assert_int_equal(rc, 0);
	}

	rc = smd_dev_assign(dev_id2, 1);
	assert_int_equal(rc, -DER_EXIST);

	rc = smd_dev_assign(dev_id2, 3);
	assert_int_equal(rc, 0);

	rc = smd_dev_set_state(dev_id2, SMD_DEV_FAULTY);
	assert_int_equal(rc, 0);

	rc = smd_dev_get_by_id(id3, &dev_info);
	assert_int_equal(rc, -DER_NONEXIST);

	rc = smd_dev_get_by_id(dev_id1, &dev_info);
	assert_int_equal(rc, 0);
	verify_dev(dev_info, dev_id1, 1);

	smd_free_dev_info(dev_info);

	rc = smd_dev_get_by_tgt(4, &dev_info);
	assert_int_equal(rc, -DER_NONEXIST);

	rc = smd_dev_get_by_tgt(3, &dev_info);
	assert_int_equal(rc, 0);
	verify_dev(dev_info, dev_id2, 2);

	smd_free_dev_info(dev_info);

	D_INIT_LIST_HEAD(&dev_list);
	rc = smd_dev_list(&dev_list, &dev_cnt);
	assert_int_equal(rc, 0);
	assert_int_equal(dev_cnt, 2);

	d_list_for_each_entry_safe(dev_info, tmp, &dev_list, sdi_link) {
		if (uuid_compare(dev_info->sdi_id, dev_id1) == 0)
			verify_dev(dev_info, dev_id1, 1);
		else if (uuid_compare(dev_info->sdi_id, dev_id2) == 0)
			verify_dev(dev_info, dev_id2, 2);
		else
			assert_true(false);

		d_list_del(&dev_info->sdi_link);
		smd_free_dev_info(dev_info);
	}
}

static void
verify_pool(struct smd_pool_info *pool_info, uuid_t id, int shift)
{
	int	i;

	assert_int_equal(uuid_compare(pool_info->spi_id, id), 0);
	assert_int_equal(pool_info->spi_tgt_cnt, 4);

	for (i = 0; i < 4; i++) {
		assert_int_equal(pool_info->spi_tgts[i], i);
		assert_int_equal(pool_info->spi_blobs[i], i << shift);
	}
}

static void
ut_pool(void **state)
{
	struct smd_pool_info	*pool_info, *tmp;
	uuid_t			 id1, id2, id3;
	uint64_t		 blob_id;
	d_list_t		 pool_list;
	int			 i, pool_cnt = 0, rc;

	uuid_generate(id1);
	uuid_generate(id2);
	uuid_generate(id3);

	for (i = 0; i < 4; i++) {
		rc = smd_pool_assign(id1, i, i << 10, 100);
		assert_int_equal(rc, 0);

		rc = smd_pool_assign(id2, i, i << 20, 200);
		assert_int_equal(rc, 0);
	}

	rc = smd_pool_assign(id1, 0, 5000, 100);
	assert_int_equal(rc, -DER_EXIST);

	rc = smd_pool_assign(id1, 4, 4 << 10, 200);
	assert_int_equal(rc, -DER_INVAL);

	rc = smd_pool_get(id1, &pool_info);
	assert_int_equal(rc, 0);
	verify_pool(pool_info, id1, 10);

	smd_free_pool_info(pool_info);

	rc = smd_pool_get(id3, &pool_info);
	assert_int_equal(rc, -DER_NONEXIST);

	for (i = 0; i < 4; i++) {
		rc = smd_pool_get_blob(id1, i, &blob_id);
		assert_int_equal(rc, 0);
		assert_int_equal(blob_id, i << 10);

		rc = smd_pool_get_blob(id2, i, &blob_id);
		assert_int_equal(rc, 0);
		assert_int_equal(blob_id, i << 20);
	}

	rc = smd_pool_get_blob(id1, 5, &blob_id);
	assert_int_equal(rc, -DER_NONEXIST);

	D_INIT_LIST_HEAD(&pool_list);
	rc = smd_pool_list(&pool_list, &pool_cnt);
	assert_int_equal(rc, 0);
	assert_int_equal(pool_cnt, 2);

	d_list_for_each_entry_safe(pool_info, tmp, &pool_list, spi_link) {
		if (uuid_compare(pool_info->spi_id, id1) == 0)
			verify_pool(pool_info, id1, 10);
		else if (uuid_compare(pool_info->spi_id, id2) == 0)
			verify_pool(pool_info, id2, 20);
		else
			assert_true(false);

		d_list_del(&pool_info->spi_link);
		smd_free_pool_info(pool_info);
	}

	rc = smd_pool_unassign(id1, 5);
	assert_int_equal(rc, -DER_NONEXIST);

	for (i = 0; i < 4; i++) {
		rc = smd_pool_unassign(id1, i);
		assert_int_equal(rc, 0);

		rc = smd_pool_unassign(id2, i);
		assert_int_equal(rc, 0);
	}

	rc = smd_pool_get(id1, &pool_info);
	assert_int_equal(rc, -DER_NONEXIST);
}

static void
ut_dev_replace(void **state)
{
	struct smd_dev_info	*dev_info, *tmp_dev;
	struct smd_pool_info	*pool_info, *tmp_pool;
	uuid_t			 dev_id3, pool_id1, pool_id2;
	d_list_t		 pool_list, dev_list;
	uint64_t		 blob_id;
	int			 i, rc, pool_cnt = 0, dev_cnt = 0;

	uuid_generate(dev_id3);
	uuid_generate(pool_id1);
	uuid_generate(pool_id2);

	/* Assign pools, they were unassigned in prior pool test */
	for (i = 0; i < 4; i++) {
		rc = smd_pool_assign(pool_id1, i, i << 10, 100);
		assert_int_equal(rc, 0);

		rc = smd_pool_assign(pool_id2, i, i << 20, 200);
		assert_int_equal(rc, 0);
	}

	D_INIT_LIST_HEAD(&pool_list);
	rc = smd_pool_list(&pool_list, &pool_cnt);
	assert_int_equal(rc, 0);
	assert_int_equal(pool_cnt, 2);

	/* Assign different blobs to dev1's targets 0, 1, 2 */
	d_list_for_each_entry(pool_info, &pool_list, spi_link) {
		pool_info->spi_blobs[0] = 555;
		pool_info->spi_blobs[1] = 666;
		pool_info->spi_blobs[2] = 777;
	}

	/* Replace dev1 with dev3 without marking dev1 as faulty */
	rc = smd_dev_replace(dev_id1, dev_id3, &pool_list);
	assert_int_equal(rc, -DER_INVAL);

	rc = smd_dev_set_state(dev_id1, SMD_DEV_FAULTY);
	assert_int_equal(rc, 0);

	/* Replace dev1 with dev2 */
	rc = smd_dev_replace(dev_id1, dev_id2, &pool_list);
	assert_int_equal(rc, -DER_INVAL);

	/* Replace dev1 with dev3 */
	rc = smd_dev_replace(dev_id1, dev_id3, &pool_list);
	assert_int_equal(rc, 0);

	/* Verify device after replace */
	D_INIT_LIST_HEAD(&dev_list);
	rc = smd_dev_list(&dev_list, &dev_cnt);
	assert_int_equal(rc, 0);
	assert_int_equal(dev_cnt, 2);

	d_list_for_each_entry_safe(dev_info, tmp_dev, &dev_list, sdi_link) {
		if (uuid_compare(dev_info->sdi_id, dev_id3) == 0)
			verify_dev(dev_info, dev_id3, 1);
		else if (uuid_compare(dev_info->sdi_id, dev_id2) == 0)
			verify_dev(dev_info, dev_id2, 2);
		else
			assert_true(false);

		d_list_del(&dev_info->sdi_link);
		smd_free_dev_info(dev_info);
	}

	/* Verify blob IDs after device replace */
	d_list_for_each_entry_safe(pool_info, tmp_pool, &pool_list, spi_link) {
		for (i = 0; i < 3; i++) {
			rc = smd_pool_get_blob(pool_info->spi_id, i, &blob_id);
			assert_int_equal(rc, 0);
			if (i == 0)
				assert_int_equal(blob_id, 555);
			else if (i == 1)
				assert_int_equal(blob_id, 666);
			else
				assert_int_equal(blob_id, 777);
		}
		d_list_del(&pool_info->spi_link);
		smd_free_pool_info(pool_info);
	}
}

static const struct CMUnitTest smd_uts[] = {
	{ "smd_ut_device", ut_device, NULL, NULL},
	{ "smd_ut_pool", ut_pool, NULL, NULL},
	{ "smd_ut_dev_replace", ut_dev_replace, NULL, NULL},
};

int main(int argc, char **argv)
{
	int	rc;

	rc = ABT_init(0, NULL);
	if (rc != 0) {
		D_PRINT("Error initializing ABT\n");
		return rc;
	}


	rc = cmocka_run_group_tests_name("SMD unit tests", smd_uts,
					 smd_ut_setup, smd_ut_teardown);

	ABT_finalize();
	return rc;
}
