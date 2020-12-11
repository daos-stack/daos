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
 "
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * DAOS server erasure-coded object IO handling.
 *
 * src/object/srv_ec.c
 */
#define D_LOGFAC	DD_FAC(object)

#include <stddef.h>
#include <stdio.h>
#include <daos/rpc.h>
#include <daos_types.h>
#include "obj_rpc.h"
#include "obj_internal.h"

static inline bool
obj_ec_is_valid_tgt(struct daos_cpd_ec_tgts *tgt_map, uint32_t map_size,
		    uint32_t id, uint32_t *shard)
{
	int	i;

	/* XXX: The distributed transaction may contains the updates of
	 *	multiple EC objects that share the same forward targets
	 *	array. So for some update of EC object, the targets may
	 *	be unordered. So checking the bitmap may be inefficient.
	 */
	for (i = 0; i < map_size; i++) {
		if (tgt_map[i].dcet_tgt_id == id) {
			*shard = tgt_map[i].dcet_shard_idx;
			return true;
		}
	}

	return false;
}

/**
 * Split EC obj read/write request.
 * For object update, client sends update request to leader, the leader needs to
 * split it for different targets before dispatch.
 */
int
obj_ec_rw_req_split(daos_unit_oid_t oid, struct obj_iod_array *iod_array,
		    uint32_t iod_nr, uint32_t start_shard, uint32_t max_shard,
		    void *tgt_map, uint32_t map_size,
		    uint32_t tgt_nr, struct daos_shard_tgt *tgts,
		    struct obj_ec_split_req **split_req)
{
	daos_iod_t		*iod;
	daos_iod_t		*iods = iod_array->oia_iods;
	struct obj_io_desc	*oiods = iod_array->oia_oiods;
	struct obj_ec_split_req	*req;
	daos_iod_t		*split_iod, *split_iods;
	struct obj_shard_iod	*siod;
	struct obj_tgt_oiod	*tgt_oiod, *tgt_oiods = NULL;
	struct dcs_iod_csums	*iod_csum = NULL;
	struct dcs_iod_csums	*iod_csums = iod_array->oia_iod_csums;
	struct dcs_iod_csums	*split_iod_csum = NULL;
	struct dcs_iod_csums	*split_iod_csums;
	uint32_t		 i, tgt_max_idx, self_tgt_idx;
	daos_size_t		 req_size, iods_size;
	daos_size_t		 csums_size = 0, singv_ci_size = 0;
	uint8_t			 tgt_bit_map[OBJ_TGT_BITMAP_LEN] = {0};
	bool			 with_csums = (iod_csums != NULL);
	void			*buf = NULL;
	uint32_t		 tgt_idx;
	int			 count = 0;
	int			 rc = 0;

	/* minimal K/P is 2/1, so at least 1 forward targets */
	D_ASSERT(tgt_nr >= 1);
	D_ASSERT(oiods != NULL);
	/* as we select the last parity node as leader, and for any update
	 * there must be a siod (the last siod) for leader except for singv.
	 */
	D_ASSERT((oiods[0].oiod_flags & OBJ_SIOD_SINGV) ||
		 oiods[0].oiod_nr >= 2);

	self_tgt_idx = oid.id_shard - start_shard;
	if (tgt_map != NULL)
		tgt_max_idx = 0;
	else
		tgt_max_idx = max_shard;

	req_size = roundup(sizeof(struct obj_ec_split_req), 8);
	iods_size = roundup(sizeof(daos_iod_t) * iod_nr, 8);
	if (with_csums) {
		csums_size = roundup(sizeof(struct dcs_iod_csums) * iod_nr, 8);
		singv_ci_size = roundup(sizeof(struct dcs_csum_info) * iod_nr,
					8);
	}
	D_ALLOC(buf, req_size + iods_size + csums_size + singv_ci_size);
	if (buf == NULL)
		return -DER_NOMEM;

	req = buf;
	req->osr_iods = buf + req_size;
	if (with_csums) {
		req->osr_iod_csums = buf + req_size + iods_size;
		req->osr_singv_cis = buf + req_size + iods_size + csums_size;
	}
	req->osr_start_shard = start_shard;

	for (i = 0; i < tgt_nr; i++) {
		if (tgt_map != NULL) {
			if (!obj_ec_is_valid_tgt(tgt_map, map_size,
						 tgts[i].st_tgt_id, &tgt_idx))
				continue;

			D_ASSERT(tgt_idx >= start_shard);

			tgt_idx -= start_shard;
			if (tgt_max_idx < tgt_idx)
				tgt_max_idx = tgt_idx;
		} else {
			tgt_idx = tgts[i].st_shard - start_shard;
			D_ASSERT(tgt_idx <= tgt_max_idx);
		}

		setbit(tgt_bit_map, tgt_idx);
		count++;
	}

	if (tgt_map != NULL) {
		D_ASSERT(count == map_size);
	} else {
		setbit(tgt_bit_map, self_tgt_idx);
		count++;
	}

	tgt_oiods = obj_ec_tgt_oiod_init(oiods, iod_nr, tgt_bit_map,
					 tgt_max_idx, count);
	if (tgt_oiods == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	req->osr_tgt_oiods = tgt_oiods;
	tgt_oiod = obj_ec_tgt_oiod_get(tgt_oiods, count, tgt_max_idx);
	D_ASSERT(tgt_oiod != NULL && tgt_oiod->oto_tgt_idx == tgt_max_idx);

	req->osr_offs = tgt_oiod->oto_offs;
	split_iods = req->osr_iods;
	split_iod_csums = req->osr_iod_csums;

	for (i = 0; i < iod_nr; i++) {
		int	idx;

		iod = &iods[i];
		if (with_csums) {
			D_ASSERT(split_iod_csums != NULL);

			split_iod_csum = &split_iod_csums[i];
			iod_csum = &iod_csums[i];
			*split_iod_csum = *iod_csum;
		}

		split_iod = &split_iods[i];
		split_iod->iod_name = iod->iod_name;
		split_iod->iod_type = iod->iod_type;
		split_iod->iod_size = iod->iod_size;

		if (tgt_oiod->oto_oiods[i].oiod_flags & OBJ_SIOD_SINGV) {
			D_ASSERT(iod->iod_type == DAOS_IOD_SINGLE);

			idx = 0;
			split_iod->iod_nr = 1;
			if (with_csums) {
				struct dcs_csum_info	*ci, *split_ci;

				D_ASSERT(split_iod_csum->ic_nr == 1);
				ci = &split_iod_csum->ic_data[0];
				if (ci->cs_nr > 1) {
					/* evenly distributed singv */
					D_ASSERT(ci->cs_nr == tgt_max_idx + 1);
					split_ci = &req->osr_singv_cis[i];
					*split_ci = *ci;
					split_iod_csum->ic_data = split_ci;
					split_ci->cs_nr = 1;
					split_ci->cs_csum +=
						tgt_max_idx * ci->cs_len;
					split_ci->cs_buf_len = ci->cs_len;
				}
			}
		} else {
			siod = &tgt_oiod->oto_oiods[i].oiod_siods[0];
			split_iod->iod_nr = siod->siod_nr;
			idx = siod->siod_idx;
			if (with_csums) {
				split_iod_csum->ic_data =
					&iod_csum->ic_data[idx];
				split_iod_csum->ic_nr = siod->siod_nr;
			}
		}
		if (iod->iod_recxs != NULL)
			split_iod->iod_recxs = &iod->iod_recxs[idx];
	}

	*split_req = req;

out:
	if (rc) {
		if (buf != NULL)
			D_FREE(buf);
		obj_ec_tgt_oiod_fini(tgt_oiods);
	}
	return rc;
}

void
obj_ec_split_req_fini(struct obj_ec_split_req *req)
{
	if (req == NULL)
		return;
	obj_ec_tgt_oiod_fini(req->osr_tgt_oiods);
	D_FREE(req);
}
