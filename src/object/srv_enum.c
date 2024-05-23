/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * Server-side enumeration routines.
 */
#define D_LOGFAC DD_FAC(object)

#include <daos_srv/vos.h>
#include <daos_srv/object.h>
#include <daos/object.h>

#include "obj_internal.h"

static int
fill_recxs(daos_handle_t ih, vos_iter_entry_t *key_ent,
	   struct ds_obj_enum_arg *arg, vos_iter_type_t type)
{
	/* check if recxs is full */
	if (arg->recxs_len >= arg->recxs_cap) {
		D_DEBUG(DB_IO, "recx_len %d recx_cap %d\n",
			arg->recxs_len, arg->recxs_cap);
		return 1;
	}

	if (arg->eprs_len >= arg->eprs_cap) {
		D_DEBUG(DB_IO, "eprs_len %d eprs_cap %d\n",
			arg->eprs_len, arg->eprs_cap);
		return 1;
	}

	arg->eprs[arg->eprs_len].epr_lo = key_ent->ie_epoch;
	arg->eprs[arg->eprs_len].epr_hi = DAOS_EPOCH_MAX;
	arg->eprs_len++;

	arg->recxs[arg->recxs_len] = key_ent->ie_recx;
	arg->recxs_len++;
	if (arg->rsize == 0) {
		arg->rsize = key_ent->ie_rsize;
	} else if (arg->rsize != key_ent->ie_rsize) {
		D_ERROR("different size "DF_U64" != "DF_U64"\n", arg->rsize,
			key_ent->ie_rsize);
		return -DER_INVAL;
	}

	D_DEBUG(DB_IO, "Pack recxs "DF_U64"/"DF_U64" recxs_len %d size "
		DF_U64"\n", key_ent->ie_recx.rx_idx, key_ent->ie_recx.rx_nr,
		arg->recxs_len, arg->rsize);

	arg->rnum++;
	return 0;
}

static int
is_sgl_full(struct ds_obj_enum_arg *arg, daos_size_t size)
{
	d_sg_list_t *sgl = arg->sgl;

	/* Find available iovs in sgl
	 * XXX this is buggy because key descriptors require keys are stored
	 * in sgl in the same order as descriptors, but it's OK for now because
	 * we only use one IOV.
	 */
	while (arg->sgl_idx < sgl->sg_nr) {
		d_iov_t *iovs = sgl->sg_iovs;

		if (iovs[arg->sgl_idx].iov_len + size >
		    iovs[arg->sgl_idx].iov_buf_len) {
			D_DEBUG(DB_IO, "current %dth iov buf is full"
				" iov_len %zd size "DF_U64" buf_len %zd\n",
				arg->sgl_idx, iovs[arg->sgl_idx].iov_len, size,
				iovs[arg->sgl_idx].iov_buf_len);
			arg->sgl_idx++;
			continue;
		}
		break;
	}

	/* Update sg_nr_out */
	if (arg->sgl_idx < sgl->sg_nr && sgl->sg_nr_out < arg->sgl_idx + 1)
		sgl->sg_nr_out = arg->sgl_idx + 1;

	/* Check if the sgl is full */
	if (arg->sgl_idx >= sgl->sg_nr) {
		D_DEBUG(DB_IO, "full sgl %d/%d size " DF_U64"\n", arg->sgl_idx,
			sgl->sg_nr, size);
		return 1;
	}

	return 0;
}

int
fill_oid(daos_unit_oid_t oid, struct ds_obj_enum_arg *arg)
{
	d_iov_t *iov;

	if (arg->size_query) {
		arg->kds_len++;
		arg->kds[0].kd_key_len += sizeof(oid);
		if (arg->kds_len >= arg->kds_cap)
			return 1;
		return 0;
	}

	/* Check if sgl or kds is full */
	if (is_sgl_full(arg, sizeof(oid)) || arg->kds_len >= arg->kds_cap)
		return 1;

	iov = &arg->sgl->sg_iovs[arg->sgl_idx];
	/* Append a new descriptor to kds. */
	memset(&arg->kds[arg->kds_len], 0, sizeof(arg->kds[arg->kds_len]));
	arg->kds[arg->kds_len].kd_key_len = sizeof(oid);
	arg->kds[arg->kds_len].kd_val_type =
				vos_iter_type_2pack_type(VOS_ITER_OBJ);
	arg->kds_len++;

	/* Append the object ID to iov. */
	daos_iov_append(iov, &oid, sizeof(oid));
	D_DEBUG(DB_IO, "Pack obj "DF_UOID" iov_len/sgl %zu/%d kds_len %d\n",
		DP_UOID(oid), iov->iov_len, arg->sgl_idx, arg->kds_len);
	return 0;
}

static int
fill_obj(daos_handle_t ih, vos_iter_entry_t *entry, struct ds_obj_enum_arg *arg,
	 vos_iter_type_t vos_type)
{
	int rc;

	D_ASSERTF(vos_type == VOS_ITER_OBJ, "%d\n", vos_type);

	rc = fill_oid(entry->ie_oid, arg);

	return rc;
}

/** Fill the arg.csum information and iov with what's in the entry */
static int
fill_data_csum(struct dcs_csum_info *src_csum_info, d_iov_t *csum_iov)
{
	int			 rc;

	if (csum_iov  == NULL || !ci_is_valid(src_csum_info))
		return 0;

	/** This must be freed by the object layer
	 * (currently in obj_enum_complete)
	 */
	rc = iov_alloc_for_csum_info(csum_iov, src_csum_info);
	if (rc != 0)
		return rc;

	rc = ci_serialize(src_csum_info, csum_iov);
	/** iov_alloc_for_csum_info should have allocated enough so this
	 * would be a programmer error and want to know right away
	 */
	D_ASSERT(rc == 0);

	return 0;
}

/**
 * Key's don't have checksums stored so key_ent won't have a valid
 * checksum and must rely on csummer to calculate a new one.
 */
static int
fill_key_csum(vos_iter_entry_t *key_ent, struct ds_obj_enum_arg *arg)
{
	struct daos_csummer	*csummer = arg->csummer;
	d_iov_t			*csum_iov = &arg->csum_iov;
	struct dcs_csum_info	*csum_info;
	int			 rc;

	if (!daos_csummer_initialized(csummer) || csummer->dcs_skip_key_calc)
		return 0;

	rc = daos_csummer_calc_key(csummer, &key_ent->ie_key, &csum_info);
	if (rc != 0)
		return rc;

	/** This must be freed by the object layer
	 * (currently in obj_enum_complete)
	 */
	iov_alloc_for_csum_info(csum_iov, csum_info);
	rc = ci_serialize(csum_info, csum_iov);
	/** iov_alloc_for_csum_info should have allocated enough so this
	 * would be a programmer error and want to know right away
	 */
	D_ASSERT(rc == 0);
	daos_csummer_free_ci(csummer, &csum_info);

	return 0;
}

static int
fill_key(daos_handle_t ih, vos_iter_entry_t *key_ent, struct ds_obj_enum_arg *arg,
	 vos_iter_type_t vos_type)
{
	d_iov_t		*iov;
	daos_size_t	 total_size;
	int		 type;
	int		 kds_cap;
	int		 rc;

	D_ASSERT(vos_type == VOS_ITER_DKEY || vos_type == VOS_ITER_AKEY);

	total_size = key_ent->ie_key.iov_len;
	if (key_ent->ie_punch)
		total_size += sizeof(key_ent->ie_punch);

	type = vos_iter_type_2pack_type(vos_type);
	/* for tweaking kds_len in fill_rec() */
	arg->last_type = type;

	/* Check if sgl or kds is full */
	if (arg->need_punch && key_ent->ie_punch != 0)
		kds_cap = arg->kds_cap - 1; /* one extra kds for punch eph */
	else
		kds_cap = arg->kds_cap;
	if (type == OBJ_ITER_DKEY && arg->need_punch &&
	    key_ent->ie_obj_punch != 0 && !arg->obj_punched)
		kds_cap--;                  /* extra kds for obj punch eph */

	if (arg->size_query) {
		arg->kds_len++;
		arg->kds[0].kd_key_len += total_size;
		if (arg->kds_len >= kds_cap)
			return 1;
		return 0;
	}

	if (is_sgl_full(arg, total_size) || arg->kds_len >= kds_cap) {
		/* NB: if it is rebuild object iteration, let's
		 * check if both dkey & akey was already packed
		 * (kds_len < 2) before return KEY2BIG.
		 */
		if (arg->kds_len == 0 ||
		    (arg->chk_key2big && arg->kds_len <= 2)) {
			if (arg->kds[0].kd_key_len < total_size)
				arg->kds[0].kd_key_len = total_size;
			return -DER_KEY2BIG;
		} else {
			return 1;
		}
	}

	iov = &arg->sgl->sg_iovs[arg->sgl_idx];

	if (type == OBJ_ITER_DKEY && key_ent->ie_obj_punch && arg->need_punch &&
	    !arg->obj_punched) {
		int pi_size = sizeof(key_ent->ie_obj_punch);

		arg->kds[arg->kds_len].kd_key_len = pi_size;
		arg->kds[arg->kds_len].kd_val_type = OBJ_ITER_OBJ_PUNCH_EPOCH;
		arg->kds_len++;

		D_ASSERT(iov->iov_len + pi_size <= iov->iov_buf_len);
		memcpy(iov->iov_buf + iov->iov_len, &key_ent->ie_obj_punch,
		       pi_size);

		iov->iov_len += pi_size;
		arg->obj_punched = true;
	}

	D_ASSERT(arg->kds_len < arg->kds_cap);
	arg->kds[arg->kds_len].kd_key_len = key_ent->ie_key.iov_len;
	arg->kds[arg->kds_len].kd_val_type = type;
	rc = fill_key_csum(key_ent, arg);
	if (rc != 0)
		return rc;
	arg->kds_len++;

	daos_iov_append(iov, key_ent->ie_key.iov_buf, key_ent->ie_key.iov_len);

	if (key_ent->ie_punch != 0 && arg->need_punch) {
		int pi_size = sizeof(key_ent->ie_punch);

		arg->kds[arg->kds_len].kd_key_len = pi_size;
		if (type == OBJ_ITER_AKEY)
			arg->kds[arg->kds_len].kd_val_type =
						OBJ_ITER_AKEY_EPOCH;
		else
			arg->kds[arg->kds_len].kd_val_type =
						OBJ_ITER_DKEY_EPOCH;
		arg->kds_len++;

		D_ASSERT(iov->iov_len + pi_size <= iov->iov_buf_len);
		memcpy(iov->iov_buf + iov->iov_len, &key_ent->ie_punch,
		       pi_size);

		iov->iov_len += pi_size;
	}

	D_DEBUG(DB_IO, "Pack key "DF_KEY" iov total %zd kds len %d eph "
		DF_U64" punched eph num "DF_U64"\n", DP_KEY(&key_ent->ie_key),
		iov->iov_len, arg->kds_len - 1, key_ent->ie_epoch,
		key_ent->ie_punch);
	return 0;
}

static bool
recx_eq(const daos_recx_t *a, const daos_recx_t *b)
{
	D_ASSERT(a != NULL);
	D_ASSERT(b != NULL);

	return a->rx_nr == b->rx_nr && a->rx_idx == b->rx_idx;
}

static bool
entry_is_partial_extent(const vos_iter_entry_t *key_ent)
{
	D_ASSERT(key_ent != NULL);

	return !recx_eq(&key_ent->ie_orig_recx, &key_ent->ie_recx);
}

static int
csummer_verify_recx(struct daos_csummer *csummer, d_iov_t *data_to_verify,
		    daos_recx_t *recx, daos_size_t rsize,
		    struct dcs_csum_info *csum_info)
{
	int			rc;
	struct dcs_iod_csums	iod_csum = {0};
	daos_iod_t		iod = {0};
	d_sg_list_t		sgl = {0};

	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_recxs = recx;
	iod.iod_nr = 1;
	iod.iod_size = rsize;

	sgl.sg_iovs = data_to_verify;
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;

	iod_csum.ic_nr = 1;
	iod_csum.ic_data = csum_info;

	rc = daos_csummer_verify_iod(csummer, &iod, &sgl,
				     &iod_csum, NULL, 0, NULL);
	if (rc != 0)
		D_ERROR("Corruption found for recx "DF_RECX"\n",
			DP_RECX(*recx));

	return rc;
}

static int
csummer_alloc_csum_info(struct daos_csummer *csummer,
			daos_recx_t *recx, daos_size_t rsize,
			struct dcs_csum_info **csum_info)
{
	daos_size_t		 chunksize;
	daos_size_t		 csum_nr;
	struct dcs_csum_info	*result = NULL;
	uint16_t		 csum_len;

	D_ASSERT(recx != NULL);
	D_ASSERT(csum_info != NULL);
	D_ASSERT(csummer != NULL);
	D_ASSERT(rsize > 0);

	csum_len = daos_csummer_get_csum_len(csummer);
	chunksize = daos_csummer_get_rec_chunksize(csummer, rsize);
	csum_nr = daos_recx_calc_chunks(*recx, rsize, chunksize);

	D_ALLOC(result, sizeof(*result) + csum_len * csum_nr);
	if (result == NULL)
		return -DER_NOMEM;

	result->cs_csum = (uint8_t *)&result[1];
	result->cs_type = daos_csummer_get_type(csummer);
	result->cs_chunksize = chunksize;
	result->cs_nr = csum_nr;
	result->cs_len = csum_len;
	result->cs_buf_len = csum_len * csum_nr;

	(*csum_info) = result;

	return 0;
}

/**
 * Allocate memory for the csum_info struct and buffer for actual checksum, then
 * calculate the checksum.
 */
static int
csummer_alloc_calc_recx_csum(struct daos_csummer *csummer, daos_recx_t *recx,
			     daos_size_t rsize, d_iov_t *data,
			     struct dcs_csum_info **p_csum_info)
{
	int rc;
	d_sg_list_t sgl;
	struct dcs_csum_info *csum_info;

	rc = csummer_alloc_csum_info(csummer, recx, rsize, &csum_info);
	if (rc != 0)
		return rc;

	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = data;

	rc = daos_csummer_calc_one(csummer, &sgl, csum_info, rsize,
				   recx->rx_nr, recx->rx_idx);

	if (rc != 0) {
		D_ERROR("Error calculating checksum: "DF_RC"\n", DP_RC(rc));
		daos_csummer_free_ci(csummer, &csum_info);
		return rc;
	}

	(*p_csum_info) = csum_info;
	return 0;
}

/**
 * If the entry's extent is a partial extent, then calculate a new checksum for
 * it and verify the original extent. Otherwise just pack the existing checksum
 * into the output buffer
 */
static int
csum_copy_inline(int type, vos_iter_entry_t *ent, struct ds_obj_enum_arg *arg,
		 daos_handle_t ih, d_iov_t *iov_out)
{
	int rc;

	D_ASSERT(arg != NULL);
	D_ASSERT(ent != NULL);
	D_ASSERT(iov_out != NULL);

	if (type == OBJ_ITER_RECX && entry_is_partial_extent(ent) &&
	    daos_csummer_initialized(arg->csummer)) {
		struct daos_csummer	*csummer = arg->csummer;
		struct dcs_csum_info	*new_csum_info = NULL;
		vos_iter_entry_t	 ent_to_verify = *ent;
		d_iov_t			 data_to_verify = {0};
		uint64_t		 orig_data_len = 0;

		/**
		 * Verify original extent
		 * First, make a copy of the entity and update the copy to read
		 * all data that will be verified.
		 */
		orig_data_len = ent->ie_orig_recx.rx_nr * ent->ie_rsize;
		ent_to_verify.ie_recx = ent->ie_orig_recx;
		ent_to_verify.ie_biov.bi_data_len = orig_data_len;
		ent_to_verify.ie_biov.bi_addr.ba_off -=
			ent->ie_recx.rx_idx -
			ent->ie_orig_recx.rx_idx;

		D_ALLOC(data_to_verify.iov_buf, orig_data_len);
		if (data_to_verify.iov_buf == NULL)
			return -DER_NOMEM;

		data_to_verify.iov_buf_len = orig_data_len;

		rc = arg->copy_data_cb(ih, &ent_to_verify, &data_to_verify);
		if (rc != 0) {
			D_ERROR("Issue copying data\n");
			return rc;
		}

		rc = csummer_verify_recx(csummer,
					 &data_to_verify,
					 &ent_to_verify.ie_orig_recx,
					 ent_to_verify.ie_rsize,
					 &ent_to_verify.ie_csum);

		D_FREE(data_to_verify.iov_buf);
		if (rc != 0) {
			D_ERROR("Found corruption!\n");
			return rc;
		}

		rc = csummer_alloc_calc_recx_csum(csummer, &ent->ie_recx,
						  ent->ie_rsize, iov_out,
						  &new_csum_info);
		if (rc != 0) {
			D_ERROR("Issue calculating checksum\n");
			return rc;
		}

		rc = fill_data_csum(new_csum_info, &arg->csum_iov);
		daos_csummer_free_ci(csummer, &new_csum_info);
		if (rc != 0) {
			D_ERROR("Issue filling csum data\n");
			return rc;
		}
	} else {
		rc = fill_data_csum(&ent->ie_csum, &arg->csum_iov);
		if (rc != 0) {
			D_ERROR("Issue filling csum data\n");
			return rc;
		}
	}

	return 0;
}

static bool
need_new_entry(struct ds_obj_enum_arg *arg, vos_iter_entry_t *key_ent,
	       daos_size_t iod_size, int type)
{
	struct obj_enum_rec	*rec;
	d_iov_t			*iovs = arg->sgl->sg_iovs;
	uint64_t		curr_off = key_ent->ie_recx.rx_idx;
	uint64_t		curr_size = key_ent->ie_recx.rx_nr;
	uint64_t		prev_off;
	uint64_t		prev_size;

	if (arg->last_type != OBJ_ITER_RECX || type != OBJ_ITER_RECX)
		return true;

	rec = iovs[arg->sgl_idx].iov_buf + iovs[arg->sgl_idx].iov_len - sizeof(*rec);
	prev_off = rec->rec_recx.rx_idx;
	prev_size = rec->rec_recx.rx_nr;
	if (prev_off + prev_size != curr_off) /* not continuous */
		return true;

	if (arg->rsize != iod_size)
		return true;

	if (arg->ec_cell_sz > 0 &&
	    (prev_off + prev_size - 1) / arg->ec_cell_sz !=
	    (curr_off + curr_size) / arg->ec_cell_sz)
		return true;

	return false;
}

static void
insert_new_rec(struct ds_obj_enum_arg *arg, vos_iter_entry_t *new_ent, int type,
	       daos_size_t iod_size, struct obj_enum_rec **new_rec)
{
	d_iov_t			*iovs = arg->sgl->sg_iovs;
	struct obj_enum_rec	*rec;
	daos_size_t		new_idx = new_ent->ie_recx.rx_idx;
	daos_off_t		new_nr = new_ent->ie_recx.rx_nr;

	/* For cross-cell recx, let's check if the new recx needs to merge with current
	 * recx, then insert the left to the new recx.
	 */
	if (arg->last_type == OBJ_ITER_RECX && type == OBJ_ITER_RECX &&
	    arg->ec_cell_sz > 0 && arg->rsize == iod_size) {
		rec = iovs[arg->sgl_idx].iov_buf + iovs[arg->sgl_idx].iov_len - sizeof(*rec);
		*new_rec = rec;
		if (rec->rec_recx.rx_idx + rec->rec_recx.rx_nr == new_ent->ie_recx.rx_idx) {
			new_idx = roundup(DAOS_RECX_END(rec->rec_recx), arg->ec_cell_sz);
			if (new_idx > new_ent->ie_recx.rx_idx) {
				new_nr -= new_idx - new_ent->ie_recx.rx_idx;
				rec->rec_recx.rx_nr += new_ent->ie_recx.rx_nr - new_nr;
				rec->rec_epr.epr_lo = max(new_ent->ie_epoch, rec->rec_epr.epr_lo);
			}
			if (new_nr == 0)
				return;
		}
	}

	/* Grow the next new descriptor (instead of creating yet a new one). */
	arg->kds[arg->kds_len].kd_val_type = type;
	arg->kds[arg->kds_len].kd_key_len += sizeof(*rec);
	rec = iovs[arg->sgl_idx].iov_buf + iovs[arg->sgl_idx].iov_len;
	/* Append the recx record to iovs. */
	D_ASSERT(iovs[arg->sgl_idx].iov_len + sizeof(*rec) <= iovs[arg->sgl_idx].iov_buf_len);
	rec->rec_recx.rx_idx = new_idx;
	rec->rec_recx.rx_nr = new_nr;
	rec->rec_size = iod_size;
	rec->rec_epr.epr_lo = new_ent->ie_epoch;
	rec->rec_epr.epr_hi = DAOS_EPOCH_MAX;
	rec->rec_version = new_ent->ie_ver;
	rec->rec_flags = 0;
	iovs[arg->sgl_idx].iov_len += sizeof(*rec);
	arg->rsize = iod_size;
	*new_rec = rec;
}

/* Callers are responsible for incrementing arg->kds_len. See iter_akey_cb. */
static int
fill_rec(daos_handle_t ih, vos_iter_entry_t *key_ent, struct ds_obj_enum_arg *arg,
	 vos_iter_type_t vos_type, vos_iter_param_t *param, unsigned int *acts)
{
	d_iov_t			*iovs = arg->sgl->sg_iovs;
	daos_size_t		data_size = 0, iod_size;
	struct obj_enum_rec	*rec;
	daos_size_t		size = sizeof(*rec);
	bool			inline_data = false, bump_kds_len = false;
	bool			insert_new_entry = false;
	int			type;
	int			rc = 0;

	D_ASSERT(vos_type == VOS_ITER_SINGLE || vos_type == VOS_ITER_RECX);
	type = vos_iter_type_2pack_type(vos_type);

	/* Client needs zero iod_size to tell a punched record */
	if (bio_addr_is_hole(&key_ent->ie_biov.bi_addr)) {
		iod_size = 0;
	} else {
		if (type == OBJ_ITER_SINGLE) {
			iod_size = key_ent->ie_gsize;
			if (iod_size == key_ent->ie_rsize)
				data_size = iod_size;
		} else {
			iod_size = key_ent->ie_rsize;
		}
	}

	/* Inline the data? A 0 threshold disables this completely. */
	/*
	 * FIXME: transferring data from NVMe will yield, current recursive
	 * enum pack implementation doesn't support yield & re-probe.
	 */
	if (arg->inline_thres > 0 && data_size <= arg->inline_thres &&
	    data_size > 0 && bio_iov2media(&key_ent->ie_biov) != DAOS_MEDIA_NVME) {
		inline_data = true;
		size += data_size;
	}

	/*
	 * Tweak the kds_len, kds_len is increased by 1 for each
	 * dkey, akey, evtree, SV tree.
	 */
	if (arg->last_type == type) {
		D_ASSERT(arg->kds_len > 0);
		arg->kds_len--;
		bump_kds_len = true;
	}

	if (arg->size_query) {
		arg->kds_len++;
		arg->kds[0].kd_key_len += size;
		if (arg->kds_len >= arg->kds_cap)
			return 1;
		return 0;
	}

	insert_new_entry = need_new_entry(arg, key_ent, iod_size, type);
	if (insert_new_entry) {
		/* Check if there are still space */
		if (is_sgl_full(arg, size) || arg->kds_len >= arg->kds_cap) {
			/* NB: if it is rebuild object iteration, let's
			 * check if there are any recxs being packed, otherwise
			 * it will need return -DER_KEY2BIG to re-allocate
			 * the buffer and retry.
			 */
			if (arg->chk_key2big &&
			    (arg->kds_len < 3 || (arg->kds_len == 3 && !bump_kds_len))) {
				if (arg->kds[0].kd_key_len < size)
					arg->kds[0].kd_key_len = size;
				D_GOTO(out, rc = -DER_KEY2BIG);
			}
			D_GOTO(out, rc = 1);
		} else {
			insert_new_rec(arg, key_ent, type, iod_size, &rec);
		}
	} else {
		D_ASSERTF(arg->last_type == OBJ_ITER_RECX, "type=%d\n", arg->last_type);
		D_ASSERTF(type == OBJ_ITER_RECX, "type=%d\n", type);
		rec = iovs[arg->sgl_idx].iov_buf + iovs[arg->sgl_idx].iov_len - sizeof(*rec);
		rec->rec_recx.rx_nr += key_ent->ie_recx.rx_nr;
		rec->rec_epr.epr_lo = max(key_ent->ie_epoch, rec->rec_epr.epr_lo);
	}

	/*
	 * If we've decided to inline the data, append the data to iovs.
	 * NB: Punched recxs do not have any data to copy.
	 */
	if (inline_data && data_size > 0) {
		d_iov_t iov_out;

		/* For SV case, inline data must be located on SCM.
		 * For EV case, the inline data may be only part of
		 * the original extent. The other part(s) of the EV
		 * may be invisible to current enumeration. Then it
		 * may be located on SCM or NVMe.
		 */
		D_ASSERT(type != OBJ_ITER_RECX);
		D_ASSERTF(key_ent->ie_biov.bi_addr.ba_type ==
			  DAOS_MEDIA_SCM, "Invalid storage media type %d, ba_off "
			  DF_X64", thres %ld, data_size %ld, type %d, iod_size %ld\n",
			  key_ent->ie_biov.bi_addr.ba_type, key_ent->ie_biov.bi_addr.ba_off,
			  arg->inline_thres, data_size, type, iod_size);

		d_iov_set(&iov_out, iovs[arg->sgl_idx].iov_buf +
				       iovs[arg->sgl_idx].iov_len, data_size);
		D_ASSERT(arg->copy_data_cb != NULL);

		rc = csum_copy_inline(type, key_ent, arg, ih, &iov_out);
		if (rc != 0) {
			D_ERROR("Issue copying csum\n");
			return rc;
		}

		rc = arg->copy_data_cb(ih, key_ent, &iov_out);

		if (rc != 0) {
			D_ERROR("Copy recx data failed "DF_RC"\n", DP_RC(rc));
		} else {
			rec->rec_flags |= RECX_INLINE;
			iovs[arg->sgl_idx].iov_len += data_size;
			arg->kds[arg->kds_len].kd_key_len += data_size;
		}
	}

	D_DEBUG(DB_IO, "Pack rec "DF_U64"/"DF_U64
		" rsize "DF_U64" ver %u kd_len "DF_U64" type %d sgl_idx %d/%zd "
		"kds_len %d inline "DF_U64" epr "DF_U64"/"DF_U64"\n",
		key_ent->ie_recx.rx_idx, key_ent->ie_recx.rx_nr,
		rec->rec_size, rec->rec_version,
		arg->kds[arg->kds_len].kd_key_len, type, arg->sgl_idx,
		iovs[arg->sgl_idx].iov_len, arg->kds_len,
		rec->rec_flags & RECX_INLINE ? data_size : 0,
		rec->rec_epr.epr_lo, rec->rec_epr.epr_hi);

	if (arg->last_type != type) {
		arg->last_type = type;
		bump_kds_len = true;
	}
out:
	if (bump_kds_len)
		arg->kds_len++;
	return rc;
}

static int
enum_pack_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
	     vos_iter_param_t *param, void *cb_arg, unsigned int *acts)
{
	int	rc;

	switch (type) {
	case VOS_ITER_OBJ:
		rc = fill_obj(ih, entry, cb_arg, type);
		break;
	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
		rc = fill_key(ih, entry, cb_arg, type);
		break;
	case VOS_ITER_SINGLE:
	case VOS_ITER_RECX:
		if (((struct ds_obj_enum_arg *)cb_arg)->fill_recxs)
			rc = fill_recxs(ih, entry, cb_arg, type);
		else
			rc = fill_rec(ih, entry, cb_arg, type, param, acts);
		break;
	default:
		D_ASSERTF(false, "unknown/unsupported type %d\n", type);
		rc = -DER_INVAL;
	}

	return rc;
}

/**
 * Enumerate VOS objects, dkeys, akeys, and/or recxs and pack them into a set
 * of buffers.
 *
 * The buffers must be provided by the caller. They may contain existing data,
 * in which case this function appends to them.
 *
 * \param[in]		param		iteration parameters
 * \param[in]		type		iteration type
 * \param[in]		recursive	iterate to next level recursively
 * \param[in]		anchors		iteration anchors
 * \param[in,out]	arg		enumeration argument
 * \param[in]		dth		DTX handle
 *
 * \retval		0	enumeration complete
 * \retval		1	buffer(s) full
 * \retval		-DER_*	error
 */
int
ds_obj_enum_pack(vos_iter_param_t *param, vos_iter_type_t type, bool recursive,
		 struct vos_iter_anchors *anchors, struct ds_obj_enum_arg *arg,
		 enum_iterate_cb_t iter_cb, struct dtx_handle *dth)
{
	int	rc;

	D_ASSERT(!arg->fill_recxs ||
		 type == VOS_ITER_SINGLE || type == VOS_ITER_RECX);

	rc = iter_cb(param, type, recursive, anchors, enum_pack_cb, NULL,
		     arg, dth);

	D_DEBUG(DB_IO, "enum type %d rc %d\n", type, rc);
	return rc;
}
