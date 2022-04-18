/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <gurt/debug.h>
#include <daos/tests_lib.h>
#include <ddb_vos.h>
#include <ddb_common.h>
#include <daos_srv/vos.h>
#include "ddb_cmocka.h"
#include "ddb_test_driver.h"

/*
 * The tests in this file depend on a VOS instance with a bunch of data written. The tests will
 * verify that different parts of the VOS tree can be navigated/iterated. The way the
 */

static int fake_cont_handler_call_count;
static struct ddb_cont fake_cont_handler_conts[64];
int fake_cont_handler(struct ddb_cont *cont, void *args)
{
	assert_true(fake_cont_handler_call_count < ARRAY_SIZE(fake_cont_handler_conts));
	fake_cont_handler_conts[fake_cont_handler_call_count] = *cont;
	fake_cont_handler_call_count++;

	return 0;
}

static int fake_obj_handler_call_count;
static struct ddb_obj fake_obj_handler_objs[128];
int fake_obj_handler(struct ddb_obj *obj, void *args)
{
	assert_true(fake_obj_handler_call_count < ARRAY_SIZE(fake_obj_handler_objs));
	fake_obj_handler_objs[fake_obj_handler_call_count] = *obj;
	fake_obj_handler_call_count++;

	return 0;
}

static int fake_dkey_handler_call_count;
static struct ddb_key fake_dkey_handler_dkeys[1024];
int fake_dkey_handler(struct ddb_key *key, void *args)
{
	assert_true(fake_dkey_handler_call_count < ARRAY_SIZE(fake_dkey_handler_dkeys));
	fake_dkey_handler_dkeys[fake_dkey_handler_call_count] = *key;
	fake_dkey_handler_call_count++;

	return 0;
}

static int fake_akey_handler_call_count;
static struct ddb_key fake_akey_handler_akeys[2048 * 10];
int fake_akey_handler(struct ddb_key *key, void *args)
{
	assert_true(fake_akey_handler_call_count < ARRAY_SIZE(fake_akey_handler_akeys));
	fake_akey_handler_akeys[fake_akey_handler_call_count] = *key;
	fake_akey_handler_call_count++;

	return 0;
}

static int fake_sv_handler_call_count;
static struct ddb_sv fake_sv_handler_svs[2048 * 10];
int fake_sv_handler(struct ddb_sv *sv, void *args)
{
	assert_true((uint32_t)fake_sv_handler_call_count < ARRAY_SIZE(fake_sv_handler_svs));
	fake_sv_handler_svs[fake_sv_handler_call_count] = *sv;
	fake_sv_handler_call_count++;

	return 0;
}

static int fake_array_handler_call_count;
static struct ddb_array fake_array_handler_arrays[2048 * 10];
int fake_array_handler(struct ddb_array *array, void *args)
{
	assert_true(fake_array_handler_call_count < ARRAY_SIZE(fake_array_handler_arrays));
	fake_array_handler_arrays[fake_array_handler_call_count] = *array;
	fake_array_handler_call_count++;

	return 0;
}

static void
fake_call_counts_reset()
{
	fake_cont_handler_call_count = 0;
	fake_obj_handler_call_count = 0;
	fake_dkey_handler_call_count = 0;
	fake_akey_handler_call_count = 0;
	fake_sv_handler_call_count = 0;
	fake_array_handler_call_count = 0;
}

static struct vos_tree_handlers fake_handlers = {
	.ddb_cont_handler = fake_cont_handler,
	.ddb_obj_handler = fake_obj_handler,
	.ddb_dkey_handler = fake_dkey_handler,
	.ddb_akey_handler = fake_akey_handler,
	.ddb_sv_handler = fake_sv_handler,
	.ddb_array_handler = fake_array_handler,
};

#define expect_int_equal(a, b, rc) \
	do { \
		if ((a) != (b)) { \
			rc++; \
			print_error("%s:%d - %lu != %lu\n", __FILE__, __LINE__, \
					(uint64_t)(a), (uint64_t)(b)); \
		} \
	} while (0)

#define assert_ddb_iterate(poh, cont_uuid, oid, dkey, akey, recursive, expected_cont, \
			   expected_obj, expected_dkey, expected_akey,                   \
			   expected_sv, expected_array)                                    \
	assert_success(__assert_ddb_iterate(poh, cont_uuid, oid, dkey,    \
	akey, recursive, expected_cont, expected_obj,                     \
	expected_dkey, expected_akey, expected_sv, expected_array))
static int
__assert_ddb_iterate(daos_handle_t poh, uuid_t *cont_uuid, daos_unit_oid_t *oid, daos_key_t *dkey,
		     daos_key_t *akey, _Bool recursive, uint32_t expected_cont,
		     uint32_t expected_obj, uint32_t expected_dkey, uint32_t expected_akey,
		     uint32_t expected_sv, uint32_t expected_array)
{
	int			i;
	int			rc = 0;
	struct dv_tree_path	path = {0};


	if (cont_uuid)
		uuid_copy(path.vtp_cont, *cont_uuid);
	if (oid)
		path.vtp_oid = *oid;
	if (dkey)
		path.vtp_dkey = *dkey;
	if (akey)
		path.vtp_akey = *akey;


	assert_success(dv_iterate(poh, &path, recursive, &fake_handlers,
				  NULL));

	expect_int_equal(expected_cont, fake_cont_handler_call_count, rc);
	expect_int_equal(expected_obj, fake_obj_handler_call_count, rc);
	expect_int_equal(expected_dkey, fake_dkey_handler_call_count, rc);
	expect_int_equal(expected_akey, fake_akey_handler_call_count, rc);
	expect_int_equal(expected_sv, fake_sv_handler_call_count, rc);
	expect_int_equal(expected_array, fake_array_handler_call_count, rc);

	for (i = 0; i < expected_cont; i++)
		expect_int_equal(i, fake_cont_handler_conts[i].ddbc_idx, rc);

	/* Even if a parent handler isn't seen it's because only children of the parent
	 * are listed. Always assume 1 parent.
	 */

	/* In these tests the objs will always be evenly distributed in the conts */
	expected_cont = expected_cont == 0 ? 1 : expected_cont;
	for (i = 0; i < expected_obj; i++)
		expect_int_equal(i % (expected_obj / expected_cont),
				 fake_obj_handler_objs[i].ddbo_idx, rc);

	expected_obj = expected_obj == 0 ? 1 : expected_obj;
	for (i = 0; i < expected_dkey; i++)
		expect_int_equal(i % (expected_dkey / expected_obj),
				 fake_dkey_handler_dkeys[i].ddbk_idx, rc);

	expected_dkey = expected_dkey == 0 ? 1 : expected_dkey;
	for (i = 0; i < expected_akey; i++)
		expect_int_equal(i % (expected_akey / expected_dkey),
				 fake_akey_handler_akeys[i].ddbk_idx, rc);

	fake_call_counts_reset();

	return rc;
}

static void
open_pool_test(void **state)
{
	daos_handle_t		 poh;
	struct dt_vos_pool_ctx	*tctx = *state;

	assert_rc_equal(-DER_INVAL, ddb_vos_pool_open("/bad/path", &poh));

	assert_success(ddb_vos_pool_open(tctx->dvt_pmem_file, &poh));

	assert_success(ddb_vos_pool_close(poh));
}

static void
list_items_test(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	daos_handle_t		 poh = tctx->dvt_poh;

	/* list containers */
	assert_ddb_iterate(poh, NULL, NULL, NULL, NULL, false, 10, 0, 0, 0, 0, 0);
	assert_ddb_iterate(poh, NULL, NULL, NULL, NULL, true,
			   10, 100, 1000, 10000, 5000, 5000);

	/* list objects of a container */
	assert_ddb_iterate(poh, &g_uuids[0], NULL, NULL, NULL, false, 0, 10, 0, 0, 0, 0);
	assert_ddb_iterate(poh, &g_uuids[0], NULL, NULL, NULL, true,
			   0, 10, 100, 1000, 500, 500);

	/* list dkeys of an object */
	assert_ddb_iterate(poh, &g_uuids[0], &g_oids[0], NULL, NULL, false, 0, 0, 10, 0, 0, 0);
	assert_ddb_iterate(poh, &g_uuids[0], &g_oids[0], NULL, NULL, true, 0, 0, 10, 100, 50, 50);

	/* list akeys of a dkey */
	assert_ddb_iterate(poh, &g_uuids[0], &g_oids[0], &g_dkeys[0], NULL, false,
			   0, 0, 0, 10, 0, 0);
	assert_ddb_iterate(poh, &g_uuids[0], &g_oids[0], &g_dkeys[0], NULL, true,
			   0, 0, 0, 10, 5, 5);

	/* list values in akeys */
	assert_ddb_iterate(poh, &g_uuids[0], &g_oids[0], &g_dkeys[0], &g_akeys[0], false,
			   0, 0, 0, 0, 0, 1);
	assert_ddb_iterate(poh, &g_uuids[0], &g_oids[0], &g_dkeys[0], &g_akeys[1], true,
			   0, 0, 0, 0, 1, 0);
}

static void
get_cont_uuid_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	uuid_t uuid;
	uuid_t uuid_2;
	int i;

	assert_rc_equal(-DER_INVAL, dv_get_cont_uuid(tctx->dvt_poh, 10000000, uuid));
	assert_success(dv_get_cont_uuid(tctx->dvt_poh, 0, uuid));
	for (i = 1; i < 5; i++) {
		assert_success(dv_get_cont_uuid(tctx->dvt_poh, i, uuid_2));
		assert_uuid_not_equal(uuid, uuid_2);
	}

	/* while containers aren't in the same order they were inserted (and the order can't
	 * be guaranteed), it should be the same order each time assuming no data is
	 * inserted/deleted.
	 */
	for (i = 0; i < 100; i++) {
		assert_success(dv_get_cont_uuid(tctx->dvt_poh, 0, uuid_2));
		assert_uuid_equal(uuid, uuid_2);
	}
}

static void
get_oid_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	daos_unit_oid_t		 uoid;
	daos_unit_oid_t		 uoid_2;
	int			 i;
	daos_handle_t		 coh = DAOS_HDL_INVAL;

	assert_rc_equal(-DER_INVAL, dv_get_object_oid(coh, 0, &uoid));
	vos_cont_open(tctx->dvt_poh, g_uuids[0], &coh);

	assert_rc_equal(-DER_INVAL, dv_get_object_oid(coh, 10000000, &uoid));
	assert_success(dv_get_object_oid(coh, 0, &uoid));
	for (i = 1; i < 5; i++) {
		assert_success(dv_get_object_oid(coh, i, &uoid_2));
		assert_oid_not_equal(uoid.id_pub, uoid_2.id_pub);
	}

	/* while objects aren't in the same order they were inserted (and the order can't
	 * be guaranteed), it should be the same order each time assuming no data is
	 * inserted/deleted.
	 */
	for (i = 0; i < 100; i++) {
		assert_success(dv_get_object_oid(coh, 0, &uoid_2));
		assert_oid_equal(uoid.id_pub, uoid_2.id_pub);
	}

	vos_cont_close(coh);
}

static void
get_dkey_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	daos_unit_oid_t uoid = {0};
	int i;

	daos_handle_t coh = DAOS_HDL_INVAL;
	daos_key_t dkey;
	daos_key_t dkey2;

	assert_rc_equal(-DER_INVAL, dv_get_dkey(coh, uoid, 0, &dkey));
	vos_cont_open(tctx->dvt_poh, g_uuids[0], &coh);
	assert_rc_equal(-DER_INVAL, dv_get_dkey(coh, uoid, 0, &dkey));
	uoid = g_oids[0];

	assert_success(dv_get_dkey(coh, uoid, 0, &dkey));
	for (i = 1; i < 5; i++) {
		assert_success(dv_get_dkey(coh, uoid, i, &dkey2));
		assert_string_not_equal(dkey.iov_buf, dkey2.iov_buf);
	}

	for (i = 0; i < 100; i++) {
		assert_success(dv_get_dkey(coh, uoid, 0, &dkey2));
		assert_string_equal(dkey.iov_buf, dkey2.iov_buf);
	}

	vos_cont_close(coh);
}

static void
get_akey_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	daos_unit_oid_t uoid = {0};
	int i;

	daos_handle_t coh = DAOS_HDL_INVAL;
	daos_key_t dkey = {0};

	daos_key_t akey;
	daos_key_t akey2;

	assert_rc_equal(-DER_INVAL, dv_get_akey(coh, uoid, &dkey, 0, &akey));
	assert_success(vos_cont_open(tctx->dvt_poh, g_uuids[0], &coh));
	assert_rc_equal(-DER_INVAL, dv_get_akey(coh, uoid, &dkey, 0, &akey));
	uoid = g_oids[0];
	assert_rc_equal(-DER_INVAL, dv_get_akey(coh, uoid, &dkey, 0, &akey));
	dv_get_dkey(coh, uoid, 0, &dkey);

	assert_success(dv_get_akey(coh, uoid, &dkey, 0, &akey));
	for (i = 1; i < 5; i++) {
		assert_success(dv_get_akey(coh, uoid, &dkey, i, &akey2));
		assert_string_not_equal(akey.iov_buf, akey2.iov_buf);
	}

	for (i = 0; i < 100; i++) {
		assert_success(dv_get_akey(coh, uoid, &dkey, 0, &akey2));
		assert_string_equal(akey.iov_buf, akey2.iov_buf);
	}

	vos_cont_close(coh);
}

static void
get_recx_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	daos_unit_oid_t		 uoid = {0};
	daos_handle_t		 coh = DAOS_HDL_INVAL;
	daos_key_t		 dkey = {0};
	daos_key_t		 akey = {0};
	daos_recx_t		 recx = {0};

	assert_rc_equal(-DER_INVAL, dv_get_recx(coh, uoid, &dkey, &akey, 0, &recx));

	assert_success(vos_cont_open(tctx->dvt_poh, g_uuids[0], &coh));
	assert_rc_equal(-DER_INVAL, dv_get_recx(coh, uoid, &dkey, &akey, 0, &recx));
	dv_get_object_oid(coh, 0, &uoid);
	assert_rc_equal(-DER_INVAL, dv_get_recx(coh, uoid, &dkey, &akey, 0, &recx));
	dv_get_dkey(coh, uoid, 0, &dkey);
	assert_rc_equal(-DER_INVAL, dv_get_recx(coh, uoid, &dkey, &akey, 0, &recx));
	dv_get_akey(coh, uoid, &dkey, 0, &akey);
	assert_success(dv_get_recx(coh, uoid, &dkey, &akey, 0, &recx));

	vos_cont_close(coh);
}

static void
test_update_path_with_values_from_index(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;

	struct dv_tree_path_builder vt_path = {0};

	vt_path.vtp_poh = tctx->dvt_poh;
	/* Because all path part indexes are 0, should update the path with the first */
	dv_path_update_from_indexes(&vt_path);

	assert_uuid_equal(g_uuids[0], vt_path.vtp_path.vtp_cont);
	assert_oid_equal(g_oids[0].id_pub, vt_path.vtp_path.vtp_oid.id_pub);
	assert_key_equal(g_dkeys[0], vt_path.vtp_path.vtp_dkey);
	assert_key_equal(g_akeys[0], vt_path.vtp_path.vtp_akey);
	assert_int_equal(1, vt_path.vtp_path.vtp_recx.rx_idx);
	assert_int_equal(0x16, vt_path.vtp_path.vtp_recx.rx_nr);
}

static int
dv_suit_setup(void **state)
{
	return ddb_test_setup_vos(state);
}

static int
dv_suit_teardown(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;

	if (tctx == NULL)
		fail_msg("Test context wasn't setup. Possible issue in test setup\n");
	ddb_teardown_vos(state);

	return 0;
}

static int
dv_test_setup(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;

	assert_success(ddb_vos_pool_open(tctx->dvt_pmem_file, &tctx->dvt_poh));
	return 0;
}

static int
dv_test_teardown(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;

	assert_success(ddb_vos_pool_close(tctx->dvt_poh));
	return 0;
}


#define TEST(dsc, test) { dsc, test, dv_test_setup, dv_test_teardown }
const struct CMUnitTest dv_test_cases[] = {
	{ "open_pool", open_pool_test, NULL, NULL }, /* don't want this test to run with setup */
	TEST("list items", list_items_test),
	TEST("get container uuid from idx", get_cont_uuid_tests),
	TEST("get object oid from idx", get_oid_tests),
	TEST("get dkey from idx", get_dkey_tests),
	TEST("get akey from idx", get_akey_tests),
	TEST("get recx from idx", get_recx_tests),
	TEST("get data value", test_update_path_with_values_from_index),
};

int
dv_tests_run()
{
	return cmocka_run_group_tests_name("DDB VOS Interface Tests", dv_test_cases,
					   dv_suit_setup, dv_suit_teardown);
}
