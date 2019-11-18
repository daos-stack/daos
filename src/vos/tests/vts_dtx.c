/**
 * (C) Copyright 2019 Intel Corporation.
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
 * This file is part of vos/tests/
 *
 * vos/tests/vts_dtx.c
 */
#define D_LOGFAC	DD_FAC(tests)

#include <daos/common.h>
#include <daos_srv/dtx_srv.h>
#include "vts_io.h"

static void
vts_dtx_cc(void **state, bool punch)
{
	struct io_test_args	*args = *state;
	struct vos_container	*cont;
	struct dtx_id		 xid;
	struct dtx_id		 xid2;
	int			 rc;

	daos_dti_gen(&xid, false);
	daos_dti_gen(&xid2, false);

	/* Insert a DTX into committable cache. */
	rc = vos_dtx_add_cc(args->ctx.tc_co_hdl, &args->oid, &xid,
			    DAOS_EPOCH_MAX - 1, 0);
	assert_int_equal(rc, 0);

	rc = vos_dtx_lookup_cc(args->ctx.tc_co_hdl, &xid);
	assert_int_equal(rc, 0);

	rc = vos_dtx_lookup_cc(args->ctx.tc_co_hdl, &xid2);
	assert_int_equal(rc, -DER_NONEXIST);

	cont = vos_hdl2cont(args->ctx.tc_co_hdl);
	/* Remove the DTX from committable cache. */
	vos_dtx_del_cc(cont, &xid);
	rc = vos_dtx_lookup_cc(args->ctx.tc_co_hdl, &xid);
	assert_int_equal(rc, -DER_NONEXIST);
}

/* update-DTX committable cache insert/delete/query */
static void
dtx_1(void **state)
{
	vts_dtx_cc(state, false);
}

/* punch-DTX committable cache insert/delete/query */
static void
dtx_2(void **state)
{
	vts_dtx_cc(state, true);
}

/* DTX committable cache fetch committable */
static void
dtx_4(void **state)
{
	struct io_test_args	*args = *state;
	struct dtx_entry	*dtes = NULL;
	struct dtx_id		 xid[11];
	int			 rc;
	int			 i;

	for (i = 0; i < 10; i++) {
		daos_dti_gen(&xid[i], false);

		rc = vos_dtx_add_cc(args->ctx.tc_co_hdl, &args->oid, &xid[i],
				     DAOS_EPOCH_MAX - 1, 0);
		assert_int_equal(rc, 0);
	}

	rc = vos_dtx_fetch_cc(args->ctx.tc_co_hdl, 100, NULL, DAOS_EPOCH_MAX,
			      true, &dtes);
	assert_int_equal(rc, 10);

	for (i = 0; i < 10; i++) {
		if (!daos_dti_equal(&xid[i], &dtes[i].dte_xid))
			break;
	}
	assert_int_equal(i, 10);

	D_FREE(dtes);

	/** With flush set to false, we should see nothing */
	rc = vos_dtx_fetch_cc(args->ctx.tc_co_hdl, 100, NULL, DAOS_EPOCH_MAX,
			      false, &dtes);
	assert_int_equal(rc, 0);

	/* Lookup first couple of entries several times, moving to priority
	 * cache
	 */
	for (i = 0; i < 10; i++) {
		rc = vos_dtx_lookup_cc(args->ctx.tc_co_hdl, &xid[1]);
		assert_int_equal(rc, 0);
		rc = vos_dtx_lookup_cc(args->ctx.tc_co_hdl, &xid[0]);
		assert_int_equal(rc, 0);
	}

	/* Now fetch again without no flush */
	rc = vos_dtx_fetch_cc(args->ctx.tc_co_hdl, 100, NULL, DAOS_EPOCH_MAX,
			      false, &dtes);
	assert_int_equal(rc, 2);

	for (i = 0; i < 2; i++) {
		int	j;

		for (j = 0; j < 10; j++) {
			if (daos_dti_equal(&xid[j], &dtes[i].dte_xid))
				break;
		}
		if (j >= 2)
			break;
	}
	assert_int_equal(i, 2);

	D_FREE(dtes);

}

static int
vts_dtx_begin(struct dtx_id *xid, daos_unit_oid_t *oid, daos_handle_t coh,
	      daos_epoch_t epoch, struct dtx_handle **dthp)
{
	struct dtx_handle	*dth;

	D_ALLOC_PTR(dth);
	if (dth == NULL)
		return -DER_NOMEM;

	dth->dth_xid = *xid;
	dth->dth_oid = *oid;
	dth->dth_coh = coh;
	dth->dth_epoch = epoch;
	dth->dth_ver = 1; /* init version */
	dth->dth_leader = 1;
	dth->dth_ent = NULL;
	dth->dth_obj = UMOFF_NULL;

	*dthp = dth;

	return 0;
}

static void
vts_dtx_end(struct dtx_handle *dth)
{
	D_FREE_PTR(dth);
}

static void
vts_dtx_prep_update(struct io_test_args *args, struct dtx_id *xid,
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
	args->ofeat = DAOS_OF_DKEY_UINT64 | DAOS_OF_AKEY_UINT64;

	daos_dti_gen(xid, false);
	*epoch = crt_hlc_get();

	vts_key_gen(dkey_buf, args->dkey_size, true, args);
	set_iov(dkey, dkey_buf, args->ofeat & DAOS_OF_DKEY_UINT64);
	*dkey_hash = d_hash_murmur64((const unsigned char *)dkey_buf,
				      args->dkey_size, 5731);

	dts_buf_render(update_buf, buf_size);
	d_iov_set(val_iov, update_buf, buf_size);

	sgl->sg_iovs = val_iov;
	sgl->sg_nr = 1;

	d_iov_set(dkey_iov, dkey_buf, args->dkey_size);
	rex->rx_idx = hash_key(dkey_iov, args->ofeat & DAOS_OF_DKEY_UINT64);

	vts_key_gen(akey_buf, args->akey_size, false, args);
	set_iov(akey, akey_buf, args->ofeat & DAOS_OF_AKEY_UINT64);

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

/* remove DTX from committable cache after commit */
static void
dtx_5(void **state)
{
	struct io_test_args		*args = *state;
	struct dtx_handle		*dth = NULL;
	struct dtx_id			 xid;
	struct dtx_stat			 stat = { 0 };
	daos_iod_t			 iod = { 0 };
	d_sg_list_t			 sgl = { 0 };
	daos_recx_t			 rex = { 0 };
	daos_key_t			 dkey;
	daos_key_t			 akey;
	d_iov_t				 dkey_iov;
	d_iov_t				 val_iov;
	uint64_t			 epoch;
	uint64_t			 dkey_hash;
	uint64_t			 saved_committable;
	uint64_t			 saved_committed;
	char				 dkey_buf[UPDATE_DKEY_SIZE];
	char				 akey_buf[UPDATE_AKEY_SIZE];
	char				 update_buf[UPDATE_BUF_SIZE];
	int				 rc;


	vts_dtx_prep_update(args, &xid, &val_iov, &dkey_iov, &dkey, dkey_buf,
			    &akey, akey_buf, &iod, &sgl, &rex, update_buf,
			    UPDATE_BUF_SIZE, UPDATE_REC_SIZE, &dkey_hash,
			    &epoch, false);

	/* Assume I am the leader. */
	rc = vts_dtx_begin(&xid, &args->oid, args->ctx.tc_co_hdl, epoch, &dth);
	assert_int_equal(rc, 0);

	rc = io_test_obj_update(args, epoch, &dkey, &iod, &sgl, dth, true);
	assert_int_equal(rc, 0);

	/* The DTX is 'prepared'. */
	vts_dtx_end(dth);

	/* Add former DTX into committable cache. */
	rc = vos_dtx_add_cc(args->ctx.tc_co_hdl, &args->oid, &xid, epoch, 0);
	assert_int_equal(rc, 0);

	vos_dtx_stat(args->ctx.tc_co_hdl, &stat);
	saved_committable = stat.dtx_committable_count;
	saved_committed = stat.dtx_committed_count;

	/* Commit former DTX that will be removed from the committable cache. */
	rc = vos_dtx_commit(args->ctx.tc_co_hdl, &xid, 1);
	assert_int_equal(rc, 0);

	vos_dtx_stat(args->ctx.tc_co_hdl, &stat);
	assert_true(saved_committable == stat.dtx_committable_count + 1);
	assert_true(saved_committed == stat.dtx_committed_count - 1);
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

	vts_dtx_prep_update(args, &xid, &val_iov, &dkey_iov, &dkey, dkey_buf,
			    &akey, akey_buf, &iod, &sgl, &rex, update_buf,
			    UPDATE_BUF_SIZE, UPDATE_REC_SIZE, &dkey_hash,
			    &epoch, ext);

	/* Assume I am the leader. */
	rc = vts_dtx_begin(&xid, &args->oid, args->ctx.tc_co_hdl, epoch, &dth);
	assert_int_equal(rc, 0);

	rc = io_test_obj_update(args, epoch, &dkey, &iod, &sgl, dth, true);
	assert_int_equal(rc, 0);

	/* The update DTX is 'prepared'. */
	vts_dtx_end(dth);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, epoch, &dkey, &iod, &sgl, true);
	assert_int_equal(rc, 0);

	/* Data record with update DTX is invisible before commit. */
	assert_memory_not_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

	/* Commit the update DTX. */
	rc = vos_dtx_commit(args->ctx.tc_co_hdl, &xid, 1);
	assert_int_equal(rc, 0);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	/* Fetch again. */
	rc = io_test_obj_fetch(args, epoch, &dkey, &iod, &sgl, true);
	assert_int_equal(rc, 0);

	/* Data record with update DTX is readable after commit. */
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

	/* Generate the punch DTX ID. */
	daos_dti_gen(&xid, false);

	rc = vts_dtx_begin(&xid, &args->oid, args->ctx.tc_co_hdl, ++epoch,
			   &dth);
	assert_int_equal(rc, 0);

	if (punch_obj)
		rc = vos_obj_punch(args->ctx.tc_co_hdl, args->oid, epoch,
				   1, 0, NULL, 0, NULL, dth);
	else
		rc = vos_obj_punch(args->ctx.tc_co_hdl, args->oid, epoch,
				   1, 0, &dkey, 1, &akey, dth);
	assert_int_equal(rc, 0);

	/* The punch DTX is 'prepared'. */
	vts_dtx_end(dth);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, ++epoch, &dkey, &iod, &sgl, true);
	/* Punch is not yet visible */
	assert_int_equal(rc, 0);

	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

	/* Commit the punch DTX. */
	rc = vos_dtx_commit(args->ctx.tc_co_hdl, &xid, 1);
	assert_int_equal(rc, 0);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	/* Fetch again. */
	rc = io_test_obj_fetch(args, ++epoch, &dkey, &iod, &sgl, true);
	assert_int_equal(rc, 0);

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

	vts_dtx_prep_update(args, &xid, &val_iov, &dkey_iov, &dkey, dkey_buf,
			    &akey, akey_buf, &iod, &sgl, &rex, update_buf1,
			    UPDATE_BUF_SIZE, UPDATE_REC_SIZE, &dkey_hash,
			    &epoch, ext);

	/* initial update. */
	rc = io_test_obj_update(args, epoch, &dkey, &iod, &sgl, dth, true);
	assert_int_equal(rc, 0);

	dts_buf_render(update_buf2, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, update_buf2, UPDATE_BUF_SIZE);

	/* Assume I am the leader. */
	rc = vts_dtx_begin(&xid, &args->oid, args->ctx.tc_co_hdl, ++epoch,
			   &dth);
	assert_int_equal(rc, 0);

	rc = io_test_obj_update(args, epoch, &dkey, &iod, &sgl, dth, true);
	assert_int_equal(rc, 0);

	/* The update DTX is 'prepared'. */
	vts_dtx_end(dth);

	/* Aborted the update DTX. */
	rc = vos_dtx_abort(args->ctx.tc_co_hdl, epoch, &xid, 1);
	assert_int_equal(rc, 0);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, epoch, &dkey, &iod, &sgl, true);
	assert_int_equal(rc, 0);

	assert_memory_not_equal(update_buf2, fetch_buf, UPDATE_BUF_SIZE);
	/* The fetched result is the data written via the initial update. */
	assert_memory_equal(update_buf1, fetch_buf, UPDATE_BUF_SIZE);

	/* Generate the punch DTX ID. */
	daos_dti_gen(&xid, false);

	rc = vts_dtx_begin(&xid, &args->oid, args->ctx.tc_co_hdl, ++epoch,
			   &dth);
	assert_int_equal(rc, 0);

	if (punch_obj)
		rc = vos_obj_punch(args->ctx.tc_co_hdl, args->oid, epoch,
				   1, 0, NULL, 0, NULL, dth);
	else
		rc = vos_obj_punch(args->ctx.tc_co_hdl, args->oid, epoch,
				   1, 0, &dkey, 1, &akey, dth);
	assert_int_equal(rc, 0);

	/* The punch DTX is 'prepared'. */
	vts_dtx_end(dth);

	/* Aborted the punch DTX. */
	rc = vos_dtx_abort(args->ctx.tc_co_hdl, epoch, &xid, 1);
	assert_int_equal(rc, 0);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, ++epoch, &dkey, &iod, &sgl, true);
	assert_int_equal(rc, 0);

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

	vts_dtx_prep_update(args, &xid, &val_iov, &dkey_iov, &dkey, dkey_buf,
			    &akey, akey_buf, &iod, &sgl, &rex, update_buf,
			    UPDATE_BUF_SIZE, UPDATE_REC_SIZE, &dkey_hash,
			    &epoch, false);

	/* Assume I am the leader. */
	rc = vts_dtx_begin(&xid, &args->oid, args->ctx.tc_co_hdl, epoch, &dth);
	assert_int_equal(rc, 0);

	rc = io_test_obj_update(args, epoch, &dkey, &iod, &sgl, dth, true);
	assert_int_equal(rc, 0);

	/* The DTX is 'prepared'. */
	vts_dtx_end(dth);

	/* Commit the DTX. */
	rc = vos_dtx_commit(args->ctx.tc_co_hdl, &xid, 1);
	assert_int_equal(rc, 0);

	/* Double commit the DTX is harmless. */
	vos_dtx_commit(args->ctx.tc_co_hdl, &xid, 1);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, epoch, &dkey, &iod, &sgl, true);
	assert_int_equal(rc, 0);

	/* Data record is not affected by double commit. */
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

	/* Committed DTX cannot be aborted.
	 * But we cannot check "assert_int_not_equal(rc, 0)" that depends
	 * on the umem_tx_abort() which may return 0 for vmem based case.
	 */
	vos_dtx_abort(args->ctx.tc_co_hdl, epoch, &xid, 1);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, epoch, &dkey, &iod, &sgl, true);
	assert_int_equal(rc, 0);

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

	vts_dtx_prep_update(args, &xid, &val_iov, &dkey_iov, &dkey, dkey_buf,
			    &akey, akey_buf, &iod, &sgl, &rex, update_buf1,
			    UPDATE_BUF_SIZE, UPDATE_REC_SIZE, &dkey_hash,
			    &epoch, false);

	/* initial update. */
	rc = io_test_obj_update(args, epoch, &dkey, &iod, &sgl, dth, true);
	assert_int_equal(rc, 0);

	dts_buf_render(update_buf2, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, update_buf2, UPDATE_BUF_SIZE);

	/* Assume I am the leader. */
	rc = vts_dtx_begin(&xid, &args->oid, args->ctx.tc_co_hdl, ++epoch,
			   &dth);
	assert_int_equal(rc, 0);

	rc = io_test_obj_update(args, epoch, &dkey, &iod, &sgl, dth, true);
	assert_int_equal(rc, 0);

	/* The update DTX is 'prepared'. */
	vts_dtx_end(dth);

	/* Aborted the update DTX. */
	rc = vos_dtx_abort(args->ctx.tc_co_hdl, epoch, &xid, 1);
	assert_int_equal(rc, 0);

	/* Double aborted the DTX is harmless. */
	vos_dtx_abort(args->ctx.tc_co_hdl, epoch, &xid, 1);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, epoch, &dkey, &iod, &sgl, true);
	assert_int_equal(rc, 0);

	assert_memory_not_equal(update_buf2, fetch_buf, UPDATE_BUF_SIZE);
	/* The fetched result is the data written via the initial update. */
	assert_memory_equal(update_buf1, fetch_buf, UPDATE_BUF_SIZE);

	/* Aborted DTX cannot be committed. */
	vos_dtx_commit(args->ctx.tc_co_hdl, &xid, 1);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, epoch, &dkey, &iod, &sgl, true);
	assert_int_equal(rc, 0);

	assert_memory_not_equal(update_buf2, fetch_buf, UPDATE_BUF_SIZE);
	/* The fetched result is the data written via the initial update. */
	assert_memory_equal(update_buf1, fetch_buf, UPDATE_BUF_SIZE);
}

/* DTX in committable cache makes related data record as readable */
static void
dtx_16(void **state)
{
	struct io_test_args		*args = *state;
	struct dtx_handle		*dth = NULL;
	struct dtx_id			 xid;
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

	vts_dtx_prep_update(args, &xid, &val_iov, &dkey_iov, &dkey, dkey_buf,
			    &akey, akey_buf, &iod, &sgl, &rex, update_buf,
			    UPDATE_BUF_SIZE, UPDATE_REC_SIZE, &dkey_hash,
			    &epoch, false);

	rc = vts_dtx_begin(&xid, &args->oid, args->ctx.tc_co_hdl, epoch, &dth);
	assert_int_equal(rc, 0);

	rc = io_test_obj_update(args, epoch, &dkey, &iod, &sgl, dth, true);
	assert_int_equal(rc, 0);

	/* The DTX is 'prepared'. */
	vts_dtx_end(dth);

	daos_fail_loc_set(DAOS_VOS_NON_LEADER | DAOS_FAIL_ALWAYS);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = vos_fetch_begin(args->ctx.tc_co_hdl, args->oid, epoch,
			     &dkey_iov, 1, &iod, false, &ioh);
	/* The former DTX is not committed, so need to retry with leader. */
	assert_int_equal(rc, -DER_INPROGRESS);

	daos_fail_loc_reset();

	rc = io_test_obj_fetch(args, epoch, &dkey, &iod, &sgl, true);
	assert_int_equal(rc, 0);

	/* Former DTX is not committed, so nothing can be fetched. */
	assert_memory_not_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

	/* Insert a DTX into committable cache. */
	rc = vos_dtx_add_cc(args->ctx.tc_co_hdl, &args->oid, &xid, epoch, 0);
	assert_int_equal(rc, 0);

	/* Fetch again. */
	rc = io_test_obj_fetch(args, epoch, &dkey, &iod, &sgl, true);
	assert_int_equal(rc, 0);

	/* The DTX in committable cache will make related data record as
	 * readable.
	 */
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

	/* Commit the fisrt 4 DTXs. */
	rc = vos_dtx_commit(args->ctx.tc_co_hdl, &xid, 1);
	assert_int_equal(rc, 0);

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

		vts_dtx_prep_update(args, &xid[i], &val_iov, &dkey_iov, &dkey,
				    dkey_buf[i], &akey, akey_buf, &iod, &sgl,
				    &rex, update_buf, UPDATE_BUF_SIZE,
				    UPDATE_REC_SIZE, &dkey_hash, &epoch[i],
				    false);

		rc = vts_dtx_begin(&xid[i], &args->oid, args->ctx.tc_co_hdl,
				   epoch[i], &dth);
		assert_int_equal(rc, 0);

		rc = io_test_obj_update(args, epoch[i], &dkey, &iod, &sgl,
					dth, true);
		assert_int_equal(rc, 0);

		vts_dtx_end(dth);
	}

	/* Commit the fisrt 4 DTXs. */
	rc = vos_dtx_commit(args->ctx.tc_co_hdl, xid, 4);
	assert_int_equal(rc, 0);

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
			 vts_dtx_iter_cb, &vdid);
	assert_int_equal(rc, 0);

	for (i = 0; i < 4; i++) {
		assert_true(found[i]);
		found[i] = false;
	}

	/* Commit the others. */
	rc = vos_dtx_commit(args->ctx.tc_co_hdl, &xid[4], 6);
	assert_int_equal(rc, 0);

	memset(&anchors, 0, sizeof(anchors));
	vdid.count = 10;

	rc = vos_iterate(&param, VOS_ITER_DKEY, false, &anchors,
			 vts_dtx_iter_cb, &vdid);
	assert_int_equal(rc, 0);

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

		vts_dtx_prep_update(args, &xid[i], &val_iov, &dkey_iov, &dkey,
				    dkey_buf, &akey, akey_buf, &iod, &sgl,
				    &rex, update_buf, UPDATE_BUF_SIZE,
				    UPDATE_REC_SIZE, &dkey_hash, &epoch, false);

		rc = vts_dtx_begin(&xid[i], &args->oid, args->ctx.tc_co_hdl,
				   epoch, &dth);
		assert_int_equal(rc, 0);

		rc = io_test_obj_update(args, epoch, &dkey, &iod, &sgl,
					dth, true);
		assert_int_equal(rc, 0);

		vts_dtx_end(dth);
	}

	/* Commit all DTXs. */
	rc = vos_dtx_commit(args->ctx.tc_co_hdl, xid, 10);
	assert_int_equal(rc, 0);

	for (i = 0; i < 10; i++) {
		rc = vos_dtx_check(args->ctx.tc_co_hdl, &xid[i]);
		assert_int_equal(rc, DTX_ST_COMMITTED);
	}

	sleep(3);

	/* Aggregate the DTXs. */
	rc = vos_dtx_aggregate(args->ctx.tc_co_hdl);
	assert_int_equal(rc, 0);

	for (i = 0; i < 10; i++) {
		rc = vos_dtx_check(args->ctx.tc_co_hdl, &xid[i]);
		assert_int_equal(rc, -DER_NONEXIST);
	}

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, fetch_buf, UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	rc = io_test_obj_fetch(args, epoch, &dkey, &iod, &sgl, true);
	assert_int_equal(rc, 0);

	/* Related data record is still readable after DTX aggregation. */
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);
}

static int
dtx_tst_teardown(void **state)
{
	test_args_reset((struct io_test_args *) *state, VPOOL_SIZE);
	return 0;
}

static const struct CMUnitTest dtx_tests[] = {
	{ "VOS501: update-DTX committable cache insert/delete/query",
	  dtx_1, NULL, dtx_tst_teardown },
	{ "VOS502: punch-DTX committable cache insert/delete/query",
	  dtx_2, NULL, dtx_tst_teardown },
	{ "VOS504: DTX committable cache fetch committable",
	  dtx_4, NULL, dtx_tst_teardown },
	{ "VOS505: remove DTX from committable cache after commit",
	  dtx_5, NULL, dtx_tst_teardown },
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
	{ "VOS516: DTX in committable cache makes record readable",
	  dtx_16, NULL, dtx_tst_teardown },
	{ "VOS517: list dkey with DTX",
	  dtx_17, NULL, dtx_tst_teardown },
	{ "VOS518: DTX aggregation",
	  dtx_18, NULL, dtx_tst_teardown },
};

int
run_dtx_tests(void)
{
	return cmocka_run_group_tests_name("VOS DTX Test",
					   dtx_tests, setup_io,
					   teardown_io);
}
