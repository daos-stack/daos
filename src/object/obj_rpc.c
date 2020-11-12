/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * DSR: RPC Protocol Serialization Functions
 */
#define D_LOGFAC	DD_FAC(object)

#include <daos/common.h>
#include <daos/event.h>
#include <daos/rpc.h>
#include <daos/object.h>
#include "obj_rpc.h"
#include "rpc_csum.h"

static int
crt_proc_daos_key_desc_t(crt_proc_t proc, daos_key_desc_t *key)
{
	int rc;

	rc = crt_proc_uint64_t(proc, &key->kd_key_len);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &key->kd_val_type);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_obj_id_t(crt_proc_t proc, daos_obj_id_t *doi)
{
	int rc;

	rc = crt_proc_uint64_t(proc, &doi->lo);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &doi->hi);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_unit_oid_t(crt_proc_t proc, daos_unit_oid_t *doi)
{
	int rc;

	rc = crt_proc_daos_obj_id_t(proc, &doi->id_pub);
	if (rc != 0)
		return rc;

	rc = crt_proc_uint32_t(proc, &doi->id_shard);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &doi->id_pad_32);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_recx_t(crt_proc_t proc, daos_recx_t *recx)
{
	int rc;

	rc = crt_proc_uint64_t(proc, &recx->rx_idx);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &recx->rx_nr);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_epoch_range_t(crt_proc_t proc, daos_epoch_range_t *erange)
{
	int rc;

	rc = crt_proc_uint64_t(proc, &erange->epr_lo);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &erange->epr_hi);
	if (rc != 0)
		return -DER_HG;

	return 0;
}


static int
crt_proc_struct_obj_shard_iod(crt_proc_t proc, struct obj_shard_iod *siod)
{
	if (crt_proc_uint32_t(proc, &siod->siod_tgt_idx) != 0)
		return -DER_HG;
	if (crt_proc_uint32_t(proc, &siod->siod_idx) != 0)
		return -DER_HG;
	if (crt_proc_uint32_t(proc, &siod->siod_nr) != 0)
		return -DER_HG;
	if (crt_proc_uint64_t(proc, &siod->siod_off) != 0)
		return -DER_HG;
	return 0;
}

static int
crt_proc_struct_obj_io_desc(crt_proc_t proc, struct obj_io_desc *oiod)
{
	crt_proc_op_t	proc_op;
	uint32_t	i;
	int		rc;

	rc = crt_proc_uint16_t(proc, &oiod->oiod_nr);
	if (rc)
		return -DER_HG;
	rc = crt_proc_uint16_t(proc, &oiod->oiod_tgt_idx);
	if (rc)
		return -DER_HG;
	rc = crt_proc_uint32_t(proc, &oiod->oiod_flags);
	if (rc)
		return -DER_HG;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;
	if (proc_op == CRT_PROC_DECODE && oiod->oiod_nr > 0) {
		rc = obj_io_desc_init(oiod, oiod->oiod_nr, oiod->oiod_flags);
		if (rc)
			return rc;
	}

	for (i = 0; oiod->oiod_siods != NULL && i < oiod->oiod_nr; i++) {
		rc = crt_proc_struct_obj_shard_iod(proc, &oiod->oiod_siods[i]);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				obj_io_desc_fini(oiod);
			return -DER_HG;
		}
	}

	if (proc_op == CRT_PROC_FREE && oiod->oiod_siods != NULL)
		obj_io_desc_fini(oiod);

	return 0;
}

#define IOD_REC_EXIST	(1 << 0)
static int
crt_proc_daos_iod_and_csum(crt_proc_t proc, crt_proc_op_t proc_op,
			   daos_iod_t *iod, struct dcs_iod_csums *iod_csum,
			   struct obj_io_desc *oiod)
{
	uint32_t	i, start, nr;
	bool		proc_one = false;
	bool		singv = false;
	uint32_t	existing_flags = 0;
	int		rc;

	if (proc == NULL || iod == NULL) {
		D_ERROR("Invalid parameter, proc: %p, data: %p.\n", proc, iod);
		return -DER_INVAL;
	}

	rc = crt_proc_d_iov_t(proc, &iod->iod_name);
	if (rc != 0)
		return rc;

	rc = crt_proc_memcpy(proc, &iod->iod_type, sizeof(iod->iod_type));
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &iod->iod_size);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &iod->iod_flags);
	if (rc != 0)
		return -DER_HG;

	if (proc_op == CRT_PROC_ENCODE && oiod != NULL &&
	    (oiod->oiod_flags & OBJ_SIOD_PROC_ONE) != 0) {
		proc_one = true;
		if (oiod->oiod_siods != NULL) {
			start = oiod->oiod_siods[0].siod_idx;
			nr = oiod->oiod_siods[0].siod_nr;
			D_ASSERT(start < iod->iod_nr &&
				 start + nr <= iod->iod_nr);
		} else {
			D_ASSERT(oiod->oiod_flags & OBJ_SIOD_SINGV);
			if (iod_csum != NULL && iod_csum->ic_data != NULL &&
			    iod_csum->ic_data[0].cs_nr > 1) {
				start = oiod->oiod_tgt_idx;
				singv = true;
			} else {
				start = 0;
			}
			nr = 1;
		}
		rc = crt_proc_uint32_t(proc, &nr);
	} else {
		start = 0;
		rc = crt_proc_uint32_t(proc, &iod->iod_nr);
		nr = iod->iod_nr;
	}
	if (rc != 0)
		return -DER_HG;

#if 0
	if (iod->iod_nr == 0 && iod->iod_type != DAOS_IOD_ARRAY) {
		D_ERROR("invalid I/O descriptor, iod_nr = 0\n");
		return -DER_HG;
	}
#else
	/* Zero nr is possible for EC (even for singv), as different IODs
	 * possibly with different targets, so for one target server received
	 * IO request, when it with multiple IODs then it is possible that one
	 * IOD is valid (iod_nr > 0) but another IOD is invalid (iod_nr == 0).
	 * The RW handler can just ignore those invalid IODs.
	 */
	if (nr == 0)
		return 0;
#endif

	if (proc_op == CRT_PROC_ENCODE || proc_op == CRT_PROC_FREE) {
		if (iod->iod_type == DAOS_IOD_ARRAY && iod->iod_recxs != NULL)
			existing_flags |= IOD_REC_EXIST;
	}

	rc = crt_proc_uint32_t(proc, &existing_flags);
	if (rc != 0)
		return -DER_HG;

	if (proc_op == CRT_PROC_DECODE) {
		if (existing_flags & IOD_REC_EXIST) {
			D_ALLOC_ARRAY(iod->iod_recxs, nr);
			if (iod->iod_recxs == NULL)
				D_GOTO(free, rc = -DER_NOMEM);
		}
	}

	if (existing_flags & IOD_REC_EXIST) {
		D_ASSERT(iod->iod_recxs != NULL || nr == 0);
		for (i = start; i < start + nr; i++) {
			rc = crt_proc_daos_recx_t(proc, &iod->iod_recxs[i]);
			if (rc != 0) {
				if (proc_op == CRT_PROC_DECODE)
					D_GOTO(free, rc);
				return rc;
			}
		}
	}

	if (iod_csum) {
		rc = crt_proc_struct_dcs_iod_csums_adv(proc, proc_op, iod_csum,
						       singv, start, nr);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				D_GOTO(free, rc);
			return rc;
		}
	}

	if (oiod != NULL && !proc_one) {
		rc = crt_proc_struct_obj_io_desc(proc, oiod);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				D_GOTO(free, rc);
			return rc;
		}
	}

	if (proc_op == CRT_PROC_FREE) {
free:
		if ((existing_flags & IOD_REC_EXIST) && iod->iod_recxs != NULL)
			D_FREE(iod->iod_recxs);
	}

	return rc;
}

static int crt_proc_daos_iom_t(crt_proc_t proc, daos_iom_t *map)
{
	crt_proc_op_t		 proc_op;
	uint32_t		 iom_nr = 0;
	int			 i, rc;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc)
		return rc;

	rc = crt_proc_memcpy(proc, &map->iom_type, sizeof(map->iom_type));
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &map->iom_size);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_daos_recx_t(proc, &map->iom_recx_lo);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_daos_recx_t(proc, &map->iom_recx_hi);
	if (rc != 0)
		return -DER_HG;

	if (ENCODING(proc_op)) {
		iom_nr = map->iom_nr;
		if ((map->iom_flags & DAOS_IOMF_DETAIL) == 0)
			iom_nr = 0;
	}

	rc = crt_proc_uint32_t(proc, &iom_nr);
	if (rc != 0)
		return -DER_HG;

	if (DECODING(proc_op) && iom_nr > 0) {
		map->iom_nr = iom_nr;
		map->iom_nr_out = iom_nr;
		D_ALLOC_ARRAY(map->iom_recxs, map->iom_nr);
		if (map->iom_recxs == NULL)
			return -DER_NOMEM;
		for (i = 0; i < map->iom_nr; i++) {
			rc = crt_proc_daos_recx_t(proc, &map->iom_recxs[i]);
			if (rc != 0) {
				D_FREE(map->iom_recxs);
				return -DER_HG;
			}
		}
	}

	if (ENCODING(proc_op) && iom_nr > 0) {
		for (i = 0; i < iom_nr; i++) {
			rc = crt_proc_daos_recx_t(proc, &map->iom_recxs[i]);
			if (rc != 0)
				return -DER_HG;
		}
	}

	if (FREEING(proc_op))
		D_FREE(map->iom_recxs);

	return 0;
}

static int
crt_proc_struct_daos_recx_ep_list(crt_proc_t proc,
				  struct daos_recx_ep_list *list)
{
	crt_proc_op_t	proc_op;
	int		i;
	int		rc;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &list->re_nr);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_bool(proc, &list->re_ep_valid);
	if (rc != 0)
		return -DER_HG;

	if (list->re_nr == 0)
		return 0;

	switch (proc_op) {
	case CRT_PROC_DECODE:
		D_ALLOC_ARRAY(list->re_items, list->re_nr);
		if (list->re_items == NULL)
			return -DER_NOMEM;
		list->re_total = list->re_nr;
	case CRT_PROC_ENCODE:
		for (i = 0; i < list->re_nr; i++) {
			rc = crt_proc_daos_recx_t(proc,
						  &list->re_items[i].re_recx);
			if (rc != 0)
				return -DER_HG;

			if (list->re_ep_valid) {
				rc = crt_proc_daos_epoch_t(proc,
						&list->re_items[i].re_ep);
				if (rc != 0)
					return -DER_HG;
			}
			rc = crt_proc_uint32_t(proc,
					&list->re_items[i].re_rec_size);
			if (rc != 0)
				return -DER_HG;

			rc = crt_proc_uint8_t(proc, &list->re_items[i].re_type);
			if (rc != 0)
				return -DER_HG;
		}
		break;
	default: /* CRT_PROC_FREE */
		daos_recx_ep_free(list);
		break;
	}

	return 0;
}

static int
crt_proc_struct_obj_iod_array(crt_proc_t proc, struct obj_iod_array *iod_array)
{
	struct obj_io_desc	*oiod;
	void			*buf;
	crt_proc_op_t		 proc_op;
	daos_size_t		 iod_size, off_size, buf_size, csum_size;
	uint8_t			 with_iod_csums = 0;
	uint32_t		 off_nr;
	bool			 proc_one = false;
	int			 i, rc;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc)
		return rc;

	if (proc_op == CRT_PROC_ENCODE && iod_array->oia_oiods != NULL &&
	    (iod_array->oia_oiods[0].oiod_flags & OBJ_SIOD_PROC_ONE) != 0) {
		proc_one = true;
		iod_array->oia_oiod_nr = 0;
	}

	if (crt_proc_uint32_t(proc, &iod_array->oia_iod_nr) != 0)
		return -DER_HG;
	if (crt_proc_uint32_t(proc, &iod_array->oia_oiod_nr) != 0)
		return -DER_HG;
	if (iod_array->oia_iod_nr == 0)
		return 0;

	D_ASSERT(iod_array->oia_oiod_nr == iod_array->oia_iod_nr ||
		 iod_array->oia_oiod_nr == 0);

	if (proc_op == CRT_PROC_ENCODE) {
		off_nr = iod_array->oia_offs != NULL ?
			 iod_array->oia_iod_nr : 0;
		if (crt_proc_uint32_t(proc, &off_nr) != 0)
			return -DER_HG;
		with_iod_csums = iod_array->oia_iod_csums != NULL ? 1 : 0;
		if (crt_proc_uint8_t(proc, &with_iod_csums) != 0)
			return -DER_HG;
		D_ASSERT(iod_array->oia_offs != NULL || off_nr == 0);
		for (i = 0; i < off_nr; i++) {
			if (crt_proc_uint64_t(proc, &iod_array->oia_offs[i]))
				return -DER_HG;
		}
	} else if (proc_op == CRT_PROC_DECODE) {
		if (crt_proc_uint32_t(proc, &off_nr) != 0)
			return -DER_HG;
		if (crt_proc_uint8_t(proc, &with_iod_csums) != 0)
			return -DER_HG;
		iod_size = roundup(sizeof(daos_iod_t) * iod_array->oia_iod_nr,
				   8);
		off_size = sizeof(uint64_t) * off_nr;
		if (with_iod_csums)
			csum_size = roundup(sizeof(struct dcs_iod_csums) *
					    iod_array->oia_iod_nr, 8);
		else
			csum_size = 0;
		buf_size = iod_size + off_size + csum_size;
		if (iod_array->oia_oiod_nr != 0)
			buf_size += sizeof(struct obj_io_desc) *
				    iod_array->oia_oiod_nr;
		D_ALLOC(buf, buf_size);
		if (buf == NULL)
			return -DER_NOMEM;
		iod_array->oia_iods = buf;
		if (off_nr != 0) {
			iod_array->oia_offs = buf + iod_size;
			for (i = 0; i < off_nr; i++) {
				if (crt_proc_uint64_t(proc,
					&iod_array->oia_offs[i]))
					return -DER_HG;
			}
		} else {
			iod_array->oia_offs = NULL;
		}

		if (with_iod_csums)
			iod_array->oia_iod_csums = buf + iod_size + off_size;
		else
			iod_array->oia_iod_csums = NULL;

		if (iod_array->oia_oiod_nr != 0)
			iod_array->oia_oiods = buf + iod_size + off_size +
					       csum_size;
		else
			iod_array->oia_oiods = NULL;
	}

	for (i = 0; i < iod_array->oia_iod_nr; i++) {
		struct dcs_iod_csums	*iod_csum;

		if (iod_array->oia_oiod_nr != 0 || proc_one) {
			D_ASSERT(iod_array->oia_oiods != NULL);
			oiod = &iod_array->oia_oiods[i];
		} else {
			oiod = NULL;
		}

		iod_csum = (iod_array->oia_iod_csums != NULL) ?
			   (&iod_array->oia_iod_csums[i]) :
			   NULL;
		rc = crt_proc_daos_iod_and_csum(proc, proc_op,
						&iod_array->oia_iods[i],
						iod_csum,
						oiod);
		if (rc)
			break;
	}

	if (proc_op == CRT_PROC_FREE) {
		D_FREE(iod_array->oia_iods);
		return 0;
	}

	return rc;
}

static int
crt_proc_daos_anchor_t(crt_proc_t proc, daos_anchor_t *anchor)
{
	if (crt_proc_uint16_t(proc, &anchor->da_type) != 0)
		return -DER_HG;

	if (crt_proc_uint16_t(proc, &anchor->da_shard) != 0)
		return -DER_HG;

	if (crt_proc_uint32_t(proc, &anchor->da_flags) != 0)
		return -DER_HG;

	if (crt_proc_uint64_t(proc, &anchor->da_sub_anchors) != 0)
		return -DER_HG;

	if (crt_proc_raw(proc, anchor->da_buf, sizeof(anchor->da_buf)) != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_d_sg_list_t(crt_proc_t proc, d_sg_list_t *sgl)
{
	crt_proc_op_t	proc_op;
	int		i;
	int		rc;

	rc = crt_proc_uint32_t(proc, &sgl->sg_nr);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &sgl->sg_nr_out);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;

	if (proc_op == CRT_PROC_DECODE && sgl->sg_nr > 0) {
		D_ALLOC_ARRAY(sgl->sg_iovs, sgl->sg_nr);
		if (sgl->sg_iovs == NULL)
			return -DER_NOMEM;
	}

	for (i = 0; i < sgl->sg_nr; i++) {
		rc = crt_proc_d_iov_t(proc, &sgl->sg_iovs[i]);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				D_FREE(sgl->sg_iovs);
			return -DER_HG;
		}
	}

	if (proc_op == CRT_PROC_FREE && sgl->sg_iovs != NULL)
		D_FREE(sgl->sg_iovs);

	return rc;
}

static int
crt_proc_struct_daos_shard_tgt(crt_proc_t proc, struct daos_shard_tgt *st)
{
	int rc;

	rc = crt_proc_uint32_t(proc, &st->st_rank);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_uint32_t(proc, &st->st_shard);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_uint32_t(proc, &st->st_tgt_id);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_uint16_t(proc, &st->st_tgt_idx);
	if (rc != 0)
		return -DER_HG;
	/* st_ec_tgt need not pack */

	return 0;
}

/* For compounded RPC. */
static int
crt_proc_struct_dtx_epoch(crt_proc_t proc, struct dtx_epoch *epoch)
{
	int	rc;

	rc = crt_proc_uint64_t(proc, &epoch->oe_value);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &epoch->oe_first);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &epoch->oe_flags);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &epoch->oe_rpc_flags);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_struct_dtx_daos_target(crt_proc_t proc, struct dtx_daos_target *ddt)
{
	int	rc;

	rc = crt_proc_uint32_t(proc, &ddt->ddt_id);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &ddt->ddt_flags);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_struct_dtx_redundancy_group(crt_proc_t proc,
				     struct dtx_redundancy_group *drg)
{
	int	rc;
	int	i;

	rc = crt_proc_uint32_t(proc, &drg->drg_tgt_cnt);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint16_t(proc, &drg->drg_redundancy);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint16_t(proc, &drg->drg_flags);
	if (rc != 0)
		return -DER_HG;

	for (i = 0; i < drg->drg_tgt_cnt; i++) {
		rc = crt_proc_uint32_t(proc, &drg->drg_ids[i]);
		if (rc != 0)
			return -DER_HG;
	}

	return sizeof(*drg) + sizeof(drg->drg_ids[0]) * drg->drg_tgt_cnt;
}

static int
crt_proc_struct_dtx_memberships(crt_proc_t proc, struct dtx_memberships *mbs)
{
	struct dtx_redundancy_group	*drg;
	int				 rc;
	int				 i;

	rc = crt_proc_uint32_t(proc, &mbs->dm_tgt_cnt);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &mbs->dm_grp_cnt);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &mbs->dm_data_size);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint16_t(proc, &mbs->dm_flags);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint16_t(proc, &mbs->dm_padding);
	if (rc != 0)
		return -DER_HG;

	for (i = 0; i < mbs->dm_tgt_cnt; i++) {
		rc = crt_proc_struct_dtx_daos_target(proc, &mbs->dm_tgts[i]);
		if (rc != 0)
			return rc;
	}

	/* We do not need the group information if all the targets are
	 * in the same redundancy group.
	 */
	if (mbs->dm_grp_cnt == 1)
		return 0;

	drg = (struct dtx_redundancy_group *)mbs->dm_data;
	rc = sizeof(mbs->dm_tgts[0]) * mbs->dm_tgt_cnt;
	for (i = 0; i < mbs->dm_grp_cnt; i++) {
		drg = (struct dtx_redundancy_group *)((char *)drg + rc);
		rc = crt_proc_struct_dtx_redundancy_group(proc, drg);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int
crt_proc_struct_daos_cpd_sub_head(crt_proc_t proc,
				  struct daos_cpd_sub_head *dcsh)
{
	crt_proc_op_t	proc_op;
	uint32_t	size = 0;
	int		rc;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;

	if (proc_op == CRT_PROC_FREE) {
		D_FREE(dcsh->dcsh_mbs);
		return 0;
	}

	rc = crt_proc_struct_dtx_id(proc, &dcsh->dcsh_xid);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_daos_unit_oid_t(proc, &dcsh->dcsh_leader_oid);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_struct_dtx_epoch(proc, &dcsh->dcsh_epoch);
	if (rc != 0)
		return -DER_HG;

	if (proc_op == CRT_PROC_DECODE) {
		struct dtx_memberships	*mbs;

		rc = crt_proc_uint32_t(proc, &size);
		if (rc != 0)
			return -DER_HG;

		D_ALLOC(mbs, size);
		if (mbs == NULL)
			return -DER_NOMEM;

		rc = crt_proc_struct_dtx_memberships(proc, mbs);
		if (rc != 0) {
			D_FREE(mbs);
			return -DER_HG;
		}

		dcsh->dcsh_mbs = mbs;
		return 0;
	}

	D_ASSERT(proc_op == CRT_PROC_ENCODE);

	/* Pack the size of dcsh->dcsh_mbs to help decode case. */
	size = sizeof(*dcsh->dcsh_mbs) + dcsh->dcsh_mbs->dm_data_size;
	rc = crt_proc_uint32_t(proc, &size);
	if (rc != 0)
		return -DER_HG;

	return crt_proc_struct_dtx_memberships(proc, dcsh->dcsh_mbs);
}

static int
crt_proc_daos_iod_t(crt_proc_t proc, daos_iod_t *iod)
{
	crt_proc_op_t	proc_op;
	int		rc;
	int		i;

	rc = crt_proc_daos_key_t(proc, &iod->iod_name);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;

	if (proc_op == CRT_PROC_FREE) {
		D_FREE(iod->iod_recxs);
		return 0;
	}

	rc = crt_proc_memcpy(proc, &iod->iod_type, sizeof(iod->iod_type));
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &iod->iod_size);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &iod->iod_flags);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &iod->iod_nr);
	if (rc != 0)
		return -DER_HG;

	if (iod->iod_nr == 0)
		return 0;

	if (proc_op == CRT_PROC_DECODE) {
		D_ALLOC_ARRAY(iod->iod_recxs, iod->iod_nr);
		if (iod->iod_recxs == NULL)
			return -DER_NOMEM;
	}

	for (i = 0; i < iod->iod_nr; i++) {
		rc = crt_proc_daos_recx_t(proc, &iod->iod_recxs[i]);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				D_FREE(iod->iod_recxs);

			return -DER_HG;
		}
	}

	return 0;
}

static int
crt_proc_struct_daos_cpd_sub_req(crt_proc_t proc,
				 struct daos_cpd_sub_req *dcsr, bool with_oid)
{
	crt_proc_op_t	proc_op;
	int		rc;
	int		i;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint16_t(proc, &dcsr->dcsr_opc);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint16_t(proc, &dcsr->dcsr_ec_tgt_nr);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &dcsr->dcsr_nr);
	if (rc != 0)
		return -DER_HG;

	if (with_oid) {
		rc = crt_proc_daos_unit_oid_t(proc, &dcsr->dcsr_oid);
	} else if (proc_op == CRT_PROC_ENCODE) {
		daos_unit_oid_t		 oid;

		daos_dc_obj2id(dcsr->dcsr_obj, &oid.id_pub);
		/* It is not important what the id_shard is, that
		 * is packed via daos_cpd_req_idx::dcri_shard_idx.
		 */
		rc = crt_proc_daos_unit_oid_t(proc, &oid);
	}
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_daos_key_t(proc, &dcsr->dcsr_dkey);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &dcsr->dcsr_dkey_hash);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &dcsr->dcsr_api_flags);
	if (rc != 0)
		return -DER_HG;

	switch (dcsr->dcsr_opc) {
	case DCSO_UPDATE: {
		struct daos_cpd_update	*dcu = &dcsr->dcsr_update;

		if (proc_op == CRT_PROC_DECODE) {
			if (dcsr->dcsr_ec_tgt_nr != 0) {
				D_ALLOC_ARRAY(dcu->dcu_ec_tgts,
					      dcsr->dcsr_ec_tgt_nr);
				if (dcu->dcu_ec_tgts == NULL)
					D_GOTO(out, rc = -DER_NOMEM);
			}

			dcu->dcu_ec_split_req = NULL;
		}

		rc = crt_proc_struct_dcs_csum_info(proc, &dcu->dcu_dkey_csum);
		if (rc != 0)
			D_GOTO(out, rc = -DER_HG);

		for (i = 0; i < dcsr->dcsr_ec_tgt_nr; i++) {
			rc = crt_proc_uint32_t(proc,
					&dcu->dcu_ec_tgts[i].dcet_shard_idx);
			if (rc != 0)
				D_GOTO(out, rc = -DER_HG);

			rc = crt_proc_uint32_t(proc,
					&dcu->dcu_ec_tgts[i].dcet_tgt_id);
			if (rc != 0)
				D_GOTO(out, rc = -DER_HG);
		}

		rc = crt_proc_struct_obj_iod_array(proc, &dcu->dcu_iod_array);
		if (rc != 0)
			D_GOTO(out, rc = -DER_HG);

		rc = crt_proc_uint32_t(proc, &dcu->dcu_start_shard);
		if (rc != 0)
			D_GOTO(out, rc = -DER_HG);

		rc = crt_proc_uint32_t(proc, &dcu->dcu_flags);
		if (rc != 0)
			D_GOTO(out, rc = -DER_HG);

		if (dcsr->dcsr_nr == 0)
			D_GOTO(out, rc = 0);

		if (dcu->dcu_flags & DRF_CPD_BULK) {
			if (proc_op == CRT_PROC_DECODE) {
				D_ALLOC_ARRAY(dcu->dcu_bulks, dcsr->dcsr_nr);
				if (dcu->dcu_bulks == NULL)
					D_GOTO(out, rc = -DER_NOMEM);
			}

			for (i = 0; i < dcsr->dcsr_nr; i++) {
				rc = crt_proc_crt_bulk_t(proc,
							 &dcu->dcu_bulks[i]);
				if (rc != 0)
					D_GOTO(out, rc = -DER_HG);
			}
		} else {
			if (proc_op == CRT_PROC_DECODE) {
				D_ALLOC_ARRAY(dcu->dcu_sgls, dcsr->dcsr_nr);
				if (dcu->dcu_sgls == NULL)
					D_GOTO(out, rc = -DER_NOMEM);
			}

			for (i = 0; i < dcsr->dcsr_nr; i++) {
				rc = crt_proc_d_sg_list_t(proc,
							  &dcu->dcu_sgls[i]);
				if (rc != 0)
					D_GOTO(out, rc = -DER_HG);
			}
		}

		break;
	}
	case DCSO_PUNCH_OBJ:
	case DCSO_PUNCH_DKEY:
	case DCSO_PUNCH_AKEY: {
		struct daos_cpd_punch	*dcp = &dcsr->dcsr_punch;

		if (dcsr->dcsr_nr == 0)
			D_GOTO(out, rc = 0);

		if (proc_op == CRT_PROC_DECODE) {
			D_ALLOC_ARRAY(dcp->dcp_akeys, dcsr->dcsr_nr);
			if (dcp->dcp_akeys == NULL)
				return -DER_NOMEM;
		}

		for (i = 0; i < dcsr->dcsr_nr; i++) {
			rc = crt_proc_daos_key_t(proc, &dcp->dcp_akeys[i]);
			if (rc != 0)
				D_GOTO(out, rc = -DER_HG);
		}

		break;
	}
	case DCSO_READ: {
		struct daos_cpd_read	*dcr = &dcsr->dcsr_read;

		if (dcsr->dcsr_nr == 0)
			D_GOTO(out, rc = 0);

		if (proc_op == CRT_PROC_DECODE) {
			D_ALLOC_ARRAY(dcr->dcr_iods, dcsr->dcsr_nr);
			if (dcr->dcr_iods == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}

		for (i = 0; i < dcsr->dcsr_nr; i++) {
			rc = crt_proc_daos_iod_t(proc, &dcr->dcr_iods[i]);
			if (rc != 0)
				D_GOTO(out, rc = -DER_HG);
		}

		break;
	}
	default:
		return -DER_INVAL;
	}

out:
	if ((proc_op == CRT_PROC_ENCODE) ||
	    (proc_op == CRT_PROC_DECODE && rc == 0))
		return rc;

	switch (dcsr->dcsr_opc) {
	case DCSO_UPDATE:
		D_FREE(dcsr->dcsr_update.dcu_ec_tgts);
		D_FREE(dcsr->dcsr_update.dcu_sgls);
		break;
	case DCSO_PUNCH_OBJ:
	case DCSO_PUNCH_DKEY:
	case DCSO_PUNCH_AKEY:
		D_FREE(dcsr->dcsr_punch.dcp_akeys);
		break;
	case DCSO_READ:
		D_FREE(dcsr->dcsr_read.dcr_iods);
		break;
	}

	return rc;
}

static int
crt_proc_struct_daos_cpd_req_idx(crt_proc_t proc,
				 struct daos_cpd_req_idx *dcri)
{
	int	rc;

	rc = crt_proc_uint32_t(proc, &dcri->dcri_shard_idx);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &dcri->dcri_req_idx);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_struct_daos_cpd_disp_ent(crt_proc_t proc,
				  struct daos_cpd_disp_ent *dcde)
{
	crt_proc_op_t	proc_op;
	uint32_t	count;
	int		rc;
	int		i;

	rc = crt_proc_uint32_t(proc, &dcde->dcde_read_cnt);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &dcde->dcde_write_cnt);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;

	if (proc_op == CRT_PROC_FREE) {
		D_FREE(dcde->dcde_reqs);
		return 0;
	}

	count = dcde->dcde_read_cnt + dcde->dcde_write_cnt;
	if (proc_op == CRT_PROC_DECODE) {
		D_ALLOC_ARRAY(dcde->dcde_reqs, count);
		if (dcde->dcde_reqs == NULL)
			return -DER_NOMEM;
	}

	for (i = 0; i < count; i++) {
		rc = crt_proc_struct_daos_cpd_req_idx(proc,
						      &dcde->dcde_reqs[i]);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				D_FREE(dcde->dcde_reqs);

			return -DER_HG;
		}
	}

	return 0;
}

static int
crt_proc_struct_daos_cpd_sg(crt_proc_t proc, struct daos_cpd_sg *dcs)
{
	crt_proc_op_t	proc_op;
	int		rc;
	int		i;

	rc = crt_proc_uint32_t(proc, &dcs->dcs_type);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &dcs->dcs_nr);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;

	switch (dcs->dcs_type) {
	case DCST_HEAD: {
		struct daos_cpd_sub_head	*dcsh;

		if (proc_op == CRT_PROC_DECODE) {
			D_ALLOC_ARRAY(dcsh, dcs->dcs_nr);
			if (dcsh == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			dcs->dcs_buf = dcsh;
		} else {
			dcsh = dcs->dcs_buf;
		}

		for (i = 0; i < dcs->dcs_nr; i++) {
			rc = crt_proc_struct_daos_cpd_sub_head(proc, &dcsh[i]);
			if (rc != 0)
				D_GOTO(out, rc = -DER_HG);
		}

		break;
	}
	case DCST_REQ_CLI:
	case DCST_REQ_SRV: {
		struct daos_cpd_sub_req		*dcsr;
		bool				 with_oid;

		if (proc_op == CRT_PROC_DECODE) {
			D_ALLOC_ARRAY(dcsr, dcs->dcs_nr);
			if (dcsr == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			dcs->dcs_buf = dcsr;
			with_oid = true;
		} else {
			dcsr = dcs->dcs_buf;
			if (dcs->dcs_type == DCST_REQ_SRV)
				with_oid = true;
			else
				with_oid = false;
		}

		for (i = 0; i < dcs->dcs_nr; i++) {
			rc = crt_proc_struct_daos_cpd_sub_req(proc, &dcsr[i],
							      with_oid);
			if (rc != 0)
				D_GOTO(out, rc = -DER_HG);
		}

		break;
	}
	case DCST_DISP: {
		struct daos_cpd_disp_ent	*dcde;

		if (proc_op == CRT_PROC_DECODE) {
			D_ALLOC_ARRAY(dcde, dcs->dcs_nr);
			if (dcde == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			dcs->dcs_buf = dcde;
		} else {
			dcde = dcs->dcs_buf;
		}

		for (i = 0; i < dcs->dcs_nr; i++) {
			rc = crt_proc_struct_daos_cpd_disp_ent(proc, &dcde[i]);
			if (rc != 0)
				D_GOTO(out, rc = -DER_HG);
		}

		break;
	}
	case DCST_TGT: {
		struct daos_shard_tgt		*dst;

		if (proc_op == CRT_PROC_DECODE) {
			D_ALLOC_ARRAY(dst, dcs->dcs_nr);
			if (dst == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			dcs->dcs_buf = dst;
		} else {
			dst = dcs->dcs_buf;
		}

		for (i = 0; i < dcs->dcs_nr; i++) {
			rc = crt_proc_struct_daos_shard_tgt(proc, &dst[i]);
			if (rc != 0)
				D_GOTO(out, rc = -DER_HG);
		}

		break;
	}
	default:
		return -DER_INVAL;
	}

out:
	/* XXX: There is potential memory leak for the case of CRT_PROC_DECODE
	 *	with failure. We may allocate some DRAM in some low layer proc
	 *	functions when decoding former elements.
	 *
	 *	Currently, We seems not have efficient way to release them. It
	 *	is not special for CPD related proc interfaces, instead, it is
	 *	general issue for the whole CRT proc mechanism.
	 */
	if ((proc_op == CRT_PROC_FREE) ||
	    (proc_op == CRT_PROC_DECODE && rc != 0))
		D_FREE(dcs->dcs_buf);

	return rc;
}

CRT_RPC_DEFINE(obj_rw, DAOS_ISEQ_OBJ_RW, DAOS_OSEQ_OBJ_RW)
CRT_RPC_DEFINE(obj_key_enum, DAOS_ISEQ_OBJ_KEY_ENUM, DAOS_OSEQ_OBJ_KEY_ENUM)
CRT_RPC_DEFINE(obj_punch, DAOS_ISEQ_OBJ_PUNCH, DAOS_OSEQ_OBJ_PUNCH)
CRT_RPC_DEFINE(obj_query_key, DAOS_ISEQ_OBJ_QUERY_KEY, DAOS_OSEQ_OBJ_QUERY_KEY)
CRT_RPC_DEFINE(obj_sync, DAOS_ISEQ_OBJ_SYNC, DAOS_OSEQ_OBJ_SYNC)
CRT_RPC_DEFINE(obj_migrate, DAOS_ISEQ_OBJ_MIGRATE, DAOS_OSEQ_OBJ_MIGRATE)
CRT_RPC_DEFINE(obj_cpd, DAOS_ISEQ_OBJ_CPD, DAOS_OSEQ_OBJ_CPD)


/* Define for cont_rpcs[] array population below.
 * See OBJ_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
}

static struct crt_proto_rpc_format obj_proto_rpc_fmt[] = {
	OBJ_PROTO_CLI_RPC_LIST,
};

#undef X

struct crt_proto_format obj_proto_fmt = {
	.cpf_name  = "daos-obj-proto",
	.cpf_ver   = DAOS_OBJ_VERSION,
	.cpf_count = ARRAY_SIZE(obj_proto_rpc_fmt),
	.cpf_prf   = obj_proto_rpc_fmt,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_OBJ_MODULE, 0)
};

void
obj_reply_set_status(crt_rpc_t *rpc, int status)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_FETCH:
	case DAOS_OBJ_RPC_TGT_UPDATE:
		((struct obj_rw_out *)reply)->orw_ret = status;
		break;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
		((struct obj_key_enum_out *)reply)->oeo_ret = status;
		break;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_AKEYS:
		((struct obj_punch_out *)reply)->opo_ret = status;
		break;
	case DAOS_OBJ_RPC_QUERY_KEY:
		((struct obj_query_key_out *)reply)->okqo_ret = status;
		break;
	case DAOS_OBJ_RPC_SYNC:
		((struct obj_sync_out *)reply)->oso_ret = status;
		break;
	case DAOS_OBJ_RPC_CPD:
		((struct obj_cpd_out *)reply)->oco_ret = status;
		break;
	default:
		D_ASSERT(0);
	}
}

int
obj_reply_get_status(crt_rpc_t *rpc)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_TGT_UPDATE:
	case DAOS_OBJ_RPC_FETCH:
		return ((struct obj_rw_out *)reply)->orw_ret;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
		return ((struct obj_key_enum_out *)reply)->oeo_ret;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_AKEYS:
		return ((struct obj_punch_out *)reply)->opo_ret;
	case DAOS_OBJ_RPC_QUERY_KEY:
		return ((struct obj_query_key_out *)reply)->okqo_ret;
	case DAOS_OBJ_RPC_SYNC:
		return ((struct obj_sync_out *)reply)->oso_ret;
	case DAOS_OBJ_RPC_CPD:
		return ((struct obj_cpd_out *)reply)->oco_ret;
	default:
		D_ASSERT(0);
	}
	return 0;
}

void
obj_reply_map_version_set(crt_rpc_t *rpc, uint32_t map_version)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_TGT_UPDATE:
	case DAOS_OBJ_RPC_FETCH:
		((struct obj_rw_out *)reply)->orw_map_version = map_version;
		break;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
		((struct obj_key_enum_out *)reply)->oeo_map_version =
								map_version;
		break;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_AKEYS:
		((struct obj_punch_out *)reply)->opo_map_version = map_version;
		break;
	case DAOS_OBJ_RPC_QUERY_KEY:
		((struct obj_query_key_out *)reply)->okqo_map_version =
			map_version;
		break;
	case DAOS_OBJ_RPC_SYNC:
		((struct obj_sync_out *)reply)->oso_map_version = map_version;
		break;
	case DAOS_OBJ_RPC_CPD:
		((struct obj_cpd_out *)reply)->oco_map_version = map_version;
		break;
	default:
		D_ASSERT(0);
	}
}

uint32_t
obj_reply_map_version_get(crt_rpc_t *rpc)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_TGT_UPDATE:
	case DAOS_OBJ_RPC_FETCH:
		return ((struct obj_rw_out *)reply)->orw_map_version;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
		return ((struct obj_key_enum_out *)reply)->oeo_map_version;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_AKEYS:
		return ((struct obj_punch_out *)reply)->opo_map_version;
	case DAOS_OBJ_RPC_QUERY_KEY:
		return ((struct obj_query_key_out *)reply)->okqo_map_version;
	case DAOS_OBJ_RPC_SYNC:
		return ((struct obj_sync_out *)reply)->oso_map_version;
	case DAOS_OBJ_RPC_CPD:
		return ((struct obj_cpd_out *)reply)->oco_map_version;
	default:
		D_ASSERT(0);
	}
	return 0;
}
