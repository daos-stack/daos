/**
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of vos/tests/
 *
 * vos/tests/vts_dtx.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include <daos/common.h>
#include <daos_srv/dtx_srv.h>
#include <daos_srv/vos_types.h>
#include "vts_io.h"

static void
vts_init_dte(struct dtx_entry *dte)
{
	struct dtx_memberships	*mbs;
	size_t			 size;

	size = sizeof(struct dtx_memberships) + sizeof(struct dtx_daos_target);

	D_ALLOC(mbs, size);
	assert_non_null(mbs);

	mbs->dm_tgt_cnt = 1;
	mbs->dm_grp_cnt = 1;
	mbs->dm_data_size = sizeof(struct dtx_daos_target);
	mbs->dm_tgts[0].ddt_id = 1;

	/** Use unique API so new UUID is generated even on same thread */
	daos_dti_gen_unique(&dte->dte_xid);
	dte->dte_ver = 1;
	dte->dte_refs = 1;
	dte->dte_mbs = mbs;
}

void
vts_dtx_begin(const daos_unit_oid_t *oid, daos_handle_t coh, daos_epoch_t epoch,
	      uint64_t dkey_hash, struct dtx_handle **dthp)
{
	struct dtx_handle	*dth;

	D_ALLOC_PTR(dth);
	assert_non_null(dth);

	vts_init_dte(&dth->dth_dte);

	dth->dth_coh = coh;
	dth->dth_epoch = epoch;
	dth->dth_leader_oid = *oid;

	dth->dth_pinned = 0;
	dth->dth_sync = 0;
	dth->dth_cos_done = 0;
	dth->dth_resent = 0;
	dth->dth_touched_leader_oid = 0;
	dth->dth_local_tx_started = 0;
	dth->dth_solo = 0;
	dth->dth_drop_cmt = 0;
	dth->dth_modify_shared = 0;
	dth->dth_active = 0;
	dth->dth_dist = 0;
	dth->dth_for_migration = 0;
	dth->dth_ignore_uncommitted = 0;
	dth->dth_prepared = 0;
	dth->dth_aborted = 0;
	dth->dth_already = 0;
	dth->dth_need_validation = 0;

	dth->dth_dti_cos_count = 0;
	dth->dth_dti_cos = NULL;
	dth->dth_ent = NULL;
	dth->dth_flags = DTE_LEADER;
	dth->dth_modification_cnt = 1;

	dth->dth_op_seq = 1;
	dth->dth_oid_cnt = 0;
	dth->dth_oid_cap = 0;
	dth->dth_oid_array = NULL;

	dth->dth_dkey_hash = dkey_hash;

	D_INIT_LIST_HEAD(&dth->dth_share_cmt_list);
	D_INIT_LIST_HEAD(&dth->dth_share_abt_list);
	D_INIT_LIST_HEAD(&dth->dth_share_act_list);
	D_INIT_LIST_HEAD(&dth->dth_share_tbd_list);
	dth->dth_share_tbd_count = 0;
	dth->dth_shares_inited = 1;

	vos_dtx_rsrvd_init(dth);
	vos_dtx_attach(dth, false, false);

	*dthp = dth;
}

void
vts_dtx_end(struct dtx_handle *dth)
{
	struct dtx_share_peer	*dsp;

	if (dth->dth_shares_inited) {
		while ((dsp = d_list_pop_entry(&dth->dth_share_cmt_list,
					       struct dtx_share_peer,
					       dsp_link)) != NULL)
			dtx_dsp_free(dsp);

		while ((dsp = d_list_pop_entry(&dth->dth_share_abt_list,
					       struct dtx_share_peer,
					       dsp_link)) != NULL)
			dtx_dsp_free(dsp);

		while ((dsp = d_list_pop_entry(&dth->dth_share_act_list,
					       struct dtx_share_peer,
					       dsp_link)) != NULL)
			dtx_dsp_free(dsp);

		while ((dsp = d_list_pop_entry(&dth->dth_share_tbd_list,
					       struct dtx_share_peer,
					       dsp_link)) != NULL)
			dtx_dsp_free(dsp);

		dth->dth_share_tbd_count = 0;
	}

	vos_dtx_rsrvd_fini(dth);
	vos_dtx_detach(dth);
	D_FREE(dth->dth_dte.dte_mbs);
	D_FREE(dth);
}

static void
vts_dtx_prep_update(struct io_test_args *args,
		    d_iov_t *val_iov, d_iov_t *dkey_iov,
		    daos_key_t *dkey, char *dkey_buf,
		    daos_key_t *akey, char *akey_buf,
		    daos_iod_t *iod, d_sg_list_t *sgl, daos_recx_t *rex,
		    void *update_buf, int buf_size, int rec_size,
		    uint64_t *dkey_hash, uint64_t *epoch, bool ext)
{
	memset(iod, 0, sizeof(*iod));
	memset(sgl, 0, sizeof(*sgl));
	memset(rex, 0, sizeof(*rex));

	args->ta_flags = TF_ZERO_COPY;
	args->otype = DAOS_OT_MULTI_UINT64;

	*epoch = d_hlc_get();

	vts_key_gen(dkey_buf, args->dkey_size, true, args);
	set_iov(dkey, dkey_buf, is_daos_obj_type_set(args->otype, DAOS_OT_DKEY_UINT64));
	*dkey_hash = d_hash_murmur64((const unsigned char *)dkey_buf,
				      args->dkey_size, 5731);

	dts_buf_render(update_buf, buf_size);
	d_iov_set(val_iov, update_buf, buf_size);

	sgl->sg_iovs = val_iov;
	sgl->sg_nr = 1;

	d_iov_set(dkey_iov, dkey_buf, args->dkey_size);
	rex->rx_idx = hash_key(dkey_iov, is_daos_obj_type_set(args->otype, DAOS_OT_DKEY_UINT64));

	vts_key_gen(akey_buf, args->akey_size, false, args);
	set_iov(akey, akey_buf, is_daos_obj_type_set(args->otype, DAOS_OT_AKEY_UINT64));

	iod->iod_name = *akey;
	if (ext) {
		rex->rx_nr = buf_size / rec_size;
		iod->iod_type = DAOS_IOD_ARRAY;
		iod->iod_size = rec_size;
	} else {
		rex->rx_nr = 1;
		iod->iod_type = DAOS_IOD_SINGLE;
		iod->iod_size = buf_size;
	}
	iod->iod_recxs = rex;
	iod->iod_nr = 1;
}

static void
vts_dtx_commit_visibility(struct io_test_args *args, bool ext, bool punch_obj)
{
	struct dtx_handle		*dth = NULL;
	struct dtx_id			 xid;
	daos_iod_t			 iod = { 0 };
	d_sg_list_t			 sgl = { 0 };
	daos_recx_t			 rex = { 0 };
	daos_key_t			 dkey;
	daos_key_t			 akey;
	d_iov_t				 val_iov;
	d_iov_t				 dkey_iov;
	uint64_t			 epoch;
	uint64_t			 dkey_hash;
	char				 dkey_buf[UPDATE_DKEY_SIZE];
	char				 akey_buf[UPDATE_AKEY_SIZE];
	char				 update_buf[UPDATE_BUF_SIZE];
	char				 fetch_buf[UPDATE_BUF_SIZE];
	int				 rc;

	vts_dtx_prep_update(args, &val_iov, &dkey_iov, &dkey, dkey_buf,
			    &akey, akey_buf, &iod, &sgl, &rex, update_buf,
			    UPDATE_BUF_SIZE, UPDATE_REC_SIZE, &dkey_hash,
			    &epoch, ext);

	/* Assume I am the leader. */
	vts_dtx_begin(&args->oid, args->ctx.tc_co_hdl, epoch, dkey_hash, &dth);

	rc = io_test_obj_update(args, epoch, 0, &dkey, &iod, &sgl, dth, true);
	assert_rc_equal(rc, 0);

	xid = dth->dth_xid;

	/* The update DTX is 'prepared'. */
	vts_dtx_end(dth);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, epoch, 0, &dkey, &iod, &sgl, true);
	assert_rc_equal(rc, 0);

	/* Data record with update DTX is invisible before commit. */
	assert_memory_not_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

	/* Commit the update DTX. */
	rc = vos_dtx_commit(args->ctx.tc_co_hdl, &xid, 1, NULL);
	assert_rc_equal(rc, 1);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	/* Fetch again. */
	rc = io_test_obj_fetch(args, epoch, 0, &dkey, &iod, &sgl, true);
	assert_rc_equal(rc, 0);

	/* Data record with update DTX is readable after commit. */
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

	/* Generate the punch DTX. */
	vts_dtx_begin(&args->oid, args->ctx.tc_co_hdl, ++epoch, dkey_hash,
		      &dth);

	if (punch_obj)
		rc = vos_obj_punch(args->ctx.tc_co_hdl, args->oid, epoch,
				   1, 0, NULL, 0, NULL, dth);
	else
		rc = vos_obj_punch(args->ctx.tc_co_hdl, args->oid, epoch,
				   1, 0, &dkey, 1, &akey, dth);
	assert_rc_equal(rc, 0);

	xid = dth->dth_xid;

	/* The punch DTX is 'prepared'. */
	vts_dtx_end(dth);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, ++epoch, 0, &dkey, &iod, &sgl, true);
	/* Punch is not yet visible */
	assert_rc_equal(rc, 0);

	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

	/* Commit the punch DTX. */
	rc = vos_dtx_commit(args->ctx.tc_co_hdl, &xid, 1, NULL);
	assert_rc_equal(rc, 1);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	/* Fetch again. */
	rc = io_test_obj_fetch(args, ++epoch, 0, &dkey, &iod, &sgl, true);
	assert_rc_equal(rc, 0);

	/* Data record with punch DTX is invisible after commit. */
	assert_memory_not_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);
}

/* DTX commit and related data record visibility (single value, punch key) */
static void
dtx_6(void **state)
{
	vts_dtx_commit_visibility(*state, false, false);
}

/* DTX commit and related data record visibility (extent value, punch key) */
static void
dtx_7(void **state)
{
	vts_dtx_commit_visibility(*state, true, false);
}

/* DTX commit and related data record visibility (single value, punch obj) */
static void
dtx_8(void **state)
{
	vts_dtx_commit_visibility(*state, false, true);
}

/* DTX commit and related data record visibility (extent value, punch obj) */
static void
dtx_9(void **state)
{
	vts_dtx_commit_visibility(*state, true, true);
}

static void
vts_dtx_abort_visibility(struct io_test_args *args, bool ext, bool punch_obj)
{
	struct dtx_handle		*dth = NULL;
	struct dtx_id			 xid;
	daos_iod_t			 iod = { 0 };
	d_sg_list_t			 sgl = { 0 };
	daos_recx_t			 rex = { 0 };
	daos_key_t			 dkey;
	daos_key_t			 akey;
	d_iov_t				 val_iov;
	d_iov_t				 dkey_iov;
	uint64_t			 epoch;
	uint64_t			 dkey_hash;
	char				 dkey_buf[UPDATE_DKEY_SIZE];
	char				 akey_buf[UPDATE_AKEY_SIZE];
	char				 update_buf1[UPDATE_BUF_SIZE];
	char				 update_buf2[UPDATE_BUF_SIZE];
	char				 fetch_buf[UPDATE_BUF_SIZE];
	int				 rc;

	vts_dtx_prep_update(args, &val_iov, &dkey_iov, &dkey, dkey_buf,
			    &akey, akey_buf, &iod, &sgl, &rex, update_buf1,
			    UPDATE_BUF_SIZE, UPDATE_REC_SIZE, &dkey_hash,
			    &epoch, ext);

	/* initial update. */
	rc = io_test_obj_update(args, epoch, 0, &dkey, &iod, &sgl, dth, true);
	assert_rc_equal(rc, 0);

	dts_buf_render(update_buf2, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, update_buf2, UPDATE_BUF_SIZE);

	/* Assume I am the leader. */
	vts_dtx_begin(&args->oid, args->ctx.tc_co_hdl, ++epoch, dkey_hash,
		      &dth);

	rc = io_test_obj_update(args, epoch, 0, &dkey, &iod, &sgl, dth, true);
	assert_rc_equal(rc, 0);

	xid = dth->dth_xid;

	/* The update DTX is 'prepared'. */
	vts_dtx_end(dth);

	/* Aborted the update DTX. */
	rc = vos_dtx_abort(args->ctx.tc_co_hdl, &xid, epoch);
	assert_rc_equal(rc, 0);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, epoch, 0, &dkey, &iod, &sgl, true);
	assert_rc_equal(rc, 0);

	assert_memory_not_equal(update_buf2, fetch_buf, UPDATE_BUF_SIZE);
	/* The fetched result is the data written via the initial update. */
	assert_memory_equal(update_buf1, fetch_buf, UPDATE_BUF_SIZE);

	/* Generate the punch DTX. */
	vts_dtx_begin(&args->oid, args->ctx.tc_co_hdl, ++epoch, dkey_hash,
		      &dth);

	if (punch_obj)
		rc = vos_obj_punch(args->ctx.tc_co_hdl, args->oid, epoch,
				   1, 0, NULL, 0, NULL, dth);
	else
		rc = vos_obj_punch(args->ctx.tc_co_hdl, args->oid, epoch,
				   1, 0, &dkey, 1, &akey, dth);
	assert_rc_equal(rc, 0);

	xid = dth->dth_xid;

	/* The punch DTX is 'prepared'. */
	vts_dtx_end(dth);

	/* Aborted the punch DTX. */
	rc = vos_dtx_abort(args->ctx.tc_co_hdl, &xid, epoch);
	assert_rc_equal(rc, 0);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, ++epoch, 0, &dkey, &iod, &sgl, true);
	assert_rc_equal(rc, 0);

	/* The fetched result is the data written via the initial update. */
	assert_memory_equal(update_buf1, fetch_buf, UPDATE_BUF_SIZE);
}

/* DTX abort and related data record visibility (single value, punch key) */
static void
dtx_10(void **state)
{
	vts_dtx_abort_visibility(*state, false, false);
}

/* DTX abort and related data record visibility (extent value, punch key) */
static void
dtx_11(void **state)
{
	vts_dtx_abort_visibility(*state, true, false);
}

/* DTX abort and related data record visibility (single value, punch obj) */
static void
dtx_12(void **state)
{
	vts_dtx_abort_visibility(*state, false, true);
}

/* DTX abort and related data record visibility (extent value, punch obj) */
static void
dtx_13(void **state)
{
	vts_dtx_abort_visibility(*state, true, true);
}

/* DTX ops against committed DTX */
static void
dtx_14(void **state)
{
	struct io_test_args		*args = *state;
	struct dtx_handle		*dth = NULL;
	struct dtx_id			 xid;
	daos_iod_t			 iod = { 0 };
	d_sg_list_t			 sgl = { 0 };
	daos_recx_t			 rex = { 0 };
	daos_key_t			 dkey;
	daos_key_t			 akey;
	d_iov_t				 val_iov;
	d_iov_t				 dkey_iov;
	uint64_t			 epoch;
	uint64_t			 dkey_hash;
	char				 dkey_buf[UPDATE_DKEY_SIZE];
	char				 akey_buf[UPDATE_AKEY_SIZE];
	char				 update_buf[UPDATE_BUF_SIZE];
	char				 fetch_buf[UPDATE_BUF_SIZE];
	int				 rc;

	vts_dtx_prep_update(args, &val_iov, &dkey_iov, &dkey, dkey_buf,
			    &akey, akey_buf, &iod, &sgl, &rex, update_buf,
			    UPDATE_BUF_SIZE, UPDATE_REC_SIZE, &dkey_hash,
			    &epoch, false);

	/* Assume I am the leader. */
	vts_dtx_begin(&args->oid, args->ctx.tc_co_hdl, epoch, dkey_hash, &dth);

	rc = io_test_obj_update(args, epoch, 0, &dkey, &iod, &sgl, dth, true);
	assert_rc_equal(rc, 0);

	xid = dth->dth_xid;

	/* The DTX is 'prepared'. */
	vts_dtx_end(dth);

	/* Commit the DTX. */
	rc = vos_dtx_commit(args->ctx.tc_co_hdl, &xid, 1, NULL);
	assert_rc_equal(rc, 1);

	/* Double commit the DTX is harmless. */
	rc = vos_dtx_commit(args->ctx.tc_co_hdl, &xid, 1, NULL);
	assert(rc >= 0);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, epoch, 0, &dkey, &iod, &sgl, true);
	assert_rc_equal(rc, 0);

	/* Data record is not affected by double commit. */
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

	/* Committed DTX cannot be aborted. */
	rc = vos_dtx_abort(args->ctx.tc_co_hdl, &xid, epoch);
	assert_int_not_equal(rc, 0);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, epoch, 0, &dkey, &iod, &sgl, true);
	assert_rc_equal(rc, 0);

	/* Data record is not affected by failed abort. */
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);
}

/* DTX ops against aborted DTX */
static void
dtx_15(void **state)
{
	struct io_test_args		*args = *state;
	struct dtx_handle		*dth = NULL;
	struct dtx_id			 xid;
	daos_iod_t			 iod = { 0 };
	d_sg_list_t			 sgl = { 0 };
	daos_recx_t			 rex = { 0 };
	daos_key_t			 dkey;
	daos_key_t			 akey;
	d_iov_t				 val_iov;
	d_iov_t				 dkey_iov;
	uint64_t			 epoch;
	uint64_t			 dkey_hash;
	char				 dkey_buf[UPDATE_DKEY_SIZE];
	char				 akey_buf[UPDATE_AKEY_SIZE];
	char				 update_buf1[UPDATE_BUF_SIZE];
	char				 update_buf2[UPDATE_BUF_SIZE];
	char				 fetch_buf[UPDATE_BUF_SIZE];
	int				 rc;

	vts_dtx_prep_update(args, &val_iov, &dkey_iov, &dkey, dkey_buf,
			    &akey, akey_buf, &iod, &sgl, &rex, update_buf1,
			    UPDATE_BUF_SIZE, UPDATE_REC_SIZE, &dkey_hash,
			    &epoch, false);

	/* initial update. */
	rc = io_test_obj_update(args, epoch, 0, &dkey, &iod, &sgl, dth, true);
	assert_rc_equal(rc, 0);

	dts_buf_render(update_buf2, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, update_buf2, UPDATE_BUF_SIZE);

	/* Assume I am the leader. */
	vts_dtx_begin(&args->oid, args->ctx.tc_co_hdl, ++epoch, dkey_hash,
		      &dth);

	rc = io_test_obj_update(args, epoch, 0, &dkey, &iod, &sgl, dth, true);
	assert_rc_equal(rc, 0);

	xid = dth->dth_xid;

	/* The update DTX is 'prepared'. */
	vts_dtx_end(dth);

	/* Aborted the update DTX. */
	rc = vos_dtx_abort(args->ctx.tc_co_hdl, &xid, epoch);
	assert_rc_equal(rc, 0);

	/* Double aborted the DTX is harmless. */
	rc = vos_dtx_abort(args->ctx.tc_co_hdl, &xid, epoch);
	assert_int_not_equal(rc, 0);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, epoch, 0, &dkey, &iod, &sgl, true);
	assert_rc_equal(rc, 0);

	assert_memory_not_equal(update_buf2, fetch_buf, UPDATE_BUF_SIZE);
	/* The fetched result is the data written via the initial update. */
	assert_memory_equal(update_buf1, fetch_buf, UPDATE_BUF_SIZE);

	/* Aborted DTX cannot be committed. */
	rc = vos_dtx_commit(args->ctx.tc_co_hdl, &xid, 1, NULL);
	assert(rc >= 0);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, epoch, 0, &dkey, &iod, &sgl, true);
	assert_rc_equal(rc, 0);

	assert_memory_not_equal(update_buf2, fetch_buf, UPDATE_BUF_SIZE);
	/* The fetched result is the data written via the initial update. */
	assert_memory_equal(update_buf1, fetch_buf, UPDATE_BUF_SIZE);
}

/* DTX in CoS cache makes related data record as readable */
static void
dtx_16(void **state)
{
	struct io_test_args		*args = *state;
	struct dtx_handle		*dth = NULL;
	daos_iod_t			 iod = { 0 };
	d_sg_list_t			 sgl = { 0 };
	daos_recx_t			 rex = { 0 };
	daos_handle_t			 ioh;
	daos_key_t			 dkey;
	daos_key_t			 akey;
	d_iov_t				 val_iov;
	d_iov_t				 dkey_iov;
	uint64_t			 epoch;
	uint64_t			 dkey_hash;
	char				 dkey_buf[UPDATE_DKEY_SIZE];
	char				 akey_buf[UPDATE_AKEY_SIZE];
	char				 update_buf[UPDATE_BUF_SIZE];
	char				 fetch_buf[UPDATE_BUF_SIZE];
	int				 rc;

	FAULT_INJECTION_REQUIRED();

	vts_dtx_prep_update(args, &val_iov, &dkey_iov, &dkey, dkey_buf,
			    &akey, akey_buf, &iod, &sgl, &rex, update_buf,
			    UPDATE_BUF_SIZE, UPDATE_REC_SIZE, &dkey_hash,
			    &epoch, false);

	vts_dtx_begin(&args->oid, args->ctx.tc_co_hdl, epoch, dkey_hash, &dth);

	rc = io_test_obj_update(args, epoch, 0, &dkey, &iod, &sgl, dth, true);
	assert_rc_equal(rc, 0);

	daos_fail_loc_set(DAOS_VOS_NON_LEADER | DAOS_FAIL_ALWAYS);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = vos_fetch_begin(args->ctx.tc_co_hdl, args->oid, epoch,
			     &dkey_iov, 1, &iod, 0, NULL, &ioh, NULL);
	/* The former DTX is not committed, so need to retry with leader. */
	assert_rc_equal(rc, -DER_INPROGRESS);

	daos_fail_loc_reset();

	rc = io_test_obj_fetch(args, epoch, 0, &dkey, &iod, &sgl, true);
	assert_rc_equal(rc, 0);

	/* Former DTX is not committed, so nothing can be fetched. */
	assert_memory_not_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

	/* Mark the DTX as committable. */
	vos_dtx_mark_committable(dth);

	/* Fetch again. */
	rc = io_test_obj_fetch(args, epoch, 0, &dkey, &iod, &sgl, true);
	assert_rc_equal(rc, 0);

	/* The DTX in CoS cache will make related data record as readable. */
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

	/* Commit the DTX. */
	rc = vos_dtx_commit(args->ctx.tc_co_hdl, &dth->dth_xid, 1, NULL);
	assert_rc_equal(rc, 1);

	vts_dtx_end(dth);
}

struct vts_dtx_iter_data {
	void	**dkeys;
	bool	 *found;
	int	  count;
};

static int
vts_dtx_iter_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
		vos_iter_param_t *param, void *cb_arg, unsigned int *acts)
{
	struct vts_dtx_iter_data	*vdid = cb_arg;
	int				 rc = -DER_INVAL;
	int				 i;

	D_ASSERT(type == VOS_ITER_DKEY);

	assert_true(entry->ie_key.iov_buf != NULL);
	assert_true(entry->ie_key.iov_len > 0);

	for (i = 0; i < vdid->count; i++) {
		if (memcmp(entry->ie_key.iov_buf, vdid->dkeys[i],
			   entry->ie_key.iov_len) == 0) {
			assert_true(!vdid->found[i]);

			vdid->found[i] = true;
			rc = 0;
			break;
		}
	}

	assert_true(i < vdid->count);
	return rc;
}

/* list dkey with DTX */
static void
dtx_17(void **state)
{
	struct io_test_args		*args = *state;
	struct dtx_id			 xid[10];
	daos_iod_t			 iod = { 0 };
	d_sg_list_t			 sgl = { 0 };
	daos_recx_t			 rex = { 0 };
	daos_key_t			 dkey;
	daos_key_t			 akey;
	d_iov_t				 val_iov;
	uint64_t			 epoch[10];
	char				*dkey_buf[10];
	char				 akey_buf[UPDATE_AKEY_SIZE];
	char				 update_buf[UPDATE_BUF_SIZE];
	vos_iter_param_t		 param = { 0 };
	struct vos_iter_anchors		 anchors = { 0 };
	struct vts_dtx_iter_data	 vdid = { 0 };
	bool				 found[10];
	int				 rc;
	int				 i;

	/* Assume I am the leader. */
	for (i = 0; i < 10; i++) {
		struct dtx_handle		*dth = NULL;
		d_iov_t				 dkey_iov;
		uint64_t			 dkey_hash;

		dkey_buf[i] = malloc(UPDATE_DKEY_SIZE);
		assert_true(dkey_buf[i] != NULL);

		vts_dtx_prep_update(args, &val_iov, &dkey_iov, &dkey,
				    dkey_buf[i], &akey, akey_buf, &iod, &sgl,
				    &rex, update_buf, UPDATE_BUF_SIZE,
				    UPDATE_REC_SIZE, &dkey_hash, &epoch[i],
				    false);

		vts_dtx_begin(&args->oid, args->ctx.tc_co_hdl, epoch[i],
			      dkey_hash, &dth);

		rc = io_test_obj_update(args, epoch[i], 0, &dkey, &iod, &sgl,
					dth, true);
		assert_rc_equal(rc, 0);

		xid[i] = dth->dth_xid;

		vts_dtx_end(dth);
	}

	/* Commit the first 4 DTXs. */
	rc = vos_dtx_commit(args->ctx.tc_co_hdl, xid, 4, NULL);
	assert_rc_equal(rc, 4);

	param.ip_hdl = args->ctx.tc_co_hdl;
	param.ip_ih = DAOS_HDL_INVAL;
	param.ip_oid = args->oid;
	param.ip_epr.epr_lo = epoch[0];
	param.ip_epr.epr_hi = epoch[9];
	param.ip_epc_expr = VOS_IT_EPC_RE;

	for (i = 0; i < 10; i++)
		found[i] = false;

	vdid.dkeys = (void **)dkey_buf;
	vdid.found = found;
	vdid.count = 4;

	rc = vos_iterate(&param, VOS_ITER_DKEY, false, &anchors,
			 vts_dtx_iter_cb, NULL, &vdid, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 4; i++) {
		assert_true(found[i]);
		found[i] = false;
	}

	/* Commit the others. */
	rc = vos_dtx_commit(args->ctx.tc_co_hdl, &xid[4], 6, NULL);
	assert_rc_equal(rc, 6);

	memset(&anchors, 0, sizeof(anchors));
	vdid.count = 10;

	rc = vos_iterate(&param, VOS_ITER_DKEY, false, &anchors,
			 vts_dtx_iter_cb, NULL, &vdid, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 10; i++) {
		assert_true(found[i]);
		free(dkey_buf[i]);
	}
}

/* DTX aggregation */
static void
dtx_18(void **state)
{
	struct io_test_args		*args = *state;
	struct dtx_id			 xid[10];
	daos_iod_t			 iod = { 0 };
	d_sg_list_t			 sgl = { 0 };
	daos_recx_t			 rex = { 0 };
	daos_key_t			 dkey;
	daos_key_t			 akey;
	d_iov_t				 val_iov;
	uint64_t			 epoch;
	char				 dkey_buf[UPDATE_DKEY_SIZE];
	char				 akey_buf[UPDATE_AKEY_SIZE];
	char				 update_buf[UPDATE_BUF_SIZE];
	char				 fetch_buf[UPDATE_BUF_SIZE];
	int				 rc;
	int				 i;

	/* Assume I am the leader. */
	for (i = 0; i < 10; i++) {
		struct dtx_handle		*dth = NULL;
		d_iov_t				 dkey_iov;
		uint64_t			 dkey_hash;

		vts_dtx_prep_update(args, &val_iov, &dkey_iov, &dkey,
				    dkey_buf, &akey, akey_buf, &iod, &sgl,
				    &rex, update_buf, UPDATE_BUF_SIZE,
				    UPDATE_REC_SIZE, &dkey_hash, &epoch, false);

		vts_dtx_begin(&args->oid, args->ctx.tc_co_hdl, epoch, dkey_hash,
			      &dth);

		rc = io_test_obj_update(args, epoch, 0, &dkey, &iod, &sgl,
					dth, true);
		assert_rc_equal(rc, 0);

		xid[i] = dth->dth_xid;

		vts_dtx_end(dth);
	}

	/* Commit all DTXs. */
	rc = vos_dtx_commit(args->ctx.tc_co_hdl, xid, 10, NULL);
	assert_rc_equal(rc, 10);

	for (i = 0; i < 10; i++) {
		rc = vos_dtx_check(args->ctx.tc_co_hdl, &xid[i], NULL, NULL, NULL, NULL, false);
		assert_int_equal(rc, DTX_ST_COMMITTED);
	}

	sleep(3);

	/* Aggregate the DTXs. */
	rc = vos_dtx_aggregate(args->ctx.tc_co_hdl);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 10; i++) {
		rc = vos_dtx_check(args->ctx.tc_co_hdl, &xid[i], NULL, NULL, NULL, NULL, false);
		assert_rc_equal(rc, -DER_NONEXIST);
	}

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, epoch, 0, &dkey, &iod, &sgl, true);
	assert_rc_equal(rc, 0);

	/* Related data record is still readable after DTX aggregation. */
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);
}

static int
dtx_tst_teardown(void **state)
{
	test_args_reset((struct io_test_args *)*state, VPOOL_SIZE);
	return 0;
}

static const struct CMUnitTest dtx_tests[] = {
	{ "VOS506: DTX commit visibility (single value, punch key)",
	  dtx_6, NULL, dtx_tst_teardown },
	{ "VOS507: DTX commit visibility (extent value, punch key)",
	  dtx_7, NULL, dtx_tst_teardown },
	{ "VOS508: DTX commit visibility (single value, punch obj)",
	  dtx_8, NULL, dtx_tst_teardown },
	{ "VOS509: DTX commit visibility (extent value, punch obj)",
	  dtx_9, NULL, dtx_tst_teardown },
	{ "VOS510: DTX abort visibility (single value, punch key)",
	  dtx_10, NULL, dtx_tst_teardown },
	{ "VOS511: DTX abort visibility (extent value, punch key)",
	  dtx_11, NULL, dtx_tst_teardown },
	{ "VOS512: DTX abort visibility (single value, punch obj)",
	  dtx_12, NULL, dtx_tst_teardown },
	{ "VOS513: DTX abort visibility (extent value, punch obj)",
	  dtx_13, NULL, dtx_tst_teardown },
	{ "VOS514: DTX ops against committed DTX",
	  dtx_14, NULL, dtx_tst_teardown },
	{ "VOS515: DTX ops against aborted DTX",
	  dtx_15, NULL, dtx_tst_teardown },
	{ "VOS516: DTX in CoS cache makes related data record as readable",
	  dtx_16, NULL, dtx_tst_teardown },
	{ "VOS517: list dkey with DTX",
	  dtx_17, NULL, dtx_tst_teardown },
	{ "VOS518: DTX aggregation",
	  dtx_18, NULL, dtx_tst_teardown },
};

int
run_dtx_tests(const char *cfg)
{
	char	test_name[DTS_CFG_MAX];

	dts_create_config(test_name, "DTX Test %s", cfg);
	return cmocka_run_group_tests_name(test_name,
					   dtx_tests, setup_io,
					   teardown_io);
}
