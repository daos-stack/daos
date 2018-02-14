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
 * vos/tests/vts_io.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venaktesan@intel.com>
 */
#define DDSUBSYS	DDFAC(tests)

#include <vts_io.h>

/* key generator */
static unsigned int		vts_key_gen;
/** epoch generator */
static daos_epoch_t		vts_epoch_gen;

/** Create dictionary of unique cookies */
static uuid_t			cookie_dict[NUM_UNIQUE_COOKIES];


static struct vts_counter	vts_cntr;

/**
 * Stores the last key and can be used for
 * punching or overwrite
 */
char		last_dkey[UPDATE_DKEY_SIZE];
char		last_akey[UPDATE_AKEY_SIZE];

struct io_test_flag {
	char			*tf_str;
	unsigned int		 tf_bits;
};

static struct io_test_flag io_test_flags[] = {
	{
		.tf_str		= "default",
		.tf_bits	= 0,
	},
	{
		.tf_str		= "ZC",
		.tf_bits	= TF_ZERO_COPY,
	},
	{
		.tf_str		= "extent",
		.tf_bits	= TF_REC_EXT,
	},
	{
		.tf_str		= "ZC + extent",
		.tf_bits	= TF_ZERO_COPY | TF_REC_EXT,
	},
	{
		.tf_str		= NULL,
	},
};

/**
 * Stores the last cookie ID to verify
 * while updating
 */
uuid_t	last_cookie;

daos_epoch_t
gen_rand_epoch(void)
{
	vts_epoch_gen += rand() % 100;
	return vts_epoch_gen;
}

struct daos_uuid
gen_rand_cookie(void)
{
	struct daos_uuid	uuid_val;
	int			i;

	i = rand() % NUM_UNIQUE_COOKIES;
	uuid_copy(last_cookie, cookie_dict[i]);
	uuid_copy(uuid_val.uuid, cookie_dict[i]);

	return uuid_val;
}

bool
is_found(uuid_t cookie)
{
	int i;

	for (i = 0; i < NUM_UNIQUE_COOKIES; i++)
		if (!uuid_compare(cookie, cookie_dict[i]))
			return true;
	return false;
}

daos_unit_oid_t
gen_oid(void)
{
	vts_cntr.cn_oids++;
	return dts_unit_oid_gen(0, 0);
}

void
inc_cntr(unsigned long op_flags)
{
	if (op_flags & TF_OVERWRITE) {
		vts_cntr.cn_punch++;
	} else {
		vts_cntr.cn_dkeys++;
		if (op_flags & TF_FIXED_AKEY)
			vts_cntr.cn_fa_dkeys++;
	}
}

void
inc_cntr_manual(unsigned long op_flags, struct vts_counter *cntrs)
{
	if (op_flags & TF_OVERWRITE) {
		cntrs->cn_punch++;
	} else {
		cntrs->cn_dkeys++;
		if (op_flags & TF_FIXED_AKEY)
			cntrs->cn_fa_dkeys++;
	}
}

void
test_args_init(struct io_test_args *args, uint64_t pool_size)
{
	int	rc, i;

	memset(args, 0, sizeof(*args));
	memset(&vts_cntr, 0, sizeof(vts_cntr));

	vts_key_gen = 0;
	vts_epoch_gen = 1;

	for (i = 0; i < NUM_UNIQUE_COOKIES; i++)
		uuid_generate_random(cookie_dict[i]);

	rc = vts_ctx_init(&args->ctx, pool_size);
	assert_int_equal(rc, 0);
	args->oid = gen_oid();
	args->cookie_flag = false;
}

void
test_args_reset(struct io_test_args *args, uint64_t pool_size)
{
	vts_ctx_fini(&args->ctx);
	test_args_init(args, pool_size);
}

static struct io_test_args	test_args;

int
setup_io(void **state)
{
	srand(10);
	test_args_init(&test_args, VPOOL_SIZE);

	*state = &test_args;
	return 0;
}

int
teardown_io(void **state)
{
	struct io_test_args *arg = *state;

	assert_ptr_equal(arg, &test_args);
	vts_ctx_fini(&arg->ctx);
	return 0;
}

static int
io_recx_iterate(struct io_test_args *arg, vos_iter_param_t *param,
		daos_key_t *akey, int akey_id, int *recs, bool print_ent)
{
	daos_handle_t	ih;
	int		itype;
	int		nr = 0;
	int		rc;

	param->ip_akey = *akey;
	if (arg->ta_flags & TF_REC_EXT)
		itype = VOS_ITER_RECX;
	else
		itype = VOS_ITER_SINGLE;

	rc = vos_iter_prepare(itype, param, &ih);
	if (rc != 0) {
		print_error("Failed to create recx iterator: %d\n", rc);
		goto out;
	}

	rc = vos_iter_probe(ih, NULL);
	if (rc != 0 && rc != -DER_NONEXIST) {
		print_error("Failed to set iterator cursor: %d\n", rc);
		goto out;
	}

	while (rc == 0) {
		vos_iter_entry_t  ent;

		memset(&ent, 0, sizeof(ent));
		rc = vos_iter_fetch(ih, &ent, NULL);
		if (rc != 0) {
			print_error("Failed to fetch recx: %d\n", rc);
			goto out;
		}

		if (arg->cookie_flag) {
			assert_true(is_found(ent.ie_cookie));
			if (print_ent)
				D__PRINT("Cookie : %s\n",
					DP_UUID(ent.ie_cookie));
		}

		nr++;
		if (print_ent) {
			if (nr == 1) {
				D__PRINT("akey[%d]: %s\n", akey_id,
					(char *)param->ip_akey.iov_buf);
			}

			D__PRINT("\trecx %u : %s\n",
				(unsigned int)ent.ie_recx.rx_idx,
				ent.ie_iov.iov_len == 0 ?
				"[NULL]" : (char *)ent.ie_iov.iov_buf);
			D__PRINT("\tepoch: "DF_U64"\n",
				ent.ie_epr.epr_lo);
		}

		rc = vos_iter_next(ih);
		if (rc != 0 && rc != -DER_NONEXIST) {
			print_error("Failed to move cursor: %d\n", rc);
			goto out;
		}
	}
	rc = 0;
out:
	vos_iter_finish(ih);
	*recs += nr;
	return rc;
}

static int
io_akey_iterate(struct io_test_args *arg, vos_iter_param_t *param,
		daos_key_t *dkey, int dkey_id, int *akeys, int *recs,
		bool print_ent)
{
	daos_handle_t	ih;
	int		nr = 0;
	int		rc;

	param->ip_dkey = *dkey;
	rc = vos_iter_prepare(VOS_ITER_AKEY, param, &ih);
	if (rc != 0) {
		print_error("Failed to create akey iterator: %d\n", rc);
		goto out;
	}

	rc = vos_iter_probe(ih, NULL);
	if (rc != 0 && rc != -DER_NONEXIST) {
		print_error("Failed to set iterator cursor: %d\n", rc);
		goto out;
	}

	while (rc == 0) {
		vos_iter_entry_t  ent;

		rc = vos_iter_fetch(ih, &ent, NULL);
		if (rc != 0) {
			print_error("Failed to fetch akey: %d\n", rc);
			goto out;
		}

		if (print_ent && nr == 0) {
			D__PRINT("dkey[%d]: %s\n", dkey_id,
				(char *)param->ip_dkey.iov_buf);
		}

		rc = io_recx_iterate(arg, param, &ent.ie_key, nr,
				     recs, print_ent);

		nr++;
		rc = vos_iter_next(ih);
		if (rc != 0 && rc != -DER_NONEXIST) {
			print_error("Failed to move cursor: %d\n", rc);
			goto out;
		}
	}
	rc = 0;
out:
	vos_iter_finish(ih);
	*akeys += nr;
	return rc;
}

static int
io_obj_iter_test(struct io_test_args *arg, daos_epoch_range_t *epr,
		 vos_it_epc_expr_t expr,
		 int *num_dkeys, int *num_akeys, int *num_recs,
		 bool print_ent)
{
	char			buf[UPDATE_AKEY_SIZE];
	vos_iter_param_t	param;
	daos_handle_t		ih;
	bool			iter_fa;
	int			nr = 0;
	int			akeys = 0, recs = 0;
	int			rc;

	iter_fa = (arg->ta_flags & TF_FIXED_AKEY);

	memset(&param, 0, sizeof(param));
	param.ip_hdl		= arg->ctx.tc_co_hdl;
	param.ip_oid		= arg->oid;
	param.ip_epr		= *epr;
	param.ip_epc_expr	= expr;

	if (iter_fa) {
		strcpy(&buf[0], UPDATE_AKEY_FIXED);
		daos_iov_set(&param.ip_akey, &buf[0], strlen(buf));
	}

	rc = vos_iter_prepare(VOS_ITER_DKEY, &param, &ih);
	if (rc != 0) {
		print_error("Failed to prepare d-key iterator\n");
		return rc;
	}

	rc = vos_iter_probe(ih, NULL);
	if (rc != 0) {
		print_error("Failed to set iterator cursor: %d\n",
			    rc);
		goto out;
	}

	while (1) {
		vos_iter_entry_t  ent;
		daos_hash_out_t	  anchor;

		rc = vos_iter_fetch(ih, &ent, NULL);
		if (rc == -DER_NONEXIST) {
			print_message("Finishing d-key iteration\n");
			break;
		}

		if (rc != 0) {
			print_error("Failed to fetch dkey: %d\n", rc);
			goto out;
		}

		rc = io_akey_iterate(arg, &param, &ent.ie_key, nr,
				     &akeys, &recs, print_ent);
		if (rc != 0)
			goto out;

		nr++;
		rc = vos_iter_next(ih);
		if (rc == -DER_NONEXIST)
			break;

		if (rc != 0) {
			print_error("Failed to move cursor: %d\n", rc);
			goto out;
		}

		if (!(arg->ta_flags & TF_IT_ANCHOR))
			continue;

		rc = vos_iter_fetch(ih, &ent, &anchor);
		if (rc != 0) {
			assert_true(rc != -DER_NONEXIST);
			print_error("Failed to fetch anchor: %d\n",
				    rc);
			goto out;
		}

		rc = vos_iter_probe(ih, &anchor);
		if (rc != 0) {
			assert_true(rc != -DER_NONEXIST);
			print_error("Failed to probe anchor: %d\n",
				    rc);
			goto out;
		}
	}
 out:
	vos_iter_finish(ih);
	*num_dkeys = nr;
	*num_akeys = akeys;
	*num_recs  = recs;
	return rc;
}

int
io_test_obj_update(struct io_test_args *arg, int epoch, daos_key_t *dkey,
		   daos_iod_t *iod, daos_sg_list_t *sgl,
		   struct daos_uuid *dsm_cookie, bool verbose)
{
	daos_sg_list_t		*iod_sgl;
	daos_iov_t		*iod_iov;
	daos_iov_t		*srv_iov;
	daos_handle_t		ioh;
	unsigned int		off;
	int			i;
	int			rc;

	if (!(arg->ta_flags & TF_ZERO_COPY)) {
		rc = vos_obj_update(arg->ctx.tc_co_hdl, arg->oid, epoch,
				    dsm_cookie->uuid, 0, dkey, 1, iod,
				    sgl);
		if (rc != 0 && verbose)
			print_error("Failed to update: %d\n", rc);
		return rc;
	}

	rc = vos_obj_zc_update_begin(arg->ctx.tc_co_hdl,
				     arg->oid, epoch, dkey, 1, iod,
				     &ioh);
	if (rc != 0) {
		if (verbose)
			print_error("Failed to prepare ZC update: %d\n", rc);
		return rc;
	}

	srv_iov = &sgl->sg_iovs[0];

	rc = vos_obj_zc_sgl_at(ioh, 0, &iod_sgl);
	assert_int_equal(rc, 0);

	for (i = off = 0; i < iod_sgl->sg_nr_out; i++) {
		iod_iov = &iod_sgl->sg_iovs[i];
		memcpy(iod_iov->iov_buf, srv_iov->iov_buf + off,
		       iod_iov->iov_len);
		off += iod_iov->iov_len;
	}
	assert_true(srv_iov->iov_len == off);

	rc = vos_obj_zc_update_end(ioh, dsm_cookie->uuid, 0, dkey, 1, iod, 0);
	if (rc != 0 && verbose)
		print_error("Failed to submit ZC update: %d\n", rc);

	return rc;
}

int
io_test_obj_fetch(struct io_test_args *arg, int epoch, daos_key_t *dkey,
		  daos_iod_t *iod, daos_sg_list_t *sgl, bool verbose)
{
	daos_sg_list_t	*iod_sgl;
	daos_iov_t	*iod_iov;
	daos_iov_t	*dst_iov;
	daos_handle_t	 ioh;
	unsigned int	 off;
	int		 i;
	int		 rc;

	if (!(arg->ta_flags & TF_ZERO_COPY)) {
		rc = vos_obj_fetch(arg->ctx.tc_co_hdl,
				   arg->oid, epoch, dkey, 1, iod,
				   sgl);
		if (rc != 0 && verbose)
			print_error("Failed to fetch: %d\n", rc);
		return rc;
	}

	rc = vos_obj_zc_fetch_begin(arg->ctx.tc_co_hdl,
				    arg->oid, epoch, dkey, 1, iod,
				    &ioh);
	if (rc != 0) {
		if (verbose)
			print_error("Failed to prepare ZC update: %d\n", rc);
		return rc;
	}

	dst_iov = &sgl->sg_iovs[0];

	vos_obj_zc_sgl_at(ioh, 0, &iod_sgl);

	for (i = off = 0; i < iod_sgl->sg_nr_out; i++) {
		iod_iov = &iod_sgl->sg_iovs[i];
		memcpy(dst_iov->iov_buf + off, iod_iov->iov_buf,
		       iod_iov->iov_len);
		off += iod_iov->iov_len;
	}
	dst_iov->iov_len = off;
	assert_true(dst_iov->iov_buf_len >= dst_iov->iov_len);

	rc = vos_obj_zc_fetch_end(ioh, dkey, 1, iod, 0);
	if (rc != 0 && verbose)
		print_error("Failed to submit ZC update: %d\n", rc);

	return rc;
}

static int
io_update_and_fetch_dkey(struct io_test_args *arg, daos_epoch_t update_epoch,
			 daos_epoch_t fetch_epoch)
{

	int			rc = 0;
	daos_iov_t		val_iov;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		rex;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	char			update_buf[UPDATE_BUF_SIZE];
	char			fetch_buf[UPDATE_BUF_SIZE];
	daos_iod_t		iod;
	daos_sg_list_t		sgl;
	struct daos_uuid	dsm_cookie;
	unsigned int		recx_size;
	unsigned int		recx_nr;

	memset(&iod, 0, sizeof(iod));
	memset(&rex, 0, sizeof(rex));
	memset(&sgl, 0, sizeof(sgl));

	if (arg->ta_flags & TF_REC_EXT) {
		iod.iod_type = DAOS_IOD_ARRAY;
		recx_size = UPDATE_REC_SIZE;
		recx_nr   = UPDATE_BUF_SIZE / UPDATE_REC_SIZE;
	} else {
		iod.iod_type = DAOS_IOD_SINGLE;
		recx_size = UPDATE_BUF_SIZE;
		recx_nr   = 1;
	}

	if (!(arg->ta_flags & TF_PUNCH)) {
		if (arg->ta_flags & TF_OVERWRITE) {
			memcpy(dkey_buf, last_dkey, UPDATE_DKEY_SIZE);
			memcpy(akey_buf, last_akey, UPDATE_DKEY_SIZE);
		} else {
			dts_key_gen(&dkey_buf[0], UPDATE_DKEY_SIZE,
				    UPDATE_DKEY);
			memcpy(last_dkey, dkey_buf, UPDATE_DKEY_SIZE);

			if (arg->ta_flags & TF_FIXED_AKEY) {
				memset(&akey_buf[0], 0, UPDATE_AKEY_SIZE);
				strcpy(&akey_buf[0], UPDATE_AKEY_FIXED);
			} else {
				dts_key_gen(&akey_buf[0], UPDATE_AKEY_SIZE,
					    UPDATE_AKEY);
			}
			memcpy(last_akey, akey_buf, UPDATE_AKEY_SIZE);
		}


		daos_iov_set(&dkey, &dkey_buf[0], strlen(dkey_buf));
		daos_iov_set(&akey, &akey_buf[0], strlen(akey_buf));

		dts_buf_render(update_buf, UPDATE_BUF_SIZE);
		daos_iov_set(&val_iov, &update_buf[0], UPDATE_BUF_SIZE);
		iod.iod_size = recx_size;
		rex.rx_nr    = recx_nr;
	} else {
		daos_iov_set(&dkey, &last_dkey[0], UPDATE_DKEY_SIZE);
		daos_iov_set(&akey, &last_akey[0], UPDATE_AKEY_SIZE);

		memset(update_buf, 0, UPDATE_BUF_SIZE);
		daos_iov_set(&val_iov, &update_buf[0], UPDATE_BUF_SIZE);
		rex.rx_nr    = recx_nr;
		iod.iod_size = 0;
	}

	sgl.sg_nr = 1;
	sgl.sg_iovs = &val_iov;

	rex.rx_idx	= daos_hash_string_u32(dkey_buf, dkey.iov_len);
	rex.rx_idx	%= 1000000;

	iod.iod_name	= akey;
	iod.iod_recxs	= &rex;
	iod.iod_nr	= 1;

	uuid_copy(dsm_cookie.uuid, cookie_dict[(rand() % NUM_UNIQUE_COOKIES)]);

	rc = io_test_obj_update(arg, update_epoch, &dkey, &iod, &sgl,
				&dsm_cookie, true);
	if (rc)
		goto exit;

	inc_cntr(arg->ta_flags);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	daos_iov_set(&val_iov, &fetch_buf[0], UPDATE_BUF_SIZE);

	iod.iod_size = DAOS_REC_ANY;
	rc = io_test_obj_fetch(arg, fetch_epoch, &dkey, &iod, &sgl, true);
	if (rc)
		goto exit;
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

exit:
	return rc;
}

static inline int
hold_objects(struct vos_object **objs, struct daos_lru_cache *occ,
	     daos_handle_t *coh, daos_unit_oid_t *oid, int start, int end)
{
	int i = 0, rc = 0;

	for (i = start; i < end; i++) {
		rc = vos_obj_hold(occ, *coh, *oid, 1, true, &objs[i]);
		assert_int_equal(rc, 0);
	}

	return rc;
}

static void
io_oi_test(void **state)
{
	struct io_test_args	*arg = *state;
	struct vos_obj_df	*obj[2];
	struct vos_container	*cont;
	daos_unit_oid_t		oid;
	int			rc = 0;

	oid = gen_oid();

	cont = vos_hdl2cont(arg->ctx.tc_co_hdl);
	assert_ptr_not_equal(cont, NULL);

	rc = vos_oi_find_alloc(cont, oid, 1, &obj[0]);
	assert_int_equal(rc, 0);

	rc = vos_oi_find_alloc(cont, oid, 1, &obj[1]);
	assert_int_equal(rc, 0);
}

static void
io_obj_cache_test(void **state)
{
	struct io_test_args	*arg = *state;
	struct vos_test_ctx	*ctx = &arg->ctx;
	struct daos_lru_cache	*occ = NULL;
	struct vos_object	*objs[20];
	daos_unit_oid_t		 oids[2];
	int			 i;
	int			 rc;

	rc = vos_obj_cache_create(10, &occ);
	assert_int_equal(rc, 0);

	oids[0] = gen_oid();
	oids[1] = gen_oid();

	rc = hold_objects(objs, occ, &ctx->tc_co_hdl, &oids[0], 0, 10);
	assert_int_equal(rc, 0);

	rc = hold_objects(objs, occ, &ctx->tc_co_hdl, &oids[1], 10, 15);
	assert_int_equal(rc, 0);

	for (i = 0; i < 5; i++)
		vos_obj_release(occ, objs[i]);
	for (i = 10; i < 15; i++)
		vos_obj_release(occ, objs[i]);

	rc = hold_objects(objs, occ, &ctx->tc_co_hdl, &oids[1], 15, 20);
	assert_int_equal(rc, 0);

	for (i = 5; i < 10; i++)
		vos_obj_release(occ, objs[i]);
	for (i = 15; i < 20; i++)
		vos_obj_release(occ, objs[i]);

	vos_obj_cache_destroy(occ);
}

static void
io_multiple_dkey_test(void **state, unsigned int flags)
{
	struct io_test_args	*arg = *state;
	int			 i;
	int			 rc = 0;
	daos_epoch_t		 epoch = gen_rand_epoch();

	arg->ta_flags = flags;
	for (i = 0; i < VTS_IO_KEYS; i++) {
		rc = io_update_and_fetch_dkey(arg, epoch, epoch);
		assert_int_equal(rc, 0);
	}
}

static void
io_multiple_dkey(void **state)
{
	int	i;

	for (i = 0; io_test_flags[i].tf_str != NULL; i++) {
		print_message("\t%d) multi-key update/fetch/verify (%s)\n",
			      i, io_test_flags[i].tf_str);
		io_multiple_dkey_test(state, io_test_flags[i].tf_bits);
	}
}

static void
io_idx_overwrite_test(void **state, unsigned int flags)
{
	struct io_test_args	*arg = *state;
	daos_epoch_t		 epoch = gen_rand_epoch();
	int			 rc = 0;

	arg->ta_flags = flags;
	rc = io_update_and_fetch_dkey(arg, epoch, epoch);
	assert_int_equal(rc, 0);

	arg->ta_flags |= TF_OVERWRITE;
	rc = io_update_and_fetch_dkey(arg, epoch, epoch);
	assert_int_equal(rc, 0);
}

static void
io_idx_overwrite(void **state)
{
	int	i;

	for (i = 0; io_test_flags[i].tf_str != NULL; i++) {
		print_message("\t%d) overwrite (%s)\n",
			      i, io_test_flags[i].tf_str);
		io_idx_overwrite_test(state, io_test_flags[i].tf_bits);
	}
}

static void
io_iter_test(void **state)
{
	struct io_test_args	*arg = *state;
	daos_epoch_range_t	epr;
	int			rc = 0;
	int			nr, akeys, recs;

	arg->ta_flags = TF_REC_EXT;
	epr.epr_lo = vts_epoch_gen + 10;
	epr.epr_hi = DAOS_EPOCH_MAX;

	rc = io_obj_iter_test(arg, &epr, VOS_IT_EPC_GE,
			      &nr, &akeys, &recs, false);
	assert_true(rc == 0 || rc == -DER_NONEXIST);

	/**
	 * Check if enumerated keys is equal to the number of
	 * keys updated
	 */
	print_message("Enumerated: %d, total_keys: %lu.\n",
		      nr, vts_cntr.cn_dkeys);
	print_message("Enumerated akeys: %d\n", akeys);
	assert_int_equal(nr, vts_cntr.cn_dkeys);
}

static void
io_iter_test_with_anchor(void **state)
{
	struct io_test_args	*arg = *state;
	daos_epoch_range_t	epr;
	int			nr, rc = 0;
	int			akeys, recs;

	arg->ta_flags = TF_IT_ANCHOR | TF_REC_EXT;
	epr.epr_lo = vts_epoch_gen + 10;
	epr.epr_hi = DAOS_EPOCH_MAX;
	arg->cookie_flag = false;

	rc = io_obj_iter_test(arg, &epr, VOS_IT_EPC_GE,
			      &nr, &akeys, &recs, false);
	assert_true(rc == 0 || rc == -DER_NONEXIST);

	print_message("Enumerated: %d, total_dkeys: %lu.\n",
		      nr, vts_cntr.cn_dkeys);
	print_message("Enumerated akeys: %d\n", akeys);
	assert_int_equal(nr, vts_cntr.cn_dkeys);
}

#define IOT_FA_DKEYS	100

static void
io_iter_test_dkey_cond(void **state)
{
	struct io_test_args	*arg = *state;
	int			 i;
	int			 nr, rc = 0;
	int			 akeys, recs;
	daos_epoch_range_t	 epr;

	skip();

	arg->ta_flags = TF_FIXED_AKEY;
	arg->cookie_flag = false;
	epr.epr_lo = gen_rand_epoch();
	epr.epr_hi = DAOS_EPOCH_MAX;

	for (i = 0; i < IOT_FA_DKEYS; i++) {
		rc = io_update_and_fetch_dkey(arg, epr.epr_lo,
					      epr.epr_lo);
		assert_int_equal(rc, 0);
	}
	epr.epr_lo += 10;
	rc = io_obj_iter_test(arg, &epr, VOS_IT_EPC_GE,
			      &nr, &akeys, &recs, false);
	assert_true(rc == 0 || rc == -DER_NONEXIST);

	print_message("Enumerated: %d, total_keys: %lu.\n",
		      nr, vts_cntr.cn_fa_dkeys);
	print_message("Enumerated akeys: %d\n", akeys);

	assert_int_equal(nr, vts_cntr.cn_fa_dkeys);
}

#define RANGE_ITER_KEYS 10

static int
io_obj_range_iter_test(struct io_test_args *args, vos_it_epc_expr_t expr)
{
	int			i;
	int			nr, rc;
	int			akeys, recs;
	daos_epoch_range_t	epr;

	test_args_reset(args, VPOOL_SIZE);

	args->ta_flags = 0;
	epr.epr_lo = gen_rand_epoch();
	epr.epr_hi = epr.epr_lo + RANGE_ITER_KEYS * 2 - 1;
	print_message("Updates lo: "DF_U64", hi: "DF_U64"\n",
		      epr.epr_lo, (epr.epr_lo + (RANGE_ITER_KEYS * 4) - 1));
	if (expr == VOS_IT_EPC_RR)
		print_message("Enum range lo:"DF_U64", hi:"DF_U64"\n",
			      epr.epr_hi, epr.epr_lo);
	else
		print_message("Enum range lo:"DF_U64", hi:"DF_U64"\n",
			      epr.epr_lo, epr.epr_hi);

	for (i = 0; i < RANGE_ITER_KEYS * 4; i += 2) {

		args->ta_flags = 0;
		rc = io_update_and_fetch_dkey(args, epr.epr_lo + i,
					      epr.epr_lo + i);
		if (rc != 0)
			return rc;

		args->ta_flags |= TF_OVERWRITE;
		i += 2;
		rc = io_update_and_fetch_dkey(args, epr.epr_lo + i,
					      epr.epr_lo + i);
		if (rc != 0)
			return rc;
	}

	rc = io_obj_iter_test(args, &epr, expr,
			      &nr, &akeys, &recs, true);
	if (rc == -DER_NONEXIST)
		rc = 0;
	if (rc != 0)
		return rc;

	if (recs != RANGE_ITER_KEYS) {
		print_message("Enumerated records: %d, total_records: %d.\n",
			      recs, RANGE_ITER_KEYS);
		rc = -DER_IO_INVAL;
	}

	return rc;
}

static int
io_obj_recx_range_iteration(struct io_test_args *args, vos_it_epc_expr_t expr)
{
	int			i;
	int			nr, rc;
	int			akeys, recs;
	daos_epoch_range_t	epr;
	daos_epoch_t		epoch;
	int			total_in_range = 0;

	test_args_reset(args, VPOOL_SIZE);

	args->ta_flags = 0;
	epoch = gen_rand_epoch();
	epr.epr_lo = epoch + RANGE_ITER_KEYS * 2 - 1;
	epr.epr_hi = epoch + RANGE_ITER_KEYS * 3 - 1;
	print_message("Updates lo: "DF_U64", hi: "DF_U64"\n",
		      epoch, (epoch + (RANGE_ITER_KEYS * 4) - 1));
	if (expr == VOS_IT_EPC_RR)
		print_message("Enum range lo:"DF_U64", hi:"DF_U64"\n",
			      epr.epr_hi, epr.epr_lo);
	else
		print_message("Enum range lo:"DF_U64", hi:"DF_U64"\n",
			      epr.epr_lo, epr.epr_hi);

	args->ta_flags = 0;
	rc = io_update_and_fetch_dkey(args, epr.epr_lo, epr.epr_lo);
	if (rc != 0)
		return rc;

	args->ta_flags |= TF_OVERWRITE;
	for (i = 1; i < RANGE_ITER_KEYS * 4; i++) {
		rc = io_update_and_fetch_dkey(args, epoch + i,
					      epoch + i);
		if (rc != 0)
			return rc;

		if ((epoch + i <= epr.epr_hi) &&
		    (epoch + i >= epr.epr_lo))
			total_in_range++;
	}

	rc = io_obj_iter_test(args, &epr, expr,
			      &nr, &akeys, &recs, true);
	if (rc == -DER_NONEXIST)
		rc = 0;
	if (rc != 0)
		return rc;

	if (recs != total_in_range) {
		print_message("Enumerated records: %d, total_records: %d.\n",
			      recs, total_in_range);
		rc = -DER_IO_INVAL;
	}
	return rc;
}

static void
io_obj_forward_iter_test(void **state)
{

	struct io_test_args	*args = *state;
	int			rc;

	rc = io_obj_range_iter_test(args, VOS_IT_EPC_RE);
	assert_int_equal(rc, 0);
}

static void
io_obj_reverse_iter_test(void **state)
{

	struct io_test_args	*args = *state;
	int			rc;

	rc = io_obj_range_iter_test(args, VOS_IT_EPC_RR);
	assert_int_equal(rc, 0);
}

static void
io_obj_forward_recx_iter_test(void **state)
{

	struct io_test_args	*args = *state;
	int			rc;

	skip();
	rc = io_obj_recx_range_iteration(args, VOS_IT_EPC_RE);
	assert_int_equal(rc, 0);
}


static void
io_obj_reverse_recx_iter_test(void **state)
{

	struct io_test_args	*args = *state;
	int			rc;

	skip();
	rc = io_obj_recx_range_iteration(args, VOS_IT_EPC_RR);
	assert_int_equal(rc, 0);
}


static int
io_update_and_fetch_incorrect_dkey(struct io_test_args *arg,
				   daos_epoch_t update_epoch,
				   daos_epoch_t fetch_epoch)
{

	int			rc = 0;
	daos_iov_t		val_iov;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		rex;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	char			update_buf[UPDATE_BUF_SIZE];
	char			fetch_buf[UPDATE_BUF_SIZE];
	daos_iod_t		iod;
	daos_sg_list_t		sgl;
	struct daos_uuid	dsm_cookie;

	memset(&iod, 0, sizeof(iod));
	memset(&rex, 0, sizeof(rex));
	memset(&sgl, 0, sizeof(sgl));

	dts_key_gen(&dkey_buf[0], UPDATE_DKEY_SIZE, UPDATE_DKEY);
	dts_key_gen(&akey_buf[0], UPDATE_AKEY_SIZE, UPDATE_AKEY);
	memcpy(last_akey, akey_buf, UPDATE_AKEY_SIZE);

	daos_iov_set(&dkey, &dkey_buf[0], strlen(dkey_buf));
	daos_iov_set(&akey, &akey_buf[0], strlen(akey_buf));

	dts_buf_render(update_buf, UPDATE_BUF_SIZE);
	daos_iov_set(&val_iov, &update_buf[0], UPDATE_BUF_SIZE);
	iod.iod_size	= val_iov.iov_len;

	sgl.sg_nr = 1;
	sgl.sg_iovs = &val_iov;

	rex.rx_nr	= 1;
	rex.rx_idx	= daos_hash_string_u32(dkey_buf, dkey.iov_len);
	rex.rx_idx	%= 1000000;

	iod.iod_name	= akey;
	iod.iod_recxs	= &rex;
	iod.iod_nr	= 1;
	iod.iod_type	= DAOS_IOD_ARRAY;

	uuid_copy(dsm_cookie.uuid, cookie_dict[rand() % NUM_UNIQUE_COOKIES]);
	rc = io_test_obj_update(arg, update_epoch, &dkey, &iod, &sgl,
				&dsm_cookie, true);
	if (rc)
		goto exit;

	inc_cntr(arg->ta_flags);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	daos_iov_set(&val_iov, &fetch_buf[0], UPDATE_BUF_SIZE);

	/* will be set to zero after fetching a nonexistent key */
	iod.iod_size = -1;

	/* Injecting an incorrect dkey for fetch! */
	dts_key_gen(&dkey_buf[0], UPDATE_DKEY_SIZE, UPDATE_DKEY);

	rc = io_test_obj_fetch(arg, fetch_epoch, &dkey, &iod, &sgl, true);
	assert_int_equal(rc, 0);
	assert_int_equal(iod.iod_size, 0);
exit:
	return rc;
}

/** fetch from a nonexistent object */
static void
io_fetch_wo_object(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;
	daos_iov_t		val_iov;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		rex;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	char			fetch_buf[UPDATE_BUF_SIZE];
	daos_iod_t		iod;
	daos_sg_list_t		sgl;

	memset(&iod, 0, sizeof(iod));
	memset(&rex, 0, sizeof(rex));
	memset(&sgl, 0, sizeof(sgl));
	dts_key_gen(&dkey_buf[0], UPDATE_DKEY_SIZE, UPDATE_DKEY);
	dts_key_gen(&akey_buf[0], UPDATE_AKEY_SIZE, UPDATE_AKEY);
	daos_iov_set(&dkey, &dkey_buf[0], strlen(dkey_buf));
	daos_iov_set(&akey, &akey_buf[0], strlen(akey_buf));

	sgl.sg_nr = 1;
	sgl.sg_iovs = &val_iov;

	rex.rx_nr	= 1;
	rex.rx_idx	= daos_hash_string_u32(dkey_buf, dkey.iov_len);
	rex.rx_idx	%= 1000000;

	iod.iod_name	= akey;
	iod.iod_recxs	= &rex;
	iod.iod_nr	= 1;
	iod.iod_type	= DAOS_IOD_ARRAY;

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	daos_iov_set(&val_iov, &fetch_buf[0], UPDATE_BUF_SIZE);

	/* should be set to zero after fetching a nonexistent object */
	iod.iod_size = -1;
	arg->oid = gen_oid();

	rc = io_test_obj_fetch(arg, 1, &dkey, &iod, &sgl, true);
	assert_int_equal(rc, 0);
	assert_int_equal(iod.iod_size, 0);
}

static int
io_oid_iter_test(struct io_test_args *arg)
{
	vos_iter_param_t	param;
	daos_handle_t		ih;
	int			nr = 0;
	int			rc = 0;

	memset(&param, 0, sizeof(param));
	param.ip_hdl	= arg->ctx.tc_co_hdl;
	param.ip_epr.epr_lo = vts_epoch_gen + 10;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;

	rc = vos_iter_prepare(VOS_ITER_OBJ, &param, &ih);
	if (rc != 0) {
		print_error("Failed to prepare obj iterator\n");
		return rc;
	}

	rc = vos_iter_probe(ih, NULL);
	if (rc != 0) {
		print_error("Failed to set iterator cursor: %d\n", rc);
		goto out;
	}

	while (1) {
		vos_iter_entry_t	ent;
		daos_hash_out_t		anchor;

		rc = vos_iter_fetch(ih, &ent, NULL);
		if (rc == -DER_NONEXIST) {
			print_message("Finishing obj iteration\n");
			break;
		}

		if (rc != 0) {
			print_error("Failed to fetch objid: %d\n", rc);
			goto out;
		}

		D__DEBUG(DB_TRACE, "Object ID: "DF_UOID"\n",
			DP_UOID(ent.ie_oid));
		nr++;

		rc = vos_iter_next(ih);
		if (rc == -DER_NONEXIST)
			break;

		if (rc != 0) {
			print_error("Failed to move cursor: %d\n", rc);
			goto out;
		}

		if (!(arg->ta_flags & TF_IT_ANCHOR))
			continue;

		rc = vos_iter_fetch(ih, &ent, &anchor);
		if (rc != 0) {
			assert_true(rc != -DER_NONEXIST);
			print_error("Failed to fetch anchor: %d\n", rc);
			goto out;
		}

		rc = vos_iter_probe(ih, &anchor);
		if (rc != 0) {
			assert_true(rc != -DER_NONEXIST);
			print_error("Failed to probe anchor: %d\n", rc);
			goto out;
		}
	}
out:
	print_message("Enumerated %d, total_oids: %lu\n", nr, vts_cntr.cn_oids);
	assert_int_equal(nr, vts_cntr.cn_oids);
	vos_iter_finish(ih);
	return rc;
}

static int
io_set_attribute_setup(void **state)
{
	struct io_test_args	*arg = *state;
	struct vos_obj_df	*obj_df;
	struct vos_container	*cont;
	int			 rc;

	cont = vos_hdl2cont(arg->ctx.tc_co_hdl);
	assert_ptr_not_equal(cont, NULL);

	arg->oid = gen_oid();

	rc = vos_oi_find_alloc(cont, arg->oid, 1, &obj_df);
	assert_int_equal(rc, 0);

	return 0;
}

static void
io_set_attribute_test(void **state)
{
	struct io_test_args	*arg = *state;
	int rc;
	uint64_t attr;
	uint64_t i, expected;

	rc = vos_oi_get_attr(arg->ctx.tc_co_hdl, arg->oid, vts_epoch_gen + 1,
			     &attr);
	assert_int_equal(rc, 0);
	assert_int_equal(attr, 0);

	rc = vos_oi_set_attr(arg->ctx.tc_co_hdl, arg->oid, vts_epoch_gen + 1,
			     VOS_OI_FAILED);
	assert_int_equal(rc, 0);

	rc = vos_oi_get_attr(arg->ctx.tc_co_hdl, arg->oid, vts_epoch_gen + 1,
			     &attr);
	assert_int_equal(rc, 0);
	assert_int_equal(attr, VOS_OI_FAILED);

	rc = vos_oi_clear_attr(arg->ctx.tc_co_hdl, arg->oid, vts_epoch_gen + 1,
			       VOS_OI_FAILED);
	assert_int_equal(rc, 0);

	rc = vos_oi_get_attr(arg->ctx.tc_co_hdl, arg->oid, vts_epoch_gen + 1,
			     &attr);
	assert_int_equal(rc, 0);
	assert_int_equal(attr, 0);

	expected = 0;
	for (i = 0x8; i > 0; i >>= 1) {
		rc = vos_oi_set_attr(arg->ctx.tc_co_hdl, arg->oid,
				     vts_epoch_gen + 1, i);
		assert_int_equal(rc, 0);
		expected |= i;

		rc = vos_oi_get_attr(arg->ctx.tc_co_hdl, arg->oid,
				     vts_epoch_gen + 1, &attr);
		assert_int_equal(rc, 0);
		assert_int_equal(attr, expected);
	}

	for (i = 0x10; i > 0; i >>= 1) {
		rc = vos_oi_clear_attr(arg->ctx.tc_co_hdl, arg->oid,
				       vts_epoch_gen + 1, i);
		assert_int_equal(rc, 0);
		if (expected & i)
			expected ^= i;

		rc = vos_oi_get_attr(arg->ctx.tc_co_hdl, arg->oid,
				     vts_epoch_gen + 1, &attr);
		assert_int_equal(rc, 0);
		assert_int_equal(attr, expected);
	}
}
static void
pool_cont_same_uuid(void **state)
{

	int			ret = 0;
	uuid_t			pool_uuid, co_uuid;
	daos_handle_t		poh, coh;
	char			*fname = "/mnt/daos/vpool.test";
	daos_iov_t		val_iov;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		rex;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	char			update_buf[UPDATE_BUF_SIZE];
	daos_iod_t		iod;
	daos_sg_list_t		sgl;
	uuid_t			cookie;
	daos_unit_oid_t		oid;

	memset(&iod, 0, sizeof(iod));
	memset(&rex, 0, sizeof(rex));
	memset(&sgl, 0, sizeof(sgl));

	uuid_generate(pool_uuid);
	uuid_copy(co_uuid, pool_uuid);

	ret = vos_pool_create(fname, pool_uuid, 16*1024*1024);
	assert_int_equal(ret, 0);

	ret = vos_pool_open(fname, pool_uuid, &poh);
	assert_int_equal(ret, 0);

	ret = vos_cont_create(poh, co_uuid);
	assert_int_equal(ret, 0);

	ret = vos_pool_close(poh);
	assert_int_equal(ret, 0);

	poh = DAOS_HDL_INVAL;
	ret = vos_pool_open(fname, pool_uuid, &poh);
	assert_int_equal(ret, 0);

	ret = vos_cont_open(poh, co_uuid, &coh);
	assert_int_equal(ret, 0);

	dts_key_gen(&dkey_buf[0], UPDATE_DKEY_SIZE, UPDATE_DKEY);
	dts_key_gen(&dkey_buf[0], UPDATE_AKEY_SIZE, UPDATE_AKEY);
	daos_iov_set(&dkey, &dkey_buf[0], strlen(dkey_buf));
	daos_iov_set(&akey, &akey_buf[0], strlen(akey_buf));
	dts_buf_render(update_buf, UPDATE_BUF_SIZE);
	daos_iov_set(&val_iov, &update_buf[0], UPDATE_BUF_SIZE);
	iod.iod_size = UPDATE_BUF_SIZE;
	rex.rx_nr    = 1;

	sgl.sg_nr = 1;
	sgl.sg_iovs = &val_iov;

	iod.iod_name	= akey;
	iod.iod_recxs	= &rex;
	iod.iod_nr	= 1;
	iod.iod_type	= DAOS_IOD_ARRAY;

	uuid_generate(cookie);
	oid = dts_unit_oid_gen(0, 0);
	ret = vos_obj_update(coh, oid, 10, cookie, 0, &dkey, 1, &iod, &sgl);
	assert_int_equal(ret, 0);

	ret = vos_cont_close(coh);
	assert_int_equal(ret, 0);

	ret = vos_cont_destroy(poh, co_uuid);
	assert_int_equal(ret, 0);

	ret = vos_pool_close(poh);
	assert_int_equal(ret, 0);

	ret = vos_pool_destroy(fname, pool_uuid);
	assert_int_equal(ret, 0);
}

static void
io_fetch_no_exist_dkey(void **state)
{
	struct io_test_args	*arg = *state;

	arg->ta_flags = 0;
	io_update_and_fetch_incorrect_dkey(arg, 1, 1);
}

static void
io_fetch_no_exist_dkey_zc(void **state)
{
	struct io_test_args	*arg = *state;

	arg->ta_flags = TF_ZERO_COPY;
	io_update_and_fetch_incorrect_dkey(arg, 1, 1);
}

static void
io_fetch_no_exist_object(void **state)
{

	struct io_test_args	*arg = *state;

	arg->ta_flags = 0;

	io_fetch_wo_object(state);
}

static void
io_fetch_no_exist_object_zc(void **state)
{

	struct io_test_args	*arg = *state;

	arg->ta_flags = TF_ZERO_COPY;

	io_fetch_wo_object(state);
}

static void
io_simple_one_key_test(void **state, unsigned int flags)
{
	struct io_test_args	*arg = *state;
	int			rc;

	arg->ta_flags = flags;
	rc = io_update_and_fetch_dkey(arg, 1, 1);
	assert_int_equal(rc, 0);
}

static void
io_simple_one_key(void **state)
{
	int	i;

	for (i = 0; io_test_flags[i].tf_str != NULL; i++) {
		print_message("\t%d) Simple update/fetch/verify test (%s)\n",
			      i, io_test_flags[i].tf_str);
		io_simple_one_key_test(state, io_test_flags[i].tf_bits);
	}
}

static void
io_simple_one_key_cross_container(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc;
	daos_iov_t		val_iov;
	daos_recx_t		rex;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			update_buf[UPDATE_BUF_SIZE];
	char			fetch_buf[UPDATE_BUF_SIZE];
	daos_iod_t		iod;
	daos_sg_list_t		sgl;
	daos_key_t		dkey;
	daos_epoch_t		epoch = gen_rand_epoch();
	daos_unit_oid_t		l_oid;
	struct daos_uuid	cookie;

	/* Creating an additional container */
	uuid_generate_time_safe(arg->addn_co_uuid);
	rc = vos_cont_create(arg->ctx.tc_po_hdl, arg->addn_co_uuid);
	if (rc) {
		print_error("vos container creation error: %d\n", rc);
		return;
	}

	rc = vos_cont_open(arg->ctx.tc_po_hdl, arg->addn_co_uuid,
			   &arg->addn_co);
	if (rc) {
		print_error("vos container open error: %d\n", rc);
		goto failed;
	}

	memset(&iod, 0, sizeof(iod));
	memset(&rex, 0, sizeof(rex));
	memset(&sgl, 0, sizeof(sgl));
	memset(dkey_buf, 0, UPDATE_DKEY_SIZE);
	memset(update_buf, 0, UPDATE_BUF_SIZE);
	daos_iov_set(&dkey, &dkey_buf[0], UPDATE_DKEY_SIZE);

	sgl.sg_nr = 1;
	sgl.sg_iovs = &val_iov;

	dts_buf_render(update_buf, UPDATE_BUF_SIZE);
	daos_iov_set(&val_iov, &update_buf[0], UPDATE_BUF_SIZE);

	if (arg->ta_flags & TF_REC_EXT) {
		iod.iod_size = UPDATE_REC_SIZE;
		rex.rx_nr    = UPDATE_BUF_SIZE / UPDATE_REC_SIZE;
	} else {
		iod.iod_size = UPDATE_BUF_SIZE;
		rex.rx_nr    = 1;
	}
	rex.rx_idx	= daos_hash_string_u32(dkey_buf, dkey.iov_len);
	rex.rx_idx	%= 1000000;

	iod.iod_recxs	= &rex;
	iod.iod_nr	= 1;
	iod.iod_type	= DAOS_IOD_ARRAY;

	l_oid = gen_oid();
	cookie = gen_rand_cookie();
	rc  = vos_obj_update(arg->ctx.tc_co_hdl, arg->oid, epoch,
			     cookie.uuid, 0, &dkey, 1, &iod, &sgl);
	if (rc) {
		print_error("Failed to update %d\n", rc);
		goto failed;
	}

	cookie = gen_rand_cookie();
	rc = vos_obj_update(arg->addn_co, l_oid, epoch, cookie.uuid,
			    0, &dkey, 1, &iod, &sgl);
	if (rc) {
		print_error("Failed to update %d\n", rc);
		goto failed;
	}

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	daos_iov_set(&val_iov, &fetch_buf[0], UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	/**
	 * Fetch from second container with local obj id
	 * This should succeed.
	 */
	rc = vos_obj_fetch(arg->addn_co, l_oid, epoch,
			   &dkey, 1, &iod, &sgl);
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	daos_iov_set(&val_iov, &fetch_buf[0], UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	/**
	 * Fetch the objiD used in first container
	 * from second container should throw an error
	 */
	rc = vos_obj_fetch(arg->addn_co, arg->oid, epoch,
			   &dkey, 1, &iod, &sgl);
	/* This fetch should fail */
	assert_memory_not_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

failed:
	rc = vos_cont_close(arg->addn_co);
	assert_int_equal(rc, 0);

	rc = vos_cont_destroy(arg->ctx.tc_po_hdl, arg->addn_co_uuid);
	assert_int_equal(rc, 0);
}

static void
io_simple_punch(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc;

	arg->ta_flags = TF_PUNCH;
	/*
	 * Punch the last updated key at a future
	 * epoch
	 */
	rc = io_update_and_fetch_dkey(arg, 10, 10);
	assert_int_equal(rc, 0);
}



static void
io_simple_near_epoch_test(void **state, int flags)
{
	struct io_test_args	*arg = *state;
	daos_epoch_t		epoch = gen_rand_epoch();
	int			rc;

	arg->ta_flags = flags;
	rc = io_update_and_fetch_dkey(arg, epoch, epoch + 1000);
	assert_int_equal(rc, 0);
}

static void
io_simple_near_epoch(void **state)
{
	int	i;

	for (i = 0; io_test_flags[i].tf_str != NULL; i++) {
		print_message("\t%d) near epoch update/verify (%s)\n",
			      i, io_test_flags[i].tf_str);
		io_simple_near_epoch_test(state, io_test_flags[i].tf_bits);
	}
}

static int
io_iter_cookie_test(void **state)
{
	struct io_test_args	*arg = *state;

	arg->cookie_flag = true;
	return 0;
}

static void
io_pool_overflow_test(void **state)
{
	struct io_test_args	*args = *state;
	int			 i;
	int			 rc;
	daos_epoch_t		 epoch;

	test_args_reset(args, VPOOL_SIZE);

	epoch = gen_rand_epoch();
	for (i = 0; i < VTS_IO_KEYS; i++) {
		rc = io_update_and_fetch_dkey(args, epoch, epoch);
		if (rc) {
			assert_int_equal(rc, -DER_NOSPACE);
			break;
		}
	}
}

static int
io_pool_overflow_teardown(void **state)
{
	test_args_reset((struct io_test_args *)*state, VPOOL_SIZE);
	return 0;
}

static int
oid_iter_test_setup(void **state)
{
	struct io_test_args	*arg = *state;
	struct vos_obj_df	*obj_df;
	struct vos_container	*cont;
	daos_unit_oid_t		 oids[VTS_IO_OIDS];
	int			 i;
	int			 rc;

	cont = vos_hdl2cont(arg->ctx.tc_co_hdl);
	assert_ptr_not_equal(cont, NULL);

	for (i = 0; i < VTS_IO_OIDS; i++) {
		oids[i] = gen_oid();

		rc = vos_oi_find_alloc(cont, oids[i], 1, &obj_df);
		assert_int_equal(rc, 0);
	}
	return 0;
}

static void
oid_iter_test(void **state)
{
	struct io_test_args	*arg = *state;
	int			 rc;

	arg->ta_flags = 0;
	rc = io_oid_iter_test(arg);
	assert_true(rc == 0 || rc == -DER_NONEXIST);
}

static void
oid_iter_test_with_anchor(void **state)
{
	struct io_test_args	*arg = *state;
	int			 rc;

	arg->ta_flags = TF_IT_ANCHOR;
	rc = io_oid_iter_test(arg);
	assert_true(rc == 0 || rc == -DER_NONEXIST);
}

static const struct CMUnitTest io_tests[] = {
	{ "VOS201: VOS object IO index",
		io_oi_test, NULL, NULL},
	{ "VOS202: VOS object cache test",
		io_obj_cache_test, NULL, NULL},

	{ "VOS203: Simple update/fetch/verify test",
		io_simple_one_key, NULL, NULL},
	{ "VOS204: Simple Punch test",
		io_simple_punch, NULL, NULL},
	{ "VOS205: Simple near-epoch retrieval test",
		io_simple_near_epoch, NULL, NULL},
	{ "VOS220: 100K update/fetch/verify test",
		io_multiple_dkey, NULL, NULL},
	{ "VOS222: overwrite test",
		io_idx_overwrite, NULL, NULL},

	{ "VOS240.0: KV Iter tests (for dkey)",
		io_iter_test, NULL, NULL},
	{ "VOS240.1: KV Iter tests fetch cookie (for dkey)",
		io_iter_test, io_iter_cookie_test, NULL},

	{ "VOS240.1: KV Iter tests with anchor (for dkey)",
		io_iter_test_with_anchor, NULL, NULL},
	{ "VOS240.2: d-key enumeration with condition (akey)",
		io_iter_test_dkey_cond, NULL, NULL},
	{ "VOS240.3: KV range Iteration tests (for dkey)",
		io_obj_forward_iter_test, NULL, NULL},
	{ "VOS240.4: KV reverse range Iteration tests (for dkey)",
		io_obj_reverse_iter_test, NULL, NULL},
	{ "VOS240.5 KV range iteration tests (for recx)",
		io_obj_forward_recx_iter_test, NULL, NULL},
	{ "VOS240.6 KV reverse range iteration tests (for recx)",
		io_obj_reverse_recx_iter_test, NULL, NULL},

	{ "VOS245.0: Object iter test (for oid)",
		oid_iter_test, oid_iter_test_setup, NULL},
	{ "VOS245.1: Object iter test with anchor (for oid)",
		oid_iter_test_with_anchor, oid_iter_test_setup, NULL},
	{ "VOS250: VOS Set attribute test", io_set_attribute_test,
		io_set_attribute_setup, NULL},
	{ "VOS280: Same Obj ID on two containers (obj_cache test)",
		io_simple_one_key_cross_container, NULL, NULL},
	{ "VOS281.0: Fetch from non existent object",
		io_fetch_no_exist_object, NULL, NULL},
	{ "VOS281.1: Fetch from non existent object with zero-copy",
		io_fetch_no_exist_object_zc, NULL, NULL},
	{ "VOS282.0: Fetch from non existent dkey",
		io_fetch_no_exist_dkey, NULL, NULL},
	{ "VOS282.1: Fetch from non existent dkey with zero-copy",
		io_fetch_no_exist_dkey_zc, NULL, NULL},
	{ "VOS282.2: Accessing pool, container with same UUID",
		pool_cont_same_uuid, NULL, NULL},
	{ "VOS299: Space overflow negative error test",
		io_pool_overflow_test, NULL, io_pool_overflow_teardown},
};

int
run_io_test(void)
{
	return cmocka_run_group_tests_name("VOS IO tests", io_tests,
					   setup_io, teardown_io);
}
