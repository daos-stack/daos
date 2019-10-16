/*
 * (C) Copyright 2018-2019 Intel Corporation.
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
 * server: Enumeration pack & unpack utilities
 */

#define D_LOGFAC DD_FAC(server)

#include <daos_srv/daos_server.h>
#include <daos_srv/vos.h>
#include <daos/object.h>
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

	D_DEBUG(DB_IO, "Pack recxs_eprs "DF_U64"/"DF_U64" recxs_len %d size "
		DF_U64"\n", key_ent->ie_recx.rx_idx, key_ent->ie_recx.rx_nr,
		arg->recxs_len, arg->rsize);

	arg->rnum++;
	return 0;
}

static int
is_sgl_kds_full(struct dss_enum_arg *arg, daos_size_t size)
{
	d_sg_list_t *sgl = arg->sgl;

	/* Find avaible iovs in sgl
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
	if (arg->sgl_idx >= sgl->sg_nr || arg->kds_len >= arg->kds_cap) {
		D_DEBUG(DB_IO, "sgl or kds full sgl %d/%d kds %d/%d size "
			DF_U64"\n", arg->sgl_idx, sgl->sg_nr,
			arg->kds_len, arg->kds_cap, size);
		return 1;
	}

	return 0;
}

static int
fill_obj(daos_handle_t ih, vos_iter_entry_t *entry, struct dss_enum_arg *arg,
	 vos_iter_type_t vos_type)
{
	d_iov_t *iovs = arg->sgl->sg_iovs;
	int type;

	D_ASSERTF(vos_type == VOS_ITER_OBJ, "%d\n", vos_type);

	if (is_sgl_kds_full(arg, sizeof(entry->ie_oid)))
		return 1;

	type = vos_iter_type_2pack_type(vos_type);
	/* Append a new descriptor to kds. */
	D_ASSERT(arg->kds_len < arg->kds_cap);
	memset(&arg->kds[arg->kds_len], 0, sizeof(arg->kds[arg->kds_len]));
	arg->kds[arg->kds_len].kd_key_len = sizeof(entry->ie_oid);
	arg->kds[arg->kds_len].kd_val_type = type;
	arg->kds_len++;

	/* Append the object ID to iovs. */
	D_ASSERT(iovs[arg->sgl_idx].iov_len + sizeof(entry->ie_oid) <
		 iovs[arg->sgl_idx].iov_buf_len);
	memcpy(iovs[arg->sgl_idx].iov_buf + iovs[arg->sgl_idx].iov_len,
	       &entry->ie_oid, sizeof(entry->ie_oid));
	iovs[arg->sgl_idx].iov_len += sizeof(entry->ie_oid);

	D_DEBUG(DB_IO, "Pack obj "DF_UOID" iov_len %zu kds_len %d\n",
		DP_UOID(entry->ie_oid), iovs[arg->sgl_idx].iov_len,
		arg->kds_len);
	return 0;
}

static int
fill_key(daos_handle_t ih, vos_iter_entry_t *key_ent, struct dss_enum_arg *arg,
	 vos_iter_type_t vos_type)
{
	d_iov_t		*iov;
	daos_size_t	total_size;
	int		type;

	D_ASSERT(vos_type == VOS_ITER_DKEY || vos_type == VOS_ITER_AKEY);

	total_size = key_ent->ie_key.iov_len;
	if (key_ent->ie_key_punch)
		total_size += sizeof(key_ent->ie_key_punch);

	type = vos_iter_type_2pack_type(vos_type);
	/* for tweaking kds_len in fill_rec() */
	arg->last_type = type;

	if (is_sgl_kds_full(arg, total_size)) {
		/* NB: if it is rebuild object iteration, let's
		 * check if both dkey & akey was already packed
		 * (kds_len < 2) before return KEY2BIG.
		 */
		if (arg->kds_len == 0 ||
		    (arg->chk_key2big && arg->kds_len < 2)) {
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
	arg->kds[arg->kds_len].kd_csum_len = 0;
	arg->kds[arg->kds_len].kd_val_type = type;
	arg->kds_len++;

	D_ASSERT(iov->iov_len + key_ent->ie_key.iov_len <
		 iov->iov_buf_len);
	memcpy(iov->iov_buf + iov->iov_len, key_ent->ie_key.iov_buf,
	       key_ent->ie_key.iov_len);

	iov->iov_len += key_ent->ie_key.iov_len;

	if (key_ent->ie_key_punch != 0) {
		int pi_size = sizeof(key_ent->ie_key_punch);

		arg->kds[arg->kds_len].kd_key_len = pi_size;
		arg->kds[arg->kds_len].kd_csum_len = 0;
		if (type == OBJ_ITER_AKEY)
			arg->kds[arg->kds_len].kd_val_type =
						OBJ_ITER_AKEY_EPOCH;
		else
			arg->kds[arg->kds_len].kd_val_type =
						OBJ_ITER_DKEY_EPOCH;
		arg->kds_len++;

		D_ASSERT(iov->iov_len + pi_size < iov->iov_buf_len);
		memcpy(iov->iov_buf + iov->iov_len, &key_ent->ie_key_punch,
		       pi_size);

		iov->iov_len += pi_size;
	}

	D_DEBUG(DB_IO, "Pack key "DF_KEY" iov total %zd kds len %d eph "
		DF_U64" punched eph num "DF_U64"\n", DP_KEY(&key_ent->ie_key),
		iov->iov_len, arg->kds_len - 1, key_ent->ie_epoch,
		key_ent->ie_key_punch);
	return 0;
}

/* Callers are responsible for incrementing arg->kds_len. See iter_akey_cb. */
static int
fill_rec(daos_handle_t ih, vos_iter_entry_t *key_ent, struct dss_enum_arg *arg,
	 vos_iter_type_t vos_type, vos_iter_param_t *param, unsigned int *acts)
{
	d_iov_t			*iovs = arg->sgl->sg_iovs;
	struct obj_enum_rec	*rec;
	daos_size_t		data_size, iod_size;
	daos_size_t		size = sizeof(*rec);
	bool			inline_data = false, bump_kds_len = false;
	int			type;
	int			rc = 0;

	D_ASSERT(vos_type == VOS_ITER_SINGLE || vos_type == VOS_ITER_RECX);
	type = vos_iter_type_2pack_type(vos_type);

	/* Client needs zero iod_size to tell a punched record */
	if (bio_addr_is_hole(&key_ent->ie_biov.bi_addr))
		iod_size = 0;
	else
		iod_size = key_ent->ie_rsize;

	/* Inline the data? A 0 threshold disables this completely. */
	data_size = iod_size * key_ent->ie_recx.rx_nr;
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

	if (is_sgl_kds_full(arg, size)) {
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

		/* inline packing for the small recx located on SCM */
		D_ASSERT(key_ent->ie_biov.bi_addr.ba_type == DAOS_MEDIA_SCM);

		d_iov_set(&iov_out, iovs[arg->sgl_idx].iov_buf +
				       iovs[arg->sgl_idx].iov_len, data_size);
		rc = vos_iter_copy(ih, key_ent, &iov_out);
		if (rc != 0) {
			D_ERROR("Copy recx data failed %d\n", rc);
		} else {
			rec->rec_flags |= RECX_INLINE;
			iovs[arg->sgl_idx].iov_len += data_size;
			arg->kds[arg->kds_len].kd_key_len += data_size;
		}
	}

	D_DEBUG(DB_IO, "Pack rec "DF_U64"/"DF_U64
		" rsize "DF_U64" ver %u kd_len "DF_U64" type %d sgl_idx %d "
		"kds_len %d inline "DF_U64" epr "DF_U64"/"DF_U64"\n",
		key_ent->ie_recx.rx_idx, key_ent->ie_recx.rx_nr,
		key_ent->ie_rsize, rec->rec_version,
		arg->kds[arg->kds_len].kd_key_len, type, arg->sgl_idx,
		arg->kds_len, rec->rec_flags & RECX_INLINE ? data_size : 0,
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
 *
 * \retval		0	enumeration complete
 * \retval		1	buffer(s) full
 * \retval		-DER_*	error
 */
int
dss_enum_pack(vos_iter_param_t *param, vos_iter_type_t type, bool recursive,
	      struct vos_iter_anchors *anchors, struct dss_enum_arg *arg)
{
	int	rc;

	D_ASSERT(!arg->fill_recxs ||
		 type == VOS_ITER_SINGLE || type == VOS_ITER_RECX);

	rc = vos_iterate(param, type, recursive, anchors, enum_pack_cb, arg);

	D_DEBUG(DB_IO, "enum type %d tag %d rc %d\n", type,
		dss_get_module_info()->dmi_tgt_id, rc);
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

/* Parse recxs in <*data, len> and append them to iod and sgl. */
static int
unpack_recxs(daos_iod_t *iod, int *recxs_cap, d_sg_list_t *sgl,
	     daos_key_t *akey, daos_key_desc_t *kds, void **data,
	     daos_size_t len, uint32_t *version)
{
	int rc = 0;
	int type;

	if (iod->iod_name.iov_len == 0)
		daos_iov_copy(&iod->iod_name, akey);
	else
		D_ASSERT(daos_key_match(&iod->iod_name, akey));

	if (kds == NULL)
		return 0;

	if (kds->kd_val_type == OBJ_ITER_SINGLE)
		type = DAOS_IOD_SINGLE;
	else
		type = DAOS_IOD_ARRAY;

	while (len > 0) {
		struct obj_enum_rec *rec = *data;
		daos_size_t len_bak = len;

		/* Every recx begins with an obj_enum_rec. */
		if (len < sizeof(*rec)) {
			D_ERROR("invalid recxs: <%p, %zu>\n", *data, len);
			rc = -DER_INVAL;
			break;
		}

		/* Check if the version is changing. */
		if (*version == 0) {
			*version = rec->rec_version;
		} else if (*version != rec->rec_version) {
			D_DEBUG(DB_REBUILD, "different version %u != %u\n",
				*version, rec->rec_version);
			rc = UNPACK_COMPLETE_IO;
			break;
		}

		if (iod->iod_nr > 0 &&
		    (iod->iod_type == DAOS_IOD_SINGLE ||
		     iod->iod_type != type || rec->rec_size == 0 ||
		     iod->iod_size == 0)) {
			rc = UNPACK_COMPLETE_IOD;
			break;
		}

		if (iod->iod_nr == 0)
			iod->iod_type = type;

		/* If the arrays are full, grow them as if all the remaining
		 * recxs have no inline data.
		 */
		if (iod->iod_nr + 1 > *recxs_cap) {
			int cap = *recxs_cap + len / sizeof(*rec);

			rc = grow_array((void **)&iod->iod_recxs,
					sizeof(*iod->iod_recxs), *recxs_cap,
					cap);
			if (rc != 0)
				break;
			rc = grow_array((void **)&iod->iod_eprs,
					sizeof(*iod->iod_eprs), *recxs_cap,
					cap);
			if (rc != 0)
				break;
			if (sgl != NULL) {
				rc = grow_array((void **)&sgl->sg_iovs,
						sizeof(*sgl->sg_iovs),
						*recxs_cap, cap);
				if (rc != 0)
					break;
			}
			/* If we execute any of the three breaks above,
			 * *recxs_cap will be < the real capacities of some of
			 * the arrays. This is harmless, as it only causes the
			 * diff segment to be copied and zeroed unnecessarily
			 * next time we grow them.
			 */
			*recxs_cap = cap;
		}

		/* Append one more recx. */
		iod->iod_eprs[iod->iod_nr] = rec->rec_epr;
		/* Iteration does not fill the high epoch, so let's reset
		 * the high epoch with EPOCH_MAX to make vos fetch/update happy.
		 */
		iod->iod_eprs[iod->iod_nr].epr_hi = DAOS_EPOCH_MAX;
		iod->iod_recxs[iod->iod_nr] = rec->rec_recx;
		iod->iod_nr++;
		iod->iod_size = rec->rec_size;
		*data += sizeof(*rec);
		len -= sizeof(*rec);

		/* Append the data, if inline. */
		if (sgl != NULL && rec->rec_size > 0) {
			d_iov_t *iov = &sgl->sg_iovs[sgl->sg_nr];

			if (rec->rec_flags & RECX_INLINE) {
				d_iov_set(iov, *data, rec->rec_size *
						      rec->rec_recx.rx_nr);
			} else {
				d_iov_set(iov, NULL, 0);
			}

			sgl->sg_nr++;
			D_ASSERTF(sgl->sg_nr <= iod->iod_nr, "%u == %u\n",
				  sgl->sg_nr, iod->iod_nr);
			*data += iov->iov_len;
			len -= iov->iov_len;
		}

		D_DEBUG(DB_REBUILD,
			"unpacked data %p len "DF_U64" idx/nr "DF_U64"/"DF_U64
			" ver %u epr lo/hi "DF_U64"/"DF_U64" size %zd\n",
			rec, len_bak, iod->iod_recxs[iod->iod_nr - 1].rx_idx,
			iod->iod_recxs[iod->iod_nr - 1].rx_nr, rec->rec_version,
			iod->iod_eprs[iod->iod_nr - 1].epr_lo,
			iod->iod_eprs[iod->iod_nr - 1].epr_hi, iod->iod_size);
	}

	D_DEBUG(DB_REBUILD, "unpacked nr %d version/type /%u/%d rc %d\n",
		iod->iod_nr, *version, iod->iod_type, rc);
	return rc;
}

/**
 * Initialize \a io with \a iods[\a iods_cap], \a recxs_caps[\a iods_cap], and
 * \a sgls[\a iods_cap].
 *
 * \param[in,out]	io		I/O descriptor
 * \param[in]		iods		daos_iod_t array
 * \param[in]		recxs_caps	recxs capacity array
 * \param[in]		sgls		optional sgl array for inline recxs
 * \param[in]		ephs		akey punched ephs
 * \param[in]		iods_cap	maximal number of elements in \a iods,
 *					\a recxs_caps, \a sgls, and \a ephs
 */
static void
dss_enum_unpack_io_init(struct dss_enum_unpack_io *io, daos_iod_t *iods,
			int *recxs_caps, d_sg_list_t *sgls,
			daos_epoch_t *ephs, int iods_cap)
{
	memset(io, 0, sizeof(*io));

	D_ASSERTF(iods_cap > 0, "%d\n", iods_cap);
	io->ui_iods_cap = iods_cap;

	D_ASSERT(iods != NULL);
	memset(iods, 0, sizeof(*iods) * iods_cap);
	io->ui_iods = iods;

	D_ASSERT(recxs_caps != NULL);
	memset(recxs_caps, 0, sizeof(*recxs_caps) * iods_cap);
	io->ui_recxs_caps = recxs_caps;

	if (sgls != NULL) {
		memset(sgls, 0, sizeof(*sgls) * iods_cap);
		io->ui_sgls = sgls;
	}

	if (ephs != NULL) {
		memset(ephs, 0, sizeof(*ephs) * iods_cap);
		io->ui_akey_punch_ephs = ephs;
	}
}

static void
clear_iod(daos_iod_t *iod, d_sg_list_t *sgl, int *recxs_cap)
{
	daos_iov_free(&iod->iod_name);
	if (iod->iod_recxs != NULL)
		D_FREE(iod->iod_recxs);
	if (iod->iod_eprs != NULL)
		D_FREE(iod->iod_eprs);
	memset(iod, 0, sizeof(*iod));

	if (sgl != NULL) {
		if (sgl->sg_iovs != NULL)
			D_FREE(sgl->sg_iovs);
		memset(sgl, 0, sizeof(*sgl));
	}

	*recxs_cap = 0;
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

	for (i = 0; i < io->ui_iods_size; i++) {
		d_sg_list_t *sgl = NULL;

		if (io->ui_sgls != NULL)
			sgl = &io->ui_sgls[i];
		clear_iod(&io->ui_iods[i], sgl, &io->ui_recxs_caps[i]);
	}

	if (io->ui_akey_punch_ephs)
		memset(io->ui_akey_punch_ephs, 0,
		       sizeof(daos_epoch_t) * io->ui_iods_cap);
	io->ui_dkey_punch_eph = 0;
	io->ui_iods_size = 0;
	io->ui_version = 0;
}

/**
 * Finalize \a io. All iods/sgls must have already been cleard.
 *
 * \param[in]	io	I/O descriptor
 */
static void
dss_enum_unpack_io_fini(struct dss_enum_unpack_io *io)
{
	D_ASSERTF(io->ui_iods_size == 0, "%d\n", io->ui_iods_size);
	daos_iov_free(&io->ui_dkey);
}

/*
 * Close the current iod (i.e., io->ui_iods[io->ui_iods_size - 1]).
 * If it contains recxs, append it to io by incrementing
 * io->ui_iods_size. If it doesn't contain any recx, clear it.
 */
static void
close_iod(struct dss_enum_unpack_io *io)
{
	int idx;

	D_ASSERTF(io->ui_iods_cap > 0, "%d > 0\n", io->ui_iods_cap);
	D_ASSERTF(io->ui_iods_size <= io->ui_iods_cap, "%d < %d\n",
		  io->ui_iods_size, io->ui_iods_cap);

	if (io->ui_iods_size == 0)
		return;

	idx = io->ui_iods_size - 1;
	if (io->ui_iods[idx].iod_nr > 0) {
		io->ui_iods_size++;
	} else {
		d_sg_list_t *sgl = NULL;

		D_DEBUG(DB_TRACE, "iod without recxs: %d\n", idx);
		if (io->ui_sgls != NULL)
			sgl = &io->ui_sgls[idx];
		clear_iod(&io->ui_iods[idx], sgl, &io->ui_recxs_caps[idx]);
	}
}

/* Close io, pass it to cb, and clear it. */
static int
complete_io(struct dss_enum_unpack_io *io, dss_enum_unpack_cb_t cb, void *arg)
{
	int rc = 0;

	if (io->ui_iods_size == 0) {
		D_DEBUG(DB_TRACE, "io empty\n");
		goto out;
	}
	rc = cb(io, arg);
out:
	dss_enum_unpack_io_clear(io);
	return rc;
}

/**
 * Unpack dkey and akey
 */
static int
enum_unpack_key(daos_key_desc_t *kds, char *key_data,
		struct dss_enum_unpack_io *io,
		dss_enum_unpack_cb_t cb, void *cb_arg)
{
	daos_key_t	key;
	int		rc = 0;

	D_ASSERT(kds->kd_val_type == OBJ_ITER_DKEY ||
		 kds->kd_val_type == OBJ_ITER_AKEY);

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
			daos_iov_copy(&io->ui_dkey, &key);
		} else if (!daos_key_match(&io->ui_dkey, &key)) {
			/* Close current IOD if dkey are different */
			close_iod(io);
			rc = complete_io(io, cb, cb_arg);
			if (rc != 0)
				return rc;

			/* Update to the new dkey */
			daos_iov_free(&io->ui_dkey);
			rc = daos_iov_copy(&io->ui_dkey, &key);
		}
		D_DEBUG(DB_IO, "process dkey "DF_KEY": rc %d\n",
			DP_KEY(&key), rc);
		return rc;
	}

	D_DEBUG(DB_IO, "process akey %d %s\n",
		(int)key.iov_len, (char *)key.iov_buf);

	if (io->ui_iods_size == 0) {
		D_ASSERT(io->ui_iods_size < io->ui_iods_cap);
		rc = daos_iov_copy(&io->ui_iods[0].iod_name, &key);
		if (rc == 0)
			io->ui_iods_size++;
	} else if (!daos_key_match(&io->ui_iods[io->ui_iods_size - 1].iod_name,
				   &key)) {
		D_ASSERT(io->ui_iods_size < io->ui_iods_cap);
		rc = daos_iov_copy(&io->ui_iods[io->ui_iods_size].iod_name,
				   &key);
		if (rc == 0)
			io->ui_iods_size++;
	}

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

	if (io->ui_iods_size == 0) {
		D_ERROR("punched epoch for empty akey rc %d\n", -DER_INVAL);
		return -DER_INVAL;
	}

	idx = io->ui_iods_size - 1;
	D_ASSERT(io->ui_akey_punch_ephs != NULL);
	memcpy(&io->ui_akey_punch_ephs[idx], data, kds->kd_key_len);

	return 0;
}

static int
enum_unpack_recxs(daos_key_desc_t *kds, void *data,
		  struct dss_enum_unpack_io *io,
		  dss_enum_unpack_cb_t cb, void *cb_arg)
{
	daos_iod_t	*iod;
	daos_key_t	*iod_akey;
	daos_key_t	*dkey;
	void		*ptr = data;
	int		rc = 0;

	if (io->ui_iods_size == 0)
		return -DER_INVAL;

	iod = &io->ui_iods[io->ui_iods_size - 1];
	iod_akey = &iod->iod_name;
	dkey = &io->ui_dkey;
	if (dkey->iov_len == 0 || iod_akey->iov_len == 0) {
		rc = -DER_INVAL;
		D_ERROR("invalid list buf %c\n", rc);
		return rc;
	}

	while (ptr < data + kds->kd_key_len) {
		int		j = io->ui_iods_size - 1;
		daos_size_t	len;

		/* Because vos_obj_update only accept single
		 * version, let's go through the records to
		 * check different version, and* queue rebuild.
		 */
		len = data + kds->kd_key_len - ptr;
		rc = unpack_recxs(&io->ui_iods[j],
				  &io->ui_recxs_caps[j],
				  io->ui_sgls == NULL ?
				  NULL : &io->ui_sgls[j], iod_akey,
				  kds, &ptr, len,
				  &io->ui_version);
		if (rc <= 0)
			break;

		D_ASSERT(rc == UNPACK_COMPLETE_IOD ||
			 rc == UNPACK_COMPLETE_IO);
		/* Close current IOD or even current I/O.*/
		close_iod(io);
		if (rc == UNPACK_COMPLETE_IOD &&
		    io->ui_iods_size < io->ui_iods_cap) {
			rc = 0;
			continue;
		}

		rc = complete_io(io, cb, cb_arg);
		if (rc < 0)
			break;
	}

	D_DEBUG(DB_IO, "unpack recxs: %d\n", rc);
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
	} else if (daos_unit_oid_compare(io->ui_oid, *oid) !=
		   0) {
		close_iod(io);
		rc = complete_io(io, cb, cb_arg);
		if (rc != 0)
			return rc;
		daos_iov_free(&io->ui_dkey);
		io->ui_oid = *oid;
	}

	D_DEBUG(DB_REBUILD, "process obj "DF_UOID"\n",
		DP_UOID(io->ui_oid));

	return rc;
}

/**
 * Unpack the result of a dss_enum_pack enumeration into \a io, which can then
 * be used to issue a VOS update. \a arg->*_anchor are ignored currently. \a cb
 * will be called, for the caller to consume the recxs accumulated in \a io.
 *
 * \param[in]		vos_type	enumeration type
 * \param[in]		arg		enumeration argument
 * \param[in]		cb		callback
 * \param[in]		cb_arg		callback argument
 */
int
dss_enum_unpack(vos_iter_type_t vos_type, struct dss_enum_arg *arg,
		dss_enum_unpack_cb_t cb, void *cb_arg)
{
	struct dss_enum_unpack_io	io;
	daos_iod_t			iods[DSS_ENUM_UNPACK_MAX_IODS];
	int				recxs_caps[DSS_ENUM_UNPACK_MAX_IODS];
	d_sg_list_t			sgls[DSS_ENUM_UNPACK_MAX_IODS];
	daos_epoch_t			ephs[DSS_ENUM_UNPACK_MAX_IODS];
	void				*ptr;
	unsigned int			i;
	int				rc = 0;
	int				type;

	/* Currently, this function is only for unpacking recursive
	 * enumerations from arg->kds and arg->sgl.
	 */
	D_ASSERT(arg->chk_key2big && !arg->fill_recxs);

	D_ASSERT(arg->kds_len > 0);
	D_ASSERT(arg->kds != NULL);
	type = vos_iter_type_2pack_type(vos_type);
	if (arg->kds[0].kd_val_type != type) {
		D_ERROR("the first kds type %d != %d\n",
			arg->kds[0].kd_val_type, type);
		return -DER_INVAL;
	}

	dss_enum_unpack_io_init(&io, iods, recxs_caps, sgls, ephs,
				DSS_ENUM_UNPACK_MAX_IODS);
	if (type != OBJ_ITER_OBJ)
		io.ui_oid = arg->oid;

	D_ASSERTF(arg->sgl->sg_nr > 0, "%u\n", arg->sgl->sg_nr);
	D_ASSERT(arg->sgl->sg_iovs != NULL);
	ptr = arg->sgl->sg_iovs[0].iov_buf;

	for (i = 0; i < arg->kds_len; i++) {
		daos_key_desc_t *kds = &arg->kds[i];

		D_DEBUG(DB_REBUILD, "process %d type %d ptr %p len "DF_U64
			" total %zd\n", i, kds->kd_val_type, ptr,
			kds->kd_key_len, arg->sgl->sg_iovs[0].iov_len);

		D_ASSERT(kds->kd_key_len > 0);
		switch (kds->kd_val_type) {
		case OBJ_ITER_OBJ:
			rc = enum_unpack_oid(&arg->kds[i], ptr, &io, cb,
					     cb_arg);
			break;
		case OBJ_ITER_DKEY:
		case OBJ_ITER_AKEY:
			rc = enum_unpack_key(kds, ptr, &io, cb, cb_arg);
			break;
		case OBJ_ITER_RECX:
		case OBJ_ITER_SINGLE:
			rc = enum_unpack_recxs(kds, ptr, &io, cb, cb_arg);
			break;
		case OBJ_ITER_DKEY_EPOCH:
		case OBJ_ITER_AKEY_EPOCH:
			rc = enum_unpack_punched_ephs(kds, ptr, &io);
			break;
		default:
			D_ERROR("unknown kds type %d\n", kds->kd_val_type);
			rc = -DER_INVAL;
			break;
		}

		if (rc) {
			D_ERROR("unpack %dth failed: rc%d\n", i, rc);
			goto out;
		}

		/* Close the current IOD, if it reaches to the limit */
		if (io.ui_iods_size >= io.ui_iods_cap) {
			close_iod(&io);
			rc = complete_io(&io, cb, cb_arg);
			if (rc != 0) {
				D_ERROR("complete io failed: rc %d\n", rc);
				goto out;
			}
		}

		ptr += kds->kd_key_len;
	}

	if (io.ui_iods_size > 0 || io.ui_iods[0].iod_nr > 0) {
		close_iod(&io);
		rc = complete_io(&io, cb, cb_arg);
	}

out:
	D_DEBUG(DB_REBUILD, "process list buf "DF_UOID" rc %d\n",
		DP_UOID(io.ui_oid), rc);

	dss_enum_unpack_io_fini(&io);
	return rc;
}
