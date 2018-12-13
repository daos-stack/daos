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
 * This file is part of vos/tests/
 *
 * vos/tests/vts_discard.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#define D_LOGFAC	DD_FAC(tests)

#include "vts_io.h"

/**
 * Stores the last key and can be used for
 * punching or overwrite
 */
char		last_dkey[UPDATE_DKEY_SIZE];
char		last_akey[UPDATE_AKEY_SIZE];
daos_unit_oid_t	last_oid;

#define FETCH_VERBOSE false
#define UPDATE_VERBOSE false

#define TF_DISCARD_KEYS (50000)
#define TF_CREDITS_KEYS (10000)
static int init_credits_keys  = TF_CREDITS_KEYS;
static int init_discard_keys = TF_DISCARD_KEYS;

enum io_aggregate_test_type {
	IO_AGGREGATE_TEST_DKEY = 0,
	IO_AGGREGATE_TEST_AKEY = 1
};

enum multi_recx_overwrite_test_type {
	MULTI_RECX_OVERWRITE_NO_CREDITS = 0,
	MULTI_RECX_OVERWRITE_WITH_CREDITS = 1
};

enum near_epoch_test_type {
	NEAR_EPOCH_PUNCH = 0,
	NEAR_EPOCH_DISCARD_PUNCH = 1,
	NEAR_EPOCH_FETCH = 2,
	NEAR_EPOCH_OVERWRITE_FETCH = 3
};

static int
io_update(struct io_test_args *arg, daos_epoch_t update_epoch,
	  struct d_uuid *cookie, char *dkey, char *akey,
	  struct vts_counter *cntrs, struct io_req **io_req,
	  int idx, bool verbose)
{
	int			rc  = 0;
	struct io_req		*ioreq;

	D_ALLOC_PTR(ioreq);
	uuid_copy(ioreq->cookie.uuid, cookie->uuid);
	memset(&ioreq->iod, 0, sizeof(ioreq->iod));
	memset(&ioreq->rex, 0, sizeof(ioreq->rex));
	memset(&ioreq->sgl, 0, sizeof(ioreq->sgl));

	assert_true(dkey != NULL && akey != NULL);
	memcpy(&ioreq->dkey_buf[0], dkey, UPDATE_DKEY_SIZE);
	memcpy(&ioreq->akey_buf[0], akey, UPDATE_AKEY_SIZE);


	if (!(arg->ta_flags & TF_PUNCH)) {
		daos_iov_set(&ioreq->dkey, &ioreq->dkey_buf[0],
			     strlen(ioreq->dkey_buf));
		daos_iov_set(&ioreq->akey, &ioreq->akey_buf[0],
			     strlen(ioreq->akey_buf));

		memset(ioreq->update_buf, (rand() % 94) + 33, UPDATE_BUF_SIZE);
		daos_iov_set(&ioreq->val_iov, &ioreq->update_buf[0],
			     UPDATE_BUF_SIZE);
		ioreq->iod.iod_size = ioreq->val_iov.iov_len;
	} else {
		daos_iov_set(&ioreq->dkey, dkey, UPDATE_DKEY_SIZE);
		daos_iov_set(&ioreq->akey, akey, UPDATE_AKEY_SIZE);
		memset(ioreq->update_buf, 0, UPDATE_BUF_SIZE);
		daos_iov_set(&ioreq->val_iov, &ioreq->update_buf,
			     UPDATE_BUF_SIZE);
		ioreq->iod.iod_size = 0;
	}

	ioreq->sgl.sg_nr = 1;
	ioreq->sgl.sg_iovs = &ioreq->val_iov;

	ioreq->epoch		= update_epoch;
	ioreq->rex.rx_nr	= 1;
	ioreq->rex.rx_idx	= idx;
	ioreq->iod.iod_name	= ioreq->akey;
	ioreq->iod.iod_recxs	= &ioreq->rex;
	ioreq->iod.iod_nr	= 1;
	ioreq->iod.iod_type     = (arg->ta_flags & TF_REC_EXT) ?
				  DAOS_IOD_ARRAY : DAOS_IOD_SINGLE;

	rc = io_test_obj_update(arg, update_epoch, &ioreq->dkey, &ioreq->iod,
				&ioreq->sgl, &ioreq->cookie, verbose);
	if (rc)
		D_GOTO(exit, rc);

	inc_cntr_manual(arg->ta_flags, cntrs);
	if (verbose) {
		print_message("===========Update===========\n");
		print_message("dkey: %s\n", ioreq->dkey_buf);
		print_message("akey: %s\n", ioreq->akey_buf);
		print_message("recx: %u\n",
			      (unsigned int)ioreq->rex.rx_idx);
		print_message("epoch: "DF_U64"\n", ioreq->epoch);
		print_message("=============================\n");
	}
exit:
	*io_req = ioreq;
	return rc;
}

/**
 * NB: This version of fetch is for verifying in case of recx and
 * akey discard. This is used because fetch does not return -DER_NONEXIST
 * currently
 */
static int
io_fetch_empty_buf(struct io_test_args *arg, daos_epoch_t fetch_epoch,
		  struct io_req *req, bool verbose)
{
	int rc  = 0;

	if (verbose) {
		print_message("==========Fetch=============\n");
		print_message("dkey: %s\n", req->dkey_buf);
		print_message("akey: %s\n", req->akey_buf);
		print_message("Fetch_BUF: %s, epoch"DF_U64"\n",
			      req->fetch_buf, fetch_epoch);
		print_message("=============================\n");
	}

	memset(req->fetch_buf, 0, UPDATE_BUF_SIZE);
	daos_iov_set(&req->val_iov, &req->fetch_buf, UPDATE_BUF_SIZE);
	req->iod.iod_size = UPDATE_BUF_SIZE;
	rc = io_test_obj_fetch(arg, fetch_epoch, &req->dkey, &req->iod,
			       &req->sgl, FETCH_VERBOSE);
	if (rc)
		D_GOTO(exit, rc);

	assert_true(strlen(req->fetch_buf) == 0);
	if (strlen(req->fetch_buf) == 0)
		rc = -DER_NONEXIST;
exit:
	return rc;
}


static int
io_fetch(struct io_test_args *arg, daos_epoch_t fetch_epoch,
	 struct io_req *req, bool verbose)
{
	int rc  = 0;

	if (verbose) {
		print_message("dkey: %s\n", req->dkey_buf);
		print_message("akey: %s\n", req->akey_buf);
		print_message("recx: "DF_U64"\n",
			      req->rex.rx_idx);
		print_message("epoch: "DF_U64"\n", fetch_epoch);
	}

	memset(req->fetch_buf, 0, UPDATE_BUF_SIZE);
	daos_iov_set(&req->val_iov, &req->fetch_buf, UPDATE_BUF_SIZE);
	req->iod.iod_size = UPDATE_BUF_SIZE;
	rc = io_test_obj_fetch(arg, fetch_epoch, &req->dkey, &req->iod,
			       &req->sgl, FETCH_VERBOSE);
	if (rc)
		D_GOTO(exit, rc);
	if (req->iod.iod_size == 0)
		return -DER_NONEXIST;

	if (verbose)
		print_message("fetch_buf: %s, epoch"DF_U64"\n",
			      req->fetch_buf, fetch_epoch);

	assert_memory_equal(req->update_buf, req->fetch_buf, UPDATE_BUF_SIZE);

exit:
	return rc;
}

static inline void
set_key_and_index(char *dkey, char *akey, int *index)
{
	if (dkey != NULL) {
		memset(dkey, 0, UPDATE_DKEY_SIZE);
		dts_key_gen(dkey, UPDATE_DKEY_SIZE, UPDATE_DKEY);
	}

	if (akey != NULL) {
		memset(akey, 0, UPDATE_AKEY_SIZE);
		dts_key_gen(akey, UPDATE_AKEY_SIZE, UPDATE_AKEY);
	}

	if (index != NULL) {
		char buf[UPDATE_DKEY_SIZE];

		dts_key_gen(buf, UPDATE_DKEY_SIZE, UPDATE_DKEY);
		*index = (d_hash_string_u32(buf, UPDATE_DKEY_SIZE)) % 1000000;
	}
}

static int
io_simple_discard_setup(void **state)
{
	struct io_test_args	*args = *state;

	args->oid = dts_unit_oid_gen(0, 0, 0);

	return 0;
}

static inline int
io_create_object(struct vos_container *cont)
{
	int			rc = 0;
	daos_unit_oid_t		oid;
	struct vos_obj_df	*obj;

	oid = dts_unit_oid_gen(0, 0, 0);
	rc = vos_oi_find_alloc(cont, oid, 1, &obj);
	return rc;
}

static inline int
io_simple_update(struct io_test_args *arg, struct d_uuid *cookie,
		 uint64_t epoch, struct io_req **req)
{
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	struct vts_counter	cntrs;
	int			idx, rc;

	set_key_and_index(&dkey_buf[0], &akey_buf[0], &idx);
	rc = io_update(arg, epoch, cookie, &dkey_buf[0], &akey_buf[0],
		       &cntrs, req, idx, UPDATE_VERBOSE);
	return rc;
}

static void
io_simple_one_key_discard(void **state)
{
	struct io_test_args	*arg = *state;
	int			i, rc;
	struct io_req		*req[4];
	struct d_uuid		cookie;
	daos_epoch_range_t	range;

	arg->ta_flags = 0;
	/* create two objects. these need to be ignored */
	rc = io_create_object(vos_hdl2cont(arg->ctx.tc_co_hdl));
	assert_int_equal(rc, 0);
	io_create_object(vos_hdl2cont(arg->ctx.tc_co_hdl));
	assert_int_equal(rc, 0);

	cookie = gen_rand_cookie();
	for (i = 0; i < 4; i++) {
		rc = io_simple_update(arg, &cookie, i+1, &req[i]);
		assert_int_equal(rc, 0);

		rc = io_fetch(arg, i+1, req[i], FETCH_VERBOSE);
		assert_int_equal(rc, 0);
	}

	/* Discard Epoch 1 alone */
	range.epr_lo = 1;
	range.epr_hi = 1;
	rc = vos_epoch_discard(arg->ctx.tc_co_hdl, &range, cookie.uuid);
	assert_int_equal(rc, 0);

	rc = io_fetch(arg, 1, req[0], FETCH_VERBOSE);
	assert_true(rc == -DER_NONEXIST);

	rc = io_fetch(arg, 2, req[1], FETCH_VERBOSE);
	assert_int_equal(rc, 0);

	/** Discard epochs 3 -> INF */
	range.epr_lo = 3;
	range.epr_hi = DAOS_EPOCH_MAX;
	rc = vos_epoch_discard(arg->ctx.tc_co_hdl, &range, cookie.uuid);
	assert_int_equal(rc, 0);

	/**
	 * Fetch epoch 3 for ioreq 3, and ioreq 2
	 * ioreq 2 must exists and ioreq 3 shoud not
	 */
	rc = io_fetch(arg, 3, req[2], FETCH_VERBOSE);
	assert_int_equal(rc, -DER_NONEXIST);

	rc = io_fetch(arg, 3, req[1], FETCH_VERBOSE);
	assert_int_equal(rc, 0);

	for (i = 0; i < 4; i++)
		D_FREE(req[i]);
}

static int
io_simple_discard_teardown(void **state)
{
	test_args_reset((struct io_test_args *) *state, VPOOL_SIZE);
	return 0;
}

static int
io_multikey_discard_setup(void **state)
{
	struct io_test_args	*arg = *state;

	D_INIT_LIST_HEAD(&arg->req_list);
	arg->oid = dts_unit_oid_gen(0, 0, 0);
	last_oid = arg->oid;

	return 0;
}

static int
io_multi_recx_discard_setup(void **state)
{
	struct io_test_args	*arg = *state;

	D_INIT_LIST_HEAD(&arg->req_list);
	arg->ta_flags = IF_DISABLED;
	return 0;
}

static int
io_near_epoch_tests(struct io_test_args *arg, char *dkey, char *akey,
		    daos_epoch_t *epoch, struct d_uuid *cookie, int *idx,
		    int num, unsigned long *flags)
{
	int			i, mid, rc = 0;
	struct vts_counter	cntrs;
	daos_epoch_range_t	range;
	struct io_req		**reqs;
	bool			*punch;

	assert_true(num/2 >= 0);
	mid = num/2;

	D_ALLOC_ARRAY(reqs, num);
	D_ALLOC_ARRAY(punch, num);

	for (i = 0; i < num; i++) {
		struct d_uuid l_cookie;

		if (flags != NULL) {
			arg->ta_flags = flags[i];
			if (flags[i] & TF_PUNCH)
				punch[i] = true;
			else
				punch[i] = false;
		}
		if (i == num - 1)
			uuid_copy(l_cookie.uuid, cookie[mid].uuid);
		else
			uuid_copy(l_cookie.uuid, cookie[i].uuid);

		rc = io_update(arg, epoch[i], &l_cookie, dkey,
			       akey, &cntrs, &reqs[i], idx[i],
			       UPDATE_VERBOSE);
		if (rc != 0)
			D_GOTO(exit, rc);
	}
	/** Reset flags here */
	arg->ta_flags = 0;
	range.epr_lo = range.epr_hi = epoch[mid];
	rc = vos_epoch_discard(arg->ctx.tc_co_hdl, &range, cookie[mid].uuid);
	if (rc != 0)
		D_GOTO(exit, rc);

	/* Fetch should return the value buf from req 0 */
	rc = io_fetch(arg, epoch[mid], reqs[mid - 1], FETCH_VERBOSE);
	if (punch[mid - 1])
		assert_int_equal(rc, -DER_NONEXIST);

	if (rc != 0 && (punch[mid - 1] && rc != -DER_NONEXIST))
		D_GOTO(exit, rc);

	if (flags != NULL)
		arg->ta_flags = flags[mid];

	rc = io_update(arg, epoch[mid], &cookie[mid], dkey,
		       akey, &cntrs, &reqs[mid], idx[mid],
		       UPDATE_VERBOSE);
	if (rc != 0)
		D_GOTO(exit, rc);

	rc = io_fetch(arg, epoch[mid], reqs[mid], FETCH_VERBOSE);
	if (punch[mid])
		assert_int_equal(rc, -DER_NONEXIST);

	if (rc != 0 && (punch[mid] && rc != -DER_NONEXIST))
		D_GOTO(exit, rc);
	/** Success if reaches here */
	rc = 0;

exit:
	D_FREE(reqs);
	D_FREE(punch);

	return rc;
}

static inline void
set_near_epoch_tests(struct d_uuid *cookie, daos_epoch_t *epochs, int *idx,
		     int num)
{
	int	i = 0;

	for (i = 0; i < num; i++) {
		cookie[i] = gen_rand_cookie();
		epochs[i] = (i + 1) * 1000;
		if (i > 0)
			idx[i] = idx[i - 1];
	}
}

#define ARRAY_SIZE_NEAR_EPOCH_TEST (3)
static void
io_near_epoch_test_base(void **state, enum near_epoch_test_type test_type)
{
	struct io_test_args	*arg = *state;
	daos_epoch_t		epoch[ARRAY_SIZE_NEAR_EPOCH_TEST];
	struct d_uuid		cookie[ARRAY_SIZE_NEAR_EPOCH_TEST];
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	int			idx[ARRAY_SIZE_NEAR_EPOCH_TEST], i, rc = 0;
	unsigned long		flags[ARRAY_SIZE_NEAR_EPOCH_TEST];

	set_key_and_index(&dkey_buf[0], &akey_buf[0], &idx[0]);
	set_near_epoch_tests(&cookie[0], &epoch[0], &idx[0],
			     ARRAY_SIZE_NEAR_EPOCH_TEST);

	if (test_type == NEAR_EPOCH_PUNCH ||
		test_type == NEAR_EPOCH_DISCARD_PUNCH) {
		memset(&flags, 0, ARRAY_SIZE_NEAR_EPOCH_TEST *
		       sizeof(unsigned long));
		flags[test_type] = TF_PUNCH;
		rc = io_near_epoch_tests(arg, &dkey_buf[0], &akey_buf[0],
				&epoch[0], &cookie[0], &idx[0],
				ARRAY_SIZE_NEAR_EPOCH_TEST, &flags[0]);
	} else {
		/* NEAR_EPOCH_FETCH and NEAR_EPOCH_OVERWRITE_FETCH */
		if (test_type == NEAR_EPOCH_FETCH) {
			for (i = 1; i < ARRAY_SIZE_NEAR_EPOCH_TEST; i++)
				idx[i] = idx[i - 1] + 1;
		}
		rc = io_near_epoch_tests(arg, &dkey_buf[0], &akey_buf[0],
				&epoch[0], &cookie[0], &idx[0],
				ARRAY_SIZE_NEAR_EPOCH_TEST, NULL);
	}
	assert_int_equal(rc, 0);
}

static void
io_near_epoch_idx_overwrite_fetch(void **state)
{
	io_near_epoch_test_base(state, NEAR_EPOCH_OVERWRITE_FETCH);
}

static void
io_near_epoch_punch(void **state)
{
	io_near_epoch_test_base(state, NEAR_EPOCH_PUNCH);
}

static void
io_discard_punch(void **state)
{
	io_near_epoch_test_base(state, NEAR_EPOCH_DISCARD_PUNCH);
}

static void
io_test_near_epoch_fetch(void **state)
{
	io_near_epoch_test_base(state, NEAR_EPOCH_FETCH);
}

static int
io_multi_dkey_discard(struct io_test_args *arg, int flags)
{
	int			i;
	int			rc = 0;
	daos_epoch_t		epoch1, epoch2;
	struct d_uuid		cookie;
	daos_epoch_range_t	range;
	struct vts_counter	cntrs;
	struct io_req		*search_req;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	int			idx;

	arg->ta_flags = flags;
	cookie = gen_rand_cookie();

	epoch1 = 1000;
	epoch2 = 2000;
	for (i = 0; i < init_discard_keys; i++) {
		struct io_req	*req = NULL;

		set_key_and_index(&dkey_buf[0], &akey_buf[0], NULL);
		idx = i + 1;
		rc = io_update(arg, epoch1, &cookie, &dkey_buf[0],
			&akey_buf[0], &cntrs, &req, idx, UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		d_list_add(&req->rlist, &arg->req_list);
		rc = io_fetch(arg, epoch1, req, FETCH_VERBOSE);
		assert_int_equal(rc, 0);
	}

	arg->oid = dts_unit_oid_gen(0, 0, 0);
	for (i = init_discard_keys; i < init_discard_keys * 2; i++) {
		struct io_req	*req = NULL;

		set_key_and_index(&dkey_buf[0], &akey_buf[0], NULL);
		idx = i + 1;
		rc = io_update(arg, epoch2, &cookie, &dkey_buf[0],
			       &akey_buf[0], &cntrs, &req,
			       idx, UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		d_list_add(&req->rlist, &arg->req_list);
		rc  = io_fetch(arg, epoch2, req, FETCH_VERBOSE);
		assert_int_equal(rc, 0);
	}

	range.epr_lo = epoch1;
	range.epr_hi = epoch1;
	rc = vos_epoch_discard(arg->ctx.tc_co_hdl, &range, cookie.uuid);
	assert_int_equal(rc, 0);

	/** Check if the object does not exist? */
	struct vos_obj_df	*obj_res = NULL;

	rc = vos_oi_find(vos_hdl2cont(arg->ctx.tc_co_hdl), last_oid, 1,
			 &obj_res);
	assert_int_equal(rc, -DER_NONEXIST);
	assert_ptr_equal(obj_res, NULL);

	arg->oid = last_oid;

	/* Check first init_discard_keys entries in object 1 */
	d_list_for_each_entry(search_req, &arg->req_list, rlist) {
		if (search_req->epoch == epoch2)
			continue;
		rc = io_fetch_empty_buf(arg, search_req->epoch,
					search_req, FETCH_VERBOSE);
		assert_int_equal(rc, -DER_NONEXIST);
	}

	return 0;
}

static void
io_multi_dkey_discard_test(void **state)
{
	struct io_test_args	*arg = *state;

	io_multi_dkey_discard(arg, 0);
}

static void
io_multi_dkey_discard_test_zc(void **state)
{
	struct io_test_args	*arg = *state;

	io_multi_dkey_discard(arg, TF_ZERO_COPY);
}

static void
free_request_list(d_list_t *req_list)
{
	struct io_req		*tmp;
	struct io_req		*req;

	/** Free all request */
	d_list_for_each_entry_safe(req, tmp, req_list, rlist) {
		d_list_del_init(&req->rlist);
		D_FREE(req);
	}
}

static int
io_multikey_discard_teardown(void **state)
{
	struct io_test_args	*arg = *state;

	free_request_list(&arg->req_list);
	test_args_reset((struct io_test_args *) *state, VPOOL_SIZE);
	return 0;
}

static void
io_epoch_range_discard_test(void **state)
{
	struct io_test_args	*arg = *state;
	int			i;
	int			rc = 0;
	daos_epoch_t		epochs[init_discard_keys];
	struct d_uuid		cookie;
	daos_epoch_range_t	range;
	struct vts_counter	cntrs;
	struct io_req		*req[init_discard_keys];
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	int			idx;

	arg->ta_flags = 0;
	cookie = gen_rand_cookie();

	/** Need atleast 3 keys for this test */
	assert_true(init_discard_keys >= 11);

	for (i = 0; i < init_discard_keys; i++)
		epochs[i] = 1 + i;

	set_key_and_index(&dkey_buf[0], &akey_buf[0], &idx);

	/* Write to the same dkey-akey-idx on different epochs */
	for (i = 0; i < init_discard_keys; i++) {

		rc = io_update(arg, epochs[i], &cookie, &dkey_buf[0],
			       &akey_buf[0], &cntrs, &req[i], idx,
			       UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		rc = io_fetch(arg, epochs[i], req[i], FETCH_VERBOSE);
		assert_int_equal(rc, 0);
	}


	range.epr_lo = epochs[init_discard_keys - 10];
	range.epr_hi = epochs[init_discard_keys - 5];

	D_PRINT("Discard from "DF_U64" to "DF_U64" out of %d epochs\n",
		range.epr_lo, range.epr_hi, init_discard_keys);
	rc = vos_epoch_discard(arg->ctx.tc_co_hdl, &range, cookie.uuid);
	assert_int_equal(rc, 0);


	for (i = 0; i < init_discard_keys; i++) {
		/** Fall back while fetching from discarded epochs */
		if (i >= init_discard_keys - 10 && i <= init_discard_keys - 5)
		rc = io_fetch(arg, epochs[i], req[init_discard_keys - 11],
			      FETCH_VERBOSE);
		else
			rc = io_fetch(arg, epochs[i], req[i], FETCH_VERBOSE);

		assert_int_equal(rc, 0);
	}
	/** Cleanup */
	for (i = 0; i < init_discard_keys; i++)
		D_FREE(req[i]);
}

static void
io_multi_akey_discard_test(void **state)
{
	struct io_test_args	*arg = *state;
	int			i;
	int			rc = 0;
	daos_epoch_t		epoch1, epoch2;
	struct d_uuid		cookie;
	daos_epoch_range_t	range;
	struct vts_counter	cntrs;
	struct io_req		*search_req;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	int			idx;

	arg->ta_flags = 0;
	cookie = gen_rand_cookie();

	epoch1 = 1213;
	epoch2 = 8911;

	set_key_and_index(&dkey_buf[0], &akey_buf[0], &idx);

	for (i = 0; i < init_discard_keys; i++) {
		struct io_req	*req = NULL;

		rc = io_update(arg, epoch1, &cookie, &dkey_buf[0], &akey_buf[0],
			       &cntrs, &req, idx, UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		d_list_add(&req->rlist, &arg->req_list);
		rc = io_fetch(arg, epoch1, req, FETCH_VERBOSE);
		assert_int_equal(rc, 0);
		set_key_and_index(NULL, &akey_buf[0], NULL);
	}

	for (i = init_discard_keys; i < 2 * init_discard_keys; i++) {
		struct io_req	*req = NULL;

		rc = io_update(arg, epoch2, &cookie, &dkey_buf[0],
			       &akey_buf[0],  &cntrs, &req, idx,
			       UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		d_list_add(&req->rlist, &arg->req_list);
		rc = io_fetch(arg, epoch2, req, FETCH_VERBOSE);
		assert_int_equal(rc, 0);
		set_key_and_index(NULL, &akey_buf[0], NULL);
	}

	range.epr_lo = epoch1;
	range.epr_hi = epoch1;
	rc = vos_epoch_discard(arg->ctx.tc_co_hdl, &range, cookie.uuid);
	assert_int_equal(rc, 0);

	arg->oid = last_oid;
	/* Check first init_discard_keys entries in object 1 */
	d_list_for_each_entry(search_req, &arg->req_list, rlist) {
		if (epoch1 != search_req->epoch)
			continue;
		rc = io_fetch_empty_buf(arg, epoch1, search_req, false);
		assert_int_equal(rc, -DER_NONEXIST);
		i++;
	}
}

static void
io_multi_recx_overwrite_discard_test(void **state)
{
	struct io_test_args	*arg = *state;
	int			i, j;
	int			rc = 0;
	daos_epoch_t		epoch_start, discard_epoch;
	int			index_start;
	struct d_uuid		cookie;
	daos_epoch_range_t	range;
	struct vts_counter	cntrs;
	struct io_req		*search_req;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];

	if (arg->ta_flags & IF_DISABLED) {
		print_message("unsupported, skip\n");
		return;
	}

	arg->ta_flags = 0;
	cookie = gen_rand_cookie();

	epoch_start = 1234;
	discard_epoch = 1310;
	index_start = 1;

	set_key_and_index(&dkey_buf[0], &akey_buf[0], NULL);
	/**
	 * init_discard_keys/100 recx created
	 * all recx overwrite 100 epochs
	 * Starting from (epoch_start + 1 -> epoch_start + 100)
	 *
	 */
	j = 1;
	for (i = 0; i < init_discard_keys; i++) {
		struct io_req	*req = NULL;

		rc = io_update(arg, epoch_start + j,
			       &cookie, &dkey_buf[0], &akey_buf[0],
			       &cntrs, &req,
			       index_start, UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		d_list_add_tail(&req->rlist, &arg->req_list);
		rc = io_fetch(arg, epoch_start + j, req, FETCH_VERBOSE);
		assert_int_equal(rc, 0);

		if ((i + 1) % 100  == 0) {
			assert_true(j == 100);
			/** Update to next recx */
			index_start = index_start + 100;
			j = 0;
		}
		j++;
	}

	range.epr_lo = discard_epoch;
	range.epr_hi = discard_epoch;

	rc = vos_epoch_discard(arg->ctx.tc_co_hdl, &range, cookie.uuid);
	assert_int_equal(rc, 0);

	/* Check first init_discard_keys entries in object 1 */
	struct io_req *b_req = NULL;

	d_list_for_each_entry(search_req, &arg->req_list, rlist) {

		/* Save this for verifying check of previous epoch */
		if (search_req->epoch == discard_epoch - 1)
			b_req = search_req;
		if (discard_epoch == search_req->epoch) {
			assert_true(b_req != NULL);
			rc = io_fetch(arg, search_req->epoch, b_req,
				      FETCH_VERBOSE);
			assert_int_equal(rc, 0);
			b_req = NULL;
		} else {
			rc = io_fetch(arg, search_req->epoch, search_req,
				      FETCH_VERBOSE);
			assert_int_equal(rc, 0);
		}
	}
}

static void
io_multi_recx_discard_test(void **state)
{
	struct io_test_args	*arg = *state;
	int			i;
	int			rc = 0;
	daos_epoch_t		epoch1, epoch2;
	struct d_uuid		cookie;
	daos_epoch_range_t	range;
	struct vts_counter	cntrs;
	struct io_req		*search_req;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];

	if (arg->ta_flags & IF_DISABLED) {
		print_message("unsupported, skip\n");
		return;
	}

	arg->ta_flags = 0;
	cookie = gen_rand_cookie();

	epoch1 = 1234;
	epoch2 = 4567;

	set_key_and_index(&dkey_buf[0], &akey_buf[0], NULL);

	for (i = 0; i < init_discard_keys; i++) {
		struct io_req	*req = NULL;

		rc = io_update(arg, epoch1, &cookie, &dkey_buf[0], &akey_buf[0],
			       &cntrs, &req, i, UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		d_list_add(&req->rlist, &arg->req_list);
		rc = io_fetch(arg, epoch1, req, FETCH_VERBOSE);
		assert_int_equal(rc, 0);
	}

	for (i = init_discard_keys; i < 2 * init_discard_keys; i++) {
		struct io_req	*req = NULL;

		rc = io_update(arg, epoch2, &cookie, &dkey_buf[0],
			       &akey_buf[0],  &cntrs, &req, i, UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		d_list_add(&req->rlist, &arg->req_list);
		rc = io_fetch(arg, epoch2, req, FETCH_VERBOSE);
		assert_int_equal(rc, 0);
	}

	range.epr_lo = epoch1;
	range.epr_hi = epoch1;
	rc = vos_epoch_discard(arg->ctx.tc_co_hdl, &range, cookie.uuid);
	assert_int_equal(rc, 0);

	arg->oid = last_oid;
	/* Check first init_discard_keys entries in object 1 */
	d_list_for_each_entry(search_req, &arg->req_list, rlist) {
		if (epoch1 != search_req->epoch)
			continue;
		rc = io_fetch_empty_buf(arg, epoch1, search_req, false);
		assert_int_equal(rc, -DER_NONEXIST);
	}
}

static void
verify_io_fetch(struct io_test_args *arg)
{
	struct io_req	*search_req;
	int		 rc = 0;

	d_list_for_each_entry(search_req, &arg->req_list, rlist) {
		rc = io_fetch(arg, search_req->epoch, search_req,
			      FETCH_VERBOSE);
		assert_int_equal(rc, 0);
	}
}

static void
io_multi_aggregate_test(void **state, enum io_aggregate_test_type test_type)
{
	struct io_test_args	*arg = *state;
	int			 i;
	int			 rc = 0;
	daos_epoch_t		 epoch;
	struct d_uuid		 cookie;
	daos_epoch_range_t	 range;
	struct vts_counter	 cntrs;
	char			 dkey_buf[UPDATE_DKEY_SIZE];
	char			 akey_buf[UPDATE_AKEY_SIZE];
	int			 idx;
	unsigned int		 credits = -1;
	vos_purge_anchor_t	 vp_anchor = {0};
	bool			 finish;

	arg->ta_flags = 0;
	cookie = gen_rand_cookie();
	epoch = 1024;
	set_key_and_index(&dkey_buf[0], &akey_buf[0], &idx);

	for (i = 0; i < 2 * init_credits_keys; i++) {
		struct io_req	*req = NULL;

		rc = io_update(arg, epoch + i, &cookie, &dkey_buf[0],
			       &akey_buf[0], &cntrs, &req, idx,
			       UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		d_list_add(&req->rlist, &arg->req_list);
		rc = io_fetch(arg, epoch + i, req, FETCH_VERBOSE);
		assert_int_equal(rc, 0);
		if (test_type == IO_AGGREGATE_TEST_AKEY) {
			set_key_and_index(NULL, &akey_buf[0], NULL);
		} else {
			/* dkey is the only other option */
			set_key_and_index(&dkey_buf[0], NULL, NULL);
		}
	}

	range.epr_lo = epoch;
	range.epr_hi = epoch + init_credits_keys - 1;
	rc = vos_epoch_aggregate(arg->ctx.tc_co_hdl, arg->oid, &range,
				 &credits, &vp_anchor, &finish);
	assert_int_equal(rc, 0);
	assert_true(finish);

	/** Verifying aggregation */
	verify_io_fetch(arg);
}

static void
io_multi_dkey_aggregate_test(void **state)
{
	io_multi_aggregate_test(state, IO_AGGREGATE_TEST_DKEY);
}

static void
io_multi_akey_aggregate_test(void **state)
{
	io_multi_aggregate_test(state, IO_AGGREGATE_TEST_AKEY);
}

static void
io_multi_recx_aggregate_test(void **state)
{
	struct io_test_args	*arg = *state;
	int			i;
	int			rc = 0;
	daos_epoch_t		epoch;
	struct d_uuid		cookie;
	daos_epoch_range_t	range;
	struct vts_counter	cntrs;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	vos_purge_anchor_t	vp_anchor;
	unsigned int		credits = -1;
	bool			finish;

	if (arg->ta_flags & IF_DISABLED) {
		print_message("unsupported, skip\n");
		return;
	}
	arg->ta_flags = 0;
	cookie = gen_rand_cookie();

	epoch = 1;
	set_key_and_index(&dkey_buf[0], &akey_buf[0], NULL);
	memset(&vp_anchor, 0, sizeof(vos_purge_anchor_t));

	for (i = 0; i < init_credits_keys; i++) {
		struct io_req	*req = NULL;

		rc = io_update(arg, epoch + i,
			       &cookie, &dkey_buf[0], &akey_buf[0],
			       &cntrs, &req, i + 1, UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		d_list_add(&req->rlist, &arg->req_list);

		rc = io_fetch(arg, epoch + i, req, FETCH_VERBOSE);
		assert_int_equal(rc, 0);
	}

	range.epr_lo = epoch;
	range.epr_hi = epoch + init_credits_keys/2;

	rc = vos_epoch_aggregate(arg->ctx.tc_co_hdl, arg->oid, &range,
				 &credits, &vp_anchor, &finish);
	assert_int_equal(rc, 0);
	assert_true(finish);

	/** Verifying aggregation */
	verify_io_fetch(arg);
}

static void
verify_io_fetch_in_epoch_range(struct io_test_args *arg,
	daos_epoch_t min_epoch, daos_epoch_t max_epoch, d_list_t *req_list)
{
	struct io_req	*search_req;
	int		 rc = 0;


	d_list_for_each_entry(search_req, req_list, rlist) {
		if (search_req->epoch >= min_epoch &&
		    search_req->epoch < max_epoch) {
			rc = io_fetch_empty_buf(arg, search_req->epoch,
						search_req, FETCH_VERBOSE);
			assert_int_equal(rc, -DER_NONEXIST);
		} else {
			rc = io_fetch(arg, search_req->epoch, search_req,
				      FETCH_VERBOSE);
			assert_int_equal(rc, 0);
		}
	}
}

static void
io_recx_overwrite_aggregate(void **state)
{
	struct io_test_args	*arg = *state;
	int			i, index;
	int			rc = 0;
	daos_epoch_t		epoch;
	struct d_uuid		cookie;
	daos_epoch_range_t	range;
	struct vts_counter	cntrs;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	bool			finish;
	vos_purge_anchor_t	vp_anchor = {0};
	unsigned int		credits = -1;

	arg->ta_flags = 0;
	cookie = gen_rand_cookie();

	epoch = 1234;

	set_key_and_index(&dkey_buf[0], &akey_buf[0], &index);

	for (i = 0; i < 2 * init_discard_keys; i++) {
		struct io_req	*req = NULL;

		rc = io_update(arg, epoch + i,
			       &cookie, &dkey_buf[0], &akey_buf[0],
			       &cntrs, &req, index, UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		d_list_add(&req->rlist, &arg->req_list);
		rc = io_fetch(arg, epoch + i, req, FETCH_VERBOSE);
		assert_int_equal(rc, 0);
	}

	range.epr_lo = epoch;
	range.epr_hi = epoch + init_discard_keys  - 1;

	rc = vos_epoch_aggregate(arg->ctx.tc_co_hdl, arg->oid, &range,
				 &credits, &vp_anchor, &finish);
	assert_int_equal(rc, 0);
	assert_true(finish);

	/** Verifying aggregation */
	verify_io_fetch_in_epoch_range(arg, range.epr_lo, range.epr_hi,
				       &arg->req_list);
}

static void
io_recx_overwrite(struct io_test_args *arg)
{
	int			i, index;
	int			rc = 0;
	daos_epoch_t		epoch1, epoch2;
	struct d_uuid		cookie;
	daos_epoch_range_t	range;
	struct vts_counter	cntrs;
	bool			finish;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	vos_purge_anchor_t	vp_anchor = {0};
	int			credits = init_credits_keys + 100;
	daos_epoch_t		max_epoch = DAOS_EPOCH_MAX;

	cookie = gen_rand_cookie();

	epoch1 = 1000;

	set_key_and_index(&dkey_buf[0], &akey_buf[0], &index);

	for (i = 0; i < init_credits_keys; i++) {
		struct io_req	*req = NULL;

		max_epoch = epoch1 + i;
		rc = io_update(arg, max_epoch,
			       &cookie, &dkey_buf[0], &akey_buf[0],
			       &cntrs, &req, index, UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		d_list_add(&req->rlist, &arg->req_list);
		rc = io_fetch(arg, max_epoch, req, FETCH_VERBOSE);
		assert_int_equal(rc, 0);
	}

	epoch2 = 500000;

	for (i = 0; i < init_credits_keys; i++) {
		struct io_req	*req = NULL;

		rc = io_update(arg, epoch2 + i,
			       &cookie, &dkey_buf[0], &akey_buf[0],
			       &cntrs, &req, index, UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		d_list_add(&req->rlist, &arg->req_list);
		rc = io_fetch(arg, epoch2 + i, req, FETCH_VERBOSE);
		assert_int_equal(rc, 0);
	}

	range.epr_lo = epoch1;
	range.epr_hi = epoch1 + init_credits_keys + 10;

	printf("%d keys in range "DF_U64"->"DF_U64",",
	       init_credits_keys, range.epr_lo, range.epr_hi);
	printf(" using %d credits\n", credits);

	while (credits > 0) {
		unsigned int local_credits = 1;

		rc = vos_epoch_aggregate(arg->ctx.tc_co_hdl, arg->oid, &range,
					 &local_credits, &vp_anchor, &finish);
		assert_int_equal(rc, 0);
		if ((arg->ta_flags & TF_REPORT_AGGREGATION) && finish)
			break;
		credits -= 1;
	}
	assert_true(finish);

	/** Verifying aggregation */
	verify_io_fetch_in_epoch_range(arg, range.epr_lo, max_epoch,
				       &arg->req_list);
}

#define CREDITS_PER_ITERATION (1)
static void
io_multi_recx_overwrite_test(struct io_test_args *arg,
	enum multi_recx_overwrite_test_type test_type)
{
	int			i, index;
	int			rc = 0;
	daos_epoch_t		epoch;
	struct d_uuid		cookie;
	daos_epoch_range_t	range;
	struct vts_counter	cntrs;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	int			overwrite = 0;
	d_list_t		agg_entries;
	unsigned int		l_credits;
	vos_purge_anchor_t	vp_anchor;
	bool			finish;

	if (arg->ta_flags & IF_DISABLED) {
		print_message("unsupported, skip\n");
		return;
	}
	arg->ta_flags = 0;
	cookie = gen_rand_cookie();
	epoch = 1;

	set_key_and_index(&dkey_buf[0], &akey_buf[0], &index);
	memset(&vp_anchor, 0, sizeof(vos_purge_anchor_t));
	D_INIT_LIST_HEAD(&agg_entries);

	for (i = 0; i < init_discard_keys; i++) {
		struct io_req	*req = NULL;

		rc = io_update(arg, epoch + i,
			       &cookie, &dkey_buf[0], &akey_buf[0],
			       &cntrs, &req, index, UPDATE_VERBOSE);
		assert_int_equal(rc, 0);

		if (overwrite)
			d_list_add(&req->rlist, &arg->req_list);
		else
			d_list_add(&req->rlist, &agg_entries);

		rc = io_fetch(arg, epoch + i, req, FETCH_VERBOSE);
		assert_int_equal(rc, 0);

		overwrite++;
		if (overwrite == 2) {
			index += 1;
			overwrite  = 0;
		}
	}

	range.epr_lo = epoch;
	range.epr_hi = epoch + init_discard_keys/2;

	if (test_type == MULTI_RECX_OVERWRITE_NO_CREDITS) {
		l_credits = -1; /* Set all bits to 1, or infinite credits */
		rc = vos_epoch_aggregate(arg->ctx.tc_co_hdl, arg->oid, &range,
					 &l_credits, &vp_anchor, &finish);
		assert_int_equal(rc, 0);
	} else {
		int loop = init_discard_keys + 1000;

		print_message("%d credit(s)/iteration in %d iterations\n",
			      CREDITS_PER_ITERATION, loop);
		for (i = 0; i < loop; i++) {
			l_credits = CREDITS_PER_ITERATION;
			rc = vos_epoch_aggregate(arg->ctx.tc_co_hdl, arg->oid,
						 &range, &l_credits,
						 &vp_anchor, &finish);
			assert_int_equal(rc, 0);
		}
	}
	assert_true(finish);

	daos_unit_oid_t		oid_tmp;
	vos_cont_info_t		info;
	daos_handle_t		coh;

	memset(&oid_tmp, 0, sizeof(daos_unit_oid_t));
	rc = vos_epoch_aggregate(arg->ctx.tc_co_hdl, oid_tmp,
				 &range, &l_credits, &vp_anchor, &finish);
	assert_int_equal(rc, 0);

	rc = vos_cont_query(arg->ctx.tc_co_hdl, &info);
	assert_true(range.epr_hi == info.pci_purged_epoch);

	rc = vos_cont_open(arg->ctx.tc_po_hdl, arg->ctx.tc_co_uuid, &coh);
	assert_int_equal(rc, 0);

	rc = vos_cont_query(coh, &info);
	assert_true(range.epr_hi == info.pci_purged_epoch);

	rc = vos_cont_close(coh);
	assert_int_equal(rc, 0);

	/** Verifying aggregation */
	/**
	 * Checking all writes at max_epoch for that record
	 * this would be retained
	 */
	verify_io_fetch(arg);
	verify_io_fetch_in_epoch_range(arg, range.epr_lo, range.epr_hi,
				       &agg_entries);

	/** Aggregate list is created here locally. cleaning up!*/
	free_request_list(&agg_entries);
}

static void
io_recx_overwrite_credits(void **state)
{
	struct io_test_args	*arg = *state;

	arg->ta_flags = 0;
	io_recx_overwrite(arg);
}

static void
io_recx_overwrite_report(void **state)
{
	struct io_test_args	*arg = *state;

	arg->ta_flags = TF_REPORT_AGGREGATION;
	io_recx_overwrite(arg);
}

static void
io_multi_recx_overwrite_test_without_credits(void **state)
{
	struct io_test_args	*arg = *state;

	io_multi_recx_overwrite_test(arg, MULTI_RECX_OVERWRITE_NO_CREDITS);
}

static void
io_multi_recx_overwrite_test_with_credits(void **state)
{
	struct io_test_args	*arg = *state;

	io_multi_recx_overwrite_test(arg, MULTI_RECX_OVERWRITE_WITH_CREDITS);
}

static const struct CMUnitTest discard_tests[] = {
	{ "VOS301: VOS Simple discard test",
		io_simple_one_key_discard, io_simple_discard_setup,
		io_simple_discard_teardown},
	{ "VOS302.0: VOS Near Epoch fetch test",
		io_test_near_epoch_fetch, io_multikey_discard_setup,
		io_multikey_discard_teardown},
	{ "VOS302.1: VOS Near Epoch fetch test overwrite idx",
		io_near_epoch_idx_overwrite_fetch,
		io_multikey_discard_setup,
		io_multikey_discard_teardown},
	{ "VOS302.2: VOS Near Epoch punch test",
		io_near_epoch_punch,
		io_multikey_discard_setup,
		io_multikey_discard_teardown},
	{ "VOS302.3: VOS discard punched record test",
		io_discard_punch,
		io_multikey_discard_setup,
		io_multikey_discard_teardown},
	{ "VOS303.0: VOS multikey discard test",
		io_multi_dkey_discard_test, io_multikey_discard_setup,
		io_multikey_discard_teardown},
	{ "VOS303.1: VOS multikey discard test Zero copy",
		io_multi_dkey_discard_test_zc, io_multikey_discard_setup,
		io_multikey_discard_teardown},
	{ "VOS304: VOS multi akey discard test",
		io_multi_akey_discard_test, io_multikey_discard_setup,
		io_multikey_discard_teardown},
	{ "VOS305.0: VOS multi recx discard test",
		io_multi_recx_discard_test,
		io_multi_recx_discard_setup,
		io_multikey_discard_teardown},
	{ "VOS305.1: VOS multi recx and overwrite discard test",
		io_multi_recx_overwrite_discard_test,
		io_multi_recx_discard_setup,
		io_multikey_discard_teardown},
	{ "VOS306: VOS epoch range discard test",
		io_epoch_range_discard_test, io_multikey_discard_setup,
		io_multikey_discard_teardown},
};

static const struct CMUnitTest aggregate_tests[] = {
	{ "VOS401.1: VOS recx overwrite aggregate test",
		io_recx_overwrite_aggregate, io_multikey_discard_setup,
		io_multikey_discard_teardown},
	{ "VOS401.2: VOS recx overwrite aggregate with credits",
		io_recx_overwrite_credits, io_multikey_discard_setup,
		io_multikey_discard_teardown},
	{ "VOS401.3: VOS recx overwrite aggregated with completion reporting",
		io_recx_overwrite_report, io_multikey_discard_setup,
		io_multikey_discard_teardown},
	{ "VOS402.1: VOS multi recx overwrite test without credits",
		io_multi_recx_overwrite_test_without_credits,
		io_multi_recx_discard_setup, io_multikey_discard_teardown},
	{ "VOS402.2: VOS multi recx overwrite test with credits",
		io_multi_recx_overwrite_test_with_credits,
		io_multi_recx_discard_setup, io_multikey_discard_teardown},
	{ "VOS403.1: VOS dkey update aggregate test",
		io_multi_dkey_aggregate_test, io_multikey_discard_setup,
		io_multikey_discard_teardown},
	{ "VOS403.2: VOS akey update aggregate test",
		io_multi_akey_aggregate_test, io_multikey_discard_setup,
		io_multikey_discard_teardown},
	{ "VOS403.3: VOS recx update aggregate test",
		io_multi_recx_aggregate_test, io_multi_recx_discard_setup,
		io_multikey_discard_teardown},

};

int
run_discard_tests(int keys)
{
	if (keys) {
		init_discard_keys = keys;
		init_credits_keys = keys / 2;
	}
	return cmocka_run_group_tests_name("VOS Discard test", discard_tests,
					   setup_io, teardown_io);
}

int
run_aggregate_tests(void)
{
	return cmocka_run_group_tests_name("VOS Aggregate test",
					   aggregate_tests, setup_io,
					   teardown_io);
}
