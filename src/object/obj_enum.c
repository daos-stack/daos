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
 * object: Enumeration pack & unpack utilities
 */

#define D_LOGFAC DD_FAC(object)

#include <daos/container.h>
#include <daos_srv/vos.h>
#include "obj_internal.h"

/* obj_enum_rec.rec_flags */
#define RECX_INLINE	(1U << 0)

struct obj_enum_rec {
	daos_recx_t		rec_recx;
	daos_epoch_range_t	rec_epr;
	uint64_t		rec_size;
	uint32_t		rec_version;
	uint32_t		rec_flags;
};

static int
fill_recxs(daos_handle_t ih, vos_iter_entry_t *key_ent,
	   struct daos_enum_arg *arg, vos_iter_type_t type)
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
is_sgl_kds_full(struct daos_enum_arg *arg, daos_size_t size)
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
fill_obj(daos_handle_t ih, vos_iter_entry_t *entry, struct daos_enum_arg *arg,
	 vos_iter_type_t type)
{
	d_iov_t *iovs = arg->sgl->sg_iovs;

	D_ASSERTF(type == VOS_ITER_OBJ, "%d\n", type);

	if (is_sgl_kds_full(arg, sizeof(entry->ie_oid)))
		return 1;

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
fill_key(daos_handle_t ih, vos_iter_entry_t *key_ent, struct daos_enum_arg *arg,
	 vos_iter_type_t type)
{
	d_iov_t	*iovs = arg->sgl->sg_iovs;
	daos_size_t	 size;

	D_ASSERT(type == VOS_ITER_DKEY || type == VOS_ITER_AKEY);
	size = key_ent->ie_key.iov_len;

	/* for tweaking kds_len in fill_rec() */
	arg->last_type = type;

	if (is_sgl_kds_full(arg, size)) {
		/* NB: if it is rebuild object iteration, let's
		 * check if both dkey & akey was already packed
		 * (kds_len < 2) before return KEY2BIG.
		 */
		if (arg->kds_len == 0 ||
		    (arg->chk_key2big && arg->kds_len < 2)) {
			if (arg->kds[0].kd_key_len < size)
				arg->kds[0].kd_key_len = size;
			return -DER_KEY2BIG;
		} else {
			return 1;
		}
	}

	D_ASSERT(arg->kds_len < arg->kds_cap);
	arg->kds[arg->kds_len].kd_key_len = size;
	arg->kds[arg->kds_len].kd_csum_len = 0;
	arg->kds[arg->kds_len].kd_val_type = type;
	arg->kds_len++;

	if (arg->eprs != NULL) {
		arg->eprs[arg->eprs_len].epr_lo = key_ent->ie_epoch;
		arg->eprs[arg->eprs_len].epr_hi = DAOS_EPOCH_MAX;
		arg->eprs_len++;
	}

	D_ASSERT(iovs[arg->sgl_idx].iov_len + key_ent->ie_key.iov_len <
		 iovs[arg->sgl_idx].iov_buf_len);
	memcpy(iovs[arg->sgl_idx].iov_buf + iovs[arg->sgl_idx].iov_len,
	       key_ent->ie_key.iov_buf, key_ent->ie_key.iov_len);

	iovs[arg->sgl_idx].iov_len += key_ent->ie_key.iov_len;
	D_DEBUG(DB_IO, "Pack key %d %s iov total %zd kds len %d eph "
		DF_U64"\n", (int)key_ent->ie_key.iov_len,
		(char *)key_ent->ie_key.iov_buf,
		iovs[arg->sgl_idx].iov_len, arg->kds_len - 1,
		key_ent->ie_epoch);

	return 0;
}

/* Callers are responsible for incrementing arg->kds_len. See iter_akey_cb. */
static int
fill_rec(daos_handle_t ih, vos_iter_entry_t *key_ent, struct daos_enum_arg *arg,
	 vos_iter_type_t type, vos_iter_param_t *param, unsigned int *acts)
{
	d_iov_t		*iovs = arg->sgl->sg_iovs;
	struct obj_enum_rec	*rec;
	daos_size_t		 data_size, iod_size;
	daos_size_t		 size = sizeof(*rec);
	bool			 inline_data = false, bump_kds_len = false;
	int			 rc = 0;

	D_ASSERT(type == VOS_ITER_SINGLE || type == VOS_ITER_RECX);

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
	if (inline_data && data_size > 0 && arg->copy_cb) {
		d_iov_t iov_out;

		/* inline packing for the small recx located on SCM */
		D_ASSERT(key_ent->ie_biov.bi_addr.ba_type == DAOS_MEDIA_SCM);

		d_iov_set(&iov_out, iovs[arg->sgl_idx].iov_buf +
				       iovs[arg->sgl_idx].iov_len, data_size);
		rc = arg->copy_cb(ih, key_ent, &iov_out);
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
		/** This eprs will not be used,
		 * because the epoch for each record will be returned
		 * through obj_enum_rec anyway, see fill_rec().
		 * This "empty" eprs is just for eprs and kds to
		 * be matched, so it would be easier for unpacking
		 * see daos_enum_unpack().
		 */
		if (arg->eprs != NULL) {
			arg->eprs[arg->eprs_len].epr_lo = DAOS_EPOCH_MAX;
			arg->eprs[arg->eprs_len].epr_hi = DAOS_EPOCH_MAX;
			arg->eprs_len++;
		}
	}
out:
	if (bump_kds_len)
		arg->kds_len++;
	return rc;
}

int
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
		if (((struct daos_enum_arg *)cb_arg)->fill_recxs)
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

	if (kds->kd_val_type == VOS_ITER_SINGLE)
		type = DAOS_IOD_SINGLE;
	else
		type = DAOS_IOD_ARRAY;

	while (len > 0) {
		struct obj_enum_rec *rec = *data;

		D_DEBUG(DB_TRACE, "data %p len "DF_U64"\n", *data, len);

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
			D_DEBUG(DB_TRACE, "different version %u != %u\n",
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

		D_DEBUG(DB_TRACE, "unpack %p idx/nr "DF_U64"/"DF_U64 "ver %u"
			" epr lo/hi "DF_U64"/"DF_U64" size %zd\n",
			*data, iod->iod_recxs[iod->iod_nr - 1].rx_idx,
			iod->iod_recxs[iod->iod_nr - 1].rx_nr, rec->rec_version,
			iod->iod_eprs[iod->iod_nr - 1].epr_lo,
			iod->iod_eprs[iod->iod_nr - 1].epr_hi, iod->iod_size);
	}

	D_DEBUG(DB_TRACE, "pack nr %d version/type /%u/%d rc %d\n",
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
 * \param[in]		ephs		epoch array
 * \param[in]		iods_cap	maximal number of elements in \a iods,
 *					\a recxs_caps, \a sgls, and \a ephs
 */
static void
daos_enum_unpack_io_init(struct daos_enum_unpack_io *io, daos_iod_t *iods,
			 int *recxs_caps, d_sg_list_t *sgls,
			 daos_epoch_t *ephs, int iods_cap)
{
	int i;

	memset(io, 0, sizeof(*io));

	io->ui_dkey_eph = DAOS_EPOCH_MAX;

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

	for (i = 0; i < iods_cap; i++)
		ephs[i] = DAOS_EPOCH_MAX;

	io->ui_akey_ephs = ephs;
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
daos_enum_unpack_io_clear(struct daos_enum_unpack_io *io)
{
	int i;

	for (i = 0; i < io->ui_iods_len; i++) {
		d_sg_list_t *sgl = NULL;

		if (io->ui_sgls != NULL)
			sgl = &io->ui_sgls[i];
		clear_iod(&io->ui_iods[i], sgl, &io->ui_recxs_caps[i]);
		if (io->ui_akey_ephs)
			io->ui_akey_ephs[i] = DAOS_EPOCH_MAX;
	}

	io->ui_dkey_eph = DAOS_EPOCH_MAX;
	io->ui_iods_len = 0;
	io->ui_version = 0;
}

/**
 * Finalize \a io. All iods/sgls must have already been cleard.
 *
 * \param[in]	io	I/O descriptor
 */
static void
daos_enum_unpack_io_fini(struct daos_enum_unpack_io *io)
{
	D_ASSERTF(io->ui_iods_len == 0, "%d\n", io->ui_iods_len);
	daos_iov_free(&io->ui_dkey);
}

/*
 * Close the current iod (i.e., io->ui_iods[io->ui_iods_len]). If it contains
 * recxs, append it to io by incrementing io->ui_iods_len. If it doesn't
 * contain any recx, clear it.
 */
static void
close_iod(struct daos_enum_unpack_io *io)
{
	D_ASSERTF(io->ui_iods_cap > 0, "%d > 0\n", io->ui_iods_cap);
	D_ASSERTF(io->ui_iods_len < io->ui_iods_cap, "%d < %d\n",
		  io->ui_iods_len, io->ui_iods_cap);
	if (io->ui_iods[io->ui_iods_len].iod_nr > 0) {
		io->ui_iods_len++;
	} else {
		d_sg_list_t *sgl = NULL;

		D_DEBUG(DB_TRACE, "iod without recxs: %d\n", io->ui_iods_len);
		if (io->ui_sgls != NULL)
			sgl = &io->ui_sgls[io->ui_iods_len];
		clear_iod(&io->ui_iods[io->ui_iods_len], sgl,
			  &io->ui_recxs_caps[io->ui_iods_len]);
	}
}

/* Close io, pass it to cb, and clear it. */
static int
complete_io(struct daos_enum_unpack_io *io, daos_enum_unpack_cb_t cb, void *arg)
{
	int rc = 0;

	if (io->ui_iods_len == 0) {
		D_DEBUG(DB_TRACE, "io empty\n");
		goto out;
	}
	rc = cb(io, arg);
out:
	daos_enum_unpack_io_clear(io);
	return rc;
}

/**
 * Unpack the result of a daos_enum_pack enumeration into \a io, which can then
 * be used to issue a VOS update or some consistency check. \a arg->*_anchor are
 * ignored currently. \a cb will be called, for the caller to consume the recxs
 * accumulated in \a io.
 *
 * \param[in]		type	enumeration type
 * \param[in]		arg	enumeration argument
 * \param[in]		cb	callback
 * \param[in]		cb_arg	callback argument
 */
int
daos_enum_unpack(vos_iter_type_t type, struct daos_enum_arg *arg,
		 daos_enum_unpack_cb_t cb, void *cb_arg)
{
	struct daos_enum_unpack_io	io;
	daos_iod_t			iods[DAOS_ENUM_UNPACK_MAX_IODS];
	int				recxs_caps[DAOS_ENUM_UNPACK_MAX_IODS];
	daos_epoch_t			ephs[DAOS_ENUM_UNPACK_MAX_IODS];
	d_sg_list_t			sgls[DAOS_ENUM_UNPACK_MAX_IODS];
	daos_key_t			akey = { 0 };
	daos_epoch_range_t		*eprs = arg->eprs;
	void				*ptr;
	unsigned int			i;
	int				rc = 0;

	/* Currently, this function is only for unpacking recursive
	 * enumerations from arg->kds and arg->sgl.
	 */
	D_ASSERT(arg->chk_key2big && !arg->fill_recxs);

	D_ASSERT(arg->kds_len > 0);
	D_ASSERT(arg->kds != NULL);
	if (arg->kds[0].kd_val_type != type) {
		D_ERROR("the first kds type %d != %d\n",
			arg->kds[0].kd_val_type, type);
		return -DER_INVAL;
	}

	daos_enum_unpack_io_init(&io, iods, recxs_caps, sgls, ephs,
				 DAOS_ENUM_UNPACK_MAX_IODS);
	if (type > VOS_ITER_OBJ)
		io.ui_oid = arg->oid;

	D_ASSERTF(arg->sgl->sg_nr > 0, "%u\n", arg->sgl->sg_nr);
	D_ASSERT(arg->sgl->sg_iovs != NULL);
	ptr = arg->sgl->sg_iovs[0].iov_buf;

	for (i = 0; i < arg->kds_len; i++) {
		D_DEBUG(DB_TRACE, "process %d type %d ptr %p len "DF_U64
			" total %zd\n", i, arg->kds[i].kd_val_type, ptr,
			arg->kds[i].kd_key_len, arg->sgl->sg_iovs[0].iov_len);

		D_ASSERT(arg->kds[i].kd_key_len > 0);
		if (arg->kds[i].kd_val_type == VOS_ITER_OBJ) {
			daos_unit_oid_t *oid = ptr;

			if (arg->kds[i].kd_key_len != sizeof(*oid)) {
				D_ERROR("Invalid object ID size: "DF_U64
					" != %zu\n", arg->kds[i].kd_key_len,
					sizeof(*oid));
				rc = -DER_INVAL;
				break;
			}
			if (daos_unit_oid_is_null(io.ui_oid)) {
				io.ui_oid = *oid;
			} else if (daos_unit_oid_compare(io.ui_oid, *oid) !=
				   0) {
				close_iod(&io);
				rc = complete_io(&io, cb, cb_arg);
				if (rc != 0)
					break;
				daos_iov_free(&io.ui_dkey);
				io.ui_oid = *oid;
			}
			D_DEBUG(DB_TRACE, "process obj "DF_UOID"\n",
				DP_UOID(io.ui_oid));
		} else if (arg->kds[i].kd_val_type == VOS_ITER_DKEY) {
			daos_key_t tmp_key;

			tmp_key.iov_buf = ptr;
			tmp_key.iov_buf_len = arg->kds[i].kd_key_len;
			tmp_key.iov_len = arg->kds[i].kd_key_len;

			if (io.ui_dkey.iov_len == 0) {
				daos_iov_copy(&io.ui_dkey, &tmp_key);
			} else if (!daos_key_match(&io.ui_dkey, &tmp_key) ||
				   (eprs != NULL &&
				    io.ui_dkey_eph != eprs[i].epr_lo)) {
				close_iod(&io);
				rc = complete_io(&io, cb, cb_arg);
				if (rc != 0)
					break;

				if (!daos_key_match(&io.ui_dkey, &tmp_key)) {
					daos_iov_free(&io.ui_dkey);
					daos_iov_copy(&io.ui_dkey, &tmp_key);
				}
			}

			if (eprs != NULL)
				io.ui_dkey_eph = eprs[i].epr_lo;

			D_DEBUG(DB_TRACE, "process dkey %d %s eph "DF_U64"\n",
				(int)io.ui_dkey.iov_len,
				(char *)io.ui_dkey.iov_buf,
				eprs ? io.ui_dkey_eph : 0);
		} else if (arg->kds[i].kd_val_type == VOS_ITER_AKEY) {
			daos_key_t *iod_akey;

			akey.iov_buf = ptr;
			akey.iov_buf_len = arg->kds[i].kd_key_len;
			akey.iov_len = arg->kds[i].kd_key_len;
			if (io.ui_dkey.iov_buf == NULL) {
				D_ERROR("No dkey for akey %*.s invalid buf.\n",
				      (int)akey.iov_len, (char *)akey.iov_buf);
				rc = -DER_INVAL;
				break;
			}

			D_DEBUG(DB_TRACE, "process akey %d %s eph "DF_U64"\n",
				(int)akey.iov_len, (char *)akey.iov_buf,
				eprs ? eprs[i].epr_lo : 0);

			if (io.ui_iods_len >= io.ui_iods_cap) {
				close_iod(&io);
				rc = complete_io(&io, cb, cb_arg);
				if (rc < 0)
					goto out;
			}

			/* If there are no records for akey(punched akey rec),
			 * then ui_iods_len might still point to the last dkey,
			 * i.e. close_iod are not being called.
			 */
			iod_akey = &io.ui_iods[io.ui_iods_len].iod_name;
			if ((iod_akey->iov_len != 0 &&
			    !daos_key_match(iod_akey, &akey)) ||
			    (eprs != NULL && io.ui_akey_ephs[io.ui_iods_len] !=
			     eprs[i].epr_lo)) {
				io.ui_iods_len++;
				if (io.ui_iods_len >= io.ui_iods_cap) {
					rc = complete_io(&io, cb, cb_arg);
					if (rc < 0)
						goto out;
				}
			}

			rc = unpack_recxs(&io.ui_iods[io.ui_iods_len],
					  NULL, NULL, &akey, NULL, NULL,
					  0, NULL);
			if (rc < 0)
				goto out;

			if (eprs)
				io.ui_akey_ephs[io.ui_iods_len] =
							eprs[i].epr_lo;
		} else if (arg->kds[i].kd_val_type == VOS_ITER_SINGLE ||
			   arg->kds[i].kd_val_type == VOS_ITER_RECX) {
			void *data = ptr;

			if (io.ui_dkey.iov_len == 0 || akey.iov_len == 0) {
				D_ERROR("invalid list buf for kds %d\n", i);
				rc = -DER_INVAL;
				break;
			}

			while (data < ptr + arg->kds[i].kd_key_len) {
				daos_size_t	len;
				int		j = io.ui_iods_len;

				/* Because vos_obj_update only accept single
				 * version, let's go through the records to
				 * check different version.
				 */
				len = ptr + arg->kds[i].kd_key_len - data;
				rc = unpack_recxs(&io.ui_iods[j],
						  &io.ui_recxs_caps[j],
						  io.ui_sgls == NULL ?
						  NULL : &io.ui_sgls[j], &akey,
						  &arg->kds[i], &data, len,
						  &io.ui_version);
				if (rc < 0)
					goto out;

				/* All records referred by this kds has been
				 * packed, then it does not need to send
				 * right away, might pack more next round.
				 */
				if (rc == 0)
					break;

				D_ASSERT(rc == UNPACK_COMPLETE_IOD ||
					 rc == UNPACK_COMPLETE_IO);
				/* Close current IOD or even current I/O.*/
				close_iod(&io);
				if (rc == UNPACK_COMPLETE_IOD &&
				    io.ui_iods_len < io.ui_iods_cap)
					continue;

				rc = complete_io(&io, cb, cb_arg);
				if (rc < 0)
					goto out;
			}
		} else {
			D_ERROR("unknow kds type %d\n",
				arg->kds[i].kd_val_type);
			rc = -DER_INVAL;
			break;
		}
		ptr += arg->kds[i].kd_key_len;
	}

	if (io.ui_iods_len > 0 || io.ui_iods[0].iod_nr > 0) {
		close_iod(&io);
		rc = complete_io(&io, cb, cb_arg);
	}

	D_DEBUG(DB_TRACE, "process list buf "DF_UOID" rc %d\n",
		DP_UOID(io.ui_oid), rc);

out:
	daos_enum_unpack_io_fini(&io);
	return rc;
}

void
daos_enum_dkeys_init_arg(struct obj_enum_dkeys_arg *oeda, daos_unit_oid_t oid)
{
	memset(oeda, 0, sizeof(*oeda));

	dc_obj_shard2anchor(&oeda->dkey_anchor, oid.id_shard);

	oeda->buf = oeda->inline_buf;
	oeda->buf_len = ITER_BUF_SIZE;

	oeda->sgl.sg_iovs = &oeda->iov;

	oeda->enum_arg.oid = oid;
	oeda->enum_arg.chk_key2big = true;
	oeda->enum_arg.kds = oeda->kds;
	oeda->enum_arg.kds_cap = KDS_NUM;
	oeda->enum_arg.sgl = &oeda->sgl;
	oeda->enum_arg.eprs = oeda->eprs;
	oeda->enum_arg.eprs_cap = KDS_NUM;
}

void
daos_enum_dkeys_fini_arg(struct obj_enum_dkeys_arg *oeda)
{
	if (oeda->buf != oeda->inline_buf && oeda->buf != NULL)
		D_FREE(oeda->buf);
	if (oeda->buf_saved != NULL)
		D_FREE(oeda->buf_saved);
}

void
daos_enum_dkeys_prep_unpack(struct obj_enum_dkeys_arg *oeda)
{
	oeda->iov.iov_len = oeda->size;
	oeda->enum_arg.sgl_idx = 1;
	oeda->enum_arg.kds_len = oeda->num;
	oeda->enum_arg.eprs_len = oeda->num;
}

int
daos_enum_dkeys_do_list(daos_handle_t oh, daos_epoch_t *epoch,
			struct obj_enum_dkeys_arg *oeda,
			daos_obj_list_obj_cb_t list_cb, uint32_t flags)
{
	int	rc;

	if (oeda->need_retry) {
		/* Restore the anchors. */
		memcpy(&oeda->dkey_anchor, &oeda->dkey_anchor_saved,
		       sizeof(daos_anchor_t));
		memcpy(&oeda->akey_anchor, &oeda->akey_anchor_saved,
		       sizeof(daos_anchor_t));
		memcpy(&oeda->anchor, &oeda->anchor_saved,
		       sizeof(daos_anchor_t));

		D_ASSERT(oeda->size <= oeda->buf_len);

		oeda->size_saved = oeda->size;
		if (oeda->size > 0) {
			if (oeda->buf_len_saved < oeda->size) {
				if (oeda->buf_saved != NULL)
					D_FREE(oeda->buf_saved);

				oeda->buf_len_saved = oeda->buf_len;
				D_ALLOC(oeda->buf_saved, oeda->buf_len_saved);
				if (oeda->buf_saved == NULL)
					return -DER_NOMEM;
			}

			memcpy(oeda->buf_saved, oeda->buf, oeda->size);
		}
	} else {
		/* Backup the anchors. */
		memcpy(&oeda->dkey_anchor_saved, &oeda->dkey_anchor,
		       sizeof(daos_anchor_t));
		memcpy(&oeda->akey_anchor_saved, &oeda->akey_anchor,
		       sizeof(daos_anchor_t));
		memcpy(&oeda->anchor_saved, &oeda->anchor,
		       sizeof(daos_anchor_t));

		memset(oeda->kds, 0, sizeof(daos_key_desc_t) * KDS_NUM);
		memset(oeda->eprs, 0, sizeof(daos_epoch_range_t) * KDS_NUM);
		memset(oeda->buf, 0, oeda->buf_len);

		oeda->iov.iov_len = 0;
		oeda->iov.iov_buf = oeda->buf;
		oeda->iov.iov_buf_len = oeda->buf_len;

		oeda->sgl.sg_nr = 1;
		oeda->sgl.sg_nr_out = 1;

		oeda->size = 0;
		oeda->num = KDS_NUM;

		oeda->has_retried = 0;
		oeda->lost_shard = 0;
	}

	daos_anchor_set_flags(&oeda->dkey_anchor, flags);

	rc = list_cb(oh, epoch, NULL, NULL, &oeda->size, &oeda->num, oeda->kds,
		     oeda->eprs, &oeda->sgl, &oeda->anchor, &oeda->dkey_anchor,
		     &oeda->akey_anchor);
	if (rc == -DER_KEY2BIG) {
		D_DEBUG(DB_TRACE, "list obj dkeys on shard "DF_UOID
			" got -DER_KEY2BIG, key_len "DF_U64"\n",
			DP_UOID(oeda->enum_arg.oid), oeda->kds[0].kd_key_len);
		oeda->buf_len = roundup(oeda->kds[0].kd_key_len * 2, 8);
		if (oeda->buf != oeda->inline_buf)
			D_FREE(oeda->buf);

		D_ALLOC(oeda->buf, oeda->buf_len);
		if (oeda->buf == NULL)
			rc = -DER_NOMEM;
		else
			rc = 1;
	}

	return rc;
}

int
daos_enum_dkeys(daos_handle_t oh, daos_unit_oid_t oid, daos_epoch_t epoch,
		daos_obj_list_obj_cb_t list_cb,
		daos_enum_unpack_cb_t unpack_cb, void *arg)
{
	struct obj_enum_dkeys_arg	oeda;
	int				rc = 0;

	daos_enum_dkeys_init_arg(&oeda, oid);

	do {
		rc = daos_enum_dkeys_do_list(oh, &epoch, &oeda, list_cb,
					     DIOF_TO_LEADER);
		/* Enlarge the buffer and re-list the dkeys. */
		if (rc > 0)
			continue;

		if (rc < 0) {
			/* container might have been destroyed. Or there is
			 * no spare target left for this object see
			 * obj_grp_valid_shard_get()
			 */
			if (rc == -DER_NONEXIST)
				rc = 0;
			break;
		}

		if (oeda.num == 0)
			break;

		daos_enum_dkeys_prep_unpack(&oeda);
		rc = daos_enum_unpack(VOS_ITER_DKEY, &oeda.enum_arg,
				      unpack_cb, arg);
		if (rc != 0) {
			D_ERROR("list obj dkeys "DF_UOID" failed: rc = %d\n",
				DP_UOID(oid), rc);
			break;
		}
	} while (!daos_anchor_is_eof(&oeda.dkey_anchor));

	daos_enum_dkeys_fini_arg(&oeda);

	return rc;
}
