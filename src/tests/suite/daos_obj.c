/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * tests/suite/daos_obj.c
 */
#define D_LOGFAC	DD_FAC(tests)
#include "daos_test.h"

#include "daos_iotest.h"
#include <daos_types.h>
#include <daos/checksum.h>
#include <daos/placement.h>

int dts_obj_class	= OC_RP_2G1;
int dts_obj_replica_cnt	= 2;

void
ioreq_init(struct ioreq *req, daos_handle_t coh, daos_obj_id_t oid,
	   daos_iod_type_t iod_type, test_arg_t *arg)
{
	int rc;
	int i;
	bool ev_flag;

	memset(req, 0, sizeof(*req));

	req->iod_type = iod_type;
	req->arg = arg;
	if (arg->async) {
		rc = daos_event_init(&req->ev, arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}

	arg->expect_result = 0;
	daos_fail_num_set(arg->fail_num);
	daos_fail_value_set(arg->fail_value);
	daos_fail_loc_set(arg->fail_loc);

	/* init sgl */
	for (i = 0; i < IOREQ_SG_IOD_NR; i++) {
		req->sgl[i].sg_nr = IOREQ_SG_NR;
		req->sgl[i].sg_iovs = req->val_iov[i];
	}

	/* init record extent */
	for (i = 0; i < IOREQ_SG_IOD_NR; i++) {
		int j;

		for (j = 0; j < IOREQ_IOD_NR; j++) {
			req->rex[i][j].rx_nr = 1;
			req->rex[i][j].rx_idx = 0;

			/** epoch range: required by the wire format */
			req->erange[i][j].epr_lo = 0;
			req->erange[i][j].epr_hi = DAOS_EPOCH_MAX;
		}

		/* I/O descriptor */
		req->iod[i].iod_recxs = req->rex[i];
		req->iod[i].iod_nr = IOREQ_IOD_NR;
		req->iod[i].iod_type = iod_type;
	}
	D_DEBUG(DF_MISC, "open oid="DF_OID"\n", DP_OID(oid));

	/** open the object */
	rc = daos_obj_open(coh, oid, DAOS_OO_RW, &req->oh,
			   req->arg->async ? &req->ev : NULL);
	assert_rc_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_test(&req->ev, DAOS_EQ_WAIT, &ev_flag);
		assert_rc_equal(rc, 0);
		assert_int_equal(ev_flag, true);
		assert_int_equal(req->ev.ev_error, 0);
	}
}

void
ioreq_fini(struct ioreq *req)
{
	int rc;

	rc = daos_obj_close(req->oh, NULL);
	assert_rc_equal(rc, 0);

	req->arg->fail_loc = 0;
	req->arg->fail_value = 0;
	req->arg->fail_num = 0;
	daos_fail_loc_set(0);
	if (req->arg->async) {
		rc = daos_event_fini(&req->ev);
		assert_rc_equal(rc, 0);
	}
}

/* no wait for async insert, for sync insert it still will block */
static void
insert_internal_nowait(daos_key_t *dkey, int nr, d_sg_list_t *sgls,
		       daos_iod_t *iods, daos_handle_t th, struct ioreq *req,
		       uint64_t flags)
{
	int rc;

	/** execute update operation */
	rc = daos_obj_update(req->oh, th, flags, dkey, nr, iods, sgls,
			     req->arg->async ? &req->ev : NULL);
	if (!req->arg->async)
		assert_rc_equal(rc, req->arg->expect_result);
}

static void
ioreq_dkey_set(struct ioreq *req, const char *dkey)
{
	d_iov_set(&req->dkey, (void *)dkey, strlen(dkey));
}

static void
ioreq_akey_set(struct ioreq *req, const char *akey)
{
	d_iov_set(&req->akey, (void *)akey, strlen(akey));
}

static void
ioreq_io_akey_set(struct ioreq *req, const char **akey, int nr)
{
	int i;

	assert_in_range(nr, 1, IOREQ_SG_IOD_NR);
	/** akey */
	for (i = 0; i < nr; i++)
		d_iov_set(&req->iod[i].iod_name, (void *)akey[i],
			     strlen(akey[i]));
}

static void
ioreq_sgl_simple_set(struct ioreq *req, void **value, daos_size_t *data_size,
		     int nr)
{
	d_sg_list_t *sgl = req->sgl;
	int i;

	assert_in_range(nr, 1, IOREQ_SG_IOD_NR);
	for (i = 0; i < nr; i++) {
		sgl[i].sg_nr = 1;
		sgl[i].sg_nr_out = 0;
		d_iov_set(&sgl[i].sg_iovs[0], value[i], data_size[i]);
	}
}

static void
ioreq_iod_simple_set(struct ioreq *req, daos_size_t *iod_size, bool lookup,
		     uint64_t *idx, int nr, int *rx_nr)
{
	daos_iod_t *iod = req->iod;
	int i;

	assert_in_range(nr, 1, IOREQ_SG_IOD_NR);
	for (i = 0; i < nr; i++) {
		/** record extent */
		iod[i].iod_type = req->iod_type;
		iod[i].iod_size = iod_size[i];
		if (req->iod_type == DAOS_IOD_ARRAY) {
			iod[i].iod_recxs[0].rx_idx = idx[i] +
				(req->arg->idx_no_jump ? 0 : i * SEGMENT_SIZE);
			iod[i].iod_recxs[0].rx_nr = rx_nr[i];
		}
		iod[i].iod_nr = 1;
	}
}

static void
ioreq_iod_recxs_set(struct ioreq *req, int idx, daos_size_t size,
		   daos_recx_t *recxs, int nr)
{
	daos_iod_t *iod = &req->iod[idx];

	assert_in_range(nr, 1, IOREQ_IOD_NR);
	iod->iod_type = req->iod_type;
	iod->iod_size = size;
	if (req->iod_type == DAOS_IOD_ARRAY) {
		iod->iod_nr = nr;
		iod->iod_recxs = recxs;
	} else {
		iod->iod_nr = 1;
		iod->iod_recxs = NULL;
	}
}

void
insert_recxs_nowait(const char *dkey, const char *akey, daos_size_t iod_size,
		    daos_handle_t th, daos_recx_t *recxs, int nr, void *data,
		    daos_size_t data_size, struct ioreq *req)
{
	assert_in_range(nr, 1, IOREQ_IOD_NR);

	/* dkey */
	ioreq_dkey_set(req, dkey);

	/* akey */
	ioreq_io_akey_set(req, &akey, 1);

	/* set sgl */
	if (data != NULL)
		ioreq_sgl_simple_set(req, &data, &data_size, 1);

	/* iod, recxs */
	ioreq_iod_recxs_set(req, 0, iod_size, recxs, nr);

	insert_internal_nowait(&req->dkey, 1, req->sgl, req->iod, th, req, 0);
}

void
insert_nowait(const char *dkey, int nr, const char **akey,
	      daos_size_t *iod_size, int *rx_nr, uint64_t *idx, void **val,
	      daos_handle_t th, struct ioreq *req, uint64_t flags)
{
	daos_size_t	data_size[nr];
	int		i;

	assert_in_range(nr, 1, IOREQ_SG_IOD_NR);

	for (i = 0; i < nr; i++)
		data_size[i] = iod_size[i] * rx_nr[i];

	/* dkey */
	ioreq_dkey_set(req, dkey);

	/* akey */
	ioreq_io_akey_set(req, akey, nr);

	/* set sgl */
	if (val != NULL)
		ioreq_sgl_simple_set(req, val, data_size, nr);

	/* set iod */
	ioreq_iod_simple_set(req, iod_size, false, idx, nr, rx_nr);

	insert_internal_nowait(&req->dkey, nr, val == NULL ? NULL : req->sgl,
			       req->iod, th, req, flags);
}

void
insert_test(struct ioreq *req, uint64_t timeout)
{
	bool	ev_flag;
	int	rc;

	if (!req->arg->async)
		return;

	rc = daos_event_test(&req->ev, timeout, &ev_flag);
	assert_rc_equal(rc, 0);
}

void
insert_wait(struct ioreq *req)
{
	insert_test(req, DAOS_EQ_WAIT);
	if (req->arg->async)
		assert_int_equal(req->ev.ev_error, req->arg->expect_result);
}

/**
 * Main insert function.
 */
void
insert(const char *dkey, int nr, const char **akey, daos_size_t *iod_size,
       int *rx_nr, uint64_t *idx, void **val, daos_handle_t th,
	struct ioreq *req, uint64_t flags)
{
	insert_nowait(dkey, nr, akey, iod_size, rx_nr, idx, val, th, req,
		      flags);
	insert_wait(req);
}

/**
 * Helper function to insert a single record (nr=1). The number of record
 * extents is set to 1, meaning the iod size is equal to data size (record size(
 * in this simple use case.
 */
void
insert_single(const char *dkey, const char *akey, uint64_t idx, void *value,
	      daos_size_t iod_size, daos_handle_t th, struct ioreq *req)
{
	int rx_nr = 1;

	insert(dkey, 1, &akey, &iod_size, &rx_nr, &idx, &value, th, req, 0);
}

void
insert_single_with_flags(const char *dkey, const char *akey, uint64_t idx,
			 void *value, daos_size_t iod_size, daos_handle_t th,
			 struct ioreq *req, uint64_t flags)
{
	int rx_nr = 1;

	insert(dkey, 1, &akey, &iod_size, &rx_nr, &idx, &value, th, req, flags);
}

/**
 * Helper function to insert a single record (nr=1) by inserting multiple
 * contiguous record extents and specifying an iod size (extent/array boundary).
 */
void
insert_single_with_rxnr(const char *dkey, const char *akey, uint64_t idx,
			void *value, daos_size_t iod_size, int rx_nr,
			 daos_handle_t th, struct ioreq *req)
{
	insert(dkey, /*nr*/1, &akey, &iod_size, &rx_nr, &idx,
	       value != NULL ? &value : NULL, th, req, 0);
}

void
insert_recxs(const char *dkey, const char *akey, daos_size_t iod_size,
	     daos_handle_t th, daos_recx_t *recxs, int nr, void *data,
	     daos_size_t data_size, struct ioreq *req)
{
	insert_recxs_nowait(dkey, akey, iod_size, th, recxs, nr, data,
			    data_size, req);
	insert_wait(req);
}

void
inset_recxs_dkey_uint64(uint64_t *dkey, const char *akey, daos_size_t iod_size,
	     daos_handle_t th, daos_recx_t *recxs, int nr, void *data,
	     daos_size_t data_size, struct ioreq *req)
{
	assert_in_range(nr, 1, IOREQ_IOD_NR);

	/* dkey */
	d_iov_set(&req->dkey, (void *)dkey, sizeof(uint64_t));

	/* akey */
	ioreq_io_akey_set(req, &akey, 1);

	/* set sgl */
	if (data != NULL)
		ioreq_sgl_simple_set(req, &data, &data_size, 1);

	/* iod, recxs */
	ioreq_iod_recxs_set(req, 0, iod_size, recxs, nr);

	insert_internal_nowait(&req->dkey, 1, req->sgl, req->iod, th, req, 0);

	insert_wait(req);
}

void
punch_obj(daos_handle_t th, struct ioreq *req)
{
	int rc;

	rc = daos_obj_punch(req->oh, th, 0, NULL);
	assert_rc_equal(rc, 0);
}

void
punch_dkey(const char *dkey, daos_handle_t th, struct ioreq *req)
{
	int rc;

	ioreq_dkey_set(req, dkey);

	rc = daos_obj_punch_dkeys(req->oh, th, 0, 1, &req->dkey, NULL);
	assert_rc_equal(rc, req->arg->expect_result);
}

void
punch_dkey_with_flags(const char *dkey, daos_handle_t th, struct ioreq *req,
		      uint64_t flags)
{
	int rc;

	ioreq_dkey_set(req, dkey);

	rc = daos_obj_punch_dkeys(req->oh, th, flags, 1, &req->dkey, NULL);
	assert_rc_equal(rc, req->arg->expect_result);
}

void
punch_akey(const char *dkey, const char *akey, daos_handle_t th,
	   struct ioreq *req)
{
	daos_key_t daos_akey;
	int rc;

	ioreq_dkey_set(req, dkey);

	daos_akey.iov_buf = (void *)akey;
	daos_akey.iov_len = strlen(akey);
	daos_akey.iov_buf_len = strlen(akey);

	rc = daos_obj_punch_akeys(req->oh, th, 0, &req->dkey, 1, &daos_akey,
				  NULL);
	assert_rc_equal(rc, req->arg->expect_result);
}

void
punch_akey_with_flags(const char *dkey, const char *akey, daos_handle_t th,
		      struct ioreq *req, uint64_t flags)
{
	daos_key_t daos_akey;
	int rc;

	ioreq_dkey_set(req, dkey);

	daos_akey.iov_buf = (void *)akey;
	daos_akey.iov_len = strlen(akey);
	daos_akey.iov_buf_len = strlen(akey);

	rc = daos_obj_punch_akeys(req->oh, th, flags, &req->dkey, 1, &daos_akey,
				  NULL);
	assert_rc_equal(rc, req->arg->expect_result);
}

void
punch_single(const char *dkey, const char *akey, uint64_t idx,
	     daos_handle_t th, struct ioreq *req)
{
	daos_size_t size = 0;

	insert_single(dkey, akey, idx, NULL, size, th, req);
}

void
punch_recxs(const char *dkey, const char *akey, daos_recx_t *recxs,
	    int nr, daos_handle_t th, struct ioreq *req)
{
	insert_recxs(dkey, akey, 0, th, recxs, nr, NULL, 0, req);
}

void
punch_rec_with_rxnr(const char *dkey, const char *akey, uint64_t idx, int rx_nr,
		    daos_handle_t th, struct ioreq *req)
{

	insert_single_with_rxnr(dkey, akey, idx, NULL, /*iod_size*/0, rx_nr,
				th, req);
}

static void
lookup_internal(daos_key_t *dkey, int nr, d_sg_list_t *sgls,
		daos_iod_t *iods, daos_handle_t th, struct ioreq *req,
		bool empty)
{
	uint64_t api_flags;
	bool ev_flag;
	int rc;

	if (empty)
		api_flags = DAOS_COND_DKEY_FETCH | DAOS_COND_AKEY_FETCH;
	else
		api_flags = 0;

	/** execute fetch operation */
	rc = daos_obj_fetch(req->oh, th, api_flags, dkey, nr, iods, sgls,
			    NULL, req->arg->async ? &req->ev : NULL);
	if (!req->arg->async) {
		req->result = rc;
		if (rc != -DER_INPROGRESS && !req->arg->not_check_result)
			assert_rc_equal(rc, req->arg->expect_result);
		return;
	}

	/** wait for fetch completion */
	rc = daos_event_test(&req->ev, DAOS_EQ_WAIT, &ev_flag);
	assert_rc_equal(rc, 0);
	assert_int_equal(ev_flag, true);
	req->result = req->ev.ev_error;
	if (req->ev.ev_error != -DER_INPROGRESS && !req->arg->not_check_result)
		assert_int_equal(req->ev.ev_error, req->arg->expect_result);
}

void
lookup_recxs(const char *dkey, const char *akey, daos_size_t iod_size,
	     daos_handle_t th, daos_recx_t *recxs, int nr, void *data,
	     daos_size_t data_size, struct ioreq *req)
{
	/* dkey */
	ioreq_dkey_set(req, dkey);

	/* akey */
	ioreq_io_akey_set(req, &akey, 1);

	/* set sgl */
	assert_int_not_equal(data, NULL);
	ioreq_sgl_simple_set(req, &data, &data_size, 1);

	/* iod, recxs */
	ioreq_iod_recxs_set(req, 0, iod_size, recxs, nr);

	lookup_internal(&req->dkey, 1, req->sgl, req->iod, th, req, false);
}

/**
 * Main lookup function for fetch operation.
 */
void
lookup(const char *dkey, int nr, const char **akey, uint64_t *idx,
	daos_size_t *iod_size, void **val, daos_size_t *data_size,
	daos_handle_t th, struct ioreq *req, bool empty)
{
	int		i;
	int		rx_nr[nr];

	assert_in_range(nr, 1, IOREQ_SG_IOD_NR);

	for (i = 0; i < nr; i++) {
		if (iod_size[i] != DAOS_REC_ANY) /* extent is known */
			rx_nr[i] = data_size[i] / iod_size[i];
		else
			rx_nr[i] = 1;
	}

	/* dkey */
	ioreq_dkey_set(req, dkey);

	/* akey */
	ioreq_io_akey_set(req, akey, nr);

	/* set sgl */
	ioreq_sgl_simple_set(req, val, data_size, nr);

	/* set iod */
	ioreq_iod_simple_set(req, iod_size, true, idx, nr, rx_nr);

	req->result = -1;
	lookup_internal(&req->dkey, nr, req->sgl, req->iod, th, req, empty);
	for (i = 0; i < nr; i++)
		/** record extent */
		iod_size[i] = req->iod[i].iod_size;
}

/**
 * Helper function to fetch a single record (nr=1). Iod size is set to
 * DAOS_REC_ANY, which indicates that extent is unknown, and the entire record
 * should be returned in a single extent (as it most likely was inserted that
 * way). This lookup will only return 1 extent, therefore is not appropriate to
 * use if the record was inserted using insert_single_with_rxnr() in most cases.
 */
void
lookup_single(const char *dkey, const char *akey, uint64_t idx,
	      void *val, daos_size_t data_size, daos_handle_t th,
	      struct ioreq *req)
{
	daos_size_t read_size = DAOS_REC_ANY; /* extent is unknown */

	lookup(dkey, 1, &akey, &idx, &read_size, &val, &data_size, th, req,
	       false);
}

/**
 * Helper function to fetch a single record (nr=1) with a known iod/extent size.
 * The number of record extents is calculated before the fetch using the iod
 * size and the data size.
 */
void
lookup_single_with_rxnr(const char *dkey, const char *akey, uint64_t idx,
			void *val, daos_size_t iod_size, daos_size_t data_size,
			daos_handle_t th, struct ioreq *req)
{
	lookup(dkey, 1, &akey, &idx, &iod_size, &val, &data_size, th, req,
	       false);
}

void
lookup_empty_single(const char *dkey, const char *akey, uint64_t idx,
		    void *val, daos_size_t data_size, daos_handle_t th,
		    struct ioreq *req)
{
	daos_size_t read_size = DAOS_REC_ANY; /* extent is unknown */

	lookup(dkey, 1, &akey, &idx, &read_size, &val, &data_size, th,
	       req, true);
}

/**
 * get the Pool storage info.
 */
int pool_storage_info(test_arg_t *arg, daos_pool_info_t *pinfo)
{
	int rc;

	/*get only pool space info*/
	pinfo->pi_bits = DPI_SPACE;
	rc = daos_pool_query(arg->pool.poh, NULL, pinfo, NULL, NULL);
	if (rc != 0) {
		print_message("pool query failed %d\n", rc);
		return rc;
	}

	print_message("SCM space: Total = %" PRIu64 " Free= %" PRIu64"\t"
	"NVMe space: Total = %" PRIu64 " Free= %" PRIu64"\n",
	pinfo->pi_space.ps_space.s_total[0],
	pinfo->pi_space.ps_space.s_free[0],
	pinfo->pi_space.ps_space.s_total[1],
	pinfo->pi_space.ps_space.s_free[1]);

	return rc;
}

/**
 * Enabled/Disabled Aggrgation strategy for Pool.
 */
static int
set_pool_reclaim_strategy(test_arg_t *arg, char *strategy)
{
	return dmg_pool_set_prop(arg->dmg_config, "reclaim",
				 strategy, arg->pool.pool_uuid);
}

/**
 * Very basic test for overwrites in different transactions.
 */
static void
io_overwrite_small(void **state, daos_obj_id_t oid)
{
	/* This test is disabled because it doesn't work with the incarnation
	 * log.  It's a happy accident that it works now.   We don't support
	 * multiple updates in the same transaction because DTX allocates a
	 * new DTX entry for the same epoch.  The incarnation log rejects this.
	 * It will be fixed when distributed transactions are fully implemented.
	 */
#if 0
	test_arg_t	*arg = *state;
	struct ioreq	 req;
	daos_size_t	 size;
	char		 ow_buf[] = "DAOS";
	char		 fbuf[] = "DAOS";
	int		 i;
	daos_handle_t	 th1, th2;

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	size = strlen(ow_buf);

	print_message("Test: small overwrite (rec size: %lu)\n", size);

	daos_tx_open(arg->coh, &th1, NULL);
	for (i = 0; i < size; i++)
		insert_single("d", "a", i, &ow_buf[i], 1, th1, &req);

	daos_tx_open(arg->coh, &th2, NULL);
	for (i = 0; i < size; i++) {
		ow_buf[i] += 32;
		insert_single("d", "a", i, &ow_buf[i], 1, th2, &req);
	}

	memset(fbuf, 0, sizeof(fbuf));
	for (i = 0; i < size; i++)
		lookup_single("d", "a", i, &fbuf[i], 1, th2, &req);
	print_message("fbuf = %s\n", fbuf);
	assert_string_equal(fbuf, ow_buf);

	for (i = 0; i < size; i++) {
		ow_buf[i] -= 32;
		lookup_single("d", "a", i, &fbuf[i], 1, th1, &req);
	}
	print_message("fbuf = %s\n", fbuf);
	assert_string_equal(fbuf, ow_buf);

	daos_tx_close(th1, NULL);
	daos_tx_close(th2, NULL);
	ioreq_fini(&req);
#endif
}

/**
 * Test mixed SCM & NVMe overwrites in different transactions with a large
 * record size. Iod size is needed for insert/lookup since the same akey is
 * being used.
 * Adding Pool size verification to check <4K size writes to SCM and >4K to
 * NVMe.
 */
static void
io_overwrite_large(void **state, daos_obj_id_t oid)
{
	test_arg_t	*arg0 = *state;
	test_arg_t	*arg = NULL;
	struct ioreq	 req;
	char		*ow_buf;
	char		*fbuf;
	const char	 dkey[] = "ep_ow_large dkey";
	const char	 akey[] = "ep_ow_large akey";
	daos_size_t	 overwrite_sz;
	daos_size_t	 size = 12 * 1024; /* record size */
	int		 buf_idx; /* overwrite buffer index */
	int		 rx_nr; /* number of record extents */
	int		 rec_idx = 0; /* index for next insert */
	int		 i;
	int		 rc;
	daos_pool_info_t pinfo;
	daos_size_t	 nvme_initial_size;
	daos_size_t	 nvme_current_size;
	char		*aggr_disabled = "disabled";
	char		*aggr_set_time = "time";

	rc = test_setup((void **)&arg, SETUP_CONT_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

	/* Disabled Pool Aggrgation */
	rc = set_pool_reclaim_strategy(arg, aggr_disabled);
	assert_rc_equal(rc, 0);
	/**
	 * set_pool_reclaim_strategy() to disable aggregation
	 * assumes all aggregation ULTs on all servers taking
	 * effect immediately, this may not be the case.
	 * Adding delay so that ULTs finish the round of aggregation.
	 */
	sleep(10);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/* Alloc and set buffer to be a string of all uppercase letters */
	D_ALLOC(ow_buf, size);
	assert_non_null(ow_buf);
	dts_buf_render_uppercase(ow_buf, size);
	/* Alloc the fetch buffer */
	D_ALLOC(fbuf, size);
	assert_non_null(fbuf);
	memset(fbuf, 0, size);

	/* Overwrite a variable number of chars to lowercase in different txs*/
	print_message("Test: large overwrite (rec size: %lu)\n", size);

	/* Set and verify the full initial string in first transaction */
	rx_nr = size / OW_IOD_SIZE;
	insert_single_with_rxnr(dkey, akey, /*idx*/0, ow_buf, OW_IOD_SIZE,
				rx_nr, DAOS_TX_NONE, &req);
	lookup_single_with_rxnr(dkey, akey, /*idx*/0, fbuf, OW_IOD_SIZE, size,
				DAOS_TX_NONE, &req);
	assert_memory_equal(ow_buf, fbuf, size);

	/*Get the initial pool size after writing first transaction*/
	rc = pool_storage_info(arg, &pinfo);
	assert_rc_equal(rc, 0);
	nvme_initial_size = pinfo.pi_space.ps_space.s_free[1];

	/**
	 * Mixed SCM & NVMe overwrites. 3k, 4k, and 5k overwrites for a 12k
	 * record.
	 */
	for (buf_idx = 0, rx_nr = 3; buf_idx < size; buf_idx += overwrite_sz) {
		overwrite_sz = OW_IOD_SIZE * rx_nr;
		if (overwrite_sz > size)
			break;
		memset(fbuf, 0, size);

		/**
		 * Overwrite buffer will always start at the previously
		 * overwritten index.
		 */
		for (i = buf_idx; i < overwrite_sz + buf_idx; i++)
			ow_buf[i] += 32; /* overwrite to lowercase */

		/**
		 * Insert overwrite in a new transaction. Overwrite will be an
		 * increasing number of rec extents (rx_nr) with the same
		 * iod_size, all inserted contiguously at the next index.
		 */
		insert_single_with_rxnr(dkey, akey, rec_idx, ow_buf + buf_idx,
					OW_IOD_SIZE, rx_nr, DAOS_TX_NONE,
					&req);
		/* Fetch entire record with new overwrite for comparison */
		lookup_single_with_rxnr(dkey, akey, /*idx*/0, fbuf, OW_IOD_SIZE,
					size, DAOS_TX_NONE, &req);
		assert_memory_equal(ow_buf, fbuf, size);
		/* Print message upon successful overwrite */
		print_message("overwrite size:%lu\n", overwrite_sz);

		rec_idx += rx_nr; /* next index for insert/lookup */
		/* Increment next overwrite size by 1 record extent */
		rx_nr++;

		/*Verify the SCM/NVMe Pool Free size based on transfer size*/
		rc = pool_storage_info(arg, &pinfo);
		assert_rc_equal(rc, 0);
		nvme_current_size = pinfo.pi_space.ps_space.s_free[1];
		if (overwrite_sz < 4096) {
		/*NVMe Size should not be changed as overwrite_sz is <4K*/
			if (nvme_initial_size != nvme_current_size) {
				fail_msg("Observed Value= %"
				PRIu64", & Expected Value =%"PRIu64"",
				nvme_current_size, nvme_initial_size);
			}
		} else {
		/*NVMe_Free_Size should decrease overwrite_sz is >4K*/
			if (nvme_current_size > nvme_initial_size -
				overwrite_sz) {
				fail_msg("\nNVMe_current_size =%"
					PRIu64 " > NVMe_initial_size = %"
					PRIu64 "- overwrite_sz =%" PRIu64 "",
					nvme_current_size, nvme_initial_size,
					overwrite_sz);
			}
		}
		nvme_initial_size = pinfo.pi_space.ps_space.s_free[1];
	}

	/* Enabled Pool Aggrgation */
	rc = set_pool_reclaim_strategy(arg, aggr_set_time);
	assert_rc_equal(rc, 0);

	D_FREE(fbuf);
	D_FREE(ow_buf);
	ioreq_fini(&req);
	test_teardown((void **)&arg);
}

/**
 * Very basic test for full overwrite in separate transactions for both large
 * and small record sizes.
 */
static void
io_overwrite_full(void **state, daos_obj_id_t oid, daos_size_t size)
{
	test_arg_t	*arg = *state;
	struct ioreq	 req;
	char		*ow_buf;
	char		*fbuf;
	char		 dkey[25];
	char		 akey[25];
	int		 i;

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	sprintf(dkey, "ep_ow_full dkey_%d", (int)size);
	sprintf(akey, "ep_ow_full akey_%d", (int)size);

	/* Alloc and set buffer to be a string of all uppercase letters */
	D_ALLOC(ow_buf, size);
	assert_non_null(ow_buf);
	dts_buf_render_uppercase(ow_buf, size);
	/* Alloc the fetch buffer */
	D_ALLOC(fbuf, size);
	assert_non_null(fbuf);
	memset(fbuf, 0, size);

	print_message("Test: full overwrite (rec size: %lu)\n", size);

	/* Set and verify the full initial string */
	insert_single_with_rxnr(dkey, akey, /*idx*/0, ow_buf, /*iod_size*/size,
				/*rx_nr*/1, DAOS_TX_NONE, &req);
	lookup_single_with_rxnr(dkey, akey, /*idx*/0, fbuf, /*iod_size*/size,
				size, DAOS_TX_NONE, &req);
	assert_memory_equal(ow_buf, fbuf, size);

	/* Overwrite the entire original buffer to all lowercase chars */
	for (i = 0; i < size; i++)
		ow_buf[i] += 32;

	memset(fbuf, 0, size);
	/* Insert full record overwrite */
	insert_single_with_rxnr(dkey, akey, /*idx*/0, ow_buf, /*iod_size*/size,
				/*rx_nr*/1, DAOS_TX_NONE, &req);
	/* Fetch entire record with new overwrite for comparison */
	lookup_single_with_rxnr(dkey, akey, /*idx*/0, fbuf, /*iod_size*/size,
				size, DAOS_TX_NONE, &req);
	assert_memory_equal(ow_buf, fbuf, size);
	/* Print message upon successful overwrite */
	print_message("overwrite size: %lu\n", size);

	D_FREE(fbuf);
	D_FREE(ow_buf);
	ioreq_fini(&req);
}

/**
 * Test overwrite in both different transactions and full overwrite in the same
 * transaction.
 */
static void
io_overwrite(void **state)
{
	daos_obj_id_t	 oid;
	test_arg_t	*arg = *state;

	/** choose random object */
	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);

	/* Full overwrite of a SCM record */
	io_overwrite_full(state, oid, IO_SIZE_SCM);

	/* Full overwrite of an NVMe record */
	io_overwrite_full(state, oid, IO_SIZE_NVME);

	/* Small 'DAOS' buffer used to show 1 char overwrites */
	io_overwrite_small(state, oid);

	/** Large record with mixed SCM/NVMe overwrites */
	io_overwrite_large(state, oid);
}

static void
io_rewritten_array_with_mixed_size(void **state)
{
	test_arg_t		*arg0 = *state;
	test_arg_t		*arg = NULL;
	struct ioreq		req;
	daos_obj_id_t		oid;
	daos_pool_info_t	pinfo;
	char			*ow_buf;
	char			*fbuf;
	const char		dkey[] = "dkey";
	const char		akey[] = "akey";
	daos_size_t		size = 4 * 1024; /* record size */
	int			buf_idx; /* overwrite buffer index */
	int			rx_nr; /* number of record extents */
	int			record_set;
	int			rc;
	int			test_run_time = 0;
	int			total_run_time = 20;
	daos_size_t		nvme_initial_size;
	daos_size_t		nvme_current_size;
	char			*aggr_disabled = "disabled";
	char			*aggr_set_time = "time";

	rc = test_setup((void **)&arg, SETUP_CONT_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

	/* choose random object */
	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/* Alloc and set buffer to be a string*/
	D_ALLOC(ow_buf, size);
	assert_non_null(ow_buf);
	dts_buf_render(ow_buf, size);
	/* Alloc the fetch buffer */
	D_ALLOC(fbuf, size);
	assert_non_null(fbuf);
	memset(fbuf, 0, size);

	/* Disabled Pool Aggregation */
	rc = set_pool_reclaim_strategy(arg, aggr_disabled);
	assert_rc_equal(rc, 0);

	/* Get the pool info at the beginning */
	rc = pool_storage_info(arg, &pinfo);
	assert_rc_equal(rc, 0);
	nvme_initial_size = pinfo.pi_space.ps_space.s_free[1];

	/* Set and verify the full initial string in first transaction */
	rx_nr = size / OW_IOD_SIZE;
	/* Insert the initial 4K record which will go through NVMe */
	insert_single_with_rxnr(dkey, akey, /*idx*/0, ow_buf, OW_IOD_SIZE,
				rx_nr, DAOS_TX_NONE, &req);
	/* Lookup the initial 4K record */
	lookup_single_with_rxnr(dkey, akey, /*idx*/0, fbuf, OW_IOD_SIZE, size,
				DAOS_TX_NONE, &req);
	/* Verify the 4K data */
	assert_memory_equal(ow_buf, fbuf, size);

	/**
	*Get the pool storage information
	*/
	rc = pool_storage_info(arg, &pinfo);
	assert_rc_equal(rc, 0);
	nvme_current_size = pinfo.pi_space.ps_space.s_free[1];

	/**
	* Verify data written on NVMe by comparing the NVMe Free size.
	*/
	if (nvme_current_size > nvme_initial_size - size) {
		fail_msg("\nNVMe_current_size =%"
			PRIu64 " > NVMe_initial_size = %"
			PRIu64 "- written size =%" PRIu64 "",
			nvme_current_size, nvme_initial_size, size);
	}
	nvme_initial_size = pinfo.pi_space.ps_space.s_free[1];

	for (record_set = 0, buf_idx = 0; record_set < 10; record_set++) {
		buf_idx += 20;
		memset(fbuf, 0, size);
		/* Change Two bytes value to original array*/
		ow_buf[buf_idx + 1] = 48;
		ow_buf[buf_idx + 2] = 49;

		/* Re-write the same array with modified Two values*/
		insert_single_with_rxnr(dkey, akey, /*idx*/0, ow_buf,
			OW_IOD_SIZE, 1, DAOS_TX_NONE, &req);

		/*Read and verify the data with Two updated values*/
		lookup_single_with_rxnr(dkey, akey, /*idx*/0, fbuf,
			OW_IOD_SIZE, size, DAOS_TX_NONE, &req);
		assert_memory_equal(ow_buf, fbuf, size);

		/*Verify the pool size*/
		rc = pool_storage_info(arg, &pinfo);
		assert_rc_equal(rc, 0);
		nvme_current_size = pinfo.pi_space.ps_space.s_free[1];

		/**
		* Data written on SCM so NVMe free size should not change.
		* However VEA free (called from aggregation) usually put the
		* freed extent in an aging buffer for 10 seconds
		* so sleep two seconds before next record update.
		*/

		if (nvme_current_size != nvme_initial_size) {
			print_message("Size verification: Partial FAIL "
				"record set=%d Sleep for 2 seconds",
				record_set);
			sleep(2);
			test_run_time = test_run_time + 2;
		} else
			print_message("Size verification: PASS for "
				"record set=%d\n", record_set);

		/**
		* Fail the test if the test run time is higher than
		* total timeout (20 seconds) while writing all 10 records.
		*/

		if (test_run_time >= total_run_time)
			fail_msg("NVMe Free size should not be changed "
				"after writing all %d records to SCM",
				record_set);

		nvme_initial_size = pinfo.pi_space.ps_space.s_free[1];
	}

	/* Enabled Pool Aggregation */
	rc = set_pool_reclaim_strategy(arg, aggr_set_time);
	assert_rc_equal(rc, 0);

	D_FREE(fbuf);
	D_FREE(ow_buf);
	ioreq_fini(&req);
	test_teardown((void **)&arg);
}

/** i/o to variable idx offset */
static void
io_var_idx_offset(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_off_t	 offset;

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	for (offset = (UINT64_MAX >> 1); offset > 0; offset >>= 8) {
		char buf[10];


		print_message("idx offset: %lu\n", offset);

		/** Insert */
		insert_single("var_idx_off_d", "var_idx_off_a", offset, "data",
		       strlen("data") + 1, DAOS_TX_NONE, &req);

		/** Lookup */
		memset(buf, 0, 10);
		lookup_single("var_idx_off_d", "var_idx_off_a", offset,
			      buf, 10, DAOS_TX_NONE, &req);
		assert_int_equal(req.iod[0].iod_size, strlen(buf) + 1);

		/** Verify data consistency */
		assert_string_equal(buf, "data");
	}

	ioreq_fini(&req);

}

/** i/o to variable akey size */
static void
io_var_akey_size(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_size_t	 size;
	const int	 max_size = 1 << 20;
	char		*key;

	/** akey not supported yet */
	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	D_ALLOC(key, max_size + 1);
	assert_non_null(key);
	memset(key, 'a', max_size);

	for (size = 1; size <= max_size; size <<= 2) {
		char buf[10];

		print_message("akey size: %lu\n", size);

		/** Insert */
		key[size] = '\0';
		insert_single("var_akey_size_d", key, 0, "data",
			      strlen("data") + 1, DAOS_TX_NONE, &req);

		/** Lookup */
		memset(buf, 0, 10);
		lookup_single("var_akey_size_d", key, 0, buf,
			      10, DAOS_TX_NONE, &req);
		assert_int_equal(req.iod[0].iod_size, strlen("data") + 1);

		/** Verify data consistency */
		assert_string_equal(buf, "data");
		key[size] = 'b';
	}

	D_FREE(key);
	ioreq_fini(&req);
}

/** i/o to variable dkey size */
static void
io_var_dkey_size(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_size_t	 size;
	const int	 max_size = 1 << 20;
	char		*key;

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	D_ALLOC(key, max_size + 1);
	assert_non_null(key);
	memset(key, 'a', max_size);

	for (size = 1; size <= max_size; size <<= 2) {
		char buf[10];

		print_message("dkey size: %lu\n", size);

		/** Insert */
		key[size] = '\0';
		insert_single(key, "var_dkey_size_a", 0, "data",
			      strlen("data") + 1, DAOS_TX_NONE, &req);

		/** Lookup */
		memset(buf, 0, 10);
		lookup_single(key, "var_dkey_size_a", 0, buf, 10,
			      DAOS_TX_NONE, &req);
		assert_int_equal(req.iod[0].iod_size, strlen("data") + 1);

		/** Verify data consistency */
		assert_string_equal(buf, "data");
		key[size] = 'b';
	}

	D_FREE(key);
	ioreq_fini(&req);
}

/**
 * Test I/O and data verification with variable unaligned record sizes for both
 * NVMe and SCM.
 */
static void
io_var_rec_size(void **state)
{
	test_arg_t	*arg = *state;
	uint64_t	 dkey_num;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_size_t	 size;
	const int	 max_size = 1U << 20;
	char		*fetch_buf;
	char		*update_buf;

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	dkey_num = rand();

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	D_ALLOC(fetch_buf, max_size);
	assert_non_null(fetch_buf);

	D_ALLOC(update_buf, max_size);
	assert_non_null(update_buf);

	dts_buf_render(update_buf, max_size);

	for (size = 1; size <= max_size; size <<= 1, dkey_num++) {
		char dkey[30];

		/**
		 * Adjust size to be unaligned, always include 1 byte test
		 * (minimal supported size).
		 */
		size += (size == 1) ? 0 : (rand() % 10);
		print_message("Record size: %lu val: \'%c\' dkey: %lu\n",
			      size, update_buf[0], dkey_num);

		/** Insert */
		sprintf(dkey, DF_U64, dkey_num);
		insert_single(dkey, "var_rec_size_a", 0, update_buf,
			      size, DAOS_TX_NONE, &req);

		/** Lookup */
		memset(fetch_buf, 0, max_size);
		lookup_single(dkey, "var_rec_size_a", 0, fetch_buf,
			      max_size, DAOS_TX_NONE, &req);
		assert_int_equal(req.iod[0].iod_size, size);

		/** Verify data consistency */
		assert_memory_equal(update_buf, fetch_buf, size);
	}

	D_FREE(update_buf);
	D_FREE(fetch_buf);
	ioreq_fini(&req);
}

/**
 * Test update/fetch with data verification of varying size and IOD type.
 * Size is either small I/O to SCM or larger (>=4k) I/O to NVMe, and IOD
 * type is either array or single value.
 */
void
io_simple_internal(void **state, daos_obj_id_t oid, unsigned int size,
		   daos_iod_type_t iod_type, const char dkey[],
		   const char akey[])
{
	test_arg_t	*arg = *state;
	struct ioreq	 req;
	char		*fetch_buf;
	char		*update_buf;

	ioreq_init(&req, arg->coh, oid, iod_type, arg);

	D_ALLOC(fetch_buf, size);
	assert_non_null(fetch_buf);
	D_ALLOC(update_buf, size);
	assert_non_null(update_buf);
	dts_buf_render(update_buf, size);

	/** Insert */
	insert_single(dkey, akey, 0, update_buf, size, DAOS_TX_NONE, &req);

	/** Lookup */
	memset(fetch_buf, 0, size);
	lookup_single(dkey, akey, 0, fetch_buf, size, DAOS_TX_NONE, &req);

	/** Verify data consistency */
	if (!daos_obj_is_echo(oid)) {
		assert_int_equal(req.iod[0].iod_size, size);
		assert_memory_equal(update_buf, fetch_buf, size);
	}
	punch_dkey(dkey, DAOS_TX_NONE, &req);
	D_FREE(update_buf);
	D_FREE(fetch_buf);
	ioreq_fini(&req);
}

/**
 * Very basic update/fetch with data verification with varying record size and
 * IOD type.
 */
static void
io_simple(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	print_message("Insert(e=0)/lookup(e=0)/verify simple kv record\n");

	/** Test first for SCM, then on NVMe with record size > 4k */
	print_message("DAOS_IOD_ARRAY:SCM\n");
	io_simple_internal(state, oid, IO_SIZE_SCM, DAOS_IOD_ARRAY,
			   "io_simple_scm_array dkey",
			   "io_simple_scm_array akey");
	print_message("DAOS_IOD_ARRAY:NVMe\n");
	io_simple_internal(state, oid, IO_SIZE_NVME, DAOS_IOD_ARRAY,
			   "io_simple_nvme_array dkey",
			   "io_simple_nvme_array akey");
	print_message("DAOS_IOD_SINGLE:SCM\n");
	io_simple_internal(state, oid, IO_SIZE_SCM, DAOS_IOD_SINGLE,
			   "io_simple_scm_single dkey",
			   "io_simple_scm_single akey");
	print_message("DAOS_IOD_SINGLE:NVMe\n");
	io_simple_internal(state, oid, IO_SIZE_NVME, DAOS_IOD_SINGLE,
			   "io_simple_nvme_single dkey",
			   "io_simple_nvme_single akey");
}

int
enumerate_dkey(daos_handle_t th, uint32_t *number, daos_key_desc_t *kds,
	       daos_anchor_t *anchor, void *buf, daos_size_t len,
	       struct ioreq *req)
{
	int rc;

	ioreq_sgl_simple_set(req, &buf, &len, 1);
	/** execute fetch operation */
	rc = daos_obj_list_dkey(req->oh, th, number, kds, req->sgl, anchor,
				req->arg->async ? &req->ev : NULL);
	if (req->arg->async) {
		bool ev_flag;

		assert_rc_equal(rc, 0);
		rc = daos_event_test(&req->ev, DAOS_EQ_WAIT, &ev_flag);
		assert_rc_equal(rc, 0);
		assert_int_equal(ev_flag, true);
		rc = req->ev.ev_error;
	}

	if (rc != -DER_KEY2BIG)
		assert_rc_equal(rc, 0);

	return rc;
}

int
enumerate_akey(daos_handle_t th, char *dkey, uint32_t *number,
	       daos_key_desc_t *kds, daos_anchor_t *anchor, void *buf,
	       daos_size_t len, struct ioreq *req)
{
	int rc;

	ioreq_sgl_simple_set(req, &buf, &len, 1);
	ioreq_dkey_set(req, dkey);
	/** execute fetch operation */
	rc = daos_obj_list_akey(req->oh, th, &req->dkey, number, kds,
				req->sgl, anchor,
				req->arg->async ? &req->ev : NULL);
	if (req->arg->async) {
		bool ev_flag;

		assert_rc_equal(rc, 0);
		rc = daos_event_test(&req->ev, DAOS_EQ_WAIT, &ev_flag);
		assert_rc_equal(rc, 0);
		assert_int_equal(ev_flag, true);
		rc = req->ev.ev_error;
	}

	if (rc != -DER_KEY2BIG)
		assert_rc_equal(rc, 0);

	return rc;
}

void
enumerate_rec(daos_handle_t th, char *dkey, char *akey,
	      daos_size_t *size, uint32_t *number, daos_recx_t *recxs,
	      daos_epoch_range_t *eprs, daos_anchor_t *anchor, bool incr,
	      struct ioreq *req)
{
	int rc;

	ioreq_dkey_set(req, dkey);
	ioreq_akey_set(req, akey);
	rc = daos_obj_list_recx(req->oh, th, &req->dkey, &req->akey,
				size, number, recxs, eprs, anchor, incr,
				req->arg->async ? &req->ev : NULL);
	assert_rc_equal(rc, 0);

	if (req->arg->async) {
		bool ev_flag;

		rc = daos_event_test(&req->ev, DAOS_EQ_WAIT, &ev_flag);
		assert_rc_equal(rc, 0);
		assert_int_equal(ev_flag, true);
		assert_int_equal(req->ev.ev_error, 0);
	}
}

#define ENUM_KEY_BUF		32 /* size of each dkey/akey */
#define ENUM_LARGE_KEY_BUF	(512 * 1024) /* 512k large key */
#define ENUM_KEY_REC_NR		1000 /* number of keys/records to insert */
#define ENUM_PRINT		100 /* print every 100th key/record */
#define ENUM_DESC_NR		5 /* number of keys/records returned by enum */
#define ENUM_DESC_BUF		512 /* all keys/records returned by enum */
#define ENUM_IOD_SIZE		1024 /* used for mixed record enumeration */
#define ENUM_NR_NVME		5 /* consecutive rec exts in an NVMe extent */
#define ENUM_NR_SCM		2 /* consecutive rec exts in an SCM extent */

static void
insert_records(daos_obj_id_t oid, struct ioreq *req, char *data_buf,
	       uint64_t start_idx)
{
	uint64_t	 idx;
	int		 num_rec_exts;
	int		 i;

	print_message("Insert %d records from index "DF_U64
		      " under the same key (obj:"DF_OID")\n", ENUM_KEY_REC_NR,
		      start_idx, DP_OID(oid));
	idx = start_idx; /* record extent index */
	for (i = 0; i < ENUM_KEY_REC_NR; i++) {
		/* insert alternating SCM (2k) and NVMe (5k) records */
		if (i % 2 == 0)
			num_rec_exts = ENUM_NR_SCM; /* rx_nr=2 for SCM test */
		else
			num_rec_exts = ENUM_NR_NVME; /* rx_nr=5 for NVMe test */
		insert_single_with_rxnr("d_key", "a_rec", idx, data_buf,
					ENUM_IOD_SIZE, num_rec_exts,
					DAOS_TX_NONE, req);
		idx += num_rec_exts;
		/* Prevent records coalescing on aggregation */
		idx += 1;
	}
}

static int
iterate_records(struct ioreq *req, char *dkey, char *akey, int iod_size)
{
	daos_anchor_t	anchor;
	daos_anchor_t	anchor_des;
	int		key_nr;
	int		i;
	uint32_t	number;

	/** Enumerate all mixed NVMe and SCM records */
	key_nr = 0;
	memset(&anchor, 0, sizeof(anchor));
	memset(&anchor_des, 0, sizeof(anchor));
	while (!daos_anchor_is_eof(&anchor)) {
		daos_epoch_range_t	eprs[5];
		daos_recx_t		recxs[5];
		daos_size_t		size;

		number = 5;
		enumerate_rec(DAOS_TX_NONE, dkey, akey, &size,
			      &number, recxs, eprs, &anchor, true, req);
		if (number == 0)
			continue;

		for (i = 0; i < (number - 1); i++) {
			assert_true(size == iod_size);
			assert_true(recxs[i].rx_idx < recxs[i+1].rx_idx);
			/* Print a subset of enumerated records */
			if ((i + key_nr) % ENUM_PRINT != 0)
				continue;
			print_message("i:%d iod_size:%d rx_nr:%d, rx_idx:%d\n",
				      i + key_nr, (int)size,
				      (int)recxs[i].rx_nr,
				      (int)recxs[i].rx_idx);
			i++; /* print the next record to see both rec sizes */
			print_message("i:%d iod_size:%d rx_nr:%d, rx_idx:%d\n",
				      i + key_nr, (int)size,
				      (int)recxs[i].rx_nr,
				      (int)recxs[i].rx_idx);
		}

		number = 5;
		enumerate_rec(DAOS_TX_NONE, dkey, akey, &size,
			      &number, recxs, eprs, &anchor_des, false, req);
		if (number == 0)
			continue;
		for (i = 0; i < (number - 1); i++) {
			assert_true(recxs[i].rx_idx > recxs[i+1].rx_idx);
			/* Print a subset of enumerated records */
			if ((i + key_nr) % ENUM_PRINT != 0)
				continue;
			print_message("i:%d iod_size:%d rx_nr:%d, rx_idx:%d\n",
				      i + key_nr, (int)size,
				      (int)recxs[i].rx_nr,
				      (int)recxs[i].rx_idx);
		}

		key_nr += number;
	}

	return key_nr;
}

#define ENUM_BUF_SIZE (128 * 1024)
/** very basic enumerate */
static void
enumerate_simple(void **state)
{
	test_arg_t	*arg = *state;
	char		*small_buf;
	char		*buf;
	daos_size_t	 buf_len;
	char		*ptr;
	char		 key[ENUM_KEY_BUF];
	char		*large_key = NULL;
	char		*large_buf = NULL;
	char		*data_buf;
	daos_key_desc_t  kds[ENUM_DESC_NR];
	daos_anchor_t	 anchor;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	uint32_t	 number;
	int		 key_nr;
	int		 i;
	int		 rc;

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	D_ALLOC(small_buf, ENUM_DESC_BUF);
	D_ALLOC(large_key, ENUM_LARGE_KEY_BUF);
	memset(large_key, 'L', ENUM_LARGE_KEY_BUF);
	large_key[ENUM_LARGE_KEY_BUF - 1] = '\0';
	D_ALLOC(large_buf, ENUM_LARGE_KEY_BUF * 2);

	D_ALLOC(data_buf, ENUM_BUF_SIZE);
	assert_non_null(data_buf);
	dts_buf_render(data_buf, ENUM_BUF_SIZE);

	/**
	 * Insert 1000 dkey records, all with the same key value and the same
	 * akey.
	 */
	print_message("Insert %d dkeys (obj:"DF_OID")\n", ENUM_KEY_REC_NR,
		      DP_OID(oid));
	for (i = 0; i < ENUM_KEY_REC_NR; i++) {
		sprintf(key, "%d", i);
		if (i == ENUM_KEY_REC_NR/3) {
			/* Insert one large dkey (512K "L's") */
			print_message("Insert (i=%d) dkey=LARGE_KEY\n", i);
			insert_single(large_key, "a_key", 0, "data",
				      strlen("data") + 1, DAOS_TX_NONE, &req);
		} else {
			/* Insert dkeys 0-999 */
			insert_single(key, "a_key", 0, "data",
				      strlen("data") + 1, DAOS_TX_NONE, &req);
		}
	}

	/* Enumerate all dkeys */
	print_message("Enumerate dkeys\n");
	memset(&anchor, 0, sizeof(anchor));
	for (number = ENUM_DESC_NR, key_nr = 0;
	     !daos_anchor_is_eof(&anchor);
	     number = ENUM_DESC_NR) {
		buf = small_buf;
		buf_len = ENUM_DESC_BUF;
		memset(buf, 0, buf_len);
		/**
		 * Return an array of "number" dkeys to buf, using "kds" for
		 * index to get the dkey.
		 */
		rc = enumerate_dkey(DAOS_TX_NONE, &number, kds, &anchor, buf,
				    buf_len, &req);
		if (rc == -DER_KEY2BIG) {
			/**
			 * Retry dkey enumeration with a larger buffer since
			 * one of the returned key descriptors is the large key.
			 */
			print_message("Ret:-DER_KEY2BIG, len:"DF_U64"\n",
				      kds[0].kd_key_len);
			assert_int_equal((int)kds[0].kd_key_len,
					 ENUM_LARGE_KEY_BUF - 1);
			buf = large_buf;
			buf_len = ENUM_LARGE_KEY_BUF * 2;
			rc = enumerate_dkey(DAOS_TX_NONE, &number, kds,
					    &anchor, buf, buf_len, &req);
		}
		assert_rc_equal(rc, 0);

		if (number == 0)
			continue; /* loop should break for EOF */

		for (ptr = buf, i = 0; i < number; i++) {
			if (kds[i].kd_key_len > ENUM_KEY_BUF) {
				print_message("dkey:'%c...' len:%d\n", ptr[0],
					      (int)kds[i].kd_key_len);
			} else if ((i + key_nr) % ENUM_PRINT == 0) {
				/* Print a subset of enumerated dkeys */
				snprintf(key, kds[i].kd_key_len + 1, "%s", ptr);
				print_message("i:%d dkey:%s len:%d\n",
					      i + key_nr, key,
					      (int)kds[i].kd_key_len);
			}
			ptr += kds[i].kd_key_len;
		}
		key_nr += number;
	}
	/* Confirm the number of dkeys enumerated equal the number inserted */
	assert_int_equal(key_nr, ENUM_KEY_REC_NR);

	/**
	 * Insert 1000 akey records, all with the same key value and the same
	 * dkey.
	 */
	print_message("Insert %d akeys (obj:"DF_OID")\n", ENUM_KEY_REC_NR,
		      DP_OID(oid));
	for (i = 0; i < ENUM_KEY_REC_NR; i++) {
		sprintf(key, "%d", i);
		if (i == ENUM_KEY_REC_NR/7) {
			/* Insert one large akey (512K "L's") */
			print_message("Insert (i=%d) akey=LARGE_KEY\n", i);
			insert_single("d_key", large_key, 0, "data",
				      strlen("data") + 1, DAOS_TX_NONE, &req);
		} else {
			/* Insert akeys 0-999 */
			insert_single("d_key", key, 0, "data",
				      strlen("data") + 1, DAOS_TX_NONE, &req);
		}
	}

	/* Enumerate all akeys */
	print_message("Enumerate akeys\n");
	memset(&anchor, 0, sizeof(anchor));
	for (number = ENUM_DESC_NR, key_nr = 0;
	     !daos_anchor_is_eof(&anchor);
	     number = ENUM_DESC_NR) {
		buf = small_buf;
		buf_len = ENUM_DESC_BUF;
		memset(buf, 0, buf_len);
		/**
		 * Return an array of "number" akeys to buf, using "kds" for
		 * index to get the akey.
		 */
		rc = enumerate_akey(DAOS_TX_NONE, "d_key", &number, kds,
				    &anchor, buf, buf_len, &req);
		if (rc == -DER_KEY2BIG) {
			/**
			 * Retry akey enumeration with a larger buffer since one
			 * of the returned key descriptors is the large key.
			 */
			print_message("Ret:-DER_KEY2BIG, len:"DF_U64"\n",
				      kds[0].kd_key_len);
			assert_int_equal((int)kds[0].kd_key_len,
					 ENUM_LARGE_KEY_BUF - 1);
			buf = large_buf;
			buf_len = ENUM_LARGE_KEY_BUF * 2;
			rc = enumerate_akey(DAOS_TX_NONE, "d_key", &number,
					    kds, &anchor, buf, buf_len, &req);
		}
		assert_rc_equal(rc, 0);

		if (number == 0)
			break; /* loop should break for EOF */

		for (ptr = buf, i = 0; i < number; i++) {
			if (kds[i].kd_key_len > ENUM_KEY_BUF) {
				print_message("akey:'%c...' len:%d\n", ptr[0],
					      (int)kds[i].kd_key_len);
			} else if ((i + key_nr) % ENUM_PRINT == 0) {
				/* Print a subset of enumerated akeys */
				snprintf(key, kds[i].kd_key_len + 1, "%s", ptr);
				print_message("i:%d akey:%s len:%d\n",
					      i + key_nr, key,
					     (int)kds[i].kd_key_len);
			}
			ptr += kds[i].kd_key_len;
		}
		key_nr += number;
	}
	/* Confirm the number of akeys enumerated equal the number inserted */
	assert_int_equal(key_nr, ENUM_KEY_REC_NR);

	/**
	 * Insert N mixed NVMe and SCM records, all with same dkey and akey.
	 */
	insert_records(oid, &req, data_buf, 0);
	key_nr = iterate_records(&req, "d_key", "a_rec", ENUM_IOD_SIZE);
	assert_int_equal(key_nr, ENUM_KEY_REC_NR);

	/**
	 * Insert N mixed NVMe and SCM records starting at offset 1,
	 * all with same dkey and akey.
	 */
	insert_records(oid, &req, data_buf, 1);
	key_nr = iterate_records(&req, "d_key", "a_rec", ENUM_IOD_SIZE);
	/** Records could be merged with previous updates by aggregation */
	print_message("key_nr = %d\n", key_nr);

	/**
	 * Insert N mixed NVMe and SCM records starting at offset 2,
	 * all with same dkey and akey.
	 */
	insert_records(oid, &req, data_buf, 2);
	key_nr = iterate_records(&req, "d_key", "a_rec", ENUM_IOD_SIZE);
	/** Records could be merged with previous updates by aggregation */
	print_message("key_nr = %d\n", key_nr);

	for (i = 0; i < 10; i++)
		insert_single_with_rxnr("d_key", "a_lrec", i * 128 * 1024,
					data_buf, 1, 128 * 1024, DAOS_TX_NONE,
					&req);
	key_nr = iterate_records(&req, "d_key", "a_lrec", 1);
	print_message("key_nr = %d\n", key_nr);
	D_FREE(small_buf);
	D_FREE(large_buf);
	D_FREE(large_key);
	D_FREE(data_buf);
	/** XXX Verify kds */
	ioreq_fini(&req);
}

#define PUNCH_NUM_KEYS 5
#define PUNCH_IOD_SIZE 1024
#define PUNCH_SCM_NUM_EXTS 2 /* SCM 2k record */
#define PUNCH_NVME_NUM_EXTS 5 /* NVMe 5k record */
#define PUNCH_ENUM_NUM 2
/**
 * Test akey punch, dkey punch, record punch and object punch with mixed large
 * NVMe and small SCM record sizes. Verify punched keys with key
 * enumeration. Record enumeration is still under development, so for now verify
 * punched records with lookup only.
 */
static void
punch_simple_internal(void **state, daos_obj_id_t oid)
{
	test_arg_t	*arg = *state;
	struct ioreq	 req;
	uint32_t	 enum_num;
	daos_key_desc_t  kds[2];
	daos_anchor_t	 anchor_out;
	char		*buf;
	char		*dkeys[PUNCH_NUM_KEYS];
	char		*data_buf;
	char		*rec_fetch;
	char		*rec_verify;
	daos_size_t	 size;
	int		 num_rec_exts = 0;
	int		 total_keys = 0;
	int		 rc;
	int		 i;

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	D_ALLOC(data_buf, IO_SIZE_NVME);
	dts_buf_render(data_buf, IO_SIZE_NVME);
	D_ALLOC(buf, 512);

	/**
	 * Insert 1 record per akey at different dkeys. Record sizes are
	 * alternating SCM (2 consecutive extents = 2k), and NVME (5 consecutive
	 * record extents = 5k).
	 */
	print_message("Inserting records.\n");
	for (i = 0; i < PUNCH_NUM_KEYS; i++) {
		if (i % 2 == 0)
			num_rec_exts = PUNCH_SCM_NUM_EXTS;
		else
			num_rec_exts = PUNCH_NVME_NUM_EXTS;
		D_ASPRINTF(dkeys[i], "punch_simple_dkey%d", i);
		print_message("\tinsert dkey:%s, akey:'akey', rx_nr:%d\n",
			      dkeys[i], num_rec_exts);
		insert_single_with_rxnr(dkeys[i], "akey",/*idx*/ 0, data_buf,
					PUNCH_IOD_SIZE, num_rec_exts,
					DAOS_TX_NONE, &req);
	}
	/* Insert a few more unique akeys at the first dkey */
	num_rec_exts = PUNCH_NVME_NUM_EXTS;
	print_message("\tinsert dkey:%s, akey:'akey0', rx_nr:%d\n",
		      dkeys[0], num_rec_exts);
	insert_single_with_rxnr(dkeys[0], "akey0",/*idx*/ 0, data_buf,
				PUNCH_IOD_SIZE, num_rec_exts, DAOS_TX_NONE,
				&req);
	print_message("\tinsert dkey:%s, akey:'akey1', rx_nr:%d\n",
		      dkeys[0], num_rec_exts);
	insert_single_with_rxnr(dkeys[0], "akey1",/*idx*/ 0, data_buf,
				PUNCH_IOD_SIZE, num_rec_exts, DAOS_TX_NONE,
				&req);

	/**
	 * Punch records.
	 */
	print_message("Punch a few records:\n");
	num_rec_exts = PUNCH_NVME_NUM_EXTS;
	print_message("\tpunch dkey:%s, akey:'akey0', rx_nr:%d\n",
		      dkeys[0], num_rec_exts);
	punch_rec_with_rxnr(dkeys[0], "akey0", /*idx*/0, num_rec_exts,
			    DAOS_TX_NONE, &req);
	print_message("\tpunch dkey:%s, akey:'akey1', rx_nr:%d\n",
			dkeys[0], num_rec_exts);
	punch_rec_with_rxnr(dkeys[0], "akey1", /*idx*/0, num_rec_exts,
			    DAOS_TX_NONE, &req);
	/* punch non-exist long ext (full-stripe for EC) */
	punch_rec_with_rxnr(dkeys[0], "akey1", 1 << 20, 1 << 20, DAOS_TX_NONE, &req);


	/**
	 * Lookup and enumerate records for both punched and non-punched
	 * entries.
	 */
	print_message("Lookup & enumerate non-punched records:\n");
	for (i = 0; i < PUNCH_NUM_KEYS; i++) {
		if (i % 2 == 0)
			num_rec_exts = PUNCH_SCM_NUM_EXTS;
		else
			num_rec_exts = PUNCH_NVME_NUM_EXTS;
		size = PUNCH_IOD_SIZE * num_rec_exts;
		D_ALLOC(rec_fetch, size);

		/* Lookup all non-punched records */
		lookup_single_with_rxnr(dkeys[i], "akey", /*idx*/0, rec_fetch,
					PUNCH_IOD_SIZE, size, DAOS_TX_NONE,
					&req);
		assert_memory_equal(rec_fetch, data_buf, size);
		/* Enumerate all non-punched records */
		memset(&anchor_out, 0, sizeof(anchor_out));
		total_keys = 0;
		while (!daos_anchor_is_eof(&anchor_out)) {
			daos_epoch_range_t	eprs[PUNCH_ENUM_NUM];
			daos_recx_t		recxs[PUNCH_ENUM_NUM];

			enum_num = PUNCH_ENUM_NUM;
			enumerate_rec(DAOS_TX_NONE, dkeys[i], "akey", &size,
				      &enum_num, recxs, eprs, &anchor_out, true,
				      &req);
			total_keys += enum_num;
		}
		print_message("\tdkey:%s, akey:'akey', #rec:%d\n", dkeys[i],
			      total_keys);
		assert_int_equal(total_keys, 1);

		D_FREE(rec_fetch);
	}

	/* Lookup punched records, verify no data was fetched by lookup */
	print_message("Lookup & enumerate punched records:\n");
	num_rec_exts = PUNCH_NVME_NUM_EXTS;
	size = PUNCH_IOD_SIZE * num_rec_exts;
	D_ALLOC(rec_verify, size);
	dts_buf_render(rec_verify, size);
	D_ALLOC(rec_fetch, size);
	memcpy(rec_fetch, rec_verify, size);
	assert_memory_equal(rec_fetch, rec_verify, size);

	/* Lookup first punched record */
	lookup_single_with_rxnr(dkeys[0], "akey0", /*idx*/0, rec_fetch,
				/*iod_size*/0, size, DAOS_TX_NONE, &req);
	assert_memory_equal(rec_fetch, rec_verify, size);
	/* Enumerate first punched record*/
	memset(&anchor_out, 0, sizeof(anchor_out));
	total_keys = 0;
	while (!daos_anchor_is_eof(&anchor_out)) {
		daos_epoch_range_t	eprs[PUNCH_ENUM_NUM];
		daos_recx_t		recxs[PUNCH_ENUM_NUM];

		enum_num = PUNCH_ENUM_NUM;
		enumerate_rec(DAOS_TX_NONE, dkeys[0], "akey0", &size, &enum_num,
			      recxs, eprs, &anchor_out, true, &req);
		total_keys += enum_num;
	}
	print_message("\tdkey:%s, akey:'akey0', #rec:%d\n", dkeys[0],
		      total_keys);
	assert_int_equal(total_keys, 0);

	/* Lookup second punched record */
	lookup_single_with_rxnr(dkeys[0], "akey1", /*idx*/0, rec_fetch,
				/*iod_size*/0, size, DAOS_TX_NONE, &req);
	assert_memory_equal(rec_fetch, rec_verify, size);
	/* Enumerate second punched record */
	memset(&anchor_out, 0, sizeof(anchor_out));
	total_keys = 0;
	while (!daos_anchor_is_eof(&anchor_out)) {
		daos_epoch_range_t	eprs[PUNCH_ENUM_NUM];
		daos_recx_t		recxs[PUNCH_ENUM_NUM];

		enum_num = PUNCH_ENUM_NUM;
		enumerate_rec(DAOS_TX_NONE, dkeys[0], "akey1", &size, &enum_num,
			      recxs, eprs, &anchor_out, true, &req);
		total_keys += enum_num;
	}
	print_message("\tdkey:%s, akey:'akey1', #rec:%d\n", dkeys[0],
		      total_keys);
	assert_int_equal(total_keys, 0);

	D_FREE(rec_fetch);
	D_FREE(rec_verify);

	/**
	 * Punch akeys (along with all records) from object.
	 */
	print_message("Punch all akeys\n");
	for (i = 0; i < PUNCH_NUM_KEYS; i++)
		punch_akey(dkeys[i], "akey", DAOS_TX_NONE, &req);
	punch_akey(dkeys[0], "akey0", DAOS_TX_NONE, &req);
	punch_akey(dkeys[0], "akey1", DAOS_TX_NONE, &req);

	/* Enumerate akeys */
	print_message("Enumerate akeys:\n");
	for (i = 0; i < PUNCH_NUM_KEYS; i++) {
		memset(&anchor_out, 0, sizeof(anchor_out));
		memset(buf, 0, 512);
		total_keys = 0;
		while (!daos_anchor_is_eof(&anchor_out)) {
			enum_num = PUNCH_ENUM_NUM;
			rc = enumerate_akey(DAOS_TX_NONE, dkeys[i], &enum_num,
					    kds, &anchor_out, buf, 512, &req);
			assert_rc_equal(rc, 0);
			total_keys += enum_num;
		}
		print_message("\tdkey:%s, #akeys:%d\n", dkeys[i], total_keys);
		assert_int_equal(total_keys, 0);
	}

	/**
	 * Punch dkeys (along with all akeys) from object.
	 */
	print_message("Punch all dkeys\n");
	for (i = 0; i < PUNCH_NUM_KEYS; i++)
		punch_dkey(dkeys[i], DAOS_TX_NONE, &req);

	/* Enumerate dkeys */
	print_message("Enumerate dkeys:\n");
	memset(&anchor_out, 0, sizeof(anchor_out));
	memset(buf, 0, 512);
	total_keys = 0;
	while (!daos_anchor_is_eof(&anchor_out)) {
		enum_num = PUNCH_ENUM_NUM;
		rc = enumerate_dkey(DAOS_TX_NONE, &enum_num, kds, &anchor_out,
				    buf, 512, &req);
		assert_rc_equal(rc, 0);
		total_keys += enum_num;
	}
	print_message("\t#dkeys:%d\n", total_keys);
	assert_int_equal(total_keys, 0);

	/**
	 * Re-insert 1 record per akey at different dkeys. Record sizes are
	 * alternating SCM (2 consecutive extents = 2k), and NVME (5 consecutive
	 * record extents = 5k).
	 */
	for (i = 0; i < PUNCH_NUM_KEYS; i++) {
		if (i % 2 == 0)
			num_rec_exts = PUNCH_SCM_NUM_EXTS;
		else
			num_rec_exts = PUNCH_NVME_NUM_EXTS;
		sprintf(dkeys[i], "punch_simple_dkey%d", i + PUNCH_NUM_KEYS);
		print_message("insert dkey:%s, akey:'akey', rx_nr:%d\n",
			      dkeys[i], num_rec_exts);
		insert_single_with_rxnr(dkeys[i], "akey", /*idx*/ 0, data_buf,
					PUNCH_IOD_SIZE, num_rec_exts,
					DAOS_TX_NONE, &req);
	}
	/* Insert a few more unique akeys at the first dkey */
	print_message("insert dkey:%s, akey:'akey0', rx_nr:%d\n",
		      dkeys[0], num_rec_exts);
	insert_single_with_rxnr(dkeys[0], "akey0",/*idx*/ 0, data_buf,
				PUNCH_IOD_SIZE, num_rec_exts, DAOS_TX_NONE,
				&req);
	print_message("insert dkey:%s, akey:'akey1', rx_nr:%d\n",
		      dkeys[0], num_rec_exts);
	insert_single_with_rxnr(dkeys[0], "akey1",/*idx*/ 0, data_buf,
				PUNCH_IOD_SIZE, num_rec_exts, DAOS_TX_NONE,
				&req);

	/**
	 * Object punch (punch all keys associated with object).
	 */
	print_message("Punch entire object\n");
	punch_obj(DAOS_TX_NONE, &req);

	/* Enumerate dkeys */
	print_message("Enumerate dkeys:\n");
	memset(&anchor_out, 0, sizeof(anchor_out));
	memset(buf, 0, 512);
	total_keys = 0;
	while (!daos_anchor_is_eof(&anchor_out)) {
		enum_num = PUNCH_ENUM_NUM;
		rc = enumerate_dkey(DAOS_TX_NONE, &enum_num, kds, &anchor_out,
				    buf, 512, &req);
		assert_rc_equal(rc, 0);
		total_keys += enum_num;
	}
	print_message("\t#dkeys:%d\n", total_keys);
	assert_int_equal(total_keys, 0);

	D_FREE(buf);
	D_FREE(data_buf);
	for (i = 0; i < PUNCH_NUM_KEYS; i++)
		D_FREE(dkeys[i]);

	ioreq_fini(&req);
	par_barrier(PAR_COMM_WORLD);
}

#define MANYREC_NUMRECS	5
/**
 * Basic test for dkey/akey punch and full object punch.
 */
static void
punch_simple(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	punch_simple_internal(state, oid);

	/* OC_SX with some special handling for obj punch */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	punch_simple_internal(state, oid);
}

/**
 * Test update/fetch with data verification of multiple records of varying size
 * and IOD type. Size is either small I/O to SCM or larger (>=4k) I/O to NVMe,
 * and IOD type is either array or single value.
 */
static void
io_manyrec_internal(void **state, daos_obj_id_t oid, unsigned int size,
		    daos_iod_type_t iod_type, const char dkey[],
		    const char akey[])
{
	test_arg_t	*arg = *state;
	struct ioreq	 req;
	char		*akeys[MANYREC_NUMRECS];
	char		*rec[MANYREC_NUMRECS];
	daos_size_t	rec_size[MANYREC_NUMRECS];
	int		rx_nr[MANYREC_NUMRECS];
	daos_off_t	offset[MANYREC_NUMRECS];
	char		*val[MANYREC_NUMRECS];
	daos_size_t	val_size[MANYREC_NUMRECS];
	int		i;

	ioreq_init(&req, arg->coh, oid, iod_type, arg);

	for (i = 0; i < MANYREC_NUMRECS; i++) {
		akeys[i] = calloc(30, 1);
		assert_non_null(akeys[i]);
		snprintf(akeys[i], 30, "%s%d", akey, i);
		D_ALLOC(rec[i], size);
		assert_non_null(rec[i]);
		dts_buf_render(rec[i], size);
		rec_size[i] = size;
		rx_nr[i] = 1;
		offset[i] = i * size;
		val[i] = calloc(size, 1);
		assert_non_null(val[i]);
		val_size[i] = size;
	}

	/** Insert */
	insert(dkey, MANYREC_NUMRECS, (const char **)akeys,
	       rec_size, rx_nr, offset, (void **)rec, DAOS_TX_NONE, &req, 0);

	/** Lookup */
	lookup(dkey, MANYREC_NUMRECS, (const char **)akeys, offset, rec_size,
	       (void **)val, val_size, DAOS_TX_NONE, &req, false);

	/** Verify data consistency */
	for (i = 0; i < MANYREC_NUMRECS; i++) {
		print_message("\tsize = %lu\n", req.iod[i].iod_size);
		assert_int_equal(req.iod[i].iod_size, rec_size[i]);
		assert_memory_equal(val[i], rec[i], rec_size[i]);
		D_FREE(val[i]);
		D_FREE(akeys[i]);
		D_FREE(rec[i]);
	}
	ioreq_fini(&req);
}

/**
 * Very basic update/fetch with data verification of multiple records, with
 * varying record size and IOD type.
 */
static void
io_manyrec(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	print_message("Insert(e=0)/lookup(e=0)/verify complex kv records:\n");

	print_message("DAOS_IOD_ARRAY:SCM\n");
	io_manyrec_internal(state, oid, IO_SIZE_SCM, DAOS_IOD_ARRAY,
			    "io_manyrec_scm_array dkey",
			    "io_manyrec_scm_array akey");

	print_message("DAOS_IOD_ARRAY:NVME\n");
	io_manyrec_internal(state, oid, IO_SIZE_NVME, DAOS_IOD_ARRAY,
			    "io_manyrec_nvme_array dkey",
			    "io_manyrec_array akey");

	print_message("DAOS_IOD_SINGLE:SCM\n");
	io_manyrec_internal(state, oid, IO_SIZE_SCM, DAOS_IOD_SINGLE,
			    "io_manyrec_scm_single dkey",
			    "io_manyrec_scm_single akey");

	print_message("DAOS_IOD_SINGLE:NVME\n");
	io_manyrec_internal(state, oid, IO_SIZE_NVME, DAOS_IOD_SINGLE,
			    "io_manyrec_nvme_single dkey",
			    "io_manyrec_nvme_single akey");

}

/**
 * More complex update/fetch with data verification of multiple records with
 * mixed record size and IOD type all within a single RPC.
 */
static void
io_complex(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	const char	 dkey[] = "io_complex dkey";
	const char	 akey[] = "io_complex akey";
	char		*akeys[4];
	char		*rec[4];
	daos_size_t	 rec_size[4];
	daos_off_t	 offset[4];
	char		*val[4];
	unsigned int	 size;
	int		 i;

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	for (i = 0; i < 4; i++) {
		D_ASPRINTF(akeys[i], "%s%d", akey, i);
		if (i % 2 == 0)
			size = IO_SIZE_SCM;
		else
			size = IO_SIZE_NVME;
		D_ALLOC(rec[i], size);
		dts_buf_render(rec[i], size);
		rec_size[i] = size;
		offset[i] = i * size;
		D_ALLOC(val[i], size);
	}

	/** Insert */
	for (i = 0; i < 4; i++) {
		if (i == 0 || i == 3)
			req.iod_type = DAOS_IOD_SINGLE;
		else
			req.iod_type = DAOS_IOD_ARRAY;
		insert_single(dkey, akeys[i], offset[i], rec[i], rec_size[i],
			      DAOS_TX_NONE, &req);
	}

	/** Lookup & verify */
	for (i = 0; i < 4; i++) {
		if (i == 0 || i == 3) {
			req.iod_type = DAOS_IOD_SINGLE;
			print_message("DAOS_IOD_SINGLE:%s\n", rec_size[i] ==
				      IO_SIZE_SCM ? "SCM" : "NVME");
		} else {
			req.iod_type = DAOS_IOD_ARRAY;
			print_message("DAOS_IOD_ARRAY:%s\n", rec_size[i] ==
				      IO_SIZE_SCM ? "SCM" : "NVME");
		}

		lookup_single(dkey, akeys[i], offset[i], val[i], rec_size[i],
			      DAOS_TX_NONE, &req);
		print_message("\tsize = %lu\n", rec_size[i]);
		assert_memory_equal(val[i], rec[i], rec_size[i]);
		D_FREE(val[i]);
		D_FREE(rec[i]);
		D_FREE(akeys[i]);

	}

	ioreq_fini(&req);
}

#define STACK_BUF_LEN		24
#define TEST_BULK_BUF_LEN	(128 * 1024)

static void
basic_byte_array(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_sg_list_t	 sgl;
	d_iov_t		 sg_iov[2];
	daos_iod_t	 iod;
	daos_recx_t	 recx[5];
	char		 stack_buf_out[STACK_BUF_LEN];
	char		 stack_buf[STACK_BUF_LEN];
	char		 *bulk_buf = NULL;
	char		 *bulk_buf_out = NULL;
	char		 *buf;
	char		 *buf_out;
	int		 buf_len, tmp_len;
	int		 step = 1;
	int		 rc;

	D_ALLOC(bulk_buf, TEST_BULK_BUF_LEN);
	D_ASSERT(bulk_buf != NULL);
	D_ALLOC(bulk_buf_out, TEST_BULK_BUF_LEN);
	D_ASSERT(bulk_buf_out != NULL);
	dts_buf_render(stack_buf, STACK_BUF_LEN);
	dts_buf_render(bulk_buf, TEST_BULK_BUF_LEN);

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/* step 1 to test the inline data transfer, and step 2 to test the
	 * bulk transfer path.
	 */
next_step:
	/** init scatter/gather */
	D_ASSERT(step == 1 || step == 2);
	buf = step == 1 ? stack_buf : bulk_buf;
	buf_len = step == 1 ? STACK_BUF_LEN : TEST_BULK_BUF_LEN;
	d_iov_set(&sg_iov[0], buf, buf_len);
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 1;
	sgl.sg_iovs	= sg_iov;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	tmp_len = buf_len / 3;
	recx[0].rx_idx = 0;
	recx[0].rx_nr  = tmp_len;
	recx[1].rx_idx = tmp_len + 111;
	recx[1].rx_nr = tmp_len;
	recx[2].rx_idx = buf_len + 333;
	recx[2].rx_nr = buf_len - 2 * tmp_len;

	iod.iod_size	= 1;
	iod.iod_nr	= 3;
	iod.iod_recxs	= recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	print_message("writing %d bytes in two recxs ...\n", buf_len);
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	/** fetch */
	print_message("fetch with zero iov_len\n");
	iod.iod_size	= 1;
	recx[0].rx_idx	= 0;
	recx[0].rx_nr	= tmp_len * 1;
	iod.iod_nr	= 1;
	sg_iov[0].iov_len = 0;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL,
			    NULL);
	assert_rc_equal(rc, 0);

	print_message("reading data back with less buffer ...\n");
	recx[0].rx_idx	= 0;
	recx[0].rx_nr	= tmp_len;
	iod.iod_nr	= 3;
	iod.iod_size	= DAOS_REC_ANY;
	buf_out = step == 1 ? stack_buf_out : bulk_buf_out;
	memset(buf_out, 0, buf_len);
	tmp_len = buf_len / 2;
	d_iov_set(&sg_iov[0], buf_out, tmp_len);
	d_iov_set(&sg_iov[1], buf_out + tmp_len, (buf_len - tmp_len) / 2);
	sgl.sg_nr = 2;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL,
			    NULL);
	print_message("fetch with less buffer got rc %d, iod_size %d.\n",
		      rc, (int)iod.iod_size);
	assert_rc_equal(rc, -DER_REC2BIG);
	assert_int_equal(iod.iod_size, 1);

	print_message("reading un-existed record ...\n");
	recx[3].rx_idx	= 2 * buf_len + 40960;
	recx[3].rx_nr	= buf_len;
	iod.iod_nr	= 1;
	iod.iod_recxs	= &recx[3];
	iod.iod_size	= DAOS_REC_ANY;
	assert_int_equal(iod.iod_size, 0);
	d_iov_set(&sg_iov[1], buf_out + tmp_len, buf_len - tmp_len);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    NULL, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(sgl.sg_nr_out, 0);

	print_message("reading all data back ...\n");
	memset(buf_out, 0, buf_len);
	sgl.sg_nr_out	= 0;
	iod.iod_nr	= 3;
	iod.iod_recxs	= recx;
	iod.iod_size	= DAOS_REC_ANY;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    NULL, NULL);
	assert_rc_equal(rc, 0);
	/** Verify data consistency */
	print_message("validating data ... sg_nr_out %d, iod_size %d.\n",
		      sgl.sg_nr_out, (int)iod.iod_size);
	assert_int_equal(sgl.sg_nr_out, 2);
	assert_memory_equal(buf, buf_out, buf_len);

	print_message("short read should get iov_len with tail hole trimmed\n");
	memset(buf_out, 0, buf_len);
	tmp_len = buf_len / 3;
	sgl.sg_nr_out	= 0;
	sgl.sg_nr = 1;
	d_iov_set(&sg_iov[0], buf_out, tmp_len + 99);
	recx[4].rx_idx	= 0;
	recx[4].rx_nr	= tmp_len + 44;
	iod.iod_nr	= 1;
	iod.iod_recxs	= &recx[4];
	iod.iod_size	= DAOS_REC_ANY;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    NULL, NULL);
	assert_rc_equal(rc, 0);
	/** Verify data consistency */
	print_message("validating data ... sg_nr_out %d, iov_len %d.\n",
		      sgl.sg_nr_out, (int)sgl.sg_iovs[0].iov_len);
	assert_int_equal(sgl.sg_nr_out, 1);
	assert_int_equal(sgl.sg_iovs[0].iov_len, tmp_len);
	assert_memory_equal(buf, buf_out, tmp_len);

	if (step++ == 1)
		goto next_step;

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	print_message("all good\n");
	D_FREE(bulk_buf);
	D_FREE(bulk_buf_out);
}

static void
read_empty_records_internal(void **state, unsigned int size)
{
	test_arg_t *arg = *state;
	int buf;
	int *buf_out;
	daos_obj_id_t oid;
	daos_handle_t oh;
	d_iov_t dkey;
	d_sg_list_t sgl;
	d_iov_t sg_iov;
	daos_iod_t iod;
	daos_recx_t recx;
	int rc, i;

	/** open object */
	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey_empty", strlen("dkey_empty"));

	buf = 2000;
	/** init scatter/gather */
	d_iov_set(&sg_iov, &buf, sizeof(int));
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &sg_iov;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx.rx_idx	= (size/2) * sizeof(int);
	recx.rx_nr	= sizeof(int);
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	D_ALLOC_ARRAY(buf_out, size);
	assert_non_null(buf_out);

	for (i = 0; i < size; i++)
		buf_out[i] = 2016;

	/** fetch */
	d_iov_set(&sg_iov, buf_out, sizeof(int) * size);

	iod.iod_size	= 1;
	recx.rx_idx = 0;
	recx.rx_nr = sizeof(int) * size;
	iod.iod_recxs = &recx;

	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL,
			    NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < size; i++) {
		/**
		 * MSC - we should discuss more if the records with lower
		 * indices need to be 0
		 */
		/*
		 * if (i < STACK_BUF_LEN/2)
		 * assert_int_equal(buf_out[i], 0);
		*/
		if (i == size/2)
			assert_int_equal(buf_out[i], buf);
		else
			assert_int_equal(buf_out[i], 2016);
	}

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	D_FREE(buf_out);
}

static void
read_empty_records(void **state)
{
	/* inline transfer */
	read_empty_records_internal(state, STACK_BUF_LEN);
}

static void
read_large_empty_records(void **state)
{
	/* buffer size > 4k for bulk transfer */
	read_empty_records_internal(state, 8192);
}

#define NUM_AKEYS 5

static void
fetch_size(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		 dkey;
	d_sg_list_t	 sgl[NUM_AKEYS];
	d_iov_t	 sg_iov[NUM_AKEYS];
	daos_iod_t	 iod[NUM_AKEYS];
	char		*buf[NUM_AKEYS];
	char		*akey[NUM_AKEYS];
	const char	*akey_fmt = "akey%d";
	int		 i, rc;
	daos_size_t	 size = 131071;

	/** open object */
	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	for (i = 0; i < NUM_AKEYS; i++) {
		D_ALLOC(akey[i], strlen(akey_fmt) + 1);
		sprintf(akey[i], akey_fmt, i);

		D_ALLOC(buf[i], size * (i+1));
		assert_non_null(buf[i]);

		dts_buf_render(buf[i], size * (i+1));

		/** init scatter/gather */
		d_iov_set(&sg_iov[i], buf[i], size * (i+1));
		sgl[i].sg_nr		= 1;
		sgl[i].sg_nr_out	= 0;
		sgl[i].sg_iovs		= &sg_iov[i];

		/** init I/O descriptor */
		d_iov_set(&iod[i].iod_name, akey[i], strlen(akey[i]));
		iod[i].iod_nr		= 1;
		iod[i].iod_size		= size * (i+1);
		iod[i].iod_recxs	= NULL;
		iod[i].iod_type		= DAOS_IOD_SINGLE;
	}

	/** update record */
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, NUM_AKEYS, iod, sgl,
			     NULL);
	assert_rc_equal(rc, 0);

	/** fetch record size */
	for (i = 0; i < NUM_AKEYS; i++)
		iod[i].iod_size	= DAOS_REC_ANY;

	print_message("fetch with unknown iod_size and NULL sgl\n");
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, NUM_AKEYS, iod, NULL,
			    NULL, NULL);
	assert_rc_equal(rc, 0);
	for (i = 0; i < NUM_AKEYS; i++)
		assert_int_equal(iod[i].iod_size, size * (i+1));

	print_message("fetch with unknown iod_size and less buffer\n");
	for (i = 0; i < NUM_AKEYS; i++) {
		d_iov_set(&sg_iov[i], buf[i], size * (i+1) - 1);
		iod[i].iod_size	= DAOS_REC_ANY;
	}
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, NUM_AKEYS, iod, sgl,
			    NULL, NULL);
	assert_rc_equal(rc, -DER_REC2BIG);
	for (i = 0; i < NUM_AKEYS; i++)
		assert_int_equal(iod[i].iod_size, size * (i+1));

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < NUM_AKEYS; i++) {
		D_FREE(akey[i]);
		D_FREE(buf[i]);
	}
}

static void
io_simple_update_timeout(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	arg->fail_loc = DAOS_SHARD_OBJ_UPDATE_TIMEOUT | DAOS_FAIL_SOME;
	arg->fail_num = 5;

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	io_simple_internal(state, oid, 64, DAOS_IOD_ARRAY, "test_update dkey",
			   "test_update akey");

}

static void
io_simple_fetch_timeout(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	arg->fail_loc = DAOS_SHARD_OBJ_FETCH_TIMEOUT | DAOS_FAIL_SOME;
	arg->fail_num = 5;

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	io_simple_internal(state, oid, 64, DAOS_IOD_ARRAY, "test_fetch dkey",
			   "test_fetch akey");
}

static void
io_simple_update_timeout_single(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	arg->fail_loc = DAOS_SHARD_OBJ_UPDATE_TIMEOUT_SINGLE | DAOS_FAIL_ONCE;
	arg->fail_value = rand() % dts_obj_replica_cnt;

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	io_simple_internal(state, oid, 64, DAOS_IOD_ARRAY,
			   "test_update_to dkey", "test_update_to akey");
}

static void
io_simple_update_crt_error(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	arg->fail_loc = DAOS_SHARD_OBJ_RW_CRT_ERROR | DAOS_FAIL_ONCE;

	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	io_simple_internal(state, oid, 64, DAOS_IOD_ARRAY,
			   "test_update_err dkey", "test_update_err akey");
}

static void
io_simple_update_crt_req_error(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	arg->fail_loc = DAOS_OBJ_REQ_CREATE_TIMEOUT | DAOS_FAIL_ONCE;

	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	io_simple_internal(state, oid, 64, DAOS_IOD_ARRAY,
			   "test_update_err_req dkey",
			   "test_update_err_req akey");
}

void
close_reopen_coh_oh(test_arg_t *arg, struct ioreq *req, daos_obj_id_t oid)
{
	int rc;

	print_message("closing object\n");
	rc = daos_obj_close(req->oh, NULL /* ev */);
	assert_rc_equal(rc, 0);

	print_message("closing container\n");
	rc = daos_cont_close(arg->coh, NULL /* ev */);
	assert_rc_equal(rc, 0);

	print_message("reopening container\n");
	if (arg->myrank == 0) {
		rc = daos_cont_open(arg->pool.poh, arg->co_str, DAOS_COO_RW,
				    &arg->coh, &arg->co_info, NULL /* ev */);
		assert_rc_equal(rc, 0);
	}
	handle_share(&arg->coh, HANDLE_CO, arg->myrank, arg->pool.poh, 1);

	print_message("reopening object\n");
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &req->oh, NULL);
	assert_rc_equal(rc, 0);
}

/**
 * Basic test to insert a few large and small records in different transactions,
 * discard one of the transactions and commit the rest, then verify records both
 * prior to and after the discarded transaction.
 */
static void
tx_discard(void **state)
{
	/*
	 * FIXME: This obsolete epoch model transaction API test have been
	 * broken by online aggregation, needs be removed or updated as per
	 * new transaction model.
	 */
	print_message("Skip obsolete test\n");
	skip();
#if 0
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	const int	 nakeys = 4;
	const size_t	 nakeys_strlen = 4 /* "9999" */;
	const char	 dkey[] = "tx_discard dkey";
	const char	*akey_fmt = "tx_discard akey%d";
	char		*akey[nakeys];
	char		*rec[nakeys];
	daos_size_t	 rec_size[nakeys];
	int		 rx_nr[nakeys];
	daos_off_t	 offset[nakeys];
	char		*rec_nvme;
	char		*rec_scm;
	char		*val[nakeys];
	daos_size_t	 val_size[nakeys];
	char		*rec_verify;
	daos_handle_t	 th[3];
	int		 i, t;
	int		 rc;

	par_barrier(PAR_COMM_WORLD);

	D_ALLOC(rec_nvme, IO_SIZE_NVME);
	assert_non_null(rec_nvme);
	dts_buf_render(rec_nvme, IO_SIZE_NVME);
	D_ALLOC(rec_scm, IO_SIZE_SCM);
	assert_non_null(rec_scm);
	dts_buf_render(rec_scm, IO_SIZE_SCM);

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	/** Prepare buffers for a fixed set of d-keys and a-keys. */
	for (i = 0; i < nakeys; i++) {
		D_ALLOC(akey[i], strlen(akey_fmt) + nakeys_strlen + 1);
		assert_non_null(akey[i]);
		sprintf(akey[i], akey_fmt, i);
		rec[i] = calloc(i % 2 == 0 ? IO_SIZE_NVME : IO_SIZE_SCM, 1);
		assert_non_null(rec[i]);
		offset[i] = i * 20;
		val[i] = calloc(i % 2 == 0 ? IO_SIZE_NVME : IO_SIZE_SCM, 1);
		assert_non_null(val[i]);
		if (i % 2 == 0)
			val_size[i] = IO_SIZE_NVME;
		else
			val_size[i] = IO_SIZE_SCM;
	}

	/** Write three timestamps to same set of d-key and a-keys. */
	for (t = 0; t < 3; t++) {
		rc = daos_tx_open(arg->coh, &th[t], NULL);
		assert_success(rc);

		print_message("writing to transaction %d\n", t);
		for (i = 0; i < nakeys; i++) {
			if (i % 2 == 0) {
				memcpy(rec[i], rec_nvme, IO_SIZE_NVME);
				rec_size[i] = IO_SIZE_NVME;
			} else {
				memcpy(rec[i], rec_scm, IO_SIZE_SCM);
				rec_size[i] = IO_SIZE_SCM;
			}
			rec[i][0] = i + '0'; /* akey */
			rec[i][1] = t + '0'; /*  tx handle */
			rx_nr[i] = 1;
			print_message("\takey[%d], val(%d):'%c%c'\n", i,
				      (int)rec_size[i], rec[i][0], rec[i][1]);
		}
		insert(dkey, nakeys, (const char **)akey, rec_size, rx_nr,
		       offset, (void **)rec, th[t], &req);
		par_barrier(PAR_COMM_WORLD);
	}

	for (t = 0; t < 3; t++) {
		/** Discard second timestamp. */
		if (t == 1) {
			print_message("aborting transaction %d.\n", t);
			rc = daos_tx_abort(th[t], NULL);
			assert_success(rc);
		} else {
			print_message("committing transaction %d.\n", t);
			rc = daos_tx_commit(th[t], NULL);
			assert_success(rc);
		}
		par_barrier(PAR_COMM_WORLD);
	}

	/** Check the three transactions. */
	for (t = 0; t < 3; t++) {
		print_message("verifying transaction %d\n", t);
		lookup(dkey, nakeys, (const char **)akey, offset, rec_size,
		       (void **)val, val_size, th[t], &req, false);
		for (i = 0; i < nakeys; i++) {
			rec_verify = calloc(i % 2 == 0 ? IO_SIZE_NVME :
					    IO_SIZE_SCM, 1);
			assert_non_null(rec_verify);
			if (i % 2 == 0)
				memcpy(rec_verify, rec_nvme, IO_SIZE_NVME);
			else
				memcpy(rec_verify, rec_scm, IO_SIZE_SCM);

			if (t == 1) { /* discarded */
				rec_verify[0] = i + '0'; /*akey*/
				rec_verify[1] = t - 1 + '0'; /*previous tx*/
			} else { /* intact */
				rec_verify[0] = i + '0';
				rec_verify[1] = t + '0';
			}
			assert_int_equal(req.iod[i].iod_size,
					 strlen(rec_verify) + 1);
			print_message("\takey[%d], val(%d):'%c%c'\n",
				      i, (int)req.iod[i].iod_size, val[i][0],
				      val[i][1]);
			assert_memory_equal(val[i], rec_verify,
					    req.iod[i].iod_size);
			D_FREE(rec_verify);
		}
		rc = daos_tx_close(th[t], NULL);
		assert_success(rc);
	}

	/** Close and reopen the container and the obj. */
	par_barrier(PAR_COMM_WORLD);
	close_reopen_coh_oh(arg, &req, oid);

	/** Verify record is the same as the last committed transaction. */
	lookup(dkey, nakeys, (const char **)akey, offset, rec_size,
	       (void **)val, val_size, DAOS_TX_NONE, &req, false);
	print_message("verifying transaction after container re-open\n");
	for (i = 0; i < nakeys; i++) {
		rec_verify = calloc(i % 2 == 0 ? IO_SIZE_NVME :
				    IO_SIZE_SCM, 1);
		assert_non_null(rec_verify);
		if (i % 2 == 0)
			memcpy(rec_verify, rec_nvme, IO_SIZE_NVME);
		else
			memcpy(rec_verify, rec_scm, IO_SIZE_SCM);

		rec_verify[0] = i + '0';
		rec_verify[1] = 2 + '0';
		assert_int_equal(req.iod[i].iod_size,
				 strlen(rec_verify) + 1);
		print_message("\takey[%d], val(%d):'%c%c'\n", i,
			      (int)req.iod[i].iod_size, val[i][0], val[i][1]);
		assert_memory_equal(val[i], rec_verify,
				    req.iod[i].iod_size);
		D_FREE(rec_verify);
	}

	for (i = 0; i < nakeys; i++) {
		D_FREE(val[i]);
		D_FREE(akey[i]);
		D_FREE(rec[i]);
	}
	D_FREE(rec_nvme);
	D_FREE(rec_scm);

	ioreq_fini(&req);
	par_barrier(PAR_COMM_WORLD);
#endif
}

/**
 * Basic test to insert a few large and small records at different transactions,
 * commit only the first few TXs, and verify that all TXs remain after
 * container close and non-committed TXs were successfully discarded.
 */
static void
tx_commit(void **state)
{
	/*
	 * FIXME: This obsolete epoch model transaction API test have been
	 * broken by online aggregation, needs be removed or updated as per
	 * new transaction model.
	 */
	print_message("Skip obsolete test\n");
	skip();
#if 0
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	const int	 nakeys = 4;
	const size_t	 nakeys_strlen = 4 /* "9999" */;
	char		 dkey[] = "tx_commit dkey";
	const char	*akey_fmt = "tx_commit akey%d";
	char		*akey[nakeys];
	char		*rec[nakeys];
	daos_size_t	 rec_size[nakeys];
	int		 rx_nr[nakeys];
	daos_off_t	 offset[nakeys];
	char		*rec_nvme;
	char		*rec_scm;
	char		*val[nakeys];
	daos_size_t	 val_size[nakeys];
	char		*rec_verify;
	daos_handle_t	 th[3];
	int		 i, t;
	int		 rc;

	par_barrier(PAR_COMM_WORLD);

	D_ALLOC(rec_nvme, IO_SIZE_NVME);
	assert_non_null(rec_nvme);
	dts_buf_render(rec_nvme, IO_SIZE_NVME);
	D_ALLOC(rec_scm, IO_SIZE_SCM);
	assert_non_null(rec_scm);
	dts_buf_render(rec_scm, IO_SIZE_SCM);

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	/** Prepare buffers for a fixed set of d-keys and a-keys. */
	for (i = 0; i < nakeys; i++) {
		D_ALLOC(akey[i], strlen(akey_fmt) + nakeys_strlen + 1);
		assert_non_null(akey[i]);
		sprintf(akey[i], akey_fmt, i);
		rec[i] = calloc(i % 2 == 0 ? IO_SIZE_NVME : IO_SIZE_SCM, 1);
		assert_non_null(rec[i]);
		offset[i] = i * 20;
		val[i] = calloc(i % 2 == 0 ? IO_SIZE_NVME : IO_SIZE_SCM, 1);
		assert_non_null(val[i]);
		if (i % 2 == 0)
			val_size[i] = IO_SIZE_NVME;
		else
			val_size[i] = IO_SIZE_SCM;
	}

	/** Write at 3 different txs to same set of d-key and a-keys. */
	for (t = 0; t < 3; t++) {
		rc = daos_tx_open(arg->coh, &th[t], NULL);
		assert_success(rc);
		print_message("writing to transaction %d\n", t);
		for (i = 0; i < nakeys; i++) {
			if (i % 2 == 0) {
				memcpy(rec[i], rec_nvme, IO_SIZE_NVME);
				rec_size[i] = IO_SIZE_NVME;
			} else {
				memcpy(rec[i], rec_scm, IO_SIZE_SCM);
				rec_size[i] = IO_SIZE_SCM;
			}
			rec[i][0] = i + '0'; /* akey */
			rec[i][1] = t + '0'; /* tx handle */
			rx_nr[i] = 1;
			print_message("\takey[%d], val(%d):'%c%c'\n", i,
				      (int)rec_size[i], rec[i][0], rec[i][1]);
		}
		insert(dkey, nakeys, (const char **)akey, /*iod_size*/rec_size,
			rx_nr, offset, (void **)rec, th[t], &req);
		par_barrier(PAR_COMM_WORLD);
	}

	/** Check the three transactions. */
	for (t = 0; t < 3; t++) {
		print_message("verifying transaction %d\n", t);
		lookup(dkey, nakeys, (const char **)akey, offset, rec_size,
		       (void **)val, val_size, th[t], &req, false);
		for (i = 0; i < nakeys; i++) {
			rec_verify = calloc(i % 2 == 0 ? IO_SIZE_NVME :
					    IO_SIZE_SCM, 1);
			assert_non_null(rec_verify);
			if (i % 2 == 0)
				memcpy(rec_verify, rec_nvme, IO_SIZE_NVME);
			else
				memcpy(rec_verify, rec_scm, IO_SIZE_SCM);

			rec_verify[0] = i + '0'; /* akey */
			rec_verify[1] = t + '0'; /* tx handle*/
			assert_int_equal(req.iod[i].iod_size,
					 strlen(rec_verify) + 1);
			assert_memory_equal(val[i], rec_verify,
					    req.iod[i].iod_size);
			D_FREE(rec_verify);
		}
	}

	par_barrier(PAR_COMM_WORLD);

	/** Commit only the first 2 transactions */
	for (t = 0; t < 3; t++) {
		if (t != 2) {
			print_message("committing transaction %d\n", t);
			rc = daos_tx_commit(th[t], NULL);
			assert_success(rc);
		} else {
			print_message("aborting transaction %d\n", t);
			rc = daos_tx_abort(th[t], NULL);
			assert_success(rc);
		}
		rc = daos_tx_close(th[t], NULL);
		assert_success(rc);
		par_barrier(PAR_COMM_WORLD);
	}

	/** Close and reopen the container and the obj */
	close_reopen_coh_oh(arg, &req, oid);

	/** enumerate. */
	{
		daos_anchor_t	anchor;
		int		found = 0;

		memset(&anchor, 0, sizeof(anchor));
		while (!daos_anchor_is_eof(&anchor)) {
			uint32_t		n = 1;
			daos_key_desc_t		kd;
			char			*buf[64];

			rc = enumerate_akey(DAOS_TX_NONE, dkey, &n, &kd,
					    &anchor, buf, sizeof(buf), &req);
			assert_success(rc);
			found += n;
		}
		assert_int_equal(found, nakeys);
	}

	/**
	 * Check data after container close. Last tx was not committed and
	 * should be discarded, therefore data should be from transaction 2.
	 */
	print_message("verifying transaction after container re-open\n");
	lookup(dkey, nakeys, (const char **)akey, offset, rec_size,
	       (void **)val, val_size, DAOS_TX_NONE, &req, false);
	t = 1;
	for (i = 0; i < nakeys; i++) {
		rec_verify = calloc(i % 2 == 0 ? IO_SIZE_NVME : IO_SIZE_SCM, 1);
		assert_non_null(rec_verify);
		if (i % 2 == 0)
			memcpy(rec_verify, rec_nvme, IO_SIZE_NVME);
		else
			memcpy(rec_verify, rec_scm, IO_SIZE_SCM);

		rec_verify[0] = i + '0';
		rec_verify[1] = t + '0';
		assert_int_equal(req.iod[i].iod_size, strlen(rec_verify) + 1);
		print_message("\trank %d, akey[%d], val(%d):'%c%c'\n",
			      arg->myrank, i, (int)req.iod[i].iod_size,
			      val[i][0], val[i][1]);
		assert_memory_equal(val[i], rec_verify,
				    req.iod[i].iod_size);
		D_FREE(rec_verify);
	}

	for (i = 0; i < nakeys; i++) {
		D_FREE(val[i]);
		D_FREE(akey[i]);
		D_FREE(rec[i]);
	}
	D_FREE(rec_nvme);
	D_FREE(rec_scm);

	ioreq_fini(&req);
	par_barrier(PAR_COMM_WORLD);
#endif
}

static void
io_nospace(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		buf_size = 1 << 20;
	char		*large_buf;
	char		key[32];
	int		i;

	FAULT_INJECTION_REQUIRED();

	/** choose random object */
	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);

	D_ALLOC(large_buf, buf_size);
	assert_non_null(large_buf);
	arg->fail_loc = DAOS_OBJ_UPDATE_NOSPACE | DAOS_FAIL_ALWAYS;
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 5; i++) {
		sprintf(key, "dkey%d", i);
		/** Insert */
		arg->expect_result = -DER_NOSPACE;
		insert_single(key, "akey", 0, "data",
			      strlen("data") + 1, DAOS_TX_NONE, &req);
		insert_single(key, "akey", 0, large_buf,
			      buf_size, DAOS_TX_NONE, &req);
	}
	D_FREE(large_buf);
	ioreq_fini(&req);
}

static void
write_record_multiple_times(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	char		 buf[10];
	char		 large_update_buf[4096];
	char		 large_fetch_buf[4096];

	oid = daos_test_oid_gen(arg->coh, 0, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** write twice */
	insert_single("dkey", "akey", 0, "data", strlen("data") + 1,
		      DAOS_TX_NONE, &req);
	insert_single("dkey", "akey", 0, "data", strlen("data") + 1,
		      DAOS_TX_NONE, &req);
	/** Lookup */
	memset(buf, 0, 10);
	lookup_single("dkey", "akey", 0, buf, 10, DAOS_TX_NONE, &req);
	assert_int_equal(req.iod[0].iod_size, strlen(buf) + 1);
	/** Verify data consistency */
	assert_string_equal(buf, "data");

	dts_buf_render(large_update_buf, 4096);
	/** write twice */
	insert_single("dkey_large", "akey_large", 0, large_update_buf, 4096,
		      DAOS_TX_NONE, &req);
	insert_single("dkey_large", "akey_large", 0, large_update_buf, 4096,
		      DAOS_TX_NONE, &req);

	memset(large_fetch_buf, 0, 4096);
	lookup_single("dkey_large", "akey_large", 0, large_fetch_buf, 4096,
		      DAOS_TX_NONE, &req);

	assert_int_equal(req.iod[0].iod_size, 4096);
	assert_memory_equal(large_update_buf, large_fetch_buf, 4096);

	ioreq_fini(&req);
}

static void
echo_fetch_update(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	oid = daos_test_oid_gen(arg->coh, DAOS_OC_ECHO_TINY_RW, 0, 0,
				arg->myrank);
	io_simple_internal(state, oid, 64, DAOS_IOD_ARRAY, "echo_test dkey",
			   "echo_test akey");

	oid = daos_test_oid_gen(arg->coh, DAOS_OC_ECHO_TINY_RW, 0, 0,
				arg->myrank);
	io_simple_internal(state, oid, 8192, DAOS_IOD_ARRAY,
			   "echo_test_large dkey", "echo_test_large akey");
}

static void
tgt_idx_change_retry(void **state)
{
	test_arg_t		*arg0 = *state;
	test_arg_t		*arg = NULL;
	daos_obj_id_t		 oid;
	struct ioreq		 req;
	const char		 dkey[] = "tgt_change dkey";
	char			*akey[5];
	char			*rec[5];
	daos_size_t		 rec_size[5];
	int			 rx_nr[5];
	daos_off_t		 offset[5];
	char			*val[5];
	daos_size_t		 val_size[5];
	struct daos_obj_layout	*layout;
	d_rank_t		 rank = 0;
	int			 replica;
	int			 i;
	int			 rc;

	dt_redun_fac = DAOS_PROP_CO_REDUN_RF1;
	rc = test_setup((void **)&arg, SETUP_CONT_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);
	dt_redun_fac = 0;

	/* create a 3 replica small object, to test the case that:
	 * update:
	 * 1) shard 0 IO finished, then the target x of shard 0 dead/excluded
	 * 2) shard 1 and shard 2 IO still in-flight (not scheduled)
	 * 3) obj IO retry, shard 0 goes to new target y
	 *
	 * Then fetch and verify the data.
	 */

	/* needs at lest 4 targets, exclude one and another 3 raft nodes */
	if (!test_runable(arg, 4))
		skip();

	if (!arg0->async) {
		if (arg->myrank == 0)
			print_message("this test can-only run in async mode\n");
		skip();
	}
	async_enable((void **)&arg);

	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R3S_SPEC_RANK, 0, 0,
				arg->myrank);
	oid = dts_oid_set_rank(oid, 2);
	replica = rand() % 3;
	arg->fail_loc = DAOS_OBJ_TGT_IDX_CHANGE;
	arg->fail_num = replica;
	if (arg->myrank == 0) {
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     DAOS_OBJ_TGT_IDX_CHANGE,
				     replica, NULL);
	}
	par_barrier(PAR_COMM_WORLD);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	print_message("Insert(e=0)/lookup(e=0)/verify complex kv record "
		      "with target change.\n");
	for (i = 0; i < 5; i++) {
		akey[i] = calloc(20, 1);
		assert_non_null(akey[i]);
		sprintf(akey[i], "test_update akey%d", i);
		rec[i] = calloc(20, 1);
		assert_non_null(rec[i]);
		sprintf(rec[i], "test_update val%d", i);
		rec_size[i] = strlen(rec[i]);
		rx_nr[i] = 1;
		offset[i] = i * 20;
		val[i] = calloc(64, 1);
		assert_non_null(val[i]);
		val_size[i] = 64;
	}

	/** Insert */
	insert_nowait(dkey, 5, (const char **)akey, /*iod_size*/rec_size,
		      rx_nr, offset, (void **)rec, DAOS_TX_NONE, &req, 0);

	if (arg->myrank == 0) {
		/** verify the object layout */
		rc = daos_obj_layout_get(arg->coh, oid, &layout);
		assert_rc_equal(rc, 0);
		assert_int_equal(layout->ol_nr, 1);
		assert_int_equal(layout->ol_shards[0]->os_replica_nr, 3);
		/* FIXME disable rank compare until we fix the layout_get */
		/* assert_int_equal(layout->ol_shards[0]->os_ranks[0], 2); */
		rank = layout->ol_shards[0]->os_shard_loc[replica].sd_rank;
		rc = daos_obj_layout_free(layout);
		assert_rc_equal(rc, 0);

		/** exclude target of the replica */
		print_message("rank 0 excluding rank %u ...\n", rank);
		rc = dmg_pool_exclude(arg->dmg_config, arg->pool.pool_uuid,
				      arg->group, rank, -1);
		assert_success(rc);

		/** progress the async IO (not must) */
		insert_test(&req, 1000);

		/** wait until rebuild done */
		test_rebuild_wait(&arg, 1);

		/** verify the target of shard 0 changed */
		rc = daos_obj_layout_get(arg->coh, oid, &layout);
		assert_rc_equal(rc, 0);
		assert_int_equal(layout->ol_nr, 1);
		assert_int_equal(layout->ol_shards[0]->os_replica_nr, 3);
		/* FIXME disable rank compare until we fix the layout_get */
		/* assert_int_not_equal(layout->ol_shards[0]->os_ranks[replica],
		 *		     rank);
		*/
		print_message("target of shard %d changed from %d to %d\n",
			      replica, rank,
			      layout->ol_shards[0]->os_shard_loc[0].sd_rank);
		rc = daos_obj_layout_free(layout);
		assert_rc_equal(rc, 0);
	}

	daos_fail_loc_set(0);
	if (arg->myrank == 0) {
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, 0,
				     0, NULL);
	}
	par_barrier(PAR_COMM_WORLD);
	insert_wait(&req);

	daos_fail_loc_set(DAOS_OBJ_SPECIAL_SHARD);
	/** lookup through each replica and verify data */
	for (replica = 0; replica < 3; replica++) {
		daos_fail_num_set(replica);
		for (i = 0; i < 5; i++)
			memset(val[i], 0, 64);

		lookup(dkey, 5, (const char **)akey, offset, rec_size,
		       (void **)val, val_size, DAOS_TX_NONE, &req, false);

		for (i = 0; i < 5; i++) {
			assert_int_equal(req.iod[i].iod_size, strlen(rec[i]));
			assert_memory_equal(val[i], rec[i], strlen(rec[i]));
		}
	}

	for (i = 0; i < 5; i++) {
		D_FREE(val[i]);
		D_FREE(akey[i]);
		D_FREE(rec[i]);
	}

	if (arg->myrank == 0) {
		print_message("rank 0 adding rank %u ...\n", rank);
		rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group,
					  rank, -1);
		assert_success(rc);
	}
	par_barrier(PAR_COMM_WORLD);
	ioreq_fini(&req);
	test_teardown((void **)&arg);
}

static void
fetch_replica_unavail(void **state)
{
	test_arg_t		*arg = *state;
	daos_obj_id_t		 oid;
	struct ioreq		 req;
	const char		 dkey[] = "test_update dkey";
	const char		 akey[] = "test_update akey";
	const char		 rec[]  = "test_update record";
	uint32_t		 size = 64;
	d_rank_t		 rank = 2;
	char			*buf;
	int			 rc = 0;

	FAULT_INJECTION_REQUIRED();

	/* needs at lest 4 targets, exclude one and another 3 raft nodes */
	if (!test_runable(arg, 4))
		skip();

	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R1S_SPEC_RANK, 0, 0,
				arg->myrank);
	oid = dts_oid_set_rank(oid, rank);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	/** Insert */
	print_message("Insert(e=0)/exclude all tgts/lookup(e=0) verify "
		      "get -DER_NONEXIST\n");
	insert_single(dkey, akey, 0, (void *)rec, strlen(rec), DAOS_TX_NONE,
		      &req);

	if (arg->myrank == 0) {
		/** exclude the target of this obj's replicas */
		rc = dmg_pool_exclude(arg->dmg_config, arg->pool.pool_uuid,
				      arg->group, rank, -1);
		assert_success(rc);
	}
	par_barrier(PAR_COMM_WORLD);

	/** Lookup */
	D_ALLOC(buf, size);
	assert_non_null(buf);
	/** inject CRT error failure to update pool map + retry */
	daos_fail_loc_set(DAOS_SHARD_OBJ_RW_CRT_ERROR | DAOS_FAIL_ONCE);
	arg->expect_result = -DER_NONEXIST;
	lookup_single(dkey, akey, 0, buf, size, DAOS_TX_NONE, &req);

	if (arg->myrank == 0) {
		/* wait until rebuild done */
		test_rebuild_wait(&arg, 1);

		/* add back the excluded targets */
		rc = dmg_pool_reintegrate(arg->dmg_config, arg->pool.pool_uuid, arg->group,
					  rank, -1);
		assert_success(rc);

		/* wait until reintegration is done */
		test_rebuild_wait(&arg, 1);
		daos_cont_status_clear(arg->coh, NULL);
	}
	D_FREE(buf);
	par_barrier(PAR_COMM_WORLD);
	ioreq_fini(&req);
}

static void
update_overlapped_recxs(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t		dkey;
	d_sg_list_t	 sgl;
	d_iov_t		sg_iov;
	daos_iod_t	 iod;
	daos_recx_t	 recx[128];
	char		 buf[STACK_BUF_LEN];
	int		 i;
	int		 rc;

	/** open object */
	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	d_iov_set(&sg_iov, buf, sizeof(buf));
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	iod.iod_size	= 1;
	iod.iod_recxs	= recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	print_message("writing with overlapped recxs should get -DER_INVAL\n");
	recx[0].rx_idx	= 0;
	recx[0].rx_nr	= 3;
	recx[1].rx_idx	= 4;
	recx[1].rx_nr	= 1;
	recx[2].rx_idx	= 3;
	recx[2].rx_nr	= 2;
	iod.iod_nr	= 3;
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	for (i = 0; i < 128; i++) {
		if (i != 111) {
			recx[i].rx_idx = i * 2;
			recx[i].rx_nr  = 2;
		} else {
			recx[i].rx_idx = (i + 1) * 2;
			recx[i].rx_nr  = 1;
		}
	}
	iod.iod_nr	= 128;
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, -DER_INVAL);


	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	print_message("all good\n");
}

/** very basic key query test */
static void
io_obj_key_query(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	daos_iod_t	iod = {0};
	d_sg_list_t	sgl = {0};
	uint32_t	update_var = 0xdeadbeef;
	d_iov_t		val_iov;
	d_iov_t		dkey;
	d_iov_t		akey;
	daos_recx_t	recx;
	uint64_t	dkey_val, akey_val;
	uint32_t	flags;
	daos_handle_t	th;
	int		rc;

	/** open object */
	oid = daos_test_oid_gen(arg->coh, OC_SX, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey, akey */
	dkey_val = akey_val = 0;
	d_iov_set(&dkey, &dkey_val, sizeof(uint64_t));
	d_iov_set(&akey, &akey_val, sizeof(uint64_t));

	flags = DAOS_GET_DKEY;
	rc = daos_obj_query_key(oh, DAOS_TX_NONE, flags, &dkey, NULL, NULL,
				NULL);
	assert_rc_equal(rc, -DER_INVAL);

	flags = DAOS_GET_MAX;
	rc = daos_obj_query_key(oh, DAOS_TX_NONE, flags, &dkey, NULL, NULL,
				NULL);
	assert_rc_equal(rc, -DER_INVAL);

	flags = DAOS_GET_MAX | DAOS_GET_MIN;
	rc = daos_obj_query_key(oh, DAOS_TX_NONE, flags, &dkey, NULL, NULL,
				NULL);
	assert_rc_equal(rc, -DER_INVAL);

	flags = DAOS_GET_DKEY | DAOS_GET_MAX | DAOS_GET_MIN;
	rc = daos_obj_query_key(oh, DAOS_TX_NONE, flags, &dkey, NULL, NULL,
				NULL);
	assert_rc_equal(rc, -DER_INVAL);

	flags = DAOS_GET_AKEY | DAOS_GET_MIN;
	rc = daos_obj_query_key(oh, DAOS_TX_NONE, flags, &dkey, NULL, NULL,
				NULL);
	assert_rc_equal(rc, -DER_INVAL);

	flags = DAOS_GET_DKEY | DAOS_GET_AKEY | DAOS_GET_RECX | DAOS_GET_MAX;
	rc = daos_obj_query_key(oh, DAOS_TX_NONE, flags, &dkey, &akey, NULL,
				NULL);
	assert_rc_equal(rc, -DER_INVAL);

	flags = DAOS_GET_DKEY | DAOS_GET_MIN;
	rc = daos_obj_query_key(oh, DAOS_TX_NONE, flags, &dkey, NULL, NULL,
				NULL);
	assert_rc_equal(rc, -DER_INVAL);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	oid = daos_test_oid_gen(arg->coh, OC_SX,
				DAOS_OT_MULTI_UINT64,
				0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	dkey_val = 5;
	akey_val = 10;
	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_name = akey;
	iod.iod_recxs = &recx;
	iod.iod_nr = 1;
	iod.iod_size = sizeof(update_var);

	d_iov_set(&val_iov, &update_var, sizeof(update_var));
	sgl.sg_iovs = &val_iov;
	sgl.sg_nr = 1;

	recx.rx_idx = 5;
	recx.rx_nr = 1;

	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	dkey_val = 10;
	val_iov.iov_buf_len += 1024;
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);
	d_iov_set(&val_iov, &update_var, sizeof(update_var));

	recx.rx_idx = 50;
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	/*
	 * Not essential to this test, opening a TX helps us exercise
	 * dc_tx_get_epoch through the daos_obj_query_key fanout.
	 */
	rc = daos_tx_open(arg->coh, &th, 0, NULL);
	assert_rc_equal(rc, 0);

	flags = DAOS_GET_DKEY | DAOS_GET_AKEY | DAOS_GET_RECX | DAOS_GET_MAX;
	rc = daos_obj_query_key(oh, th, flags, &dkey, &akey, &recx, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(*(uint64_t *)dkey.iov_buf, 10);
	assert_int_equal(*(uint64_t *)akey.iov_buf, 10);
	assert_int_equal(recx.rx_idx, 50);
	assert_int_equal(recx.rx_nr, 1);

	flags = DAOS_GET_AKEY | DAOS_GET_RECX | DAOS_GET_MAX;
	rc = daos_obj_query_key(oh, th, flags, &dkey, &akey, &recx, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(*(uint64_t *)akey.iov_buf, 10);
	assert_int_equal(recx.rx_idx, 50);
	assert_int_equal(recx.rx_nr, 1);

	flags = DAOS_GET_RECX | DAOS_GET_MAX;
	rc = daos_obj_query_key(oh, th, flags, &dkey, &akey, &recx, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(recx.rx_idx, 50);
	assert_int_equal(recx.rx_nr, 1);

	rc = daos_tx_close(th, NULL);
	assert_rc_equal(rc, 0);

	flags = DAOS_GET_RECX | DAOS_GET_MAX;
	rc = daos_obj_query_key(oh, DAOS_TX_NONE, flags, &dkey, &akey, &recx, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(recx.rx_idx, 50);
	assert_int_equal(recx.rx_nr, 1);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);
	print_message("all good\n");
}

/**
 * Simple test to trigger the blob unmap callback. This is done by inserting
 * a few NVMe records, deleting some of the NVMe records by discarding one of
 * the transactions, waiting a long enough time for the free
 * extents to expire, and then inserting another record to trigger the callback.
 */
static void
blob_unmap_trigger(void **state)
{
	/*
	 * FIXME: obsolete tx_abort can't be used for deleting committed data
	 * anymore, need to figure out a new way to trigger blob unmap.
	 */
	print_message("Skip obsolete test\n");
	skip();
#if 0
	daos_obj_id_t	 oid;
	test_arg_t	*arg = *state;
	struct ioreq	 req;
	char		*update_buf;
	char		*fetch_buf;
	char		*enum_buf;
	char		 dkey[5] = "dkey";
	char		 akey[20];
	int		 nvme_recs = 1;
	daos_handle_t	 th[3];
	int		 i, t;
	int		 rc;

	par_barrier(PAR_COMM_WORLD);

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	/* Tx discard only currently supports DAOS_IOD_SINGLE type */
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	D_ALLOC(fetch_buf, IO_SIZE_NVME);
	assert_non_null(fetch_buf);
	D_ALLOC(update_buf, IO_SIZE_NVME);
	assert_non_null(update_buf);
	dts_buf_render(update_buf, IO_SIZE_NVME);
	D_ALLOC(enum_buf, 512);
	assert_non_null(enum_buf);

	/**
	 * Insert a few NVMe records. Write all three transactions at different
	 * akeys and the same dkey.
	 */
	for (t = 0; t < 3; t++) {
		rc = daos_tx_open(arg->coh, &th[t], NULL);
		assert_success(rc);

		for (i = 0; i < nvme_recs; i++) {
			sprintf(akey, "blob_unmap_akey%d", i);
			print_message("insert dkey:'%s', akey:'%s', tx:%d\n",
				      dkey, akey, t);
			insert_single(dkey, akey, 0, update_buf, IO_SIZE_NVME,
				      th[t], &req);
			/* Verify record was inserted */
			memset(fetch_buf, 0, IO_SIZE_NVME);
			lookup_single(dkey, akey, 0, fetch_buf, IO_SIZE_NVME,
				      th[t], &req);
			assert_memory_equal(update_buf, fetch_buf,
					    IO_SIZE_NVME);
		}
		par_barrier(PAR_COMM_WORLD);
	}

	/* Discard the NVMe records (Discard second tx) */
	print_message("Discarding second transaction\n");
	rc = daos_tx_abort(th[1], NULL);
	assert_success(rc);
	rc = daos_tx_close(th[1], NULL);
	assert_success(rc);

	par_barrier(PAR_COMM_WORLD);

	/* Wait for >= VEA_MIGRATE_INTVL */
	print_message("Wait for free extents to expire (15 sec)\n");
	sleep(15);

	/* Insert another NVMe record, will trigger free extent migration */
	sprintf(akey, "blob_unmap akey%d", nvme_recs);
	print_message("insert dkey:'%s', akey:'%s'\n", dkey, akey);
	rc = daos_tx_open(arg->coh, &th[1], NULL);
	assert_success(rc);
	insert_single(dkey, akey, 0, update_buf, IO_SIZE_NVME, th[1], &req);
	/* Verify record was inserted */
	memset(fetch_buf, 0, IO_SIZE_NVME);
	lookup_single(dkey, akey, 0, fetch_buf, IO_SIZE_NVME, th[1], &req);
	assert_memory_equal(update_buf, fetch_buf, IO_SIZE_NVME);

	print_message("Blob unmap callback triggered\n");

	for (t = 0; t < 3; t++) {
		rc = daos_tx_close(th[t], NULL);
		assert_success(rc);
	}

	D_FREE(enum_buf);
	D_FREE(fetch_buf);
	D_FREE(update_buf);
	ioreq_fini(&req);
	par_barrier(PAR_COMM_WORLD);
#endif
}

static void
punch_then_lookup(void **state)
{
	daos_obj_id_t	 oid;
	daos_key_t	dkey;
	test_arg_t	*arg = *state;
	d_sg_list_t	sgl;
	d_iov_t		sg_iovs[10];
	daos_iod_t	iod;
	daos_recx_t	recx[10];
	struct ioreq	req;
	char		data_buf[10];
	char		fetch_buf[10] = { 0 };
	int		rc;
	int		i;

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	print_message("Insert 10 records\n");
	memset(data_buf, 'a', 10);
	for (i = 0; i < 10; i++)
		insert_single_with_rxnr("dkey", "akey", i, &data_buf[i],
					1, 1, DAOS_TX_NONE, &req);

	print_message("Punch 2nd record:\n");
	punch_rec_with_rxnr("dkey", "akey", 2, 1, DAOS_TX_NONE, &req);

	print_message("Lookup non-punched records:\n");
	memset(fetch_buf, 'b', 10);
	for (i = 0; i < 10; i++) {
		d_iov_set(&sg_iovs[i], &fetch_buf[i], 1);
		recx[i].rx_idx = i;
		recx[i].rx_nr = 1;
	}
	sgl.sg_nr = 10;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = sg_iovs;

	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	d_iov_set(&dkey, "dkey", strlen("dkey"));
	iod.iod_nr	= 10;
	iod.iod_size	= 1;
	iod.iod_recxs	= recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	rc = daos_obj_fetch(req.oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL,
			    NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(sgl.sg_nr_out, 10);
	for (i = 0; i < 10; i++) {
		if (i == 2)
			assert_memory_equal(&fetch_buf[i], "b", 1);
		else
			assert_memory_equal(&fetch_buf[i], "a", 1);
	}
}

/**
 * Verify the number of record after punch and enumeration.
 */
static void
punch_enum_then_verify_record_count(void **state)
{
	daos_obj_id_t	oid;
	test_arg_t	*arg = *state;
	struct ioreq	req;
	daos_anchor_t	anchor;
	char		data_buf[100];
	char		fetch_buf[100] = { 0 };
	int		i;
	int		total_rec = 0;
	uint32_t	number;

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	memset(data_buf, 'a', 100);
	/** Insert record with 100 extents **/
	print_message("Insert single record with 100 extents\n");
	insert_single_with_rxnr("dkey", "akey", 0, data_buf,
		1, 100, DAOS_TX_NONE, &req);

	/** Punch record extent from 50 to 60 **/
	print_message("Punch record 50 to 60:\n");
	punch_rec_with_rxnr("dkey", "akey", 50, 10, DAOS_TX_NONE, &req);

	/** Lookup all the records **/
	print_message("Lookup all the records:\n");
	lookup_single_with_rxnr("dkey", "akey", 0, fetch_buf,
		1, 100, DAOS_TX_NONE, &req);

	print_message("Verify punch and non-punch data:\n");
	for (i = 0; i < 100; i++) {
		/** Verify the punch record extent from 50-59 are empty **/
		if (i >= 50 && i <= 59)
			assert_memory_equal(&fetch_buf[i], "", 1);
		/** Verify the non-punch records extent data **/
		else
			assert_memory_equal(&fetch_buf[i], "a", 1);
	}

	/** Enumerate record */
	print_message("Enumerating record extents...\n");
	total_rec = 0;
	memset(&anchor, 0, sizeof(anchor));
	while (!daos_anchor_is_eof(&anchor)) {
		daos_epoch_range_t	eprs[5];
		daos_recx_t		recxs[5];
		daos_size_t		size;

		number = 5;
		enumerate_rec(DAOS_TX_NONE, "dkey", "akey", &size,
			      &number, recxs, eprs, &anchor, true, &req);
		total_rec += number;
	}

	/** Record count should be 2**/
	assert_int_equal(total_rec, 2);
	print_message("Number of record after Enumeration = %d\n", total_rec);
}

static void
split_sgl_internal(void **state, int size)
{
	test_arg_t *arg = *state;
	char *sbuf1;
	char *sbuf2;
	daos_obj_id_t oid;
	daos_handle_t oh;
	d_iov_t dkey;
	d_sg_list_t sgl;
	d_iov_t sg_iov[2];
	daos_iod_t iod;
	daos_recx_t recx;
	int i;
	int rc;

	/** open object */
	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	D_ALLOC(sbuf1, size / 2);
	D_ALLOC(sbuf2, size / 2);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));
	memset(sbuf1, 'a', size/2);
	memset(sbuf2, 'a', size/2);
	/** init scatter/gather */
	d_iov_set(&sg_iov[0], sbuf1, size/2);
	d_iov_set(&sg_iov[1], sbuf2, size/2);
	sgl.sg_nr = 2;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = sg_iov;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));
	iod.iod_nr	= 1;
	iod.iod_size	= size;
	recx.rx_idx	= 0;
	recx.rx_nr	= 1;
	iod.iod_recxs	= &recx;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update by split sgls */
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	/** reset sg_iov */
	memset(sbuf1, 0, size/2);
	memset(sbuf2, 0, size/2);
	d_iov_set(&sg_iov[0], sbuf1, size/2);
	d_iov_set(&sg_iov[1], sbuf2, size/2);
	sgl.sg_nr = 2;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = sg_iov;

	/** Let's use different iod_size to see if fetch
	 *  can reset the correct iod_size
	 */
	iod.iod_size = size/2;
	recx.rx_idx = 0;
	recx.rx_nr = 1;
	iod.iod_recxs = &recx;

	/* fetch by split sgls */
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL,
			    NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(iod.iod_size, size);
	assert_int_equal(sgl.sg_nr_out, 2);

	for (i = 0 ; i < size/2; i++) {
		if (sbuf1[i] != 'a' || sbuf2[i] != 'a')
			print_message("i is %d\n", i);
		assert_int_equal(sbuf1[i], 'a');
		assert_int_equal(sbuf2[i], 'a');
	}
	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	D_FREE(sbuf1);
	D_FREE(sbuf2);
}

static void
split_sgl_update_fetch(void **state)
{
	/* inline transfer */
	split_sgl_internal(state, 500);
	/* bulk transfer */
	split_sgl_internal(state, 10000);
}

static void
io_pool_map_refresh_trigger(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	d_rank_t	leader;
	d_rank_t	rank = 1;

	FAULT_INJECTION_REQUIRED();

	/* needs at lest 2 targets */
	if (!test_runable(arg, 2))
		skip();

	/* Choose an rank other than leader */
	test_get_leader(arg, &leader);
	while (rank == leader)
		rank = (rank + 1) % arg->srv_nnodes;

	print_message("leader %u rank %u\n", leader, rank);
	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R1S_SPEC_RANK, 0, 0,
				arg->myrank);
	oid = dts_oid_set_rank(oid, rank);

	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				 DAOS_FORCE_REFRESH_POOL_MAP | DAOS_FAIL_ONCE,
				 0, NULL);
	par_barrier(PAR_COMM_WORLD);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	/** Insert */
	insert_single("dkey", "akey", 0, "data",
		      strlen("data") + 1, DAOS_TX_NONE, &req);

	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     0, 0, NULL);

	ioreq_fini(&req);
}

/**
 * Test to fetch the non existence keys.
 */
static void nonexistent_internal(void **state, daos_obj_id_t oid,
				 const char key_type[])
{
	test_arg_t	*arg = *state;
	struct ioreq	req;
	char		*update_buf;
	char		*fetch_buf;
	unsigned int	size   = IO_SIZE_NVME;

	D_ALLOC(update_buf, size);
	assert_non_null(update_buf);
	dts_buf_render(update_buf, size);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	D_ALLOC(fetch_buf, size);
	assert_non_null(fetch_buf);
	memset(fetch_buf, 0, size);

	/* Insert single dkey/akey,skip insert record for oid*/
	if (strcmp(key_type, "oid") != 0) {
		insert_single("exist dkey", "exist akey", 0, update_buf, size,
			DAOS_TX_NONE, &req);
	}

	if (strcmp(key_type, "akey") == 0) {
		/*Fetch non existent akey*/
		lookup_single("exist dkey", "nonexistent akey", 0, fetch_buf,
			      size, DAOS_TX_NONE, &req);
	} else if (strcmp(key_type, "dkey") == 0) {
		/*Fetch non existent dkey*/
		lookup_single("nonexistent dkey", "exist akey", 0, fetch_buf,
			      size, DAOS_TX_NONE, &req);
	} else if (strcmp(key_type, "oid") == 0) {
		/*Fetch non existent oid*/
		lookup_single("nonexistent dkey", "nonexistent akey", 0,
			      fetch_buf, size, DAOS_TX_NONE, &req);
	}

	/**
	* As per current implementation non existing keys
	* will not use the buffer at all during fetch
	*/
	assert_false(fetch_buf[0] != '\0');

	D_FREE(update_buf);
	D_FREE(fetch_buf);

	ioreq_fini(&req);
}

/**
 * Fetch mixed existing and nonexistent akey with variable
 * record sizes for both NVMe and SCM.
 */
static void fetch_mixed_keys_internal(void **state, daos_obj_id_t oid,
				      unsigned int    size,
				      daos_iod_type_t iod_type,
				      const char dkey[], const char akey[])
{
	test_arg_t   *arg = *state;
	struct ioreq req;
	char         *akeys[MANYREC_NUMRECS];
	char         *rec[MANYREC_NUMRECS];
	daos_size_t  rec_size[MANYREC_NUMRECS];
	int          rx_nr[MANYREC_NUMRECS];
	daos_off_t   offset[MANYREC_NUMRECS];
	char         *val[MANYREC_NUMRECS];
	daos_size_t  val_size[MANYREC_NUMRECS];
	int          i;

	ioreq_init(&req, arg->coh, oid, iod_type, arg);

	for (i = 0; i < MANYREC_NUMRECS; i++) {
		akeys[i] = calloc(30, 1);
		assert_non_null(akeys[i]);
		snprintf(akeys[i], 30, "%s%d", akey, i);
		D_ALLOC(rec[i], size);
		assert_non_null(rec[i]);
		dts_buf_render(rec[i], size);
		rec_size[i] = size;
		rx_nr[i]    = 1;
		offset[i]   = i * size;
		val[i]      = calloc(size, 1);
		assert_non_null(val[i]);
		val_size[i] = size;
	}

	/** Insert */
	insert(dkey, MANYREC_NUMRECS, (const char **)akeys, rec_size, rx_nr,
	       offset, (void **)rec, DAOS_TX_NONE, &req, 0);

	/* update the non existent akeys*/
	snprintf(akeys[1], 30, "%sA", akey);
	snprintf(akeys[3], 30, "%sB", akey);

	/** Lookup */
	lookup(dkey, MANYREC_NUMRECS, (const char **)akeys, offset, rec_size,
	       (void **)val, val_size, DAOS_TX_NONE, &req, false);

	/** Verify data consistency for valid akeys*/
	for (i = 0; i < MANYREC_NUMRECS; i++) {
		if (i == 1 || i == 3) {
			/* for non existent akeys, buffer will not be used */
			print_message("\tNon existent key=%s \tsize = %lu\n",
				      akeys[i], req.iod[i].iod_size);
			assert_false(val[i][0] != '\0');
		} else {
			print_message("\tExistent key=%s \tsize = %lu\n",
				      akeys[i], req.iod[i].iod_size);
			assert_int_equal(req.iod[i].iod_size, rec_size[i]);
			assert_memory_equal(val[i], rec[i], rec_size[i]);
		}
		D_FREE(val[i]);
		D_FREE(rec[i]);
		D_FREE(akeys[i]);
	}

	ioreq_fini(&req);
}

/**
 * Fetch mixed existing and nonexistent akeys in single fetch call.
 */
static void fetch_mixed_keys(void **state)
{
	test_arg_t    *arg = *state;
	daos_obj_id_t oid;

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);

	/** Test non nonexistent oid */
	print_message("Fetch nonexistent OID\n");
	nonexistent_internal(state, oid, "oid");

	/** Test non nonexistent dkey */
	print_message("Fetch nonexistent DKEY\n");
	nonexistent_internal(state, oid, "dkey");

	/** Test non nonexistent akey */
	print_message("Fetch nonexistent AKEY\n");
	nonexistent_internal(state, oid, "akey");

	print_message("DAOS_IOD_ARRAY:SCM\n");
	fetch_mixed_keys_internal(state, oid, IO_SIZE_SCM, DAOS_IOD_ARRAY,
				  "io_manyrec_scm_array dkey",
				  "io_manyrec_scm_array akey");

	print_message("DAOS_IOD_ARRAY:NVME\n");
	fetch_mixed_keys_internal(state, oid, IO_SIZE_NVME, DAOS_IOD_ARRAY,
				  "io_manyrec_nvme_array dkey",
				  "io_manyrec_nvme_array akey");

	print_message("DAOS_IOD_SINGLE:SCM\n");
	fetch_mixed_keys_internal(state, oid, IO_SIZE_SCM, DAOS_IOD_SINGLE,
				  "io_manyrec_scm_single dkey",
				  "io_manyrec_scm_single akey");

	print_message("DAOS_IOD_SINGLE:NVME\n");
	fetch_mixed_keys_internal(state, oid, IO_SIZE_NVME, DAOS_IOD_SINGLE,
				  "io_manyrec_nvme_single dkey",
				  "io_manyrec_nvme_single akey");
}

static void
io_capa_iv_fetch(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	d_rank_t	leader;

	/* needs at lest 2 targets */
	if (!test_runable(arg, 2))
		skip();

	test_get_leader(arg, &leader);
	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R1S_SPEC_RANK, 0, 0,
				arg->myrank);
	oid = dts_oid_set_rank(oid, leader == 0 ? leader + 1 : leader - 1);

	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				 DAOS_FORCE_CAPA_FETCH | DAOS_FAIL_ONCE,
				 0, NULL);
	par_barrier(PAR_COMM_WORLD);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	/** Insert */
	insert_single("dkey", "akey", 0, "data",
		      strlen("data") + 1, DAOS_TX_NONE, &req);

	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
				     0, 0, NULL);

	ioreq_fini(&req);
}

static void
io_invalid(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	d_iov_t	 dkey;
	d_sg_list_t	 sgl;
	d_iov_t	 sg_iov[2];
	daos_iod_t	 iod;
	daos_recx_t	 recx[10];
	char		 buf[32];
	char		 buf1[32];
	char		 large_buf[8192];
	char		 origin_buf[8192];
	int		 rc;

	/** open object */
	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	/** init dkey */
	d_iov_set(&dkey, "dkey", strlen("dkey"));
	d_iov_set(&iod.iod_name, "akey", strlen("akey"));

	/** smaller buffer */
	d_iov_set(&sg_iov[0], buf, 12);
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov[0];
	iod.iod_size	= 1;
	iod.iod_recxs	= recx;
	iod.iod_type	= DAOS_IOD_ARRAY;
	recx[0].rx_idx	= 0;
	recx[0].rx_nr	= 32;
	iod.iod_nr	= 1;

	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, -DER_REC2BIG);

	/** more buffers */
	memset(buf, 'b', 32);
	memset(buf1, 'b', 32);
	d_iov_set(&sg_iov[0], buf, 32);
	d_iov_set(&sg_iov[1], buf1, 32);
	sgl.sg_nr		= 2;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= sg_iov;
	iod.iod_size	= 1;
	iod.iod_recxs	= recx;
	iod.iod_type	= DAOS_IOD_ARRAY;
	recx[0].rx_idx	= 0;
	recx[0].rx_nr	= 32;
	iod.iod_nr	= 1;

	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	memset(buf, 'a', 32);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    NULL, NULL);
	assert_rc_equal(rc, 0);
	assert_memory_equal(buf, buf1, 32);

	/* larger buffer */
	memset(large_buf, 'a', 8192);
	memset(origin_buf, 'a', 8192);
	d_iov_set(&sg_iov[0], large_buf, 8192);
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= &sg_iov[0];
	iod.iod_size	= 1;
	iod.iod_recxs	= recx;
	iod.iod_type	= DAOS_IOD_ARRAY;
	recx[0].rx_idx	= 0;
	recx[0].rx_nr	= 12;
	iod.iod_nr	= 1;

	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, 0);

	memset(large_buf, 'b', 12);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl,
			    NULL, NULL);
	assert_memory_equal(large_buf, origin_buf, 8192);
	assert_rc_equal(rc, 0);
}

static void
io_fetch_retry_another_replica(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	char		fetch_buf[32];
	char		update_buf[32];

	FAULT_INJECTION_REQUIRED();

	/* needs at lest 2 targets */
	if (!test_runable(arg, 2))
		skip();

	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R2S_SPEC_RANK, 0, 0,
				arg->myrank);
	oid = dts_oid_set_rank(oid, 0);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert */
	dts_buf_render(update_buf, 32);
	insert_single("d_key_retry", "a_key_retry", 0, update_buf,
		      32, DAOS_TX_NONE, &req);

	/* Fail the first try */
	if (arg->myrank == 0)
		daos_debug_set_params(arg->group, 0, DMG_KEY_FAIL_LOC,
				 DAOS_OBJ_FETCH_DATA_LOST | DAOS_FAIL_ONCE,
				 0, NULL);

	sleep(3);
	daos_fail_loc_set(DAOS_OBJ_TRY_SPECIAL_SHARD | DAOS_FAIL_ONCE);
	daos_fail_value_set(0);

	/** Lookup */
	memset(fetch_buf, 0, 32);
	lookup_single("d_key_retry", "a_key_retry", 0, fetch_buf,
		      32, DAOS_TX_NONE, &req);

	assert_memory_equal(update_buf, fetch_buf, 32);
	ioreq_fini(&req);
}

static int
compare_oclass(daos_handle_t coh, daos_obj_id_t oid, daos_oclass_id_t ecid)
{
	daos_oclass_id_t	cid;
	int			rc;

	/** get oclass of OID */
	cid = daos_obj_id2class(oid);

	/*
	 * get the expected oclass - this is needed to convert things with GX to
	 * fit them in current system.
	 */
	rc = daos_obj_generate_oid(coh, &oid, 0, ecid, 0, 0);
	assert_rc_equal(rc, 0);
	ecid = daos_obj_id2class(oid);

	if (cid == ecid)
		return 0;
	else
		return 1;
}

static int
check_oclass(daos_handle_t coh, int domain_nr, daos_oclass_hints_t hints,
	     enum daos_otype_t type, enum daos_obj_resil res, unsigned int nr,
	     daos_oclass_id_t ecid)
{
	daos_obj_id_t		oid;
	daos_oclass_id_t        cid;
	struct daos_oclass_attr	*attr;
	char			name[MAX_OBJ_CLASS_NAME_LEN];
	int			rc;

	oid.hi = 1;
	oid.lo = 1;
	rc = daos_obj_generate_oid(coh, &oid, type, 0, hints, 0);
	assert_rc_equal(rc, 0);

	cid = daos_obj_id2class(oid);
	attr = daos_oclass_attr_find(oid, NULL);
	if (!attr) {
		rc = -EINVAL;
		goto out;
	}

	daos_oclass_id2name(cid, name);
	printf("%s\n", name);
	assert_int_equal(attr->ca_resil, res);
	if (res == DAOS_RES_REPL) {
		assert_int_equal(attr->u.rp.r_num, nr);
	} else if (res == DAOS_RES_EC) {
		assert_int_equal(attr->u.ec.e_p, nr - 1);

		if (nr == 1 || nr == 2) {
			if (domain_nr >= 18)
				assert_int_equal(attr->u.ec.e_k, 16);
			else if (domain_nr >= 10)
				assert_int_equal(attr->u.ec.e_k, 8);
			else if (domain_nr >= 6)
				assert_int_equal(attr->u.ec.e_k, 4);
			else
				assert_int_equal(attr->u.ec.e_k, 2);
		} else if (nr == 3) {
			if (domain_nr >= 20)
				assert_int_equal(attr->u.ec.e_k, 16);
			else if (domain_nr >= 12)
				assert_int_equal(attr->u.ec.e_k, 8);
			else if (domain_nr >= 8)
				assert_int_equal(attr->u.ec.e_k, 4);
			else
				assert_int_equal(attr->u.ec.e_k, 2);
		} else {
			D_ASSERT(0);
		}
	}

	/** need an easier way to determine grp nr. for now use fit for GX */
	rc = compare_oclass(coh, oid, ecid);
	if (rc) {
		char ename[MAX_OBJ_CLASS_NAME_LEN];

		daos_oclass_id2name(ecid, ename);
		fail_msg("Mismatch oclass %s vs %s\n", name, ename);
		rc = -DER_MISMATCH;
	}

out:
	return rc;
}

/** i/o to variable idx offset */
static void
oclass_auto_setting(void **state)
{
	test_arg_t		*arg = *state;
	uuid_t			uuid;
	daos_handle_t		coh;
	char			str[37];
	daos_pool_info_t	info = {0};
	struct pl_map_attr	attr = {0};
	daos_oclass_id_t	ecidx, ecid1;
	daos_prop_t             *prop = NULL;
	enum daos_otype_t	feat_kv, feat_array, feat_byte_array;
	int			rc;

	rc = daos_pool_query(arg->pool.poh, NULL, &info, NULL, NULL);
	assert_rc_equal(rc, 0);

	rc = pl_map_query(arg->pool.pool_uuid, &attr);
	assert_rc_equal(rc, 0);

	/** set the expect EC object class ID based on domain nr */
	if (attr.pa_domain_nr >= 18) {
		ecidx = OC_EC_16P1GX;
		ecid1 = OC_EC_16P1G1;
	} else if (attr.pa_domain_nr >= 10) {
		ecidx = OC_EC_8P1GX;
		ecid1 = OC_EC_8P1G1;
	} else if (attr.pa_domain_nr >= 6) {
		ecidx = OC_EC_4P1GX;
		ecid1 = OC_EC_4P1G1;
	} else {
		ecidx = OC_EC_2P1GX;
		ecid1 = OC_EC_2P1G1;
	}

	feat_array = DAOS_OT_ARRAY;
	feat_byte_array = DAOS_OT_ARRAY_BYTE;
	feat_kv = DAOS_OT_KV_HASHED;

	prop = daos_prop_alloc(1);
	assert_non_null(prop);

	/** create container with RF = 0 */
	print_message("OID settings with container RF0:\n");
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	prop->dpp_entries[0].dpe_val = DAOS_PROP_CO_REDUN_RF0;
	rc = daos_cont_create(arg->pool.poh, &uuid, prop, NULL);
	assert_rc_equal(rc, 0);
	uuid_unparse(uuid, str);
	rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	/** ALL oids by default will use OC_S1. */
	print_message("DEFAULT oid class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, 0, 0,
			  DAOS_RES_REPL, 1, OC_S1);
	assert_rc_equal(rc, 0);

	/** KV object will select SX */
	print_message("KV oid class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, 0, feat_kv,
			  DAOS_RES_REPL, 1, OC_SX);
	assert_rc_equal(rc, 0);

	/** Array object will select SX */
	print_message("ARRAY oid class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, 0, feat_array,
			  DAOS_RES_REPL, 1, OC_SX);
	assert_rc_equal(rc, 0);

	print_message("BYTE ARRAY oid class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, 0, feat_byte_array,
			  DAOS_RES_REPL, 1, OC_SX);
	assert_rc_equal(rc, 0);

	print_message("oid with DAOS_OCH_RDD_RP hint:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, DAOS_OCH_RDD_RP, 0,
			  DAOS_RES_REPL, 2, OC_RP_2G1);
	assert_rc_equal(rc, 0);

	/** KV object with EC hint should use OC_EC_NP1GX */
	print_message("KV oid with DAOS_OCH_RDD_EC hint:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, DAOS_OCH_RDD_EC, feat_kv,
			  DAOS_RES_EC, 2, ecidx);
	assert_rc_equal(rc, 0);

	/** KV object with REPL hint should use OC_RP_GX */
	print_message("KV oid with DAOS_OCH_RDD_RP hint:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, DAOS_OCH_RDD_RP, feat_kv,
			  DAOS_RES_REPL, 2, OC_RP_2GX);
	assert_rc_equal(rc, 0);

	/** Array object with RP hint should use OC_RP_GX */
	print_message("Array oid with DAOS_OCH_RDD_RP hint:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, DAOS_OCH_RDD_RP, feat_array,
			  DAOS_RES_REPL, 2, OC_RP_2GX);
	assert_rc_equal(rc, 0);

	/** object with EC hint should use OC_EC_NP1G1 */
	print_message("oid with DAOS_OCH_RDD_EC hint:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, DAOS_OCH_RDD_EC, 0,
			  DAOS_RES_EC, 2, ecid1);
	assert_rc_equal(rc, 0);

	/** object with REPL hint should use OC_RP_G1 */
	print_message("oid with DAOS_OCH_RDD_RP hint:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, DAOS_OCH_RDD_RP, 0,
			  DAOS_RES_REPL, 2, OC_RP_2G1);
	assert_rc_equal(rc, 0);

	/** RP hint with Tiny sharding should use RP_2G1 */
	print_message("oid with DAOS_OCH_RDD_RP | DAOS_OCH_SHD_TINY hint:\t");
	rc = check_oclass(coh, attr.pa_domain_nr,
			  DAOS_OCH_RDD_RP | DAOS_OCH_SHD_TINY, feat_byte_array,
			  DAOS_RES_REPL, 2, OC_RP_2G1);
	assert_rc_equal(rc, 0);

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, str, 0, NULL);
	assert_rc_equal(rc, 0);

	print_message("\nOID settings with container RF1:\n");
	/** create container with rf = 1 */
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	prop->dpp_entries[0].dpe_val = DAOS_PROP_CO_REDUN_RF1;
	rc = daos_cont_create(arg->pool.poh, &uuid, prop, NULL);
	assert_rc_equal(rc, 0);
	uuid_unparse(uuid, str);
	rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	/** default oid should be OC_RP_2G1 */
	print_message("DEFAULT oid class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, 0, 0,
			  DAOS_RES_REPL, 2, OC_RP_2G1);
	assert_rc_equal(rc, 0);

	/** oid with EC hint should be OC_EC_NP1G1 */
	print_message("oid with EC hint class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, DAOS_OCH_RDD_EC, 0,
			  DAOS_RES_EC, 2, ecid1);
	assert_rc_equal(rc, 0);

	/** KV oid should be OC_RP_2GX */
	print_message("KV oid class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, 0, feat_kv,
			  DAOS_RES_REPL, 2, OC_RP_2GX);
	assert_rc_equal(rc, 0);

	/** ARRAY oid should be OC_EC_NP1GX */
	print_message("ARRAY oid class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, 0, feat_array,
			  DAOS_RES_EC, 2, ecidx);
	assert_rc_equal(rc, 0);

	/** Byte Array oid should be OC_EC_NP1GX */
	print_message("BYTE ARRAY oid class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, 0, feat_byte_array,
			  DAOS_RES_EC, 2, ecidx);
	assert_rc_equal(rc, 0);

	/** Array object with RP hint should use OC_RP_GX */
	print_message("Byte Array with DAOS_OCH_RDD_RP hint:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, DAOS_OCH_RDD_RP, feat_byte_array,
			  DAOS_RES_REPL, 2, OC_RP_2GX);
	assert_rc_equal(rc, 0);

	print_message("KV oid with DAOS_OCH_RDD_EC hint:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, DAOS_OCH_RDD_EC, feat_kv,
			  DAOS_RES_EC, 2, ecidx);
	assert_rc_equal(rc, 0);

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, str, 0, NULL);
	assert_rc_equal(rc, 0);

	print_message("\nOID settings with container RF2:\n");
	/** create container with rf = 2 */
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	prop->dpp_entries[0].dpe_val = DAOS_PROP_CO_REDUN_RF2;
	rc = daos_cont_create(arg->pool.poh, &uuid, prop, NULL);
	assert_rc_equal(rc, 0);
	uuid_unparse(uuid, str);
	rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	/** adjust the expected EC object class ID based on domain nr */
	if (attr.pa_domain_nr >= 20) {
		ecidx = OC_EC_16P2GX;
		ecid1 = OC_EC_16P2G1;
	} else if (attr.pa_domain_nr >= 12) {
		ecidx = OC_EC_8P2GX;
		ecid1 = OC_EC_8P2G1;
	} else if (attr.pa_domain_nr >= 8) {
		ecidx = OC_EC_4P2GX;
		ecid1 = OC_EC_4P2G1;
	} else {
		ecidx = OC_EC_2P2GX;
		ecid1 = OC_EC_2P2G1;
	}

	/** default oid should be OC_RP_3G1 */
	print_message("DEFAULT oid class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, 0, 0,
			  DAOS_RES_REPL, 3, OC_RP_3G1);
	assert_rc_equal(rc, 0);

	/** KV oid should be OC_RP_3GX fit to current DAOS system */
	print_message("KV oid class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, 0, feat_kv,
			  DAOS_RES_REPL, 3, OC_RP_3GX);
	assert_rc_equal(rc, 0);

	/** oid with EC hint should be OC_EC_NP2G1 */
	print_message("oid with hint DAOS_OCH_RDD_EC class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, DAOS_OCH_RDD_EC, 0,
			  DAOS_RES_EC, 3, ecid1);
	assert_rc_equal(rc, 0);

	/** ARRAY oid should be ecid */
	print_message("ARRAY oid class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, 0, feat_array,
			  DAOS_RES_EC, 3, ecidx);
	assert_rc_equal(rc, 0);

	/** Byte Array oid should be ecid */
	print_message("BYTE ARRAY oid class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, 0, feat_byte_array,
			  DAOS_RES_EC, 3, ecidx);
	assert_rc_equal(rc, 0);

	/** Array object with RP hint should use OC_RP_GX */
	print_message("Byte Array with DAOS_OCH_RDD_RP hint:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, DAOS_OCH_RDD_RP, feat_byte_array,
			  DAOS_RES_REPL, 3, OC_RP_3GX);
	assert_rc_equal(rc, 0);

	print_message("KV oid with DAOS_OCH_RDD_EC hint:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, DAOS_OCH_RDD_EC, feat_kv,
			  DAOS_RES_EC, 3, ecidx);
	assert_rc_equal(rc, 0);

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, str, 0, NULL);
	assert_rc_equal(rc, 0);

	print_message("\nOID settings with container RF3:\n");
	/** create container with rf = 3 */
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	prop->dpp_entries[0].dpe_val = DAOS_PROP_CO_REDUN_RF3;
	rc = daos_cont_create(arg->pool.poh, &uuid, prop, NULL);
	assert_rc_equal(rc, 0);
	uuid_unparse(uuid, str);
	rc = daos_cont_open(arg->pool.poh, str, DAOS_COO_RW, &coh, NULL, NULL);
	assert_rc_equal(rc, 0);

	/** default oid should be OC_RP_4G1 */
	print_message("DEFAULT oid class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, 0, 0,
			  DAOS_RES_REPL, 4, OC_RP_4G1);
	assert_rc_equal(rc, 0);

	/** KV oid should be OC_RP_4GX fit to current DAOS system */
	print_message("KV oid class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, 0, feat_kv,
			  DAOS_RES_REPL, 4, OC_RP_4GX);
	assert_rc_equal(rc, 0);

	/** oid with EC hint should still be OC_RP_4GX */
	print_message("oid with EC hint class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, DAOS_OCH_RDD_EC, 0,
			  DAOS_RES_REPL, 4, OC_RP_4G1);
	assert_rc_equal(rc, 0);

	/** ARRAY oid should be OC_RP_4GX */
	print_message("ARRAY oid class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, 0, feat_array,
			  DAOS_RES_REPL, 4, OC_RP_4GX);
	assert_rc_equal(rc, 0);

	/** Byte Array oid should be OC_RP_4GX */
	print_message("BYTE ARRAY oid class:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, 0, feat_byte_array,
			  DAOS_RES_REPL, 4, OC_RP_4GX);
	assert_rc_equal(rc, 0);

	print_message("KV oid with DAOS_OCH_RDD_EC hint:\t");
	rc = check_oclass(coh, attr.pa_domain_nr, DAOS_OCH_RDD_EC, feat_kv,
			  DAOS_RES_REPL, 4, OC_RP_4GX);
	assert_rc_equal(rc, 0);

	rc = daos_cont_close(coh, NULL);
	assert_rc_equal(rc, 0);
	rc = daos_cont_destroy(arg->pool.poh, str, 0, NULL);
	assert_rc_equal(rc, 0);

}

static void
int_key_setting(void **state, int size)
{
	test_arg_t              *arg = *state;
	daos_obj_id_t		oid;
	daos_handle_t		oh;
	d_iov_t			dkey;
	char			*dkey_buf;
	char			*akey_buf;
	d_sg_list_t		sgl;
	d_iov_t			sg_iov;
	daos_iod_t		iod;
	char			buf[STACK_BUF_LEN];
	int                     rc;

	dkey_buf = malloc(size);
	if (!dkey_buf) {
		print_message("allocation memory failed\n");
		return;
	}

	akey_buf = malloc(size);
	if (!akey_buf) {
		print_message("allocation memory failed\n");
		free(dkey_buf);
		return;
	}

	/*
	 * Object with integer dkey / akey should fail IO with -DER_INVAL if
	 * key size is not correct.
	 */
	oid = daos_test_oid_gen(arg->coh, OC_S1, DAOS_OT_DKEY_UINT64, 0,
				arg->myrank);

	dts_buf_render(buf, STACK_BUF_LEN);
	dts_buf_render(dkey_buf, size);
	dts_buf_render(akey_buf, size);

	/** init dkey */
	d_iov_set(&dkey, dkey_buf, size);

	/** init scatter/gather */
	d_iov_set(&sg_iov, buf, sizeof(buf));
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	d_iov_set(&iod.iod_name, akey_buf, size);
	iod.iod_size    = STACK_BUF_LEN;
	iod.iod_type	= DAOS_IOD_SINGLE;
	iod.iod_recxs	= NULL;
	iod.iod_nr	= 1;

	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	print_message("Update with invalid DKEY\n");
	/** update record */
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	print_message("Fetch with invalid DKEY\n");
	/** fetch record size */
	iod.iod_size	= DAOS_REC_ANY;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod,
			    NULL, NULL, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	print_message("Punch with invalid DKEY\n");
	/** Punch Dkey */
	rc = daos_obj_punch_dkeys(oh, DAOS_TX_NONE, 0, 1, &dkey, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	oid = daos_test_oid_gen(arg->coh, OC_S1, DAOS_OT_AKEY_UINT64, 0,
				arg->myrank);

	rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh, NULL);
	assert_rc_equal(rc, 0);

	print_message("Update with invalid AKEY\n");
	/** update record */
	iod.iod_size	= STACK_BUF_LEN;
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod, &sgl, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	print_message("Fetch with invalid AKEY\n");
	/** fetch record size */
	iod.iod_size	= DAOS_REC_ANY;
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, 0, &dkey, 1, &iod,
			    NULL, NULL, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	print_message("Punch with invalid AKEY\n");
	/** Punch Akey */
	rc = daos_obj_punch_akeys(oh, DAOS_TX_NONE, 0, &dkey, 1,
				  &iod.iod_name, NULL);
	assert_rc_equal(rc, -DER_INVAL);

	rc = daos_obj_close(oh, NULL);
	assert_rc_equal(rc, 0);

	free(dkey_buf);
	free(akey_buf);
}

static void invalid_int_key_setting(void **state)
{
	int_key_setting(state, 128);
	int_key_setting(state, 3);
}

static void
enum_recxs_with_aggregation_internal(void **state, bool incr)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	char		data_buf[10];
	char		*aggr_disabled = "disabled";
	char		*aggr_set_time = "time";
	daos_anchor_t	anchor;
	bool		enable_agg = false;
	int		total_size = 0;
	int		i;
	int		rc;

	rc = set_pool_reclaim_strategy(arg, aggr_disabled);
	assert_rc_equal(rc, 0);
	sleep(10);

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	print_message("Insert 10 records\n");
	memset(data_buf, 'a', 10);
	for (i = 0; i < 10; i++)
		insert_single_with_rxnr("dkey", "akey", i, &data_buf[i],
					1, 1, DAOS_TX_NONE, &req);
	total_size = 0;
	memset(&anchor, 0, sizeof(anchor));
	while (!daos_anchor_is_eof(&anchor)) {
		daos_epoch_range_t	eprs[3];
		daos_recx_t		recxs[3] = { 0 };
		daos_size_t		size;
		uint32_t		number = 3;

		enumerate_rec(DAOS_TX_NONE, "dkey", "akey", &size,
			      &number, recxs, eprs, &anchor, incr, &req);
		for (i = 0; i < number; i++)
			total_size += recxs[i].rx_nr;

		if (!enable_agg) {
			/* Enabled Pool Aggrgation */
			print_message("enable aggregation\n");
			rc = set_pool_reclaim_strategy(arg, aggr_set_time);
			assert_rc_equal(rc, 0);
			daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
					      DAOS_FORCE_EC_AGG, 0, NULL);
			sleep(40);
			daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC,
					      0, 0, NULL);
			enable_agg = true;
		}
	}

	assert_rc_equal(total_size, 10);
}

static void
enum_recxs_with_aggregation(void **state)
{
	enum_recxs_with_aggregation_internal(state, true);
	enum_recxs_with_aggregation_internal(state, false);
}

static void
io_tx_convert(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;

	oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	par_barrier(PAR_COMM_WORLD);

	arg->fail_loc = DAOS_FAIL_TX_CONVERT | DAOS_FAIL_ALWAYS;
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	/** Insert */
	insert_single("dkey", "akey", 0, "data",
		      strlen("data") + 1, DAOS_TX_NONE, &req);

	punch_obj(DAOS_TX_NONE, &req);
	ioreq_fini(&req);
}

static void
obj_open_perf(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	*oh;
	uint64_t	start_usec, end_usec;
	float		opens_per_sec;
	int		i, nr, rc;

	nr = 10000;
	D_ALLOC_ARRAY(oh, nr);
	assert_non_null(oh);

	start_usec = daos_getutime();
	for (i = 0; i < nr; i++) {
		oid = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
		rc = daos_obj_open(arg->coh, oid, DAOS_OO_RW, &oh[i], NULL);
		assert_rc_equal(rc, 0);
	}
	end_usec = daos_getutime();
	opens_per_sec = (nr * 1000.0 * 1000) / (end_usec - start_usec);

	print_message("opens per second %.2f (total #obj_opens %d)\n", opens_per_sec, nr);

	for (i = 0; i < nr; i++) {
		rc = daos_obj_close(oh[i], NULL);
		assert_rc_equal(rc, 0);
	}

	D_FREE(oh);
}

static bool
oid_in_list(daos_obj_id_t oid, daos_obj_id_t *oid_list, uint32_t nr)
{
	uint32_t i;

	for (i = 0; i < nr; i++)
		if (oid.lo == oid_list[i].lo && oid.hi == oid_list[i].hi)
			return true;

	return false;
}

#define OIT_TEST_OID_NR	(128)

static int
oit_get_markdata_as1(daos_obj_id_t oid, d_iov_t *marker)
{
	uint64_t	*data;

	if (marker == NULL || marker->iov_len == 0 || marker->iov_buf == NULL)
		return 0;
	data = marker->iov_buf;
	if (*data != 1)
		return 0;
	return 1;
}

static void
oit_eq_init(struct ioreq *req)
{
	int	rc;

	if (req->arg->async) {
		rc = daos_event_init(&req->ev, req->arg->eq, NULL);
		assert_rc_equal(rc, 0);
	}
}

static void
oit_eq_fini(struct ioreq *req)
{
	int	rc;
	bool	ev_flag;

	if (req->arg->async) {
		rc = daos_event_test(&req->ev, DAOS_EQ_WAIT, &ev_flag);
		assert_rc_equal(rc, 0);
		assert_int_equal(ev_flag, true);
		assert_int_equal(req->ev.ev_error, 0);
		rc = daos_event_fini(&req->ev);
	}
}

static void
oit_mark(daos_handle_t oh, daos_obj_id_t oid, d_iov_t *marker, struct ioreq *req)
{
	int rc;

	oit_eq_init(req);
	rc = daos_oit_mark(oh, oid, marker, req->arg->async ? &req->ev : NULL);
	assert_rc_equal(rc, 0);
	oit_eq_fini(req);
}

static void
oit_list_filter(void **state)
{
	test_arg_t		*arg0 = *state;
	test_arg_t		*arg = NULL;
	struct ioreq		req;
	daos_obj_id_t		*oid, oid_new;
	daos_obj_id_t		*oid_list;
	char			*ow_buf;
	char			*fbuf;
	d_iov_t			marker;
	uint64_t		mark_data;
	daos_size_t		size = 128;
	daos_epoch_t		snap_epoch;
	daos_handle_t		toh;
	daos_anchor_t		anchor;
	uint32_t		oids_nr;
	int			total;
	int			i, rc;
	bool			ev_flag;

	rc = test_setup((void **)&arg, SETUP_CONT_CONNECT, arg0->multi_rank,
			SMALL_POOL_SIZE, 0, NULL);
	assert_success(rc);

	/* Alloc and set buffer to be a string*/
	D_ALLOC(ow_buf, size);
	assert_non_null(ow_buf);
	dts_buf_render(ow_buf, size);
	/* Alloc the fetch buffer */
	D_ALLOC(fbuf, size);
	assert_non_null(fbuf);

	D_ALLOC_ARRAY(oid, OIT_TEST_OID_NR);
	assert_non_null(oid);
	D_ALLOC_ARRAY(oid_list, OIT_TEST_OID_NR);
	assert_non_null(oid_list);

	for (i = 0; i < OIT_TEST_OID_NR; i++) {
		const char		dkey[] = "dkey";
		const char		akey[] = "akey";

		oid[i] = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
		ioreq_init(&req, arg->coh, oid[i], DAOS_IOD_ARRAY, arg);

		insert_single_with_rxnr(dkey, akey, /*idx*/0, ow_buf, size,
					1, DAOS_TX_NONE, &req);
		memset(fbuf, 0, size);
		lookup_single_with_rxnr(dkey, akey, /*idx*/0, fbuf, size, size,
					DAOS_TX_NONE, &req);
		assert_memory_equal(ow_buf, fbuf, size);
		print_message("updated oid[%d] = "DF_OID"\n", i, DP_OID(oid[i]));
		ioreq_fini(&req);
	}

	rc = daos_cont_create_snap(arg->coh, &snap_epoch, NULL, NULL);
	assert_rc_equal(rc, 0);

	arg->async = arg0->async;
	req.arg = arg;
	oit_eq_init(&req);
	rc = daos_cont_snap_oit_create(arg->coh, snap_epoch, NULL, arg->async ? &req.ev : NULL);
	assert_rc_equal(rc, 0);
	oit_eq_fini(&req);

	oit_eq_init(&req);
	rc = daos_oit_open(arg->coh, snap_epoch, &toh, arg->async ? &req.ev : NULL);
	assert_rc_equal(rc, 0);
	oit_eq_fini(&req);

	print_message("list oit after updated %d objests\n", OIT_TEST_OID_NR);
	memset(&anchor, 0, sizeof(anchor));
	for (total = 0; true; ) {
		oids_nr = OIT_TEST_OID_NR;
		oit_eq_init(&req);
		rc = daos_oit_list(toh, oid_list, &oids_nr, &anchor, arg->async ? &req.ev : NULL);
		assert_rc_equal(rc, 0);
		oit_eq_fini(&req);
		assert_int_equal(oids_nr + total, OIT_TEST_OID_NR);
		for (i = 0; i < oids_nr; i++) {
			print_message("list oid[%d] ="DF_OID"\n", total, DP_OID(oid_list[i]));
			total++;
			D_ASSERT(oid_in_list(oid_list[i], oid, OIT_TEST_OID_NR));
		}
		if (daos_anchor_is_eof(&anchor)) {
			print_message("listed %d objects\n", total);
			break;
		}
	}

	print_message("mark a few oids in the OIT\n");
	mark_data = 1;
	d_iov_set(&marker, &mark_data, sizeof(mark_data));
	oit_mark(toh, oid[0], &marker, &req);
	oit_mark(toh, oid[1], &marker, &req);
	mark_data = 0;
	oit_mark(toh, oid[7], &marker, &req);
	oit_mark(toh, oid[15], &marker, &req);

	print_message("mark a non-existed oid should fail with -DER_NONEXIST\n");
	oid_new = daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
	oit_eq_init(&req);
	rc = daos_oit_mark(toh, oid_new, &marker, arg->async ? &req.ev : NULL);
	if (req.arg->async) {
		assert_rc_equal(rc, 0);
		rc = daos_event_test(&req.ev, DAOS_EQ_WAIT, &ev_flag);
		assert_rc_equal(rc, 0);
		assert_int_equal(ev_flag, true);
		assert_int_equal(req.ev.ev_error, -DER_NONEXIST);
		rc = daos_event_fini(&req.ev);
	} else {
		assert_rc_equal(rc, -DER_NONEXIST);
	}

	print_message("list un-marked oids in the OIT\n");
	memset(&anchor, 0, sizeof(anchor));
	for (total = 0; true; ) {
		oids_nr = OIT_TEST_OID_NR;
		oit_eq_init(&req);
		rc = daos_oit_list_unmarked(toh, oid_list, &oids_nr, &anchor,
					    arg->async ? &req.ev : NULL);
		assert_rc_equal(rc, 0);
		oit_eq_fini(&req);
		assert_int_equal(oids_nr + total, OIT_TEST_OID_NR - 4);
		for (i = 0; i < oids_nr; i++) {
			print_message("list oid[%d] ="DF_OID"\n", total, DP_OID(oid_list[i]));
			total++;
			D_ASSERT(oid_in_list(oid_list[i], oid, OIT_TEST_OID_NR));
		}
		if (oids_nr > 0) {
			D_ASSERT(!oid_in_list(oid[0], oid_list, oids_nr));
			D_ASSERT(!oid_in_list(oid[1], oid_list, oids_nr));
			D_ASSERT(!oid_in_list(oid[7], oid_list, oids_nr));
			D_ASSERT(!oid_in_list(oid[15], oid_list, oids_nr));
		}
		if (daos_anchor_is_eof(&anchor)) {
			print_message("listed %d objects\n", total);
			break;
		}
	}

	print_message("clear an oid's marker in the OIT\n");
	oit_eq_init(&req);
	rc = daos_oit_mark(toh, oid[15], NULL, arg->async ? &req.ev : NULL);
	assert_rc_equal(rc, 0);
	oit_eq_fini(&req);
	print_message("list un-marked oids in the OIT\n");
	memset(&anchor, 0, sizeof(anchor));
	for (total = 0; true; ) {
		oids_nr = OIT_TEST_OID_NR;
		oit_eq_init(&req);
		rc = daos_oit_list_unmarked(toh, oid_list, &oids_nr, &anchor,
					    arg->async ? &req.ev : NULL);
		assert_rc_equal(rc, 0);
		oit_eq_fini(&req);
		assert_int_equal(oids_nr + total, OIT_TEST_OID_NR - 3);
		for (i = 0; i < oids_nr; i++) {
			print_message("list oid[%d] ="DF_OID"\n", total, DP_OID(oid_list[i]));
			total++;
			D_ASSERT(oid_in_list(oid_list[i], oid, OIT_TEST_OID_NR));
		}
		if (oids_nr > 0) {
			D_ASSERT(!oid_in_list(oid[0], oid_list, oids_nr));
			D_ASSERT(!oid_in_list(oid[1], oid_list, oids_nr));
			D_ASSERT(!oid_in_list(oid[7], oid_list, oids_nr));
			D_ASSERT(oid_in_list(oid[15], oid_list, oids_nr));
		}
		if (daos_anchor_is_eof(&anchor)) {
			print_message("listed %d objects\n", total);
			break;
		}
	}

	print_message("list oids with marker as 1\n");
	memset(&anchor, 0, sizeof(anchor));
	for (total = 0; true; ) {
		oids_nr = OIT_TEST_OID_NR;
		oit_eq_init(&req);
		rc = daos_oit_list_filter(toh, oid_list, &oids_nr, &anchor, oit_get_markdata_as1,
					  arg->async ? &req.ev : NULL);
		assert_rc_equal(rc, 0);
		oit_eq_fini(&req);
		assert_int_equal(oids_nr + total, 2);
		for (i = 0; i < oids_nr; i++) {
			print_message("list oid[%d] ="DF_OID"\n", total, DP_OID(oid_list[i]));
			total++;
			D_ASSERT(oid_in_list(oid_list[i], oid, OIT_TEST_OID_NR));
		}
		if (oids_nr > 0) {
			D_ASSERT(oid_in_list(oid[0], oid_list, oids_nr));
			D_ASSERT(oid_in_list(oid[1], oid_list, oids_nr));
		}
		if (daos_anchor_is_eof(&anchor)) {
			print_message("listed %d objects\n", total);
			break;
		}
	}

	oit_eq_init(&req);
	rc = daos_cont_snap_oit_destroy(arg->coh, toh, arg->async ? &req.ev : NULL);
	D_ASSERT(rc == 0);
	oit_eq_fini(&req);

	oit_eq_init(&req);
	rc = daos_oit_close(toh, arg->async ? &req.ev : NULL);
	D_ASSERT(rc == 0);
	oit_eq_fini(&req);

	oit_eq_init(&req);
	rc = daos_oit_open(arg->coh, snap_epoch, &toh, arg->async ? &req.ev : NULL);
	if (req.arg->async) {
		rc = daos_event_test(&req.ev, DAOS_EQ_WAIT, &ev_flag);
		assert_rc_equal(rc, 0);
		assert_int_equal(ev_flag, true);
		assert_int_equal(req.ev.ev_error, -DER_NONEXIST);
		rc = daos_event_fini(&req.ev);
	} else {
		assert_rc_equal(rc, -DER_NONEXIST);
	}

	D_FREE(fbuf);
	D_FREE(ow_buf);
	D_FREE(oid);
	D_FREE(oid_list);
	test_teardown((void **)&arg);
}

static const struct CMUnitTest io_tests[] = {
	{ "IO1: simple update/fetch/verify",
	  io_simple, async_disable, test_case_teardown},
	{ "IO2: simple update/fetch/verify (async)",
	  io_simple, async_enable, test_case_teardown},
	{ "IO3: i/o with variable rec size",
	  io_var_rec_size, async_disable, test_case_teardown},
	{ "IO4: i/o with variable rec size(async)",
	  io_var_rec_size, async_enable, test_case_teardown},
	{ "IO5: i/o with variable dkey size",
	  io_var_dkey_size, async_enable, test_case_teardown},
	{ "IO6: i/o with variable akey size",
	  io_var_akey_size, async_disable, test_case_teardown},
	{ "IO7: i/o with variable index",
	  io_var_idx_offset, async_enable, test_case_teardown},
	{ "IO8: variable size record overwrite",
	  io_overwrite, async_enable, test_case_teardown},
	{ "IO9: simple enumerate",
	  enumerate_simple, async_disable, test_case_teardown},
	{ "IO10: simple punch",
	  punch_simple, async_disable, test_case_teardown},
	{ "IO11: multiple record update/fetch/verify",
	  io_manyrec, async_disable, test_case_teardown},
	{ "IO12: complex update/fetch/verify",
	  io_complex, async_disable, test_case_teardown},
	{ "IO13: basic byte array with record size fetching",
	  basic_byte_array, async_disable, test_case_teardown},
	{ "IO14: timeout simple update",
	  io_simple_update_timeout, async_disable, test_case_teardown},
	{ "IO15: timeout simple fetch",
	  io_simple_fetch_timeout, async_disable, test_case_teardown},
	{ "IO16: timeout on 1 shard simple update",
	  io_simple_update_timeout_single, async_disable, test_case_teardown},
	{ "IO17: transaction discard",
	  tx_discard, async_disable, test_case_teardown},
	{ "IO18: transaction commit",
	  tx_commit, async_disable, test_case_teardown},
	{ "IO19: no space", io_nospace, async_disable, test_case_teardown},
	{ "IO20: fetch size with NULL sgl",
	  fetch_size, async_disable, test_case_teardown},
	{ "IO21: io crt error",
	  io_simple_update_crt_error, async_disable, test_case_teardown},
	{ "IO22: io crt error (async)",
	  io_simple_update_crt_error, async_enable, test_case_teardown},
	{ "IO23: io crt req create timeout (sync)",
	  io_simple_update_crt_req_error, async_disable, test_case_teardown},
	{ "IO24: io crt req create timeout (async)",
	  io_simple_update_crt_req_error, async_enable, test_case_teardown},
	{ "IO25: Read from unwritten records",
	  read_empty_records, async_disable, test_case_teardown},
	{ "IO26: Read from large unwritten records",
	  read_large_empty_records, async_disable, test_case_teardown},
	{ "IO27: written records repeatedly",
	  write_record_multiple_times, async_disable, test_case_teardown},
	{ "IO28: echo fetch/update",
	  echo_fetch_update, async_disable, test_case_teardown},
	{ "IO29: basic object key query testing",
	  io_obj_key_query, async_disable, test_case_teardown},
	{ "IO30: shard target idx change cause retry",
	  tgt_idx_change_retry, async_enable, test_case_teardown},
	{ "IO31: fetch when all replicas unavailable",
	  fetch_replica_unavail, async_enable, test_case_teardown},
	{ "IO32: update with overlapped recxs",
	  update_overlapped_recxs, async_enable, test_case_teardown},
	{ "IO33: trigger blob unmap",
	  blob_unmap_trigger, async_disable, test_case_teardown},
	{ "IO34: punch then lookup",
	  punch_then_lookup, async_disable, test_case_teardown},
	{ "IO35: split update fetch",
	  split_sgl_update_fetch, async_disable, test_case_teardown},
	{ "IO36: trigger server pool map refresh",
	  io_pool_map_refresh_trigger, async_disable, test_case_teardown},
	{ "IO37: Fetch existing and nonexistent akeys in single fetch call",
	  fetch_mixed_keys, async_disable, test_case_teardown},
	{ "IO38: force capability IV fetch",
	  io_capa_iv_fetch, async_disable, test_case_teardown},
	{ "IO39: Update with invalid sg and record",
	  io_invalid, async_disable, test_case_teardown},
	{ "IO40: Record count after punch/enumeration",
	  punch_enum_then_verify_record_count, async_disable,
	  test_case_teardown},
	{ "IO41: IO Rewritten data fetch and validate pool size",
	  io_rewritten_array_with_mixed_size, async_disable,
	  test_case_teardown},
	{ "IO42: IO fetch from an alternative node after first try failed",
	  io_fetch_retry_another_replica, async_disable,
	  test_case_teardown},
	{ "IO43: Object class selection",
	  oclass_auto_setting, async_disable, test_case_teardown},
	{ "IO44: INT dkey/akey checks",
	  invalid_int_key_setting, async_disable, test_case_teardown},
	{ "IO45: enum recxs with aggregation",
	  enum_recxs_with_aggregation, async_disable, test_case_teardown},
	{ "IO46: tx convert",
	  io_tx_convert, async_disable, test_case_teardown},
	{ "IO47: obj_open perf", obj_open_perf, async_disable, test_case_teardown},
	{ "IO48: oit_list_filter", oit_list_filter, async_disable, test_case_teardown},
	{ "IO49: oit_list_filter async", oit_list_filter, async_enable, test_case_teardown},
};

int
obj_setup_internal(void **state)
{
	test_arg_t	*arg;

	arg = *state;

	if (arg->pool.pool_info.pi_nnodes < 2)
		dts_obj_class = OC_S1;
	else if (arg->obj_class != OC_UNKNOWN)
		dts_obj_class = arg->obj_class;

	dt_redun_lvl = DAOS_PROP_CO_REDUN_RANK;
	return 0;
}

int
obj_setup(void **state)
{
	int	rc;

	rc = test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE,
			0, NULL);
	if (rc != 0)
		return rc;

	return obj_setup_internal(state);
}

int
run_daos_io_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;
	char oclass[MAX_OBJ_CLASS_NAME_LEN + 1] = {0};
	char buf[32];

	par_barrier(PAR_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(io_tests);
		sub_tests = NULL;
	}

	if (dt_obj_class != OC_UNKNOWN) {
		oclass[0] = '_';
		daos_oclass_id2name(dt_obj_class, &oclass[1]);
	}
	snprintf(buf, sizeof(buf), "DAOS_IO%s", oclass);
	buf[sizeof(buf) - 1] = 0;

	rc = run_daos_sub_tests(buf, io_tests,
				ARRAY_SIZE(io_tests), sub_tests, sub_tests_size,
				obj_setup, test_teardown);

	par_barrier(PAR_COMM_WORLD);
	return rc;
}
