/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is for simple tests of extend, which does not need to kill the
 * rank, and only used to verify the consistency after different data model
 * extend.
 *
 * tests/suite/daos_extend_simple.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"
#include "dfs_test.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

#define KEY_NR		10
#define OBJ_NR		10

static void
extend_dkeys(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	struct ioreq	req;
	int		i;
	int		j;
	int		rc;

	print_message("BEGIN %s\n", __FUNCTION__);

	if (!test_runable(arg, 3))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, OC_RP_3G1, 0, 0,
					    arg->myrank);
		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);

		/** Insert 10 records */
		print_message("Insert %d kv record in object "DF_OID"\n",
			      KEY_NR, DP_OID(oids[i]));
		for (j = 0; j < KEY_NR; j++) {
			char	key[32] = {0};

			sprintf(key, "dkey_0_%d", j);
			insert_single(key, "a_key", 0, "data",
				      strlen("data") + 1,
				      DAOS_TX_NONE, &req);
		}
		ioreq_fini(&req);
	}

	extend_single_pool_rank(arg, 3);

	for (i = 0; i < OBJ_NR; i++) {
		rc = daos_obj_verify(arg->coh, oids[i], DAOS_EPOCH_MAX);
		assert_rc_equal(rc, 0);
	}
}

static void
extend_akeys(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	struct ioreq	req;
	int		i;
	int		j;
	int		rc;

	print_message("BEGIN %s\n", __FUNCTION__);

	if (!test_runable(arg, 3))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, OC_RP_3G1, 0, 0,
					    arg->myrank);
		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);

		/** Insert 10 records */
		print_message("Insert %d kv record in object "DF_OID"\n",
			      KEY_NR, DP_OID(oids[i]));
		for (j = 0; j < KEY_NR; j++) {
			char	akey[16];

			sprintf(akey, "%d", j);
			insert_single("dkey_1_0", akey, 0, "data",
				      strlen("data") + 1,
				      DAOS_TX_NONE, &req);
		}
		ioreq_fini(&req);
	}

	extend_single_pool_rank(arg, 3);
	for (i = 0; i < OBJ_NR; i++) {
		rc = daos_obj_verify(arg->coh, oids[i], DAOS_EPOCH_MAX);
		assert_rc_equal(rc, 0);
	}
}

static void
extend_indexes(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	struct ioreq	req;
	int		i;
	int		j;
	int		k;
	int		rc;

	print_message("BEGIN %s\n", __FUNCTION__);

	if (!test_runable(arg, 3))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, OC_RP_3G1, 0, 0,
					    arg->myrank);
		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);

		/** Insert 10 records */
		print_message("Insert %d kv record in object "DF_OID"\n",
			      KEY_NR, DP_OID(oids[i]));

		for (j = 0; j < KEY_NR; j++) {
			char	key[32] = {0};

			sprintf(key, "dkey_2_%d", j);
			for (k = 0; k < 20; k++)
				insert_single(key, "a_key", k, "data",
					      strlen("data") + 1, DAOS_TX_NONE,
					      &req);
		}
		ioreq_fini(&req);
	}

	extend_single_pool_rank(arg, 3);
	for (i = 0; i < OBJ_NR; i++) {
		rc = daos_obj_verify(arg->coh, oids[i], DAOS_EPOCH_MAX);
		assert_rc_equal(rc, 0);
	}
}

static void
extend_large_rec(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oids[OBJ_NR];
	struct ioreq	req;
	char		buffer[5000];
	int		i;
	int		j;
	int		rc;

	print_message("BEGIN %s\n", __FUNCTION__);

	if (!test_runable(arg, 3))
		return;

	memset(buffer, 'a', 5000);
	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, OC_RP_3G1, 0, 0,
					    arg->myrank);
		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);

		/** Insert 10 records */
		print_message("Insert %d kv record in object "DF_OID"\n",
			      KEY_NR, DP_OID(oids[i]));
		for (j = 0; j < KEY_NR; j++) {
			char	key[32] = {0};

			sprintf(key, "dkey_3_%d", j);
			insert_single(key, "a_key", 0, buffer, 5000,
				      DAOS_TX_NONE, &req);
		}
		ioreq_fini(&req);
	}

	extend_single_pool_rank(arg, 3);
	for (i = 0; i < OBJ_NR; i++) {
		rc = daos_obj_verify(arg->coh, oids[i], DAOS_EPOCH_MAX);
		assert_rc_equal(rc, 0);
	}
}

static void
extend_objects(void **state)
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_obj_id_t	oids[OBJ_NR];
	int		i;

	print_message("BEGIN %s\n", __FUNCTION__);

	if (!test_runable(arg, 3))
		return;

	for (i = 0; i < OBJ_NR; i++) {
		oids[i] = daos_test_oid_gen(arg->coh, OC_S1, 0,
					    0, arg->myrank);
		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);

		insert_single("dkey", "akey", 0, "data", strlen("data") + 1,
			      DAOS_TX_NONE, &req);
		ioreq_fini(&req);
	}

	extend_single_pool_rank(arg, 3);

	for (i = 0; i < OBJ_NR; i++) {
		char buffer[16];

		ioreq_init(&req, arg->coh, oids[i], DAOS_IOD_ARRAY, arg);
		memset(buffer, 0, 16);
		lookup_single("dkey", "akey", 0, buffer, 16,
			      DAOS_TX_NONE, &req);
		assert_string_equal(buffer, "data");
		ioreq_fini(&req);
	}
}

#define EXTEND_OBJ_NR	1000
struct extend_cb_arg{
	daos_obj_id_t	*oids;
	dfs_t		*dfs_mt;
	dfs_obj_t	*dir;
	d_rank_t	rank;
	int		opc;
	bool		kill;
};

enum extend_opc {
	EXTEND_PUNCH,
	EXTEND_STAT,
	EXTEND_ENUMERATE,
	EXTEND_FETCH,
	EXTEND_UPDATE,
};

static void
extend_read_check(dfs_t *dfs_mt, dfs_obj_t *dir)
{
	char		*buf = NULL;
	char		*verify_buf = NULL;
	daos_size_t	buf_size = 512 * 1024;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	d_iov_t		verify_iov;
	int		i;

	buf = malloc(buf_size);
	verify_buf = malloc(buf_size);
	assert_non_null(buf);
	assert_non_null(verify_buf);
	d_iov_set(&iov, buf, buf_size);
	d_iov_set(&verify_iov, buf, buf_size);
	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;

	for (i = 0; i < 20; i++) {
		char filename[32];
		daos_size_t read_size = buf_size;
		dfs_obj_t *obj;
		int rc;

		sprintf(filename, "file%d", i);
		rc = dfs_open(dfs_mt, dir, filename, S_IFREG | S_IWUSR | S_IRUSR,
			      O_RDWR, OC_EC_2P1GX, 1048576, NULL, &obj);
		assert_int_equal(rc, 0);

		memset(verify_buf, 'a' + i, buf_size);
		rc = dfs_read(dfs_mt, obj, &sgl, 0, &read_size, NULL);
		assert_int_equal(rc, 0);
		assert_int_equal((int)read_size, buf_size);
		assert_memory_equal(buf, verify_buf, read_size);
		rc = dfs_release(obj);
		assert_int_equal(rc, 0);
	}
	free(buf);
	free(verify_buf);
}

static void
extend_write(dfs_t *dfs_mt, dfs_obj_t *dir)
{
	char		*buf = NULL;
	daos_size_t	buf_size = 512 * 1024;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	int		i;

	buf = malloc(buf_size);
	assert_non_null(buf);
	d_iov_set(&iov, buf, buf_size);
	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;

	for (i = 0; i < 20; i++) {
		char filename[32];
		dfs_obj_t *obj;
		int rc;

		sprintf(filename, "file%d", i);
		rc = dfs_open(dfs_mt, dir, filename, S_IFREG | S_IWUSR | S_IRUSR,
			      O_RDWR | O_CREAT, OC_EC_2P1GX, 1048576, NULL, &obj);
		assert_int_equal(rc, 0);

		memset(buf, 'a' + i, buf_size);
		rc = dfs_write(dfs_mt, obj, &sgl, 0, NULL);
		assert_int_equal(rc, 0);
		rc = dfs_release(obj);
		assert_int_equal(rc, 0);
	}
	free(buf);
}

static int
extend_cb_internal(void *arg)
{
	test_arg_t		*test_arg = arg;
	struct extend_cb_arg	*cb_arg = test_arg->rebuild_cb_arg;
	dfs_t			*dfs_mt = cb_arg->dfs_mt;
	daos_obj_id_t		*oids = cb_arg->oids;
	dfs_obj_t		*dir = cb_arg->dir;
	struct dirent		ents[10];
	int			opc = cb_arg->opc;
	int			total_entries = 0;
	uint32_t		num_ents = 10;
	daos_anchor_t		anchor = { 0 };
	const char		*pre_op = (cb_arg->kill ? "kill" : "extend");
	int			rc;
	int			i;

	print_message("sleep 10 seconds then %s %u and start op %d\n", pre_op,
		      cb_arg->rank, opc);
	sleep(10);

	if (cb_arg->kill) {
		daos_kill_server(test_arg, test_arg->pool.pool_uuid, test_arg->group,
				 test_arg->pool.alive_svc, cb_arg->rank);
	} else {
		/* it should fail with -DER_BUSY */
		print_message("extend pool " DF_UUID " rank %u\n",
			      DP_UUID(test_arg->pool.pool_uuid), cb_arg->rank);
		rc = dmg_pool_extend(test_arg->dmg_config, test_arg->pool.pool_uuid,
				     test_arg->group, &cb_arg->rank, 1);
		assert_int_equal(rc, 0);
	}
	/* Kill another rank during extend */
	switch(opc) {
	case EXTEND_PUNCH:
		print_message("punch objects during %s\n", pre_op);
		for (i = 0; i < EXTEND_OBJ_NR; i++) {
			char filename[32];

			sprintf(filename, "file%d", i);
			rc = dfs_remove(dfs_mt, dir, filename, true, &oids[i]);
			assert_int_equal(rc, 0);
		}
		break;
	case EXTEND_STAT:
		print_message("stat objects during %s\n", pre_op);
		for (i = 0; i < EXTEND_OBJ_NR; i++) {
			char		filename[32];
			struct stat	stbuf;

			sprintf(filename, "file%d", i);
			rc = dfs_stat(dfs_mt, dir, filename, &stbuf);
			assert_int_equal(rc, 0);
		}
		break;
	case EXTEND_ENUMERATE:
		print_message("enumerate objects during %s\n", pre_op);
		while (!daos_anchor_is_eof(&anchor)) {
			num_ents = 10;
			rc = dfs_readdir(dfs_mt, dir, &anchor, &num_ents, ents);
			assert_int_equal(rc, 0);
			total_entries += num_ents;
		}
		assert_int_equal(total_entries, 1000);
		break;
	case EXTEND_FETCH:
		print_message("fetch objects during %s\n", pre_op);
		extend_read_check(dfs_mt, dir);
		break;
	case EXTEND_UPDATE:
		print_message("update objects during %s\n", pre_op);
		extend_write(dfs_mt, dir);
		break;
	default:
		break;
	}

	daos_debug_set_params(test_arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);

	return 0;
}

void
dfs_extend_internal(void **state, int opc, test_rebuild_cb_t extend_cb, bool kill)
{
	test_arg_t	*arg = *state;
	dfs_t		*dfs_mt;
	daos_handle_t	co_hdl;
	dfs_obj_t	*obj;
	dfs_obj_t	*dir;
	uuid_t		co_uuid;
	int		i;
	d_rank_t	extend_rank = 3;
	char		str[37];
	daos_obj_id_t	oids[EXTEND_OBJ_NR];
	struct extend_cb_arg cb_arg;
	dfs_attr_t attr = {};
	int		rc;

	attr.da_props = daos_prop_alloc(2);
	assert_non_null(attr.da_props);
	attr.da_props->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_LVL;
	attr.da_props->dpp_entries[0].dpe_val = DAOS_PROP_CO_REDUN_RANK;
	attr.da_props->dpp_entries[1].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	attr.da_props->dpp_entries[1].dpe_val = DAOS_PROP_CO_REDUN_RF1;
	rc = dfs_cont_create(arg->pool.poh, &co_uuid, &attr, &co_hdl, &dfs_mt);
	daos_prop_free(attr.da_props);
	assert_int_equal(rc, 0);
	print_message("Created DFS Container "DF_UUIDF"\n", DP_UUID(co_uuid));

	rc = dfs_open(dfs_mt, NULL, "dir", S_IFDIR | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, OC_EC_2P1GX, 0, NULL, &dir);
	assert_int_equal(rc, 0);

	/* Create 1000 files */
	if (opc == EXTEND_FETCH) {
		extend_write(dfs_mt, dir);
	} else {
		for (i = 0; i < EXTEND_OBJ_NR; i++) {
			char filename[32];

			sprintf(filename, "file%d", i);
			rc = dfs_open(dfs_mt, dir, filename, S_IFREG | S_IWUSR | S_IRUSR,
				      O_RDWR | O_CREAT, OC_EC_2P1GX, 1048576, NULL, &obj);
			assert_int_equal(rc, 0);
			dfs_obj2id(obj, &oids[i]);
			rc = dfs_release(obj);
			assert_int_equal(rc, 0);
		}
	}

	cb_arg.oids = oids;
	cb_arg.dfs_mt = dfs_mt;
	cb_arg.dir = dir;
	cb_arg.opc = opc;
	cb_arg.kill = kill;
	if (kill)
		cb_arg.rank = 2;
	else
		cb_arg.rank = 4;

	arg->rebuild_cb = extend_cb;
	arg->rebuild_cb_arg = &cb_arg;

	/* HOLD rebuild ULT. FIXME: maybe change to use test_set_engine_fail_loc()? */
	print_message("inject DAOS_REBUILD_TGT_SCAN_HANG fault on engines\n");
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			      DAOS_REBUILD_TGT_SCAN_HANG | DAOS_FAIL_ALWAYS, 0, NULL);

	arg->no_rebuild=1;
	extend_single_pool_rank(arg, extend_rank);
	arg->no_rebuild=0;

	print_message("sleep 30 secs for rank %u %s\n", cb_arg.rank,
		      cb_arg.kill ? "exclude" : "extend");
	sleep(30);
	print_message("wait for rebuild due to rank %u extend and rank %u %s\n", extend_rank,
		      cb_arg.rank, cb_arg.kill ? "exclude" : "extend");
	test_rebuild_wait(&arg, 1);

	if (opc == EXTEND_UPDATE) {
		print_message("First extend update read check\n");
		extend_read_check(dfs_mt, dir);
	}

	arg->rebuild_cb = NULL;
	arg->rebuild_cb_arg = NULL;
	if (kill) {
		print_message("reintegrate rank %u\n", cb_arg.rank);
		reintegrate_single_pool_rank(arg, cb_arg.rank, true);
	}

	if (opc == EXTEND_UPDATE) {
		print_message("Second extend update read check\n");
		extend_read_check(dfs_mt, dir);
	}

	rc = dfs_release(dir);
	assert_int_equal(rc, 0);
	rc = dfs_umount(dfs_mt);
	assert_int_equal(rc, 0);

	rc = daos_cont_close(co_hdl, NULL);
	assert_rc_equal(rc, 0);

	uuid_unparse(co_uuid, str);
	rc = daos_cont_destroy(arg->pool.poh, str, 1, NULL);
	assert_rc_equal(rc, 0);
}

void
dfs_extend_punch_kill(void **state)
{
	FAULT_INJECTION_REQUIRED();

	print_message("BEGIN %s\n", __FUNCTION__);
	dfs_extend_internal(state, EXTEND_PUNCH, extend_cb_internal, true);
}

void
dfs_extend_punch_extend(void **state)
{
	FAULT_INJECTION_REQUIRED();

	print_message("BEGIN %s\n", __FUNCTION__);
	dfs_extend_internal(state, EXTEND_PUNCH, extend_cb_internal, false);
}

void
dfs_extend_stat_kill(void **state)
{
	FAULT_INJECTION_REQUIRED();

	print_message("BEGIN %s\n", __FUNCTION__);
	dfs_extend_internal(state, EXTEND_STAT, extend_cb_internal, true);
}

void
dfs_extend_stat_extend(void **state)
{
	FAULT_INJECTION_REQUIRED();

	print_message("BEGIN %s\n", __FUNCTION__);
	dfs_extend_internal(state, EXTEND_STAT, extend_cb_internal, false);
}

void
dfs_extend_enumerate_kill(void **state)
{
	FAULT_INJECTION_REQUIRED();

	print_message("BEGIN %s\n", __FUNCTION__);
	dfs_extend_internal(state, EXTEND_ENUMERATE, extend_cb_internal, true);
}

void
dfs_extend_enumerate_extend(void **state)
{
	FAULT_INJECTION_REQUIRED();

	print_message("BEGIN %s\n", __FUNCTION__);
	dfs_extend_internal(state, EXTEND_ENUMERATE, extend_cb_internal, false);
}

void
dfs_extend_fetch_kill(void **state)
{
	FAULT_INJECTION_REQUIRED();

	print_message("BEGIN %s\n", __FUNCTION__);
	dfs_extend_internal(state, EXTEND_FETCH, extend_cb_internal, true);
}

void
dfs_extend_fetch_extend(void **state)
{
	FAULT_INJECTION_REQUIRED();

	print_message("BEGIN %s\n", __FUNCTION__);
	dfs_extend_internal(state, EXTEND_FETCH, extend_cb_internal, false);
}

void
dfs_extend_write_kill(void **state)
{
	FAULT_INJECTION_REQUIRED();

	print_message("BEGIN %s\n", __FUNCTION__);
	dfs_extend_internal(state, EXTEND_UPDATE, extend_cb_internal, true);
}

void
dfs_extend_write_extend(void **state)
{
	FAULT_INJECTION_REQUIRED();

	print_message("BEGIN %s\n", __FUNCTION__);
	dfs_extend_internal(state, EXTEND_UPDATE, extend_cb_internal, false);
}

void
dfs_extend_fail_retry(void **state)
{
	test_arg_t	*arg = *state;
	dfs_t		*dfs_mt;
	daos_handle_t	co_hdl;
	dfs_obj_t	*dir;
	uuid_t		co_uuid;
	char		str[37];
	dfs_attr_t attr = {};
	int		rc;

	FAULT_INJECTION_REQUIRED();

	print_message("BEGIN %s\n", __FUNCTION__);

	attr.da_props = daos_prop_alloc(1);
	assert_non_null(attr.da_props);
	attr.da_props->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_LVL;
	attr.da_props->dpp_entries[0].dpe_val = DAOS_PROP_CO_REDUN_RANK;
	rc = dfs_cont_create(arg->pool.poh, &co_uuid, &attr, &co_hdl, &dfs_mt);
	daos_prop_free(attr.da_props);
	assert_int_equal(rc, 0);
	print_message("Created DFS Container "DF_UUIDF"\n", DP_UUID(co_uuid));

	rc = dfs_open(dfs_mt, NULL, "dir", S_IFDIR | S_IWUSR | S_IRUSR,
		      O_RDWR | O_CREAT, OC_EC_2P1GX, 0, NULL, &dir);
	assert_int_equal(rc, 0);

	extend_write(dfs_mt, dir);
	/* extend failure */
	print_message("first extend will fail then exclude\n");
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			      DAOS_REBUILD_OBJ_FAIL | DAOS_FAIL_ALWAYS, 0, NULL);
	arg->no_rebuild = 1;
	extend_single_pool_rank(arg, 3);
	print_message("sleep 30 seconds for extend to fail and exit\n");
	sleep(30);
	arg->no_rebuild = 0;
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0, 0, NULL);
	extend_read_check(dfs_mt, dir);

	print_message("retry extend\n");
	/* retry extend */
	extend_single_pool_rank(arg, 3);
	extend_read_check(dfs_mt, dir);

	rc = dfs_release(dir);
	assert_int_equal(rc, 0);
	rc = dfs_umount(dfs_mt);
	assert_int_equal(rc, 0);

	rc = daos_cont_close(co_hdl, NULL);
	assert_rc_equal(rc, 0);

	uuid_unparse(co_uuid, str);
	rc = daos_cont_destroy(arg->pool.poh, str, 1, NULL);
	assert_rc_equal(rc, 0);
}

/** create a new pool/container for each test */
static const struct CMUnitTest extend_tests[] = {
	{"EXTEND1: extend small rec multiple dkeys",
	 extend_dkeys, rebuild_sub_3nodes_rf0_setup, test_teardown},
	{"EXTEND2: extend small rec multiple akeys",
	 extend_akeys, rebuild_sub_3nodes_rf0_setup, test_teardown},
	{"EXTEND3: extend small rec multiple indexes",
	 extend_indexes, rebuild_sub_3nodes_rf0_setup, test_teardown},
	{"EXTEND4: extend large rec single index",
	 extend_large_rec, rebuild_sub_3nodes_rf0_setup, test_teardown},
	{"EXTEND5: extend multiple objects",
	 extend_objects, rebuild_sub_3nodes_rf0_setup, test_teardown},
	{"EXTEND6: punch object during extend and kill",
	 dfs_extend_punch_kill, rebuild_sub_3nodes_rf0_setup, test_teardown},
	{"EXTEND7: punch object during extend and extend",
	 dfs_extend_punch_extend, rebuild_sub_3nodes_rf0_setup, test_teardown},
	{"EXTEND8: stat object during extend and kill",
	 dfs_extend_stat_kill, rebuild_sub_3nodes_rf0_setup, test_teardown},
	{"EXTEND9: stat object during extend and extend",
	 dfs_extend_stat_extend, rebuild_sub_3nodes_rf0_setup, test_teardown},
	{"EXTEND10: enumerate object during extend and kill",
	 dfs_extend_enumerate_kill, rebuild_sub_3nodes_rf0_setup, test_teardown},
	{"EXTEND11: enumerate object during extend and extend",
	 dfs_extend_enumerate_extend, rebuild_sub_3nodes_rf0_setup, test_teardown},
	{"EXTEND12: read object during extend and kill",
	 dfs_extend_fetch_kill, rebuild_sub_3nodes_rf0_setup, test_teardown},
	{"EXTEND13: read object during extend and extend",
	 dfs_extend_fetch_extend, rebuild_sub_3nodes_rf0_setup, test_teardown},
	{"EXTEND14: write object during extend and kill",
	 dfs_extend_write_kill, rebuild_sub_3nodes_rf0_setup, test_teardown},
	{"EXTEND15: write object during extend and extend",
	 dfs_extend_write_extend, rebuild_sub_3nodes_rf0_setup, test_teardown},
	{"EXTEND16: extend fail then retry",
	 dfs_extend_fail_retry, rebuild_sub_3nodes_rf0_setup, test_teardown},
};

int
run_daos_extend_simple_test(int rank, int size, int *sub_tests,
			    int sub_tests_size)
{
	int rc = 0;

	par_barrier(PAR_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(extend_tests);
		sub_tests = NULL;
	}

	run_daos_sub_tests_only("DAOS_Extend_Simple", extend_tests,
				ARRAY_SIZE(extend_tests), sub_tests,
				sub_tests_size);

	par_barrier(PAR_COMM_WORLD);

	return rc;
}
