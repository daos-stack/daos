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

/**
 * Split EC obj read/write request.
 * For object update, client sends update request to leader, the leader needs to
 * split it for different targets before dispatch.
 */
int
obj_ec_rw_req_split(struct obj_rw_in *orw, struct obj_ec_split_req **split_req)
{
	daos_iod_t		*iod, *iods = orw->orw_iod_array.oia_iods;
	struct obj_io_desc	*oiods = orw->orw_iod_array.oia_oiods;
	struct daos_shard_tgt	*fw_tgts = orw->orw_shard_tgts.ca_arrays;
	struct obj_ec_split_req	*req;
	daos_iod_t		*split_iod, *split_iods;
	struct obj_shard_iod	*siod;
	struct obj_tgt_oiod	*tgt_oiod, *tgt_oiods = NULL;
	uint32_t		 tgt_nr = orw->orw_shard_tgts.ca_count;
	uint32_t		 iod_nr = orw->orw_nr;
	uint32_t		 start_shard = orw->orw_start_shard;
	uint32_t		 i, tgt_idx, tgt_max_idx;
	daos_size_t		 req_size, iods_size;
	uint8_t			 tgt_bit_map[OBJ_TGT_BITMAP_LEN] = {0};
	void			*buf = NULL;
	int			 rc = 0;

	/* minimal K/P is 2/1, so at least 2 forward targets */
	D_ASSERT(tgt_nr >= 2);
	D_ASSERT(oiods != NULL);
	/* as we select the last parity node as leader, and for any update
	 * there must be a siod (the last siod) for leader except for singv.
	 */
	D_ASSERT((oiods[0].oiod_flags & OBJ_SIOD_SINGV) ||
		 oiods[0].oiod_nr >= 2);
	tgt_max_idx = orw->orw_oid.id_shard - start_shard;

	req_size = roundup(sizeof(struct obj_ec_split_req), 8);
	iods_size = roundup(sizeof(daos_iod_t) * iod_nr, 8);
	D_ALLOC(buf, req_size + iods_size);
	if (buf == NULL)
		return -DER_NOMEM;
	req = buf;
	req->osr_iods = buf + req_size;
	req->osr_start_shard = start_shard;

	for (i = 0; i < tgt_nr; i++) {
		tgt_idx = fw_tgts[i].st_shard - start_shard;
		D_ASSERT(tgt_idx < tgt_max_idx);
		setbit(tgt_bit_map, tgt_idx);
	}
	setbit(tgt_bit_map, tgt_max_idx);

	tgt_oiods = obj_ec_tgt_oiod_init(oiods, iod_nr, tgt_bit_map,
					 tgt_max_idx, tgt_nr + 1);
	if (tgt_oiods == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	req->osr_tgt_oiods = tgt_oiods;
	tgt_oiod = obj_ec_tgt_oiod_get(tgt_oiods, tgt_nr + 1, tgt_max_idx);
	D_ASSERT(tgt_oiod != NULL && tgt_oiod->oto_tgt_idx == tgt_max_idx);
	req->osr_offs = tgt_oiod->oto_offs;

	split_iods = req->osr_iods;
	for (i = 0; i < iod_nr; i++) {
		int	idx;

		iod = &iods[i];
		split_iod = &split_iods[i];
		split_iod->iod_name = iod->iod_name;
		split_iod->iod_type = iod->iod_type;
		split_iod->iod_size = iod->iod_size;
		if (tgt_oiod->oto_oiods[i].oiod_flags & OBJ_SIOD_SINGV) {
			D_ASSERT(iod->iod_type == DAOS_IOD_SINGLE);
			idx = 0;
			split_iod->iod_nr = 1;
		} else {
			siod = &tgt_oiod->oto_oiods[i].oiod_siods[0];
			split_iod->iod_nr = siod->siod_nr;
			idx = siod->siod_idx;
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
