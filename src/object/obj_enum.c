/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * Generic enumeration routines.
 */
#define D_LOGFAC DD_FAC(object)

#include <daos/object.h>
#include "obj_internal.h"

static d_iov_t *
io_csums_iov(struct dc_obj_enum_unpack_io *io)
{
	return &io->ui_csum_iov;
}

static int
grow_array(void **arrayp, size_t elem_size, int old_len, int new_len)
{
	void *p;

	D_ASSERTF(old_len < new_len, "%d < %d\n", old_len, new_len);
	D_REALLOC(p, *arrayp, elem_size * old_len, elem_size * new_len);
	if (p == NULL)
		return -DER_NOMEM;
	*arrayp = p;
	return 0;
}

int
iov_alloc_for_csum_info(d_iov_t *iov, struct dcs_csum_info *csum_info)
{
	size_t size_needed = ci_size(*csum_info);

	/** Make sure the csum buffer is big enough ... resize if needed */
	if (iov->iov_buf == NULL) {
		D_ALLOC(iov->iov_buf, size_needed);
		if (iov->iov_buf == NULL)
			return -DER_NOMEM;
		iov->iov_buf_len = size_needed;
		iov->iov_len = 0;
	} else if (iov->iov_len + size_needed > iov->iov_buf_len) {
		void	*p;
		size_t	 new_size = max(iov->iov_buf_len * 2,
					      iov->iov_len + size_needed);

		D_REALLOC(p, iov->iov_buf, iov->iov_buf_len, new_size);
		if (p == NULL)
			return -DER_NOMEM;
		iov->iov_buf = p;
		iov->iov_buf_len = new_size;
	}

	return 0;
}

enum {
	UNPACK_COMPLETE_IO = 1,	/* Only finish current I/O */
	UNPACK_COMPLETE_IOD = 2,	/* Only finish current IOD */
};

/**
 * Deserialize the next csum_info in the iov and increment the iov. If a
 * csum_iov_out is provided, then serialize to it.
 */
static int
unpack_recx_csum(d_iov_t *csum_iov, d_iov_t *csum_iov_out)
{
	int rc;

	if (csum_iov == NULL || csum_iov->iov_len <= 0)
		return 0;

	/** unpack csums */
	struct dcs_csum_info *tmp_csum_info;

	D_ASSERT(csum_iov->iov_buf != NULL);
	ci_cast(&tmp_csum_info, csum_iov);
	if (tmp_csum_info == NULL) {
		D_ERROR("Expected a valid checksum info to unpack\n");
		return -DER_CSUM;
	}

	ci_move_next_iov(tmp_csum_info, csum_iov);

	if (csum_iov_out == NULL)
		return 0;

	/** will be freed with iod.recxs in clear_top_iod */
	rc = iov_alloc_for_csum_info(csum_iov_out, tmp_csum_info);
	if (rc != 0)
		return rc;

	rc = ci_serialize(tmp_csum_info, csum_iov_out);
	D_ASSERT(rc == 0);

	return 0;
}

/* Parse recxs in <*data, len> and append them to iod and sgl. */
static int
unpack_recxs(daos_iod_t *iod, daos_epoch_t **recx_ephs, int *recxs_cap,
	     daos_epoch_t *eph,  d_sg_list_t *sgl,
	     daos_key_desc_t *kds, void *data, d_iov_t *csum_iov_in,
	     d_iov_t *csum_iov_out, unsigned int type)
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

		rc = grow_array((void **)recx_ephs,
				sizeof(daos_epoch_t), *recxs_cap, cap);
		if (rc != 0)
			D_GOTO(out, rc);

		if (sgl != NULL) {
			rc = grow_array((void **)&sgl->sg_iovs,
					sizeof(*sgl->sg_iovs),
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

	(*recx_ephs)[iod->iod_nr] = rec->rec_epr.epr_lo;
	iod->iod_recxs[iod->iod_nr] = rec->rec_recx;
	iod->iod_nr++;
	iod->iod_size = rec->rec_size;

	/* Append the data and checksum (if enabled), if inline. */
	if (sgl != NULL && rec->rec_size > 0) {
		d_iov_t *iov = &sgl->sg_iovs[sgl->sg_nr];

		if (rec->rec_flags & RECX_INLINE) {
			d_iov_set(iov, data + sizeof(*rec), rec->rec_size *
					     rec->rec_recx.rx_nr);
			/** will be freed with iod.recxs in clear_top_iod */
			rc = unpack_recx_csum(csum_iov_in, csum_iov_out);
			if (rc != 0)
				D_GOTO(out, rc);
		} else {
			d_iov_set(iov, NULL, 0);
		}

		sgl->sg_nr++;
		D_ASSERTF(sgl->sg_nr <= iod->iod_nr, "%u == %u\n",
			  sgl->sg_nr, iod->iod_nr);
	}

	D_DEBUG(DB_IO, "unpacked data %p idx/nr "DF_X64"/"DF_U64
		" ver %u eph "DF_X64" size %zd epr ["DF_X64"/"DF_X64"]\n",
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
 * \param[in]		iods		daos_iod_t array
 * \param[in]		recxs_caps	recxs capacity array
 * \param[in]		sgls		optional sgl array for inline recxs
 * \param[in]		akey_ephs	akey punched ephs
 * \param[in]		punched_ephs	record punched ephs
 * \param[in]		iods_cap	maximal number of elements in \a iods,
 *					\a recxs_caps, \a sgls, and \a ephs
 */
static void
dss_enum_unpack_io_init(struct dc_obj_enum_unpack_io *io, daos_iod_t *iods,
			int *recxs_caps, d_sg_list_t *sgls,
			daos_epoch_t *akey_ephs, daos_epoch_t *punched_ephs,
			daos_epoch_t **recx_ephs, int iods_cap)
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

	io->ui_iods_top = -1;
	if (sgls != NULL) {
		memset(sgls, 0, sizeof(*sgls) * iods_cap);
		io->ui_sgls = sgls;
	}

	if (akey_ephs != NULL) {
		memset(akey_ephs, 0, sizeof(*akey_ephs) * iods_cap);
		io->ui_akey_punch_ephs = akey_ephs;
	}

	if (punched_ephs != NULL) {
		memset(punched_ephs, 0, sizeof(*punched_ephs) * iods_cap);
		io->ui_rec_punch_ephs = punched_ephs;
	}

	if (recx_ephs != NULL) {
		memset(recx_ephs, 0, sizeof(*recx_ephs) * iods_cap);
		io->ui_recx_ephs = recx_ephs;
	}
}

/**
 * Clear the iods/sgls in \a io.
 *
 * \param[in]	io	I/O descriptor
 */
static void
dc_obj_enum_unpack_io_clear(struct dc_obj_enum_unpack_io *io)
{
	int i;

	for (i = 0; i <= io->ui_iods_top; i++) {
		if (io->ui_sgls != NULL)
			d_sgl_fini(&io->ui_sgls[i], false);
		daos_iov_free(&io->ui_csum_iov);

		daos_iov_free(&io->ui_iods[i].iod_name);
		D_FREE(io->ui_iods[i].iod_recxs);
		D_FREE(io->ui_recx_ephs[i]);
		io->ui_recx_ephs[i] = NULL;
	}
	memset(io->ui_iods, 0, sizeof(*io->ui_iods) * io->ui_iods_cap);
	memset(io->ui_recxs_caps, 0,
	       sizeof(*io->ui_recxs_caps) * io->ui_iods_cap);
	if (io->ui_akey_punch_ephs != NULL)
		memset(io->ui_akey_punch_ephs, 0,
		       sizeof(*io->ui_akey_punch_ephs) * io->ui_iods_cap);
	if (io->ui_rec_punch_ephs != NULL)
		memset(io->ui_rec_punch_ephs, 0,
		       sizeof(*io->ui_rec_punch_ephs) * io->ui_iods_cap);
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
dc_obj_enum_unpack_io_fini(struct dc_obj_enum_unpack_io *io)
{
	D_ASSERTF(io->ui_iods_top == -1, "%d\n", io->ui_iods_top);
	daos_iov_free(&io->ui_csum_iov);
	daos_iov_free(&io->ui_dkey);
}

static void
clear_top_iod(struct dc_obj_enum_unpack_io *io)
{
	int idx = io->ui_iods_top;

	if (idx == -1)
		return;

	if (io->ui_iods[idx].iod_nr == 0) {
		D_DEBUG(DB_IO, "iod without recxs: %d\n", idx);

		if (io->ui_sgls != NULL)
			d_sgl_fini(&io->ui_sgls[idx], false);

		daos_iov_free(&io->ui_iods[idx].iod_name);
		D_FREE(io->ui_iods[idx].iod_recxs);
		memset(&io->ui_iods[idx], 0, sizeof(*io->ui_iods));

		io->ui_recxs_caps[idx] = 0;
		io->ui_iods_top--;
	}
}

/* Close io, pass it to cb, and clear it. */
static int
complete_io(struct dc_obj_enum_unpack_io *io, dc_obj_enum_unpack_cb_t cb, void *arg)
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
	dc_obj_enum_unpack_io_clear(io);
	return rc;
}

static int
next_iod(struct dc_obj_enum_unpack_io *io, dc_obj_enum_unpack_cb_t cb, void *cb_arg,
	 d_iov_t *iod_name);

/* complete the IO, and initialize the first IOD */
static int
complete_io_init_iod(struct dc_obj_enum_unpack_io *io,
		     dc_obj_enum_unpack_cb_t cb, void *cb_arg,
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
next_iod(struct dc_obj_enum_unpack_io *io, dc_obj_enum_unpack_cb_t cb, void *cb_arg,
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
		struct dc_obj_enum_unpack_io *io, d_iov_t *csum_iov,
		dc_obj_enum_unpack_cb_t cb, void *cb_arg)
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
			if (rc)
				return rc;
			io->ui_dkey_hash = obj_dkey2hash(io->ui_oid.id_pub, &key);
		} else if (!daos_key_match(&io->ui_dkey, &key)) {
			/* Close current IOD if dkey are different */
			rc = complete_io(io, cb, cb_arg);
			if (rc != 0)
				return rc;

			/* Update to the new dkey */
			daos_iov_free(&io->ui_dkey);
			rc = daos_iov_copy(&io->ui_dkey, &key);
			io->ui_dkey_hash = obj_dkey2hash(io->ui_oid.id_pub, &key);
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
			 struct dc_obj_enum_unpack_io *io)
{
	int idx;

	if (kds->kd_key_len != sizeof(daos_epoch_t))
		return -DER_INVAL;

	if (kds->kd_val_type == OBJ_ITER_OBJ_PUNCH_EPOCH) {
		memcpy(&io->ui_obj_punch_eph, data, kds->kd_key_len);
		return 0;
	}

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
		  struct dc_obj_enum_unpack_io *io, d_iov_t *csum_iov,
		  dc_obj_enum_unpack_cb_t cb, void *cb_arg)
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

	/* Check version/type first to see if the current IO should be complete.
	 * Only one version/type per VOS update.
	 */
	if ((io->ui_version != 0 && io->ui_version != rec->rec_version) ||
	    (io->ui_type != 0 && io->ui_type != type)) {
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

	if (io->ui_type == 0)
		io->ui_type = type;

	if (io->ui_version == 0)
		io->ui_version = rec->rec_version;

	top = io->ui_iods_top;
	rc = unpack_recxs(&io->ui_iods[top], &io->ui_recx_ephs[top],
			  &io->ui_recxs_caps[top], &io->ui_rec_punch_ephs[top],
			  io->ui_sgls == NULL ?  NULL : &io->ui_sgls[top],
			  kds, ptr, csum_iov, io_csums_iov(io), type);
free:
	daos_iov_free(&iod_akey);
	D_DEBUG(DB_IO, "unpack recxs: "DF_RC"\n", DP_RC(rc));
	return rc;
}

static int
enum_unpack_oid(daos_key_desc_t *kds, void *data,
		struct dc_obj_enum_unpack_io *io,
		dc_obj_enum_unpack_cb_t cb, void *cb_arg)
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
	struct dc_obj_enum_unpack_io	*io;
	dc_obj_enum_unpack_cb_t		cb;
	d_iov_t				*csum_iov;
	void				*cb_arg;
};

static int
enum_obj_io_unpack_cb(daos_key_desc_t *kds, void *ptr, unsigned int size,
		      void *arg)
{
	struct io_unpack_arg		*unpack_arg = arg;
	struct dc_obj_enum_unpack_io	*io = unpack_arg->io;
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
	case OBJ_ITER_OBJ_PUNCH_EPOCH:
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
	struct daos_sgl_idx sgl_idx = {0};
	char		*ptr;
	unsigned int	i;
	int		rc = 0;

	D_ASSERTF(sgl->sg_nr > 0, "%u\n", sgl->sg_nr);
	D_ASSERT(sgl->sg_iovs != NULL);
	for (i = 0; i < nr; i++) {
		daos_key_desc_t *kds = &kdss[i];

		ptr = sgl_indexed_byte(sgl, &sgl_idx);
		D_ASSERTF(ptr != NULL, "kds and sgl don't line up");

		D_DEBUG(DB_REBUILD, "process %d, type %d, ptr %p, len "DF_U64
			", total %zd\n", i, kds->kd_val_type, ptr,
			kds->kd_key_len, sgl->sg_iovs[0].iov_len);
		if (kds->kd_val_type == 0 ||
		    (kds->kd_val_type != type && type != -1)) {
			sgl_move_forward(sgl, &sgl_idx, kds->kd_key_len);
			D_DEBUG(DB_REBUILD, "skip type/size %d/%zd\n",
				kds->kd_val_type, kds->kd_key_len);
			continue;
		}

		if (kds->kd_val_type == OBJ_ITER_RECX ||
		    kds->kd_val_type == OBJ_ITER_SINGLE) {
			/*
			 * XXX: Assuming that data for a single kds is entirely
			 * contained in a single iov
			 */
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
		sgl_move_forward(sgl, &sgl_idx, kds->kd_key_len);
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
 * Unpack the result of a dc_obj_enum_pack enumeration into \a io, which can then
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
dc_obj_enum_unpack(daos_unit_oid_t oid, daos_key_desc_t *kds, int kds_num,
		   d_sg_list_t *sgl, d_iov_t *csum, dc_obj_enum_unpack_cb_t cb,
		   void *cb_arg)
{
	struct dc_obj_enum_unpack_io	io = { 0 };
	daos_iod_t			iods[OBJ_ENUM_UNPACK_MAX_IODS];
	int				recxs_caps[OBJ_ENUM_UNPACK_MAX_IODS];
	d_sg_list_t			sgls[OBJ_ENUM_UNPACK_MAX_IODS];
	daos_epoch_t			ephs[OBJ_ENUM_UNPACK_MAX_IODS];
	daos_epoch_t			punched_ephs[OBJ_ENUM_UNPACK_MAX_IODS];
	daos_epoch_t			*recx_ephs[OBJ_ENUM_UNPACK_MAX_IODS];
	d_iov_t				csum_iov_in = {0};
	struct io_unpack_arg		unpack_arg;
	int				rc = 0;

	D_ASSERT(kds_num > 0);
	D_ASSERT(kds != NULL);

	if (csum != NULL)
		/** make a copy of it because the iteration processes modifies
		 * the iov
		 */
		csum_iov_in = *csum;

	dss_enum_unpack_io_init(&io, iods, recxs_caps, sgls,
				ephs, punched_ephs, recx_ephs,
				OBJ_ENUM_UNPACK_MAX_IODS);

	D_ASSERTF(sgl->sg_nr > 0, "%u\n", sgl->sg_nr);
	D_ASSERT(sgl->sg_iovs != NULL);
	unpack_arg.cb = cb;
	unpack_arg.io = &io;
	unpack_arg.cb_arg = cb_arg;
	unpack_arg.csum_iov = &csum_iov_in;
	rc = obj_enum_iterate(kds, sgl, kds_num, -1, enum_obj_io_unpack_cb,
			      &unpack_arg);
	if (rc)
		D_GOTO(out, rc);
out:
	if (io.ui_iods_top >= 0)
		rc = complete_io(&io, cb, cb_arg);

	D_DEBUG(DB_REBUILD, "process list buf "DF_UOID" rc "DF_RC"\n",
		DP_UOID(io.ui_oid), DP_RC(rc));

	dc_obj_enum_unpack_io_fini(&io);
	return rc;
}
