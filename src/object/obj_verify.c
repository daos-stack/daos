/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * src/object/obj_verify.c
 */
#define D_LOGFAC	DD_FAC(object)

#include <daos/object.h>
#include <daos/container.h>
#include <daos/pool.h>
#include <daos/task.h>
#include <daos_task.h>
#include <daos_types.h>
#include <daos_srv/vos_types.h>
#include "obj_rpc.h"
#include "obj_internal.h"

#define DAOS_SIZE_MAX		(~0ULL)
#define DAOS_VERIFY_BUFSIZE	(1 << 27)

static int
dc_obj_verify_list(struct dc_obj_verify_args *dova)
{
	tse_task_t	*task;
	int		 rc;

	D_ASSERT(!dova->eof);

	memset(dova->kds, 0, sizeof(daos_key_desc_t) * DOVA_NUM);
	memset(dova->list_buf, 0, dova->list_buf_len);

	dova->list_sgl.sg_nr = 1;
	dova->list_sgl.sg_nr_out = 1;
	dova->list_sgl.sg_iovs = &dova->list_iov;

	dova->size = 0;
	dova->num = DOVA_NUM;

again:
	dova->list_iov.iov_len = 0;
	dova->list_iov.iov_buf = dova->list_buf;
	dova->list_iov.iov_buf_len = dova->list_buf_len;

	rc = dc_obj_list_obj_task_create(dova->oh, dova->th, NULL, NULL, NULL,
					 &dova->size, &dova->num, dova->kds,
					 &dova->list_sgl, &dova->anchor,
					 &dova->dkey_anchor, &dova->akey_anchor,
					 true, NULL, NULL, NULL, &task);
	if (rc != 0)
		return rc;

	rc = dc_task_schedule(task, true);
	if (rc == -DER_KEY2BIG) {
		dova->list_buf_len = roundup(dova->kds[0].kd_key_len * 2, 8);
		if (dova->list_buf != dova->inline_buf)
			D_FREE(dova->list_buf);

		D_ALLOC(dova->list_buf, dova->list_buf_len);
		if (dova->list_buf != NULL)
			goto again;

		return -DER_NOMEM;
	}

	/* The verification works on stable epoch. If pool map
	 * is refreshed, we just re-check from current position.
	 */
	if (rc == -DER_STALE)
		goto again;

	if (rc == -DER_NONEXIST) {
		dova->non_exist = 1;
		dova->eof = 1;
		return 1;
	}

	if (rc == 0 && (daos_anchor_is_eof(&dova->dkey_anchor) || dova->num < 2))
		dova->eof = 1;

	return rc;
}

static int
dc_obj_verify_fetch(struct dc_obj_verify_args *dova)
{
	struct dc_obj_verify_cursor	*cursor = &dova->cursor;
	daos_iod_t			*iod = &cursor->iod;
	tse_task_t			*task;
	uint32_t			 shard;
	size_t				 size;
	int				 rc;

	if (dova->data_fetched)
		return 0;

	size = roundup(iod->iod_size * iod->iod_recxs->rx_nr, 8);
	if (size > dova->fetch_buf_len) {
		D_FREE(dova->fetch_buf);
		dova->fetch_buf_len = size;
	} else if (dova->fetch_buf == NULL) {
		dova->fetch_buf_len = size;
	}

	if (dova->fetch_buf == NULL) {
		D_ALLOC(dova->fetch_buf, dova->fetch_buf_len);
		if (dova->fetch_buf == NULL)
			return -DER_NOMEM;
	}

	dova->fetch_iov.iov_len = 0;
	dova->fetch_iov.iov_buf = dova->fetch_buf;
	dova->fetch_iov.iov_buf_len = dova->fetch_buf_len;

	dova->fetch_sgl.sg_nr = 1;
	dova->fetch_sgl.sg_nr_out = 1;
	dova->fetch_sgl.sg_iovs = &dova->fetch_iov;

	shard = dc_obj_anchor2shard(&dova->dkey_anchor);
	rc = dc_obj_fetch_task_create(dova->oh, dova->th, 0, &cursor->dkey, 1,
				      DIOF_TO_SPEC_SHARD, iod, &dova->fetch_sgl,
				      NULL, &shard, NULL, NULL, NULL, &task);
	if (rc == 0)
		rc = dc_task_schedule(task, true);

	if (rc == 0)
		dova->data_fetched = 1;

	return rc;
}

static int
dc_obj_verify_check_existence(struct dc_obj_verify_args *dova,
			      daos_obj_id_t oid, uint32_t start, uint32_t reps)
{
	int	i;

	for (i = 1; i < reps; i++) {
		if (dova[0].non_exist == dova[i].non_exist)
			continue;

		D_INFO(DF_OID" (reps %d, inconsistent) "
		       "shard %u %s, but shard %u %s.\n",
		       DP_OID(oid), reps, start,
		       dova[0].non_exist ? "non-exist" : "exist",
		       start + i, dova[i].non_exist ? "non-exist" : "exist");
		return -DER_MISMATCH;
	}

	return dova[0].non_exist ? 1 : 0;
}

/*
 * \return	1: Next one belong to another dkey, stop current cursor moving.
 * \return	0: Next one belong to the same dkey, continue to move cursor.
 * \return	Negative value if error.
 */
static int
dc_obj_verify_parse_dkey(struct dc_obj_verify_args *dova, daos_obj_id_t oid,
			 uint32_t gen, int idx)
{
	struct dc_obj_verify_cursor	*cursor = &dova->cursor;
	daos_key_t			 dkey;
	int				 rc = 0;

	dkey.iov_buf = cursor->ptr;
	dkey.iov_buf_len = dova->kds[idx].kd_key_len;
	dkey.iov_len = dova->kds[idx].kd_key_len;

	if (gen == cursor->gen) {
		D_ASSERT(cursor->type != OBJ_ITER_NONE);
		D_ASSERT(cursor->dkey.iov_buf != NULL);

		if (!daos_key_match(&cursor->dkey, &dkey))
			rc = 1;

		return rc;
	}

	D_ASSERTF(cursor->type == OBJ_ITER_NONE,
		  "Invalid cursor type(1) %d\n", cursor->type);

	cursor->type = OBJ_ITER_DKEY;
	cursor->gen++;

	if (cursor->dkey.iov_buf == NULL ||
	    !daos_key_match(&cursor->dkey, &dkey)) {
		daos_iov_free(&cursor->dkey);
		rc = daos_iov_copy(&cursor->dkey, &dkey);
	}

	return rc;
}

/*
 * \return	1: Next one belong to another akey, stop current cursor moving.
 * \return	0: Next one belong to the same akey, continue to move cursor.
 * \return	Negative value if error.
 */
static int
dc_obj_verify_parse_akey(struct dc_obj_verify_args *dova, daos_obj_id_t oid,
			 uint32_t gen, int idx)
{
	struct dc_obj_verify_cursor	*cursor = &dova->cursor;
	daos_key_t			 akey;
	int				 rc = 0;

	if (cursor->dkey.iov_len == 0) {
		D_ERROR(DF_OID" dkey is empty\n", DP_OID(oid));
		return -DER_IO;
	}

	akey.iov_buf = cursor->ptr;
	akey.iov_buf_len = dova->kds[idx].kd_key_len;
	akey.iov_len = dova->kds[idx].kd_key_len;

	if (gen == cursor->gen) {
		D_ASSERT(cursor->type != OBJ_ITER_NONE);

		if (cursor->type == OBJ_ITER_RECX ||
		    cursor->type == OBJ_ITER_SINGLE ||
		    cursor->type == OBJ_ITER_AKEY) {
			D_ASSERT(cursor->iod.iod_name.iov_buf != NULL);

			if (!daos_key_match(&cursor->iod.iod_name, &akey))
				rc = 1;

			return rc;
		}

		if (cursor->type == OBJ_ITER_DKEY)
			cursor->type = OBJ_ITER_AKEY;
	} else {
		D_ASSERTF(cursor->type == OBJ_ITER_NONE,
			  "Invalid cursor type(2) %d\n", cursor->type);

		cursor->type = OBJ_ITER_AKEY;
		cursor->gen++;
	}

	if (cursor->iod.iod_name.iov_buf == NULL ||
	    !daos_key_match(&cursor->iod.iod_name, &akey)) {
		daos_iov_free(&cursor->iod.iod_name);
		rc = daos_iov_copy(&cursor->iod.iod_name, &akey);
	}

	return rc;
}

static int
dc_obj_verify_parse_sv(struct dc_obj_verify_args *dova, daos_obj_id_t oid,
		       uint32_t gen, int idx)
{
	struct dc_obj_verify_cursor	*cursor = &dova->cursor;
	daos_iod_t			*iod = &cursor->iod;
	struct obj_enum_rec		*rec;
	void				*data;
	size_t				 size;

	if (iod->iod_name.iov_len == 0) {
		D_ERROR(DF_OID" akey is empty\n", DP_OID(oid));
		return -DER_IO;
	}

	if (gen == cursor->gen) {
		D_ASSERT(cursor->type != OBJ_ITER_NONE);

		if (cursor->type == OBJ_ITER_RECX) {
			/* The value is either SV or EV, cannot be both. */
			D_ERROR(DF_OID" akey "DF_KEY
				" misc SV and EV together.\n",
				DP_OID(oid), DP_KEY(&iod->iod_name));
			return -DER_IO;
		}

		if (cursor->type == OBJ_ITER_SINGLE) {
			/* We have already specified the epoch when enumerate,
			 * so there will be at most one SV rec can be returned
			 * for an akey.
			 */
			D_ERROR(DF_OID" akey "DF_KEY
				" returned multiple SV recs.\n",
				DP_OID(oid), DP_KEY(&iod->iod_name));
			return -DER_IO;
		}
	} else {
		D_ASSERTF(cursor->type == OBJ_ITER_NONE,
			  "Invalid cursor type(3) %d\n", cursor->type);

		cursor->gen++;
	}

	rec = cursor->ptr;
	data = cursor->ptr + sizeof(*rec);

	iod->iod_type = DAOS_IOD_SINGLE;
	iod->iod_size = rec->rec_size;
	iod->iod_recxs->rx_idx = 0;
	iod->iod_recxs->rx_nr = 1;

	/* Inline data. */
	if (rec->rec_flags & RECX_INLINE) {
		size = rec->rec_size * rec->rec_recx.rx_nr;
		if (size > dova->fetch_buf_len) {
			D_FREE(dova->fetch_buf);
			dova->fetch_buf_len = roundup(size, 8);
		} else if (dova->fetch_buf == NULL) {
			dova->fetch_buf_len = roundup(size, 8);
		}

		if (dova->fetch_buf == NULL) {
			D_ALLOC(dova->fetch_buf, dova->fetch_buf_len);
			if (dova->fetch_buf == NULL)
				return -DER_NOMEM;
		}

		memcpy(dova->fetch_buf, data, size);
		dova->fetch_iov.iov_len = size;
		dova->fetch_iov.iov_buf = dova->fetch_buf;
		dova->fetch_iov.iov_buf_len = dova->fetch_buf_len;
		dova->data_fetched = 1;

		data += size;
	}

	if (data != cursor->ptr + dova->kds[idx].kd_key_len) {
		D_ERROR(DF_OID" akey "DF_KEY
			" returned invalid SV rec, size %ld.\n",
			DP_OID(oid), DP_KEY(&iod->iod_name),
			dova->kds[idx].kd_key_len);
		return -DER_IO;
	}

	cursor->type = OBJ_ITER_SINGLE;
	return 0;
}

static int
dc_obj_verify_parse_ev(struct dc_obj_verify_args *dova, daos_obj_id_t oid,
		       uint32_t gen, int idx)
{
	struct dc_obj_verify_cursor	*cursor = &dova->cursor;
	daos_iod_t			*iod = &cursor->iod;
	daos_recx_t			*i_recx = iod->iod_recxs;
	void				*data;
	uint64_t			 tmp;
	size_t				 size;

	if (iod->iod_name.iov_len == 0) {
		D_ERROR(DF_OID" akey is empty\n", DP_OID(oid));
		return -DER_IO;
	}

	if (gen == cursor->gen) {
		D_ASSERT(cursor->type != OBJ_ITER_NONE);

		if (cursor->type == OBJ_ITER_SINGLE) {
			/* The value is either SV or EV, cannot be both. */
			D_ERROR(DF_OID" akey "DF_KEY
				" misc EV and SV together.\n",
				DP_OID(oid), DP_KEY(&iod->iod_name));
			return -DER_IO;
		}
	} else {
		D_ASSERTF(cursor->type == OBJ_ITER_NONE,
			  "Invalid cursor type(4) %d\n", cursor->type);

		cursor->gen++;
	}

	cursor->type = OBJ_ITER_RECX;
	iod->iod_type = DAOS_IOD_ARRAY;
	data = cursor->ptr + cursor->iod_off;

	while (data < cursor->ptr + dova->kds[idx].kd_key_len) {
		struct obj_enum_rec	*rec = data;
		daos_recx_t		*r_recx = &rec->rec_recx;

		if (iod->iod_size == DAOS_SIZE_MAX) {
			iod->iod_size = rec->rec_size;
		} else if (iod->iod_size != rec->rec_size) {
			/* Not merge punched and non-punched. */
			if (iod->iod_size == 0 || rec->rec_size == 0) {
				cursor->iod_off = data - cursor->ptr;
				return 1;
			}

			D_ERROR(DF_OID" akey "DF_KEY
				"contains multiple EV rec size %ld/%lu\n",
				DP_OID(oid), DP_KEY(&iod->iod_name),
				iod->iod_size, (unsigned long)rec->rec_size);
			return -DER_IO;
		}

		tmp = i_recx->rx_idx + i_recx->rx_nr;
		if (r_recx->rx_idx < tmp) {
			D_ERROR(DF_OID" akey "DF_KEY
				" contains recs overlap %lu/%lu/%lu\n",
				DP_OID(oid), DP_KEY(&iod->iod_name),
				(unsigned long)r_recx->rx_idx,
				(unsigned long)i_recx->rx_idx,
				(unsigned long)i_recx->rx_nr);
			return -DER_IO;
		}

		if (tmp == 0) {
			i_recx->rx_idx = r_recx->rx_idx;
		} else if (r_recx->rx_idx > tmp) {
			/* Not adjacent, cannot be merged. */
			cursor->iod_off = data - cursor->ptr;
			return 1;
		}

		i_recx->rx_nr += r_recx->rx_nr;
		size = iod->iod_size * i_recx->rx_nr;
		if (size > DAOS_VERIFY_BUFSIZE) {
			/* To avoid the trouble caused by huge buffer, we split
			 * current record and handle remaining in next cycle.
			 */
			tmp = (size - DAOS_VERIFY_BUFSIZE) / iod->iod_size;
			i_recx->rx_nr -= tmp;
			r_recx->rx_idx += r_recx->rx_nr - tmp;
			r_recx->rx_nr = tmp;

			cursor->iod_off = data - cursor->ptr;
			return 1;
		}

		data += sizeof(*rec);
		/* Ignore inline data to simplify the logic. */
		if (rec->rec_flags & RECX_INLINE)
			data += rec->rec_size * r_recx->rx_nr;
	}

	cursor->iod_off = 0;
	return 0;
}

static int
dc_obj_verify_move_cursor(struct dc_obj_verify_args *dova, daos_obj_id_t oid)
{
	struct dc_obj_verify_cursor	*cursor = &dova->cursor;
	daos_iod_t			*iod = &cursor->iod;
	uint32_t			 gen = cursor->gen + 1;
	int				 rc = 0;
	int				 i;

	dova->data_fetched = 0;

	iod->iod_type = DAOS_IOD_NONE;
	iod->iod_size = DAOS_SIZE_MAX;
	memset(iod->iod_recxs, 0, sizeof(*iod->iod_recxs));
	cursor->type = OBJ_ITER_NONE;
	if (cursor->kds_idx == dova->num) {
		if (dova->eof)
			return 0;

		goto list;
	}

again:
	if (cursor->ptr == NULL) {
		cursor->ptr = dova->list_sgl.sg_iovs->iov_buf;
		D_ASSERT(cursor->ptr != NULL);
	}

	for (i = cursor->kds_idx; i < dova->num;
	     cursor->ptr += dova->kds[i++].kd_key_len, cursor->kds_idx++) {
		switch (dova->kds[i].kd_val_type) {
		case OBJ_ITER_DKEY:
			rc = dc_obj_verify_parse_dkey(dova, oid, gen, i);
			break;
		case OBJ_ITER_AKEY:
			rc = dc_obj_verify_parse_akey(dova, oid, gen, i);
			break;
		case OBJ_ITER_SINGLE:
			rc = dc_obj_verify_parse_sv(dova, oid, gen, i);
			break;
		case OBJ_ITER_RECX:
			rc = dc_obj_verify_parse_ev(dova, oid, gen, i);
			break;
		case OBJ_ITER_DKEY_EPOCH:
		case OBJ_ITER_AKEY_EPOCH:
		case OBJ_ITER_OBJ:
			break;
		default:
			D_ERROR(DF_OID" invalid type %d\n",
				DP_OID(oid), dova->kds[i].kd_val_type);
			return -DER_INVAL;
		}

		if (rc != 0)
			return rc > 0 ? 0 : rc;
	}

list:
	if (dova->eof)
		return 0;

	cursor->kds_idx = 0;
	cursor->iod_off = 0;
	cursor->ptr = NULL;

	rc = dc_obj_verify_list(dova);
	if (rc < 0)
		return rc;

	D_ASSERT(rc == 0);
	goto again;
}

static int
dc_obj_verify_cmp(struct dc_obj_verify_args *dova_a,
		  struct dc_obj_verify_args *dova_b, daos_obj_id_t oid,
		  uint32_t reps, uint32_t shard_a, uint32_t shard_b)
{
	struct dc_obj_verify_cursor	*cur_a = &dova_a->cursor;
	struct dc_obj_verify_cursor	*cur_b = &dova_b->cursor;
	int				 rc;

	if (cur_a->type != cur_b->type) {
		D_INFO(DF_OID" (reps %u, inconsistent) "
		       "shard %u has rec type %u, "
		       "but shard %u has rec type %u.\n",
		       DP_OID(oid), reps, shard_a, cur_a->type,
		       shard_b, cur_b->type);
		return -DER_MISMATCH;
	}

	/* The end. */
	if (cur_a->type == OBJ_ITER_NONE)
		return 0;

	if (!daos_key_match(&cur_a->dkey, &cur_b->dkey)) {
		D_INFO(DF_OID" (reps %u, inconsistent) "
			"shard %u has dkey "DF_KEY", but shard %u has dkey "DF_KEY".\n",
			DP_OID(oid), reps,
			shard_a, DP_KEY(&cur_a->dkey),
			shard_b, DP_KEY(&cur_b->dkey));
		return -DER_MISMATCH;
	}

	/* Punched dkey. */
	if (cur_a->type == OBJ_ITER_DKEY_EPOCH || cur_a->type == OBJ_ITER_DKEY)
		return 0;

	if (!daos_key_match(&cur_a->iod.iod_name, &cur_b->iod.iod_name)) {
		D_INFO(DF_OID" (reps %u, inconsistent) shard %u has akey "
		       DF_KEY", but shard %u has akey "DF_KEY".\n",
		       DP_OID(oid), reps,
		       shard_a, DP_KEY(&cur_a->iod.iod_name),
		       shard_b, DP_KEY(&cur_b->iod.iod_name));
		return -DER_MISMATCH;
	}

	/* Punched akey. */
	if (cur_a->type == OBJ_ITER_AKEY_EPOCH || cur_a->type == OBJ_ITER_AKEY)
		return 0;

	if (cur_a->type == OBJ_ITER_RECX) {
		if (cur_a->iod.iod_recxs->rx_idx !=
		    cur_b->iod.iod_recxs->rx_idx) {
			D_INFO(DF_OID" (reps %u, inconsistent) "
			       "shard %u has EV rec start %lu, "
			       "but shard %u has EV rec start %lu.\n",
			       DP_OID(oid), reps,
			       shard_a, cur_a->iod.iod_recxs->rx_idx,
			       shard_b, cur_b->iod.iod_recxs->rx_idx);
			return -DER_MISMATCH;
		}

		if (cur_a->iod.iod_recxs->rx_nr !=
		    cur_b->iod.iod_recxs->rx_nr) {
			D_INFO(DF_OID" (reps %u, inconsistent) "
			       "shard %u has EV rec len %lu, "
			       "but shard %u has EV rec len %lu.\n",
			       DP_OID(oid), reps,
			       shard_a, cur_a->iod.iod_recxs->rx_nr,
			       shard_b, cur_b->iod.iod_recxs->rx_nr);
			return -DER_MISMATCH;
		}
	}

	if (cur_a->iod.iod_size != cur_b->iod.iod_size) {
		D_INFO(DF_OID" (reps %u, inconsistent) "
		       "type %u, shard %u has rec size %lu, "
		       "but shard %u has rec size %lu.\n",
		       DP_OID(oid), reps, cur_a->type, shard_a,
		       cur_a->iod.iod_size, shard_b, cur_b->iod.iod_size);
		return -DER_MISMATCH;
	}

	/* Punched record, do nothing. */
	if (cur_a->iod.iod_size == 0)
		return 0;

	D_ASSERT(cur_a->iod.iod_size != DAOS_SIZE_MAX);

	rc = dc_obj_verify_fetch(dova_a);
	if (rc != 0)
		return rc;

	rc = dc_obj_verify_fetch(dova_b);
	if (rc != 0)
		return rc;

	D_ASSERT(dova_a->fetch_iov.iov_buf == dova_a->fetch_buf);
	D_ASSERT(dova_b->fetch_iov.iov_buf == dova_b->fetch_buf);

	if (dova_a->fetch_iov.iov_len != dova_b->fetch_iov.iov_len) {
		D_INFO(DF_OID" (reps %u, inconsistent) "
		       "type %u, fetched %ld bytes from shard %u, "
		       "but fetched %ld bytes from shard %u.\n",
		       DP_OID(oid), reps, cur_a->type,
		       dova_a->fetch_iov.iov_len, shard_a,
		       dova_b->fetch_iov.iov_len, shard_b);
		return -DER_MISMATCH;
	}

	if (memcmp(dova_a->fetch_iov.iov_buf, dova_b->fetch_iov.iov_buf,
		   dova_a->fetch_iov.iov_len) != 0) {
		D_INFO(DF_OID" (reps %u, inconsistent) "
		       "type %u, shard %u and shard %u have "
		       "different data, size %lu.\n",
		       DP_OID(oid), reps, cur_a->type, shard_a, shard_b,
		       dova_a->fetch_iov.iov_len);
		return -DER_MISMATCH;
	}

	return 0;
}

static int
dc_obj_verify_ec_cb(struct dss_enum_unpack_io *io, void *arg)
{
	struct dc_obj_verify_args	*dova = arg;
	struct dc_object		*obj = obj_hdl2ptr(dova->oh);
	d_sg_list_t			sgls[DSS_ENUM_UNPACK_MAX_IODS];
	d_iov_t				iovs[DSS_ENUM_UNPACK_MAX_IODS] = { 0 };
	d_sg_list_t			sgls_verify[DSS_ENUM_UNPACK_MAX_IODS];
	d_iov_t				iovs_verify[DSS_ENUM_UNPACK_MAX_IODS] = { 0 };
	daos_iod_t			iods[DSS_ENUM_UNPACK_MAX_IODS] = { 0 };
	tse_task_t			*task;
	tse_task_t			*verify_task;
	uint64_t			shard = dova->current_shard;
	int				nr = io->ui_iods_top + 1;
	int				i;
	int				idx = 0;
	int				rc;

	D_DEBUG(DB_TRACE, "compare "DF_KEY" nr %d shard "DF_U64"\n", DP_KEY(&io->ui_dkey),
		nr, shard);
	if (nr == 0)
		return 0;

	for (i = 0; i < nr; i++) {
		daos_size_t	size;
		char		*data;
		char		*data_verify;
		daos_iod_t	*iod = &io->ui_iods[i];

		/* skip punched iod */
		if (iod->iod_size == 0)
			continue;

		size = daos_iods_len(iod, 1);
		D_ASSERT(size != -1);
		D_ALLOC(data, size);
		if (data == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		d_iov_set(&iovs[idx], data, size);
		sgls[idx].sg_nr = 1;
		sgls[idx].sg_nr_out = 1;
		sgls[idx].sg_iovs = &iovs[idx];

		D_ALLOC(data_verify, size);
		if (data_verify == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		d_iov_set(&iovs_verify[idx], data_verify, size);
		sgls_verify[idx].sg_nr = 1;
		sgls_verify[idx].sg_nr_out = 1;
		sgls_verify[idx].sg_iovs = &iovs_verify[idx];
		if (iod->iod_type == DAOS_IOD_ARRAY) {
			rc = obj_recx_ec2_daos(obj_get_oca(obj),
					       io->ui_oid.id_shard,
					       &iod->iod_recxs, &iod->iod_nr);
			if (rc != 0)
				D_GOTO(out, rc);
		}
		iods[idx++] = *iod;
	}

	if (idx == 0) {
		D_DEBUG(DB_TRACE, "all punched "DF_KEY" nr %d shard "DF_U64"\n",
			DP_KEY(&io->ui_dkey), nr, shard);
		return 0;
	}

	/* Fetch by specific shard */
	rc = dc_obj_fetch_task_create(dova->oh, dova->th, 0, &io->ui_dkey, idx,
				      0, iods, sgls, NULL, &shard, NULL, NULL, NULL,
				      &task);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = dc_task_schedule(task, true);
	if (rc != 0)
		D_GOTO(out, rc);

	daos_fail_loc_set(DAOS_OBJ_FORCE_DEGRADE | DAOS_FAIL_ONCE);
	rc = dc_obj_fetch_task_create(dova->oh, dova->th, 0, &io->ui_dkey, idx,
				      0, iods, sgls_verify, NULL, &shard, NULL, NULL,
				      NULL, &verify_task);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = dc_task_schedule(verify_task, true);
	if (rc)
		D_GOTO(out, rc);
	daos_fail_loc_set(0);

	for (i = 0; i < idx; i++) {
		if (sgls[i].sg_iovs[0].iov_len != sgls_verify[i].sg_iovs[0].iov_len ||
		    memcmp(sgls[i].sg_iovs[0].iov_buf, sgls_verify[i].sg_iovs[0].iov_buf,
			   sgls[i].sg_iovs[0].iov_len)) {
			D_ERROR(DF_OID" shard %u mismatch\n", DP_OID(obj->cob_md.omd_id),
				dova->current_shard);
			D_GOTO(out, rc = -DER_MISMATCH);
		}
		D_DEBUG(DB_TRACE, DF_OID" shard %u match\n", DP_OID(obj->cob_md.omd_id),
			dova->current_shard);
	}
out:
	for (i = 0; i < idx; i++) {
		if (iovs[i].iov_buf)
			D_FREE(iovs[i].iov_buf);

		if (iovs_verify[i].iov_buf)
			D_FREE(iovs_verify[i].iov_buf);
	}

	return rc;
}

static int
dc_obj_verify_ec_rdg(struct dc_object *obj, struct dc_obj_verify_args *dova,
		     uint32_t rdg_idx, daos_handle_t th)
{
	struct daos_oclass_attr *oca;
	uint32_t		start;
	int			data_nr;
	int			i;
	int			rc = 0;

	oca = obj_get_oca(obj);
	D_ASSERT(oca->ca_resil == DAOS_RES_EC);
	data_nr = obj_ec_data_tgt_nr(oca);
	start = rdg_idx * obj_ec_tgt_nr(oca);
	for (i = 0; i < data_nr; i++) {
		struct dc_obj_verify_cursor	*cursor = &dova->cursor;
		daos_unit_oid_t			oid;

		memset(&dova->anchor, 0, sizeof(dova->anchor));
		memset(&dova->dkey_anchor, 0, sizeof(dova->dkey_anchor));
		memset(&dova->akey_anchor, 0, sizeof(dova->akey_anchor));
		dc_obj_shard2anchor(&dova->dkey_anchor, start + i);
		daos_anchor_set_flags(&dova->dkey_anchor, DIOF_TO_SPEC_SHARD |
							  DIOF_WITH_SPEC_EPOCH);
		dova->th = th;
		dova->eof = 0;
		dova->non_exist = 0;
		dova->current_shard = i;
		memset(cursor, 0, sizeof(*cursor));
		cursor->iod.iod_nr = 1;
		cursor->iod.iod_recxs = &cursor->recx;

		oid.id_pub = obj->cob_md.omd_id;
		oid.id_shard = start + i;
		while (!dova->eof) {
			rc = dc_obj_verify_list(dova);
			if (rc < 0) {
				D_ERROR("Failed to verify object list: "DF_RC"\n",
					DP_RC(rc));
				D_GOTO(out, rc);
			}

			rc = dss_enum_unpack(oid, dova->kds, dova->num, &dova->list_sgl,
					     NULL, dc_obj_verify_ec_cb, dova);
			if (rc) {
				D_ERROR("Failed to verify ec object: "DF_RC"\n",
					DP_RC(rc));
				D_GOTO(out, rc);
			}
		}
	}

out:
	return rc;
}

/* verify the replication object */
static int
dc_obj_verify_rep_rdg(struct dc_object *obj, struct dc_obj_verify_args *dova,
		      uint32_t rdg_idx, uint32_t reps, daos_handle_t th)
{
	daos_obj_id_t	oid = obj->cob_md.omd_id;
	uint32_t	start = rdg_idx * reps;
	int		rc = 0;
	int		i;

	for (i = 0; i < reps; i++) {
		struct dc_obj_verify_cursor	*cursor = &dova[i].cursor;

		memset(&dova[i].anchor, 0, sizeof(dova[i].anchor));
		memset(&dova[i].dkey_anchor, 0, sizeof(dova[i].dkey_anchor));
		memset(&dova[i].akey_anchor, 0, sizeof(dova[i].akey_anchor));
		dc_obj_shard2anchor(&dova[i].dkey_anchor, start + i);
		daos_anchor_set_flags(&dova[i].dkey_anchor,
				DIOF_TO_SPEC_SHARD | DIOF_WITH_SPEC_EPOCH);

		dova[i].th = th;
		dova[i].eof = 0;
		dova[i].non_exist = 0;

		memset(cursor, 0, sizeof(*cursor));
		/* We merge the recxs if they can be merged.
		 * So always single IOD.
		 */
		cursor->iod.iod_nr = 1;
		cursor->iod.iod_recxs = &cursor->recx;

		rc = dc_obj_verify_list(&dova[i]);
		if (rc < 0) {
			D_ERROR("Failed to verify object list: "DF_RC"\n",
				DP_RC(rc));
			goto out;
		}
	}

	rc = dc_obj_verify_check_existence(dova, oid, start, reps);
	if (rc != 0)
		goto out;

	do {
		for (i = 0; i < reps; i++) {
			rc = dc_obj_verify_move_cursor(&dova[i], oid);
			if (rc != 0) {
				D_ERROR("Failed to verify cursor: "DF_RC"\n",
					DP_RC(rc));
				goto out;
			}
		}

		for (i = 1; i < reps; i++) {
			rc = dc_obj_verify_cmp(&dova[0], &dova[i],
					       oid, reps, start, start + i);
			if (rc == -DER_CSUM) {
				D_ERROR("Failed to verify because of "
					"data corruption");
				D_GOTO(out, rc = -DER_MISMATCH);
			}
			if (rc != 0) {
				D_ERROR("Failed to verify cmp: "DF_RC"\n",
					DP_RC(rc));
				goto out;
			}
		}
	} while (dova[0].cursor.type != OBJ_ITER_NONE);

	D_ASSERT(dova[0].eof);

	/* Check EOF */
	for (i = 1; i < reps; i++) {
		if (dova[i].cursor.type != OBJ_ITER_NONE || !dova[i].eof) {
			D_INFO(DF_OID" (reps %d, inconsistent) "
			       "shard %u eof, but shard %u not eof.\n",
			       DP_OID(oid), reps, start, start + i);
			D_GOTO(out, rc = -DER_MISMATCH);
		}
	}
out:
	return rc;
}

int
dc_obj_verify_rdg(struct dc_object *obj, struct dc_obj_verify_args *dova,
		  uint32_t rdg_idx, uint32_t reps, daos_epoch_t epoch)
{
	daos_handle_t	th;
	int		rc;

	rc = dc_tx_local_open(obj->cob_coh, epoch, 0, &th);
	if (rc != 0) {
		D_ERROR("dc_tx_local-open failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (obj_is_ec(obj))
		rc = dc_obj_verify_ec_rdg(obj, dova, rdg_idx, th);
	else
		rc = dc_obj_verify_rep_rdg(obj, dova, rdg_idx, reps, th);

	dc_tx_local_close(th);

	return rc > 0 ? 0 : rc;
}
