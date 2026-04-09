/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is for common functions used between daos_extend_simple.c and
 * daos_rebuild_interactive.c tests.
 *
 * tests/suite/daos_extend_common.c
 *
 */
#define D_LOGFAC DD_FAC(tests)

#include "daos_test.h"
#include "daos_iotest.h"
#include "dfs_test.h"
#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/container.h>

/* clang-format off */
const char *extend_opstrs[] = {
	"EXTEND_PUNCH",
	"EXTEND_STAT",
	"EXTEND_ENUMERATE",
	"EXTEND_FETCH",
	"EXTEND_UPDATE"
};
/* clang-format on */

void
extend_read_check(dfs_t *dfs_mt, dfs_obj_t *dir)
{
	char       *buf        = NULL;
	char       *verify_buf = NULL;
	daos_size_t buf_size   = 512 * 1024;
	d_sg_list_t sgl;
	d_iov_t     iov;
	d_iov_t     verify_iov;
	int         i;

	buf        = malloc(buf_size);
	verify_buf = malloc(buf_size);
	print_message("%s(): allocations buf_size=" DF_U64 ", buf=%p, verify_buf=%p\n",
		      __FUNCTION__, buf_size, buf, verify_buf);
	assert_non_null(buf);
	assert_non_null(verify_buf);
	d_iov_set(&iov, buf, buf_size);
	d_iov_set(&verify_iov, buf, buf_size);
	sgl.sg_nr   = 1;
	sgl.sg_iovs = &iov;

	for (i = 0; i < 20; i++) {
		char        filename[32];
		daos_size_t read_size = buf_size;
		dfs_obj_t  *obj;
		int         rc;

		sprintf(filename, "file%d", i);
		rc = dfs_open(dfs_mt, dir, filename, S_IFREG | S_IWUSR | S_IRUSR, O_RDWR,
			      OC_EC_2P1GX, 1048576, NULL, &obj);
		print_message("%s(): dfs_open(filename=%s) rc=%d\n", __FUNCTION__, filename, rc);
		assert_int_equal(rc, 0);

		memset(verify_buf, 'a' + i, buf_size);
		rc = dfs_read(dfs_mt, obj, &sgl, 0, &read_size, NULL);
		print_message("%s(): dfs_read() read_size=" DF_U64 ", rc=%d\n", __FUNCTION__,
			      read_size, rc);
		assert_int_equal(rc, 0);
		assert_int_equal((int)read_size, buf_size);
		assert_memory_equal(buf, verify_buf, read_size);
		rc = dfs_release(obj);
		print_message("%s(): dfs_release() rc=%d\n", __FUNCTION__, rc);
		assert_int_equal(rc, 0);
	}
	free(buf);
	free(verify_buf);
	print_message("%s(): done, freed buf and verify_buf\n", __FUNCTION__);
}

void
extend_write(dfs_t *dfs_mt, dfs_obj_t *dir)
{
	char       *buf      = NULL;
	daos_size_t buf_size = 512 * 1024;
	d_sg_list_t sgl;
	d_iov_t     iov;
	int         i;

	buf = malloc(buf_size);
	assert_non_null(buf);
	d_iov_set(&iov, buf, buf_size);
	sgl.sg_nr   = 1;
	sgl.sg_iovs = &iov;

	for (i = 0; i < 20; i++) {
		char       filename[32];
		dfs_obj_t *obj;
		int        rc;

		sprintf(filename, "file%d", i);
		rc = dfs_open(dfs_mt, dir, filename, S_IFREG | S_IWUSR | S_IRUSR, O_RDWR | O_CREAT,
			      OC_EC_2P1GX, 1048576, NULL, &obj);
		assert_int_equal(rc, 0);

		memset(buf, 'a' + i, buf_size);
		rc = dfs_write(dfs_mt, obj, &sgl, 0, NULL);
		assert_int_equal(rc, 0);
		rc = dfs_release(obj);
		assert_int_equal(rc, 0);
	}
	free(buf);
}

void
dfs_extend_internal(void **state, int opc, test_rebuild_cb_t extend_cb, bool kill)
{
	test_arg_t          *arg = *state;
	dfs_t               *dfs_mt;
	daos_handle_t        co_hdl;
	dfs_obj_t           *obj;
	dfs_obj_t           *dir;
	uuid_t               co_uuid;
	int                  i;
	d_rank_t             extend_rank = 3;
	char                 str[37];
	daos_obj_id_t        oids[EXTEND_OBJ_NR];
	struct extend_cb_arg cb_arg;
	dfs_attr_t           attr = {};
	int                  rc;

	attr.da_props = daos_prop_alloc(2);
	assert_non_null(attr.da_props);
	attr.da_props->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_LVL;
	attr.da_props->dpp_entries[0].dpe_val  = DAOS_PROP_CO_REDUN_RANK;
	attr.da_props->dpp_entries[1].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	attr.da_props->dpp_entries[1].dpe_val  = DAOS_PROP_CO_REDUN_RF1;
	rc = dfs_cont_create(arg->pool.poh, &co_uuid, &attr, &co_hdl, &dfs_mt);
	daos_prop_free(attr.da_props);
	assert_int_equal(rc, 0);
	print_message("Created DFS Container " DF_UUIDF "\n", DP_UUID(co_uuid));

	rc = dfs_open(dfs_mt, NULL, "dir", S_IFDIR | S_IWUSR | S_IRUSR, O_RDWR | O_CREAT,
		      OC_EC_2P1GX, 0, NULL, &dir);
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

	cb_arg.oids   = oids;
	cb_arg.dfs_mt = dfs_mt;
	cb_arg.dir    = dir;
	cb_arg.opc    = opc;
	cb_arg.kill   = kill;
	if (kill)
		cb_arg.rank = 2;
	else
		cb_arg.rank = 4;

	arg->rebuild_cb     = extend_cb;
	arg->rebuild_cb_arg = &cb_arg;

	/* HOLD rebuild ULT. FIXME: maybe change to use test_set_engine_fail_loc()? */
	print_message("inject DAOS_REBUILD_TGT_SCAN_HANG fault on engines\n");
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			      DAOS_REBUILD_TGT_SCAN_HANG | DAOS_FAIL_ALWAYS, 0, NULL);

	arg->no_rebuild =
	    1; /* This has no effect for RB_OP_TYPE_ADD - so can this be removed here? */
	extend_single_pool_rank(arg, extend_rank);
	arg->no_rebuild = 0;

	print_message("sleep 30 secs for rank %u %s\n", cb_arg.rank,
		      cb_arg.kill ? "kill/exclude" : "extend");
	sleep(30);
	print_message("wait for rebuild due to rank %u extend and rank %u %s\n", extend_rank,
		      cb_arg.rank, cb_arg.kill ? "kill/exclude" : "extend");
	test_rebuild_wait(&arg, 1);

	if (opc == EXTEND_UPDATE) {
		print_message("First extend update read check\n");
		extend_read_check(dfs_mt, dir);
	}

	arg->rebuild_cb     = NULL;
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
