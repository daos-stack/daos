/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_srv/dlck.h>

#include <vos_internal.h>

#include "dlck_ut.h"

static const char Po_uuid_str[] = PO_UUID_STR;
static const char Co_uuid_str[] = "0faccb2b-d498-49d4-aeef-0668e929e919";

/**
 * The smaller tree order allows the creation of more complex tree structures using a smaller number
 * of records.
 */
#define VOS_CONT_ORDER_ALT BTR_ORDER_MIN

static const char co_uuid_str_tmpl[] = "a367beed-8857-461c-a532-92ca618e589_";

struct cont_df_args {
	struct vos_cont_df *ca_cont_df;
	struct vos_pool    *ca_pool;
};

static void
setup(daos_handle_t *poh)
{
	static struct vos_pool pool           = {0};
	static umem_ops_t      umm_ops        = {0};
	static struct btr_root pool_cont_root = {0};
	struct umem_attr       uma            = {.uma_id = UMEM_CLASS_VMEM, .uma_pool = NULL};
	uuid_t                 co_uuid;
	int                    rc;

	/** register required btree classes */
	rc = vos_cont_tab_register();
	assert_int_equal(rc, 0);
	rc = vos_obj_tab_register();
	assert_int_equal(rc, 0);

	pool.vp_umm.umm_ops = &umm_ops;
	*poh                = vos_pool2hdl(&pool);

	/**
	 * this ought to mimic how an actual container table is created when a VOS pool is created
	 */
	rc = dbtree_create_inplace(VOS_BTR_CONT_TABLE, 0, VOS_CONT_ORDER_ALT, &uma, &pool_cont_root,
				   &pool.vp_cont_th);
	assert_int_equal(rc, 0);

	/** populate the container table */
	char co_uuid_str[UUID_STR_LEN];
	for (int i = 0; i < 16; ++i) {
		snprintf(co_uuid_str, UUID_STR_LEN, "%.35s%x", co_uuid_str_tmpl, i);
		rc = uuid_parse(co_uuid_str, co_uuid);
		assert_int_equal(rc, 0);
		rc = vos_cont_create(*poh, co_uuid);
		assert_int_equal(rc, 0);
	}

	/**
	 * XXX should be used to validate the containers' tree after the tree is validated and
	 * fixed.
	 */
	daos_handle_t       ih;
	d_iov_t             val;
	struct cont_df_args args;
	char                uuid_str[UUID_STR_LEN];
	rc = dbtree_iter_prepare(pool.vp_cont_th, 0, &ih);
	assert_int_equal(rc, 0);
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, DAOS_INTENT_DEFAULT, NULL, NULL);
	assert_int_equal(rc, 0);
	val.iov_buf = &args;
	do {
		rc = dbtree_iter_fetch(ih, NULL, &val, NULL);
		assert_int_equal(rc, 0);
		uuid_unparse(args.ca_cont_df->cd_id, uuid_str);
		printf("%s\n", uuid_str);
		rc = dbtree_iter_next(ih);
	} while (rc == 0);
	rc = dbtree_iter_finish(ih);
	assert_int_equal(rc, 0);
}

// static bool
// ut_check_offset(umem_off_t off)
// {
// 	return true;
// }

struct DLCK_callbacks callbacks = {.dc_ask_yes_no = NULL};

int
main(int argc, char **argv)
{
	uuid_t        po_uuid;
	daos_handle_t poh;
	uuid_t        co_uuid;
	daos_handle_t coh;
	int           rc;

	DLCK_Callbacks = &callbacks;

	d_register_alt_assert(mock_assert);

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc) {
		print_error("Error initializing debug system\n");
		return rc;
	}

	rc = vos_standalone_tls_init(DAOS_TGT_TAG);
	assert_int_equal(rc, 0);

	rc = vos_self_init(VOS_PATH, true, BIO_STANDALONE_TGT_ID);
	if (rc) {
		print_error("Error initializing VOS instance\n");
		goto exit_0;
	}

	rc = uuid_parse(Po_uuid_str, po_uuid);
	assert_int_equal(rc, 0);
	rc = uuid_parse(Co_uuid_str, co_uuid);
	assert_int_equal(rc, 0);

	rc = vos_pool_open(POOL_PATH, po_uuid, 0, &poh);
	assert_int_equal(rc, 0);

	rc = vos_cont_open(poh, co_uuid, &coh);
	assert_int_equal(rc, 0);

	// setup(&poh);
	(void)setup;

	// rc = dlck_vos_cont_dtx_recover(coh);
	// assert_int_equal(rc, 0);

	rc = vos_cont_close(coh);
	assert_int_equal(rc, 0);

	rc = vos_pool_close(poh);
	assert_int_equal(rc, 0);

	vos_self_fini();

exit_0:
	daos_debug_fini();

	return 0;
}