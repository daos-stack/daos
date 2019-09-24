/**
 * (C) Copyright 2019 Intel Corporation.
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
 * src/object/cli_verify.c
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
#define DAOS_VERIFY_BUFSIZE	(1 << 28)

static int
dc_obj_verify_list(struct dc_obj_verify_args *dova)
{
	tse_task_t	*task;
	int		 rc;

	D_ASSERT(!dova->eof);

	memset(dova->kds, 0, sizeof(daos_key_desc_t) * DOVA_NUM);
	memset(dova->eprs, 0, sizeof(daos_epoch_range_t) * DOVA_NUM);
	memset(dova->list_buf, 0, dova->list_buf_len);

	dova->list_sgl.sg_nr = 1;
	dova->list_sgl.sg_nr_out = 1;
	dova->list_sgl.sg_iovs = &dova->list_iov;

	dova->size = 0;
	dova->num = DOVA_NUM;

	daos_anchor_set_flags(&dova->dkey_anchor,
			      DIOF_TO_SPEC_SHARD | DIOF_WITH_SPEC_EPOCH);

again:
	dova->list_iov.iov_len = 0;
	dova->list_iov.iov_buf = dova->list_buf;
	dova->list_iov.iov_buf_len = dova->list_buf_len;

	rc = dc_obj_list_obj_task_create(dova->oh, dova->th, NULL, NULL,
					 &dova->size, &dova->num, dova->kds,
					 dova->eprs, &dova->list_sgl,
					 &dova->anchor, &dova->dkey_anchor,
					 &dova->akey_anchor, true, NULL, NULL,
					 &task);
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
		rc = 1;
	} else if (rc == 0 && daos_anchor_is_eof(&dova->dkey_anchor)) {
		dova->eof = 1;
	}

	return rc;
}

static int
dc_obj_verify_fetch(struct dc_obj_verify_args *dova)
{
	struct dc_obj_verify_cursor	*cursor = &dova->cursor;
	daos_iod_t			*iod = &cursor->iod;
	tse_task_t			*task;
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

	rc = dc_obj_fetch_shard_task_create(dova->oh, dova->th,
		DIOF_TO_SPEC_SHARD, dc_obj_anchor2shard(&dova->dkey_anchor),
		&cursor->dkey, 1, iod, &dova->fetch_sgl, NULL, NULL, NULL,
		&task);
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

static int
dc_obj_verify_check_eof(struct dc_obj_verify_args *dova,
			daos_obj_id_t oid, uint32_t start, uint32_t reps)
{
	int	i;

	for (i = 1; i < reps; i++) {
		if (dova[i].cursor.type == VOS_ITER_NONE)
			continue;

		D_INFO(DF_OID" (reps %d, inconsistent) "
		       "shard %u eof, but shard %u not eof.\n",
		       DP_OID(oid), reps, start, start + i);
		return -DER_MISMATCH;
	}

	if (dova[0].eof)
		return 1;

	return 0;
}

static void
dc_obj_verify_reset_cursor(struct dc_obj_verify_cursor *cursor)
{
	cursor->kds_idx = 0;
	cursor->iod_off = 0;
	cursor->ptr = NULL;
}

static int
dc_obj_verify_parse_dkey(struct dc_obj_verify_args *dova, daos_obj_id_t oid,
			 uint32_t gen, int idx)
{
	struct dc_obj_verify_cursor	*cursor = &dova->cursor;
	daos_key_t			 dkey;

	dkey.iov_buf = cursor->ptr;
	dkey.iov_buf_len = dova->kds[idx].kd_key_len;
	dkey.iov_len = dova->kds[idx].kd_key_len;

	if (gen == cursor->gen)
		return 1;

	if (cursor->dkey.iov_buf == NULL ||
	    !daos_key_match(&cursor->dkey, &dkey)) {
		daos_iov_free(&cursor->dkey);
		daos_iov_copy(&cursor->dkey, &dkey);
	}

	cursor->gen++;
	cursor->type = VOS_ITER_DKEY;
	return 0;
}

static int
dc_obj_verify_parse_akey(struct dc_obj_verify_args *dova, daos_obj_id_t oid,
			 uint32_t gen, int idx)
{
	struct dc_obj_verify_cursor	*cursor = &dova->cursor;
	daos_key_t			 akey;

	if (cursor->dkey.iov_len == 0) {
		D_ERROR(DF_OID" dkey is empty\n", DP_OID(oid));
		return -DER_IO;
	}

	akey.iov_buf = cursor->ptr;
	akey.iov_buf_len = dova->kds[idx].kd_key_len;
	akey.iov_len = dova->kds[idx].kd_key_len;

	if (gen == cursor->gen) {
		if (cursor->type == VOS_ITER_RECX ||
		    cursor->type == VOS_ITER_SINGLE ||
		    cursor->type == VOS_ITER_AKEY)
			return 1;
	} else {
		cursor->gen++;
	}

	if (cursor->iod.iod_name.iov_buf == NULL ||
	    !daos_key_match(&cursor->iod.iod_name, &akey)) {
		daos_iov_free(&cursor->iod.iod_name);
		daos_iov_copy(&cursor->iod.iod_name, &akey);
	}

	cursor->type = VOS_ITER_AKEY;
	return 0;
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
		if (cursor->type == VOS_ITER_RECX) {
			/* The value is either SV or EV, cannot be both. */
			D_ERROR(DF_OID" akey %s contains both SV and EV.\n",
				DP_OID(oid), (char *)iod->iod_name.iov_buf);
			return -DER_IO;
		} else if (cursor->type == VOS_ITER_SINGLE) {
			/* We have already specified the epoch when enumerate,
			 * so there will be at most one SV rec can be returned
			 * for an akey.
			 */
			D_ERROR(DF_OID" akey %s returned multiple SV recs.\n",
				DP_OID(oid), (char *)iod->iod_name.iov_buf);
			return -DER_IO;
		}
	} else {
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
		D_ERROR(DF_OID" akey %s returned invalid SV rec, size %ld.\n",
			DP_OID(oid), (char *)iod->iod_name.iov_buf,
			dova->kds[idx].kd_key_len);
		return -DER_IO;
	}

	cursor->type = VOS_ITER_SINGLE;
	return 0;
}

static int
dc_obj_verify_parse_ev(struct dc_obj_verify_args *dova, daos_obj_id_t oid,
		       uint32_t gen, int idx)
{
	struct dc_obj_verify_cursor	*cursor = &dova->cursor;
	daos_iod_t			*iod = &cursor->iod;
	daos_recx_t			*i_recx = iod->iod_recxs;
	daos_recx_t			*r_recx;
	struct obj_enum_rec		*rec;
	void				*data;
	uint64_t			 tmp;
	size_t				 size;

	if (iod->iod_name.iov_len == 0) {
		D_ERROR(DF_OID" akey is empty\n", DP_OID(oid));
		return -DER_IO;
	}

	if (gen == cursor->gen) {
		if (cursor->type == VOS_ITER_SINGLE) {
			/* The value is either SV or EV, cannot be both. */
			D_ERROR(DF_OID" akey %s contains both SV and EV.\n",
				DP_OID(oid), (char *)iod->iod_name.iov_buf);
			return -DER_IO;
		}
	} else {
		cursor->gen++;
	}

	cursor->type = VOS_ITER_RECX;
	iod->iod_type = DAOS_IOD_ARRAY;
	data = cursor->ptr + cursor->iod_off;
	while (data < cursor->ptr + dova->kds[idx].kd_key_len) {
		rec = data;
		r_recx = &rec->rec_recx;

		if (iod->iod_size == DAOS_SIZE_MAX) {
			iod->iod_size = rec->rec_size;
		} else if (iod->iod_size != rec->rec_size) {
			/* Not merge punched and non-punched. */
			if (iod->iod_size == 0 || rec->rec_size == 0) {
				cursor->iod_off = data - cursor->ptr;
				return 1;
			}

			D_ERROR(DF_OID" akey %s contains multiple EV rec "
				"size %ld/%lu\n",
				DP_OID(oid), (char *)iod->iod_name.iov_buf,
				iod->iod_size, (unsigned long)rec->rec_size);
			return -DER_IO;
		}

		tmp = i_recx->rx_idx + i_recx->rx_nr;
		if (r_recx->rx_idx < tmp) {
			D_ERROR(DF_OID" akey %s contains recs "
				"overlap %lu/%lu/%lu\n",
				DP_OID(oid), (char *)iod->iod_name.iov_buf,
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
	int				 rc;
	int				 i;

	dova->data_fetched = 0;

	iod->iod_type = DAOS_IOD_NONE;
	iod->iod_size = DAOS_SIZE_MAX;
	memset(iod->iod_recxs, 0, sizeof(*iod->iod_recxs));
	cursor->type = VOS_ITER_NONE;
	if (cursor->kds_idx == dova->num) {
		if (dova->eof)
			return 1;

		goto list;
	}

again:
	if (cursor->ptr == NULL)
		cursor->ptr = dova->list_sgl.sg_iovs->iov_buf;

	D_ASSERT(cursor->ptr != NULL);

	for (i = cursor->kds_idx; i < dova->num;
	     cursor->ptr += dova->kds[i++].kd_key_len, cursor->kds_idx++) {
		switch (dova->kds[i].kd_val_type) {
		case VOS_ITER_DKEY:
			rc = dc_obj_verify_parse_dkey(dova, oid, gen, i);
			break;
		case VOS_ITER_AKEY:
			rc = dc_obj_verify_parse_akey(dova, oid, gen, i);
			break;
		case VOS_ITER_SINGLE:
			rc = dc_obj_verify_parse_sv(dova, oid, gen, i);
			break;
		case VOS_ITER_RECX:
			rc = dc_obj_verify_parse_ev(dova, oid, gen, i);
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
		return cursor->type == VOS_ITER_NONE ? 1 : 0;

	dc_obj_verify_reset_cursor(cursor);
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

	switch (cur_a->type) {
	case VOS_ITER_NONE:
		/* The end. */
		break;
	case VOS_ITER_DKEY:
		/* Punched dkey, do nothing. */
		break;
	case VOS_ITER_AKEY:
		/* Punched akey, do nothing. */
		break;
	case VOS_ITER_RECX:
		if (cur_a->iod.iod_recxs->rx_idx !=
		    cur_b->iod.iod_recxs->rx_idx) {
			D_INFO(DF_OID" (reps %u, inconsistent) "
			       "shard %u has EV rec start %lu, "
			       "but shard %u has EV rec start %lu.\n",
			       DP_OID(oid), reps, shard_a,
			       cur_a->iod.iod_recxs->rx_idx,
			       shard_b, cur_b->iod.iod_recxs->rx_idx);
			return -DER_MISMATCH;
		}

		if (cur_a->iod.iod_recxs->rx_nr !=
		    cur_b->iod.iod_recxs->rx_nr) {
			D_INFO(DF_OID" (reps %u, inconsistent) "
			       "shard %u has EV rec len %lu, "
			       "but shard %u has EV rec len %lu.\n",
			       DP_OID(oid), reps, shard_a,
			       cur_a->iod.iod_recxs->rx_nr,
			       shard_b, cur_b->iod.iod_recxs->rx_nr);
			return -DER_MISMATCH;
		}

		/* Fall through. */
	case VOS_ITER_SINGLE:
		if (cur_a->iod.iod_size != cur_b->iod.iod_size) {
			D_INFO(DF_OID" (reps %u, inconsistent) "
			       "type %u, shard %u has rec size %lu, "
			       "but shard %u has rec size %lu.\n",
			       DP_OID(oid), reps, cur_a->type, shard_a,
			       cur_a->iod.iod_size, shard_b,
			       cur_b->iod.iod_size);
			return -DER_MISMATCH;
		}

		/* Punched record, do nothing. */
		if (cur_a->iod.iod_size == 0)
			break;

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

		break;
	default:
		D_ASSERT(0);
	}

	return 0;
}

int
dc_obj_verify_rdg(struct dc_object *obj, struct dc_obj_verify_args *dova,
		  uint32_t rdg_idx, uint32_t reps, daos_epoch_t epoch)
{
	daos_obj_id_t	oid = obj->cob_md.omd_id;
	daos_handle_t	th;
	uint32_t	start = rdg_idx * reps;
	int		rc = 0;
	int		i;

	rc = dc_tx_local_open(obj->cob_coh, epoch, &th);
	if (rc != 0)
		return rc;

	for (i = 0; i < reps; i++) {
		struct dc_obj_verify_cursor	*cursor = &dova[i].cursor;

		memset(&dova[i].anchor, 0, sizeof(dova[i].anchor));
		memset(&dova[i].dkey_anchor, 0, sizeof(dova[i].dkey_anchor));
		memset(&dova[i].akey_anchor, 0, sizeof(dova[i].akey_anchor));
		dc_obj_shard2anchor(&dova[i].dkey_anchor, start + i);

		dova[i].th = th;
		dova[i].eof = 0;
		dova[i].non_exist = 0;

		cursor->gen = 0;
		cursor->ptr = NULL;
		dc_obj_verify_reset_cursor(cursor);

		rc = dc_obj_verify_list(&dova[i]);
		if (rc < 0)
			goto out;
	}

	rc = dc_obj_verify_check_existence(dova, oid, start, reps);
	if (rc != 0)
		goto out;

	do {
		for (i = 0; i < reps; i++) {
			rc = dc_obj_verify_move_cursor(&dova[i], oid);
			if (rc < 0)
				goto out;
		}

		for (i = 1; i < reps; i++) {
			rc = dc_obj_verify_cmp(&dova[0], &dova[i],
					       oid, reps, start, start + i);
			if (rc != 0)
				goto out;
		}
	} while (dova[0].cursor.type != VOS_ITER_NONE);

	rc = dc_obj_verify_check_eof(dova, oid, start, reps);

out:
	dc_tx_local_close(th);

	return rc > 0 ? 0 : rc;
}
