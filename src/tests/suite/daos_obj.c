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
 * This file is part of daos
 *
 * tests/suite/daos_obj.c
 */
#define DD_SUBSYS	DD_FAC(tests)
#include "daos_iotest.h"

void
ioreq_init(struct ioreq *req, daos_obj_id_t oid, test_arg_t *arg)
{
	int rc;
	int i;
	bool ev_flag;

	memset(req, 0, sizeof(*req));

	req->arg = arg;
	if (arg->async) {
		rc = daos_event_init(&req->ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	arg->expect_result = 0;
	daos_fail_loc_set(arg->fail_loc);
	daos_fail_value_set(arg->fail_value);

	/* init sgl */
	for (i = 0; i < IOREQ_SG_VD_NR; i++) {
		req->sgl[i].sg_nr.num = IOREQ_SG_NR;
		req->sgl[i].sg_iovs = req->val_iov[i];
	}

	/* init csum */
	daos_csum_set(&req->csum, &req->csum_buf[0], UPDATE_CSUM_SIZE);

	/* init record extent */
	for (i = 0; i < IOREQ_SG_VD_NR; i++) {
		int j;

		for (j = 0; j < IOREQ_VD_NR; j++) {
			req->rex[i][j].rx_nr = 1;
			req->rex[i][j].rx_idx = 0;

			/** epoch range: required by the wire format */
			req->erange[i][j].epr_lo = 0;
			req->erange[i][j].epr_hi = DAOS_EPOCH_MAX;
		}

		/* vector I/O descriptor */
		req->vio[i].vd_recxs = req->rex[i];
		req->vio[i].vd_nr = IOREQ_VD_NR;

		/* epoch descriptor */
		req->vio[i].vd_eprs = req->erange[i];

		req->vio[i].vd_kcsum.cs_csum = NULL;
		req->vio[i].vd_kcsum.cs_buf_len = 0;
		req->vio[i].vd_kcsum.cs_len = 0;

	}
	D_DEBUG(DF_MISC, "open oid="DF_OID"\n", DP_OID(oid));

	/** open the object */
	rc = daos_obj_open(arg->coh, oid, 0, 0, &req->oh,
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

static void
insert_internal(daos_key_t *dkey, int nr, daos_sg_list_t *sgls,
		daos_vec_iod_t *vds, daos_epoch_t epoch, struct ioreq *req)
{
	bool ev_flag;
	int rc;

	/** execute update operation */
	rc = daos_obj_update(req->oh, epoch, dkey, nr, vds, sgls,
			     req->arg->async ? &req->ev : NULL);
	if (!req->arg->async) {
		assert_int_equal(rc, req->arg->expect_result);
		return;
	}

	rc = daos_event_test(&req->ev, DAOS_EQ_WAIT, &ev_flag);
	assert_int_equal(rc, 0);
	assert_int_equal(ev_flag, true);
	assert_int_equal(req->ev.ev_error, req->arg->expect_result);
}

static void
ioreq_dkey_set(struct ioreq *req, const char *dkey)
{
	daos_iov_set(&req->dkey, (void *)dkey, strlen(dkey));
}

static void
ioreq_akey_set(struct ioreq *req, const char **akey, int nr)
{
	int i;

	assert_in_range(nr, 1, IOREQ_SG_VD_NR);
	/** akey */
	for (i = 0; i < nr; i++)
		daos_iov_set(&req->vio[i].vd_name, (void *)akey[i],
			     strlen(akey[i]));
}

static void
ioreq_sgl_simple_set(struct ioreq *req, void **value,
		     daos_size_t *size, int nr)
{
	daos_sg_list_t *sgl = req->sgl;
	int i;

	assert_in_range(nr, 1, IOREQ_SG_VD_NR);
	for (i = 0; i < nr; i++) {
		sgl[i].sg_nr.num = 1;
		daos_iov_set(&sgl[i].sg_iovs[0], value[i], size[i]);
	}
}

static void
ioreq_iod_simple_set(struct ioreq *req, daos_size_t *size,
		     uint64_t *idx, daos_epoch_t *epoch, int nr)
{
	daos_vec_iod_t *vio = req->vio;
	int i;

	assert_in_range(nr, 1, IOREQ_SG_VD_NR);
	for (i = 0; i < nr; i++) {
		/** record extent */
		vio[i].vd_recxs[0].rx_rsize = size[i];
		vio[i].vd_recxs[0].rx_idx = idx[i] + i * SEGMENT_SIZE;
		vio[i].vd_recxs[0].rx_nr = 1;

		/** XXX: to be fixed */
		vio[i].vd_eprs[0].epr_lo = *epoch;
		vio[i].vd_nr = 1;
	}
}

static void
insert(const char *dkey, int nr, const char **akey, uint64_t *idx,
       void **val, daos_size_t *size, daos_epoch_t *epoch, struct ioreq *req)
{
	assert_in_range(nr, 1, IOREQ_SG_VD_NR);

	/* dkey */
	ioreq_dkey_set(req, dkey);

	/* akey */
	ioreq_akey_set(req, akey, nr);

	/* set sgl */
	if (val != NULL)
		ioreq_sgl_simple_set(req, val, size, nr);

	/* set iod */
	ioreq_iod_simple_set(req, size, idx, epoch, nr);

	insert_internal(&req->dkey, nr, val == NULL ? NULL : req->sgl,
			req->vio, *epoch, req);
}

void
insert_single(const char *dkey, const char *akey, uint64_t idx,
	      void *value, daos_size_t size, daos_epoch_t epoch,
	      struct ioreq *req)
{
	insert(dkey, 1, &akey, &idx, &value, &size, &epoch, req);
}

static void
punch(const char *dkey, const char *akey, uint64_t idx,
      daos_epoch_t epoch, struct ioreq *req)
{
	daos_size_t size = 0;

	insert(dkey, 1, &akey, &idx, NULL, &size, &epoch, req);
}

static void
lookup_internal(daos_key_t *dkey, int nr, daos_sg_list_t *sgls,
		daos_vec_iod_t *vds, daos_epoch_t epoch, struct ioreq *req)
{
	bool ev_flag;
	int rc;

	/** execute fetch operation */
	rc = daos_obj_fetch(req->oh, epoch, dkey, nr, vds, sgls,
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

static void
lookup(const char *dkey, int nr, const char **akey, uint64_t *idx,
       daos_size_t *read_size, void **val, daos_size_t *size,
       daos_epoch_t *epoch, struct ioreq *req)
{
	assert_in_range(nr, 1, IOREQ_SG_VD_NR);

	/* dkey */
	ioreq_dkey_set(req, dkey);

	/* akey */
	ioreq_akey_set(req, akey, nr);

	/* set sgl */
	ioreq_sgl_simple_set(req, val, size, nr);

	/* set iod */
	ioreq_iod_simple_set(req, read_size, idx, epoch, nr);

	lookup_internal(&req->dkey, nr, req->sgl, req->vio, *epoch, req);
}

void
lookup_single(const char *dkey, const char *akey, uint64_t idx,
	      void *val, daos_size_t size, daos_epoch_t epoch,
	      struct ioreq *req)
{
	daos_size_t read_size = DAOS_REC_ANY;

	lookup(dkey, 1, &akey, &idx, &read_size, &val, &size, &epoch, req);
}

/** test overwrite in different epoch */
static void
io_epoch_overwrite(void **state)
{
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_size_t	 size;
	char		 ubuf[] = "DAOS";
	char		 fbuf[] = "DAOS";
	int		 i;
	daos_epoch_t	 e = 0;

	/** choose random object */
	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, ((test_arg_t *)*state)->myrank);

	ioreq_init(&req, oid, (test_arg_t *)*state);
	size = strlen(ubuf);

	for (i = 0; i < size; i++)
		insert_single("d", "a", i, &ubuf[i], 1, e, &req);

	for (i = 0; i < size; i++) {
		e++;
		ubuf[i] += 32;
		insert_single("d", "a", i, &ubuf[i], 1, e, &req);
	}

	memset(fbuf, 0, sizeof(fbuf));
	for (;;) {
		for (i = 0; i < size; i++)
			lookup_single("d", "a", i, &fbuf[i], 1, e, &req);
		print_message("e = %lu, fbuf = %s\n", e, fbuf);
		assert_string_equal(fbuf, ubuf);
		if (e == 0)
			break;
		e--;
		ubuf[e] -= 32;
	}

	ioreq_fini(&req);
}

/** i/o to variable idx offset */
static void
io_var_idx_offset(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_off_t	 offset;

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	ioreq_init(&req, oid, arg);

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
		assert_int_equal(req.rex[0][0].rx_rsize, strlen(buf) + 1);

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
	const int	 max_size = 1 << 10;
	char		*key;

	/** akey not supported yet */
	skip();

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	ioreq_init(&req, oid, arg);

	key = malloc(max_size + 1);
	assert_non_null(key);
	memset(key, 'a', max_size);

	for (size = 1; size <= max_size; size <<= 1) {
		char buf[10];

		print_message("akey size: %lu\n", size);

		/** Insert */
		key[size] = '\0';
		insert_single("var_akey_size_d", key, 0, "data",
			      strlen("data") + 1, 0, &req);

		/** Lookup */
		memset(buf, 0, 10);
		lookup_single("var_dkey_size_d", key, 0, buf,
			      10, 0, &req);
		assert_int_equal(req.rex[0][0].rx_rsize, strlen("data") + 1);

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
	const int	 max_size = 1 << 10;
	char		*key;

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	ioreq_init(&req, oid, arg);

	key = malloc(max_size + 1);
	assert_non_null(key);
	memset(key, 'a', max_size);

	for (size = 1; size <= max_size; size <<= 1) {
		char buf[10];

		print_message("dkey size: %lu\n", size);

		/** Insert */
		key[size] = '\0';
		insert_single(key, "var_dkey_size_a", 0, "data",
			      strlen("data") + 1, 0, &req);

		/** Lookup */
		memset(buf, 0, 10);
		lookup_single(key, "var_dkey_size_a", 0, buf, 10, 0, &req);
		assert_int_equal(req.rex[0][0].rx_rsize, strlen("data") + 1);

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
	const int	 max_size = 1 << 22;
	char		*fetch_buf;
	char		*update_buf;

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	/** random epoch as well */
	epoch = rand();

	ioreq_init(&req, oid, arg);

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
		assert_int_equal(req.rex[0][0].rx_rsize, size);

		/** Verify data consistency */
		assert_memory_equal(update_buf, fetch_buf, size);
	}

	free(update_buf);
	free(fetch_buf);
	ioreq_fini(&req);
}

static void
io_simple_internal(void **state, daos_obj_id_t oid)
{
	test_arg_t	*arg = *state;
	struct ioreq	 req;
	const char	 dkey[] = "test_update dkey";
	const char	 akey[] = "test_update akey";
	const char	 rec[]  = "test_update record";
	char		*buf;

	ioreq_init(&req, oid, arg);

	/** Insert */
	print_message("Insert(e=0)/lookup(e=0)/verify simple kv record\n");

	insert_single(dkey, akey, 0, (void *)rec, strlen(rec), 0, &req);

	/** Lookup */
	buf = calloc(64, 1);
	assert_non_null(buf);
	lookup_single(dkey, akey, 0, buf, 64, 0, &req);

	/** Verify data consistency */
	print_message("size = %lu\n", req.rex[0][0].rx_rsize);
	assert_int_equal(req.rex[0][0].rx_rsize, strlen(rec));
	assert_memory_equal(buf, rec, strlen(rec));
	free(buf);
	ioreq_fini(&req);
}

/** very basic update/fetch with data verification */
static void
io_simple(void **state)
{
	daos_obj_id_t	 oid;

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, ((test_arg_t *)state)->myrank);
	io_simple_internal(state, oid);
}

void
enumerate_dkey(daos_epoch_t epoch, uint32_t *number, daos_key_desc_t *kds,
	       daos_hash_out_t *anchor, void *buf, daos_size_t len,
	       struct ioreq *req)
{
	int rc;

	ioreq_sgl_simple_set(req, &buf, &len, 1);
	/** execute fetch operation */
	rc = daos_obj_list_dkey(req->oh, epoch, number, kds, req->sgl, anchor,
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

static void
enumerate_akey(daos_epoch_t epoch, char *dkey, uint32_t *number,
	       daos_key_desc_t *kds, daos_hash_out_t *anchor, void *buf,
	       daos_size_t len, struct ioreq *req)
{
	int rc;

	ioreq_sgl_simple_set(req, &buf, &len, 1);
	ioreq_dkey_set(req, dkey);
	/** execute fetch operation */
	rc = daos_obj_list_akey(req->oh, epoch, &req->dkey, number, kds,
				req->sgl, anchor,
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

#define ENUM_KEY_BUF	32
#define ENUM_KEY_NR	1000

#define ENUM_DESC_BUF	512
#define ENUM_DESC_NR	5

/** very basic enumerate */
static void
enumerate_simple(void **state)
{
	char		*buf;
	char		*ptr;
	char		 key[ENUM_KEY_BUF];
	daos_key_desc_t  kds[ENUM_DESC_NR];
	daos_hash_out_t  hash_out;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	uint32_t	 number;
	int		 key_nr;
	int		 i;

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, ((test_arg_t *)*state)->myrank);
	ioreq_init(&req, oid, (test_arg_t *)*state);

	/** Insert record*/
	print_message("Insert %d kv record in object "DF_OID"\n", ENUM_KEY_NR,
		      DP_OID(oid));
	for (i = 0; i < ENUM_KEY_NR; i++) {
		sprintf(key, "%d", i);
		insert_single(key, "a_key", 0, "data",
			      strlen("data") + 1, 0, &req);
	}

	print_message("Enumerate records\n");
	buf = malloc(ENUM_DESC_BUF);

	memset(&hash_out, 0, sizeof(hash_out));
	/** enumerate records */
	for (number = ENUM_DESC_NR, key_nr = 0; !daos_hash_is_eof(&hash_out);
	     number = ENUM_DESC_NR) {
		memset(buf, 0, ENUM_DESC_BUF);
		enumerate_dkey(0, &number, kds, &hash_out, buf, ENUM_DESC_BUF,
			       &req);
		if (number == 0)
			continue; /* loop should break for EOF */

		key_nr += number;
		for (ptr = buf, i = 0; i < number; i++) {
			snprintf(key, kds[i].kd_key_len + 1, ptr);
			print_message("i %d key %s len %d\n", i, key,
				      (int)kds[i].kd_key_len);
			ptr += kds[i].kd_key_len;
		}
	}
	assert_int_equal(key_nr, ENUM_KEY_NR);

	print_message("Insert %d kv record\n", ENUM_KEY_NR);
	for (i = 0; i < ENUM_KEY_NR; i++) {
		sprintf(key, "%d", i);
		insert_single("d_key", key, 0, "data",
			      strlen("data") + 1, 0, &req);
	}

	memset(&hash_out, 0, sizeof(hash_out));
	/** enumerate records */
	for (number = ENUM_DESC_NR, key_nr = 0; !daos_hash_is_eof(&hash_out);
	     number = ENUM_DESC_NR) {
		memset(buf, 0, ENUM_DESC_BUF);
		enumerate_akey(0, "d_key", &number, kds, &hash_out,
			       buf, ENUM_DESC_BUF, &req);
		if (number == 0)
			continue; /* loop should break for EOF */

		key_nr += number;
		for (ptr = buf, i = 0; i < number; i++) {
			snprintf(key, kds[i].kd_key_len + 1, ptr);
			print_message("i %d key %s len %d\n", i, key,
				     (int)kds[i].kd_key_len);
			ptr += kds[i].kd_key_len;
		}
	}

	free(buf);
	/** XXX Verify kds */
	ioreq_fini(&req);
	assert_int_equal(key_nr, ENUM_KEY_NR);
}

/** basic punch test */
static void
punch_simple(void **state)
{
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	uint32_t	number = 2;
	daos_key_desc_t kds[2];
	daos_hash_out_t hash_out;
	char		*buf;
	int		total_keys = 0;

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, ((test_arg_t *)*state)->myrank);
	ioreq_init(&req, oid, (test_arg_t *)*state);

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

	memset(&hash_out, 0, sizeof(hash_out));
	buf = calloc(512, 1);
	/** enumerate records */
	print_message("Enumerate records\n");
	while (number > 0) {
		enumerate_dkey(0, &number, kds, &hash_out, buf, 512, &req);
		total_keys += number;
		if (daos_hash_is_eof(&hash_out))
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

	memset(&hash_out, 0, sizeof(hash_out));
	/** enumerate records */
	print_message("Enumerate records again\n");
	while (number > 0) {
		enumerate_dkey(0, &number, kds, &hash_out, buf, 512, &req);
		total_keys += number;
		if (daos_hash_is_eof(&hash_out))
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
	daos_off_t	offset[5];
	char		*val[5];
	daos_size_t	val_size[5];
	daos_epoch_t	epoch = 0;
	int		i;

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	ioreq_init(&req, oid, arg);

	print_message("Insert(e=0)/lookup(e=0)/verify complex kv record\n");
	for (i = 0; i < 5; i++) {
		akey[i] = calloc(20, 1);
		assert_non_null(akey[i]);
		sprintf(akey[i], "test_update akey%d", i);
		rec[i] = calloc(20, 1);
		assert_non_null(rec[i]);
		sprintf(rec[i], "test_update val%d", i);
		rec_size[i] = strlen(rec[i]);
		offset[i] = i * 20;
		val[i] = calloc(64, 1);
		assert_non_null(val[i]);
		val_size[i] = 64;
	}

	/** Insert */
	insert(dkey, 5, (const char **)akey, offset, (void **)rec,
	       rec_size, &epoch, &req);

	/** Lookup */
	lookup(dkey, 5, (const char **)akey, offset, rec_size,
	       (void **)val, val_size, &epoch, &req);

	/** Verify data consistency */
	for (i = 0; i < 5; i++) {
		print_message("size = %lu\n", req.rex[i][0].rx_rsize);
		assert_int_equal(req.rex[i][0].rx_rsize, strlen(rec[i]));
		assert_memory_equal(val[i], rec[i], strlen(rec[i]));
		free(val[i]);
		free(akey[i]);
		free(rec[i]);
	}
	ioreq_fini(&req);
}

#define STACK_BUF_LEN	24

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
	daos_vec_iod_t	 iod;
	daos_recx_t	 recx[STACK_BUF_LEN];
	char		 buf_out[STACK_BUF_LEN];
	char		 buf[STACK_BUF_LEN];
	int		 i;
	int		 rc;

	dts_buf_render(buf, STACK_BUF_LEN);

	/** open object */
	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, 0, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	/** init dkey */
	daos_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	daos_iov_set(&sg_iov, buf, sizeof(buf));
	sgl.sg_nr.num		= 1;
	sgl.sg_nr.num_out	= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	daos_iov_set(&iod.vd_name, "akey", strlen("akey"));
	daos_csum_set(&iod.vd_kcsum, NULL, 0);
	for (i = 0; i < STACK_BUF_LEN; i++) {
		recx[i].rx_rsize = 1;
		recx[i].rx_idx	 = i;
		recx[i].rx_nr	 = 1;
	}
	iod.vd_nr	= STACK_BUF_LEN;
	iod.vd_recxs	= recx;
	iod.vd_eprs	= NULL;
	iod.vd_csums	= NULL;

	/** update record */
	print_message("writing %d bytes with one recx per byte\n",
		      STACK_BUF_LEN);
	rc = daos_obj_update(oh, epoch, &dkey, 1, &iod, &sgl, NULL);
	assert_int_equal(rc, 0);

	/** fetch */
	print_message("reading data back ...\n");
	memset(buf_out, 0, sizeof(buf_out));
	daos_iov_set(&sg_iov, buf_out, sizeof(buf_out));
	rc = daos_obj_fetch(oh, epoch, &dkey, 1, &iod, &sgl, NULL, NULL);
	assert_int_equal(rc, 0);
	/** Verify data consistency */
	print_message("validating data ...\n");
	assert_memory_equal(buf, buf_out, sizeof(buf));

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_int_equal(rc, 0);
	print_message("all good\n");
}

static void
fetch_size(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_handle_t	 oh;
	daos_epoch_t	 epoch = 2;
	daos_iov_t	 dkey;
	daos_sg_list_t	 sgl;
	daos_iov_t	 sg_iov;
	daos_vec_iod_t	 iod;
	daos_recx_t	 recx;
	char		*buf;
	int		 rc;
	int		 size = 131071;

	buf = malloc(size);
	assert_non_null(buf);

	dts_buf_render(buf, size);

	/** open object */
	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	rc = daos_obj_open(arg->coh, oid, 0, 0, &oh, NULL);
	assert_int_equal(rc, 0);

	/** init dkey */
	daos_iov_set(&dkey, "dkey", strlen("dkey"));

	/** init scatter/gather */
	daos_iov_set(&sg_iov, buf, size);
	sgl.sg_nr.num		= 1;
	sgl.sg_nr.num_out	= 0;
	sgl.sg_iovs		= &sg_iov;

	/** init I/O descriptor */
	daos_iov_set(&iod.vd_name, "akey", strlen("akey"));
	daos_csum_set(&iod.vd_kcsum, NULL, 0);
	iod.vd_nr	= 1;
	recx.rx_rsize	= size;
	recx.rx_idx	= 0;
	recx.rx_nr	= 1;
	iod.vd_recxs	= &recx;
	iod.vd_eprs	= NULL;
	iod.vd_csums	= NULL;

	/** update record */
	rc = daos_obj_update(oh, epoch, &dkey, 1, &iod, &sgl, NULL);
	assert_int_equal(rc, 0);

	/** fetch record size */
	recx.rx_rsize	= DAOS_REC_ANY;
	rc = daos_obj_fetch(oh, epoch, &dkey, 1, &iod, NULL, NULL, NULL);
	assert_int_equal(rc, 0);
	assert_int_equal(recx.rx_rsize, size);

	/** close object */
	rc = daos_obj_close(oh, NULL);
	assert_int_equal(rc, 0);

	free(buf);
}

static void
io_simple_update_timeout(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	arg->fail_loc = DAOS_SHARD_OBJ_UPDATE_TIMEOUT | DAOS_FAIL_SOME;
	arg->fail_value = 5;

	oid = dts_oid_gen(DAOS_OC_LARGE_RW, arg->myrank);
	io_simple_internal(state, oid);
}

static void
io_simple_fetch_timeout(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	arg->fail_loc = DAOS_SHARD_OBJ_FETCH_TIMEOUT | DAOS_FAIL_SOME;
	arg->fail_value = 5;

	oid = dts_oid_gen(DAOS_OC_LARGE_RW, arg->myrank);
	io_simple_internal(state, oid);
}

static void
io_simple_update_crt_error(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	arg->fail_loc = DAOS_SHARD_OBJ_RW_CRT_ERROR | DAOS_FAIL_ONCE;

	oid = dts_oid_gen(DAOS_OC_LARGE_RW, arg->myrank);
	io_simple_internal(state, oid);
}

static void
io_simple_update_crt_req_error(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;

	arg->fail_loc = DAOS_OBJ_REQ_CREATE_TIMEOUT | DAOS_FAIL_ONCE;

	oid = dts_oid_gen(DAOS_OC_LARGE_RW, arg->myrank);
	io_simple_internal(state, oid);
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
		rc = daos_cont_open(arg->poh, arg->co_uuid, DAOS_COO_RW,
				    &arg->coh, &arg->co_info, NULL /* ev */);
		assert_int_equal(rc, 0);
	}
	handle_share(&arg->coh, HANDLE_CO, arg->myrank, arg->poh, 1);

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

	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);
	ioreq_init(&req, oid, arg);

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
			print_message("  a-key[%d] '%s' val '%.*s'\n", i,
				      akey[i], (int)rec_size[i], rec[i]);
		}
		insert(dkey, nakeys, (const char **)akey, offset, (void **)rec,
		       rec_size, &e, &req);
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
		       rec_size, (void **)val, val_size, &e, &req);
		for (i = 0; i < nakeys; i++) {
			if (e == epoch + 1)	/* discarded */
				sprintf(rec_verify, val_fmt, i, e - 1);
			else			/* intact */
				sprintf(rec_verify, val_fmt, i, e);
			assert_int_equal(req.rex[i][0].rx_rsize,
					 strlen(rec_verify));
			print_message("  a-key[%d] '%s' val '%.*s'\n", i,
				      akey[i], (int)req.rex[i][0].rx_rsize,
				      val[i]);
			assert_memory_equal(val[i], rec_verify,
					    req.rex[i][0].rx_rsize);
		}
	}
	free(rec_verify);

	/** Close and reopen the container and the obj. */
	MPI_Barrier(MPI_COMM_WORLD);
	close_reopen_coh_oh(arg, &req, oid);

	/** Verify that the three epochs are empty. */
	for (e = epoch; e < epoch + 3; e++) {
		daos_hash_out_t	hash_out;
		int		found = 0;

		print_message("verifying epoch "DF_U64"\n", e);
		memset(&hash_out, 0, sizeof(hash_out));
		while (!daos_hash_is_eof(&hash_out)) {
			uint32_t		n = 1;
			daos_key_desc_t		kd;
			char			*buf[64];

			enumerate_dkey(e, &n, &kd, &hash_out, buf, sizeof(buf),
				       &req);
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
	oid = dts_oid_gen(DAOS_OC_REPL_MAX_RW, arg->myrank);

	large_buf = malloc(buf_size);
	assert_non_null(large_buf);
	arg->fail_loc = DAOS_OBJ_UPDATE_NOSPACE;
	ioreq_init(&req, oid, arg);
	for (i = 0; i < 5; i++) {
		sprintf(key, "dkey%d", i);
		/** Insert */
		arg->expect_result = -DER_NOSPACE;
		insert_single(key, "akey", 0, "data",
			      strlen("data") + 1, 0, &req);
		insert_single(key, "akey", 0, large_buf,
			      buf_size, 0, &req);
	}
	ioreq_fini(&req);
}

static const struct CMUnitTest io_tests[] = {
	{ "IO1: simple update/fetch/verify",
	  io_simple, async_disable, NULL},
	{ "IO2: simple update/fetch/verify (async)",
	  io_simple, async_enable, NULL},
	{ "IO3: i/o with variable rec size",
	  io_var_rec_size, async_disable, NULL},
	{ "IO4: i/o with variable rec size(async)",
	  io_var_rec_size, async_enable, NULL},
	{ "IO5: i/o with variable dkey size",
	  io_var_dkey_size, async_enable, NULL},
	{ "IO6: i/o with variable akey size",
	  io_var_akey_size, async_disable, NULL},
	{ "IO7: i/o with variable index",
	  io_var_idx_offset, async_enable, NULL},
	{ "IO8: overwrite in different epoch",
	  io_epoch_overwrite, async_enable, NULL},
	{ "IO9: simple enumerate", enumerate_simple,
	  async_disable, NULL},
	{ "IO10: simple punch", punch_simple,
	  async_disable, NULL},
	{ "IO11: complex update/fetch/verify", io_complex,
	  async_disable, NULL},
	{ "IO12: basic byte array with record size fetching",
	  basic_byte_array, async_disable, NULL},
	{ "IO13: timeout simple update (async)",
	  io_simple_update_timeout, async_disable, NULL},
	{ "IO14: timeout simple fetch (async)",
	  io_simple_fetch_timeout, async_disable, NULL},
	{ "IO15: epoch discard", epoch_discard,
	  async_disable, NULL},
	{ "IO16: no space", io_nospace, async_disable, NULL},
	{ "IO17: fetch size with NULL sgl", fetch_size, async_disable, NULL},
	{ "IO18: io crt error", io_simple_update_crt_error,
	  async_disable, NULL},
	{ "IO19: io crt error (async)", io_simple_update_crt_error,
	  async_enable, NULL},
	{ "IO20: io crt req create timeout (sync)",
	  io_simple_update_crt_req_error, async_disable, NULL},
	{ "IO21: io crt req create timeout (async)",
	  io_simple_update_crt_req_error, async_enable, NULL},
};

int
obj_setup(void **state)
{
	return test_setup(state, SETUP_CONT_CONNECT, true);
}

int
run_daos_io_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("DAOS I/O tests", io_tests,
					 obj_setup, test_teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
