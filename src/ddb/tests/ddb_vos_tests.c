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
			   expected_obj, expected_dkey, expected_akey, \
			   expected_sv, expected_array) \
	assert_success(__assert_ddb_iterate(poh, cont_uuid, oid, dkey, \
	akey, recursive, expected_cont, expected_obj, \
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
get_cont_uuid_from_idx_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	uuid_t uuid;
	uuid_t uuid_2;
	int i;

	assert_rc_equal(-DER_NONEXIST, dv_get_cont_uuid(tctx->dvt_poh, 10000000, uuid));
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
get_oid_from_idx_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	daos_unit_oid_t		 uoid;
	daos_unit_oid_t		 uoid_2;
	int			 i;
	daos_handle_t		 coh = DAOS_HDL_INVAL;

	assert_rc_equal(-DER_INVAL, dv_get_object_oid(coh, 0, &uoid));
	vos_cont_open(tctx->dvt_poh, g_uuids[0], &coh);

	assert_rc_equal(-DER_NONEXIST, dv_get_object_oid(coh, 10000000, &uoid));
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
get_dkey_from_idx_tests(void **state)
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
get_akey_from_idx_tests(void **state)
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
	assert_rc_equal(-DER_NONEXIST, dv_get_akey(coh, uoid, &dkey, 0, &akey));
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
get_recx_from_idx_tests(void **state)
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
	assert_rc_equal(-DER_NONEXIST, dv_get_recx(coh, uoid, &dkey, &akey, 0, &recx));
	dv_get_dkey(coh, uoid, 0, &dkey);
	assert_rc_equal(-DER_NONEXIST, dv_get_recx(coh, uoid, &dkey, &akey, 0, &recx));
	dv_get_akey(coh, uoid, &dkey, 0, &akey);
	assert_success(dv_get_recx(coh, uoid, &dkey, &akey, 0, &recx));

	vos_cont_close(coh);
}

static void
update_path_with_values_from_index_tests(void **state)
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
	assert_int_equal(9, vt_path.vtp_path.vtp_recx.rx_idx);
	assert_int_equal(10, vt_path.vtp_path.vtp_recx.rx_nr);
}

static void
idx_in_path_must_be_valid_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;

	struct dv_tree_path_builder vt_path = {0};

	vt_path.vtp_poh = tctx->dvt_poh;
	vt_path.vtp_cont_idx = 999;
	assert_rc_equal(-DER_NONEXIST, dv_path_update_from_indexes(&vt_path));
	vt_path.vtp_cont_idx = 0;

	vt_path.vtp_oid_idx = 999;
	assert_rc_equal(-DER_NONEXIST, dv_path_update_from_indexes(&vt_path));
	vt_path.vtp_oid_idx = 0;

	vt_path.vtp_dkey_idx = 999;
	assert_rc_equal(-DER_NONEXIST, dv_path_update_from_indexes(&vt_path));
	vt_path.vtp_dkey_idx = 0;

	vt_path.vtp_akey_idx = 999;
	assert_rc_equal(-DER_NONEXIST, dv_path_update_from_indexes(&vt_path));
	vt_path.vtp_akey_idx = 0;
}

#define assert_path(path, expected) \
do { \
	struct dv_tree_path_builder __vt = {0}; \
	daos_handle_t poh = {0}; \
	assert_success(ddb_vtp_init(poh, path, &__vt)); \
	assert_vtp_eq(expected, __vt); \
	ddb_vtp_fini(&__vt); \
} while (0)

static void
path_must_be_valid_tests(void **state)
{
	struct dt_vos_pool_ctx		*tctx = *state;
	daos_handle_t			 poh = tctx->dvt_poh;
	struct dv_tree_path		 vt_path = {0};

	/* Nothing set should be a valid path */
	assert_success(ddb_vtp_verify(poh, &vt_path));

	uuid_parse(g_invalid_uuid_str, vt_path.vtp_cont);
	assert_rc_equal(-DER_NONEXIST, ddb_vtp_verify(poh, &vt_path));

	uuid_copy(vt_path.vtp_cont, g_uuids[0]);
	assert_success(ddb_vtp_verify(poh, &vt_path));

	vt_path.vtp_oid = g_invalid_oid;
	assert_rc_equal(-DER_NONEXIST, ddb_vtp_verify(poh, &vt_path));
	vt_path.vtp_oid = g_oids[0];
	assert_success(ddb_vtp_verify(poh, &vt_path));

	vt_path.vtp_dkey = g_invalid_key;
	assert_rc_equal(-DER_NONEXIST, ddb_vtp_verify(poh, &vt_path));
	vt_path.vtp_dkey = g_dkeys[0];
	assert_success(ddb_vtp_verify(poh, &vt_path));

	vt_path.vtp_akey = g_invalid_key;
	assert_rc_equal(-DER_NONEXIST, ddb_vtp_verify(poh, &vt_path));
	vt_path.vtp_akey = g_akeys[0];
	assert_success(ddb_vtp_verify(poh, &vt_path));

	vt_path.vtp_recx = g_invalid_recx;
	assert_rc_equal(-DER_NONEXIST, ddb_vtp_verify(poh, &vt_path));
	vt_path.vtp_recx = g_recxs[0];
	assert_success(ddb_vtp_verify(poh, &vt_path));
}

static int fake_dump_superblock_cb_called;
static struct ddb_superblock fake_dump_superblock_cb_sb;
static int
fake_dump_superblock_cb(void *cb_arg, struct ddb_superblock *sb)
{
	fake_dump_superblock_cb_called++;
	fake_dump_superblock_cb_sb = *sb;

	return 0;
}

static void
get_superblock_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;

	assert_rc_equal(-DER_INVAL, dv_superblock(DAOS_HDL_INVAL,
						  fake_dump_superblock_cb, NULL));

	assert_success(dv_superblock(tctx->dvt_poh, fake_dump_superblock_cb, NULL));
	assert_int_equal(1, fake_dump_superblock_cb_called);

	/* just do some basics to verify got a valid pool df */
	assert_true(fake_dump_superblock_cb_sb.dsb_durable_format_version);
}

static void
obj_id_2_ddb_test(void **state)
{
	struct ddb_obj	obj = {0};
	daos_obj_id_t	oid = {0};

	daos_obj_set_oid(&oid, DAOS_OT_MULTI_HASHED, OR_RP_2, 2, 0);

	dv_oid_to_obj(oid, &obj);

	assert_int_equal(2, obj.ddbo_nr_grps);
	assert_string_equal("DAOS_OT_MULTI_HASHED", obj.ddbo_otype_str);
}


static uint32_t fake_dump_value_cb_called;
static d_iov_t fake_dump_value_cb_value;
static uint8_t fake_dump_value_cb_value_buf[128];
static int
fake_dump_value_cb(void *cb_args, d_iov_t *value)
{
	fake_dump_value_cb_called++;
	assert_true(value->iov_len <= ARRAY_SIZE(fake_dump_value_cb_value_buf));
	fake_dump_value_cb_value = *value;
	fake_dump_value_cb_value.iov_buf = fake_dump_value_cb_value_buf;
	memcpy(fake_dump_value_cb_value_buf, value->iov_buf, value->iov_len);
	return 0;
}

static int
test_dump_value(daos_handle_t poh, uuid_t cont_uuid, daos_unit_oid_t oid, daos_key_t *dkey,
		daos_key_t *akey, daos_recx_t *recx, dv_dump_value_cb dump_cb, void *cb_arg)
{
	struct dv_tree_path	 path = {0};

	uuid_copy(path.vtp_cont, cont_uuid);
	path.vtp_oid = oid;
	path.vtp_dkey = *dkey;
	path.vtp_akey = *akey;
	if (recx)
		path.vtp_recx = *recx;

	return dv_dump_value(poh, &path, dump_cb, cb_arg);

}

static void
get_value_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	daos_recx_t		 recx = {.rx_idx = 0, .rx_nr = 10};

	/* first akey is a recx */
	assert_success(test_dump_value(tctx->dvt_poh, g_uuids[0], g_oids[0], &g_dkeys[0],
				       &g_akeys[0], &recx, fake_dump_value_cb, NULL));

	assert_int_equal(1, fake_dump_value_cb_called);
	assert_non_null(fake_dump_value_cb_value.iov_buf);
	assert_true(fake_dump_value_cb_value.iov_len > 0);

	/* second akey is a single value */
	fake_dump_value_cb_called = 0;
	assert_success(test_dump_value(tctx->dvt_poh, g_uuids[0], g_oids[0], &g_dkeys[0],
				       &g_akeys[1], NULL, fake_dump_value_cb, NULL));

	assert_int_equal(1, fake_dump_value_cb_called);
	assert_non_null(fake_dump_value_cb_value.iov_buf);
	assert_true(fake_dump_value_cb_value.iov_len > 0);
}

static uint32_t fake_dump_ilog_entry_called;
static int
fake_dump_ilog_entry(void *cb_arg, struct ddb_ilog_entry *entry)
{
	fake_dump_ilog_entry_called++;
	return 0;
}

static void
get_obj_ilog_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	daos_handle_t coh;

	daos_unit_oid_t null_oid = {0};
	daos_unit_oid_t bad_oid = {.id_pub.lo = 1};

	assert_success(vos_cont_open(tctx->dvt_poh, g_uuids[0], &coh));

	assert_rc_equal(-DER_INVAL, dv_get_obj_ilog_entries(DAOS_HDL_INVAL, null_oid,
							    fake_dump_ilog_entry, NULL));
	assert_rc_equal(-DER_INVAL, dv_get_obj_ilog_entries(DAOS_HDL_INVAL, g_oids[0],
							    fake_dump_ilog_entry, NULL));
	assert_rc_equal(-DER_INVAL, dv_get_obj_ilog_entries(coh, null_oid,
							    fake_dump_ilog_entry, NULL));
	assert_rc_equal(-DER_INVAL, dv_get_obj_ilog_entries(coh, bad_oid,
							    fake_dump_ilog_entry, NULL));

	assert_success(dv_get_obj_ilog_entries(coh, g_oids[0], fake_dump_ilog_entry, NULL));

	assert_int_equal(1, fake_dump_ilog_entry_called);

	vos_cont_close(coh);
}

static void
abort_obj_ilog_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	daos_handle_t		 coh = {0};
	daos_unit_oid_t		 null_oid = {0};

	fake_dump_ilog_entry_called = 0;

	/* error handling */
	assert_rc_equal(-DER_INVAL, dv_process_obj_ilog_entries(coh, null_oid, DDB_ILOG_OP_ABORT));

	assert_success(vos_cont_open(tctx->dvt_poh, g_uuids[0], &coh));

	/* First make sure there is an ilog to rm */
	assert_success(dv_get_obj_ilog_entries(coh, g_oids[0], fake_dump_ilog_entry, NULL));
	assert_int_equal(1, fake_dump_ilog_entry_called);
	fake_dump_ilog_entry_called = 0;

	/* Abort the ilogs */
	assert_success(dv_process_obj_ilog_entries(coh, g_oids[0], DDB_ILOG_OP_ABORT));

	/* Now should not be any ilog entries */
	assert_success(dv_get_obj_ilog_entries(coh, g_oids[0], fake_dump_ilog_entry, NULL));
	assert_int_equal(0, fake_dump_ilog_entry_called);

	vos_cont_close(coh);
}

static void
get_dkey_ilog_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	daos_handle_t		 coh;
	daos_unit_oid_t		 null_oid = {0};

	assert_success(vos_cont_open(tctx->dvt_poh, g_uuids[0], &coh));

	assert_rc_equal(-DER_INVAL, dv_get_key_ilog_entries(DAOS_HDL_INVAL, null_oid, NULL, NULL,
							    fake_dump_ilog_entry, NULL));

	fake_dump_ilog_entry_called = 0;
	assert_success(dv_get_key_ilog_entries(coh, g_oids[1], &g_dkeys[0], NULL,
					       fake_dump_ilog_entry,
					       NULL));
	assert_int_equal(1, fake_dump_ilog_entry_called);

	fake_dump_ilog_entry_called = 0;
	assert_success(dv_get_key_ilog_entries(coh, g_oids[1], &g_dkeys[0], &g_akeys[0],
					       fake_dump_ilog_entry,
					       NULL));
	assert_int_equal(1, fake_dump_ilog_entry_called);
	fake_dump_ilog_entry_called = 0;

	vos_cont_close(coh);
}

static void
abort_dkey_ilog_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	daos_handle_t		 coh;
	daos_unit_oid_t		 null_oid = {0};

	assert_success(vos_cont_open(tctx->dvt_poh, g_uuids[1], &coh));

	assert_invalid(dv_process_key_ilog_entries(DAOS_HDL_INVAL, null_oid, NULL, NULL,
						   DDB_ILOG_OP_UNKNOWN));


	/* akey */
	assert_success(dv_get_key_ilog_entries(coh, g_oids[0], &g_dkeys[0], &g_akeys[0],
					       fake_dump_ilog_entry, NULL));
	assert_int_equal(1, fake_dump_ilog_entry_called);

	assert_success(dv_process_key_ilog_entries(coh, g_oids[0], &g_dkeys[0], &g_akeys[0],
						   DDB_ILOG_OP_ABORT));

	fake_dump_ilog_entry_called = 0;
	assert_success(dv_get_key_ilog_entries(coh, g_oids[0], &g_dkeys[0], &g_akeys[0],
					       fake_dump_ilog_entry, NULL));
	assert_int_equal(0, fake_dump_ilog_entry_called);

	/* dkey */
	assert_success(dv_get_key_ilog_entries(coh, g_oids[0], &g_dkeys[0], NULL,
					       fake_dump_ilog_entry, NULL));
	assert_int_equal(1, fake_dump_ilog_entry_called);

	assert_success(dv_process_key_ilog_entries(coh, g_oids[0], &g_dkeys[0], NULL,
						   DDB_ILOG_OP_ABORT));

	fake_dump_ilog_entry_called = 0;
	assert_success(dv_get_key_ilog_entries(coh, g_oids[0], &g_dkeys[0], NULL,
					       fake_dump_ilog_entry, NULL));
	assert_int_equal(0, fake_dump_ilog_entry_called);

	vos_cont_close(coh);
}

int entry_handler_called;
static int
committed_entry_handler(struct dv_dtx_committed_entry *entry, void *cb_arg)
{
	entry_handler_called++;
	return 0;
}

static int
active_entry_handler(struct dv_dtx_active_entry *entry, void *cb_arg)
{
	entry_handler_called++;
	return 0;
}

static void
get_dtx_tables_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	daos_handle_t		 coh = DAOS_HDL_INVAL;

	assert_rc_equal(-DER_INVAL, dv_committed_dtx(coh, committed_entry_handler, NULL));
	assert_int_equal(0, entry_handler_called);

	assert_rc_equal(-DER_INVAL, dv_active_dtx(coh, active_entry_handler, NULL));
	assert_int_equal(0, entry_handler_called);

	assert_success(vos_cont_open(tctx->dvt_poh, g_uuids[0], &coh));

	dvt_vos_insert_2_records_with_dtx(coh);

	assert_success(dv_committed_dtx(coh, committed_entry_handler, NULL));
	assert_int_equal(1, entry_handler_called);

	entry_handler_called = 0;
	assert_success(dv_active_dtx(coh, active_entry_handler, NULL));
	assert_int_equal(1, entry_handler_called);

	vos_cont_close(coh);
}

static void
verify_correct_params_for_update_value_tests(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;
	daos_handle_t		poh = tctx->dvt_poh;
	struct dv_tree_path	vtp = {};
	d_iov_t			value_iov = {0};

	assert_rc_equal(-DER_INVAL, dv_update(DAOS_HDL_INVAL, &vtp, &value_iov, 1));
	assert_rc_equal(-DER_INVAL, dv_update(poh, &vtp, &value_iov, 1));

	uuid_copy(vtp.vtp_cont, g_uuids[3]);
	vtp.vtp_oid = g_oids[0];
	vtp.vtp_dkey = g_dkeys[0];
	vtp.vtp_akey = g_akeys[0];
	assert_rc_equal(-DER_INVAL, dv_update(poh, &vtp, &value_iov, 1));
}

static void
assert_update_existing_path(daos_handle_t poh, struct dv_tree_path *vtp)
{
	d_iov_t	value_iov = {0};
	char	value_buf[256];

	/* First get the value_buf using dump_value then use it to create an updated value */
	assert_success(dv_dump_value(poh, vtp, fake_dump_value_cb, NULL));
	snprintf(value_buf, 256, "Updated: %s", fake_dump_value_cb_value_buf);

	d_iov_set(&value_iov, value_buf, strlen(value_buf));

	/* if it's an array path, update so will be same length as new value */
	if (vtp->vtp_recx.rx_nr > 0)
		vtp->vtp_recx.rx_nr = value_iov.iov_len;
	assert_success(dv_update(poh, vtp, &value_iov, 3));

	/* Verify that after loading the value_buf, the same value_buf is dumped */
	assert_success(dv_dump_value(poh, vtp, fake_dump_value_cb, NULL));
	assert_key_equal(value_iov, fake_dump_value_cb_value);
}

static void
update_value_to_modify_tests(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;
	daos_handle_t		poh = tctx->dvt_poh;
	struct dv_tree_path	vtp = {};
	daos_handle_t		coh;


	uuid_copy(vtp.vtp_cont, g_uuids[3]);
	vtp.vtp_oid = g_oids[0];
	vtp.vtp_dkey = g_dkeys[0];
	vtp.vtp_akey = g_akeys[1]; /* single value type */

	assert_update_existing_path(poh, &vtp);

	vtp.vtp_akey = g_akeys[0]; /* array value type */
	dv_cont_open(poh, vtp.vtp_cont, &coh);
	dv_get_recx(coh, vtp.vtp_oid, &vtp.vtp_dkey, &vtp.vtp_akey, 0, &vtp.vtp_recx);
	dv_cont_close(&coh);
	assert_update_existing_path(poh, &vtp);
}

static void
assert_update_new_path(daos_handle_t poh, struct dv_tree_path *vtp)
{
	d_iov_t	 value_iov = {0};
	char	*value_buf = "A New value";

	/* First check that the value doesn't exist */
	memset(fake_dump_value_cb_value_buf, 0, ARRAY_SIZE(fake_dump_value_cb_value_buf));
	assert_success(dv_dump_value(poh, vtp, fake_dump_value_cb, NULL));
	assert_string_equal("", fake_dump_value_cb_value_buf);

	d_iov_set(&value_iov, value_buf, strlen(value_buf));

	assert_success(dv_update(poh, vtp, &value_iov, 3));

	/* Verify that after loading the value_buf, the same value_buf is dumped */
	assert_success(dv_dump_value(poh, vtp, fake_dump_value_cb, NULL));
	assert_key_equal(value_iov, fake_dump_value_cb_value);
}

static void
update_value_to_insert_tests(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;
	daos_handle_t		poh = tctx->dvt_poh;
	struct dv_tree_path	vtp = {};

	uuid_copy(vtp.vtp_cont, g_uuids[3]);
	/*
	 * Create a new object with dkey & akey. If this succeeds, we assume that could also create
	 * a new dkey within an existing oid, etc
	 */
	vtp.vtp_oid = dvt_gen_uoid(999);
	vtp.vtp_dkey = g_dkeys[0];
	vtp.vtp_akey = g_akeys[0];

	assert_update_new_path(poh, &vtp);
}

static void
clear_committed_table(void **state)
{
	struct dt_vos_pool_ctx *tctx = *state;
	daos_handle_t		poh = tctx->dvt_poh;
	daos_handle_t		coh;

	dv_cont_open(poh, g_uuids[5], &coh);

	dvt_vos_insert_2_records_with_dtx(coh);

	assert_success(dv_clear_committed_table(coh));

	entry_handler_called = 0;
	dv_committed_dtx(coh, committed_entry_handler, NULL);

	assert_int_equal(0, entry_handler_called);

	dv_cont_close(&coh);

}

#define DELETE_SUCCESS(poh, vtp) assert_success(dv_delete(poh, &vtp))
static void
delete_path_parts_tests(void **state)
{
	struct dt_vos_pool_ctx	*tctx = *state;
	daos_handle_t		 poh = tctx->dvt_poh;
	daos_handle_t		 coh;
	struct dv_tree_path	 vtp = {0};
	uuid_t			 cont_test;
	daos_unit_oid_t		 uoid_test = {0};
	daos_key_t		 dkey_test = {0};
	daos_key_t		 akey_test = {0};

	/* Don't allow empty path */
	assert_rc_equal(-DER_INVAL, dv_delete(poh, &vtp));

	dv_get_cont_uuid(poh, 0, vtp.vtp_cont);
	DELETE_SUCCESS(poh, vtp);
	dv_get_cont_uuid(poh, 0, cont_test);
	assert_uuid_not_equal(vtp.vtp_cont, cont_test);
	/* shouldn't be able to delete same container */
	assert_rc_equal(-DER_NONEXIST, dv_delete(poh, &vtp));

	/*
	 * Remaining deletes happen within a container, so open the container to get the
	 * VOS path part identifier
	 */
	dv_get_cont_uuid(poh, 0, vtp.vtp_cont);
	assert_success(dv_cont_open(poh, vtp.vtp_cont, &coh));

	/*
	 * Delete an object
	 * get oid from index 0. This will be deleted, so should not exist after
	 */
	assert_success(dv_get_object_oid(coh, 0, &vtp.vtp_oid));
	DELETE_SUCCESS(poh, vtp);
	/* index 0 should not be same oid now */
	assert_success(dv_get_object_oid(coh, 0, &uoid_test));
	assert_oid_not_equal(vtp.vtp_oid.id_pub, uoid_test.id_pub);
	/* Shouldn't be able to delete the same object again */
	assert_rc_equal(-DER_NONEXIST, dv_delete(poh, &vtp));

	/*
	 * delete dkey
	 */
	vtp.vtp_oid = uoid_test; /* reset uoid_before to oid that hasn't been deleted */
	dv_get_dkey(coh, vtp.vtp_oid, 0, &vtp.vtp_dkey);
	DELETE_SUCCESS(poh, vtp);
	/* should still have the object */
	assert_success(dv_get_object_oid(coh, 0, &uoid_test));
	assert_oid_equal(vtp.vtp_oid.id_pub, uoid_test.id_pub);
	dv_get_dkey(coh, vtp.vtp_oid, 0, &dkey_test);
	assert_key_not_equal(vtp.vtp_dkey, dkey_test);

	/*
	 * delete akey
	 */
	vtp.vtp_dkey = dkey_test;
	dv_get_akey(coh, vtp.vtp_oid, &vtp.vtp_dkey, 0, &vtp.vtp_akey);
	DELETE_SUCCESS(poh, vtp);
	/* should still have the object and dkey */
	assert_success(dv_get_object_oid(coh, 0, &uoid_test));
	assert_oid_equal(vtp.vtp_oid.id_pub, uoid_test.id_pub);
	dv_get_dkey(coh, vtp.vtp_oid, 0, &dkey_test);
	assert_key_equal(vtp.vtp_dkey, dkey_test);
	dv_get_akey(coh, vtp.vtp_oid, &vtp.vtp_dkey, 0, &akey_test);
	assert_key_not_equal(vtp.vtp_akey, akey_test);

	dv_cont_close(&coh);
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


/*
 * All these tests use the same VOS tree that is created at suit_setup. Therefore, tests
 * that modify the state of the tree (delete, add, etc) should be run after all others.
 */
#define TEST(x) { #x, x, dv_test_setup, dv_test_teardown }
const struct CMUnitTest dv_test_cases[] = {
	{ "open_pool", open_pool_test, NULL, NULL }, /* don't want this test to run with setup */
	TEST(list_items_test),
	TEST(get_cont_uuid_from_idx_tests),
	TEST(get_oid_from_idx_tests),
	TEST(get_dkey_from_idx_tests),
	TEST(get_akey_from_idx_tests),
	TEST(get_recx_from_idx_tests),
	TEST(update_path_with_values_from_index_tests),
	TEST(idx_in_path_must_be_valid_tests),
	TEST(path_must_be_valid_tests),
	TEST(get_value_tests),
	TEST(get_obj_ilog_tests),
	TEST(abort_obj_ilog_tests),
	TEST(get_dkey_ilog_tests),
	TEST(abort_dkey_ilog_tests),
	TEST(get_superblock_tests),
	TEST(obj_id_2_ddb_test),
	TEST(get_dtx_tables_tests),
	TEST(delete_path_parts_tests),
	TEST(verify_correct_params_for_update_value_tests),
	TEST(update_value_to_modify_tests),
	TEST(update_value_to_insert_tests),
	TEST(clear_committed_table),
};

int
dv_tests_run()
{
	return cmocka_run_group_tests_name("DDB VOS Interface Tests", dv_test_cases,
					   dv_suit_setup, dv_suit_teardown);
}
