/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
 * This file is part of daos
 *
 * tests/suite/daos_obj.c
 */
#define D_LOGFAC	DD_FAC(tests)
#include "daos_iotest.h"

#define IO_SIZE_NVME	(5ULL << 10) /* all records  >= 4K */
#define	IO_SIZE_SCM	64

int dts_obj_class	= DAOS_OC_R2S_RW;
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
		assert_int_equal(rc, 0);
	}

	arg->expect_result = 0;
	daos_fail_loc_set(arg->fail_loc);
	daos_fail_value_set(arg->fail_value);

	/* init sgl */
	for (i = 0; i < IOREQ_SG_IOD_NR; i++) {
		req->sgl[i].sg_nr = IOREQ_SG_NR;
		req->sgl[i].sg_iovs = req->val_iov[i];
	}

	/* init csum */
	daos_csum_set(&req->csum, &req->csum_buf[0], UPDATE_CSUM_SIZE);

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

		/* epoch descriptor */
		req->iod[i].iod_eprs = req->erange[i];

		req->iod[i].iod_kcsum.cs_csum = NULL;
		req->iod[i].iod_kcsum.cs_buf_len = 0;
		req->iod[i].iod_kcsum.cs_len = 0;
		req->iod[i].iod_type = iod_type;

	}
	D_DEBUG(DF_MISC, "open oid="DF_OID"\n", DP_OID(oid));

	/** open the object */
	rc = daos_obj_open(coh, oid, 0, 0, &req->oh,
			   req->arg->async ? &req->ev : NULL);
	assert_int_equal(rc, 0);

	if (arg->async) {
		rc = daos_event_test(&req->ev, DAOS_EQ_WAIT, &ev_flag);
		assert_int_equal(rc, 0);
		assert_int_equal(ev_flag, true);
		assert_int_equal(req->ev.ev_error, 0);
	}
}

void
ioreq_fini(struct ioreq *req)
{
	int rc;

	rc = daos_obj_close(req->oh, NULL);
	assert_int_equal(rc, 0);

	req->arg->fail_loc = 0;
	req->arg->fail_value = 0;
	daos_fail_loc_set(0);
	if (req->arg->async) {
		rc = daos_event_fini(&req->ev);
		assert_int_equal(rc, 0);
	}
}

/* no wait for async insert, for sync insert it still will block */
static void
insert_internal_nowait(daos_key_t *dkey, int nr, daos_sg_list_t *sgls,
		       daos_iod_t *iods, daos_epoch_t epoch, struct ioreq *req)
{
	int rc;

	/** execute update operation */
	rc = daos_obj_update(req->oh, epoch, dkey, nr, iods, sgls,
			     req->arg->async ? &req->ev : NULL);
	if (!req->arg->async)
		assert_int_equal(rc, req->arg->expect_result);
}

static void
ioreq_dkey_set(struct ioreq *req, const char *dkey)
{
	daos_iov_set(&req->dkey, (void *)dkey, strlen(dkey));
}

static void
ioreq_akey_set(struct ioreq *req, const char *akey)
{
	daos_iov_set(&req->akey, (void *)akey, strlen(akey));
}

static void
ioreq_io_akey_set(struct ioreq *req, const char **akey, int nr)
{
	int i;

	assert_in_range(nr, 1, IOREQ_SG_IOD_NR);
	/** akey */
	for (i = 0; i < nr; i++)
		daos_iov_set(&req->iod[i].iod_name, (void *)akey[i],
			     strlen(akey[i]));
}

static void
ioreq_sgl_simple_set(struct ioreq *req, void **value, daos_size_t *data_size,
		     int nr)
{
	daos_sg_list_t *sgl = req->sgl;
	int i;

	assert_in_range(nr, 1, IOREQ_SG_IOD_NR);
	for (i = 0; i < nr; i++) {
		sgl[i].sg_nr = 1;
		sgl[i].sg_nr_out = 0;
		daos_iov_set(&sgl[i].sg_iovs[0], value[i], data_size[i]);
	}
}

static void
ioreq_iod_simple_set(struct ioreq *req, daos_size_t *iod_size, bool lookup,
		     uint64_t *idx, daos_epoch_t *epoch, int nr, int *rx_nr)
{
	daos_iod_t *iod = req->iod;
	int i;

	assert_in_range(nr, 1, IOREQ_SG_IOD_NR);
	for (i = 0; i < nr; i++) {
		/** record extent */
		iod[i].iod_type = req->iod_type;
		iod[i].iod_size = iod_size[i];
		if (req->iod_type == DAOS_IOD_ARRAY) {
			iod[i].iod_recxs[0].rx_idx = idx[i] + i * SEGMENT_SIZE;
			iod[i].iod_recxs[0].rx_nr = rx_nr[i];
		}

		iod[i].iod_eprs[0].epr_lo = *epoch;
		iod[i].iod_eprs[0].epr_hi = lookup ? *epoch : DAOS_EPOCH_MAX;

		iod[i].iod_nr = 1;
	}
}

static void
ioreq_iod_recxs_set(struct ioreq *req, int idx, daos_size_t size,
		   daos_recx_t *recxs, daos_epoch_range_t *eprs, int nr)
{
	daos_iod_t *iod = &req->iod[idx];

	assert_in_range(nr, 1, IOREQ_IOD_NR);
	iod->iod_type = req->iod_type;
	iod->iod_size = size;
	if (req->iod_type == DAOS_IOD_ARRAY) {
		iod->iod_nr = nr;
		iod->iod_recxs = recxs;
		iod->iod_eprs = eprs;
	} else {
		iod->iod_nr = 1;
	}
}

void
insert_recxs_nowait(const char *dkey, const char *akey, daos_size_t iod_size,
		    daos_epoch_t epoch, daos_recx_t *recxs, int nr, void *data,
		    daos_size_t data_size, struct ioreq *req)
{
	daos_epoch_range_t	eprs[IOREQ_IOD_NR];
	int			i;

	assert_in_range(nr, 1, IOREQ_IOD_NR);

	/* dkey */
	ioreq_dkey_set(req, dkey);

	/* akey */
	ioreq_io_akey_set(req, &akey, 1);

	/* set sgl */
	assert_int_not_equal(data, NULL);
	ioreq_sgl_simple_set(req, &data, &data_size, 1);

	/* iod, recxs */
	for (i = 0; i < nr; i++) {
		eprs[i].epr_lo = epoch;
		eprs[i].epr_hi = epoch;
	}
	ioreq_iod_recxs_set(req, 0, iod_size, recxs, eprs, nr);

	insert_internal_nowait(&req->dkey, 1, req->sgl, req->iod, epoch, req);
}

void
insert_nowait(const char *dkey, int nr, const char **akey,
	      daos_size_t *iod_size, int *rx_nr, uint64_t *idx, void **val,
	      daos_epoch_t *epoch, struct ioreq *req)
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
	ioreq_iod_simple_set(req, iod_size, false, idx, epoch, nr, rx_nr);

	insert_internal_nowait(&req->dkey, nr, val == NULL ? NULL : req->sgl,
			       req->iod, *epoch, req);
}

void
insert_test(struct ioreq *req, uint64_t timeout)
{
	bool	ev_flag;
	int	rc;

	if (!req->arg->async)
		return;

	rc = daos_event_test(&req->ev, timeout, &ev_flag);
	assert_int_equal(rc, 0);
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
       int *rx_nr, uint64_t *idx, void **val, daos_epoch_t *epoch,
       struct ioreq *req)
{
	insert_nowait(dkey, nr, akey, iod_size, rx_nr, idx, val, epoch, req);
	insert_wait(req);
}

/**
 * Helper funtion to insert a single record (nr=1). The number of record
 * extents is set to 1, meaning the iod size is equal to data size (record size(
 * in this simple use case.
 */
void
insert_single(const char *dkey, const char *akey, uint64_t idx, void *value,
	      daos_size_t iod_size, daos_epoch_t epoch, struct ioreq *req)
{
	int	rx_nr = 1;

	insert(dkey, /*nr*/1, &akey, &iod_size, &rx_nr, &idx, &value,
	       &epoch, req);
}

/**
 * Helper function to insert a single record (nr=1) by inserting multiple
 * contiguous record extents and specifying an iod size (extent/array boundary).
 */
void
insert_single_with_rxnr(const char *dkey, const char *akey, uint64_t idx,
			void *value, daos_size_t iod_size, int rx_nr,
			daos_epoch_t epoch, struct ioreq *req)
{
	insert(dkey, /*nr*/1, &akey, &iod_size, &rx_nr, &idx, &value, &epoch,
	       req);
}

void
insert_recxs(const char *dkey, const char *akey, daos_size_t iod_size,
	     daos_epoch_t epoch, daos_recx_t *recxs, int nr, void *data,
	     daos_size_t data_size, struct ioreq *req)
{
	insert_recxs_nowait(dkey, akey, iod_size, epoch, recxs, nr, data,
			    data_size, req);
	insert_wait(req);
}

void
punch_obj(daos_epoch_t eph, struct ioreq *req)
{
	int rc;

	rc = daos_obj_punch(req->oh, eph, NULL);
	assert_int_equal(rc, 0);
}

void
punch_dkey(const char *dkey, daos_epoch_t eph, struct ioreq *req)
{
	int rc;

	ioreq_dkey_set(req, dkey);

	rc = daos_obj_punch_dkeys(req->oh, eph, 1, &req->dkey, NULL);
	assert_int_equal(rc, 0);
}

void
punch_akey(const char *dkey, const char *akey, daos_epoch_t eph,
	   struct ioreq *req)
{
	daos_key_t daos_akey;
	int rc;

	ioreq_dkey_set(req, dkey);

	daos_akey.iov_buf = (void *)akey;
	daos_akey.iov_len = strlen(akey);
	daos_akey.iov_buf_len = strlen(akey);

	rc = daos_obj_punch_akeys(req->oh, eph, &req->dkey, 1, &daos_akey,
				  NULL);
	assert_int_equal(rc, 0);
}

static void
punch(const char *dkey, const char *akey, uint64_t idx,
      daos_epoch_t epoch, struct ioreq *req)
{
	daos_size_t size = 0;

	insert_single(dkey, akey, idx, NULL, size, epoch, req);
}

static void
lookup_internal(daos_key_t *dkey, int nr, daos_sg_list_t *sgls,
		daos_iod_t *iods, daos_epoch_t epoch, struct ioreq *req,
		bool empty)
{
	bool ev_flag;
	int rc;

	/** execute fetch operation */
	rc = daos_obj_fetch(req->oh, epoch, dkey, nr, iods, sgls,
			    NULL, req->arg->async ? &req->ev : NULL);
	if (!req->arg->async) {
		assert_int_equal(rc, req->arg->expect_result);
		return;
	}

	/** wait for fetch completion */
	rc = daos_event_test(&req->ev, DAOS_EQ_WAIT, &ev_flag);
	assert_int_equal(rc, 0);
	assert_int_equal(ev_flag, true);
	assert_int_equal(req->ev.ev_error, req->arg->expect_result);
}

void
lookup_recxs(const char *dkey, const char *akey, daos_size_t iod_size,
	     daos_epoch_t epoch, daos_recx_t *recxs, int nr, void *data,
	     daos_size_t data_size, struct ioreq *req)
{
	daos_epoch_range_t	eprs[IOREQ_IOD_NR];
	int			i;

	assert_in_range(nr, 1, IOREQ_IOD_NR);

	/* dkey */
	ioreq_dkey_set(req, dkey);

	/* akey */
	ioreq_io_akey_set(req, &akey, 1);

	/* set sgl */
	assert_int_not_equal(data, NULL);
	ioreq_sgl_simple_set(req, &data, &data_size, 1);

	/* iod, recxs */
	for (i = 0; i < nr; i++) {
		eprs[i].epr_lo = epoch;
		eprs[i].epr_hi = DAOS_EPOCH_MAX;
	}
	ioreq_iod_recxs_set(req, 0, iod_size, recxs, eprs, nr);

	lookup_internal(&req->dkey, 1, req->sgl, req->iod, epoch, req, false);
}

/**
 * Main lookup function for fetch operation.
 */
void
lookup(const char *dkey, int nr, const char **akey, uint64_t *idx,
       daos_size_t *iod_size, void **val, daos_size_t *data_size,
       daos_epoch_t *epoch, struct ioreq *req, bool empty)
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
	ioreq_iod_simple_set(req, iod_size, true, idx, epoch, nr, rx_nr);

	lookup_internal(&req->dkey, nr, req->sgl, req->iod, *epoch, req,
			empty);
}

/**
 * Helper funtion to fetch a single record (nr=1). Iod size is set to
 * DAOS_REC_ANY, which indicates that extent is unknown, and the entire record
 * should be returned in a single extent (as it most likey was inserted that
 * way). This lookup will only return 1 extent, therefore is not appropriate to
 * use if the record was inserted using insert_single_with_rxnr() in most cases.
 */
void
lookup_single(const char *dkey, const char *akey, uint64_t idx,
	      void *val, daos_size_t data_size, daos_epoch_t epoch,
	      struct ioreq *req)
{
	daos_size_t read_size = DAOS_REC_ANY; /* extent is unknown */

	lookup(dkey, /*nr*/1, &akey, &idx, &read_size, &val, &data_size, &epoch,
	       req, /*empty?*/false);
}

/**
 * Helper funtion to fetch a single record (nr=1) with a known iod/extent size.
 * The number of record extents is calculated before the fetch using the iod
 * size and the data size.
 */
void
lookup_single_with_rxnr(const char *dkey, const char *akey, uint64_t idx,
			void *val, daos_size_t iod_size, daos_size_t data_size,
			daos_epoch_t epoch, struct ioreq *req)
{
	lookup(dkey, /*nr*/1, &akey, &idx, &iod_size, &val, &data_size, &epoch,
	       req, /*empty?*/false);
}

void
lookup_empty_single(const char *dkey, const char *akey, uint64_t idx,
		    void *val, daos_size_t data_size, daos_epoch_t epoch,
		    struct ioreq *req)
{
	daos_size_t read_size = DAOS_REC_ANY; /* extent is unknown */

	lookup(dkey, /*nr*/1, &akey, &idx, &read_size, &val, &data_size, &epoch,
	       req, /*empty?*/true);
}

/**
 * Very basic test for overwrites in different epochs.
 */
static void
io_epoch_overwrite_small(void **state, daos_obj_id_t oid)
{
	test_arg_t	*arg = *state;
	struct ioreq	 req;
	daos_size_t	 size;
	char		 ow_buf[] = "DAOS";
	char		 fbuf[] = "DAOS";
	int		 i;
	daos_epoch_t	 e = 0;

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	size = strlen(ow_buf);

	print_message("Testing overwrite with record size: %lu\n", size);

	for (i = 0; i < size; i++)
		insert_single("d", "a", i, &ow_buf[i], 1, e, &req);

	for (i = 0; i < size; i++) {
		e++;
		ow_buf[i] += 32;
		insert_single("d", "a", i, &ow_buf[i], 1, e, &req);
	}

	memset(fbuf, 0, sizeof(fbuf));
	for (;;) {
		for (i = 0; i < size; i++)
			lookup_single("d", "a", i, &fbuf[i], 1, e, &req);
		print_message("e = %lu, fbuf = %s\n", e, fbuf);
		assert_string_equal(fbuf, ow_buf);
		if (e == 0)
			break;
		e--;
		ow_buf[e] -= 32;
	}

	ioreq_fini(&req);
}

#define OW_IOD_SIZE	1024 /* used for mixed record overwrite */
/**
 * Test mixed SCM & NVMe overwrites in different epochs with a large
 * record size. Iod size is needed for insert/lookup since the same akey is
 * being used.
 */
static void
io_epoch_overwrite_large(void **state, daos_obj_id_t oid)
{
	test_arg_t	*arg = *state;
	struct ioreq	 req;
	daos_epoch_t	 e = 0;
	char		*ow_buf;
	char		*fbuf;
	const char	 dkey[] = "ep_ow_large dkey";
	const char	 akey[] = "ep_ow_large akey";
	daos_size_t	 overwrite_sz;
	unsigned int	 size = 12 * 1024; /* record size */
	int		 buf_idx; /* overwrite buffer index */
	int		 rx_nr; /* number of record extents */
	int		 rec_idx = 0; /* index for next insert */
	int		 i;

	if (size < OW_IOD_SIZE || (size % OW_IOD_SIZE != 0))
		return;

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/* Alloc and set buffer to be a sting of all uppercase letters */
	ow_buf = malloc(size);
	assert_non_null(ow_buf);
	dts_buf_render_uppercase(ow_buf, size);
	/* Alloc the fetch buffer */
	fbuf = malloc(size);
	assert_non_null(fbuf);
	memset(fbuf, 0, size);

	/* Overwrite a variable number of chars to lowercase at a new epoch */
	print_message("Testing overwrite with record size: %u\n", size);

	/* Set and verify the full initial string as epoch 0 */
	rx_nr = size / OW_IOD_SIZE;
	insert_single_with_rxnr(dkey, akey, /*idx*/0, ow_buf, OW_IOD_SIZE,
				rx_nr, e, &req);
	lookup_single_with_rxnr(dkey, akey, /*idx*/0, fbuf, OW_IOD_SIZE, size,
				e, &req);
	assert_memory_equal(ow_buf, fbuf, size);

	/**
	 * Mixed SCM & NVMe overwrites. 3k, 4k, and 5k overwrites for a 12k
	 * record.
	 */
	for (buf_idx = 0, rx_nr = 3; buf_idx < size; buf_idx += overwrite_sz) {
		overwrite_sz = OW_IOD_SIZE * rx_nr;
		if (overwrite_sz > size)
			break;
		e++;
		memset(fbuf, 0, size);

		/**
		 * Overwrite buffer will always start at the previously
		 * overwritten index.
		 */
		for (i = buf_idx; i < overwrite_sz + buf_idx; i++)
			ow_buf[i] += 32; /* overwrite to lowercase */

		/**
		 * Insert overwrite at a new epoch. Overwrite will be an
		 * increasing number of rec extents (rx_nr) with the same
		 * iod_size, all inserted contiguously at the next index.
		 */
		insert_single_with_rxnr(dkey, akey, rec_idx, ow_buf + buf_idx,
					OW_IOD_SIZE, rx_nr, e, &req);
		/* Fetch entire record with new overwrite for comparison */
		lookup_single_with_rxnr(dkey, akey, /*idx*/0, fbuf, OW_IOD_SIZE,
					size, e, &req);
		assert_memory_equal(ow_buf, fbuf, size);
		/* Print message upon successful overwrite */
		print_message("overwrite size:%d, e:%lu\n", (int)overwrite_sz,
			      e);

		rec_idx += rx_nr; /* next index for insert/lookup */
		/* Increment next overwrite size by 1 record extent */
		rx_nr++;
	}

	free(fbuf);
	free(ow_buf);
	ioreq_fini(&req);
}

/**
 * Test overwrite in different epochs.
 */
static void
io_epoch_overwrite(void **state)
{
	daos_obj_id_t	 oid;
	test_arg_t	*arg = *state;

	/** choose random object */
	oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);

	/* Small 'DAOS' buffer used to show 1 char overwrites*/
	io_epoch_overwrite_small(state, oid);

	/** Large record with mixed SCM/NVMe overwrites */
	io_epoch_overwrite_large(state, oid);
}

/** i/o to variable idx offset */
static void
io_var_idx_offset(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_off_t	 offset;

	oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	for (offset = UINT64_MAX; offset > 0; offset >>= 8) {
		char buf[10];


		print_message("idx offset: %lu\n", offset);

		/** Insert */
		insert_single("var_idx_off_d", "var_idx_off_a", offset, "data",
		       strlen("data") + 1, 0, &req);

		/** Lookup */
		memset(buf, 0, 10);
		lookup_single("var_idx_off_d", "var_idx_off_a", offset,
			      buf, 10, 0, &req);
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
	oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	key = malloc(max_size + 1);
	assert_non_null(key);
	memset(key, 'a', max_size);

	for (size = 1; size <= max_size; size <<= 2) {
		char buf[10];

		print_message("akey size: %lu\n", size);

		/** Insert */
		key[size] = '\0';
		insert_single("var_akey_size_d", key, 0, "data",
			      strlen("data") + 1, 0, &req);

		/** Lookup */
		memset(buf, 0, 10);
		lookup_single("var_akey_size_d", key, 0, buf,
			      10, 0, &req);
		assert_int_equal(req.iod[0].iod_size, strlen("data") + 1);

		/** Verify data consistency */
		assert_string_equal(buf, "data");
		key[size] = 'b';
	}

	free(key);
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

	oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	key = malloc(max_size + 1);
	assert_non_null(key);
	memset(key, 'a', max_size);

	for (size = 1; size <= max_size; size <<= 2) {
		char buf[10];

		print_message("dkey size: %lu\n", size);

		/** Insert */
		key[size] = '\0';
		insert_single(key, "var_dkey_size_a", 0, "data",
			      strlen("data") + 1, 0, &req);

		/** Lookup */
		memset(buf, 0, 10);
		lookup_single(key, "var_dkey_size_a", 0, buf, 10, 0, &req);
		assert_int_equal(req.iod[0].iod_size, strlen("data") + 1);

		/** Verify data consistency */
		assert_string_equal(buf, "data");
		key[size] = 'b';
	}

	free(key);
	ioreq_fini(&req);
}

/** i/o to variable aligned record size */
static void
io_var_rec_size(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_epoch_t	 epoch;
	struct ioreq	 req;
	daos_size_t	 size;
	const int	 max_size = 1U << 20;
	char		*fetch_buf;
	char		*update_buf;

	oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);
	/** random epoch as well */
	epoch = rand();

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	fetch_buf = malloc(max_size);
	assert_non_null(fetch_buf);

	update_buf = malloc(max_size);
	assert_non_null(update_buf);

	dts_buf_render(update_buf, max_size);

	for (size = 1; size <= max_size; size <<= 1, epoch++) {
		char dkey[30];

		print_message("Record size: %lu val: \'%c\' epoch: %lu\n",
			      size, update_buf[0], epoch);

		/** Insert */
		sprintf(dkey, DF_U64, epoch);
		insert_single(dkey, "var_rec_size_a", 0, update_buf,
			      size, epoch, &req);

		/** Lookup */
		memset(fetch_buf, 0, max_size);
		lookup_single(dkey, "var_rec_size_a", 0, fetch_buf,
			      max_size, epoch, &req);
		assert_int_equal(req.iod[0].iod_size, size);

		/** Verify data consistency */
		assert_memory_equal(update_buf, fetch_buf, size);
	}

	free(update_buf);
	free(fetch_buf);
	ioreq_fini(&req);
}

/**
 * Test update/fetch with data verification in epoch 0 of varing size. Size is
 * either small I/O to SCM or larger (>=4k) I/O to NVMe.
 */
static void
io_simple_internal(void **state, daos_obj_id_t oid, unsigned int size,
		   const char dkey[], const char akey[])
{
	test_arg_t	*arg = *state;
	struct ioreq	 req;
	char		*fetch_buf;
	char		*update_buf;

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	fetch_buf = malloc(size);
	assert_non_null(fetch_buf);
	update_buf = malloc(size);
	assert_non_null(update_buf);
	dts_buf_render(update_buf, size);

	/** Insert */
	print_message("Insert(e=0)/lookup(e=0)/verify simple kv record\n");
	print_message("Record size: %u\n", size);

	insert_single(dkey, akey, 0, update_buf, size, 0, &req);

	/** Lookup */
	memset(fetch_buf, 0, size);
	lookup_single(dkey, akey, 0, fetch_buf, size, 0, &req);

	/** Verify data consistency */
	if (daos_obj_id2class(oid) != DAOS_OC_ECHO_RW) {
		assert_int_equal(req.iod[0].iod_size, size);
		assert_memory_equal(update_buf, fetch_buf, size);
	}
	free(update_buf);
	free(fetch_buf);
	ioreq_fini(&req);
}

/**
 * Very basic update/fetch with data verification.
 */
static void
io_simple(void **state)
{
	daos_obj_id_t	 oid;

	oid = dts_oid_gen(dts_obj_class, 0, ((test_arg_t *)state)->myrank);

	/** Test first for SCM, then on NVMe with record size > 4k */
	io_simple_internal(state, oid, IO_SIZE_SCM, "io_simple scm dkey",
			   "io_simple scm akey");
	io_simple_internal(state, oid, IO_SIZE_NVME, "io_simple nvme dkey",
			   "io_simple nvme akey");
}

int
enumerate_dkey(daos_epoch_t epoch, uint32_t *number, daos_key_desc_t *kds,
	       daos_anchor_t *anchor, void *buf, daos_size_t len,
	       struct ioreq *req)
{
	int rc;

	ioreq_sgl_simple_set(req, &buf, &len, 1);
	/** execute fetch operation */
	rc = daos_obj_list_dkey(req->oh, epoch, number, kds, req->sgl, anchor,
				req->arg->async ? &req->ev : NULL);
	if (req->arg->async) {
		bool ev_flag;

		assert_int_equal(rc, 0);
		rc = daos_event_test(&req->ev, DAOS_EQ_WAIT, &ev_flag);
		assert_int_equal(rc, 0);
		assert_int_equal(ev_flag, true);
		rc = req->ev.ev_error;
	}

	if (rc != -DER_KEY2BIG)
		assert_int_equal(rc, 0);

	return rc;
}

static int
enumerate_akey(daos_epoch_t epoch, char *dkey, uint32_t *number,
	       daos_key_desc_t *kds, daos_anchor_t *anchor, void *buf,
	       daos_size_t len, struct ioreq *req)
{
	int rc;

	ioreq_sgl_simple_set(req, &buf, &len, 1);
	ioreq_dkey_set(req, dkey);
	/** execute fetch operation */
	rc = daos_obj_list_akey(req->oh, epoch, &req->dkey, number, kds,
				req->sgl, anchor,
				req->arg->async ? &req->ev : NULL);
	if (req->arg->async) {
		bool ev_flag;

		assert_int_equal(rc, 0);
		rc = daos_event_test(&req->ev, DAOS_EQ_WAIT, &ev_flag);
		assert_int_equal(rc, 0);
		assert_int_equal(ev_flag, true);
		rc = req->ev.ev_error;
	}

	if (rc != -DER_KEY2BIG)
		assert_int_equal(rc, 0);

	return rc;
}

static void
enumerate_rec(daos_epoch_t epoch, char *dkey, char *akey,
	      daos_size_t *size, uint32_t *number, daos_recx_t *recxs,
	      daos_epoch_range_t *eprs, daos_anchor_t *anchor, bool incr,
	      struct ioreq *req)
{
	int rc;

	ioreq_dkey_set(req, dkey);
	ioreq_akey_set(req, akey);
	rc = daos_obj_list_recx(req->oh, epoch, &req->dkey, &req->akey,
				size, number, recxs, eprs, anchor, incr,
				req->arg->async ? &req->ev : NULL);
	assert_int_equal(rc, 0);

	if (req->arg->async) {
		bool ev_flag;

		rc = daos_event_test(&req->ev, DAOS_EQ_WAIT, &ev_flag);
		assert_int_equal(rc, 0);
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
	uint64_t	 idx;
	uint32_t	 number;
	int		 key_nr;
	int		 i;
	int		 rc;
	int		 num_rec_exts;

	oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	small_buf = malloc(ENUM_DESC_BUF);
	large_key = malloc(ENUM_LARGE_KEY_BUF);
	memset(large_key, 'L', ENUM_LARGE_KEY_BUF);
	large_key[ENUM_LARGE_KEY_BUF - 1] = '\0';
	large_buf = malloc(ENUM_LARGE_KEY_BUF * 2);

	data_buf = malloc(IO_SIZE_NVME);
	assert_non_null(data_buf);
	dts_buf_render(data_buf, IO_SIZE_NVME);

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
				      strlen("data") + 1, 0, &req);
		} else {
			/* Insert dkeys 0-999 */
			insert_single(key, "a_key", 0, "data",
				      strlen("data") + 1, 0, &req);
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
		rc = enumerate_dkey(0, &number, kds, &anchor, buf,
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
			rc = enumerate_dkey(0, &number, kds, &anchor, buf,
					    buf_len, &req);
		}
		assert_int_equal(rc, 0);

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
				      strlen("data") + 1, 0, &req);
		} else {
			/* Insert akeys 0-999 */
			insert_single("d_key", key, 0, "data",
				      strlen("data") + 1, 0, &req);
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
		rc = enumerate_akey(0, "d_key", &number, kds, &anchor,
				    buf, buf_len, &req);
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
			rc = enumerate_akey(0, "d_key", &number, kds, &anchor,
					    buf, buf_len, &req);
		}
		assert_int_equal(rc, 0);

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
	 * Insert 1000 mixed NVMe and SCM records, all with same dkey and akey.
	 */
	print_message("Insert %d records under the same key (obj:"DF_OID")\n",
		      ENUM_KEY_REC_NR, DP_OID(oid));
	idx = 0; /* record extent index */
	for (i = 0; i < ENUM_KEY_REC_NR; i++) {
		/* insert alternating SCM (2k) and NVMe (5k) records */
		if (i % 2 == 0)
			num_rec_exts = ENUM_NR_SCM; /* rx_nr=2 for SCM test */
		else
			num_rec_exts = ENUM_NR_NVME; /* rx_nr=5 for NVMe test */
		insert_single_with_rxnr("d_key", "a_rec", idx, data_buf,
					ENUM_IOD_SIZE, num_rec_exts, 0, &req);
			idx += num_rec_exts;
	}

	memset(&anchor, 0, sizeof(anchor));
	/** Enumerate all mixed NVMe and SCM records */
	for (number = ENUM_DESC_NR, key_nr = 0;
	     !daos_anchor_is_eof(&anchor);
	     number = ENUM_DESC_NR) {
		daos_epoch_range_t eprs[5];
		daos_recx_t recxs[5];
		daos_size_t	size;

		number = 5;
		enumerate_rec(0, "d_key", "a_rec", &size,
			      &number, recxs, eprs, &anchor, true, &req);
		if (number == 0)
			break; /* loop should break for EOF */

		for (i = 0; i < number; i++) {
			assert_true(size == ENUM_IOD_SIZE);
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
		key_nr += number;
	}

	free(small_buf);
	free(large_buf);
	free(large_key);
	free(data_buf);
	/** XXX Verify kds */
	ioreq_fini(&req);
	assert_int_equal(key_nr, ENUM_KEY_REC_NR);
}

/** basic punch test */
static void
punch_simple(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	uint32_t	 number = 2;
	daos_key_desc_t  kds[2];
	daos_anchor_t	 anchor_out;
	char		*buf;
	int		 total_keys = 0;
	int		 rc;

	oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** Insert record*/
	print_message("Insert a few kv record\n");
	insert_single("punch_test0", "a_key", 0, "data",
		      strlen("data") + 1, 0, &req);
	insert_single("punch_test1", "a_key", 0, "data",
		      strlen("data") + 1, 0, &req);
	insert_single("punch_test2", "a_key", 0, "data",
		      strlen("data") + 1, 0, &req);
	insert_single("punch_test3", "a_key", 0, "data",
		      strlen("data") + 1, 0, &req);
	insert_single("punch_test4", "a_key", 0, "data",
		      strlen("data") + 1, 0, &req);

	memset(&anchor_out, 0, sizeof(anchor_out));
	buf = calloc(512, 1);
	/** enumerate records */
	print_message("Enumerate records\n");
	while (number > 0) {
		rc = enumerate_dkey(0, &number, kds, &anchor_out, buf, 512,
				    &req);
		assert_int_equal(rc, 0);
		total_keys += number;
		if (daos_anchor_is_eof(&anchor_out))
			break;
		number = 2;
	}
	assert_int_equal(total_keys, 5);

	/** punch records */
	print_message("Punch records\n");
	punch("punch_test0", "a_key", 0, 1, &req);
	punch("punch_test1", "a_key", 0, 1, &req);
	punch("punch_test2", "a_key", 0, 1, &req);
	punch("punch_test3", "a_key", 0, 1, &req);
	punch("punch_test4", "a_key", 0, 1, &req);

	memset(&anchor_out, 0, sizeof(anchor_out));
	/** enumerate records */
	print_message("Enumerate records again\n");
	while (number > 0) {
		rc = enumerate_dkey(0, &number, kds, &anchor_out, buf, 512,
				    &req);
		assert_int_equal(rc, 0);
		total_keys += number;
		if (daos_anchor_is_eof(&anchor_out))
			break;
		number = 2;
	}
	print_message("get keys %d\n", total_keys);
	free(buf);
	ioreq_fini(&req);
}

static void
io_complex(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	const char	 dkey[] = "test_update dkey";
	char		*akey[5];
	char		*rec[5];
	daos_size_t	rec_size[5];
	int		rx_nr[5];
	daos_off_t	offset[5];
	char		*val[5];
	daos_size_t	val_size[5];
	daos_epoch_t	epoch = 0;
	int		i;

	oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	print_message("Insert(e=0)/lookup(e=0)/verify complex kv record\n");
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
	insert(dkey, 5, (const char **)akey, /*iod_size*/rec_size, rx_nr,
	       offset, (void **)rec, &epoch, &req);

	/** Lookup */
	lookup(dkey, 5, (const char **)akey, offset, rec_size,
	       (void **)val, val_size, &epoch, &req, false);

	/** Verify data consistency */
	for (i = 0; i < 5; i++) {
		print_message("size = %lu\n", req.iod[i].iod_size);
		assert_int_equal(req.iod[i].iod_size, strlen(rec[i]));
		assert_memory_equal(val[i], rec[i], strlen(rec[i]));
		free(val[i]);
		free(akey[i]);
		free(rec[i]);
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
	daos_epoch_t	 epoch = 2;
	daos_iov_t	 dkey;
	daos_sg_list_t	 sgl;
	daos_iov_t	 sg_iov;
	daos_iod_t	 iod;
	daos_recx_t	 recx;
	char		 stack_buf_out[STACK_BUF_LEN];
	char		 stack_buf[STACK_BUF_LEN];
	char		 *bulk_buf = NULL;
	char		 *bulk_buf_out = NULL;
	char		 *buf;
	char		 *buf_out;
	int		 buf_len;
	int		 step = 1;
	int		 rc;

	D_ALLOC(bulk_buf, TEST_BULK_BUF_LEN);
	D_ASSERT(bulk_buf != NULL);
	D_ALLOC(bulk_buf_out, TEST_BULK_BUF_LEN);
	D_ASSERT(bulk_buf_out != NULL);
	dts_buf_render(stack_buf, STACK_BUF_LEN);
	dts_buf_render(bulk_buf, TEST_BULK_BUF_LEN);

	/** open object */
	oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, 0, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	/** init dkey */
	daos_iov_set(&dkey, "dkey", strlen("dkey"));

	/* step 1 to test the inline data transfer, and step 2 to test the
	 * bulk transfer path.
	 */
next_step:
	/** init scatter/gather */
	D_ASSERT(step == 1 || step == 2);
	buf = step == 1 ? stack_buf : bulk_buf;
	buf_len = step == 1 ? STACK_BUF_LEN : TEST_BULK_BUF_LEN;
	daos_iov_set(&sg_iov, buf, buf_len);
	sgl.sg_nr	= 1;
	sgl.sg_nr_out	= 0;
	sgl.sg_iovs	= &sg_iov;

	/** init I/O descriptor */
	daos_iov_set(&iod.iod_name, "akey", strlen("akey"));
	daos_csum_set(&iod.iod_kcsum, NULL, 0);
	recx.rx_idx = 0;
	recx.rx_nr  = buf_len;

	iod.iod_size	= 1;
	iod.iod_nr	= 1;
	iod.iod_recxs	= &recx;
	iod.iod_eprs	= NULL;
	iod.iod_csums	= NULL;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	print_message("writing %d bytes with one recx per byte\n", buf_len);
	rc = daos_obj_update(oh, epoch, &dkey, 1, &iod, &sgl, NULL);
	assert_int_equal(rc, 0);

	iod.iod_size	= DAOS_REC_ANY;
	print_message("reading data back with less buffer ...\n");
	buf_out = step == 1 ? stack_buf_out : bulk_buf_out;
	memset(buf_out, 0, buf_len);
	daos_iov_set(&sg_iov, buf_out, buf_len / 2);
	rc = daos_obj_fetch(oh, epoch, &dkey, 1, &iod, &sgl, NULL, NULL);
	print_message("fetch with less buffer got %d.\n", rc);
	assert_int_equal(rc, -DER_REC2BIG);

	/** fetch */
	print_message("reading data back ...\n");
	memset(buf_out, 0, buf_len);
	daos_iov_set(&sg_iov, buf_out, buf_len);
	rc = daos_obj_fetch(oh, epoch, &dkey, 1, &iod, &sgl, NULL, NULL);
	assert_int_equal(rc, 0);
	/** Verify data consistency */
	print_message("validating data ...\n");
	assert_memory_equal(buf, buf_out, sizeof(buf));

	if (step++ == 1) {
		epoch++;
		goto next_step;
	}

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_int_equal(rc, 0);
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
	daos_epoch_t epoch = 2;
	daos_iov_t dkey;
	daos_sg_list_t sgl;
	daos_iov_t sg_iov;
	daos_iod_t iod;
	daos_recx_t recx;
	int rc, i;

	/** open object */
	oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, 0, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	/** init dkey */
	daos_iov_set(&dkey, "dkey_empty", strlen("dkey_empty"));

	buf = 2000;
	/** init scatter/gather */
	daos_iov_set(&sg_iov, &buf, sizeof(int));
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &sg_iov;

	/** init I/O descriptor */
	daos_iov_set(&iod.iod_name, "akey", strlen("akey"));
	daos_csum_set(&iod.iod_kcsum, NULL, 0);
	iod.iod_nr	= 1;
	iod.iod_size	= 1;
	recx.rx_idx	= (size/2) * sizeof(int);
	recx.rx_nr	= sizeof(int);
	iod.iod_recxs	= &recx;
	iod.iod_eprs	= NULL;
	iod.iod_csums	= NULL;
	iod.iod_type	= DAOS_IOD_ARRAY;

	/** update record */
	rc = daos_obj_update(oh, epoch, &dkey, 1, &iod, &sgl, NULL);
	assert_int_equal(rc, 0);

	buf_out = malloc(size * sizeof(*buf_out));
	assert_non_null(buf_out);

	for (i = 0; i < size; i++)
		buf_out[i] = 2016;

	/** fetch */
	daos_iov_set(&sg_iov, buf_out, sizeof(int) * size);

	iod.iod_size	= 1;
	recx.rx_idx = 0;
	recx.rx_nr = sizeof(int) * size;
	iod.iod_recxs = &recx;

	rc = daos_obj_fetch(oh, epoch, &dkey, 1, &iod, &sgl, NULL, NULL);
	assert_int_equal(rc, 0);

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
	assert_int_equal(rc, 0);
	free(buf_out);
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
	daos_epoch_t	 epoch = 2;
	daos_iov_t	 dkey;
	daos_sg_list_t	 sgl[NUM_AKEYS];
	daos_iov_t	 sg_iov[NUM_AKEYS];
	daos_iod_t	 iod[NUM_AKEYS];
	char		*buf[NUM_AKEYS];
	char		*akey[NUM_AKEYS];
	const char	*akey_fmt = "akey%d";
	int		 i, rc;
	daos_size_t	 size = 131071;

	/** open object */
	oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, 0, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	/** init dkey */
	daos_iov_set(&dkey, "dkey", strlen("dkey"));

	for (i = 0; i < NUM_AKEYS; i++) {
		akey[i] = malloc(strlen(akey_fmt) + 1);
		sprintf(akey[i], akey_fmt, i);

		buf[i] = malloc(size * (i+1));
		assert_non_null(buf[i]);

		dts_buf_render(buf[i], size * (i+1));

		/** init scatter/gather */
		daos_iov_set(&sg_iov[i], buf[i], size * (i+1));
		sgl[i].sg_nr		= 1;
		sgl[i].sg_nr_out	= 0;
		sgl[i].sg_iovs		= &sg_iov[i];

		/** init I/O descriptor */
		daos_iov_set(&iod[i].iod_name, akey[i], strlen(akey[i]));
		daos_csum_set(&iod[i].iod_kcsum, NULL, 0);
		iod[i].iod_nr		= 1;
		iod[i].iod_size		= size * (i+1);
		iod[i].iod_recxs	= NULL;
		iod[i].iod_eprs		= NULL;
		iod[i].iod_csums	= NULL;
		iod[i].iod_type		= DAOS_IOD_SINGLE;
	}

	/** update record */
	rc = daos_obj_update(oh, epoch, &dkey, NUM_AKEYS, iod, sgl, NULL);
	assert_int_equal(rc, 0);

	/** fetch record size */
	for (i = 0; i < NUM_AKEYS; i++)
		iod[i].iod_size	= DAOS_REC_ANY;

	rc = daos_obj_fetch(oh, epoch, &dkey, NUM_AKEYS, iod, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	for (i = 0; i < NUM_AKEYS; i++)
		assert_int_equal(iod[i].iod_size, size * (i+1));

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_int_equal(rc, 0);

	for (i = 0; i < NUM_AKEYS; i++) {
		free(akey[i]);
		free(buf[i]);
	}
}

static void
io_simple_update_timeout(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	arg->fail_loc = DAOS_SHARD_OBJ_UPDATE_TIMEOUT | DAOS_FAIL_SOME;
	arg->fail_value = 5;

	oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);
	io_simple_internal(state, oid, 64, "test_update dkey",
			   "test_update akey");
}

static void
io_simple_fetch_timeout(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	arg->fail_loc = DAOS_SHARD_OBJ_FETCH_TIMEOUT | DAOS_FAIL_SOME;
	arg->fail_value = 5;

	oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);
	io_simple_internal(state, oid, 64, "test_fetch dkey",
			   "test_fetch akey");
}

static void
io_simple_update_timeout_single(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	arg->fail_loc = DAOS_SHARD_OBJ_UPDATE_TIMEOUT_SINGLE | DAOS_FAIL_ONCE;
	arg->fail_value = rand() % dts_obj_replica_cnt;

	oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);
	io_simple_internal(state, oid, 64, "test_update_to dkey",
			   "test_update_to akey");
}

static void
io_simple_update_crt_error(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	arg->fail_loc = DAOS_SHARD_OBJ_RW_CRT_ERROR | DAOS_FAIL_ONCE;

	oid = dts_oid_gen(DAOS_OC_LARGE_RW, 0, arg->myrank);
	io_simple_internal(state, oid, 64, "test_update_err dkey",
			   "test_update_err akey");
}

static void
io_simple_update_crt_req_error(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	arg->fail_loc = DAOS_OBJ_REQ_CREATE_TIMEOUT | DAOS_FAIL_ONCE;

	oid = dts_oid_gen(DAOS_OC_LARGE_RW, 0, arg->myrank);
	io_simple_internal(state, oid, 64, "test_update_err_req dkey",
			   "test_update_err_req akey");
}

static void
close_reopen_coh_oh(test_arg_t *arg, struct ioreq *req, daos_obj_id_t oid)
{
	int rc;

	print_message("closing object\n");
	rc = daos_obj_close(req->oh, NULL /* ev */);
	assert_int_equal(rc, 0);

	print_message("closing container\n");
	rc = daos_cont_close(arg->coh, NULL /* ev */);
	assert_int_equal(rc, 0);

	print_message("reopening container\n");
	if (arg->myrank == 0) {
		rc = daos_cont_open(arg->pool.poh, arg->co_uuid, DAOS_COO_RW,
				    &arg->coh, &arg->co_info, NULL /* ev */);
		assert_int_equal(rc, 0);
	}
	handle_share(&arg->coh, HANDLE_CO, arg->myrank, arg->pool.poh, 1);

	print_message("reopening object\n");
	rc = daos_obj_open(arg->coh, oid, 0 /* epoch */, 0 /* mode */, &req->oh,
			   NULL /* ev */);
	assert_int_equal(rc, 0);
}

static void
epoch_discard(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	const int	 nakeys = 1;
	const size_t	 nakeys_strlen = 4 /* "9999" */;
	const char	 dkey[] = "epoch_discard dkey";
	const char	*akey_fmt = "epoch_discard akey%d";
	char		*akey[nakeys];
	char		*rec[nakeys];
	daos_size_t	 rec_size[nakeys];
	int		 rx_nr[nakeys];
	daos_off_t	 offset[nakeys];
	const char	*val_fmt = "epoch_discard val%d epoch"DF_U64;
	const size_t	 epoch_strlen = 10;
	char		*val[nakeys];
	daos_size_t	 val_size[nakeys];
	char		*rec_verify;
	daos_epoch_state_t epoch_state;
	daos_epoch_t	 epoch;
	daos_epoch_t	 e;
	int		 i;
	int		 rc;

	MPI_Barrier(MPI_COMM_WORLD);

	/** Get a hold of an epoch. */
	if (arg->myrank == 0) {
		rc = daos_epoch_query(arg->coh, &epoch_state, NULL /* ev */);
		assert_int_equal(rc, 0);
		epoch = epoch_state.es_hce + 1;
		rc = daos_epoch_hold(arg->coh, &epoch, NULL /* state */,
				     NULL /* ev */);
		assert_int_equal(rc, 0);
	}
	MPI_Bcast(&epoch, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);

	oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_SINGLE, arg);

	/** Prepare buffers for a fixed set of d-keys and a-keys. */
	for (i = 0; i < nakeys; i++) {
		akey[i] = malloc(strlen(akey_fmt) + nakeys_strlen + 1);
		assert_non_null(akey[i]);
		sprintf(akey[i], akey_fmt, i);
		rec[i] = malloc(strlen(val_fmt) + nakeys_strlen + epoch_strlen +
				1);
		assert_non_null(rec[i]);
		offset[i] = i * 20;
		val[i] = calloc(64, 1);
		assert_non_null(val[i]);
		val_size[i] = 64;
	}

	/** Write LHE, LHE + 1, and LHE + 2. To same set of d-key and a-keys. */
	for (e = epoch; e < epoch + 3; e++) {
		print_message("writing to epoch "DF_U64"\n", e);
		for (i = 0; i < nakeys; i++) {
			sprintf(rec[i], val_fmt, i, e);
			rec_size[i] = strlen(rec[i]);
			rx_nr[i] = 1;
			print_message("  a-key[%d] '%s' val '%d %s'\n", i,
				      akey[i], (int)rec_size[i], rec[i]);
		}
		insert(dkey, nakeys, (const char **)akey, /*iod_size*/rec_size,
			rx_nr, offset, (void **)rec, &e, &req);
	}

	/** Discard LHE + 1. */
	MPI_Barrier(MPI_COMM_WORLD);
	if (arg->myrank == 0) {
		print_message("discarding epoch "DF_U64"\n", epoch + 1);
		rc = daos_epoch_discard(arg->coh, epoch + 1, &epoch_state,
					NULL /* ev */);
		assert_int_equal(rc, 0);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	/** Check the three epochs. */
	rec_verify = malloc(strlen(val_fmt) + nakeys_strlen + epoch_strlen + 1);
	for (e = epoch; e < epoch + 3; e++) {
		print_message("verifying epoch "DF_U64"\n", e);
		lookup(dkey, nakeys, (const char **)akey, offset,
		       rec_size, (void **)val, val_size, &e, &req,
		       false);
		for (i = 0; i < nakeys; i++) {
			if (e == epoch + 1)	/* discarded */
				sprintf(rec_verify, val_fmt, i, e - 1);
			else			/* intact */
				sprintf(rec_verify, val_fmt, i, e);
			assert_int_equal(req.iod[i].iod_size,
					 strlen(rec_verify));
			print_message("  a-key[%d] '%s' val '%d %s'\n", i,
				      akey[i], (int)req.iod[i].iod_size,
				      val[i]);
			assert_memory_equal(val[i], rec_verify,
					    req.iod[i].iod_size);
		}
	}
	free(rec_verify);

	/** Close and reopen the container and the obj. */
	MPI_Barrier(MPI_COMM_WORLD);
	close_reopen_coh_oh(arg, &req, oid);

	/** Verify that the three epochs are empty. */
	for (e = epoch; e < epoch + 3; e++) {
		daos_anchor_t	anchor;
		int		found = 0;

		print_message("verifying epoch "DF_U64"\n", e);
		memset(&anchor, 0, sizeof(anchor));
		while (!daos_anchor_is_eof(&anchor)) {
			uint32_t		n = 1;
			daos_key_desc_t		kd;
			char			*buf[64];

			rc = enumerate_dkey(e, &n, &kd, &anchor, buf,
					    sizeof(buf), &req);
			assert_int_equal(rc, 0);
			print_message("  n %u\n", n);
			found += n;
		}
		assert_int_equal(found, 0);
	}

	for (i = 0; i < nakeys; i++) {
		free(val[i]);
		free(akey[i]);
		free(rec[i]);
	}
	ioreq_fini(&req);
	MPI_Barrier(MPI_COMM_WORLD);
}

static void
io_nospace(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	struct ioreq	req;
	int		buf_size = 1 << 20;
	char		*large_buf;
	char		key[10];
	int		i;

	/** choose random object */
	oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);

	large_buf = malloc(buf_size);
	assert_non_null(large_buf);
	arg->fail_loc = DAOS_OBJ_UPDATE_NOSPACE;
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	for (i = 0; i < 5; i++) {
		sprintf(key, "dkey%d", i);
		/** Insert */
		arg->expect_result = -DER_NOSPACE;
		insert_single(key, "akey", 0, "data",
			      strlen("data") + 1, 0, &req);
		insert_single(key, "akey", 0, large_buf,
			      buf_size, 0, &req);
	}
	free(large_buf);
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

	oid = dts_oid_gen(0, 0, arg->myrank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/** write twice */
	insert_single("dkey", "akey", 0, "data", strlen("data") + 1, 0,
		      &req);
	insert_single("dkey", "akey", 0, "data", strlen("data") + 1, 0,
		      &req);
	/** Lookup */
	memset(buf, 0, 10);
	lookup_single("dkey", "akey", 0, buf, 10, 0, &req);
	assert_int_equal(req.iod[0].iod_size, strlen(buf) + 1);
	/** Verify data consistency */
	assert_string_equal(buf, "data");

	dts_buf_render(large_update_buf, 4096);
	/** write twice */
	insert_single("dkey_large", "akey_large", 0, large_update_buf, 4096, 0,
		      &req);
	insert_single("dkey_large", "akey_large", 0, large_update_buf, 4096, 0,
		      &req);

	memset(large_fetch_buf, 0, 4096);
	lookup_single("dkey_large", "akey_large", 0, large_fetch_buf, 4096, 0,
		      &req);

	assert_int_equal(req.iod[0].iod_size, 4096);
	assert_memory_equal(large_update_buf, large_fetch_buf, 4096);

	ioreq_fini(&req);
}

static void
echo_fetch_update(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	oid = dts_oid_gen(DAOS_OC_ECHO_RW, 0, arg->myrank);
	io_simple_internal(state, oid, 64, "echo_test dkey", "echo_test akey");

	oid = dts_oid_gen(DAOS_OC_ECHO_RW, 0, arg->myrank);
	io_simple_internal(state, oid, 8192, "echo_test_large dkey",
			   "echo_test_large akey");
}

static void
tgt_idx_change_retry(void **state)
{
	test_arg_t		*arg = *state;
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
	daos_epoch_t		 epoch = 0;
	struct daos_obj_layout	*layout;
	d_rank_t		 rank = 0;
	int			 replica;
	int			 i;
	int			 rc;

	/* create a 3 replica small object, to test the case that:
	 * update:
	 * 1) shard 0 IO finished, then the target x of shard 0 dead/excluded
	 * 2) shard 1 and shard 2 IO still inflight (not scheduled)
	 * 3) obj IO retry, shard 0 goes to new target y
	 *
	 * Then fetch and verify the data.
	 */

	/* needs at lest 4 targets, exclude one and another 3 raft nodes */
	if (!test_runable(arg, 4))
		skip();
	if (!arg->async) {
		if (arg->myrank == 0)
			print_message("this test can-only run in async mode\n");
		skip();
	}

	oid = dts_oid_gen(DAOS_OC_R3S_SPEC_RANK, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, 2);
	replica = rand() % 3;
	arg->fail_loc = DAOS_OBJ_TGT_IDX_CHANGE | DAOS_FAIL_VALUE;
	arg->fail_value = replica;

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
		      rx_nr, offset, (void **)rec, &epoch, &req);

	if (arg->myrank == 0) {
		/** verify the object layout */
		rc = daos_obj_layout_get(arg->coh, oid, &layout);
		assert_int_equal(rc, 0);
		assert_int_equal(layout->ol_nr, 1);
		assert_int_equal(layout->ol_shards[0]->os_replica_nr, 3);
		/* FIXME disable rank compare until we fix the layout_get */
		/* assert_int_equal(layout->ol_shards[0]->os_ranks[0], 2); */
		rank = layout->ol_shards[0]->os_ranks[replica];
		rc = daos_obj_layout_free(layout);
		assert_int_equal(rc, 0);

		/** exclude target of the replica */
		print_message("rank 0 excluding target rank %u ...\n", rank);
		daos_exclude_server(arg->pool.pool_uuid, arg->group,
				    &arg->pool.svc, rank);
		assert_int_equal(rc, 0);

		/** progress the async IO (not must) */
		insert_test(&req, 1000);

		/** wait until rebuild done */
		test_rebuild_wait(&arg, 1);

		/** verify the target of shard 0 changed */
		rc = daos_obj_layout_get(arg->coh, oid, &layout);
		assert_int_equal(rc, 0);
		assert_int_equal(layout->ol_nr, 1);
		assert_int_equal(layout->ol_shards[0]->os_replica_nr, 3);
		/* FIXME disable rank compare until we fix the layout_get */
		/* assert_int_not_equal(layout->ol_shards[0]->os_ranks[replica],
		 *		     rank);
		*/
		print_message("target of shard %d changed from %d to %d\n",
			      replica, rank, layout->ol_shards[0]->os_ranks[0]);
		rc = daos_obj_layout_free(layout);
		assert_int_equal(rc, 0);
	}

	daos_fail_loc_set(0);
	insert_wait(&req);

	daos_fail_loc_set(DAOS_OBJ_SPECIAL_SHARD | DAOS_FAIL_VALUE);
	/** lookup through each replica and verify data */
	for (replica = 0; replica < 3; replica++) {
		daos_fail_value_set(replica);
		for (i = 0; i < 5; i++)
			memset(val[i], 0, 64);

		lookup(dkey, 5, (const char **)akey, offset, rec_size,
		       (void **)val, val_size, &epoch, &req, false);

		for (i = 0; i < 5; i++) {
			assert_int_equal(req.iod[i].iod_size, strlen(rec[i]));
			assert_memory_equal(val[i], rec[i], strlen(rec[i]));
		}
	}

	for (i = 0; i < 5; i++) {
		free(val[i]);
		free(akey[i]);
		free(rec[i]);
	}

	if (arg->myrank == 0) {
		print_message("rank 0 adding target rank %u ...\n", rank);
		daos_add_server(arg->pool.pool_uuid, arg->group, &arg->pool.svc,
				rank);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	ioreq_fini(&req);
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
	daos_pool_info_t	 info;
	d_rank_t		 rank = 2;
	char			*buf;
	int			 rc;

	/* needs at lest 4 targets, exclude one and another 3 raft nodes */
	if (!test_runable(arg, 4))
		skip();

	oid = dts_oid_gen(DAOS_OC_R1S_SPEC_RANK, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, rank);

	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	/** Insert */
	print_message("Insert(e=0)/exclude all tgts/lookup(e=0) verify "
		      "get -DER_NONEXIST\n");
	insert_single(dkey, akey, 0, (void *)rec, strlen(rec), 0, &req);

	if (arg->myrank == 0) {
		/** disable rebuild */
		rc = daos_pool_query(arg->pool.poh, NULL, &info, NULL);
		assert_int_equal(rc, 0);
		rc = daos_mgmt_params_set(arg->group, info.pi_leader,
			DSS_KEY_FAIL_LOC,
			DAOS_REBUILD_DISABLE | DAOS_FAIL_VALUE,
			NULL);
		assert_int_equal(rc, 0);

		/** exclude the target of this obj's replicas */
		daos_exclude_server(arg->pool.pool_uuid, arg->group,
				    &arg->pool.svc, rank);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	/** Lookup */
	buf = calloc(size, 1);
	assert_non_null(buf);
	/** inject CRT error failure to update pool map + retry */
	daos_fail_loc_set(DAOS_SHARD_OBJ_RW_CRT_ERROR | DAOS_FAIL_ONCE);
	arg->expect_result = -DER_NONEXIST;
	lookup_single(dkey, akey, 0, buf, size, 0, &req);

	if (arg->myrank == 0) {
		/* add back the excluded targets */
		daos_add_server(arg->pool.pool_uuid, arg->group, &arg->pool.svc,
				rank);

		/* re-enable rebuild */
		rc = daos_mgmt_params_set(arg->group, info.pi_leader,
			DSS_KEY_FAIL_LOC, 0, NULL);
		assert_int_equal(rc, 0);
	}
	free(buf);
	MPI_Barrier(MPI_COMM_WORLD);
	ioreq_fini(&req);
}

static void
update_overlapped_recxs(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	daos_epoch_t	 epoch = 2;
	daos_iov_t	 dkey;
	daos_sg_list_t	 sgl;
	daos_iov_t	 sg_iov;
	daos_iod_t	 iod;
	daos_recx_t	 recx[128];
	char		 buf[STACK_BUF_LEN];
	int		 i;
	int		 rc;

	/** open object */
	oid = dts_oid_gen(dts_obj_class, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, 0, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	/** init dkey */
	daos_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	daos_iov_set(&sg_iov, buf, sizeof(buf));
	sgl.sg_nr		= 1;
	sgl.sg_nr_out		= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	daos_iov_set(&iod.iod_name, "akey", strlen("akey"));
	daos_csum_set(&iod.iod_kcsum, NULL, 0);
	iod.iod_size	= 1;
	iod.iod_recxs	= recx;
	iod.iod_eprs	= NULL;
	iod.iod_csums	= NULL;
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
	rc = daos_obj_update(oh, epoch, &dkey, 1, &iod, &sgl, NULL);
	assert_int_equal(rc, -DER_INVAL);

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
	rc = daos_obj_update(oh, epoch, &dkey, 1, &iod, &sgl, NULL);
	assert_int_equal(rc, -DER_INVAL);


	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_int_equal(rc, 0);
	print_message("all good\n");
}

/** very basic key query test */
static void
io_obj_key_query(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	oid;
	daos_handle_t	oh;
	daos_epoch_t	epoch = 2;
	daos_iov_t	dkey;
	daos_iov_t	akey;
	daos_recx_t	recx;
	uint64_t	dkey_val, akey_val;
	uint32_t	flags;
	int		rc;

	/** open object */
	oid = dts_oid_gen(DAOS_OC_LARGE_RW, 0, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, 0, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	/** init dkey, akey */
	dkey_val = akey_val = 0;
	daos_iov_set(&dkey, &dkey_val, sizeof(uint64_t));
	daos_iov_set(&akey, &akey_val, sizeof(uint64_t));

	flags = DAOS_GET_DKEY;
	rc = daos_obj_key_query(oh, epoch, flags, &dkey, NULL, NULL, NULL);
	assert_int_equal(rc, -DER_INVAL);

	flags = DAOS_GET_MAX;
	rc = daos_obj_key_query(oh, epoch, flags, &dkey, NULL, NULL, NULL);
	assert_int_equal(rc, -DER_INVAL);

	flags = DAOS_GET_MAX | DAOS_GET_MIN;
	rc = daos_obj_key_query(oh, epoch, flags, &dkey, NULL, NULL, NULL);
	assert_int_equal(rc, -DER_INVAL);

	flags = DAOS_GET_DKEY | DAOS_GET_MAX | DAOS_GET_MIN;
	rc = daos_obj_key_query(oh, epoch, flags, &dkey, NULL, NULL, NULL);
	assert_int_equal(rc, -DER_INVAL);

	flags = DAOS_GET_AKEY | DAOS_GET_MIN;
	rc = daos_obj_key_query(oh, epoch, flags, &dkey, NULL, NULL, NULL);
	assert_int_equal(rc, -DER_INVAL);

	flags = DAOS_GET_DKEY | DAOS_GET_AKEY | DAOS_GET_RECX | DAOS_GET_MAX;
	rc = daos_obj_key_query(oh, epoch, flags, &dkey, &akey, NULL, NULL);
	assert_int_equal(rc, -DER_INVAL);

	flags = DAOS_GET_DKEY | DAOS_GET_MIN;
	rc = daos_obj_key_query(oh, epoch, flags, &dkey, NULL, NULL, NULL);
	assert_int_equal(rc, -DER_INVAL);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_int_equal(rc, 0);

	oid = dts_oid_gen(DAOS_OC_LARGE_RW,
			  DAOS_OF_DKEY_UINT64 | DAOS_OF_AKEY_UINT64,
			  arg->myrank);
	rc = daos_obj_open(arg->coh, oid, 0, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	flags = 0;
	flags = DAOS_GET_DKEY | DAOS_GET_AKEY | DAOS_GET_RECX | DAOS_GET_MIN;
	rc = daos_obj_key_query(oh, epoch, flags, &dkey, &akey, &recx, NULL);
	assert_int_equal(rc, 0);

	dkey_val = *((uint64_t *)dkey.iov_buf);
	print_message("DKEY Query = %"PRIu64"\n", dkey_val);
	akey_val = *((uint64_t *)akey.iov_buf);
	print_message("AKEY Query = %"PRIu64"\n", akey_val);
	print_message("RECX Query (idx = %"PRIu64"); (nr = %"PRIu64")\n",
		      recx.rx_idx, recx.rx_nr);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_int_equal(rc, 0);
	print_message("all good\n");
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
	{ "IO8: overwrite in different epoch",
	  io_epoch_overwrite, async_enable, test_case_teardown},
	{ "IO9: simple enumerate", enumerate_simple,
	  async_disable, test_case_teardown},
	{ "IO10: simple punch", punch_simple,
	  async_disable, test_case_teardown},
	{ "IO11: complex update/fetch/verify", io_complex,
	  async_disable, test_case_teardown},
	{ "IO12: basic byte array with record size fetching",
	  basic_byte_array, async_disable, test_case_teardown},
	{ "IO13: timeout simple update",
	  io_simple_update_timeout, async_disable, test_case_teardown},
	{ "IO14: timeout simple fetch",
	  io_simple_fetch_timeout, async_disable, test_case_teardown},
	{ "IO15: timeout on 1 shard simple update",
	  io_simple_update_timeout_single, async_disable, test_case_teardown},
	{ "IO16: epoch discard", epoch_discard,
	  async_disable, test_case_teardown},
	{ "IO17: no space", io_nospace, async_disable, test_case_teardown},
	{ "IO18: fetch size with NULL sgl", fetch_size, async_disable,
	  test_case_teardown},
	{ "IO19: io crt error", io_simple_update_crt_error,
	  async_disable, test_case_teardown},
	{ "IO20: io crt error (async)", io_simple_update_crt_error,
	  async_enable, test_case_teardown},
	{ "IO21: io crt req create timeout (sync)",
	  io_simple_update_crt_req_error, async_disable, test_case_teardown},
	{ "IO22: io crt req create timeout (async)",
	  io_simple_update_crt_req_error, async_enable, test_case_teardown},
	{ "IO23: Read from unwritten records", read_empty_records,
	  async_disable, test_case_teardown},
	{ "IO24: Read from large unwritten records", read_large_empty_records,
	  async_disable, test_case_teardown},
	{ "IO25: written records repeatly", write_record_multiple_times,
	  async_disable, test_case_teardown},
	{ "IO26: echo fetch/update", echo_fetch_update,
	  async_disable, test_case_teardown},
	{ "IO27: basic object key query testing",
	  io_obj_key_query, async_disable, test_case_teardown},
	{ "IO28: shard target idx change cause retry", tgt_idx_change_retry,
	  async_enable, test_case_teardown},
	{ "IO29: fetch when all replicas unavailable", fetch_replica_unavail,
	  async_enable, test_case_teardown},
	{ "IO30: update with overlapped recxs", update_overlapped_recxs,
	  async_enable, test_case_teardown},
};

int
obj_setup_internal(void **state)
{
	test_arg_t	*arg;

	arg = *state;
	if (arg->pool.pool_info.pi_ntargets < 2)
		dts_obj_class = DAOS_OC_TINY_RW;

	return 0;
}

int
obj_setup(void **state)
{
	int	rc;

	rc = test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE,
			NULL);
	if (rc != 0)
		return rc;

	return obj_setup_internal(state);
}

int
run_daos_io_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		rc = cmocka_run_group_tests_name("DAOS I/O tests", io_tests,
						 obj_setup, test_teardown);
		MPI_Barrier(MPI_COMM_WORLD);
		return rc;
	}

	rc = run_daos_sub_tests(io_tests, ARRAY_SIZE(io_tests),
				DEFAULT_POOL_SIZE, sub_tests, sub_tests_size,
				obj_setup_internal, NULL);

	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
