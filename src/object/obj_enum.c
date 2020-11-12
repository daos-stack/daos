/*
 * (C) Copyright 2018-2020 Intel Corporation.
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
 * \file
 *
 * Enumeration pack & unpack object.
 */
#define D_LOGFAC DD_FAC(object)

#include <daos_srv/daos_server.h>
#include <daos_srv/vos.h>
#include <daos/object.h>

static struct dcs_iod_csums *
io_iod_csums(const struct dss_enum_unpack_io *io, int i)
{
	if (io->ui_iods_csums != NULL)
		return &io->ui_iods_csums[i];
	return NULL;
}

static int
fill_recxs(daos_handle_t ih, vos_iter_entry_t *key_ent,
	   struct dss_enum_arg *arg, vos_iter_type_t type)
{
	/* check if recxs is full */
	if (arg->recxs_len >= arg->recxs_cap) {
		D_DEBUG(DB_IO, "recx_len %d recx_cap %d\n",
			arg->recxs_len, arg->recxs_cap);
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
is_sgl_full(struct dss_enum_arg *arg, daos_size_t size)
{
	d_sg_list_t *sgl = arg->sgl;

	/* Find available iovs in sgl
	 * XXX this is buggy because key descriptors require keys are stored
	 * in sgl in the same order as descriptors, but it's OK for now because
	 * we only use one IOV.
	 */
	while (arg->sgl_idx < sgl->sg_nr) {
		d_iov_t *iovs = sgl->sg_iovs;

		if (iovs[arg->sgl_idx].iov_len + size >=
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
fill_oid(daos_unit_oid_t oid, struct dss_enum_arg *arg)
{
	d_iov_t *iov;

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
fill_obj(daos_handle_t ih, vos_iter_entry_t *entry, struct dss_enum_arg *arg,
	 vos_iter_type_t vos_type)
{
	int rc;

	D_ASSERTF(vos_type == VOS_ITER_OBJ, "%d\n", vos_type);

	rc = fill_oid(entry->ie_oid, arg);

	return rc;
}

static int
iov_alloc_for_csum_info(d_iov_t *iov, struct dcs_csum_info *csum_info)
{
	size_t size_needed = ci_size(*csum_info);

	/** Make sure the csum buffer is big enough ... resize if needed */
	if (iov->iov_buf == NULL) {
		/** This must be freed by the object layer
		 * (currently in obj_enum_complete)
		 */
		D_ALLOC(iov->iov_buf, size_needed);
		if (iov->iov_buf == NULL)
			return -DER_NOMEM;
		iov->iov_buf_len = size_needed;
		iov->iov_len = 0;
	} else if (iov->iov_len + size_needed > iov->iov_buf_len) {
		void	*p;
		size_t	 new_size = max(iov->iov_buf_len * 2,
					      iov->iov_len + size_needed);

		D_REALLOC(p, iov->iov_buf, new_size);
		if (p == NULL)
			return -DER_NOMEM;
		iov->iov_buf = p;
		iov->iov_buf_len = new_size;
	}

	return 0;
}

/** Fill the arg.csum information and iov with what's in the entry */
static int
fill_data_csum(struct dcs_csum_info *src_csum_info, d_iov_t *csum_iov)
{
	int			 rc;

	if (csum_iov  == NULL || !ci_is_valid(src_csum_info))
		return 0;

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
fill_key_csum(vos_iter_entry_t *key_ent, struct dss_enum_arg *arg)
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
fill_key(daos_handle_t ih, vos_iter_entry_t *key_ent, struct dss_enum_arg *arg,
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

		D_ASSERT(iov->iov_len + pi_size < iov->iov_buf_len);
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

/* Callers are responsible for incrementing arg->kds_len. See iter_akey_cb. */
static int
fill_rec(daos_handle_t ih, vos_iter_entry_t *key_ent, struct dss_enum_arg *arg,
	 vos_iter_type_t vos_type, vos_iter_param_t *param, unsigned int *acts)
{
	d_iov_t			*iovs = arg->sgl->sg_iovs;
	struct obj_enum_rec	*rec;
	daos_size_t		data_size = 0, iod_size;
	daos_size_t		size = sizeof(*rec);
	bool			inline_data = false, bump_kds_len = false;
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
			else
				data_size = 0;
		} else {
			iod_size = key_ent->ie_rsize;
			data_size = iod_size * key_ent->ie_recx.rx_nr;
		}
	}

	/* Inline the data? A 0 threshold disables this completely. */
	if (arg->inline_thres > 0 && data_size <= arg->inline_thres &&
	    data_size > 0) {
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

	fill_data_csum(&key_ent->ie_csum, &arg->csum_iov);

	if (is_sgl_full(arg, size) || arg->kds_len >= arg->kds_cap) {
		/* NB: if it is rebuild object iteration, let's
		 * check if both dkey & akey was already packed
		 * (kds_len < 3) before return KEY2BIG.
		 */
		if ((arg->chk_key2big && arg->kds_len < 3)) {
			if (arg->kds[0].kd_key_len < size)
				arg->kds[0].kd_key_len = size;
			D_GOTO(out, rc = -DER_KEY2BIG);
		}
		D_GOTO(out, rc = 1);
	}

	/* Grow the next new descriptor (instead of creating yet a new one). */
	arg->kds[arg->kds_len].kd_val_type = type;
	arg->kds[arg->kds_len].kd_key_len += sizeof(*rec);

	/* Append the recx record to iovs. */
	D_ASSERT(iovs[arg->sgl_idx].iov_len + sizeof(*rec) <
		 iovs[arg->sgl_idx].iov_buf_len);
	rec = iovs[arg->sgl_idx].iov_buf + iovs[arg->sgl_idx].iov_len;
	rec->rec_recx = key_ent->ie_recx;
	rec->rec_size = iod_size;
	rec->rec_epr.epr_lo = key_ent->ie_epoch;
	rec->rec_epr.epr_hi = DAOS_EPOCH_MAX;
	rec->rec_version = key_ent->ie_ver;
	rec->rec_flags = 0;
	iovs[arg->sgl_idx].iov_len += sizeof(*rec);

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
		if (type != OBJ_ITER_RECX)
			D_ASSERTF(key_ent->ie_biov.bi_addr.ba_type ==
				  DAOS_MEDIA_SCM,
				  "Invalid storage media type %d, ba_off "
				  DF_X64", thres %ld, data_size %ld, type %d, "
				  "iod_size %ld\n",
				  key_ent->ie_biov.bi_addr.ba_type,
				  key_ent->ie_biov.bi_addr.ba_off,
				  arg->inline_thres, data_size, type, iod_size);

		d_iov_set(&iov_out, iovs[arg->sgl_idx].iov_buf +
				       iovs[arg->sgl_idx].iov_len, data_size);
		D_ASSERT(arg->copy_data_cb != NULL);
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
		" rsize "DF_U64" ver %u kd_len "DF_U64" type %d sgl_idx %d/%zd"
		"kds_len %d inline "DF_U64" epr "DF_U64"/"DF_U64"\n",
		key_ent->ie_recx.rx_idx, key_ent->ie_recx.rx_nr,
		key_ent->ie_rsize, rec->rec_version,
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
		if (((struct dss_enum_arg *)cb_arg)->fill_recxs)
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
dss_enum_pack(vos_iter_param_t *param, vos_iter_type_t type, bool recursive,
	      struct vos_iter_anchors *anchors, struct dss_enum_arg *arg,
	      enum_iterate_cb_t iter_cb, struct dtx_handle *dth)
{
	int	rc;

	D_ASSERT(!arg->fill_recxs ||
		 type == VOS_ITER_SINGLE || type == VOS_ITER_RECX);

	rc = iter_cb(param, type, recursive, anchors, enum_pack_cb, NULL,
		     arg, dth);

	D_DEBUG(DB_IO, "enum type %d rc "DF_RC"\n", type, DP_RC(rc));
	return rc;
}

static int
grow_array(void **arrayp, size_t elem_size, int old_len, int new_len)
{
	void *p;

	D_ASSERTF(old_len < new_len, "%d < %d\n", old_len, new_len);
	D_REALLOC(p, *arrayp, elem_size * new_len);
	if (p == NULL)
		return -DER_NOMEM;
	/* Until D_REALLOC does this, zero the new segment. */
	memset(p + elem_size * old_len, 0, elem_size * (new_len - old_len));
	*arrayp = p;
	return 0;
}

enum {
	UNPACK_COMPLETE_IO = 1,	/* Only finish current I/O */
	UNPACK_COMPLETE_IOD = 2,	/* Only finish current IOD */
};

static int
unpack_csum(d_iov_t *csum_iov, struct dcs_iod_csums *iod_csums)
{
	if (csum_iov != NULL && csum_iov->iov_buf) {
		/** unpack csums */
		struct dcs_csum_info *tmp_csum_info;

		ci_cast(&tmp_csum_info, csum_iov);
		if (tmp_csum_info == NULL)
			return 0;

		ci_move_next_iov(tmp_csum_info, csum_iov);
		iod_csums->ic_data[iod_csums->ic_nr] = *tmp_csum_info;
		/** will be freed in clear_iod_csum() */
		D_ALLOC(iod_csums->ic_data[iod_csums->ic_nr].cs_csum,
			ci_csums_len(*tmp_csum_info));
		if (iod_csums->ic_data[iod_csums->ic_nr].cs_csum == NULL)
			return -DER_NOMEM;
		memcpy(iod_csums->ic_data[iod_csums->ic_nr].cs_csum,
		       tmp_csum_info->cs_csum, ci_csums_len(*tmp_csum_info));

		iod_csums->ic_nr++;
	}

	return 0;
}

/* Parse recxs in <*data, len> and append them to iod and sgl. */
static int
unpack_recxs(daos_iod_t *iod, int *recxs_cap, daos_epoch_t *eph,
	     daos_epoch_t *min_eph, d_sg_list_t *sgl,
	     daos_key_desc_t *kds, void *data,
	     d_iov_t *csum_iov, struct dcs_iod_csums *iod_csums,
	     unsigned int type)
{
	struct obj_enum_rec	*rec = data;
	int			rc = 0;

	if (kds == NULL)
		return 0;

	if (iod->iod_nr == 0)
		iod->iod_type = type;

	/* If the arrays are full, grow them as if all the remaining
	 * recxs have no inline data.
	 */
	if (iod->iod_nr + 1 > *recxs_cap) {
		int cap = *recxs_cap + 32;

		rc = grow_array((void **)&iod->iod_recxs,
				sizeof(*iod->iod_recxs), *recxs_cap, cap);
		if (rc != 0)
			D_GOTO(out, rc);

		if (sgl != NULL) {
			rc = grow_array((void **)&sgl->sg_iovs,
					sizeof(*sgl->sg_iovs),
					*recxs_cap, cap);
			if (rc != 0)
				D_GOTO(out, rc);
		}

		/** will be freed with iod.recxs in clear_top_iod */
		if (csum_iov != NULL && csum_iov->iov_buf) {
			rc = grow_array((void **)&iod_csums->ic_data,
					sizeof(*iod_csums->ic_data),
					*recxs_cap, cap);
			if (rc != 0)
				D_GOTO(out, rc);
		}

		/* If we execute any of the three breaks above,
		 * *recxs_cap will be < the real capacities of some of
		 * the arrays. This is harmless, as it only causes the
		 * diff segment to be copied and zeroed unnecessarily
		 * next time we grow them.
		 */
		*recxs_cap = cap;
	}

	/* Get the max epoch for the current iod, might be used by
	 * punch rebuild, see rebuild_punch_one()
	 */
	if (*eph < rec->rec_epr.epr_lo)
		*eph = rec->rec_epr.epr_lo;

	if (*min_eph == 0 || rec->rec_epr.epr_lo < *min_eph)
		*min_eph = rec->rec_epr.epr_lo;

	iod->iod_recxs[iod->iod_nr] = rec->rec_recx;
	iod->iod_nr++;
	iod->iod_size = rec->rec_size;

	/* Append the data, if inline. */
	if (sgl != NULL && rec->rec_size > 0) {
		d_iov_t *iov = &sgl->sg_iovs[sgl->sg_nr];

		if (rec->rec_flags & RECX_INLINE) {
			d_iov_set(iov, data + sizeof(*rec), rec->rec_size *
					     rec->rec_recx.rx_nr);
		} else {
			d_iov_set(iov, NULL, 0);
		}

		rc = unpack_csum(csum_iov, iod_csums);
		if (rc != 0)
			return rc;
		sgl->sg_nr++;
		D_ASSERTF(sgl->sg_nr <= iod->iod_nr, "%u == %u\n",
			  sgl->sg_nr, iod->iod_nr);
	}

	D_DEBUG(DB_IO, "unpacked data %p idx/nr "DF_U64"/"DF_U64
		" ver %u eph "DF_U64" size %zd epr ["DF_U64"/"DF_U64"]\n",
		rec, iod->iod_recxs[iod->iod_nr - 1].rx_idx,
		iod->iod_recxs[iod->iod_nr - 1].rx_nr, rec->rec_version,
		*eph, iod->iod_size, rec->rec_epr.epr_lo, rec->rec_epr.epr_hi);

out:
	D_DEBUG(DB_IO, "unpacked nr %d version/type /%u/%d rc "DF_RC"\n",
		iod->iod_nr, rec->rec_version, iod->iod_type, DP_RC(rc));
	return rc;
}

/**
 * Initialize \a io with \a iods[\a iods_cap], \a recxs_caps[\a iods_cap], and
 * \a sgls[\a iods_cap].
 *
 * \param[in,out]	io		I/O descriptor
 * \param[in]		oid		oid
 * \param[in]		iods		daos_iod_t array
 * \param[in]		recxs_caps	recxs capacity array
 * \param[in]		sgls		optional sgl array for inline recxs
 * \param[in]		akey_ephs	akey punched ephs
 * \param[in]		rec_ephs	record punched ephs
 * \param[in]		iods_cap	maximal number of elements in \a iods,
 *					\a recxs_caps, \a sgls, and \a ephs
 */
static void
dss_enum_unpack_io_init(struct dss_enum_unpack_io *io, daos_unit_oid_t oid,
			daos_iod_t *iods, struct dcs_iod_csums *iods_csums,
			int *recxs_caps, d_sg_list_t *sgls,
			daos_epoch_t *akey_ephs, daos_epoch_t *rec_ephs,
			daos_epoch_t *rec_min_ephs, int iods_cap)
{
	memset(io, 0, sizeof(*io));

	D_ASSERTF(iods_cap > 0, "%d\n", iods_cap);
	io->ui_iods_cap = iods_cap;

	D_ASSERT(iods != NULL);
	memset(iods, 0, sizeof(*iods) * iods_cap);
	io->ui_iods = iods;

	D_ASSERT(iods_csums != NULL);
	memset(iods_csums, 0, sizeof(*iods_csums) * iods_cap);
	io->ui_iods_csums = iods_csums;

	D_ASSERT(recxs_caps != NULL);
	memset(recxs_caps, 0, sizeof(*recxs_caps) * iods_cap);
	io->ui_recxs_caps = recxs_caps;

	io->ui_iods_top = -1;
	if (sgls != NULL) {
		memset(sgls, 0, sizeof(*sgls) * iods_cap);
		io->ui_sgls = sgls;
	}

	if (akey_ephs != NULL) {
		memset(akey_ephs, 0, sizeof(*akey_ephs) * iods_cap);
		io->ui_akey_punch_ephs = akey_ephs;
	}

	if (rec_ephs != NULL) {
		memset(rec_ephs, 0, sizeof(*rec_ephs) * iods_cap);
		io->ui_rec_punch_ephs = rec_ephs;
	}

	if (rec_min_ephs != NULL) {
		memset(rec_min_ephs, 0, sizeof(*rec_min_ephs) * iods_cap);
		io->ui_rec_min_ephs = rec_min_ephs;
	}

}

static void
clear_iod(daos_iod_t *iod, d_sg_list_t *sgl, int *recxs_cap)
{
	daos_iov_free(&iod->iod_name);
	if (iod->iod_recxs != NULL)
		D_FREE(iod->iod_recxs);
	memset(iod, 0, sizeof(*iod));

	if (sgl != NULL) {
		if (sgl->sg_iovs != NULL)
			D_FREE(sgl->sg_iovs);
		memset(sgl, 0, sizeof(*sgl));
	}

	*recxs_cap = 0;
}
static void
clear_iod_csum(struct dcs_iod_csums *iod_csum)
{
	int i;

	if (iod_csum == NULL || iod_csum->ic_data == NULL)
		return;

	for (i = 0; i < iod_csum->ic_nr; i++)
		if (iod_csum->ic_data[i].cs_csum != NULL)
			D_FREE(iod_csum->ic_data->cs_csum);

	D_FREE(iod_csum->ic_data);
}

/**
 * Clear the iods/sgls in \a io.
 *
 * \param[in]	io	I/O descriptor
 */
static void
dss_enum_unpack_io_clear(struct dss_enum_unpack_io *io)
{
	int i;

	for (i = 0; i <= io->ui_iods_top; i++) {
		d_sg_list_t *sgl = NULL;

		if (io->ui_sgls != NULL)
			sgl = &io->ui_sgls[i];
		clear_iod_csum(io_iod_csums(io, i));
		clear_iod(&io->ui_iods[i], sgl, &io->ui_recxs_caps[i]);
	}

	if (io->ui_akey_punch_ephs)
		memset(io->ui_akey_punch_ephs, 0,
		       sizeof(daos_epoch_t) * io->ui_iods_cap);
	if (io->ui_rec_punch_ephs)
		memset(io->ui_rec_punch_ephs, 0,
		       sizeof(daos_epoch_t) * io->ui_iods_cap);
	io->ui_dkey_punch_eph = 0;
	io->ui_iods_top = -1;
	io->ui_version = 0;
	io->ui_type = 0;
}

/**
 * Finalize \a io. All iods/sgls must have already been cleared.
 *
 * \param[in]	io	I/O descriptor
 */
static void
dss_enum_unpack_io_fini(struct dss_enum_unpack_io *io)
{
	D_ASSERTF(io->ui_iods_top == -1, "%d\n", io->ui_iods_top);
	daos_iov_free(&io->ui_dkey);
}

static void
clear_top_iod(struct dss_enum_unpack_io *io)
{
	int idx = io->ui_iods_top;

	if (idx == -1)
		return;

	if (io->ui_iods[idx].iod_nr == 0) {
		d_sg_list_t *sgl = NULL;

		D_DEBUG(DB_IO, "iod without recxs: %d\n", idx);
		if (io->ui_sgls != NULL)
			sgl = &io->ui_sgls[idx];
		clear_iod_csum(io_iod_csums(io, idx));
		clear_iod(&io->ui_iods[idx], sgl, &io->ui_recxs_caps[idx]);
		io->ui_iods_top--;
	}
}

/* Close io, pass it to cb, and clear it. */
static int
complete_io(struct dss_enum_unpack_io *io, dss_enum_unpack_cb_t cb, void *arg)
{
	int rc = 0;

	if (io->ui_iods_top == -1) {
		D_DEBUG(DB_IO, "io empty\n");
		goto out;
	}

	/* in case there are some garbage */
	clear_top_iod(io);

	rc = cb(io, arg);
out:
	dss_enum_unpack_io_clear(io);
	return rc;
}

static int
next_iod(struct dss_enum_unpack_io *io, dss_enum_unpack_cb_t cb, void *cb_arg,
	 d_iov_t *iod_name);

/* complete the IO, and initialize the first IOD */
static int
complete_io_init_iod(struct dss_enum_unpack_io *io,
		     dss_enum_unpack_cb_t cb, void *cb_arg,
		     d_iov_t *new_iod_name)
{
	daos_iod_t	*top_iod;
	d_iov_t		iod_akey = { 0 };
	int		rc;

	if (io->ui_iods_top < 0)
		return 0;

	if (new_iod_name == NULL) {
		/* Keeps the original top iod_name for initializing the new
		 * IOD after complete.
		 */
		top_iod = &io->ui_iods[io->ui_iods_top];
		rc = daos_iov_copy(&iod_akey, &top_iod->iod_name);
		if (rc)
			D_GOTO(free, rc);
		new_iod_name = &iod_akey;
	}

	rc = complete_io(io, cb, cb_arg);
	if (rc)
		D_GOTO(free, rc);

	rc = next_iod(io, cb, cb_arg, new_iod_name);
	if (rc)
		D_GOTO(free, rc);
free:
	daos_iov_free(&iod_akey);
	return rc;
}

/*
 * Move to next iod of io.
 * If it contains recxs, append it to io by incrementing
 * io->ui_iods_top. If it doesn't contain any recx, clear it.
 */
static int
next_iod(struct dss_enum_unpack_io *io, dss_enum_unpack_cb_t cb, void *cb_arg,
	 d_iov_t *new_iod_name)
{
	int	idx;
	int	rc = 0;

	D_ASSERTF(io->ui_iods_cap > 0, "%d > 0\n", io->ui_iods_cap);

	/* Reclaim the top if needed */
	idx = io->ui_iods_top;
	if (idx != -1 && io->ui_iods[idx].iod_nr == 0)
		clear_top_iod(io);

	/* Reach the limit, complete the current IO */
	if (io->ui_iods_top == io->ui_iods_cap - 1)
		return complete_io_init_iod(io, cb, cb_arg, new_iod_name);

	io->ui_iods_top++;
	io->ui_rec_min_ephs[io->ui_iods_top] = 0;
	/* Init the iod_name of the new IOD */
	if (new_iod_name == NULL && idx != -1)
		new_iod_name = &io->ui_iods[idx].iod_name;
	if (new_iod_name != NULL)
		rc = daos_iov_copy(&io->ui_iods[io->ui_iods_top].iod_name,
				   new_iod_name);

	D_DEBUG(DB_IO, "move to top %d\n", io->ui_iods_top);

	return rc;
}

/**
 * Unpack dkey and akey
 */
static int
enum_unpack_key(daos_key_desc_t *kds, char *key_data,
		struct dss_enum_unpack_io *io, d_iov_t *csum_iov,
		dss_enum_unpack_cb_t cb, void *cb_arg)
{
	daos_key_t	key;
	int		rc = 0;

	D_ASSERT(kds->kd_val_type == OBJ_ITER_DKEY ||
		 kds->kd_val_type == OBJ_ITER_AKEY);

	if (csum_iov != NULL && csum_iov->iov_buf) {
		struct dcs_csum_info *csum_info;

		/** keys aren't stored or needed by the I/O (they will have
		 * already been verified at this point), so just move the iov
		 * along.
		 */
		ci_cast(&csum_info, csum_iov);
		ci_move_next_iov(csum_info, csum_iov);
	}

	key.iov_buf = key_data;
	key.iov_buf_len = kds->kd_key_len;
	key.iov_len = kds->kd_key_len;
	if (kds->kd_val_type == OBJ_ITER_AKEY &&
	    io->ui_dkey.iov_buf == NULL) {
		D_ERROR("No dkey for akey "DF_KEY" invalid buf.\n",
			DP_KEY(&key));
		return -DER_INVAL;
	}

	if (kds->kd_val_type == OBJ_ITER_DKEY) {
		if (io->ui_dkey.iov_len == 0) {
			rc = daos_iov_copy(&io->ui_dkey, &key);
		} else if (!daos_key_match(&io->ui_dkey, &key)) {
			/* Close current IOD if dkey are different */
			rc = complete_io(io, cb, cb_arg);
			if (rc != 0)
				return rc;

			/* Update to the new dkey */
			daos_iov_free(&io->ui_dkey);
			rc = daos_iov_copy(&io->ui_dkey, &key);
		}
		D_DEBUG(DB_IO, "process dkey "DF_KEY": rc "DF_RC"\n",
			DP_KEY(&key), DP_RC(rc));
		return rc;
	}

	D_DEBUG(DB_IO, "process akey " DF_KEY "\n",
		DP_KEY(&key));

	if (io->ui_iods_top == -1 ||
	    !daos_key_match(&io->ui_iods[io->ui_iods_top].iod_name, &key))
		/* empty io or current key does not match */
		rc = next_iod(io, cb, cb_arg, &key);

	return rc;
}

/**
 * Unpack punched epochs.
 */
static int
enum_unpack_punched_ephs(daos_key_desc_t *kds, char *data,
			 struct dss_enum_unpack_io *io)
{
	int idx;

	if (kds->kd_key_len != sizeof(daos_epoch_t))
		return -DER_INVAL;

	if (kds->kd_val_type == OBJ_ITER_DKEY_EPOCH) {
		memcpy(&io->ui_dkey_punch_eph, data, kds->kd_key_len);
		return 0;
	}

	if (io->ui_iods_top == -1) {
		D_ERROR("punched epoch for empty akey rc %d\n", -DER_INVAL);
		return -DER_INVAL;
	}

	idx = io->ui_iods_top;
	D_ASSERT(io->ui_akey_punch_ephs != NULL);
	memcpy(&io->ui_akey_punch_ephs[idx], data, kds->kd_key_len);

	return 0;
}

static int
enum_unpack_recxs(daos_key_desc_t *kds, void *data,
		  struct dss_enum_unpack_io *io, d_iov_t *csum_iov,
		  dss_enum_unpack_cb_t cb, void *cb_arg)
{
	daos_key_t		iod_akey = { 0 };
	daos_key_t		*dkey;
	struct obj_enum_rec	*rec = data;
	void			*ptr = data;
	int			top = io->ui_iods_top;
	daos_iod_t		*top_iod;
	unsigned int		type;
	int			rc = 0;

	if (top == -1)
		return -DER_INVAL;

	dkey = &io->ui_dkey;
	if (dkey->iov_len == 0) {
		rc = -DER_INVAL;
		D_ERROR("invalid list buf "DF_RC"\n", DP_RC(rc));
		D_GOTO(free, rc);
	}

	if (kds->kd_val_type == OBJ_ITER_SINGLE)
		type = DAOS_IOD_SINGLE;
	else
		type = DAOS_IOD_ARRAY;

	if (io->ui_type == 0)
		io->ui_type = type;

	if (io->ui_version == 0)
		io->ui_version = rec->rec_version;

	/* Check version/type first to see if the current IO should be complete.
	 * Only one version/type per VOS update.
	 */
	if (io->ui_version != rec->rec_version || io->ui_type != type) {
		D_DEBUG(DB_IO, "different version %u != %u or type %u != %u\n",
			io->ui_version, rec->rec_version, io->ui_type, type);

		rc = complete_io_init_iod(io, cb, cb_arg, NULL);
		if (rc)
			D_GOTO(free, rc);
	}

	top = io->ui_iods_top;
	top_iod = &io->ui_iods[top];
	if (top_iod->iod_nr > 0) {
		/* Move to next IOD for each single value. */
		if (type == DAOS_IOD_SINGLE)
			rc = next_iod(io, cb, cb_arg, &top_iod->iod_name);
		else if (top_iod->iod_size != rec->rec_size)
			rc = next_iod(io, cb, cb_arg, &top_iod->iod_name);
		if (rc)
			D_GOTO(free, rc);
	}

	top = io->ui_iods_top;
	rc = unpack_recxs(&io->ui_iods[top], &io->ui_recxs_caps[top],
			  &io->ui_rec_punch_ephs[top],
			  &io->ui_rec_min_ephs[top],
			  io->ui_sgls == NULL ?  NULL : &io->ui_sgls[top],
			  kds, ptr, csum_iov, &io->ui_iods_csums[top], type);
free:
	daos_iov_free(&iod_akey);
	D_DEBUG(DB_IO, "unpack recxs: "DF_RC"\n", DP_RC(rc));
	return rc;
}

static int
enum_unpack_oid(daos_key_desc_t *kds, void *data,
		struct dss_enum_unpack_io *io,
		dss_enum_unpack_cb_t cb, void *cb_arg)
{
	daos_unit_oid_t	*oid = data;
	int		 rc = 0;

	if (kds->kd_key_len != sizeof(*oid)) {
		D_ERROR("Invalid object ID size: "DF_U64
			" != %zu\n", kds->kd_key_len, sizeof(*oid));
		rc = -DER_INVAL;
		return rc;
	}

	if (daos_unit_oid_is_null(io->ui_oid)) {
		io->ui_oid = *oid;
	} else if (daos_unit_oid_compare(io->ui_oid, *oid) != 0) {
		rc = complete_io(io, cb, cb_arg);
		if (rc != 0)
			return rc;
		daos_iov_free(&io->ui_dkey);
		io->ui_oid = *oid;
	}

	D_DEBUG(DB_REBUILD, "process obj "DF_UOID"\n", DP_UOID(io->ui_oid));

	return rc;
}

struct io_unpack_arg {
	struct dss_enum_unpack_io	*io;
	dss_enum_unpack_cb_t		cb;
	d_iov_t				*csum_iov;
	void				*cb_arg;
};

static int
enum_obj_io_unpack_cb(daos_key_desc_t *kds, void *ptr, unsigned int size,
		      void *arg)
{
	struct io_unpack_arg		*unpack_arg = arg;
	struct dss_enum_unpack_io	*io = unpack_arg->io;
	int				rc;

	switch (kds->kd_val_type) {
	case OBJ_ITER_OBJ:
		rc = enum_unpack_oid(kds, ptr, io, unpack_arg->cb,
				     unpack_arg->cb_arg);
		break;
	case OBJ_ITER_DKEY:
	case OBJ_ITER_AKEY:
		rc = enum_unpack_key(kds, ptr, io, unpack_arg->csum_iov,
				     unpack_arg->cb, unpack_arg->cb_arg);
		break;
	case OBJ_ITER_RECX:
	case OBJ_ITER_SINGLE:
		rc = enum_unpack_recxs(kds, ptr, io, unpack_arg->csum_iov,
				       unpack_arg->cb, unpack_arg->cb_arg);
		break;
	case OBJ_ITER_DKEY_EPOCH:
	case OBJ_ITER_AKEY_EPOCH:
		rc = enum_unpack_punched_ephs(kds, ptr, io);
		break;
	default:
		D_ERROR("unknown kds type %d\n", kds->kd_val_type);
		rc = -DER_INVAL;
		break;
	}

	/* Complete the IO if it reaches to the limit */
	if (io->ui_iods_top == io->ui_iods_cap - 1) {
		rc = complete_io_init_iod(io, unpack_arg->cb,
					  unpack_arg->cb_arg, NULL);
		if (rc != 0)
			D_ERROR("complete io failed: rc "DF_RC"\n",
				DP_RC(rc));
	}

	return rc;
}

int
obj_enum_iterate(daos_key_desc_t *kdss, d_sg_list_t *sgl, int nr,
		 unsigned int type, obj_enum_process_cb_t cb,
		 void *cb_arg)
{
	char		*ptr;
	unsigned int	i;
	int		rc = 0;

	D_ASSERTF(sgl->sg_nr > 0, "%u\n", sgl->sg_nr);
	D_ASSERT(sgl->sg_iovs != NULL);
	ptr = sgl->sg_iovs[0].iov_buf;
	for (i = 0; i < nr; i++) {
		daos_key_desc_t *kds = &kdss[i];

		D_DEBUG(DB_REBUILD, "process %d type %d ptr %p len "DF_U64
			" total %zd\n", i, kds->kd_val_type, ptr,
			kds->kd_key_len, sgl->sg_iovs[0].iov_len);
		if (kds->kd_val_type == 0 ||
		    (kds->kd_val_type != type && type != -1)) {
			ptr += kds->kd_key_len;
			D_DEBUG(DB_REBUILD, "skip type/size %d/%zd\n",
				kds->kd_val_type, kds->kd_key_len);
			continue;
		}

		if (kds->kd_val_type == OBJ_ITER_RECX ||
		    kds->kd_val_type == OBJ_ITER_SINGLE) {
			char *end = ptr + kds->kd_key_len;
			char *data = ptr;

			while (data < end) {
				struct obj_enum_rec *rec;

				rec = (struct obj_enum_rec *)data;
				rc = cb(kds, data, sizeof(struct obj_enum_rec),
					cb_arg);
				if (rc < 0) /* normal case */
					break;
				if (rec->rec_flags & RECX_INLINE)
					data += sizeof(struct obj_enum_rec) +
							rec->rec_size *
							rec->rec_recx.rx_nr;
				else
					data += sizeof(struct obj_enum_rec);
			}
		} else {
			rc = cb(kds, ptr, kds->kd_key_len, cb_arg);
		}
		ptr += kds->kd_key_len;
		if (rc) {
			D_ERROR("iterate %dth failed: rc"DF_RC"\n", i,
				DP_RC(rc));
			break;
		}
	}

	D_DEBUG(DB_REBUILD, "process %d list buf rc "DF_RC"\n", nr, DP_RC(rc));

	return rc;
}

/**
 * Unpack the result of a dss_enum_pack enumeration into \a io, which can then
 * be used to issue a VOS update. \a arg->*_anchor are ignored currently. \a cb
 * will be called, for the caller to consume the recxs accumulated in \a io.
 *
 * \param[in]	oid	OID
 * \param[in]	kds	key description
 * \param[in]	kds_num	kds number
 * \param[in]	sgl	sgl
 * \param[in]	csum	checksum buffer
 * \param[in]	cb	callback
 * \param[in]	cb_arg	callback argument
 */
int
dss_enum_unpack(daos_unit_oid_t oid, daos_key_desc_t *kds, int kds_num,
		d_sg_list_t *sgl, d_iov_t *csum, dss_enum_unpack_cb_t cb,
		void *cb_arg)
{
	struct dss_enum_unpack_io	io = { 0 };
	daos_iod_t			iods[DSS_ENUM_UNPACK_MAX_IODS];
	struct dcs_iod_csums		iods_csums[DSS_ENUM_UNPACK_MAX_IODS];
	int				recxs_caps[DSS_ENUM_UNPACK_MAX_IODS];
	d_sg_list_t			sgls[DSS_ENUM_UNPACK_MAX_IODS];
	daos_epoch_t			ephs[DSS_ENUM_UNPACK_MAX_IODS];
	daos_epoch_t			rec_ephs[DSS_ENUM_UNPACK_MAX_IODS];
	daos_epoch_t			rec_min_ephs[DSS_ENUM_UNPACK_MAX_IODS];
	d_iov_t				csum_iov = { 0 };
	struct io_unpack_arg		unpack_arg;
	int				rc = 0;

	D_ASSERT(kds_num > 0);
	D_ASSERT(kds != NULL);
	dss_enum_unpack_io_init(&io, oid, iods, iods_csums, recxs_caps, sgls,
				ephs, rec_ephs, rec_min_ephs,
				DSS_ENUM_UNPACK_MAX_IODS);

	if (csum)
		csum_iov = *csum;
	D_ASSERTF(sgl->sg_nr > 0, "%u\n", sgl->sg_nr);
	D_ASSERT(sgl->sg_iovs != NULL);
	unpack_arg.cb = cb;
	unpack_arg.io = &io;
	unpack_arg.cb_arg = cb_arg;
	unpack_arg.csum_iov = &csum_iov;
	rc = obj_enum_iterate(kds, sgl, kds_num, -1, enum_obj_io_unpack_cb,
			      &unpack_arg);
	if (rc)
		D_GOTO(out, rc);

	if (io.ui_iods_top >= 0)
		rc = complete_io(&io, cb, cb_arg);

out:
	D_DEBUG(DB_REBUILD, "process list buf "DF_UOID" rc "DF_RC"\n",
		DP_UOID(io.ui_oid), DP_RC(rc));

	dss_enum_unpack_io_fini(&io);
	return rc;
}
