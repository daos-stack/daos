/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is for common functions used between daos_drain_simple.c and
 * daos_rebuild_interactive.c tests.
 *
 * tests/suite/daos_drain_common.c
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
const char *extend_drain_opstrs[] = {
	"EXTEND_DRAIN_PUNCH",
	"EXTEND_DRAIN_STAT",
	"EXTEND_DRAIN_ENUMERATE",
	"EXTEND_DRAIN_FETCH",
	"EXTEND_DRAIN_UPDATE",
	"EXTEND_DRAIN_OVERWRITE",
	"EXTEND_DRAIN_WRITELOOP"
};
/* clang-format on */

void
extend_drain_read_check(dfs_t *dfs_mt, dfs_obj_t *dir, uint32_t objclass, uint32_t objcnt,
			daos_size_t total_size, char start_char)
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
	assert_non_null(buf);
	assert_non_null(verify_buf);
	d_iov_set(&iov, buf, buf_size);
	d_iov_set(&verify_iov, buf, buf_size);
	sgl.sg_nr   = 1;
	sgl.sg_iovs = &iov;

	for (i = 0; i < objcnt; i++) {
		char        filename[32];
		daos_size_t read_size = buf_size;
		dfs_obj_t  *obj;
		daos_off_t  offset = 0;
		daos_size_t total  = total_size;
		int         rc;

		sprintf(filename, "file%d", i);
		rc = dfs_open(dfs_mt, dir, filename, S_IFREG | S_IWUSR | S_IRUSR, O_RDWR, objclass,
			      1048576, NULL, &obj);
		assert_int_equal(rc, 0);

		memset(verify_buf, start_char + i, buf_size);

		while (total > 0) {
			memset(buf, 0, buf_size);
			rc = dfs_read(dfs_mt, obj, &sgl, offset, &read_size, NULL);
			assert_int_equal(rc, 0);
			assert_memory_equal(buf, verify_buf, read_size);
			offset += read_size;
			total -= read_size;
		}

		rc = dfs_release(obj);
		assert_int_equal(rc, 0);
	}
	free(buf);
	free(verify_buf);
}

void
extend_drain_write(dfs_t *dfs_mt, dfs_obj_t *dir, uint32_t objclass, uint32_t objcnt,
		   daos_size_t total_size, char write_char, daos_obj_id_t *oids)
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

	for (i = 0; i < objcnt; i++) {
		char        filename[32];
		dfs_obj_t  *obj;
		daos_size_t total  = total_size;
		daos_off_t  offset = 0;
		int         rc;

		sprintf(filename, "file%d", i);
		rc = dfs_open(dfs_mt, dir, filename, S_IFREG | S_IWUSR | S_IRUSR, O_RDWR | O_CREAT,
			      OC_EC_2P1GX, 1048576, NULL, &obj);
		assert_int_equal(rc, 0);
		if (oids != NULL)
			dfs_obj2id(obj, &oids[i]);

		memset(buf, write_char + i, buf_size);
		while (total > 0) {
			rc = dfs_write(dfs_mt, obj, &sgl, offset, NULL);
			assert_int_equal(rc, 0);
			offset += buf_size;
			total -= buf_size;
		}
		rc = dfs_release(obj);
		assert_int_equal(rc, 0);
	}
	free(buf);
}

void
extend_drain_check(dfs_t *dfs_mt, dfs_obj_t *dir, int objclass, int opc)
{
	switch (opc) {
	case EXTEND_DRAIN_PUNCH:
		break;
	case EXTEND_DRAIN_OVERWRITE:
		extend_drain_read_check(dfs_mt, dir, objclass, EXTEND_DRAIN_OBJ_NR, WRITE_SIZE,
					'b');
		break;
	case EXTEND_DRAIN_WRITELOOP:
		extend_drain_read_check(dfs_mt, dir, objclass, 1, 512 * 1048576, 'a');
		break;
	default:
		extend_drain_read_check(dfs_mt, dir, objclass, EXTEND_DRAIN_OBJ_NR, WRITE_SIZE,
					'a');
		break;
	}
}

void
dfs_extend_drain_common(void **state, int opc, uint32_t objclass,
			test_rebuild_cb_t extend_drain_cb_fn)
{
	test_arg_t                *arg = *state;
	dfs_t                     *dfs_mt;
	daos_handle_t              co_hdl;
	dfs_obj_t                 *dir;
	uuid_t                     co_uuid;
	char                       str[37];
	daos_obj_id_t              oids[EXTEND_DRAIN_OBJ_NR];
	struct extend_drain_cb_arg cb_arg;
	dfs_attr_t                 attr = {};
	int                        rc;

	FAULT_INJECTION_REQUIRED();

	if (!test_runable(arg, 4))
		return;

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

	rc = dfs_open(dfs_mt, NULL, "dir", S_IFDIR | S_IWUSR | S_IRUSR, O_RDWR | O_CREAT, objclass,
		      0, NULL, &dir);
	assert_int_equal(rc, 0);

	/* Create 10 files */
	if (opc != EXTEND_DRAIN_UPDATE)
		extend_drain_write(dfs_mt, dir, objclass, EXTEND_DRAIN_OBJ_NR, WRITE_SIZE, 'a',
				   oids);

	cb_arg.oids         = oids;
	cb_arg.dfs_mt       = dfs_mt;
	cb_arg.dir          = dir;
	cb_arg.opc          = opc;
	cb_arg.objclass     = objclass;
	arg->rebuild_cb     = extend_drain_cb_fn;
	arg->rebuild_cb_arg = &cb_arg;

	/* HOLD rebuild ULT */
	daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
			      DAOS_REBUILD_TGT_SCAN_HANG | DAOS_FAIL_ALWAYS, 0, NULL);
	drain_single_pool_rank(arg, ranks_to_kill[0], false);

	extend_drain_check(dfs_mt, dir, objclass, opc);

	/* Unclear if kill engine is necessary for a drain / reintegrate test.
	 * Consider instead test_rebuild_wait() and reintegrate_single_pool_rank(restart=false).
	 */
	daos_kill_server(arg, arg->pool.pool_uuid, arg->group, arg->pool.alive_svc,
			 ranks_to_kill[0]);

	arg->rebuild_cb     = NULL;
	arg->rebuild_cb_arg = NULL;
	reintegrate_single_pool_rank(arg, ranks_to_kill[0], true);

	extend_drain_check(dfs_mt, dir, objclass, opc);

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
