/*
 * (C) Copyright 2018 Intel Corporation.
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
 * server: VOS-Related Utilities
 */

#define D_LOGFAC DD_FAC(server)

#include <daos_srv/daos_server.h>
#include <daos_srv/vos.h>
#if 1 /* TODO: Remove after moving obj_enum_rec in. */
#include <daos/object.h>
#endif

/**
 * Iterate VOS entries (i.e., containers, objects, dkeys, etc.) and call \a
 * cb(\a arg) for each entry.
 *
 * If \a cb returns a nonzero (either > 0 or < 0) value that is not
 * -DER_NONEXIST, this function stops the iteration and returns that nonzero
 * value from \a cb. If \a cb returns -DER_NONEXIST, this function completes
 * the iteration and returns 0. If \a cb returns 0, the iteration continues.
 *
 * \param[in]		type	entry type
 * \param[in]		param	parameters for \a type
 * \param[in,out]	anchor	[in]: where to begin; [out]: where stopped
 * \param[in]		cb	callback called for each entry
 * \param[in]		arg	callback argument
 *
 * \retval		0	iteration complete
 * \retval		> 0	callback return value
 * \retval		-DER_*	error (but never -DER_NONEXIST)
 */
int
dss_vos_iterate(vos_iter_type_t type, vos_iter_param_t *param,
		daos_anchor_t *anchor, dss_vos_iterate_cb_t cb, void *arg)
{
	daos_anchor_t		*probe_hash = NULL;
	vos_iter_entry_t	key_ent;
	daos_handle_t		ih;
	int			rc;

	rc = vos_iter_prepare(type, param, &ih);
	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			daos_anchor_set_eof(anchor);
			rc = 0;
		} else {
			D_ERROR("failed to prepare iterator (type=%d): %d\n",
				type, rc);
		}
		D_GOTO(out, rc);
	}

	if (!daos_anchor_is_zero(anchor))
		probe_hash = anchor;

	rc = vos_iter_probe(ih, probe_hash);
	if (rc != 0) {
		if (rc == -DER_NONEXIST || rc == -DER_AGAIN) {
			daos_anchor_set_eof(anchor);
			rc = 0;
		} else {
			D_ERROR("failed to probe iterator (type=%d anchor=%p): "
				"%d\n", type, probe_hash, rc);
		}
		D_GOTO(out_iter_fini, rc);
	}

	while (1) {
		rc = vos_iter_fetch(ih, &key_ent, anchor);
		if (rc != 0) {
			D_ERROR("failed to fetch iterator (type=%d): %d\n",
				type, rc);
			break;
		}

		rc = cb(ih, &key_ent, type, param, arg);
		if (rc != 0)
			break;

		rc = vos_iter_next(ih);
		if (rc) {
			if (rc != -DER_NONEXIST)
				D_ERROR("failed to iterate next (type=%d): "
					"%d\n", type, rc);
			break;
		}
	}

	if (rc == -DER_NONEXIST) {
		daos_anchor_set_eof(anchor);
		rc = 0;
	}

out_iter_fini:
	vos_iter_finish(ih);
out:
	return rc;
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

	D_DEBUG(DB_IO, "Pack recxs_eprs "DF_U64"/"DF_U64" recxs_len %d size "
		DF_U64"\n", key_ent->ie_recx.rx_idx, key_ent->ie_recx.rx_nr,
		arg->recxs_len, arg->rsize);

	arg->rnum++;
	return 0;
}

static int
fill_recxs_cb(daos_handle_t ih, vos_iter_entry_t *key_ent,
	      vos_iter_type_t type, vos_iter_param_t *param, void *arg)
{
	return fill_recxs(ih, key_ent, arg, type);
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
		daos_iov_t *iovs = sgl->sg_iovs;

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
	 vos_iter_type_t type)
{
	daos_iov_t *iovs = arg->sgl->sg_iovs;

	D_ASSERTF(type == VOS_ITER_OBJ, "%d\n", type);

	if (is_sgl_kds_full(arg, sizeof(entry->ie_oid)))
		return 1;

	/* Append a new descriptor to kds. */
	D_ASSERT(arg->kds_len < arg->kds_cap);
	memset(&arg->kds[arg->kds_len], 0, sizeof(arg->kds[arg->kds_len]));
	arg->kds[arg->kds_len].kd_key_len = sizeof(entry->ie_oid);
	arg->kds[arg->kds_len].kd_val_types = type;
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
fill_obj_cb(daos_handle_t ih, vos_iter_entry_t *key_ent, vos_iter_type_t type,
	    vos_iter_param_t *param, void *arg)
{
	return fill_obj(ih, key_ent, arg, type);
}

static int
fill_key(daos_handle_t ih, vos_iter_entry_t *key_ent, struct dss_enum_arg *arg,
	 vos_iter_type_t type)
{
	daos_iov_t	*iovs = arg->sgl->sg_iovs;
	daos_size_t	 size;

	D_ASSERT(type == VOS_ITER_DKEY || type == VOS_ITER_AKEY);
	size = key_ent->ie_key.iov_len;

	if (is_sgl_kds_full(arg, size))
		return 1;

	D_ASSERT(arg->kds_len < arg->kds_cap);
	arg->kds[arg->kds_len].kd_key_len = size;
	arg->kds[arg->kds_len].kd_csum_len = 0;
	arg->kds[arg->kds_len].kd_val_types = type;
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
	D_DEBUG(DB_IO, "Pack key %.*s iov total %zd"
		" kds len %d\n", (int)key_ent->ie_key.iov_len,
		(char *)key_ent->ie_key.iov_buf,
		iovs[arg->sgl_idx].iov_len, arg->kds_len - 1);

	return 0;
}

static int
fill_key_cb(daos_handle_t ih, vos_iter_entry_t *key_ent, vos_iter_type_t type,
	    vos_iter_param_t *param, void *arg)
{
	return fill_key(ih, key_ent, arg, type);
}

/* Copy the data of the recx into buf. */
/* TODO: Use entry->ie_eiov when implemented. */
static void
copy_data(vos_iter_type_t type, vos_iter_param_t *param,
	  vos_iter_entry_t *entry, void *buf, size_t len)
{
	daos_iod_t	iod;
	daos_iov_t	iov;
	daos_sg_list_t	sgl;
	int		rc;

	D_ASSERT(type == VOS_ITER_SINGLE || type == VOS_ITER_RECX);

	memset(&iod, 0, sizeof(iod));
	iod.iod_name = param->ip_akey;
	if (type == VOS_ITER_SINGLE)
		iod.iod_type = DAOS_IOD_SINGLE;
	else
		iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_nr = 1;
	iod.iod_recxs = &entry->ie_recx;
	iod.iod_eprs = NULL;

	iov.iov_buf = buf;
	iov.iov_buf_len = len;
	iov.iov_len = 0;
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	rc = vos_obj_fetch(param->ip_hdl, param->ip_oid, entry->ie_epoch,
			   &param->ip_dkey, 1 /* iod_nr */, &iod, &sgl);
	/* This vos_obj_fetch call is a workaround anyway. */
	D_ASSERTF(rc == 0, "%d\n", rc);
	D_ASSERT(iod.iod_size == entry->ie_rsize);
}

/* Callers are responsible for incrementing arg->kds_len. See iter_akey_cb. */
static int
fill_rec(daos_handle_t ih, vos_iter_entry_t *key_ent, struct dss_enum_arg *arg,
	 vos_iter_type_t type, vos_iter_param_t *param)
{
	daos_iov_t		*iovs = arg->sgl->sg_iovs;
	struct obj_enum_rec	*rec;
	daos_size_t		 data_size;
	daos_size_t		 size = sizeof(*rec);
	bool			 inline_data = false;

	D_ASSERT(type == VOS_ITER_SINGLE || type == VOS_ITER_RECX);

	/* Inline the data? A 0 threshold disables this completely. */
	data_size = key_ent->ie_rsize * key_ent->ie_recx.rx_nr;
	if (arg->inline_thres > 0 && data_size <= arg->inline_thres) {
		inline_data = true;
		size += data_size;
	}

	if (is_sgl_kds_full(arg, size))
		return 1;

	/* Grow the next new descriptor (instead of creating yet a new one). */
	arg->kds[arg->kds_len].kd_val_types = type;
	arg->kds[arg->kds_len].kd_key_len += sizeof(*rec);

	/* Append the recx record to iovs. */
	D_ASSERT(iovs[arg->sgl_idx].iov_len + sizeof(*rec) <
		 iovs[arg->sgl_idx].iov_buf_len);
	rec = iovs[arg->sgl_idx].iov_buf + iovs[arg->sgl_idx].iov_len;
	rec->rec_recx = key_ent->ie_recx;
	rec->rec_size = key_ent->ie_rsize;
	rec->rec_epr.epr_lo = key_ent->ie_epoch;
	rec->rec_epr.epr_hi = DAOS_EPOCH_MAX;
	uuid_copy(rec->rec_cookie, key_ent->ie_cookie);
	rec->rec_version = key_ent->ie_ver;
	rec->rec_flags = 0;
	iovs[arg->sgl_idx].iov_len += sizeof(*rec);

	/* If we've decided to inline the data, append the data to iovs. */
	if (inline_data) {
		arg->kds[arg->kds_len].kd_key_len += data_size;
		rec->rec_flags |= RECX_INLINE;
		/* Punched recxs do not have any data to copy. */
		if (data_size > 0)
			copy_data(type, param, key_ent,
				  iovs[arg->sgl_idx].iov_buf +
				  iovs[arg->sgl_idx].iov_len, data_size);
		iovs[arg->sgl_idx].iov_len += data_size;
	}

	D_DEBUG(DB_IO, "Pack rec "DF_U64"/"DF_U64
		" rsize "DF_U64" cookie "DF_UUID" ver %u"
		" kd_len "DF_U64" type %d sgl_idx %d kds_len %d inline "DF_U64
		" epr "DF_U64"/"DF_U64"\n", key_ent->ie_recx.rx_idx,
		key_ent->ie_recx.rx_nr, key_ent->ie_rsize,
		DP_UUID(rec->rec_cookie), rec->rec_version,
		arg->kds[arg->kds_len].kd_key_len, type, arg->sgl_idx,
		arg->kds_len, rec->rec_flags & RECX_INLINE ? data_size : 0,
		rec->rec_epr.epr_lo, rec->rec_epr.epr_hi);
	return 0;
}

static int
fill_rec_cb(daos_handle_t ih, vos_iter_entry_t *key_ent, vos_iter_type_t type,
	    vos_iter_param_t *param, void *arg)
{
	return fill_rec(ih, key_ent, arg, type, param);
}

static int
iter_akey_cb(daos_handle_t ih, vos_iter_entry_t *key_ent, vos_iter_type_t type,
	     vos_iter_param_t *param, void *varg)
{
	struct dss_enum_arg	*arg = varg;
	vos_iter_param_t	 iter_recx_param;
	daos_anchor_t		 single_anchor = { 0 };
	int			 rc;

	D_DEBUG(DB_IO, "enum key %.*s type %d\n",
		(int)key_ent->ie_key.iov_len,
		(char *)key_ent->ie_key.iov_buf, type);

	/* Fill the current key */
	rc = fill_key(ih, key_ent, arg, VOS_ITER_AKEY);
	if (rc)
		goto out;

	iter_recx_param = *param;
	iter_recx_param.ip_akey = key_ent->ie_key;

	/* iterate array record */
	rc = dss_vos_iterate(VOS_ITER_RECX, &iter_recx_param, &arg->recx_anchor,
			     fill_rec_cb, arg);

	if (arg->kds[arg->kds_len].kd_key_len > 0) {
		arg->kds_len++;
		/** This eprs will not be used during rebuild,
		 * because the epoch for each record will be returned
		 * through obj_enum_rec anyway, see fill_rec().
		 * This "empty" eprs is just for eprs and kds to
		 * be matched, so it would be easier for unpacking
		 * see dss_enum_unpack().
		 */
		if (arg->eprs != NULL) {
			arg->eprs[arg->eprs_len].epr_lo = DAOS_EPOCH_MAX;
			arg->eprs[arg->eprs_len].epr_hi = DAOS_EPOCH_MAX;
			arg->eprs_len++;
		}
	}

	/* Exit either failure or buffer is full */
	if (rc) {
		if (rc < 0)
			D_ERROR("failed to enumerate array recxs: %d\n", rc);
		goto out;
	}

	D_ASSERT(daos_anchor_is_eof(&arg->recx_anchor));
	daos_anchor_set_zero(&arg->recx_anchor);

	/* iterate single record */
	rc = dss_vos_iterate(VOS_ITER_SINGLE, &iter_recx_param, &single_anchor,
			     fill_rec_cb, arg);

	if (rc) {
		if (rc < 0)
			D_ERROR("failed to enumerate single recxs: %d\n", rc);
		goto out;
	}

	if (arg->kds[arg->kds_len].kd_key_len > 0) {
		arg->kds_len++;
		/** empty eprs, see comments above */
		if (arg->eprs != NULL) {
			arg->eprs[arg->eprs_len].epr_lo = DAOS_EPOCH_MAX;
			arg->eprs[arg->eprs_len].epr_hi = DAOS_EPOCH_MAX;
			arg->eprs_len++;
		}
	}
out:
	return rc;
}

static int
iter_dkey_cb(daos_handle_t ih, vos_iter_entry_t *key_ent, vos_iter_type_t type,
	     vos_iter_param_t *param, void *varg)
{
	struct dss_enum_arg	*arg = varg;
	vos_iter_param_t	 iter_akey_param;
	int			 rc;

	D_DEBUG(DB_IO, "enum key %.*s type %d\n",
		(int)key_ent->ie_key.iov_len,
		(char *)key_ent->ie_key.iov_buf, type);

	/* Fill the current dkey */
	rc = fill_key(ih, key_ent, arg, VOS_ITER_DKEY);
	if (rc != 0)
		return rc;

	/* iterate akey */
	iter_akey_param = *param;
	iter_akey_param.ip_dkey = key_ent->ie_key;
	rc = dss_vos_iterate(VOS_ITER_AKEY, &iter_akey_param, &arg->akey_anchor,
			     iter_akey_cb, arg);
	if (rc) {
		if (rc < 0)
			D_ERROR("failed to enumerate akeys: %d\n", rc);
		return rc;
	}

	D_ASSERT(daos_anchor_is_eof(&arg->akey_anchor));
	daos_anchor_set_zero(&arg->akey_anchor);
	daos_anchor_set_zero(&arg->recx_anchor);

	return rc;
}

static int
iter_obj_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
	    vos_iter_param_t *param, void *varg)
{
	struct dss_enum_arg	*arg = varg;
	vos_iter_param_t	 iter_dkey_param;
	int			 rc;

	D_ASSERTF(type == VOS_ITER_OBJ, "%d\n", type);
	D_DEBUG(DB_IO, "enum obj "DF_UOID"\n", DP_UOID(entry->ie_oid));

	rc = fill_obj(ih, entry, arg, type);
	if (rc != 0)
		return rc;

	iter_dkey_param = *param;
	iter_dkey_param.ip_oid = entry->ie_oid;
	rc = dss_vos_iterate(VOS_ITER_DKEY, &iter_dkey_param, &arg->dkey_anchor,
			     iter_dkey_cb, arg);
	if (rc != 0) {
		if (rc < 0)
			D_ERROR("failed to enumerate dkeys: %d\n", rc);
		return rc;
	}

	D_ASSERT(daos_anchor_is_eof(&arg->dkey_anchor));
	daos_anchor_set_zero(&arg->dkey_anchor);
	daos_anchor_set_zero(&arg->akey_anchor);
	daos_anchor_set_zero(&arg->recx_anchor);

	return 0;
}

/**
 * Enumerate VOS objects, dkeys, akeys, and/or recxs and pack them into a set
 * of buffers.
 *
 * The buffers must be provided by the caller. They may contain existing data,
 * in which case this function appends to them.
 *
 * \param[in]		type	iteration type
 * \param[in,out]	arg	enumeration argument
 *
 * \retval		0	enumeration complete
 * \retval		1	buffer(s) full
 * \retval		-DER_*	error
 */
int
dss_enum_pack(vos_iter_type_t type, struct dss_enum_arg *arg)
{
	daos_anchor_t	       *anchor;
	dss_vos_iterate_cb_t	cb;
	int			rc;

	D_ASSERT(!arg->fill_recxs ||
		 type == VOS_ITER_SINGLE || type == VOS_ITER_RECX);
	switch (type) {
	case VOS_ITER_OBJ:
		anchor = &arg->obj_anchor;
		cb = arg->recursive ? iter_obj_cb : fill_obj_cb;
		break;
	case VOS_ITER_DKEY:
		anchor = &arg->dkey_anchor;
		cb = arg->recursive ? iter_dkey_cb : fill_key_cb;
		break;
	case VOS_ITER_AKEY:
		anchor = &arg->akey_anchor;
		cb = arg->recursive ? iter_akey_cb : fill_key_cb;
		break;
	case VOS_ITER_SINGLE:
	case VOS_ITER_RECX:
		anchor = &arg->recx_anchor;
		cb = arg->fill_recxs ? fill_recxs_cb : fill_rec_cb;
		break;
	default:
		D_ASSERTF(false, "unknown/unsupported type %d\n", type);
	}

	rc = dss_vos_iterate(type, &arg->param, anchor, cb, arg);

	D_DEBUG(DB_IO, "enum type %d tag %d rc %d\n", type,
		dss_get_module_info()->dmi_tid, rc);
	return rc;
}
