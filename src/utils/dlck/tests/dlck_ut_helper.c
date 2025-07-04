/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(telem)

#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <cmocka.h>
#include <getopt.h>
#include <sys/stat.h>
#include <daos/dtx.h>
#include <daos_srv/vos.h>
#include <daos_srv/dtx_srv.h>

#include "dlck_ut.h"

struct vos_test_ctx {
	uuid_t        tc_po_uuid;
	uuid_t        tc_co_uuid;
	daos_handle_t tc_po_hdl;
	daos_handle_t tc_co_hdl;
	int           tc_step;
};

#define VPOOL_SIZE (1024 * 1024 * 10) /** 10MiB */

static const char Po_uuid_str[]   = PO_UUID_STR;
static const char Co_uuid_str[]   = "0faccb2b-d498-49d4-aeef-0668e929e919";
static const char Dti1_uuid_str[] = "0faccb2b-d498-49d4-aeee-0668e929e000";
static const char Dti2_uuid_str[] = "525c6a15-8bc9-4918-a8fa-98b959ce6575";

static void
test_cleanup()
{
	int rc;

	rc = unlink(POOL_PATH);
	assert_true(rc == 0 || (rc == -1 && errno == ENOENT));
	rc = rmdir(VOS_PATH "/" PO_UUID_STR);
	assert_true(rc == 0 || (rc == -1 && errno == ENOENT));
	rc = unlink(VOS_PATH "/daos_sys/sys_db");
	assert_true(rc == 0 || (rc == -1 && errno == ENOENT));
	rc = rmdir(VOS_PATH "/daos_sys");
	assert_true(rc == 0 || (rc == -1 && errno == ENOENT));
}

static void
test_setup(struct vos_test_ctx *tcx)
{
	daos_size_t psize     = VPOOL_SIZE;
	daos_size_t meta_size = 0;
	int         rc;

	rc = uuid_parse(Po_uuid_str, tcx->tc_po_uuid);
	assert_int_equal(rc, 0);
	rc = uuid_parse(Co_uuid_str, tcx->tc_co_uuid);
	assert_int_equal(rc, 0);

	rc = mkdir(VOS_PATH "/" PO_UUID_STR, 0777);
	assert_int_equal(rc, 0);

	rc = vos_pool_create(POOL_PATH, tcx->tc_po_uuid, psize, psize, meta_size, 0 /* flags */,
			     0 /* version */, &tcx->tc_po_hdl);
	assert_int_equal(rc, 0);

	rc = vos_cont_create(tcx->tc_po_hdl, tcx->tc_co_uuid);
	assert_int_equal(rc, 0);

	rc = vos_cont_open(tcx->tc_po_hdl, tcx->tc_co_uuid, &tcx->tc_co_hdl);
	assert_int_equal(rc, 0);
}

static void
test_teardown(struct vos_test_ctx *tcx)
{
	int rc;

	rc = vos_cont_close(tcx->tc_co_hdl);
	assert_int_equal(rc, 0);

	rc = vos_pool_close(tcx->tc_po_hdl);
	assert_int_equal(rc, 0);
}

static void
simple_dtx(daos_handle_t coh, const char *dti_uuid_str, daos_unit_oid_t oid, daos_key_t *dkey,
	   daos_iod_t *iod, d_sg_list_t *sgl)
{
	struct dtx_id             dti        = {0};
	struct dtx_epoch          epoch      = {0};
	daos_unit_oid_t           leader_oid = {0};
	// uint32_t           flags      = 0;
	struct dtx_leader_handle *dlh;
	struct dtx_handle        *dth;
	int                       rc;

	rc = uuid_parse(dti_uuid_str, dti.dti_uuid);
	assert_int_equal(rc, 0);
	dti.dti_hlc = d_hlc_get();

	epoch.oe_value = d_hlc_get();

	// rc = dtx_begin(coh, &dti, &epoch, 1, 0, &leader_oid, NULL, 0, flags, NULL, &dth);
	// assert_int_equal(rc, 0);

	rc = dtx_leader_begin(coh, &dti, &epoch, 1, 0, &leader_oid, NULL, 0, NULL, 0, 0, NULL, NULL,
			      &dlh);
	assert_int_equal(rc, 0);

	dth = &dlh->dlh_handle;

	rc = dtx_sub_init(dth, &oid, 0);
	assert_int_equal(rc, 0);

	rc = vos_obj_update_ex(coh, oid, 0, 0, 0, dkey, 1, iod, NULL, sgl, dth);
	assert_int_equal(rc, 0);

	rc = dtx_end(dth, NULL, DER_SUCCESS);
	assert_int_equal(rc, 0);

	printf("[DTX %s] Commit? (y or [n]): ", dti_uuid_str);
	char op = getchar();
	if (op == 'y') {
		rc = vos_dtx_commit(coh, &dti, 1, true, NULL);
		assert_int_equal(rc, 1); /** total number of committed */
	}
	if (op != '\n') {
		(void)getchar(); /** consume the leftover \n */
	}
}

static int
iter_cb_printf(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
	       vos_iter_param_t *param, void *cb_arg, unsigned int *acts)
{
	assert_int_equal(type, VOS_ITER_OBJ);

	printf("- oid=(lo=%" PRIu64 ", hi=%" PRIu64 ")\n", entry->ie_oid.id_pub.lo,
	       entry->ie_oid.id_pub.hi);

	return 0;
}

static void
run_all_tests(void)
{
	daos_unit_oid_t     oid      = {0};
	uint64_t            dkey_buf = 1;
	daos_key_t          dkey;
	uint64_t            akey_buf = 2;
	daos_key_t          akey;
	daos_iod_t          iod   = {0};
	char               *value = "Aloha";
	d_sg_list_t         sgl;
	int                 rc;

	struct vos_test_ctx tcx;

	/** prepare a container */
	test_setup(&tcx);

	/** prepare DKEY, AKEY, IOD and SGL */
	d_iov_set(&dkey, (void *)&dkey_buf, sizeof(dkey_buf));
	d_iov_set(&akey, (void *)&akey_buf, sizeof(akey_buf));
	iod.iod_name  = akey;
	iod.iod_type  = DAOS_IOD_SINGLE;
	iod.iod_recxs = NULL;
	iod.iod_nr    = 1;
	iod.iod_size  = strlen(value);
	rc            = d_sgl_init(&sgl, 1);
	assert_int_equal(rc, 0);
	d_iov_set(&sgl.sg_iovs[0], (void *)value, iod.iod_size);

	/** 1st DTX */
	simple_dtx(tcx.tc_co_hdl, Dti1_uuid_str, oid, &dkey, &iod, &sgl);

	/** 2nd DTX (just a different OID) */
	oid.id_pub.lo = 1;
	simple_dtx(tcx.tc_co_hdl, Dti2_uuid_str, oid, &dkey, &iod, &sgl);

	/** iterate over OBJs */
	vos_iter_param_t        param   = {0};
	struct vos_iter_anchors anchors = {0};

	param.ip_hdl        = tcx.tc_co_hdl;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;

	printf("vos_iterate()...\n");
	rc = vos_iterate(&param, VOS_ITER_OBJ, false, &anchors, iter_cb_printf, NULL, NULL, NULL);
	printf("vos_iterate() -> rc = %d\n", rc);

	/** quick teardown */
	d_sgl_fini(&sgl, false);
	test_teardown(&tcx);
}

extern struct dss_module dtx_module;

int
main(int argc, char **argv)
{
	int rc;

	d_register_alt_assert(mock_assert);

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc) {
		print_error("Error initializing debug system\n");
		return rc;
	}

	test_cleanup();

	rc = vos_self_init(VOS_PATH, true, BIO_STANDALONE_TGT_ID);
	if (rc) {
		print_error("Error initializing VOS instance\n");
		goto exit_0;
	}

	daos_register_key(dtx_module.sm_key);

	(void)dss_tls_init(DAOS_TGT_TAG, 0, BIO_STANDALONE_TGT_ID);

	run_all_tests();

	vos_self_fini();

exit_0:
	daos_debug_fini();

	return 0;
}
