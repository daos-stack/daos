/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of vos/tests/
 *
 * vos/tests/vts_io.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venaktesan@intel.com>
 */
#define D_LOGFAC	DD_FAC(tests)

#include "vts_io.h"
#include <daos_api.h>
#include <daos/checksum.h>
#include "vts_array.h"

#define NO_FLAGS	    (0)

/** epoch generator */
static daos_epoch_t		vts_epoch_gen;

static struct vts_counter	vts_cntr;
static uint64_t			update_akey_sv;
static uint64_t			update_akey_array;
static bool			vts_nest_iterators;

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

#define vts_key_gen_helper(dest, len, ukey, lkey, arg)		\
	do {							\
		if ((arg)->ofeat & DAOS_OF_##ukey##_UINT64)	\
			dts_key_gen(dest, len, NULL);		\
		else						\
			dts_key_gen(dest, len, (arg)->lkey);	\
	} while (0)

void
vts_key_gen(char *dest, size_t len, bool is_dkey, struct io_test_args *arg)
{
	memset(dest, 0, len);
	if (is_dkey) {
		vts_key_gen_helper(dest, len, DKEY, dkey, arg);
	} else if (arg->ta_flags & TF_FIXED_AKEY) {
		if (arg->ofeat & DAOS_OF_AKEY_UINT64) {
			if (arg->ta_flags & TF_REC_EXT) {
				memcpy(&dest[0], &update_akey_array,
				       sizeof(update_akey_array));
			} else {
				memcpy(&dest[0], &update_akey_sv,
				       sizeof(update_akey_sv));
			}
		} else {
			if (arg->ta_flags & TF_REC_EXT)
				strcpy(&dest[0], UPDATE_AKEY_ARRAY);
			else
				strcpy(&dest[0], UPDATE_AKEY_SV);
		}
	} else {
		vts_key_gen_helper(dest, len, AKEY, akey, arg);
	}

}

void
set_iov(d_iov_t *iov, char *buf, int int_flag)
{
	if (int_flag)
		d_iov_set(iov, buf, sizeof(uint64_t));
	else
		d_iov_set(iov, buf, strlen(buf));
}

daos_epoch_t
gen_rand_epoch(void)
{
	vts_epoch_gen += rand() % 100;
	return vts_epoch_gen;
}

daos_unit_oid_t
gen_oid(daos_ofeat_t ofeats)
{
	vts_cntr.cn_oids++;
	return dts_unit_oid_gen(0, ofeats, 0);
}

static uint32_t	oid_seed;
static uint64_t	oid_count;

void
reset_oid_stable(uint32_t seed)
{
	oid_seed = seed;
	oid_count = 0;
}

daos_unit_oid_t
gen_oid_stable(daos_ofeat_t ofeats)
{
	daos_unit_oid_t	uoid = {0};
	uint64_t	hdr;

	hdr = oid_seed;
	oid_seed += 2441; /* prime */
	hdr <<= 32;

	uoid.id_pub.lo = oid_count;
	oid_count += 66179; /* prime */
	uoid.id_pub.lo |= hdr;
	daos_obj_generate_id(&uoid.id_pub, oid_count, OC_RP_XSF, oid_seed);
	oid_count += 1171; /* prime */

	vts_cntr.cn_oids++;
	return uoid;
}

void
inc_cntr(unsigned long op_flags)
{
	if (op_flags & (TF_OVERWRITE | TF_PUNCH)) {
		vts_cntr.cn_punch++;
	} else {
		vts_cntr.cn_dkeys++;
		if (op_flags & TF_FIXED_AKEY)
			vts_cntr.cn_fa_dkeys++;
	}
}

static daos_ofeat_t init_ofeats;
static int init_num_keys;

void
test_args_init(struct io_test_args *args,
	       uint64_t pool_size)
{
	int	rc;

	memset(args, 0, sizeof(*args));
	memset(&vts_cntr, 0, sizeof(vts_cntr));

	vts_epoch_gen = 1;

	rc = vts_ctx_init(&args->ctx, pool_size);
	if (rc != 0)
		print_error("rc = "DF_RC"\n", DP_RC(rc));
	assert_rc_equal(rc, 0);
	args->oid = gen_oid(init_ofeats);
	args->ofeat = init_ofeats;
	args->dkey = UPDATE_DKEY;
	args->akey = UPDATE_AKEY;
	args->akey_size = UPDATE_AKEY_SIZE;
	args->dkey_size = UPDATE_DKEY_SIZE;
	if (init_ofeats & DAOS_OF_AKEY_UINT64) {
		dts_key_gen((char *)&update_akey_sv,
			    sizeof(update_akey_sv), NULL);
		dts_key_gen((char *)&update_akey_array,
			    sizeof(update_akey_array), NULL);
		args->akey = NULL;
		args->akey_size = sizeof(uint64_t);
	}
	if (init_ofeats & DAOS_OF_DKEY_UINT64) {
		args->dkey = NULL;
		args->dkey_size = sizeof(uint64_t);
	}
	snprintf(args->fname, VTS_BUF_SIZE, "/mnt/daos/vpool.test_%x",
		 init_ofeats);


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
	struct vos_ts_table	*table;

	srand(time(NULL));
	test_args_init(&test_args, VPOOL_SIZE);

	table = vos_ts_table_get();
	if (table == NULL)
		return -1;

	*state = &test_args;
	return 0;
}

int
teardown_io(void **state)
{
	struct io_test_args	*arg = *state;
	struct vos_ts_table	*table = vos_ts_table_get();
	int			 rc;

	if (table) {
		vos_ts_table_free(&table);
		rc = vos_ts_table_alloc(&table);
		if (rc != 0) {
			printf("Fatal error, table couldn't be reallocated\n");
			exit(rc);
		}
		vos_ts_table_set(table);
	}

	if (arg == NULL) {
		print_message("state not set, likely due to group-setup"
			      " issue\n");
		return 0;
	}

	assert_ptr_equal(arg, &test_args);
	vts_ctx_fini(&arg->ctx);
	return 0;
}

static int
io_recx_iterate(struct io_test_args *arg, vos_iter_param_t *param,
		daos_key_t *akey, int akey_id, int *recs, bool print_ent)
{
	daos_handle_t	ih = DAOS_HDL_INVAL;
	char		fetch_buf[8192];
	d_iov_t	iov_out;
	int		itype;
	int		nr = 0;
	int		rc;

	param->ip_akey = *akey;
	if (arg->ta_flags & TF_REC_EXT)
		itype = VOS_ITER_RECX;
	else
		itype = VOS_ITER_SINGLE;

	rc = vos_iter_prepare(itype, param, &ih, NULL);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			rc = 0;
		else
			print_error("Failed to create recx iterator: "DF_RC"\n",
				DP_RC(rc));
		return rc;
	}

	rc = vos_iter_probe(ih, NULL);
	if (rc != 0 && rc != -DER_NONEXIST) {
		print_error("Failed to set iterator cursor: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

	/* 8k fetch_buf is large enough to hold largest recx */
	d_iov_set(&iov_out, fetch_buf, sizeof(fetch_buf));

	while (rc == 0) {
		vos_iter_entry_t  ent;

		memset(&ent, 0, sizeof(ent));
		rc = vos_iter_fetch(ih, &ent, NULL);
		if (rc != 0) {
			print_error("Failed to fetch recx: "DF_RC"\n",
				    DP_RC(rc));
			goto out;
		}

		rc = vos_iter_copy(ih, &ent, &iov_out);
		if (rc != 0) {
			print_error("Failed to copy recx: "DF_RC"\n",
				    DP_RC(rc));
			goto out;
		}

		nr++;
		if (print_ent) {
			if (nr == 1) {
				char *buf = param->ip_akey.iov_buf;

				if (arg->ofeat & DAOS_OF_AKEY_UINT64)
					D_PRINT("akey[%d]: "DF_U64"\n", akey_id,
						*(uint64_t *)buf);
				else
					D_PRINT("akey[%d]: %s\n", akey_id, buf);
			}

			D_PRINT("\trecx %u : %s\n",
				(unsigned int)ent.ie_recx.rx_idx,
				bio_iov2req_buf(&ent.ie_biov) == NULL ?
				"[NULL]" :
				(char *)bio_iov2req_buf(&ent.ie_biov));
			D_PRINT("\tepoch: "DF_U64"\n", ent.ie_epoch);
		}

		rc = vos_iter_next(ih);
		if (rc != 0 && rc != -DER_NONEXIST) {
			print_error("Failed to move cursor: "DF_RC"\n",
				DP_RC(rc));
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
	daos_handle_t	ih = DAOS_HDL_INVAL;
	int		nr = 0;
	int		rc;

	param->ip_dkey = *dkey;
	rc = vos_iter_prepare(VOS_ITER_AKEY, param, &ih, NULL);
	if (rc != 0) {
		print_error("Failed to create akey iterator: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

	rc = vos_iter_probe(ih, NULL);
	if (rc == -DER_NONEXIST) {
		rc = 0;
		goto out;
	}
	if (rc != 0) {
		print_error("Failed to set iterator cursor: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

	while (rc == 0) {
		vos_iter_entry_t  ent;

		rc = vos_iter_fetch(ih, &ent, NULL);
		if (rc != 0) {
			print_error("Failed to fetch akey: "DF_RC"\n",
				    DP_RC(rc));
			goto out;
		}

		if (print_ent && nr == 0) {
			char *buf = param->ip_dkey.iov_buf;

			if (arg->ofeat & DAOS_OF_DKEY_UINT64)
				D_PRINT("dkey[%d]: "DF_U64"\n", dkey_id,
					*(uint64_t *)buf);
			else
				D_PRINT("dkey[%d]: %s\n", dkey_id, buf);
		}

		if (vts_nest_iterators)
			param->ip_ih = ih;
		rc = io_recx_iterate(arg, param, &ent.ie_key, nr,
				     recs, print_ent);

		nr++;
		rc = vos_iter_next(ih);
		if (rc != 0 && rc != -DER_NONEXIST) {
			print_error("Failed to move cursor: "DF_RC"\n",
				DP_RC(rc));
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
	int			akeys = 0;
	int			recs = 0;
	int			rc;

	iter_fa = (arg->ta_flags & TF_FIXED_AKEY);

	memset(&param, 0, sizeof(param));
	param.ip_hdl		= arg->ctx.tc_co_hdl;
	param.ip_oid		= arg->oid;
	param.ip_epr		= *epr;
	param.ip_epc_expr	= expr;

	if (iter_fa) {
		vts_key_gen(buf, UPDATE_AKEY_SIZE, false, arg);
		set_iov(&param.ip_akey, &buf[0],
			arg->ofeat & DAOS_OF_AKEY_UINT64);
	}

	rc = vos_iter_prepare(VOS_ITER_DKEY, &param, &ih, NULL);
	if (rc != 0) {
		print_error("Failed to prepare d-key iterator\n");
		return rc;
	}

	rc = vos_iter_probe(ih, NULL);
	if (rc != 0 && rc != -DER_NONEXIST) {
		print_error("Failed to set iterator cursor: %d\n",
			    rc);
		goto out;
	}

	while (rc == 0) {
		vos_iter_entry_t	ent;
		daos_anchor_t		anchor;

		rc = vos_iter_fetch(ih, &ent, NULL);
		if (rc == -DER_NONEXIST) {
			print_message("Finishing d-key iteration\n");
			break;
		}

		if (rc != 0) {
			print_error("Failed to fetch dkey: "DF_RC"\n",
				    DP_RC(rc));
			goto out;
		}

		if (vts_nest_iterators)
			param.ip_ih = ih;
		rc = io_akey_iterate(arg, &param, &ent.ie_key, nr,
				     &akeys, &recs, print_ent);
		if (rc != 0)
			goto out;

		nr++;
		rc = vos_iter_next(ih);
		if (rc == -DER_NONEXIST) {
			print_message("Finishing d-key iteration\n");
			break;
		}

		if (rc != 0) {
			print_error("Failed to move cursor: "DF_RC"\n",
				DP_RC(rc));
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
	rc = 0;
 out:
	vos_iter_finish(ih);
	*num_dkeys = nr;
	*num_akeys = akeys;
	*num_recs  = recs;
	return rc;
}

static int
io_test_add_csums(daos_iod_t *iod, d_sg_list_t *sgl,
		  struct daos_csummer **p_csummer,
		  struct dcs_iod_csums **p_iod_csums)
{
	enum DAOS_HASH_TYPE	 type = HASH_TYPE_CRC64;
	size_t			 chunk_size = 1 << 12;
	int			 rc = 0;

	rc = daos_csummer_init_with_type(p_csummer, type, chunk_size, 0);
	if (rc)
		return rc;
	rc = daos_csummer_calc_iods(*p_csummer, sgl, iod, NULL, 1, false,
				    NULL, 0, p_iod_csums);
	if (rc)
		daos_csummer_destroy(p_csummer);
	return rc;
}

int
io_test_obj_update(struct io_test_args *arg, daos_epoch_t epoch, uint64_t flags,
		   daos_key_t *dkey, daos_iod_t *iod, d_sg_list_t *sgl,
		   struct dtx_handle *dth, bool verbose)
{
	struct bio_sglist	*bsgl;
	struct bio_iov		*biov;
	struct dcs_iod_csums	*iod_csums = NULL;
	struct daos_csummer	*csummer = NULL;
	d_iov_t			*srv_iov;
	daos_handle_t		ioh;
	unsigned int		off;
	int			i, rc = 0;

	if ((arg->ta_flags & TF_USE_CSUMS) && iod->iod_size > 0) {
		rc = io_test_add_csums(iod, sgl, &csummer, &iod_csums);
		if (rc != 0)
			return rc;
	}

	if (!(arg->ta_flags & TF_ZERO_COPY)) {
		rc = vos_obj_update(arg->ctx.tc_co_hdl, arg->oid, epoch, 0,
				    flags, dkey, 1, iod, iod_csums, sgl);
		if (rc != 0 && verbose)
			print_error("Failed to update: "DF_RC"\n", DP_RC(rc));
		goto end;
	}
	/* Punch can't be zero copy */
	assert_true(iod->iod_size > 0);

	rc = vos_update_begin(arg->ctx.tc_co_hdl, arg->oid, epoch, flags, dkey,
			      1, iod, iod_csums, false, 0, &ioh, dth);
	if (rc != 0) {
		if (verbose && rc != -DER_INPROGRESS)
			print_error("Failed to prepare ZC update: "DF_RC"\n",
				DP_RC(rc));
		goto end;
	}

	srv_iov = &sgl->sg_iovs[0];
	rc = bio_iod_prep(vos_ioh2desc(ioh));
	if (rc)
		goto end;

	bsgl = vos_iod_sgl_at(ioh, 0);
	assert_true(bsgl != NULL);

	for (i = off = 0; i < bsgl->bs_nr_out; i++) {
		biov = &bsgl->bs_iovs[i];
		memcpy(bio_iov2req_buf(biov), srv_iov->iov_buf + off,
		       bio_iov2req_len(biov));
		off += bio_iov2req_len(biov);
	}
	assert_true(srv_iov->iov_len == off);

	rc = bio_iod_post(vos_ioh2desc(ioh));
end:
	if (rc == 0 && (arg->ta_flags & TF_ZERO_COPY))
		rc = vos_update_end(ioh, 0, dkey, rc, dth);
	if (rc != 0 && verbose && rc != -DER_INPROGRESS &&
		(arg->ta_flags & TF_ZERO_COPY))
		print_error("Failed to submit ZC update: "DF_RC"\n", DP_RC(rc));
	if ((arg->ta_flags & TF_USE_CSUMS) && iod->iod_size > 0) {
		daos_csummer_free_ic(csummer, &iod_csums);
		daos_csummer_destroy(&csummer);
	}

	return rc;
}

int
io_test_obj_fetch(struct io_test_args *arg, daos_epoch_t epoch, uint64_t flags,
		  daos_key_t *dkey, daos_iod_t *iod, d_sg_list_t *sgl,
		  bool verbose)
{
	struct bio_sglist	*bsgl;
	struct bio_iov		*biov;
	d_iov_t			*dst_iov;
	daos_handle_t		 ioh;
	unsigned int		 off;
	int			 i;
	int			 rc;

	if (!(arg->ta_flags & TF_ZERO_COPY)) {
		rc = vos_obj_fetch(arg->ctx.tc_co_hdl,
				   arg->oid, epoch, flags, dkey, 1, iod,
				   sgl);
		if (rc != 0 && verbose)
			print_error("Failed to fetch: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	rc = vos_fetch_begin(arg->ctx.tc_co_hdl, arg->oid, epoch, dkey,
			     1, iod, flags, NULL, &ioh, NULL);
	if (rc != 0) {
		if (verbose && rc != -DER_INPROGRESS)
			print_error("Failed to prepare ZC update: "DF_RC"\n",
				DP_RC(rc));
		return rc;
	}

	dst_iov = &sgl->sg_iovs[0];
	rc = bio_iod_prep(vos_ioh2desc(ioh));
	if (rc)
		goto end;

	bsgl = vos_iod_sgl_at(ioh, 0);
	assert_true(bsgl != NULL);

	for (i = off = 0; i < bsgl->bs_nr_out; i++) {
		biov = &bsgl->bs_iovs[i];
		if (!bio_addr_is_hole(&biov->bi_addr))
			memcpy(dst_iov->iov_buf + off, bio_iov2req_buf(biov),
			       bio_iov2req_len(biov));
		off += bio_iov2req_len(biov);
	}
	dst_iov->iov_len = off;
	assert_true(dst_iov->iov_buf_len >= dst_iov->iov_len);

	rc = bio_iod_post(vos_ioh2desc(ioh));
end:
	rc = vos_fetch_end(ioh, rc);
	if (((flags & VOS_COND_FETCH_MASK) && rc == -DER_NONEXIST) ||
	    rc == -DER_INPROGRESS)
		goto skip;
	if (rc != 0 && verbose)
		print_error("Failed to submit ZC update: "DF_RC"\n", DP_RC(rc));
skip:
	return rc;
}

static int
io_update_and_fetch_dkey(struct io_test_args *arg, daos_epoch_t update_epoch,
			 daos_epoch_t fetch_epoch)
{
	int			rc = 0;
	d_iov_t			val_iov;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		rex;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	char			update_buf[UPDATE_BUF_SIZE];
	char			fetch_buf[UPDATE_BUF_SIZE];
	daos_iod_t		iod;
	d_sg_list_t		sgl;
	unsigned int		recx_size;
	unsigned int		recx_nr;

	/* Setup */
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
			memcpy(dkey_buf, last_dkey, arg->dkey_size);
			memcpy(akey_buf, last_akey, arg->akey_size);
		} else {
			vts_key_gen(&dkey_buf[0], arg->dkey_size, true, arg);
			memcpy(last_dkey, dkey_buf, arg->dkey_size);

			vts_key_gen(&akey_buf[0], arg->akey_size, false, arg);
			memcpy(last_akey, akey_buf, arg->akey_size);
		}

		set_iov(&dkey, &dkey_buf[0], arg->ofeat & DAOS_OF_DKEY_UINT64);
		set_iov(&akey, &akey_buf[0], arg->ofeat & DAOS_OF_AKEY_UINT64);

		dts_buf_render(update_buf, UPDATE_BUF_SIZE);
		d_iov_set(&val_iov, &update_buf[0], UPDATE_BUF_SIZE);
		iod.iod_size = recx_size;
		rex.rx_nr    = recx_nr;
	} else {
		set_iov(&dkey, &last_dkey[0], arg->ofeat & DAOS_OF_DKEY_UINT64);
		set_iov(&akey, &last_akey[0], arg->ofeat & DAOS_OF_AKEY_UINT64);

		memset(update_buf, 0, UPDATE_BUF_SIZE);
		d_iov_set(&val_iov, &update_buf[0], UPDATE_BUF_SIZE);
		rex.rx_nr    = recx_nr;
		iod.iod_size = 0;
	}

	sgl.sg_nr = 1;
	sgl.sg_iovs = &val_iov;

	rex.rx_idx	= hash_key(&dkey, arg->ofeat & DAOS_OF_DKEY_UINT64);

	iod.iod_name	= akey;
	iod.iod_recxs	= &rex;
	iod.iod_nr	= 1;

	/* Act */
	rc = io_test_obj_update(arg, update_epoch, 0, &dkey, &iod, &sgl,
				NULL, true);
	if (rc)
		goto exit;

	/* Changes */
	inc_cntr(arg->ta_flags);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, &fetch_buf[0], UPDATE_BUF_SIZE);

	iod.iod_size = DAOS_REC_ANY;

	/* Act again */
	rc = io_test_obj_fetch(arg, fetch_epoch, 0, &dkey, &iod, &sgl, true);
	if (rc)
		goto exit;

	/* Verify */
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

exit:
	return rc;
}

static inline int
hold_objects(struct vos_object **objs, struct daos_lru_cache *occ,
	     daos_handle_t *coh, daos_unit_oid_t *oid, int start, int end,
	     bool no_create, int exp_rc)
{
	int			i = 0, rc = 0;
	daos_epoch_range_t	epr = {0, 1};
	uint64_t		hold_flags;

	hold_flags = no_create ? 0 : VOS_OBJ_CREATE;
	hold_flags |= VOS_OBJ_VISIBLE;
	for (i = start; i < end; i++) {
		rc = vos_obj_hold(occ, vos_hdl2cont(*coh), *oid, &epr, 0,
				  hold_flags, no_create ? DAOS_INTENT_DEFAULT :
				  DAOS_INTENT_UPDATE, &objs[i], 0);
		if (rc != exp_rc)
			return 1;
	}

	return 0;
}

static void
io_oi_test(void **state)
{
	struct io_test_args	*arg = *state;
	struct vos_obj_df	*obj[2];
	struct vos_container	*cont;
	daos_unit_oid_t		oid;
	int			rc = 0;

	oid = gen_oid(arg->ofeat);

	cont = vos_hdl2cont(arg->ctx.tc_co_hdl);
	assert_ptr_not_equal(cont, NULL);

	rc = vos_oi_find_alloc(cont, oid, 1, true, &obj[0], NULL);
	assert_rc_equal(rc, 0);

	rc = vos_oi_find_alloc(cont, oid, 2, true, &obj[1], NULL);
	assert_rc_equal(rc, 0);
}

static void
io_obj_cache_test(void **state)
{
	struct io_test_args	*arg = *state;
	struct vos_test_ctx	*ctx = &arg->ctx;
	struct daos_lru_cache	*occ = NULL;
	struct vos_object	*objs[20];
	daos_epoch_range_t	 epr = {0, 1};
	daos_unit_oid_t		 oids[2];
	char			*po_name;
	uuid_t			 pool_uuid;
	daos_handle_t		 l_poh, l_coh;
	int			 i, rc;

	rc = vos_obj_cache_create(10, &occ);
	assert_rc_equal(rc, 0);

	rc = vts_alloc_gen_fname(&po_name);
	assert_int_equal(rc, 0);

	uuid_generate_time_safe(pool_uuid);
	rc = vos_pool_create(po_name, pool_uuid, VPOOL_16M, 0);
	assert_rc_equal(rc, 0);

	rc = vos_pool_open(po_name, pool_uuid, false /* small */, &l_poh);
	assert_rc_equal(rc, 0);

	rc = vos_cont_create(l_poh, ctx->tc_co_uuid);
	assert_rc_equal(rc, 0);

	rc = vos_cont_open(l_poh, ctx->tc_co_uuid, &l_coh);
	assert_rc_equal(rc, 0);

	oids[0] = gen_oid(arg->ofeat);
	oids[1] = gen_oid(arg->ofeat);

	rc = vos_obj_hold(occ, vos_hdl2cont(ctx->tc_co_hdl), oids[0], &epr, 0,
			  VOS_OBJ_CREATE | VOS_OBJ_VISIBLE, DAOS_INTENT_DEFAULT,
			  &objs[0], 0);
	assert_rc_equal(rc, 0);
	vos_obj_release(occ, objs[0], false);

	rc = vos_obj_hold(occ, vos_hdl2cont(l_coh), oids[1], &epr, 0,
			  VOS_OBJ_CREATE | VOS_OBJ_VISIBLE, DAOS_INTENT_DEFAULT,
			  &objs[0], 0);
	assert_rc_equal(rc, 0);
	vos_obj_release(occ, objs[0], false);

	rc = hold_objects(objs, occ, &ctx->tc_co_hdl, &oids[0], 0, 10, true, 0);
	assert_int_equal(rc, 0);

	rc = hold_objects(objs, occ, &ctx->tc_co_hdl, &oids[1], 10, 15, true,
			  -DER_NONEXIST);
	assert_int_equal(rc, 0);

	rc = hold_objects(objs, occ, &l_coh, &oids[1], 10, 15, true, 0);
	assert_int_equal(rc, 0);
	rc = vos_obj_hold(occ, vos_hdl2cont(l_coh), oids[1], &epr, 0,
			  VOS_OBJ_VISIBLE, DAOS_INTENT_DEFAULT, &objs[16], 0);
	assert_rc_equal(rc, 0);

	vos_obj_release(occ, objs[16], false);

	for (i = 0; i < 5; i++)
		vos_obj_release(occ, objs[i], false);
	for (i = 10; i < 15; i++)
		vos_obj_release(occ, objs[i], false);

	rc = hold_objects(objs, occ, &l_coh, &oids[1], 15, 20, true, 0);
	assert_int_equal(rc, 0);

	for (i = 5; i < 10; i++)
		vos_obj_release(occ, objs[i], false);
	for (i = 15; i < 20; i++)
		vos_obj_release(occ, objs[i], false);

	rc = vos_cont_close(l_coh);
	assert_rc_equal(rc, 0);
	rc = vos_cont_destroy(l_poh, ctx->tc_co_uuid);
	assert_rc_equal(rc, 0);
	rc = vos_pool_close(l_poh);
	assert_rc_equal(rc, 0);
	rc = vos_pool_destroy(po_name, pool_uuid);
	assert_rc_equal(rc, 0);
	vos_obj_cache_destroy(occ);
	free(po_name);
}

static void
io_multiple_dkey_test(void **state, unsigned int flags)
{
	struct io_test_args	*arg = *state;
	int			 i;
	int			 rc = 0;
	daos_epoch_t		 epoch = gen_rand_epoch();

	arg->ta_flags = flags;
	for (i = 0; i < init_num_keys; i++) {
		rc = io_update_and_fetch_dkey(arg, epoch, epoch);
		assert_rc_equal(rc, 0);
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
	assert_rc_equal(rc, 0);

	arg->ta_flags |= TF_OVERWRITE;
	rc = io_update_and_fetch_dkey(arg, epoch, epoch);
	assert_rc_equal(rc, 0);
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
io_iter_test_base(struct io_test_args *args)
{
	daos_epoch_range_t	epr;
	int			rc = 0;
	int			nr;
	int			akeys;
	int			recs;

	epr.epr_lo = vts_epoch_gen + 10;
	epr.epr_hi = DAOS_EPOCH_MAX;

	rc = io_obj_iter_test(args, &epr, VOS_IT_EPC_GE,
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
io_iter_test(void **state)
{
	struct io_test_args	*arg = *state;

	arg->ta_flags = TF_REC_EXT;
	io_iter_test_base(arg);
}

static void
io_iter_test_with_anchor(void **state)
{
	struct io_test_args	*arg = *state;

	arg->ta_flags = TF_IT_ANCHOR | TF_REC_EXT;
	io_iter_test_base(arg);
}

#define RANGE_ITER_KEYS (10)

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
			      &nr, &akeys, &recs, false);
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
			      &nr, &akeys, &recs, false);
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
io_obj_iter_test_base(void **state, vos_it_epc_expr_t direction)
{
	struct io_test_args	*args = *state;
	int			 rc;

	rc = io_obj_range_iter_test(args, direction);
	assert_rc_equal(rc, 0);
}

static void
io_obj_forward_iter_test(void **state)
{
	io_obj_iter_test_base(state, VOS_IT_EPC_RE);
}

static void
io_obj_reverse_iter_test(void **state)
{
	io_obj_iter_test_base(state, VOS_IT_EPC_RR);
}

static void
io_obj_recx_iter_test(void **state, vos_it_epc_expr_t direction)
{
	struct io_test_args	*args = *state;
	int			 rc;

	rc = io_obj_recx_range_iteration(args, direction);
	assert_rc_equal(rc, 0);
}

static void
io_obj_forward_recx_iter_test(void **state)
{
	io_obj_recx_iter_test(state, VOS_IT_EPC_RE);
}

static void
io_obj_reverse_recx_iter_test(void **state)
{
	io_obj_recx_iter_test(state, VOS_IT_EPC_RR);
}

static int
io_update_and_fetch_incorrect_dkey(struct io_test_args *arg,
				   daos_epoch_t update_epoch,
				   daos_epoch_t fetch_epoch)
{

	int			rc = 0;
	d_iov_t		val_iov;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		rex;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	char			update_buf[UPDATE_BUF_SIZE];
	char			fetch_buf[UPDATE_BUF_SIZE];
	daos_iod_t		iod;
	d_sg_list_t		sgl;

	memset(&iod, 0, sizeof(iod));
	memset(&rex, 0, sizeof(rex));
	memset(&sgl, 0, sizeof(sgl));

	vts_key_gen(&dkey_buf[0], arg->dkey_size, true, arg);
	vts_key_gen(&akey_buf[0], arg->akey_size, false, arg);
	memcpy(last_akey, akey_buf, arg->akey_size);

	set_iov(&dkey, &dkey_buf[0], arg->ofeat & DAOS_OF_DKEY_UINT64);
	set_iov(&akey, &akey_buf[0], arg->ofeat & DAOS_OF_AKEY_UINT64);

	dts_buf_render(update_buf, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, &update_buf[0], UPDATE_BUF_SIZE);
	iod.iod_size	= val_iov.iov_len;

	sgl.sg_nr = 1;
	sgl.sg_iovs = &val_iov;

	rex.rx_nr	= 1;
	rex.rx_idx	= hash_key(&dkey, arg->ofeat & DAOS_OF_DKEY_UINT64);

	iod.iod_name	= akey;
	iod.iod_recxs	= &rex;
	iod.iod_nr	= 1;
	iod.iod_type	= DAOS_IOD_ARRAY;

	rc = io_test_obj_update(arg, update_epoch, 0, &dkey, &iod, &sgl,
				NULL, true);
	if (rc)
		goto exit;

	inc_cntr(arg->ta_flags);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, &fetch_buf[0], UPDATE_BUF_SIZE);

	/* will be set to zero after fetching a nonexistent key */
	iod.iod_size = -1;

	/* Injecting an incorrect dkey for fetch! */
	vts_key_gen(&dkey_buf[0], arg->dkey_size, true, arg);

	rc = io_test_obj_fetch(arg, fetch_epoch, 0, &dkey, &iod, &sgl, true);
	assert_rc_equal(rc, 0);
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
	d_iov_t		val_iov;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		rex;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	char			fetch_buf[UPDATE_BUF_SIZE];
	daos_iod_t		iod;
	d_sg_list_t		sgl;

	memset(&iod, 0, sizeof(iod));
	memset(&rex, 0, sizeof(rex));
	memset(&sgl, 0, sizeof(sgl));
	vts_key_gen(&dkey_buf[0], arg->dkey_size, true, arg);
	vts_key_gen(&akey_buf[0], arg->akey_size, false, arg);
	set_iov(&dkey, &dkey_buf[0], arg->ofeat & DAOS_OF_DKEY_UINT64);
	set_iov(&akey, &akey_buf[0], arg->ofeat & DAOS_OF_AKEY_UINT64);

	sgl.sg_nr = 1;
	sgl.sg_iovs = &val_iov;

	rex.rx_nr	= 1;
	rex.rx_idx	= hash_key(&dkey, arg->ofeat & DAOS_OF_DKEY_UINT64);

	iod.iod_name	= akey;
	iod.iod_recxs	= &rex;
	iod.iod_nr	= 1;
	iod.iod_type	= DAOS_IOD_ARRAY;

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, &fetch_buf[0], UPDATE_BUF_SIZE);

	/* should be set to zero after fetching a nonexistent object */
	iod.iod_size = -1;
	arg->oid = gen_oid(arg->ofeat);

	rc = io_test_obj_fetch(arg, 1, 0, &dkey, &iod, &sgl, true);
	assert_rc_equal(rc, 0);
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

	rc = vos_iter_prepare(VOS_ITER_OBJ, &param, &ih, NULL);
	if (rc != 0) {
		print_error("Failed to prepare obj iterator\n");
		return rc;
	}

	rc = vos_iter_probe(ih, NULL);
	if (rc != 0) {
		print_error("Failed to set iterator cursor: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

	while (1) {
		vos_iter_entry_t	ent;
		daos_anchor_t		anchor;

		rc = vos_iter_fetch(ih, &ent, NULL);
		if (rc == -DER_NONEXIST) {
			print_message("Finishing obj iteration\n");
			break;
		}

		if (rc != 0) {
			print_error("Failed to fetch objid: "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}

		D_DEBUG(DB_TRACE, "Object ID: "DF_UOID"\n",
			DP_UOID(ent.ie_oid));
		nr++;

		rc = vos_iter_next(ih);
		if (rc == -DER_NONEXIST)
			break;

		if (rc != 0) {
			print_error("Failed to move cursor: "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}

		if (!(arg->ta_flags & TF_IT_ANCHOR))
			continue;

		rc = vos_iter_fetch(ih, &ent, &anchor);
		if (rc != 0) {
			assert_true(rc != -DER_NONEXIST);
			print_error("Failed to fetch anchor: "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}

		rc = vos_iter_probe(ih, &anchor);
		if (rc != 0) {
			assert_true(rc != -DER_NONEXIST);
			print_error("Failed to probe anchor: "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}
	}
out:
	print_message("Enumerated %d, total_oids: %lu\n", nr, vts_cntr.cn_oids);
	assert_int_equal(nr, vts_cntr.cn_oids);
	vos_iter_finish(ih);
	return rc;
}

static void
pool_cont_same_uuid(void **state)
{

	struct io_test_args	*arg = *state;
	uuid_t			pool_uuid, co_uuid;
	daos_handle_t		poh, coh;
	d_iov_t		val_iov;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		rex;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	char			update_buf[UPDATE_BUF_SIZE];
	daos_iod_t		iod;
	d_sg_list_t		sgl;
	daos_unit_oid_t		oid;
	int			ret = 0;

	memset(&iod, 0, sizeof(iod));
	memset(&rex, 0, sizeof(rex));
	memset(&sgl, 0, sizeof(sgl));

	uuid_generate(pool_uuid);
	uuid_copy(co_uuid, pool_uuid);

	ret = vos_pool_create(arg->fname, pool_uuid, VPOOL_16M, 0);
	assert_rc_equal(ret, 0);

	ret = vos_pool_open(arg->fname, pool_uuid, false /* small */, &poh);
	assert_rc_equal(ret, 0);

	ret = vos_cont_create(poh, co_uuid);
	assert_rc_equal(ret, 0);

	ret = vos_pool_close(poh);
	assert_rc_equal(ret, 0);

	poh = DAOS_HDL_INVAL;
	ret = vos_pool_open(arg->fname, pool_uuid, false /* small */, &poh);
	assert_rc_equal(ret, 0);

	ret = vos_cont_open(poh, co_uuid, &coh);
	assert_rc_equal(ret, 0);

	vts_key_gen(&dkey_buf[0], arg->dkey_size, true, arg);
	vts_key_gen(&akey_buf[0], arg->akey_size, false, arg);
	set_iov(&dkey, &dkey_buf[0], arg->ofeat & DAOS_OF_DKEY_UINT64);
	set_iov(&akey, &akey_buf[0], arg->ofeat & DAOS_OF_AKEY_UINT64);
	dts_buf_render(update_buf, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, &update_buf[0], UPDATE_BUF_SIZE);
	iod.iod_size = UPDATE_BUF_SIZE;
	rex.rx_nr    = 1;

	sgl.sg_nr = 1;
	sgl.sg_iovs = &val_iov;

	iod.iod_name	= akey;
	iod.iod_recxs	= &rex;
	iod.iod_nr	= 1;
	iod.iod_type	= DAOS_IOD_ARRAY;

	oid = gen_oid(arg->ofeat);
	ret = vos_obj_update(coh, oid, 10, 0, 0, &dkey, 1, &iod, NULL, &sgl);
	assert_rc_equal(ret, 0);

	ret = vos_cont_close(coh);
	assert_rc_equal(ret, 0);

	ret = vos_cont_destroy(poh, co_uuid);
	assert_rc_equal(ret, 0);

	ret = vos_pool_close(poh);
	assert_rc_equal(ret, 0);

	ret = vos_pool_destroy(arg->fname, pool_uuid);
	assert_rc_equal(ret, 0);
}

static void
io_fetch_no_exist_dkey_base(void **state, unsigned long flags)
{
	struct io_test_args	*arg = *state;
	int			rc;

	arg->ta_flags = flags;
	rc = io_update_and_fetch_incorrect_dkey(arg, 1, 1);
	assert_rc_equal(rc, 0);
}

static void
io_fetch_no_exist_dkey(void **state)
{
	io_fetch_no_exist_dkey_base(state, NO_FLAGS);
}

static void
io_fetch_no_exist_dkey_zc(void **state)
{
	io_fetch_no_exist_dkey_base(state, TF_ZERO_COPY);
}

static void
io_fetch_no_exist_object_base(void **state, unsigned long flags)
{
	struct io_test_args	*arg = *state;

	arg->ta_flags = flags;
	io_fetch_wo_object(state);
}

static void
io_fetch_no_exist_object(void **state)
{
	io_fetch_no_exist_object_base(state, NO_FLAGS);
}

static void
io_fetch_no_exist_object_zc(void **state)
{
	io_fetch_no_exist_object_base(state, TF_ZERO_COPY);
}

static void
io_simple_one_key_test(void **state, unsigned int flags)
{
	struct io_test_args	*arg = *state;
	int			rc;

	arg->ta_flags = flags;
	rc = io_update_and_fetch_dkey(arg, 1, 1);
	assert_rc_equal(rc, 0);
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
	d_iov_t		val_iov;
	daos_recx_t		rex;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_DKEY_SIZE];
	char			update_buf[UPDATE_BUF_SIZE];
	char			fetch_buf[UPDATE_BUF_SIZE];
	daos_iod_t		iod;
	d_sg_list_t		sgl;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_epoch_t		epoch = gen_rand_epoch();
	daos_unit_oid_t		l_oid;

	/* Creating an additional container */
	uuid_generate_time_safe(arg->addn_co_uuid);
	rc = vos_cont_create(arg->ctx.tc_po_hdl, arg->addn_co_uuid);
	if (rc) {
		print_error("vos container creation error: "DF_RC"\n",
			    DP_RC(rc));
		return;
	}

	rc = vos_cont_open(arg->ctx.tc_po_hdl, arg->addn_co_uuid,
			   &arg->addn_co);
	if (rc) {
		print_error("vos container open error: "DF_RC"\n", DP_RC(rc));
		goto failed;
	}

	memset(&iod, 0, sizeof(iod));
	memset(&rex, 0, sizeof(rex));
	memset(&sgl, 0, sizeof(sgl));
	vts_key_gen(&dkey_buf[0], arg->dkey_size, true, arg);
	vts_key_gen(&akey_buf[0], arg->akey_size, false, arg);
	memset(update_buf, 0, UPDATE_BUF_SIZE);
	set_iov(&dkey, &dkey_buf[0], arg->ofeat & DAOS_OF_DKEY_UINT64);
	set_iov(&akey, &akey_buf[0], arg->ofeat & DAOS_OF_AKEY_UINT64);

	sgl.sg_nr = 1;
	sgl.sg_iovs = &val_iov;

	dts_buf_render(update_buf, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, &update_buf[0], UPDATE_BUF_SIZE);

	if (arg->ta_flags & TF_REC_EXT) {
		iod.iod_size = UPDATE_REC_SIZE;
		rex.rx_nr    = UPDATE_BUF_SIZE / UPDATE_REC_SIZE;
	} else {
		iod.iod_size = UPDATE_BUF_SIZE;
		rex.rx_nr    = 1;
	}
	rex.rx_idx	= hash_key(&dkey, arg->ofeat & DAOS_OF_DKEY_UINT64);

	iod.iod_name	= akey;
	iod.iod_recxs	= &rex;
	iod.iod_nr	= 1;
	iod.iod_type	= DAOS_IOD_ARRAY;

	l_oid = gen_oid(arg->ofeat);
	rc  = vos_obj_update(arg->ctx.tc_co_hdl, arg->oid, epoch, 0, 0, &dkey,
			     1, &iod, NULL, &sgl);
	if (rc) {
		print_error("Failed to update "DF_RC"\n", DP_RC(rc));
		goto failed;
	}

	rc = vos_obj_update(arg->addn_co, l_oid, epoch, 0, 0, &dkey, 1, &iod,
			    NULL, &sgl);
	if (rc) {
		print_error("Failed to update "DF_RC"\n", DP_RC(rc));
		goto failed;
	}

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, &fetch_buf[0], UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	/**
	 * Fetch from second container with local obj id
	 * This should succeed.
	 */
	rc = vos_obj_fetch(arg->addn_co, l_oid, epoch,
			   0, &dkey, 1, &iod, &sgl);
	assert_memory_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

	memset(fetch_buf, 0, UPDATE_BUF_SIZE);
	d_iov_set(&val_iov, &fetch_buf[0], UPDATE_BUF_SIZE);
	iod.iod_size = DAOS_REC_ANY;

	/**
	 * Fetch the objiD used in first container
	 * from second container should throw an error
	 */
	rc = vos_obj_fetch(arg->addn_co, arg->oid, epoch,
			   0, &dkey, 1, &iod, &sgl);
	/* This fetch should fail */
	assert_memory_not_equal(update_buf, fetch_buf, UPDATE_BUF_SIZE);

failed:
	rc = vos_cont_close(arg->addn_co);
	assert_rc_equal(rc, 0);

	rc = vos_cont_destroy(arg->ctx.tc_po_hdl, arg->addn_co_uuid);
	assert_rc_equal(rc, 0);
}

static void
io_simple_punch(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc;

	/*
	 * Punch the last updated key at a future
	 * epoch
	 */
	rc = io_update_and_fetch_dkey(arg, 10, 10);
	assert_rc_equal(rc, 0);
}

static void
io_simple_near_epoch_test(void **state, int flags)
{
	struct io_test_args	*arg = *state;
	daos_epoch_t		epoch = gen_rand_epoch();
	int			rc;

	arg->ta_flags = flags;
	rc = io_update_and_fetch_dkey(arg, epoch, epoch + 1000);
	assert_rc_equal(rc, 0);
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

#define SGL_TEST_BUF_SIZE (1024)
#define SGL_TEST_BUF_COUNT (4)
static void
io_sgl_update(void **state)
{
	/* This tests uses multiple buffers for the update/write */
	struct io_test_args	*arg = *state;
	int			rc = 0;
	int			i;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		rex;
	daos_iod_t		iod;
	d_sg_list_t		sgl;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	char			*update_buffs[SGL_TEST_BUF_COUNT];
	char			fetch_buf[SGL_TEST_BUF_COUNT *
					  SGL_TEST_BUF_SIZE];
	char			ground_truth[SGL_TEST_BUF_COUNT *
					     SGL_TEST_BUF_SIZE];

	memset(&rex, 0, sizeof(rex));
	memset(&iod, 0, sizeof(iod));

	/* Set up dkey and akey */
	vts_key_gen(&dkey_buf[0], arg->dkey_size, true, arg);
	vts_key_gen(&akey_buf[0], arg->akey_size, false, arg);
	set_iov(&dkey, &dkey_buf[0], arg->ofeat & DAOS_OF_DKEY_UINT64);
	set_iov(&akey, &akey_buf[0], arg->ofeat & DAOS_OF_AKEY_UINT64);

	rex.rx_idx = hash_key(&dkey, arg->ofeat & DAOS_OF_DKEY_UINT64);
	rex.rx_nr = 1;

	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = SGL_TEST_BUF_COUNT * SGL_TEST_BUF_SIZE;
	iod.iod_name = akey;
	iod.iod_recxs = &rex;
	iod.iod_nr = 1;

	/* Allocate memory for the scatter-gather list */
	rc = d_sgl_init(&sgl, SGL_TEST_BUF_COUNT);
	assert_rc_equal(rc, 0);

	/* Allocate memory for the SGL_TEST_BUF_COUNT buffers */
	for (i = 0; i < SGL_TEST_BUF_COUNT; i++) {
		D_ALLOC_ARRAY(update_buffs[i], SGL_TEST_BUF_SIZE);
		assert_non_null(update_buffs[i]);
		/* Fill the buffer with random letters */
		dts_buf_render(update_buffs[i], SGL_TEST_BUF_SIZE);
		/* Set ground truth */
		memcpy(&ground_truth[i * SGL_TEST_BUF_SIZE], update_buffs[i],
			SGL_TEST_BUF_SIZE);
		/* Attach the buffer to the scatter-gather list */
		d_iov_set(&sgl.sg_iovs[i], update_buffs[i],
			SGL_TEST_BUF_SIZE);
	}

	/* Write/Update */
	rc = vos_obj_update(arg->ctx.tc_co_hdl, arg->oid, 1, 0, 0, &dkey, 1,
			    &iod, NULL, &sgl);
	d_sgl_fini(&sgl, true);

	if (rc) {
		print_error("Failed to update: "DF_RC"\n", DP_RC(rc));
		goto exit;
	}
	inc_cntr(arg->ta_flags);

	/* Now fetch */
	memset(fetch_buf, 0, SGL_TEST_BUF_COUNT * SGL_TEST_BUF_SIZE);
	rc = d_sgl_init(&sgl, 1);
	assert_rc_equal(rc, 0);
	d_iov_set(sgl.sg_iovs, &fetch_buf[0], SGL_TEST_BUF_COUNT *
		     SGL_TEST_BUF_SIZE);
	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, arg->oid, 1, 0, &dkey, 1, &iod,
			   &sgl);
	if (rc) {
		print_error("Failed to fetch: "DF_RC"\n", DP_RC(rc));
		goto exit;
	}
	d_sgl_fini(&sgl, false);
	/* Test if ground truth matches fetch_buf */
	assert_memory_equal(ground_truth, fetch_buf, SGL_TEST_BUF_COUNT *
			    SGL_TEST_BUF_SIZE);
exit:
	assert_rc_equal(rc, 0);
}

static void
io_sgl_fetch(void **state)
{
	/* This tests uses multiple buffers for the fetch/read */
	struct io_test_args	*arg = *state;
	int			rc = 0;
	int			i;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		rex;
	daos_iod_t		iod;
	d_sg_list_t		sgl;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	char			*fetch_buffs[SGL_TEST_BUF_COUNT];
	char			update_buf[SGL_TEST_BUF_COUNT *
					  SGL_TEST_BUF_SIZE];
	char			ground_truth[SGL_TEST_BUF_COUNT *
					     SGL_TEST_BUF_SIZE];

	memset(&rex, 0, sizeof(rex));
	memset(&iod, 0, sizeof(iod));

	/* Set up dkey and akey */
	vts_key_gen(&dkey_buf[0], arg->dkey_size, true, arg);
	vts_key_gen(&akey_buf[0], arg->akey_size, false, arg);
	set_iov(&dkey, &dkey_buf[0], arg->ofeat & DAOS_OF_DKEY_UINT64);
	set_iov(&akey, &akey_buf[0], arg->ofeat & DAOS_OF_AKEY_UINT64);

	rex.rx_idx = hash_key(&dkey, arg->ofeat & DAOS_OF_DKEY_UINT64);
	rex.rx_nr = 1;

	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = SGL_TEST_BUF_COUNT * SGL_TEST_BUF_SIZE;
	iod.iod_name = akey;
	iod.iod_recxs = &rex;
	iod.iod_nr = 1;

	/* Fill the buffer with random letters */
	dts_buf_render(&update_buf[0], SGL_TEST_BUF_COUNT * SGL_TEST_BUF_SIZE);
	/* Set ground truth */
	memcpy(&ground_truth[0], &update_buf[0], SGL_TEST_BUF_COUNT *
		SGL_TEST_BUF_SIZE);
	/* Attach the buffer to the scatter-gather list */
	d_sgl_init(&sgl, 1);
	d_iov_set(sgl.sg_iovs, &update_buf[0], SGL_TEST_BUF_COUNT *
		     SGL_TEST_BUF_SIZE);

	/* Write/Update */
	rc = vos_obj_update(arg->ctx.tc_co_hdl, arg->oid, 1, 0, 0, &dkey, 1,
			    &iod, NULL, &sgl);
	if (rc)
		goto exit;
	d_sgl_fini(&sgl, false);
	inc_cntr(arg->ta_flags);

	/* Allocate memory for the scatter-gather list */
	d_sgl_init(&sgl, SGL_TEST_BUF_COUNT);

	/* Allocate memory for the SGL_TEST_BUF_COUNT fetch buffers */
	for (i = 0; i < SGL_TEST_BUF_COUNT; i++) {
		D_ALLOC_ARRAY(fetch_buffs[i], SGL_TEST_BUF_SIZE);
		assert_non_null(fetch_buffs[i]);
		memset(fetch_buffs[i], 0, SGL_TEST_BUF_SIZE);
		/* Attach the buffer to the scatter-gather list */
		d_iov_set(&sgl.sg_iovs[i], fetch_buffs[i],
			SGL_TEST_BUF_SIZE);
	}
	/* Now fetch */
	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, arg->oid, 1, 0, &dkey, 1, &iod,
			   &sgl);
	if (rc)
		goto exit;
	/* Test if ground truth matches fetch_buffs */
	for (i = 0; i < SGL_TEST_BUF_COUNT; i++) {
		assert_memory_equal(&ground_truth[i * SGL_TEST_BUF_SIZE],
			fetch_buffs[i], SGL_TEST_BUF_SIZE);
	}
	d_sgl_fini(&sgl, true);
exit:
	assert_rc_equal(rc, 0);
}

static void
io_fetch_hole(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;
	d_iov_t		val_iov;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		rexs[3];
	daos_iod_t		iod;
	d_sg_list_t		sgl;
	char			dkey_buf[UPDATE_DKEY_SIZE];
	char			akey_buf[UPDATE_AKEY_SIZE];
	char			update_buf[3 * 1024];
	char			fetch_buf[3 * 1024];
	char			ground_truth[3 * 1024];

	memset(&rexs, 0, 2 * sizeof(daos_recx_t));
	memset(&iod, 0, sizeof(iod));
	memset(&sgl, 0, sizeof(sgl));

	/* Set up dkey and akey */
	vts_key_gen(&dkey_buf[0], arg->dkey_size, true, arg);
	vts_key_gen(&akey_buf[0], arg->akey_size, false, arg);
	set_iov(&dkey, &dkey_buf[0], arg->ofeat & DAOS_OF_DKEY_UINT64);
	set_iov(&akey, &akey_buf[0], arg->ofeat & DAOS_OF_AKEY_UINT64);

	/* Set up rexs */
	rexs[0].rx_idx = 0;
	rexs[0].rx_nr = 1024;
	rexs[1].rx_idx = 1024;
	rexs[1].rx_nr = 1024;
	rexs[2].rx_idx = 2 * 1024;
	rexs[2].rx_nr = 1024;

	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = 1;
	iod.iod_name = akey;
	iod.iod_recxs = rexs;
	iod.iod_nr = 3;

	/* Fill the update buffer */
	dts_buf_render(&update_buf[0], 3 * 1024);
	/* Set ground truth */
	memcpy(&ground_truth[0], &update_buf[0], 3 * 1024);

	/* Attach buffer to sgl */
	d_iov_set(&val_iov, &update_buf[0], 3 * 1024);
	sgl.sg_iovs = &val_iov;
	sgl.sg_nr = 1;

	/* Write/Update */
	rc = vos_obj_update(arg->ctx.tc_co_hdl, arg->oid, 1, 0, 0, &dkey, 1,
			    &iod, NULL, &sgl);
	assert_rc_equal(rc, 0);
	inc_cntr(arg->ta_flags);

	/* Fetch */
	d_iov_set(&val_iov, &fetch_buf[0], 3 * 1024);
	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, arg->oid, 1, 0, &dkey, 1, &iod,
			   &sgl);
	assert_rc_equal(rc, 0);

	assert_memory_equal(ground_truth, fetch_buf, 3 * 1024);

	/* Now just update the first and third extents */
	memset(update_buf, 0, 3 * 1024);
	/* This time only render enough for two extents */
	memset(&update_buf[0], 97, 1024); /* 97 = 'a' */
	memset(&update_buf[1024], 99, 1024); /* 99 = 'c' */
	update_buf[2047] = '\0';
	/* Update ground truth */
	memcpy(&ground_truth[0], &update_buf[0], 1024);
	memcpy(&ground_truth[2 * 1024], &update_buf[1024], 1024);

	/* Update the IOD */
	rexs[1].rx_idx = 2 * 1024;
	iod.iod_nr = 2;
	d_iov_set(&val_iov, &update_buf[0], 2 * 1024);
	sgl.sg_iovs = &val_iov;
	/* Update using epoch 2 */
	rc = vos_obj_update(arg->ctx.tc_co_hdl, arg->oid, 2, 0, 0, &dkey, 1,
			    &iod, NULL, &sgl);
	assert_rc_equal(rc, 0);

	/* Now fetch all three and test that the "hole" is untouched */
	rexs[0].rx_nr = 3 * 1024;
	iod.iod_nr = 1;
	memset(fetch_buf, 0, 3 * 1024);
	d_iov_set(&val_iov, &fetch_buf[0], 3 * 1024);
	/* Fetch using epoch 2 */
	rc = vos_obj_fetch(arg->ctx.tc_co_hdl, arg->oid, 2, 0, &dkey, 1, &iod,
			   &sgl);
	assert_rc_equal(rc, 0);

	/* Test if ground truth matches fetch_buf */
	assert_memory_equal(ground_truth, fetch_buf, 3 * 1024);
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
	for (i = 0; i < init_num_keys; i++) {
		rc = io_update_and_fetch_dkey(args, epoch, epoch);
		if (rc) {
			assert_rc_equal(rc, -DER_NOSPACE);
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
		oids[i] = gen_oid(arg->ofeat);

		rc = vos_oi_find_alloc(cont, oids[i], 1, true, &obj_df, NULL);
		assert_rc_equal(rc, 0);
	}
	return 0;
}

static void
oid_iter_test_base(void **state, unsigned int flags)
{
	struct io_test_args	*arg = *state;
	int			 rc;

	arg->ta_flags = flags;
	rc = io_oid_iter_test(arg);
	assert_true(rc == 0 || rc == -DER_NONEXIST);
}

static void
oid_iter_test(void **state)
{
	oid_iter_test_base(state, NO_FLAGS);
}

static void
oid_iter_test_with_anchor(void **state)
{
	oid_iter_test_base(state, TF_IT_ANCHOR);
}

/* Enough keys to span multiple bytes to test integer key sort order */
#define NUM_KEYS	15
#define KEY_INC		127
#define MAX_INT_KEY	(NUM_KEYS * KEY_INC)

static void gen_query_tree(struct io_test_args *arg, daos_unit_oid_t oid)
{
	daos_iod_t		iod = {0};
	d_sg_list_t		sgl = {0};
	daos_key_t		dkey;
	daos_key_t		akey;
	d_iov_t			val_iov;
	daos_recx_t		recx;
	daos_epoch_t		epoch = 1;
	uint64_t		dkey_value;
	uint64_t		akey_value;
	int			i, j;
	uint32_t		update_var = 0xdeadbeef;
	int			rc = 0;

	d_iov_set(&dkey, &dkey_value, sizeof(dkey_value));
	d_iov_set(&akey, &akey_value, sizeof(akey_value));

	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_name = akey;
	iod.iod_recxs = &recx;
	iod.iod_nr = 1;

	/* Attach buffer to sgl */
	d_iov_set(&val_iov, &update_var, sizeof(update_var));
	sgl.sg_iovs = &val_iov;
	sgl.sg_nr = 1;

	for (i = 1; i <= NUM_KEYS; i++) {
		for (j = 1; j <= NUM_KEYS; j++) {
			dkey_value = i * KEY_INC;
			akey_value = j * KEY_INC;
			iod.iod_size = sizeof(update_var);
			/* Set up rexs */
			recx.rx_idx = 0;
			recx.rx_nr = 1;

			rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, epoch++, 0,
					    0, &dkey, 1, &iod, NULL, &sgl);
			assert_rc_equal(rc, 0);

			recx.rx_idx = 1;
			rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, epoch++, 0,
					    0, &dkey, 1, &iod, NULL, &sgl);
			assert_rc_equal(rc, 0);

			recx.rx_idx = 2;
			rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, epoch++, 0,
					    0, &dkey, 1, &iod, NULL, &sgl);
			assert_rc_equal(rc, 0);

			recx.rx_idx = 1;
			recx.rx_nr = 2;
			iod.iod_size = 0; /* punch */
			rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, epoch++, 0,
					    0, &dkey, 1, &iod, NULL, &sgl);
			assert_rc_equal(rc, 0);
		}
	}

	/* One extra punch of all records at last akey in second to last dkey
	 * Checked in io_query_key
	 */
	recx.rx_idx = 0;
	recx.rx_nr = 100;
	iod.iod_size = 0; /* punch */
	dkey_value = MAX_INT_KEY - KEY_INC;
	akey_value = MAX_INT_KEY;
	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, epoch++, 0, 0, &dkey, 1,
			    &iod, NULL, &sgl);
	assert_rc_equal(rc, 0);

}

static void
io_query_key(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;
	int			i, j;
	struct dtx_handle	*dth;
	struct dtx_id		xid;
	daos_epoch_t		epoch = 1;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_key_t		dkey_read;
	daos_key_t		akey_read;
	daos_recx_t		recx_read;
	daos_unit_oid_t		oid;
	uint64_t		dkey_value;
	uint64_t		akey_value;

	d_iov_set(&dkey, &dkey_value, sizeof(dkey_value));
	d_iov_set(&akey, &akey_value, sizeof(akey_value));

	oid = gen_oid(arg->ofeat);

	gen_query_tree(arg, oid);

	for (i = 1; i <= NUM_KEYS; i++) {
		for (j = 1; j <= NUM_KEYS; j++) {
			dkey_value = i * KEY_INC;
			akey_value = j * KEY_INC;

			rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
					       DAOS_GET_MAX | DAOS_GET_RECX,
					       epoch + 3, &dkey, &akey,
					       &recx_read, NULL);
			assert_rc_equal(rc, 0);
			assert_int_equal(recx_read.rx_idx, 0);
			assert_int_equal(recx_read.rx_nr, 1);

			/* Read before punch */
			rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
					       DAOS_GET_MAX | DAOS_GET_RECX,
					       epoch + 2, &dkey, &akey,
					       &recx_read, NULL);
			assert_rc_equal(rc, 0);
			assert_int_equal(recx_read.rx_idx, 2);
			assert_int_equal(recx_read.rx_nr, 1);

			rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
					       DAOS_GET_DKEY | DAOS_GET_AKEY |
					       DAOS_GET_MAX | DAOS_GET_RECX,
					       epoch + 3, &dkey_read,
					       &akey_read, &recx_read, NULL);
			assert_rc_equal(rc, 0);
			assert_int_equal(recx_read.rx_idx, 0);
			assert_int_equal(recx_read.rx_nr, 1);
			assert_int_equal(*(uint64_t *)dkey_read.iov_buf,
					 dkey_value);
			assert_int_equal(*(uint64_t *)akey_read.iov_buf,
					 akey_value);

			/* Read before punch */
			rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
					       DAOS_GET_DKEY | DAOS_GET_AKEY |
					       DAOS_GET_MAX | DAOS_GET_RECX,
					       epoch + 2, &dkey_read,
					       &akey_read, &recx_read, NULL);
			assert_rc_equal(rc, 0);
			assert_int_equal(recx_read.rx_idx, 2);
			assert_int_equal(recx_read.rx_nr, 1);
			assert_int_equal(*(uint64_t *)dkey_read.iov_buf,
					 dkey_value);
			assert_int_equal(*(uint64_t *)akey_read.iov_buf,
					 akey_value);

			rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
					       DAOS_GET_MIN | DAOS_GET_RECX,
					       epoch + 3, &dkey, &akey,
					       &recx_read, NULL);
			assert_rc_equal(rc, 0);
			assert_int_equal(recx_read.rx_idx, 0);
			assert_int_equal(recx_read.rx_nr, 1);

			/* Read before punch */
			rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
					       DAOS_GET_MIN | DAOS_GET_RECX,
					       epoch + 2, &dkey, &akey,
					       &recx_read, NULL);
			assert_rc_equal(rc, 0);
			assert_int_equal(recx_read.rx_idx, 0);
			assert_int_equal(recx_read.rx_nr, 1);

			rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
					       DAOS_GET_DKEY | DAOS_GET_AKEY |
					       DAOS_GET_MIN | DAOS_GET_RECX,
					       epoch + 3, &dkey_read,
					       &akey_read, &recx_read, NULL);
			assert_rc_equal(rc, 0);
			assert_int_equal(recx_read.rx_idx, 0);
			assert_int_equal(recx_read.rx_nr, 1);
			assert_int_equal(*(uint64_t *)dkey_read.iov_buf,
					 KEY_INC);
			assert_int_equal(*(uint64_t *)akey_read.iov_buf,
					 KEY_INC);

			/* Read before punch */
			rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
					       DAOS_GET_DKEY | DAOS_GET_AKEY |
					       DAOS_GET_MIN | DAOS_GET_RECX,
					       epoch + 2, &dkey_read,
					       &akey_read, &recx_read, NULL);
			assert_rc_equal(rc, 0);
			assert_int_equal(recx_read.rx_idx, 0);
			assert_int_equal(recx_read.rx_nr, 1);
			assert_int_equal(*(uint64_t *)dkey_read.iov_buf,
					 KEY_INC);
			assert_int_equal(*(uint64_t *)akey_read.iov_buf,
					 KEY_INC);

			epoch += 4;
		}
	}

	epoch++; /* Extra punch in gen_query_tree */

	/* Now punch the first and last akey */
	akey_value = MAX_INT_KEY;
	dkey_value = MAX_INT_KEY;
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch++, 0, 0, &dkey, 1,
			   &akey, NULL);
	assert_rc_equal(rc, 0);

	akey_value = KEY_INC;
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch++, 0, 0, &dkey, 1,
			   &akey, NULL);
	assert_rc_equal(rc, 0);

	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid, DAOS_GET_AKEY |
			       DAOS_GET_MIN, epoch++, &dkey, &akey_read, NULL,
			       NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(*(uint64_t *)akey_read.iov_buf, KEY_INC * 2);

	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid, DAOS_GET_AKEY |
			       DAOS_GET_MAX, epoch++, &dkey, &akey_read, NULL,
			       NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(*(uint64_t *)akey_read.iov_buf, MAX_INT_KEY - KEY_INC);

	/* Punch all of the akeys in last dkey */
	for (i = 2; i < NUM_KEYS; i++) {
		akey_value = i * KEY_INC;
		rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch++, 0, 0,
				   &dkey, 1, &akey, NULL);
		assert_rc_equal(rc, 0);
	}
	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid, DAOS_GET_AKEY |
			       DAOS_GET_MAX, epoch++, &dkey, &akey_read, NULL,
			       NULL);
	assert_rc_equal(rc, -DER_NONEXIST);

	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid, DAOS_GET_AKEY |
			       DAOS_GET_DKEY | DAOS_GET_MAX, epoch++,
			       &dkey_read, &akey_read, NULL, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(*(uint64_t *)akey_read.iov_buf, MAX_INT_KEY);
	assert_int_equal(*(uint64_t *)dkey_read.iov_buf, MAX_INT_KEY - KEY_INC);

	/* Now check the extra punch from gen_query_tree */
	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid, DAOS_GET_AKEY |
			       DAOS_GET_DKEY | DAOS_GET_RECX | DAOS_GET_MAX,
			       epoch++, &dkey_read, &akey_read,
			       &recx_read, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(*(uint64_t *)akey_read.iov_buf, MAX_INT_KEY - KEY_INC);
	assert_int_equal(*(uint64_t *)dkey_read.iov_buf, MAX_INT_KEY - KEY_INC);
	assert_int_equal(recx_read.rx_nr, 1);
	assert_int_equal(recx_read.rx_idx, 0);

	/* Now punch the first and last dkey */
	dkey_value = MAX_INT_KEY;
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch++, 0, 0, &dkey, 0,
			   NULL, NULL);
	assert_rc_equal(rc, 0);

	dkey_value = KEY_INC;
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch++, 0, 0, &dkey, 0,
			   NULL, NULL);
	assert_rc_equal(rc, 0);

	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid, DAOS_GET_DKEY |
			       DAOS_GET_MIN, epoch++, &dkey_read, NULL, NULL,
			       NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(*(uint64_t *)dkey_read.iov_buf, KEY_INC * 2);

	if (getenv("DAOS_IO_BYPASS"))
		return;

	/* Only execute the transactional tests when rollback is available */

	vts_dtx_begin(&oid, arg->ctx.tc_co_hdl, epoch++, 0, &dth);

	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid, DAOS_GET_DKEY |
			       DAOS_GET_MAX, 0 /* Ignored epoch */, &dkey_read,
			       NULL, NULL, dth);
	assert_rc_equal(rc, 0);
	assert_int_equal(*(uint64_t *)dkey_read.iov_buf, MAX_INT_KEY - KEY_INC);

	vts_dtx_end(dth);

	/* Now punch the object at earlier epoch */
	vts_dtx_begin(&oid, arg->ctx.tc_co_hdl, epoch - 3, 0, &dth);
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, 0 /* ignored epoch */, 0, 0,
			   NULL, 0, NULL, dth);
	assert_rc_equal(rc, -DER_TX_RESTART);
	vts_dtx_end(dth);

	vts_dtx_begin(&oid, arg->ctx.tc_co_hdl, epoch + 1, 0, &dth);
	/* Now punch the object */
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch++, 0, 0, NULL, 0,
			   NULL, dth);
	assert_rc_equal(rc, 0);
	xid = dth->dth_xid;
	vts_dtx_end(dth);

	rc = vos_dtx_commit(arg->ctx.tc_co_hdl, &xid, 1, NULL);
	assert_rc_equal(rc, 1);

	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid, DAOS_GET_DKEY |
			       DAOS_GET_MAX, epoch++, &dkey_read, NULL, NULL,
			       NULL);
	assert_rc_equal(rc, -DER_NONEXIST);
}

static void
update_dkey(void **state, daos_unit_oid_t oid, daos_epoch_t epoch,
	    uint64_t dkey_value, const char *val)
{
	struct io_test_args	*arg = *state;
	daos_iod_t		iod = {0};
	d_sg_list_t		sgl = {0};
	daos_key_t		dkey;
	daos_key_t		akey;
	d_iov_t			val_iov;
	daos_recx_t		recx;
	uint64_t		akey_value = 0;
	int			rc = 0;

	d_iov_set(&dkey, &dkey_value, sizeof(dkey_value));
	d_iov_set(&akey, &akey_value, sizeof(akey_value));

	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_name = akey;
	iod.iod_recxs = &recx;
	iod.iod_nr = 1;

	/* Attach buffer to sgl */
	d_iov_set(&val_iov, &val, strnlen(val, 32) + 1);
	sgl.sg_iovs = &val_iov;
	sgl.sg_nr = 1;

	iod.iod_size = 1;
	/* Set up rexs */
	recx.rx_idx = 0;
	recx.rx_nr = val_iov.iov_len;

	rc = vos_obj_update(arg->ctx.tc_co_hdl, oid, epoch++, 0, 0, &dkey, 1,
			    &iod, NULL, &sgl);
	assert_rc_equal(rc, 0);
}

static void
io_query_key_punch_update(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;
	daos_epoch_t		epoch = 1;
	daos_key_t		dkey;
	daos_key_t		akey;
	daos_recx_t		recx_read;
	daos_unit_oid_t		oid;
	uint64_t		dkey_value;
	uint64_t		akey_value = 0;

	d_iov_set(&akey, &akey_value, sizeof(akey_value));

	oid = gen_oid(arg->ofeat);

	update_dkey(state, oid, epoch++, 0, "World");
	update_dkey(state, oid, epoch++, 12, "Goodbye");

	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
			       DAOS_GET_MAX | DAOS_GET_DKEY | DAOS_GET_RECX,
			       epoch++, &dkey, &akey, &recx_read, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(recx_read.rx_idx, 0);
	assert_int_equal(recx_read.rx_nr, sizeof("Goodbye"));
	assert_int_equal(*(uint64_t *)dkey.iov_buf, 12);

	/* Now punch the last dkey */
	dkey_value = 12;
	d_iov_set(&dkey, &dkey_value, sizeof(dkey_value));
	rc = vos_obj_punch(arg->ctx.tc_co_hdl, oid, epoch++, 0, 0, &dkey, 0,
			   NULL, NULL);
	assert_rc_equal(rc, 0);

	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
			       DAOS_GET_MAX | DAOS_GET_DKEY | DAOS_GET_RECX,
			       epoch++, &dkey, &akey, &recx_read, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(recx_read.rx_idx, 0);
	assert_int_equal(recx_read.rx_nr, sizeof("World"));
	assert_int_equal(*(uint64_t *)dkey.iov_buf, 0);

	/* Ok, now update the last one again */
	update_dkey(state, oid, epoch++, 12, "Hello");

	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
			       DAOS_GET_MAX | DAOS_GET_DKEY | DAOS_GET_RECX,
			       epoch++, &dkey, &akey, &recx_read, NULL);
	assert_rc_equal(rc, 0);
	assert_int_equal(recx_read.rx_nr, sizeof("Hello"));
	assert_int_equal(recx_read.rx_idx, 0);
	assert_int_equal(*(uint64_t *)dkey.iov_buf, 12);
}

static void
io_query_key_negative(void **state)
{
	struct io_test_args	*arg = *state;
	int			rc = 0;
	daos_key_t		dkey_read;
	daos_key_t		akey_read;
	daos_recx_t		recx_read;
	daos_unit_oid_t		oid;

	oid = gen_oid(arg->ofeat);

	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
			       DAOS_GET_DKEY | DAOS_GET_AKEY |
			       DAOS_GET_MAX | DAOS_GET_RECX, 4,
			       &dkey_read, &akey_read,
			       &recx_read, NULL);
	assert_rc_equal(rc, -DER_NONEXIST);

	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, oid,
			       DAOS_GET_DKEY | DAOS_GET_AKEY |
			       DAOS_GET_MIN | DAOS_GET_RECX, 4,
			       &dkey_read, &akey_read,
			       &recx_read, NULL);
	assert_rc_equal(rc, -DER_NONEXIST);

	gen_query_tree(arg, oid);

	rc = vos_obj_query_key(arg->ctx.tc_co_hdl, arg->oid,
			       DAOS_GET_DKEY | DAOS_GET_MAX, 4,
			       NULL, NULL, NULL, NULL);
	assert_rc_equal(rc, -DER_INVAL);
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
	{ "VOS206: Simple scatter-gather list test, multiple update buffers",
		io_sgl_update, NULL, NULL},
	{ "VOS207: Simple scatter-gather list test, multiple fetch buffers",
		io_sgl_fetch, NULL, NULL},
	{ "VOS208: Extent hole test",
		io_fetch_hole, NULL, NULL},
	{ "VOS220: 100K update/fetch/verify test",
		io_multiple_dkey, NULL, NULL},
	{ "VOS222: overwrite test",
		io_idx_overwrite, NULL, NULL},
	{ "VOS240.0: KV Iter tests (for dkey)",
		io_iter_test, NULL, NULL},
	{ "VOS240.1: KV Iter tests with anchor (for dkey)",
		io_iter_test_with_anchor, NULL, NULL},
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

static const struct CMUnitTest int_tests[] = {
	{ "VOS300.1: Test key query punch with subsequent update",
		io_query_key_punch_update, NULL, NULL},
	{ "VOS300.2: Key query test", io_query_key, NULL, NULL},
	{ "VOS300.3: Key query negative test",
		io_query_key_negative, NULL, NULL},
};

int
run_io_test(daos_ofeat_t feats, int keys, bool nest_iterators, const char *cfg)
{
	char buf[VTS_BUF_SIZE];
	const char *akey = "hashed";
	const char *dkey = "hashed";
	vts_nest_iterators = nest_iterators;
	int rc = 0;

	init_num_keys = VTS_IO_KEYS;

	feats = feats & DAOS_OF_MASK;
	if ((feats & DAOS_OF_DKEY_UINT64) && (feats & DAOS_OF_DKEY_LEXICAL)) {
		D_PRINT("Skipping ambiguous ofeat mask\n");
		return 0;
	}
	if ((feats & DAOS_OF_AKEY_UINT64) && (feats & DAOS_OF_AKEY_LEXICAL)) {
		D_PRINT("Skipping ambiguous ofeat mask\n");
		return 0;
	}

	if (feats & DAOS_OF_DKEY_UINT64)
		dkey = "uint";
	if (feats & DAOS_OF_DKEY_LEXICAL)
		dkey = "lex";
	if (feats & DAOS_OF_AKEY_UINT64)
		akey = "uint";
	if (feats & DAOS_OF_AKEY_LEXICAL)
		akey = "lex";

	snprintf(buf, VTS_BUF_SIZE, "# VOS IO tests (dkey=%-6s akey=%s) %s",
		 dkey, akey, cfg);
	init_ofeats = feats;
	if (keys)
		init_num_keys = keys;
	D_PRINT("Running %s\n", buf);
	if ((feats & DAOS_OF_DKEY_UINT64) && (feats & DAOS_OF_AKEY_UINT64)) {
		buf[0] = '2';
		rc = cmocka_run_group_tests_name(buf, int_tests, setup_io,
						 teardown_io);
	}
	buf[0] = '1';

	return rc + cmocka_run_group_tests_name(buf, io_tests,
						setup_io, teardown_io);
}
