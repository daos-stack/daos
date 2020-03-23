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
	struct dcs_iod_csums	*iod_csum = NULL;
	struct dcs_iod_csums	*iod_csums = orw->orw_iod_array.oia_iod_csums;
	struct dcs_iod_csums	*split_iod_csum = NULL;
	struct dcs_iod_csums	*split_iod_csums;
	uint32_t		 tgt_nr = orw->orw_shard_tgts.ca_count;
	uint32_t		 iod_nr = orw->orw_nr;
	uint32_t		 start_shard = orw->orw_start_shard;
	uint32_t		 i, tgt_idx, tgt_max_idx;
	daos_size_t		 req_size, iods_size;
	daos_size_t		 csums_size = 0, singv_ci_size = 0;
	uint8_t			 tgt_bit_map[OBJ_TGT_BITMAP_LEN] = {0};
	bool			 with_csums = (iod_csums != NULL);
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

/* Determines if entire update affects just this target. If so, no IOD
 * modifications or special bulk-transfer handling are needed for this
 * update request.
 */
static bool
ec_is_one_cell(daos_iod_t *iod, struct daos_oclass_attr *oca,
	       unsigned int tgt_idx)
{
	unsigned int    len = oca->u.ec.e_len;
	unsigned int    k = oca->u.ec.e_k;
	bool		rc;
	unsigned int	j;

	for (j = 0; j < iod->iod_nr; j++) {
		daos_recx_t     *this_recx = &iod->iod_recxs[j];
		uint64_t         recx_start_offset = this_recx->rx_idx *
						     iod->iod_size;
		uint64_t         recx_end_offset =
					(this_recx->rx_nr * iod->iod_size) +
					recx_start_offset;

		if (recx_start_offset & PARITY_INDICATOR) {
			rc = false;
			break;
		} else if (recx_start_offset/len == recx_end_offset/len &&
			(recx_start_offset % (len * k)) / len == tgt_idx) {
			rc = true;
		} else {
			rc = false;
			break;
		}
	}
	return rc;
}

/* Removes the specified recx entry from an IOD's iod_recx array.
*/
static void
ec_del_recx(daos_iod_t *iod, unsigned int idx)
{
	int j;

	D_ASSERT(iod->iod_nr >= 1 && idx < iod->iod_nr);

	for (j = idx; j < iod->iod_nr - 1; j++)
		iod->iod_recxs[j] = iod->iod_recxs[j + 1];
	iod->iod_nr--;
}

/* Process IOD array on data target. Keeps extents that are addressed
 * to this target.
 */
int
ec_data_target(unsigned int dtgt_idx, unsigned int nr, daos_iod_t *iods,
	       struct daos_oclass_attr *oca, struct ec_bulk_spec **skip_list)
{
	unsigned long	len = oca->u.ec.e_len;
	unsigned long	ss = len * oca->u.ec.e_k;
	unsigned int	i, j, idx;
	int		rc = 0;

	for (i = 0; i < nr; i++) {
		daos_iod_t	*iod = &iods[i];
		unsigned int	 loop_bound = iod->iod_nr;
		int		 sl_idx = 0;

		if (iod->iod_type == DAOS_IOD_SINGLE ||
			ec_is_one_cell(iod, oca, dtgt_idx))
			continue;
		/*  A recx will involve at most 3 skip list entries on a data
		 *  target. The plus one is to ensure there's still a zero at
		 *  the end of the array.
		 */
		D_ALLOC_ARRAY(skip_list[i], 3 * iod->iod_nr + 1);
		if (skip_list[i] == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		for (idx = 0, j = 0; j < loop_bound; j++) {
			daos_recx_t	*this_recx = &iod->iod_recxs[idx];
			uint64_t	recx_start =
					this_recx->rx_idx * iod->iod_size;
			uint64_t	so = recx_start % ss;
			unsigned int	cell = so / len;

			uint64_t	recx_size = iod->iod_size *
						 this_recx->rx_nr;


			if (iod->iod_recxs[idx].rx_idx & PARITY_INDICATOR) {
				ec_bulk_spec_set(oca->u.ec.e_len, true,
						 sl_idx++, &skip_list[i]);
				ec_del_recx(iod, idx);
				continue;
			}
			/* recx starts in this cell */
			if (cell == dtgt_idx) {
				/* recx either extends beyond cell or
				 * ends within cell
				 */
				uint64_t c_offset = recx_start % len;
				uint64_t new_len = recx_size + c_offset >= len ?
							len - c_offset :
							recx_size;

				this_recx->rx_nr = new_len / iod->iod_size;
				ec_bulk_spec_set(new_len, false,
						 sl_idx++, &skip_list[i]);
				if (recx_size > new_len) {
					ec_bulk_spec_set(recx_size - new_len,
							 true, sl_idx++,
							 &skip_list[i]);
				}
			} else if ((uint64_t)(dtgt_idx + 1) * oca->u.ec.e_len
					<= so) {
				/* this recx doesn't map to this target
				 * so we need to remove the recx
				 */
				ec_del_recx(iod, idx);
				ec_bulk_spec_set(recx_size, true, sl_idx++,
						 &skip_list[i]);
				continue;
			} else {
				unsigned int cell_start = dtgt_idx * len - so;

				if (cell_start >= recx_size) {
					/* this recx doesn't map to this target
					 * so we need to remove the recx
					 */
					ec_del_recx(iod, idx);
					ec_bulk_spec_set(recx_size, true,
							 sl_idx++,
							 &skip_list[i]);
					continue;
				}
				ec_bulk_spec_set(cell_start, true, sl_idx++,
						 &skip_list[i]);
				this_recx->rx_idx += cell_start / iod->iod_size;
				if (cell_start + oca->u.ec.e_len < recx_size) {
					this_recx->rx_nr =
						oca->u.ec.e_len / iod->iod_size;
					ec_bulk_spec_set(len, false, sl_idx++,
							 &skip_list[i]);
					ec_bulk_spec_set(recx_size -
							 (cell_start + len),
							 true, sl_idx++,
							 &skip_list[i]);
				} else {
					this_recx->rx_nr = (recx_size -
						cell_start) / iod->iod_size;
					ec_bulk_spec_set(recx_size - cell_start,
							 false, sl_idx++,
							 &skip_list[i]);
				}
			}
			idx++;
		}
	}
out:
	return rc;
}

/* Determines if parity exists for the specified stripe:
 * - stripe is the index of the stripe, zero relative.
 * - pss is the size of the parity stripe (p * len);
 */
static bool
ec_has_parity_srv(daos_recx_t *recxs, uint64_t stripe, uint32_t pss,
		  uint32_t iod_size)
{
	unsigned int j;

	for (j = 0; recxs[j].rx_idx & PARITY_INDICATOR; j++) {
		uint64_t p_stripe = (~PARITY_INDICATOR &
					(recxs[j].rx_idx * iod_size)) / pss;

		if (p_stripe == stripe)
			return true;
	}
	return false;
}

/* Process IOD array on parity target. Keeps parity extents that are addressed
 * to this target. Also retains all data extents that do not have parity for
 * their stripe.
 */
int
ec_parity_target(unsigned int ptgt_idx, unsigned int nr, daos_iod_t *iods,
		 struct daos_oclass_attr *oca, struct ec_bulk_spec **skip_list)
{
	unsigned long	ss = oca->u.ec.e_len * oca->u.ec.e_k;
	uint32_t	pss = oca->u.ec.e_len * oca->u.ec.e_p;
	unsigned int	i, j, idx;
	int		rc = 0;

	for (i = 0; i < nr; i++) {
		daos_iod_t	*iod = &iods[i];
		unsigned int loop_bound = iod->iod_nr;
		int		 sl_idx = 0;

		if (iod->iod_type == DAOS_IOD_SINGLE)
			continue;
		D_ALLOC_ARRAY(skip_list[i], iod->iod_nr + 1);
		if (skip_list[i] == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		for (idx = 0, j = 0; j < loop_bound; j++) {
			daos_recx_t	*this_recx = &iod->iod_recxs[idx];

			if (iod->iod_recxs[idx].rx_idx & PARITY_INDICATOR) {
				uint64_t	p_address = ~PARITY_INDICATOR &
					this_recx->rx_idx * iod->iod_size;
				unsigned int	so = p_address % pss;
				unsigned int	pcell = so / oca->u.ec.e_len;

				if (pcell == ptgt_idx) {
					ec_bulk_spec_set(oca->u.ec.e_len, false,
							 sl_idx++,
							 &skip_list[i]);
				} else {
					ec_del_recx(iod, idx);
					ec_bulk_spec_set(oca->u.ec.e_len, true,
							 sl_idx++,
							 &skip_list[i]);
					continue;
				}
			} else {
				uint64_t stripe = (this_recx->rx_idx *
						iod->iod_size) / ss;

				if (ec_has_parity_srv(iod->iod_recxs, stripe,
						      pss, iod->iod_size)) {

					ec_bulk_spec_set(this_recx->rx_nr *
							 iod->iod_size, true,
							 sl_idx++,
							 &skip_list[i]);
					ec_del_recx(iod, idx);
					continue;
				} else {
					ec_bulk_spec_set(this_recx->rx_nr *
							 iod->iod_size, false,
							 sl_idx++,
							 &skip_list[i]);
				}
			}
			idx++;
		}
	}
out:
	return rc;
}



/* Free the memory allocated for copy of the IOD array
 */
void
ec_free_iods(daos_iod_t *iods, int nr)
{
	int i;

	for (i = 0; i < nr; i++)
		D_FREE(iods[i].iod_recxs);
	D_FREE(iods);
}

/* Make a copy of an iod array.
 */
int
ec_copy_iods(daos_iod_t *in_iod, int nr, daos_iod_t **out_iod)
{
	int i, rc = 0;

	D_ASSERT(*out_iod == NULL);
	D_ALLOC_ARRAY(*out_iod, nr);
	if (*out_iod == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	for (i = 0; i < nr; i++) {
		(*out_iod)[i] = in_iod[i];
		if (in_iod[i].iod_type == DAOS_IOD_ARRAY) {
			D_ALLOC_ARRAY((*out_iod)[i].iod_recxs,
				      (*out_iod)[i].iod_nr);
			if ((*out_iod)[i].iod_recxs == NULL) {
				int j;

				for (j = 0; j < i; j++)
					D_FREE((*out_iod)[j].iod_recxs);
				D_FREE(*out_iod);
				D_GOTO(out, rc = -DER_NOMEM);
			}

			memcpy((*out_iod)[i].iod_recxs, in_iod[i].iod_recxs,
			       in_iod[i].iod_nr * sizeof(daos_recx_t));
		}
	}
out:
	return rc;
}
