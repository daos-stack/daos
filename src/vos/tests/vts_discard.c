/**
 * (C) Copyright 2016 Intel Corporation.
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

#include <vts_io.h>
#include <vos_hhash.h>

/**
 * Stores the last key and can be used for
 * punching or overwrite
 */
char		last_dkey[UPDATE_DKEY_SIZE];
char		last_akey[UPDATE_AKEY_SIZE];
daos_unit_oid_t	last_oid;

#define		FETCH_VERBOSE false
#define		UPDATE_VERBOSE false
static int
io_update(struct io_test_args *arg, daos_epoch_t update_epoch,
	  struct daos_uuid *cookie, char *dkey, char *akey,
	  struct vts_counter *cntrs, struct io_req **io_req,
	  int idx, bool verbose)
{

	int			rc  = 0;
	struct io_req		*ioreq;

	D_ALLOC_PTR(ioreq);
	uuid_copy(ioreq->cookie.uuid, cookie->uuid);
	memset(&ioreq->vio, 0, sizeof(ioreq->vio));
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
		ioreq->rex.rx_rsize	= ioreq->val_iov.iov_len;
	} else {
		daos_iov_set(&ioreq->dkey, dkey, UPDATE_DKEY_SIZE);
		daos_iov_set(&ioreq->akey, akey, UPDATE_AKEY_SIZE);
		memset(ioreq->update_buf, 0, UPDATE_BUF_SIZE);
		daos_iov_set(&ioreq->val_iov, &ioreq->update_buf,
			     UPDATE_BUF_SIZE);
		ioreq->rex.rx_rsize = 0;
	}

	ioreq->sgl.sg_nr.num = 1;
	ioreq->sgl.sg_iovs = &ioreq->val_iov;

	ioreq->rex.rx_nr	= 1;
	ioreq->rex.rx_idx	= idx;
	ioreq->vio.vd_name	= ioreq->akey;
	ioreq->vio.vd_recxs	= &ioreq->rex;
	ioreq->vio.vd_nr	= 1;
	ioreq->epoch		= update_epoch;

	rc = io_test_obj_update(arg, update_epoch, &ioreq->dkey, &ioreq->vio,
				&ioreq->sgl, &ioreq->cookie, verbose);
	if (rc)
		D_GOTO(exit, rc);

	inc_cntr_manual(arg->ta_flags, cntrs);
	if (verbose) {
		print_message("dkey: %s\n", ioreq->dkey_buf);
		print_message("akey: %s\n", ioreq->akey_buf);
		print_message("recx: %u\n",
			      (unsigned int)ioreq->rex.rx_idx);
	}
exit:
	*io_req = ioreq;
	return rc;
}

/**
 * NB: This version of fetch is for verifying in case of recx and
 * akey discard. This is used because  vec_fetch,
 * do not return -DER_NONEXIST currently
 */
static int
io_fetch_empty_buf(struct io_test_args *arg, daos_epoch_t fetch_epoch,
		  struct io_req *req, bool verbose)
{
	int rc  = 0;

	if (verbose) {
		print_message("dkey: %s\n", req->dkey_buf);
		print_message("akey: %s\n", req->akey_buf);
		print_message("Fetch_BUF: %s, epoch"DF_U64"\n",
			      req->fetch_buf, fetch_epoch);
	}

	memset(req->fetch_buf, 0, UPDATE_BUF_SIZE);
	daos_iov_set(&req->val_iov, &req->fetch_buf, UPDATE_BUF_SIZE);
	req->rex.rx_rsize = UPDATE_BUF_SIZE;
	rc = io_test_obj_fetch(arg, fetch_epoch, &req->dkey, &req->vio,
			       &req->sgl, false);
	if (rc)
		D_GOTO(exit, rc);

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

	memset(req->fetch_buf, 0, UPDATE_BUF_SIZE);
	daos_iov_set(&req->val_iov, &req->fetch_buf, UPDATE_BUF_SIZE);
	req->rex.rx_rsize = UPDATE_BUF_SIZE;
	rc = io_test_obj_fetch(arg, fetch_epoch, &req->dkey, &req->vio,
			       &req->sgl, false);
	if (rc)
		D_GOTO(exit, rc);
	if (!rc && req->rex.rx_rsize == 0)
		return -DER_NONEXIST;


	if (verbose) {
		print_message("dkey: %s\n", req->dkey_buf);
		print_message("akey: %s\n", req->akey_buf);
		print_message("fetch_buf: %s, epoch"DF_U64"\n",
			      req->fetch_buf, fetch_epoch);
	}

	assert_memory_equal(req->update_buf, req->fetch_buf, UPDATE_BUF_SIZE);

exit:
	return rc;
}


static inline void
set_key_and_index(char *dkey, char *akey, int *index)
{
	if (dkey != NULL) {
		memset(dkey, 0, UPDATE_DKEY_SIZE);
		gen_rand_key(dkey, UPDATE_DKEY, UPDATE_DKEY_SIZE);
	}

	if (akey != NULL) {
		memset(akey, 0, UPDATE_AKEY_SIZE);
		gen_rand_key(akey, UPDATE_AKEY, UPDATE_AKEY_SIZE);
	}

	if (index != NULL)
		*index = (daos_hash_string_u32(dkey, UPDATE_DKEY_SIZE)) %
			 1000000;
}

static int
io_simple_discard_setup(void **state)
{
	struct io_test_args	*args = *state;

	args->oid = gen_oid();

	return 0;
}

static inline int
io_create_object(struct vc_hdl *co_hdl)
{
	int			rc = 0;
	daos_unit_oid_t		oid;
	struct vos_obj		*obj;

	oid = gen_oid();
	rc = vos_oi_find_alloc(co_hdl, oid, &obj);
	return rc;
}

static inline int
io_simple_update(struct io_test_args *arg,
		 struct daos_uuid *cookie,
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
	struct daos_uuid	cookie;
	daos_epoch_range_t	range;

	arg->ta_flags = 0;
	/* create two objects. these need to be ignored */
	rc = io_create_object(vos_hdl2co(arg->ctx.tc_co_hdl));
	assert_int_equal(rc, 0);
	io_create_object(vos_hdl2co(arg->ctx.tc_co_hdl));
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
	range.epr_hi = -1;
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
		D_FREE_PTR(req[i]);
}

static int
io_simple_discard_teardown(void **state)
{
	test_args_reset((struct io_test_args *) *state);
	return 0;
}


static int
io_multikey_discard_setup(void **state)
{
	struct io_test_args	*arg = *state;

	DAOS_INIT_LIST_HEAD(&arg->req_list);
	arg->oid = gen_oid();
	last_oid = arg->oid;

	return 0;
}

static int
io_near_epoch_tests(struct io_test_args *arg, char *dkey,
		    char *akey, daos_epoch_t *epoch,
		    struct daos_uuid *cookie, int *idx,
		    int num, unsigned long *flags)
{
	int			i, mid, rc = 0;
	struct vts_counter	cntrs;
	daos_epoch_range_t	range;
	struct io_req		**reqs;
	bool			*punch;

	assert_true(num/2 >= 0);
	mid = num/2;

	D_ALLOC(reqs,  (num * sizeof(struct io_req *)));
	D_ALLOC(punch, (num * sizeof(bool)));


	for (i = 0; i < num; i++) {
		struct daos_uuid l_cookie;

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
	range.epr_lo = epoch[mid];
	rc = vos_epoch_discard(arg->ctx.tc_co_hdl, &range, cookie[mid].uuid);
	if (rc != 0)
		D_GOTO(exit, rc);

	/* Fetch should return the value buf from req 0 */
	rc = io_fetch(arg, epoch[mid], reqs[mid - 1], false);
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
	D_FREE(reqs, (num * sizeof(struct io_req *)));
	D_FREE(punch, (num * sizeof(bool)));

	return rc;
}

static inline void
set_near_epoch_tests(struct daos_uuid *cookie, daos_epoch_t *epochs,
		     int *idx, int num)
{

	int	i = 0;

	for (i = 0; i < num; i++) {
		cookie[i] = gen_rand_cookie();
		epochs[i] = (i + 1) * 1000;
		if (i > 0)
			idx[i] = idx[i - 1];
	}
}


static void
io_near_epoch_idx_overwrite_fetch(void **state)
{

	struct io_test_args	*arg = *state;
	daos_epoch_t		epoch[3];
	struct daos_uuid	cookie[3];
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	int			idx[3], rc = 0;

	set_key_and_index(&dkey_buf[0], &akey_buf[0], &idx[0]);
	set_near_epoch_tests(&cookie[0], &epoch[0], &idx[0], 3);
	arg->ta_flags = 0;

	rc = io_near_epoch_tests(arg, &dkey_buf[0], &akey_buf[0], &epoch[0],
				 &cookie[0], &idx[0], 3, NULL);
	assert_int_equal(rc, 0);
}

static void
io_near_epoch_punch(void **state)
{

	struct io_test_args	*arg = *state;
	daos_epoch_t		epoch[3];
	struct daos_uuid	cookie[3];
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	int			idx[3], rc = 0;
	unsigned long		flags[3];

	memset(&flags, '0', 3 * sizeof(unsigned long));
	set_key_and_index(&dkey_buf[0], &akey_buf[0], &idx[0]);
	set_near_epoch_tests(&cookie[0], &epoch[0], &idx[0], 3);

	flags[0] = TF_PUNCH;
	rc = io_near_epoch_tests(arg, &dkey_buf[0], &akey_buf[0], &epoch[0],
				 &cookie[0], &idx[0], 3, &flags[0]);
	assert_int_equal(rc, 0);
}

static void
io_discard_punch(void **state)
{

	struct io_test_args	*arg = *state;
	daos_epoch_t		epoch[3];
	struct daos_uuid	cookie[3];
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	int			idx[3], rc = 0;
	unsigned long		flags[3];

	memset(&flags, '0', 3 * sizeof(unsigned long));
	set_key_and_index(&dkey_buf[0], &akey_buf[0], &idx[0]);
	set_near_epoch_tests(&cookie[0], &epoch[0], &idx[0], 3);

	flags[1] = TF_PUNCH;
	rc = io_near_epoch_tests(arg, &dkey_buf[0], &akey_buf[0], &epoch[0],
				 &cookie[0], &idx[0], 3, &flags[0]);
	assert_int_equal(rc, 0);
}

static void
io_test_near_epoch_fetch(void **state)
{

	struct io_test_args	*arg = *state;
	daos_epoch_t		epoch[3];
	struct daos_uuid	cookie[3];
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	int			idx[3], i, rc = 0;

	arg->ta_flags = 0;

	set_key_and_index(&dkey_buf[0], &akey_buf[0], &idx[0]);
	set_near_epoch_tests(&cookie[0], &epoch[0], &idx[0], 3);
	for (i = 1; i < 3; i++)
		idx[i] = idx[i - 1] + 1;

	rc = io_near_epoch_tests(arg, &dkey_buf[0], &akey_buf[0], &epoch[0],
				 &cookie[0], &idx[0], 3, NULL);
	assert_int_equal(rc, 0);
}


#define TF_DISCARD_KEYS	100000

static int
io_multi_dkey_discard(struct io_test_args *arg, int flags)
{
	int			i;
	int			rc = 0;
	daos_epoch_t		epoch1, epoch2;
	struct daos_uuid	cookie;
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
	for (i = 0; i < TF_DISCARD_KEYS; i++) {
		struct io_req	*req = NULL;

		set_key_and_index(&dkey_buf[0], &akey_buf[0], &idx);
		rc = io_update(arg, epoch1, &cookie, &dkey_buf[0],
			       &akey_buf[0], &cntrs, &req, idx, UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		daos_list_add(&req->rlist, &arg->req_list);
		rc = io_fetch(arg, epoch1, req, FETCH_VERBOSE);
		assert_int_equal(rc, 0);
	}

	arg->oid = gen_oid();

	for (i = 0; i < TF_DISCARD_KEYS; i++) {
		struct io_req	*req = NULL;

		set_key_and_index(&dkey_buf[0], &akey_buf[0], &idx);
		rc = io_update(arg, epoch2, &cookie, &dkey_buf[0],
			       &akey_buf[0], &cntrs, &req,
			       idx, UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		daos_list_add(&req->rlist, &arg->req_list);
		rc  = io_fetch(arg, epoch2, req, FETCH_VERBOSE);
		assert_int_equal(rc, 0);
	}

	range.epr_lo = epoch1;
	rc = vos_epoch_discard(arg->ctx.tc_co_hdl, &range, cookie.uuid);
	assert_int_equal(rc, 0);

	/** Check if the object does not exist? */
	struct vos_obj		*obj_res;

	rc = vos_oi_find(vos_hdl2co(arg->ctx.tc_co_hdl),
			 last_oid, &obj_res);
	assert_int_equal(rc, -DER_NONEXIST);

	arg->oid = last_oid;

	/* Check first TF_DISCARD_KEYS entries in object 1 */
	i = 0;
	daos_list_for_each_entry(search_req, &arg->req_list, rlist) {
		if (i >= TF_DISCARD_KEYS)
			break;
		rc = io_fetch(arg, epoch1, search_req, UPDATE_VERBOSE);
		assert_int_equal(rc, -DER_NONEXIST);
		i++;
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


static int
io_multikey_discard_teardown(void **state)
{
	struct io_test_args	*arg = *state;
	struct io_req		*tmp;
	struct io_req		*req;

	/** Free all request */
	daos_list_for_each_entry_safe(req, tmp, &arg->req_list, rlist) {
		daos_list_del_init(&req->rlist);
		free(req);
	}

	test_args_reset((struct io_test_args *) *state);
	return 0;
}

static void
io_epoch_range_discard_test(void **state)
{

	struct io_test_args	*arg = *state;
	int			i;
	int			rc = 0;
	daos_epoch_t		epochs[TF_DISCARD_KEYS];
	struct daos_uuid	cookie;
	daos_epoch_range_t	range;
	struct vts_counter	cntrs;
	struct io_req		*req[TF_DISCARD_KEYS];
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	int			idx;

	arg->ta_flags = 0;
	cookie = gen_rand_cookie();

	/** Need atleast 3 keys for this test */
	assert_true(TF_DISCARD_KEYS >= 11);

	for (i = 0; i < TF_DISCARD_KEYS; i++)
		epochs[i] = 1 + i;

	set_key_and_index(&dkey_buf[0], &akey_buf[0], &idx);

	/* Write to the same dkey-akey-idx on different epochs */
	for (i = 0; i < TF_DISCARD_KEYS; i++) {

		rc = io_update(arg, epochs[i], &cookie, &dkey_buf[0],
			       &akey_buf[0], &cntrs, &req[i], idx,
			       UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		rc = io_fetch(arg, epochs[i], req[i], false);
		assert_int_equal(rc, 0);
	}

	range.epr_lo = epochs[TF_DISCARD_KEYS - 10];
	D_PRINT("Discard from "DF_U64" to "DF_U64" out of %d epochs\n",
		epochs[TF_DISCARD_KEYS - 10], epochs[TF_DISCARD_KEYS - 1],
		TF_DISCARD_KEYS);
	rc = vos_epoch_discard(arg->ctx.tc_co_hdl, &range, cookie.uuid);
	assert_int_equal(rc, 0);

	for (i = 0; i < TF_DISCARD_KEYS; i++) {
		 /** Fall back while fetching from discarded epochs */
		if (i >= TF_DISCARD_KEYS - 10)
			rc = io_fetch(arg, epochs[i], req[TF_DISCARD_KEYS - 11],
				      false);
		else
			rc = io_fetch(arg, epochs[i], req[i], false);

		assert_int_equal(rc, 0);
	}
	/** Cleanup */
	for (i = 0; i < TF_DISCARD_KEYS; i++)
		D_FREE_PTR(req[i]);
}

static void
io_multi_akey_discard_test(void **state)
{
	struct io_test_args	*arg = *state;
	int			i;
	int			rc = 0;
	daos_epoch_t		epoch1, epoch2;
	struct daos_uuid	cookie;
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

	for (i = 0; i < TF_DISCARD_KEYS; i++) {
		struct io_req	*req = NULL;

		rc = io_update(arg, epoch1, &cookie, &dkey_buf[0], &akey_buf[0],
			       &cntrs, &req, idx, UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		daos_list_add(&req->rlist, &arg->req_list);
		rc = io_fetch(arg, epoch1, req, false);
		assert_int_equal(rc, 0);
		set_key_and_index(NULL, &akey_buf[0], NULL);
	}

	for (i = TF_DISCARD_KEYS; i < 2 * TF_DISCARD_KEYS; i++) {
		struct io_req	*req = NULL;

		rc = io_update(arg, epoch2, &cookie, &dkey_buf[0],
			       &akey_buf[0],  &cntrs, &req, idx,
			       UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		daos_list_add(&req->rlist, &arg->req_list);
		rc = io_fetch(arg, epoch2, req, false);
		assert_int_equal(rc, 0);
		set_key_and_index(NULL, &akey_buf[0], NULL);
	}

	range.epr_lo = epoch1;
	rc = vos_epoch_discard(arg->ctx.tc_co_hdl, &range, cookie.uuid);
	assert_int_equal(rc, 0);

	arg->oid = last_oid;
	/* Check first TF_DISCARD_KEYS entries in object 1 */
	i = 0;
	daos_list_for_each_entry(search_req, &arg->req_list, rlist) {
		if (epoch1 != search_req->epoch)
			continue;
		rc = io_fetch_empty_buf(arg, epoch1, search_req, false);
		assert_int_equal(rc, -DER_NONEXIST);
		i++;
	}
}

static void
io_multi_recx_discard_test(void **state)
{
	struct io_test_args	*arg = *state;
	int			i;
	int			rc = 0;
	daos_epoch_t		epoch1, epoch2;
	struct daos_uuid	cookie;
	daos_epoch_range_t	range;
	struct vts_counter	cntrs;
	struct io_req		*search_req;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];

	arg->ta_flags = 0;
	cookie = gen_rand_cookie();

	epoch1 = 1234;
	epoch2 = 4567;

	set_key_and_index(&dkey_buf[0], &akey_buf[0], NULL);

	for (i = 0; i < TF_DISCARD_KEYS; i++) {
		struct io_req	*req = NULL;

		rc = io_update(arg, epoch1, &cookie, &dkey_buf[0], &akey_buf[0],
			       &cntrs, &req, i, UPDATE_VERBOSE);
		assert_int_equal(rc, 0);
		daos_list_add(&req->rlist, &arg->req_list);
		rc = io_fetch(arg, epoch1, req, false);
		assert_int_equal(rc, 0);
	}

	for (i = TF_DISCARD_KEYS; i < 2 * TF_DISCARD_KEYS; i++) {
		struct io_req	*req = NULL;

		rc = io_update(arg, epoch2, &cookie, &dkey_buf[0],
			       &akey_buf[0],  &cntrs, &req, i, false);
		assert_int_equal(rc, 0);
		daos_list_add(&req->rlist, &arg->req_list);
		rc = io_fetch(arg, epoch2, req, false);
		assert_int_equal(rc, 0);
	}

	range.epr_lo = epoch1;
	rc = vos_epoch_discard(arg->ctx.tc_co_hdl, &range, cookie.uuid);
	assert_int_equal(rc, 0);

	arg->oid = last_oid;
	/* Check first TF_DISCARD_KEYS entries in object 1 */
	i = 0;
	daos_list_for_each_entry(search_req, &arg->req_list, rlist) {
		if (epoch1 != search_req->epoch)
			continue;
		rc = io_fetch_empty_buf(arg, epoch1, search_req, false);
		assert_int_equal(rc, -DER_NONEXIST);
		i++;
	}
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
	{ "VOS302.2: VOS discard punched record test",
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
	{ "VOS305: VOS multi recx discard test",
		io_multi_recx_discard_test, io_multikey_discard_setup,
		io_multikey_discard_teardown},
	{ "VOS306: VOS epoch range discard test",
		io_epoch_range_discard_test, io_multikey_discard_setup,
		io_multikey_discard_teardown},
};

int
run_discard_test(void)
{
	return cmocka_run_group_tests_name("VOS Discard test", discard_tests,
					   setup_io, teardown_io);
}

