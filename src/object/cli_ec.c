/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
 * DAOS client erasure-coded object IO handling.
 *
 * src/object/cli_ec.c
 */
#define D_LOGFAC	DD_FAC(object)

#include <daos/common.h>
#include <daos_task.h>
#include <daos_types.h>
#include "obj_rpc.h"
#include "obj_internal.h"

#define EC_DEBUG 0
#define EC_REASB_TRACE 0

#if EC_REASB_TRACE
#define EC_TRACE(fmt, ...)						\
	do {								\
		fprintf(stdout, fmt, ## __VA_ARGS__);			\
		fflush(stdout);						\
	} while (0)
#else
#define EC_TRACE(fmt, ...)
#endif

static int
obj_ec_recxs_init(struct obj_ec_recx_array *recxs, uint32_t recx_nr)
{
	if (recxs->oer_recxs != NULL) {
		D_ERROR("oer_recxs non-NULL, cannot init again.\n");
		return -DER_INVAL;
	}

	if (recx_nr == 0)
		return 0;

	D_ALLOC_ARRAY(recxs->oer_recxs, recx_nr);
	if (recxs->oer_recxs == NULL)
		return -DER_NOMEM;

	return 0;
}

static void
obj_ec_pbuf_fini(struct obj_ec_recx_array *recxs)
{
	int	i;

	D_FREE(recxs->oer_pbufs[0]);
	for (i = 0; i < recxs->oer_p; i++)
		recxs->oer_pbufs[i] = NULL;
}

void
obj_ec_recxs_fini(struct obj_ec_recx_array *recxs)
{
	if (recxs == NULL)
		return;
	if (recxs->oer_recxs != NULL)
		D_FREE(recxs->oer_recxs);
	recxs->oer_nr = 0;
	recxs->oer_stripe_total = 0;
	obj_ec_pbuf_fini(recxs);
}

static int
obj_ec_pbufs_init(struct obj_ec_recx_array *recxs, uint64_t cell_bytes)
{
	void		*pbuf;
	uint8_t		*ptmp;
	uint64_t	 parity_len;
	int		 i;

	if (recxs->oer_stripe_total == 0)
		return 0;

	parity_len = roundup(recxs->oer_stripe_total * cell_bytes, 8);
	D_ALLOC(pbuf, parity_len * recxs->oer_p);
	if (pbuf == NULL)
		return -DER_NOMEM;

	ptmp = pbuf;
	for (i = 0; i < recxs->oer_p; i++) {
		recxs->oer_pbufs[i] = (void *)ptmp;
		ptmp += parity_len;
	}
	return 0;
}

static int
obj_ec_riod_init(daos_iod_t *riod, uint32_t recx_nr)
{
	riod->iod_nr = recx_nr;
	D_ALLOC_ARRAY(riod->iod_recxs, recx_nr);
	if (riod->iod_recxs == NULL)
		return -DER_NOMEM;
	return 0;
}

static int
obj_ec_seg_sorter_init(struct obj_ec_seg_sorter *sorter, uint32_t tgt_nr,
		       uint32_t seg_nr)
{
	void		*buf;
	daos_size_t	 buf_size;
	int		 i;

	buf_size = sizeof(struct obj_ec_seg_head) * tgt_nr;
	D_ALLOC(buf, buf_size + sizeof(struct obj_ec_seg) * seg_nr);
	if (buf == NULL)
		return -DER_NOMEM;

	sorter->ess_tgt_nr_total = tgt_nr;
	sorter->ess_seg_nr_total = seg_nr;
	sorter->ess_tgts = buf;
	sorter->ess_segs = buf + buf_size;
	for (i = 0; i < tgt_nr; i++) {
		sorter->ess_tgts[i].esh_tgt_idx = i;
		sorter->ess_tgts[i].esh_first = OBJ_EC_SEG_NIL;
		sorter->ess_tgts[i].esh_last = OBJ_EC_SEG_NIL;
	}
	return 0;
}

void
obj_ec_seg_sorter_fini(struct obj_ec_seg_sorter *sorter)
{
	if (sorter->ess_tgts != NULL)
		D_FREE(sorter->ess_tgts);
	memset(sorter, 0, sizeof(*sorter));
}

static void
obj_ec_seg_insert(struct obj_ec_seg_sorter *sorter, uint32_t tgt_idx,
		 d_iov_t *iovs, uint32_t iov_nr)
{
	struct obj_ec_seg_head	*tgt_head = &sorter->ess_tgts[tgt_idx];
	struct obj_ec_seg	*seg = sorter->ess_segs;
	d_iov_t			*tmp_iov;
	uint32_t		 i, seg_idx = sorter->ess_seg_nr;

	D_ASSERT(tgt_idx < sorter->ess_tgt_nr_total);
	D_ASSERT(sorter->ess_seg_nr + iov_nr <= sorter->ess_seg_nr_total);
	D_ASSERT(iov_nr > 0);
	for (i = 0; i < iov_nr; i++) {
		D_ASSERT(iovs[i].iov_len > 0);
		EC_TRACE("tgt %d insert segment iov_buf %p, iov_len %zu, "
			 "iov_buf_len %zu.\n", tgt_idx, iovs[i].iov_buf,
			 iovs[i].iov_len, iovs[i].iov_buf_len);
	}

	if (tgt_head->esh_seg_nr == 0)
		sorter->ess_tgt_nr++;

	if (tgt_head->esh_first == OBJ_EC_SEG_NIL) {
		tgt_head->esh_first = seg_idx;
	} else {
		D_ASSERT(tgt_head->esh_last != OBJ_EC_SEG_NIL);
		tmp_iov = &seg[tgt_head->esh_last].oes_iov;
		while (tmp_iov->iov_buf + tmp_iov->iov_len == iovs[0].iov_buf) {
			tmp_iov->iov_len += iovs[0].iov_len;
			tmp_iov->iov_buf_len = tmp_iov->iov_len;
			iovs++;
			iov_nr--;
			if (iov_nr == 0)
				return;
		}
		seg[tgt_head->esh_last].oes_next = seg_idx;
	}

	for (i = 0; i < iov_nr; i++) {
		seg[seg_idx].oes_iov = iovs[i];
		seg[seg_idx].oes_next = (i == iov_nr - 1) ? OBJ_EC_SEG_NIL :
					(seg_idx + 1);
		seg_idx++;
	}

	sorter->ess_seg_nr += iov_nr;
	tgt_head->esh_seg_nr += iov_nr;
	tgt_head->esh_last = sorter->ess_seg_nr - 1;
}

/* pack segments in the sorter to a compact sgl */
static void
obj_ec_seg_pack(struct obj_ec_seg_sorter *sorter, d_sg_list_t *sgl)
{
	struct obj_ec_seg_head	*tgt_head;
	struct obj_ec_seg	*seg;
	uint32_t		 tgt, idx = 0;

	D_ASSERT(sorter->ess_seg_nr <= sgl->sg_nr);
	for (tgt = 0; tgt < sorter->ess_tgt_nr_total; tgt++) {
		tgt_head = &sorter->ess_tgts[tgt];
		if (tgt_head->esh_seg_nr == 0)
			continue;
		D_ASSERT(tgt_head->esh_first != OBJ_EC_SEG_NIL);
		seg = &sorter->ess_segs[tgt_head->esh_first];
		do {
			sgl->sg_iovs[idx++] = seg->oes_iov;
			if (seg->oes_next == OBJ_EC_SEG_NIL)
				break;
			seg = &sorter->ess_segs[seg->oes_next];
		} while (1);
	}
	D_ASSERT(idx <= sgl->sg_nr);
	sgl->sg_nr = idx;
}

/* update recx_nrs on all data cells */
#define ec_data_tgt_recx_nrs(oca, recx_nrs, i)				       \
	do {								       \
		for (i = 0; i < (oca)->u.ec.e_k; i++)			       \
			recx_nrs[i]++;					       \
	} while (0)

/* update recx_nrs for replica on all parity cells */
#define ec_parity_tgt_recx_nrs(oca, recx_nrs, i, cnt)			       \
	do {								       \
		for (i = 0; i < (oca)->u.ec.e_p; i++)			       \
			recx_nrs[(oca)->u.ec.e_k + i] += cnt;		       \
	} while (0)

/* update recx_nrs on all targets */
#define ec_all_tgt_recx_nrs(oca, recx_nrs, i)				       \
	do {								       \
		for (i = 0; i < obj_ec_tgt_nr(oca); i++)		       \
			recx_nrs[i]++;					       \
	} while (0)

/* update recx_nrs for partial update */
#define ec_partial_tgt_recx_nrs(recx, stripe_rec_nr, oca, recx_nrs, i, update) \
	do {								       \
		uint64_t tmp_idx, tmp_end;				       \
		uint32_t tgt;						       \
		if (update) {						       \
			/* each parity node have one recx as replica */	       \
			ec_parity_tgt_recx_nrs(oca, recx_nrs, i, 1);	       \
		}							       \
		/* then add recx_nrs on data cells */			       \
		if ((recx)->rx_nr > ((stripe_rec_nr) - (oca)->u.ec.e_len)) {   \
			/* at most one recx on each data cell */	       \
			ec_data_tgt_recx_nrs(oca, recx_nrs, i);		       \
			break;						       \
		}							       \
		/* update recx_nrs on recx covered data cells */	       \
		tmp_idx = rounddown((recx)->rx_idx, (oca)->u.ec.e_len);	       \
		tmp_end = (recx)->rx_idx + (recx)->rx_nr;		       \
		while (tmp_idx < tmp_end) {				       \
			tgt = obj_ec_tgt_of_recx_idx(			       \
				tmp_idx, stripe_rec_nr, (oca)->u.ec.e_len);    \
			recx_nrs[tgt]++;				       \
			tmp_idx += (oca)->u.ec.e_len;			       \
		}							       \
	} while (0)

/** scan the iod to find the full_stripe recxs and some help info */
static int
obj_ec_recx_scan(daos_iod_t *iod, d_sg_list_t *sgl,
		 struct daos_oclass_attr *oca, struct obj_reasb_req *reasb_req,
		 uint32_t iod_idx, bool update)
{
	uint8_t				*tgt_bitmap = reasb_req->tgt_bitmap;
	struct obj_ec_recx_array	*ec_recx_array;
	struct obj_ec_recx		*ec_recx = NULL;
	daos_recx_t			*recx;
	uint32_t			*tgt_recx_nrs;
	uint32_t			 recx_nr, tgt_nr, seg_nr = 0;
	uint32_t			 partial_nr, oiod_flags = 0;
	uint64_t			 stripe_rec_nr;
	uint64_t			 start, end, rec_nr, rec_off;
	bool				 full_stripe_only = true;
	bool				 parity_seg_counted = false;
	bool				 frag_seg_counted = false;
	bool				 punch;
	int				 i, j, idx, rc;

	stripe_rec_nr = obj_ec_stripe_rec_nr(oca);
	ec_recx_array = &reasb_req->orr_recxs[iod_idx];
	tgt_recx_nrs = ec_recx_array->oer_tgt_recx_nrs;
	ec_recx_array->oer_k = oca->u.ec.e_k;
	ec_recx_array->oer_p = oca->u.ec.e_p;
	punch = (update && iod->iod_size == DAOS_REC_ANY);

	for (i = 0, idx = 0, rec_off = 0; i < iod->iod_nr; i++) {
		recx = &iod->iod_recxs[i];
		/* add segment number on data cells */
		seg_nr += obj_ec_recx_cell_nr(recx, oca);
		start = roundup(recx->rx_idx, stripe_rec_nr);
		end = rounddown(recx->rx_idx + recx->rx_nr, stripe_rec_nr);
		if (start >= end) {
			ec_partial_tgt_recx_nrs(recx, stripe_rec_nr, oca,
						tgt_recx_nrs, j, update);
			/* replica with one segment on each parity cell */
			if (update) {
				if (!frag_seg_counted) {
					seg_nr += oca->u.ec.e_p * sgl->sg_nr;
					frag_seg_counted = true;
				} else {
					seg_nr += oca->u.ec.e_p;
				}
				rec_off += recx->rx_nr;
			}
			full_stripe_only = false;
			continue;
		}

		/* at least one recx on each tgt for full stripe */
		if (update) {
			ec_all_tgt_recx_nrs(oca, tgt_recx_nrs, j);
		} else {
			ec_data_tgt_recx_nrs(oca, tgt_recx_nrs, j);
			continue;
		}

		/* Encoded parity code with one segment on each parity cell */
		if (!parity_seg_counted) {
			seg_nr += oca->u.ec.e_p;
			parity_seg_counted = true;
		}
		if (ec_recx_array->oer_recxs == NULL) {
			rc = obj_ec_recxs_init(ec_recx_array, iod->iod_nr - i);
			if (rc)
				return rc;
			ec_recx = ec_recx_array->oer_recxs;
		}
		D_ASSERT(ec_recx != NULL);
		ec_recx[idx].oer_idx = i;
		rec_nr = end - start;
		ec_recx[idx].oer_stripe_nr = rec_nr / stripe_rec_nr;
		ec_recx[idx].oer_byte_off = (rec_off + start - recx->rx_idx) *
					    iod->iod_size;
		ec_recx[idx].oer_recx.rx_idx = start;
		ec_recx[idx].oer_recx.rx_nr = rec_nr;
		ec_recx_array->oer_stripe_total += ec_recx[idx].oer_stripe_nr;
		idx++;
		rec_off += recx->rx_nr;
		/* partial update before or after full stripe need replica to
		 * parity target.
		 */
		partial_nr = 0;
		if (recx->rx_idx < start)
			partial_nr++;
		if (recx->rx_idx + recx->rx_nr > end)
			partial_nr++;
		if (partial_nr > 0) {
			full_stripe_only = false;
			ec_parity_tgt_recx_nrs(oca, tgt_recx_nrs, j,
					       partial_nr);
			/* replica to each parity cell */
			if (!frag_seg_counted) {
				seg_nr += oca->u.ec.e_p * sgl->sg_nr *
						partial_nr;
				frag_seg_counted = true;
			} else {
				seg_nr += oca->u.ec.e_p * partial_nr;
			}
		}
	}

	if (update && ec_recx_array->oer_recxs != NULL) {
		D_ASSERT(idx > 0 && idx <= iod->iod_nr);
		ec_recx_array->oer_nr = idx;
	} else {
		D_ASSERT(ec_recx_array->oer_nr == 0);
	}

	for (i = 0, recx_nr = 0, tgt_nr = 0; i < obj_ec_tgt_nr(oca); i++) {
		ec_recx_array->oer_tgt_recx_idxs[i] = recx_nr;
		recx_nr += tgt_recx_nrs[i];
		if (tgt_recx_nrs[i] != 0) {
			setbit(tgt_bitmap, i);
			tgt_nr++;
		}
	}
	if (update && full_stripe_only) {
		D_ASSERT(tgt_nr == obj_ec_tgt_nr(oca));
		oiod_flags = OBJ_SIOD_EVEN_DIST;
	}
	rc = obj_io_desc_init(&reasb_req->orr_oiods[iod_idx], tgt_nr,
			      oiod_flags);
	if (rc)
		goto out;
	rc = obj_ec_riod_init(&reasb_req->orr_iods[iod_idx], recx_nr);
	if (rc)
		goto out;
	/* init the reassembled sgl and seg sorter with max possible sg_nr */
	if (!punch) {
		rc = daos_sgl_init(&reasb_req->orr_sgls[iod_idx],
				   seg_nr + sgl->sg_nr);
		if (rc)
			goto out;
		rc = obj_ec_seg_sorter_init(&reasb_req->orr_sorters[iod_idx],
					    obj_ec_tgt_nr(oca),
					    seg_nr + sgl->sg_nr);
		if (rc)
			goto out;
	}
	if (update)
		rc = obj_ec_pbufs_init(ec_recx_array,
				       obj_ec_cell_bytes(iod, oca));

out:
	return rc;
}

/** Encode one full stripe, the result parity buffer will be filled. */
static int
obj_ec_stripe_encode(daos_iod_t *iod, d_sg_list_t *sgl, uint32_t iov_idx,
		     size_t iov_off, struct obj_ec_codec *codec,
		     struct daos_oclass_attr *oca, uint64_t cell_bytes,
		     unsigned char *parity_bufs[])
{
	uint64_t			 len = cell_bytes;
	unsigned int			 k = oca->u.ec.e_k;
	unsigned int			 p = oca->u.ec.e_p;
	unsigned char			*data[k];
	unsigned char			*c_data[k]; /* copied data */
	unsigned char			*from;
	struct obj_ec_singv_local	 loc = {0};
	int				 i, c_idx = 0;
	int				 rc = 0;

	if (iod->iod_type == DAOS_IOD_SINGLE)
		obj_ec_singv_local_sz(iod->iod_size, oca, k - 1, &loc);

	for (i = 0; i < k; i++) {
		c_data[i] = NULL;
		/* for singv the last data target may need padding of zero */
		if (i == k - 1) {
			len = cell_bytes - loc.esl_bytes_pad;
			D_ASSERT(len > 0 && len <= cell_bytes);
		}
		if (daos_iov_left(sgl, iov_idx, iov_off) >= len) {
			from = (unsigned char *)sgl->sg_iovs[iov_idx].iov_buf;
			data[i] = &from[iov_off];
			daos_sgl_move(sgl, iov_idx, iov_off, len);
		} else {
			uint64_t copied = 0;

			D_ALLOC(c_data[c_idx], len);
			if (c_data[c_idx] == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			while (copied < len) {
				uint64_t left;
				uint64_t cp_len;
				uint64_t tobe_cp;

				tobe_cp = len - copied;
				left = daos_iov_left(sgl, iov_idx, iov_off);
				cp_len = MIN(tobe_cp, left);
				if (cp_len == 0) {
					daos_sgl_next_iov(iov_idx, iov_off);
				} else {
					from = sgl->sg_iovs[iov_idx].iov_buf;
					memcpy(&c_data[c_idx][copied],
					       &from[iov_off], cp_len);
					daos_sgl_move(sgl, iov_idx, iov_off,
						      cp_len);
					copied += cp_len;
				}
				if (copied < len && iov_idx >= sgl->sg_nr)
					D_GOTO(out, rc = -DER_REC2BIG);
			}
			data[i] = c_data[c_idx++];
		}
	}

	ec_encode_data(cell_bytes, k, p, codec->ec_gftbls, data, parity_bufs);

out:
	for (i = 0; i < c_idx; i++)
		D_FREE(c_data[i]);
	return rc;
}

/**
 * Encode the data in full stripe recx_array, the result parity stored in
 * struct obj_ec_recx_array::oer_pbufs.
 */
static int
obj_ec_recx_encode(daos_obj_id_t oid, daos_iod_t *iod, d_sg_list_t *sgl,
		   struct daos_oclass_attr *oca,
		   struct obj_ec_recx_array *recx_array)
{
	struct obj_ec_codec	*codec;
	struct obj_ec_recx	*ec_recx;
	unsigned int		 p = oca->u.ec.e_p;
	unsigned char		*parity_buf[p];
	uint64_t		 cell_bytes, stripe_bytes;
	uint32_t		 iov_idx = 0;
	uint64_t		 iov_off = 0, last_off = 0;
	uint32_t		 encoded_nr = 0;
	uint32_t		 recx_nr, stripe_nr;
	uint32_t		 i, j, m;
	bool			 singv;
	int			 rc;

	if (recx_array->oer_stripe_total == 0)
		D_GOTO(out, rc = 0);
	singv = (iod->iod_type == DAOS_IOD_SINGLE);
	codec = obj_ec_codec_get(daos_obj_id2class(oid));
	if (codec == NULL) {
		D_ERROR("failed to get ec codec.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (singv) {
		cell_bytes = obj_ec_singv_cell_bytes(iod->iod_size, oca);
		recx_nr = 1;
	} else {
		D_ASSERT(recx_array->oer_nr > 0);
		D_ASSERT(recx_array->oer_recxs != NULL);
		cell_bytes = obj_ec_cell_bytes(iod, oca);
		recx_nr = recx_array->oer_nr;
	}
	stripe_bytes = cell_bytes * oca->u.ec.e_k;

	/* calculate EC parity for each full_stripe */
	for (i = 0; i < recx_nr; i++) {
		if (singv) {
			stripe_nr = 1;
		} else {
			ec_recx = &recx_array->oer_recxs[i];
			daos_sgl_move(sgl, iov_idx, iov_off,
				      ec_recx->oer_byte_off - last_off);
			last_off = ec_recx->oer_byte_off;
			stripe_nr = ec_recx->oer_stripe_nr;
		}
		for (j = 0; j < stripe_nr; j++) {
			for (m = 0; m < p; m++)
				parity_buf[m] = recx_array->oer_pbufs[m] +
						encoded_nr * cell_bytes;
#if EC_DEBUG
			D_PRINT("encode %d rec_offset "DF_U64", rec_nr "
				DF_U64".\n", j, iov_off / iod->iod_size,
				stripe_bytes / iod->iod_size);
#endif
			rc = obj_ec_stripe_encode(iod, sgl, iov_idx, iov_off,
						  codec, oca, cell_bytes,
						  parity_buf);
			if (rc) {
				D_ERROR("stripe encoding failed rc %d.\n", rc);
				goto out;
			}
			if (singv)
				break;
			encoded_nr++;
			daos_sgl_move(sgl, iov_idx, iov_off, stripe_bytes);
			last_off += stripe_bytes;
		}
	}

out:
	return rc;
}

/**
 * Check if a recx (identified by \a recx_idx) is with full stripe, if it is
 * then output the corresponding full stripe pointer \a ec_recx.
 */
static bool
recx_with_full_stripe(uint32_t recx_idx, struct obj_ec_recx_array *r_array,
		      struct obj_ec_recx **full_recx)
{
	struct obj_ec_recx	*ec_recx;
	uint32_t		 i;

	for (i = r_array->oer_last; i < r_array->oer_nr; i++) {
		ec_recx = &r_array->oer_recxs[i];
		if (ec_recx->oer_idx == recx_idx) {
			*full_recx = ec_recx;
			r_array->oer_last = i;
			return true;
		}
		if (ec_recx->oer_idx > recx_idx)
			break;
	}
	return false;
}

#define ec_recx_add(r_recx, r_idx, start_idx, tgt, recx_idx, recx_nr)	       \
	do {								       \
		uint32_t	cur_idx;				       \
		cur_idx = (start_idx)[tgt] + (r_idx)[tgt];		       \
		if ((r_idx[tgt] != 0) && ((r_recx)[cur_idx - 1].rx_idx +       \
			(r_recx)[cur_idx - 1].rx_nr) == (recx_idx)) {          \
			EC_TRACE("tgt %d, last_idx %d, idx "DF_U64", nr "DF_U64\
				 " merge with idx "DF_U64", nr "DF_U64"\n",    \
				 tgt, cur_idx - 1,			       \
				 (r_recx)[cur_idx - 1].rx_idx,		       \
				 (r_recx)[cur_idx - 1].rx_nr,		       \
				 recx_idx, recx_nr);			       \
			(r_recx)[cur_idx - 1].rx_nr += recx_nr;		       \
			break;						       \
		}							       \
		(r_recx)[cur_idx].rx_idx = (recx_idx);			       \
		(r_recx)[cur_idx].rx_nr = (recx_nr);			       \
		EC_TRACE("tgt %d, cur_idx %d, adding idx "DF_U64", nr "DF_U64  \
			 " start_idx[%d] %d, r_idx[%d] %d.\n", tgt, cur_idx,   \
			 recx_idx, recx_nr, tgt, (start_idx)[tgt], tgt,	       \
			 (r_idx)[tgt]);					       \
		(r_idx)[tgt]++;						       \
	} while (0)
#define ec_vos_idx(idx)							       \
	obj_ec_vos_recx_idx(idx, stripe_rec_nr, cell_rec_nr)

/**
 * Add data recx to reassemble recx array.
 * \param[in]		recx		User input recx
 * \param[out]		r_recx		reassembled recx
 * \param[int/out]	r_idx		tgts' recx index array
 * \param[in]		start_idx	tgts' recx start index array
 * \param[in]		oca		obj class attribute
 * \param[in]		add_parity	true to add to parity cells
 */
static inline void
ec_data_recx_add(daos_recx_t *recx, daos_recx_t *r_recx, uint32_t *r_idx,
		 uint32_t *start_idx, struct daos_oclass_attr *oca,
		 bool add_parity)
{
	uint64_t	stripe_rec_nr = obj_ec_stripe_rec_nr(oca);
	uint64_t	cell_rec_nr = obj_ec_cell_rec_nr(oca);
	uint64_t	start, end, r_start, r_end, tmp_idx, tmp_nr, tmp_end;
	uint32_t	i, first_tgt, last_tgt, tgt;

	if (recx->rx_nr == 0)
		return;

	EC_TRACE("adding recx idx "DF_U64", nr "DF_U64", add_parity %d.\n",
		 recx->rx_idx, recx->rx_nr, add_parity);

	if (add_parity) {
		/* replicated data on parity node need not VOS index mapping */
		for (i = 0; i < obj_ec_parity_tgt_nr(oca); i++)
			ec_recx_add(r_recx, r_idx, start_idx,
				    obj_ec_data_tgt_nr(oca) + i,
				    recx->rx_idx, recx->rx_nr);
	}

	start = recx->rx_idx;
	end = start + recx->rx_nr;
	/* for small recx, add recx per cell one by one */
	if (recx->rx_nr <= (stripe_rec_nr - cell_rec_nr)) {
		/* add first recx */
		tmp_idx = recx->rx_idx;
		tmp_nr = MIN(recx->rx_nr, cell_rec_nr - tmp_idx % cell_rec_nr);
		tgt = obj_ec_tgt_of_recx_idx(tmp_idx, stripe_rec_nr,
					     cell_rec_nr);
		ec_recx_add(r_recx, r_idx, start_idx, tgt, ec_vos_idx(tmp_idx),
			    tmp_nr);
		/* add remaining recxs */
		tmp_idx = roundup(tmp_idx + 1, cell_rec_nr);
		while (tmp_idx < end) {
			tgt = obj_ec_tgt_of_recx_idx(tmp_idx, stripe_rec_nr,
						     cell_rec_nr);
			tmp_nr = MIN(cell_rec_nr, end - tmp_idx);
			ec_recx_add(r_recx, r_idx, start_idx, tgt,
				    ec_vos_idx(tmp_idx), tmp_nr);
			tmp_idx += cell_rec_nr;
		}
		return;
	}
	/* for large recx, more efficient to calculate per target */
	first_tgt = obj_ec_tgt_of_recx_idx(start, stripe_rec_nr,
					   obj_ec_cell_rec_nr(oca));
	last_tgt = obj_ec_tgt_of_recx_idx(end - 1, stripe_rec_nr,
					   obj_ec_cell_rec_nr(oca));
	for (i = 0; i < obj_ec_data_tgt_nr(oca); i++) {
		if (i < first_tgt)
			r_start = roundup(start, stripe_rec_nr) +
				  i * cell_rec_nr;
		else if (i == first_tgt)
			r_start = start;
		else
			r_start = rounddown(start, cell_rec_nr) +
				  (i - first_tgt) * cell_rec_nr;

		if (i < last_tgt)
			r_end = rounddown(end - 1, stripe_rec_nr) +
				(i + 1) * cell_rec_nr;
		else if (i == last_tgt)
			r_end = end;
		else
			r_end = rounddown(end, stripe_rec_nr) -
				stripe_rec_nr + (i + 1) * cell_rec_nr;
		D_ASSERT(r_end > r_start);
		D_ASSERT(i == obj_ec_tgt_of_recx_idx(r_start, stripe_rec_nr,
						     cell_rec_nr));
		tmp_idx = ec_vos_idx(r_start);
		tmp_end = ec_vos_idx(r_end);
		if (r_end % cell_rec_nr == 0 && r_end % stripe_rec_nr != 0)
			tmp_end += cell_rec_nr;
		tmp_nr = tmp_end - tmp_idx;
		EC_TRACE("tgt %d, r_start "DF_U64", r_end "DF_U64", tmp_idx "
			 DF_U64", tmp_end "DF_U64", first_tgt %d,last_tgt %d\n",
			 i, r_start, r_end, tmp_idx, tmp_end, first_tgt,
			 last_tgt);
		ec_recx_add(r_recx, r_idx, start_idx, i, tmp_idx, tmp_nr);
	}
}

/** Add parity recx (full-stripe) to reassemble recx array */
static inline void
ec_parity_recx_add(daos_recx_t *recx, daos_recx_t *r_recx, uint32_t *r_idx,
		   uint32_t *start_idx, struct daos_oclass_attr *oca)
{
	uint64_t	stripe_rec_nr = obj_ec_stripe_rec_nr(oca);
	uint64_t	cell_rec_nr = obj_ec_cell_rec_nr(oca);
	uint64_t	tmp_idx, tmp_nr;
	uint32_t	i;

	D_ASSERTF((recx->rx_idx % stripe_rec_nr) == 0, "bad rx_idx\n");
	D_ASSERTF((recx->rx_nr % stripe_rec_nr) == 0, "bad rx_nr\n");
	D_ASSERT(recx->rx_nr > 0);
	tmp_idx = ec_vos_idx(recx->rx_idx) | PARITY_INDICATOR;
	tmp_nr = (recx->rx_nr / stripe_rec_nr) * cell_rec_nr;

	for (i = 0; i < obj_ec_parity_tgt_nr(oca); i++)
		ec_recx_add(r_recx, r_idx, start_idx,
			    obj_ec_data_tgt_nr(oca) + i,
			    tmp_idx, tmp_nr);
}

/**
 * Add mem segment to seg_sorter, then later can pack to reassemble sgl.
 *
 * \param[in]		recx		User input recx
 * \param[in]		iod_size	recored size
 * \param[in]		sgl		User input sgl
 * \param[in]		idx		index of sgl iov
 * \param[in]		off		offset of the sgl iov
 * \param[in]		oca		obj class attribute
 * \param[in]		iovs		temporary buffer for iov segments
 * \param[in]		iov_capa	capacity number of iovs
 * \param[out]		sorter		seg sorter to insert mem segments
 * \param[in]		add_parity	true to add to parity cells
 */
static void
ec_data_seg_add(daos_recx_t *recx, daos_size_t iod_size, d_sg_list_t *sgl,
		uint32_t *idx, uint64_t *off, struct daos_oclass_attr *oca,
		d_iov_t *iovs, uint32_t iov_capa,
		struct obj_ec_seg_sorter *sorter, bool add_parity)
{
	uint64_t	stripe_rec_nr = obj_ec_stripe_rec_nr(oca);
	uint64_t	cell_rec_nr = obj_ec_cell_rec_nr(oca);
	uint64_t	recx_size, recx_idx, recx_nr, iov_off, end;
	uint32_t	i, iov_idx, tgt, iov_nr = 0;

	if (recx->rx_nr == 0)
		return;
	recx_size = recx->rx_nr * iod_size;

	if (add_parity) {
		iov_idx = *idx;
		iov_off = *off;
		daos_sgl_consume(sgl, iov_idx, iov_off, recx_size, iovs,
				 iov_nr);
		D_ASSERT(iov_nr <= iov_capa);
		for (i = 0; i < obj_ec_parity_tgt_nr(oca); i++)
			obj_ec_seg_insert(sorter, obj_ec_data_tgt_nr(oca) + i,
					  iovs, iov_nr);
	}

	iov_idx = *idx;
	iov_off = *off;
	end = recx->rx_idx + recx->rx_nr;
	/* add segment one by one, start from first cell */
	recx_idx = (recx)->rx_idx;
	recx_nr = MIN(recx->rx_nr, cell_rec_nr - recx_idx % cell_rec_nr);
	recx_size = recx_nr * iod_size;
	tgt = obj_ec_tgt_of_recx_idx(recx_idx, stripe_rec_nr, cell_rec_nr);
	daos_sgl_consume(sgl, iov_idx, iov_off, recx_size, iovs, iov_nr);
	D_ASSERT(iov_nr <= iov_capa);
	obj_ec_seg_insert(sorter, tgt, iovs, iov_nr);
	/* add remaining recxs */
	recx_idx = roundup(recx_idx + 1, cell_rec_nr);
	while (recx_idx < end) {
		recx_nr = MIN(cell_rec_nr, end - recx_idx);
		tgt = obj_ec_tgt_of_recx_idx(recx_idx, stripe_rec_nr,
					     cell_rec_nr);
		recx_size = recx_nr * iod_size;
		daos_sgl_consume(sgl, iov_idx, iov_off, recx_size, iovs,
				 iov_nr);
		D_ASSERT(iov_nr <= iov_capa);
		obj_ec_seg_insert(sorter, tgt, iovs, iov_nr);
		recx_idx += cell_rec_nr;
	}
	*idx = iov_idx;
	*off = iov_off;
}

static void
ec_parity_seg_add(struct obj_ec_recx_array *ec_recxs, daos_iod_t *iod,
		  struct daos_oclass_attr *oca,
		  struct obj_ec_seg_sorter *sorter)
{
	uint64_t	cell_bytes = obj_ec_cell_bytes(iod, oca);
	d_iov_t		iov;
	uint32_t	i;

	if (ec_recxs->oer_stripe_total == 0)
		return;
	iov.iov_len = ec_recxs->oer_stripe_total * cell_bytes;
	iov.iov_buf_len = iov.iov_len;
	for (i = 0; i < obj_ec_parity_tgt_nr(oca); i++) {
		iov.iov_buf = ec_recxs->oer_pbufs[i];
		obj_ec_seg_insert(sorter, obj_ec_data_tgt_nr(oca) + i,
				  &iov, 1);
	}
}

static void
dump_recx(daos_recx_t *recx, struct daos_oclass_attr *oca,
	  uint64_t stripe_rec_nr, uint32_t tgt)
{
	uint64_t	tmp_idx, start;

	if (oca == NULL) {
		/* just dump raw recx */
		if (recx->rx_idx & PARITY_INDICATOR) {
			tmp_idx = recx->rx_idx & (~PARITY_INDICATOR);
			D_PRINT(" [P_"DF_U64", "DF_U64"]", tmp_idx,
				recx->rx_nr);
		} else {
			D_PRINT(" ["DF_U64", "DF_U64"]", recx->rx_idx,
				recx->rx_nr);
		}
		return;
	}

	/* when oca != NULL, translate VOS idx to original daos index */
	if (tgt < obj_ec_data_tgt_nr(oca)) {
		start = obj_ec_idx_of_vos_idx(recx->rx_idx, stripe_rec_nr,
				obj_ec_cell_rec_nr(oca), tgt);
		D_PRINT(" ["DF_U64", "DF_U64"]", start, recx->rx_nr);
	} else {
		if (recx->rx_idx & PARITY_INDICATOR) {
			tmp_idx = recx->rx_idx & (~PARITY_INDICATOR);
			start = obj_ec_idx_of_vos_idx(tmp_idx, stripe_rec_nr,
					obj_ec_cell_rec_nr(oca),
					tgt - obj_ec_data_tgt_nr(oca));
			D_PRINT(" [P_"DF_U64", "DF_U64"]", start,
				recx->rx_nr);
		} else {
			D_PRINT(" ["DF_U64", "DF_U64"]", recx->rx_idx,
				recx->rx_nr);
		}
	}
}

void
obj_reasb_req_dump(struct obj_reasb_req *reasb_req, d_sg_list_t *usgl,
		   struct daos_oclass_attr *oca, uint64_t stripe_rec_nr,
		   uint32_t iod_idx)
{
	daos_iod_t			*iod;
	daos_recx_t			*recx;
	d_sg_list_t			*sgl;
	d_iov_t				*iov;
	struct obj_io_desc		*oiod;
	struct obj_shard_iod		*siod;
	struct obj_ec_recx_array	*ec_recx_array;
	uint32_t			*tgt_recx_nrs;
	uint32_t			*tgt_recx_idxs;
	struct obj_ec_recx		*ec_recx;
	uint8_t				*tgt_bitmap = reasb_req->tgt_bitmap;
	uint64_t			 offset = 0;
	uint32_t			 i, j, idx, tgt;

	i = iod_idx;
	iod = &reasb_req->orr_iods[i];
	sgl = &reasb_req->orr_sgls[i];
	oiod = &reasb_req->orr_oiods[i];
	ec_recx_array = &reasb_req->orr_recxs[i];
	tgt_recx_nrs = ec_recx_array->oer_tgt_recx_nrs;
	tgt_recx_idxs = ec_recx_array->oer_tgt_recx_idxs;
	D_PRINT("================ reasb req %d ================\n", i);
	D_PRINT("iod, akey %s, iod_size "DF_U64", iod_nr %d\n",
		(char *)iod->iod_name.iov_buf, iod->iod_size,
		iod->iod_nr);
	D_PRINT("recxs per target [daos_idx, nr]:\n");
	for (tgt = 0; tgt < obj_ec_tgt_nr(oca); tgt++) {
		if (tgt_recx_nrs[tgt] == 0)
			continue;
		D_PRINT("tgt[%2d]: ", tgt);
		for (j = 0; j < tgt_recx_nrs[tgt]; j++) {
			idx = tgt_recx_idxs[tgt] + j;
			recx = &iod->iod_recxs[idx];
			dump_recx(recx, oca, stripe_rec_nr, tgt);
		}
		D_PRINT("\n");
	}

	if (iod->iod_recxs != NULL) {
		D_PRINT("\nrecxs array [vos_idx, nr]:\n");
		for (j = 0; j < iod->iod_nr; j++) {
			recx = &iod->iod_recxs[j];
			if (j % 8 == 0)
				D_PRINT("[%3d]:", j);
			dump_recx(recx, NULL, 0, 0);
			if (j % 8 == 7)
				D_PRINT("\n");
		}
		D_PRINT("\n");
	}

	D_PRINT("\nsgl, sg_nr %d, sg_nr_out %d\n", sgl->sg_nr, sgl->sg_nr_out);
	D_PRINT("segments [iov_buf (offset), iov_len]:\n");
	D_PRINT("(offset is only meaningful for data (non-parity) "
		"segments when user sgl with only one segment)\n");
	for (j = 0; j < sgl->sg_nr; j++) {
		iov = &sgl->sg_iovs[j];
		offset = (uintptr_t)(iov->iov_buf) -
			 (uintptr_t)(usgl->sg_iovs[0].iov_buf);
		if (j % 4 == 0)
			D_PRINT("[%3d]:", j);
		D_PRINT(" [%p(off "DF_U64"), %zu]", iov->iov_buf,
			offset, iov->iov_len);
		if (j % 4 == 3)
			D_PRINT("\n");
	}
	D_PRINT("\n");

	D_PRINT("\noiod, oiod_nr %d, oiod_flags 0x%x\n",
		oiod->oiod_nr, oiod->oiod_flags);
	D_PRINT("siods [siod_tgt_idx, (siod_idx, siod_nr), siod_off]:\n");
	for (j = 0; oiod->oiod_siods != NULL && j < oiod->oiod_nr; j++) {
		siod = &oiod->oiod_siods[j];
		D_PRINT("[%3d]:", j);
		D_PRINT(" [%d, (%d, %d), "DF_U64"]\n",
			siod->siod_tgt_idx, siod->siod_idx,
			siod->siod_nr, siod->siod_off);
	}

	D_PRINT("\nec_recx_array, oer_stripe_total %d, oer_nr %d\n",
		ec_recx_array->oer_stripe_total,
		ec_recx_array->oer_nr);
	D_PRINT("ec full stripes [oer_idx, oer_stripe_nr, oer_byte_off,"
		" (start, end)]:\n");
	for (j = 0; ec_recx_array->oer_recxs != NULL &&
		    j < ec_recx_array->oer_nr; j++) {
		ec_recx = &ec_recx_array->oer_recxs[j];
		recx = &ec_recx->oer_recx;
		if (j % 8 == 0)
			D_PRINT("[%3d]:", j);
		D_PRINT(" [%d, %d, "DF_U64", ("DF_U64", "DF_U64")]",
			ec_recx->oer_idx, ec_recx->oer_stripe_nr,
			ec_recx->oer_byte_off, recx->rx_idx,
			recx->rx_idx + recx->rx_nr);
		if (j % 8 == 7)
			D_PRINT("\n");
	}
	D_PRINT("\n");

	D_PRINT("\ntarget bit map:\n");
	for (tgt = 0; tgt < obj_ec_tgt_nr(oca); tgt++) {
		D_PRINT("tgt_%d:%d,", tgt, isset(tgt_bitmap, tgt) != 0);
		if (tgt % 8 == 7)
			D_PRINT("\n");
	}
	D_PRINT("\n");
}

#define EC_INLINE_IOVS		(16)
/**
 * Reassemble iod/sgl/recx for EC.
 * Input user \a iod, \a sgl, and \a recx_array,
 * Output reassembled \a riod, \a rsgl and \a oiod.
 */
static int
obj_ec_recx_reasb(daos_iod_t *iod, d_sg_list_t *sgl,
		  struct daos_oclass_attr *oca,
		  struct obj_reasb_req *reasb_req, uint32_t iod_idx,
		  bool update)
{
	struct obj_ec_recx_array	*ec_recx_array =
						&reasb_req->orr_recxs[iod_idx];
	daos_iod_t			*riod = &reasb_req->orr_iods[iod_idx];
	d_sg_list_t			*rsgl = &reasb_req->orr_sgls[iod_idx];
	struct obj_io_desc		*oiod = &reasb_req->orr_oiods[iod_idx];
	struct obj_shard_iod		*siod;
	struct obj_ec_seg_sorter	*sorter =
					&reasb_req->orr_sorters[iod_idx];
	uint32_t			*tgt_recx_nrs =
					 ec_recx_array->oer_tgt_recx_nrs;
	uint32_t			*tgt_recx_idxs =
					 ec_recx_array->oer_tgt_recx_idxs;
	uint64_t			 stripe_rec_nr =
						 obj_ec_stripe_rec_nr(oca);
	uint64_t			 cell_rec_nr = obj_ec_cell_rec_nr(oca);
	struct obj_ec_recx		*full_ec_recx;
	uint32_t			 tidx[OBJ_EC_MAX_M] = {0};
	uint32_t			 ridx[OBJ_EC_MAX_M] = {0};
	d_iov_t				 iov_inline[EC_INLINE_IOVS];
	daos_recx_t			*recx, *full_recx, tmp_recx;
	d_iov_t				*iovs = NULL;
	uint32_t			 i, j, k, idx, last;
	uint32_t			 tgt_nr, empty_nr;
	uint32_t			 iov_idx = 0, iov_nr = sgl->sg_nr;
	uint64_t			 iov_off = 0, recx_end, full_end;
	uint64_t			 rec_nr, iod_size = iod->iod_size;
	bool				 with_full_stripe;
	bool				 punch;
	int				 rc = 0;

	D_ASSERT(cell_rec_nr > 0);
	if (iov_nr <= EC_INLINE_IOVS) {
		iovs = iov_inline;
	} else {
		D_ALLOC_ARRAY(iovs, iov_nr);
		if (iovs == NULL)
			return -DER_NOMEM;
	}
	punch = (update && iod->iod_size == DAOS_REC_ANY);

	/* for array case */
	for (i = 0; i < iod->iod_nr; i++) {
		recx = &iod->iod_recxs[i];
		with_full_stripe = recx_with_full_stripe(i, ec_recx_array,
							 &full_ec_recx);
		if (!with_full_stripe || !update) {
			ec_data_recx_add(recx, riod->iod_recxs, ridx,
					 tgt_recx_idxs, oca, update);
			ec_data_seg_add(recx, iod_size, sgl, &iov_idx, &iov_off,
					oca, iovs, iov_nr, sorter, update);
			continue;
		}

		full_recx = &full_ec_recx->oer_recx;
		D_ASSERT(recx->rx_idx <= full_recx->rx_idx);
		if (recx->rx_idx < full_recx->rx_idx) {
			tmp_recx.rx_idx = recx->rx_idx;
			tmp_recx.rx_nr = full_recx->rx_idx - recx->rx_idx;
			D_ASSERTF(tmp_recx.rx_nr == (stripe_rec_nr -
					recx->rx_idx % stripe_rec_nr),
				  "bad recx\n");
			ec_data_recx_add(&tmp_recx, riod->iod_recxs, ridx,
					 tgt_recx_idxs, oca, true);
			ec_data_seg_add(&tmp_recx, iod_size, sgl, &iov_idx,
					&iov_off, oca, iovs, iov_nr, sorter,
					true);
		}
		ec_data_recx_add(full_recx, riod->iod_recxs, ridx,
				 tgt_recx_idxs, oca, false);
		ec_data_seg_add(full_recx, iod_size, sgl, &iov_idx, &iov_off,
				oca, iovs, iov_nr, sorter, false);
		recx_end = recx->rx_idx + recx->rx_nr;
		full_end = full_recx->rx_idx + full_recx->rx_nr;
		D_ASSERT(recx_end >= full_end);
		if (recx_end > full_end) {
			tmp_recx.rx_idx = full_end;
			tmp_recx.rx_nr = recx_end - full_end;
			ec_data_recx_add(&tmp_recx, riod->iod_recxs, ridx,
					 tgt_recx_idxs, oca, true);
			ec_data_seg_add(&tmp_recx, iod_size, sgl, &iov_idx,
					&iov_off, oca, iovs, iov_nr, sorter,
					true);
		}
	}

	if (update) {
		for (i = 0; i < ec_recx_array->oer_nr; i++) {
			full_ec_recx = &ec_recx_array->oer_recxs[i];
			full_recx = &full_ec_recx->oer_recx;
			ec_parity_recx_add(full_recx, riod->iod_recxs, ridx,
					   tgt_recx_idxs, oca);
		}
		ec_parity_seg_add(ec_recx_array, iod, oca, sorter);
	}

	if (!punch)
		obj_ec_seg_pack(sorter, rsgl);

	/* generate the oiod/siod */
	tgt_nr = update ? obj_ec_tgt_nr(oca) : obj_ec_data_tgt_nr(oca);
	for (i = 0, idx = 0, last = 0; i < tgt_nr; i++) {
		/* get each tgt's idx in the compact oiod_siods array */
		if (tgt_recx_nrs[i] != 0)
			tidx[i] = idx++;
		else
			tidx[i] = -1;
		for (j = last; j < tgt_recx_idxs[i] + tgt_recx_nrs[i]; j++) {
			if (riod->iod_recxs[j].rx_nr != 0)
				continue;
			/* being merged so left empty space */
			D_ASSERT(j != tgt_recx_idxs[i]);
			D_ASSERT(j < riod->iod_nr);
			for (k = j; k < tgt_recx_nrs[i] + tgt_recx_idxs[i]; k++)
				D_ASSERT(riod->iod_recxs[k].rx_nr == 0);
			empty_nr = tgt_recx_nrs[i] + tgt_recx_idxs[i] - j;
			for (k = j; k < riod->iod_nr - empty_nr; k++)
				riod->iod_recxs[k] =
					riod->iod_recxs[k + empty_nr];
			for (k = riod->iod_nr - empty_nr;
			     k < riod->iod_nr; k++) {
				riod->iod_recxs[k].rx_idx = 0;
				riod->iod_recxs[k].rx_nr = 0;
			}
			tgt_recx_nrs[i] -= empty_nr;
			for (k = i + 1; k < tgt_nr; k++)
				tgt_recx_idxs[k] -= empty_nr;
			riod->iod_nr -= empty_nr;
			break;
		}
		last = tgt_recx_idxs[i] + tgt_recx_nrs[i];
	}
	oiod->oiod_nr = idx;
	for (i = 0, rec_nr = 0, last = 0; i < tgt_nr; i++) {
		if (tgt_recx_nrs[i] == 0)
			continue;
		siod = &oiod->oiod_siods[tidx[i]];
		siod->siod_tgt_idx = i;
		siod->siod_idx = tgt_recx_idxs[i];
		siod->siod_nr = tgt_recx_nrs[i];
		siod->siod_off = rec_nr * iod_size;
		for (idx = last; idx < tgt_recx_idxs[i] + tgt_recx_nrs[i];
		     idx++) {
			rec_nr += riod->iod_recxs[idx].rx_nr;
		}
		last = tgt_recx_idxs[i] + tgt_recx_nrs[i];
	}

#if EC_DEBUG
	obj_reasb_req_dump(reasb_req, sgl, oca, stripe_rec_nr, iod_idx);
#endif

	if (iovs != NULL && iovs != iov_inline)
		D_FREE(iovs);
	return rc;
}

#define obj_ec_set_tgt(tgt_bitmap, idx, start, end)			\
	do {								\
		for (idx = start; idx <= end; idx++)			\
			setbit(tgt_bitmap, idx);			\
	} while (0)

static int
obj_ec_singv_req_reasb(daos_obj_id_t oid, daos_iod_t *iod, d_sg_list_t *sgl,
		       struct daos_oclass_attr *oca,
		       struct obj_reasb_req *reasb_req,
		       uint32_t iod_idx, bool update)
{
	struct obj_ec_recx_array	*ec_recx_array;
	uint8_t				*tgt_bitmap = reasb_req->tgt_bitmap;
	d_sg_list_t			*r_sgl;
	bool				 punch, singv_parity = false;
	uint64_t			 cell_bytes;
	uint32_t			 idx, tgt_nr;
	int				 rc = 0;

	ec_recx_array = &reasb_req->orr_recxs[iod_idx];
	punch = (update && iod->iod_size == DAOS_REC_ANY);

	ec_recx_array->oer_k = oca->u.ec.e_k;
	ec_recx_array->oer_p = oca->u.ec.e_p;
	if (obj_ec_singv_one_tgt(iod, sgl, oca)) {
		/* small singv stores on one target and replicates to all
		 * parity targets.
		 */
		idx = obj_ec_singv_small_idx(oca, iod);
		setbit(tgt_bitmap, idx);
		tgt_nr = 1;
		if (update) {
			tgt_nr += obj_ec_parity_tgt_nr(oca);
			obj_ec_set_tgt(tgt_bitmap, idx, obj_ec_data_tgt_nr(oca),
				       obj_ec_tgt_nr(oca) - 1);
		}
	} else {
		/* large singv evenly distributed to all data targets */
		if (update) {
			tgt_nr = obj_ec_tgt_nr(oca);
			obj_ec_set_tgt(tgt_bitmap, idx, 0,
				       obj_ec_tgt_nr(oca) - 1);
			if (!punch)
				singv_parity = true;
		} else {
			tgt_nr = obj_ec_data_tgt_nr(oca);
			obj_ec_set_tgt(tgt_bitmap, idx, 0,
				       obj_ec_data_tgt_nr(oca) - 1);
		}
	}

	reasb_req->orr_iods[iod_idx].iod_nr = 1;
	rc = obj_io_desc_init(&reasb_req->orr_oiods[iod_idx], tgt_nr,
			      OBJ_SIOD_SINGV);
	if (rc)
		goto out;

	r_sgl = &reasb_req->orr_sgls[iod_idx];
	if (singv_parity) {
		/* encode the EC parity for evenly distributed singv update */
		ec_recx_array->oer_stripe_total = 1;
		D_ASSERT(iod->iod_size != DAOS_REC_ANY);
		cell_bytes = obj_ec_singv_cell_bytes(iod->iod_size, oca);
		rc = obj_ec_pbufs_init(ec_recx_array, cell_bytes);
		if (rc)
			goto out;
		rc = obj_ec_recx_encode(oid, iod, sgl, oca, ec_recx_array);
		if (rc) {
			D_ERROR(DF_OID" obj_ec_recx_encode failed %d.\n",
				DP_OID(oid), rc);
			goto out;
		}
		/* reassemble the sgl */
		rc = daos_sgl_init(r_sgl,
				   sgl->sg_nr + obj_ec_parity_tgt_nr(oca));
		if (rc)
			goto out;
		memcpy(r_sgl->sg_iovs, sgl->sg_iovs,
		       sizeof(*sgl->sg_iovs) * sgl->sg_nr);
		for (idx = 0; idx < obj_ec_parity_tgt_nr(oca); idx++)
			d_iov_set(&r_sgl->sg_iovs[sgl->sg_nr + idx],
				  ec_recx_array->oer_pbufs[idx], cell_bytes);
	} else {
		/* copy the sgl */
		rc = daos_sgl_init(r_sgl, sgl->sg_nr);
		if (rc)
			goto out;
		memcpy(r_sgl->sg_iovs, sgl->sg_iovs,
		       sizeof(*sgl->sg_iovs) * sgl->sg_nr);
	}

#if EC_DEBUG
	obj_reasb_req_dump(reasb_req, sgl, oca, 0, iod_idx);
#endif

out:
	return rc;
}

int
obj_ec_req_reasb(daos_obj_rw_t *args, daos_obj_id_t oid,
		 struct daos_oclass_attr *oca, struct obj_reasb_req *reasb_req,
		 bool update)
{
	daos_iod_t		*iods;
	d_sg_list_t		*sgls;
	uint32_t		 iod_nr = args->nr;
	int			 i, rc = 0;

	iods = args->iods;
	sgls = args->sgls;
	for (i = 0; i < iod_nr; i++) {
		if (iods[i].iod_type == DAOS_IOD_SINGLE) {
			rc = obj_ec_singv_req_reasb(oid, &iods[i], &sgls[i],
						    oca, reasb_req, i, update);
			if (rc) {
				D_ERROR(DF_OID" singv_req_reasb failed %d.\n",
					DP_OID(oid), rc);
				goto out;
			}
			continue;
		}

		/* For array EC obj, scan/encode/reasb for each iod */
		rc = obj_ec_recx_scan(&iods[i], &sgls[i], oca, reasb_req, i,
				      update);
		if (rc) {
			D_ERROR(DF_OID" obj_ec_recx_scan failed %d.\n",
				DP_OID(oid), rc);
			goto out;
		}

		rc = obj_ec_recx_encode(oid, &iods[i], &sgls[i], oca,
					&reasb_req->orr_recxs[i]);
		if (rc) {
			D_ERROR(DF_OID" obj_ec_recx_encode failed %d.\n",
				DP_OID(oid), rc);
			goto out;
		}

		rc = obj_ec_recx_reasb(&iods[i], &sgls[i], oca, reasb_req, i,
				       update);
		if (rc) {
			D_ERROR(DF_OID" obj_ec_recx_reasb failed %d.\n",
				DP_OID(oid), rc);
			goto out;
		}
	}

	for (i = 0; i < obj_ec_tgt_nr(oca); i++) {
		if (isset(reasb_req->tgt_bitmap, i))
			reasb_req->orr_tgt_nr++;
	}

	if (!update) {
		reasb_req->tgt_oiods = obj_ec_tgt_oiod_init(
			reasb_req->orr_oiods, iod_nr, reasb_req->tgt_bitmap,
			obj_ec_tgt_nr(oca) - 1, reasb_req->orr_tgt_nr);
		if (reasb_req->tgt_oiods == NULL) {
			D_ERROR(DF_OID" obj_ec_tgt_oiod_init failed.\n",
				DP_OID(oid));
			rc = -DER_NOMEM;
			goto out;
		}
	}

out:
	return rc;
}

void
obj_ec_tgt_oiod_fini(struct obj_tgt_oiod *tgt_oiods)
{
	if (tgt_oiods == NULL)
		return;
	D_FREE(tgt_oiods[0].oto_offs);
	D_FREE(tgt_oiods);
}

struct obj_tgt_oiod *
obj_ec_tgt_oiod_get(struct obj_tgt_oiod *tgt_oiods, uint32_t tgt_nr,
		    uint32_t tgt_idx)
{
	struct obj_tgt_oiod	*tgt_oiod;
	uint32_t		 tgt;

	for (tgt = 0; tgt < tgt_nr; tgt++) {
		tgt_oiod = &tgt_oiods[tgt];
		if (tgt_oiod->oto_tgt_idx == tgt_idx)
			return tgt_oiod;
	}

	return NULL;
}

struct obj_tgt_oiod *
obj_ec_tgt_oiod_init(struct obj_io_desc *r_oiods, uint32_t iod_nr,
		     uint8_t *tgt_bitmap, uint32_t tgt_max_idx, uint32_t tgt_nr)
{
	struct obj_tgt_oiod	*tgt_oiod, *tgt_oiods;
	struct obj_io_desc	*oiod, *r_oiod;
	struct obj_shard_iod	*siod, *r_siod;
	void			*buf;
	uint8_t			*tmp_ptr;
	daos_size_t		 off_size, oiod_size, siod_size, item_size;
	uint32_t		 i, j, idx, tgt;

	D_ASSERT(tgt_nr > 0 && iod_nr > 0);

	D_ALLOC_ARRAY(tgt_oiods, tgt_nr);
	if (tgt_oiods == NULL)
		return NULL;

	off_size = sizeof(uint64_t);
	oiod_size = roundup(sizeof(struct obj_io_desc), 8);
	siod_size = roundup(sizeof(struct obj_shard_iod), 8);
	item_size = (off_size + oiod_size + siod_size) * iod_nr;
	D_ALLOC(buf, item_size * tgt_nr);
	if (buf == NULL) {
		D_FREE(tgt_oiods);
		return NULL;
	}

	for (i = 0, idx = 0; i < tgt_nr; i++, idx++) {
		while (isclr(tgt_bitmap, idx))
			idx++;
		D_ASSERT(idx <= tgt_max_idx);
		tgt_oiod = &tgt_oiods[i];
		tgt_oiod->oto_iod_nr = iod_nr;
		tgt_oiod->oto_tgt_idx = idx;
		tmp_ptr = buf + i * item_size;
		tgt_oiod->oto_offs = (void *)tmp_ptr;
		tmp_ptr += off_size * iod_nr;
		tgt_oiod->oto_oiods = (void *)tmp_ptr;
		tmp_ptr += oiod_size * iod_nr;
		for (j = 0; j < iod_nr; j++) {
			oiod = &tgt_oiod->oto_oiods[j];
			oiod->oiod_nr = 1;
			oiod->oiod_flags = OBJ_SIOD_PROC_ONE;
			siod = (void *)tmp_ptr;
			tmp_ptr += siod_size;
			siod->siod_tgt_idx = idx;
			siod->siod_nr = 0;
			oiod->oiod_siods = siod;
		}
	}

	/* traverse reassembled oiod and fill the tgt_oiod (per target oiod) */
	for (i = 0; i < iod_nr; i++) {
		r_oiod = &r_oiods[i];
		if (r_oiod->oiod_flags & OBJ_SIOD_SINGV) {
			for (j = 0; j < tgt_nr; j++) {
				tgt_oiod = &tgt_oiods[j];
				oiod = &tgt_oiod->oto_oiods[i];
				oiod->oiod_flags |= OBJ_SIOD_SINGV;
				oiod->oiod_nr = 0;
				oiod->oiod_siods = NULL;
			}
			continue;
		}
		for (j = 0; j < r_oiod->oiod_nr; j++) {
			r_siod = &r_oiod->oiod_siods[j];
			tgt = r_siod->siod_tgt_idx;
			tgt_oiod = obj_ec_tgt_oiod_get(tgt_oiods, tgt_nr, tgt);
			D_ASSERT(tgt_oiod->oto_tgt_idx == tgt);
			tgt_oiod->oto_offs[i] = r_siod->siod_off;
			siod = &tgt_oiod->oto_oiods[i].oiod_siods[0];
			D_ASSERT(siod->siod_tgt_idx == tgt);
			siod->siod_idx = r_siod->siod_idx;
			siod->siod_nr = r_siod->siod_nr;
			D_ASSERT(siod->siod_nr > 0);
		}
	}

	return tgt_oiods;
}

/* EC struct used to save state during encoding and to drive resource recovery.
 */
struct ec_params {
	daos_iod_t		*iods;	/* Replaces iod array in update.
					 * NULL except head of list
					 */
	d_sg_list_t		*sgls;	/* Replaces sgl array in update.
					 * NULL except head
					 */
	unsigned int		nr;	/* number of records in iods and sgls
					 * (same as update_t)
					 */
	daos_iod_t		niod;	/* replacement IOD for an input IOD
					 * that includes full stripe.
					 */
	d_sg_list_t		nsgl;	/* replacement SGL for an input IOD that
					 * includes full stripe.
					 */
	struct obj_ec_parity	p_segs;	/* Structure containing array of
					 * pointers to parity extents.
					 */
	struct ec_params        *next;	/* Pointer to next entry in list. */
};

struct ec_fetch_params {
	daos_iod_t		*iods;	/* Replaces iod array in fetch. */
	struct ec_fetch_params	*next;/* Next entry in list */
	daos_iod_t		 niod;
	unsigned int		 nr;	/* number of records in iods    */
};

static bool
ec_is_full_stripe(daos_iod_t *iod, struct daos_oclass_attr *oca,
		  unsigned int recx_idx)
{
	uint32_t	ss = oca->u.ec.e_k * oca->u.ec.e_len;
	uint64_t	start = iod->iod_recxs[recx_idx].rx_idx * iod->iod_size;
	uint64_t	length = iod->iod_recxs[recx_idx].rx_nr * iod->iod_size;
	uint64_t	so = ss - start % ss;

	if (length < ss && start/ss == (start+length)/ss) {
		return false;
	}
	if (start % ss)
		length -= so;

	if (length < ss)
		return false;
	return true;
}

/* Determines weather a given IOD contains a recx that is at least a full
 * stripe's worth of data.
 */
static bool
ec_has_full_or_mult_stripe(daos_iod_t *iod, struct daos_oclass_attr *oca,
			   uint64_t *tgt_set)
{
	unsigned int	ss = oca->u.ec.e_k * oca->u.ec.e_len;
	unsigned int	i;

	for (i = 0; i < iod->iod_nr; i++) {
		if (iod->iod_type == DAOS_IOD_ARRAY) {
			uint64_t start =
				iod->iod_recxs[i].rx_idx * iod->iod_size;
			uint64_t length =
				iod->iod_recxs[i].rx_nr * iod->iod_size;

			if (length < ss && start/ss == (start+length)/ss) {
				continue;
			} else if (start % ss) {
				uint64_t so = ss - start % ss;

				start += so;
				length -= so;
				if (length >= ss) {
					*tgt_set = ~0UL;
				}
			} else {
				*tgt_set = ~0UL;
			}
			return true;
		} else if (iod->iod_type == DAOS_IOD_SINGLE) {
			*tgt_set = ~0UL;
			return false;
		}
	}
	return false;
}

/* Initialize a param structure for an IOD--SGL pair. */
static void
ec_init_params(struct ec_params *params, daos_iod_t *iod, d_sg_list_t *sgl)
{
	memset(params, 0, sizeof(struct ec_params));
	params->niod            = *iod;
	params->niod.iod_recxs  = NULL;
	params->niod.iod_nr     = 0;
}

/* The head of the params list contains the replacement IOD and SGL arrays.
 * These are used only when stripes have been encoded for the update.
 *
 * Called for head of list only (for the first IOD in the input that contains
 * a full stripe.
 */
static int
ec_set_head_params(struct ec_params *head, daos_obj_update_t *args,
		   unsigned int cnt)
{
	unsigned int i;

	D_ALLOC_ARRAY(head->iods, args->nr);
	if (head->iods == NULL)
		return -DER_NOMEM;
	D_ALLOC_ARRAY(head->sgls, args->nr);
	if (head->sgls == NULL) {
		D_FREE(head->iods);
		return -DER_NOMEM;
	}
	for (i = 0; i < cnt; i++) {
		head->iods[i] = args->iods[i];
		head->sgls[i] = args->sgls[i];
		head->nr++;
	}
	return 0;
}

/* Moves the SGL "cursors" to the start of a full stripe */
static void
ec_move_sgl_cursors(d_sg_list_t *sgl, size_t size, unsigned int *sg_idx,
		 size_t *sg_off)
{
	if (size < sgl->sg_iovs[*sg_idx].iov_len - *sg_off) {
		*sg_off += size;
	} else {
		size_t buf_len = sgl->sg_iovs[*sg_idx].iov_len - *sg_off;

		for (*sg_off = 0; *sg_idx < sgl->sg_nr; (*sg_idx)++) {
			if (buf_len + sgl->sg_iovs[*sg_idx].iov_len > size) {
				*sg_off = size - buf_len;
				break;
			}
			buf_len += sgl->sg_iovs[*sg_idx].iov_len;
		}
	}
}

/* Allocates a stripe's worth of parity cells. */
static int
ec_allocate_parity(struct obj_ec_parity *par, unsigned int len, unsigned int p,
		unsigned int prior_cnt)
{
	unsigned char	**nbuf;
	unsigned int	i;
	int		rc = 0;

	D_REALLOC_ARRAY(nbuf, par->p_bufs, (prior_cnt + p));
	if (nbuf == NULL)
		return -DER_NOMEM;
	par->p_bufs = nbuf;

	for (i = prior_cnt; i < prior_cnt + p; i++) {
		D_ALLOC(par->p_bufs[i], len);
		if (par->p_bufs[i] == NULL)
			return -DER_NOMEM;
		par->p_nr++;
	}
	return rc;
}

/* Encode all of the full stripes contained within the recx at recx_idx.
 */
static int
ec_array_encode(struct ec_params *params, daos_obj_id_t oid, daos_iod_t *iod,
		d_sg_list_t *sgl, struct daos_oclass_attr *oca,
		int recx_idx, unsigned int *sg_idx, size_t *sg_off)
{
	uint64_t	 s_cur;
	unsigned int	 len = oca->u.ec.e_len;
	unsigned int	 k = oca->u.ec.e_k;
	daos_recx_t     *this_recx = &iod->iod_recxs[recx_idx];
	uint64_t	 ss = (uint64_t)len * k;
	uint64_t	 recx_start_offset = this_recx->rx_idx * iod->iod_size;
	uint64_t	 recx_end_offset = (this_recx->rx_nr * iod->iod_size) +
					   recx_start_offset;
	uint64_t	 so = recx_start_offset % ss ?
						ss - recx_start_offset % ss : 0;
	unsigned int	 p = oca->u.ec.e_p;
	unsigned int	 i;
	int		 rc = 0;

	/* This recx is not a full stripe, so move sgl cursors and return */
	if (!ec_is_full_stripe(iod, oca, recx_idx)) {
		ec_move_sgl_cursors(sgl, this_recx->rx_nr * iod->iod_size,
				    sg_idx, sg_off);
		return rc;
	}

	/* s_cur is the index (in bytes) into the recx where a full stripe
	 * begins.
	 */
	s_cur = recx_start_offset + so;

	if (s_cur != recx_start_offset)
		/* if the start of stripe is not at beginning of recx, move
		 * the sgl index to where the stripe begins).
		 */
		ec_move_sgl_cursors(sgl, so, sg_idx, sg_off);

	for ( ; s_cur + ss <= recx_end_offset; s_cur += ss) {
		daos_recx_t *nrecx;

		rc = ec_allocate_parity(&(params->p_segs), len, p,
					params->niod.iod_nr);
		if (rc != 0)
			return rc;
		rc = obj_encode_full_stripe(oid, sgl, sg_idx, sg_off,
					    &(params->p_segs),
					    params->niod.iod_nr);
		if (rc != 0)
			return rc;
		/* Parity is prepended to the recx array, so we have to add
		 * them here for each encoded stripe.
		 */
		D_REALLOC_ARRAY(nrecx, (params->niod.iod_recxs),
				(params->niod.iod_nr+p));
		if (nrecx == NULL)
			return -DER_NOMEM;
		params->niod.iod_recxs = nrecx;
		for (i = 0; i < p; i++) {
			params->niod.iod_recxs[params->niod.iod_nr].rx_idx =
			PARITY_INDICATOR | (s_cur+i*len)/params->niod.iod_size;
			params->niod.iod_recxs[params->niod.iod_nr++].rx_nr =
				len / params->niod.iod_size;
		}
	}
	if (s_cur - ss < recx_end_offset) {
		s_cur -= ss;
		ec_move_sgl_cursors(sgl, recx_end_offset-s_cur, sg_idx, sg_off);
	}
	return rc;
}

/* Updates the params instance for a IOD -- SGL pair.
 * The parity recxs have already been added, this function appends the
 * original recx entries.
 * The parity cells are placed first in the SGL, followed by the
 * input entries.
 */
static int
ec_update_params(struct ec_params *params, daos_iod_t *iod, d_sg_list_t *sgl,
		 struct daos_ec_attr ec_attr)
{
	daos_recx_t	*nrecx;			/*new recx */
	daos_iod_t	*niod = &params->niod;	/* new iod  */
	unsigned int	 len = ec_attr.e_len;
	unsigned short	 k = ec_attr.e_k;
	unsigned int	 ss = len * k;
	unsigned int	 i;
	int		 rc = 0;

	D_REALLOC_ARRAY(nrecx, (niod->iod_recxs), (niod->iod_nr + iod->iod_nr));
	if (nrecx == NULL)
		return -DER_NOMEM;
	niod->iod_recxs = nrecx;
	for (i = 0; i < iod->iod_nr; i++) {
		uint64_t rem = iod->iod_recxs[i].rx_nr * iod->iod_size;
		uint64_t start = iod->iod_recxs[i].rx_idx * iod->iod_size;
		uint64_t partial = start % ss ? ss - start % ss : 0;
		uint32_t stripe_cnt = 0;
		uint32_t partial_cnt = 0;

		if (partial && partial < rem) {
			D_REALLOC_ARRAY(nrecx,
					(niod->iod_recxs),
					(niod->iod_nr +
					iod->iod_nr + 1));
			if (nrecx == NULL) {
				D_FREE(niod->iod_recxs);
				return -DER_NOMEM;
			}
			niod->iod_recxs = nrecx;
			niod->iod_recxs[params->niod.iod_nr].rx_nr =
							partial/iod->iod_size;
			niod->iod_recxs[params->niod.iod_nr++].rx_idx =
							start/iod->iod_size;
			start += partial;
			rem -= partial;
			partial_cnt = 1;
		}

		stripe_cnt = rem / ss;

		if (rem % (len*k)) {
			stripe_cnt++;
		}
		/* can't have more than one stripe in a recx entry */
		if (stripe_cnt > 1) {
			D_REALLOC_ARRAY(nrecx,
					(niod->iod_recxs),
					(niod->iod_nr + iod->iod_nr +
					 partial_cnt + stripe_cnt - 1));
			if (nrecx == NULL) {
				D_FREE(niod->iod_recxs);
				return -DER_NOMEM;
			}
			niod->iod_recxs = nrecx;
		}
		D_ASSERT(rem > 0);
		while (rem) {
			if (rem <= ss) {
				niod->iod_recxs[params->niod.iod_nr].rx_nr =
					rem/iod->iod_size;
				niod->iod_recxs[params->niod.iod_nr++].rx_idx =
					start/iod->iod_size;
				rem = 0;
			} else {
				niod->iod_recxs[params->niod.iod_nr].rx_nr =
					ss/iod->iod_size;
				niod->iod_recxs[params->niod.iod_nr++].rx_idx =
					start/iod->iod_size;
				start += ss;
				rem -= ss;
			}
		}
	}

	D_ALLOC_ARRAY(params->nsgl.sg_iovs, (params->p_segs.p_nr + sgl->sg_nr));
	if (params->nsgl.sg_iovs == NULL)
		return -DER_NOMEM;
	for (i = 0; i < params->p_segs.p_nr; i++) {
		params->nsgl.sg_iovs[i].iov_buf = params->p_segs.p_bufs[i];
		params->nsgl.sg_iovs[i].iov_buf_len = len;
		params->nsgl.sg_iovs[i].iov_len = len;
		params->nsgl.sg_nr++;
	}
	for (i = 0; i < sgl->sg_nr; i++)
		params->nsgl.sg_iovs[params->nsgl.sg_nr++] = sgl->sg_iovs[i];

	return rc;
}

/* Recover EC allocated memory */
static void
ec_free_params(struct ec_params *head)
{
	D_FREE(head->iods);
	D_FREE(head->sgls);
	while (head != NULL) {
		int i;
		struct ec_params *current = head;

		D_FREE(current->niod.iod_recxs);
		D_FREE(current->nsgl.sg_iovs);
		for (i = 0; i < current->p_segs.p_nr; i++)
			D_FREE(current->p_segs.p_bufs[i]);
		D_FREE(current->p_segs.p_bufs);
		head = current->next;
		D_FREE(current);
	}
}

static void
ec_free_fetch_params(struct ec_fetch_params *head)
{
	D_FREE(head->iods);
	while (head != NULL) {
		struct ec_fetch_params *current = head;

		D_FREE(current->niod.iod_recxs);
		head = current->next;
		D_FREE(current);
	}
}

/* Call-back that recovers EC allocated memory  */
static int
ec_free_params_cb(tse_task_t *task, void *data)
{
	struct ec_params *head = *((struct ec_params **)data);
	int		  rc = task->dt_result;

	ec_free_params(head);
	return rc;
}

/* Call-back that recovers EC allocated memory for fetch  */
static int
ec_free_fetch_params_cb(tse_task_t *task, void *data)
{
	struct ec_fetch_params *head = *((struct ec_fetch_params **)data);
	int			rc = task->dt_result;

	ec_free_fetch_params(head);
	return rc;
}


/* Identifies the applicable subset of forwarding targets for non-full-stripe
 * EC updates. If called for EC fetch, the tgt_set is set to the addressed data
 * targets.
 *
 * For single values, the tgt_set includes the first data target, and all parity
 * targets for update. For fetch, the first data target is selected. (This will
 * change once encoding of single values is supported).
 */
void
ec_get_tgt_set(daos_iod_t *iods, unsigned int nr, struct daos_oclass_attr *oca,
	       bool parity_include, uint64_t *tgt_set)
{
	unsigned int    len = oca->u.ec.e_len;
	unsigned int    k = oca->u.ec.e_k;
	unsigned int    p_offset, p = oca->u.ec.e_p;
	uint64_t	ss;
	uint64_t	full;
	unsigned int	i, j;

	if (parity_include) {
		for (i = 0; i < p; i++)
			*tgt_set |= 1UL << i;
		full = (1UL << (k+p)) - 1;
	} else
		full = ((1UL << (k+p)) - 1) - ((1UL << p) - 1);

	for (i = 0; i < nr; i++) {
		if (iods->iod_type != DAOS_IOD_ARRAY) {
			*tgt_set |= 1UL << p;
			continue;
		}

		for (j = 0; j < iods[i].iod_nr; j++) {
			uint64_t ext_idx;
			uint64_t rs = iods[i].iod_recxs[j].rx_idx *
						iods[i].iod_size;
			uint64_t re = iods[i].iod_recxs[j].rx_nr *
						iods[i].iod_size + rs - 1;

			/* No partial-parity updates, so this function won't be
			 * called if parity is present in a update request.
			 * For fetch (!parity_include), the code allows parity
			 * segments to be requested (and handles them
			 * separately).
			 */
			if (PARITY_INDICATOR & rs) {
				/* This allows selecting a parity target
				 * for fetch. If combined with regualar
				 * data extents, parity ranges must come
				 * first in the the recx array.
				 */
				D_ASSERT(!parity_include);
				ss = (uint64_t)p * len;
				p_offset = 0;

			} else {
				ss = (uint64_t)k * len;
				p_offset = p;
			}
			/* Walk from start to end by len, except for the last
			 * iteration. (could cross a cell boundary with less
			 * than a cell's worth remaining).
			 */
			for (ext_idx = rs; ext_idx <= re;
			     ext_idx += (re - ext_idx < len && ext_idx != re) ?
			     re-ext_idx : len) {
				unsigned int cell = (ext_idx % ss)/len;

				*tgt_set |= 1UL << (cell+p_offset);
				if ((*tgt_set == full) && parity_include) {
					*tgt_set = 0;
					return;
				} else if (*tgt_set == full) {
					return;
				}

			}
		}
	}
}

static inline bool
ec_has_parity_cli(daos_iod_t *iod)
{
	return iod->iod_recxs[0].rx_idx & PARITY_INDICATOR;
}

static int
ec_set_head_fetch_params(struct ec_fetch_params *head, daos_iod_t *iods,
			 unsigned int nr, unsigned int cnt)
{
	unsigned int i;

	D_ALLOC_ARRAY(head->iods, nr);
	if (head->iods == NULL)
		return -DER_NOMEM;
	for (i = 0; i < cnt; i++) {
		head->iods[i] = iods[i];
		head->nr++;
	}
	return 0;
}

static int
ec_iod_stripe_cnt(daos_iod_t *iod, struct daos_ec_attr ec_attr)
{
	unsigned int	len = ec_attr.e_len;
	unsigned short	k = ec_attr.e_k;
	unsigned int	ss = len * k;
	unsigned int	i;
	unsigned int	total_stripe_cnt = 0;

	if (iod->iod_type == DAOS_IOD_SINGLE)
		return iod->iod_nr;

	for (i = 0; i < iod->iod_nr; i++) {
		uint64_t start = iod->iod_recxs[i].rx_idx * iod->iod_size;
		uint64_t rem = iod->iod_recxs[i].rx_nr * iod->iod_size;
		uint64_t partial = start % ss ? ss - start % ss : 0;
		uint32_t stripe_cnt = 0;

		if (partial && partial < rem) {
			rem -= partial;
			stripe_cnt++;
		}
		stripe_cnt += rem / ss;
		if (rem % ss) {
			stripe_cnt++;
		}
		total_stripe_cnt += stripe_cnt;
	}
	return total_stripe_cnt;
}

static int
ec_update_fetch_params(struct ec_fetch_params *params, daos_iod_t *iod,
		       struct daos_ec_attr ec_attr, int stripe_cnt)
{
	unsigned int	 len = ec_attr.e_len;
	unsigned short	 k = ec_attr.e_k;
	unsigned int	 ss = len * k;
	unsigned int	 i;
	int		 rc = 0;

	D_ALLOC_ARRAY(params->niod.iod_recxs, stripe_cnt);
	if (params->niod.iod_recxs == NULL)
		return -DER_NOMEM;
	for (i = 0; i < iod->iod_nr; i++) {
		uint64_t rem = iod->iod_recxs[i].rx_nr * iod->iod_size;
		uint64_t start = iod->iod_recxs[i].rx_idx * iod->iod_size;
		uint64_t partial = start % ss ? ss - start % ss : 0;

		if (partial && partial < rem) {
			params->niod.iod_recxs[params->niod.iod_nr].rx_nr
				= partial/iod->iod_size;
			params->niod.iod_recxs[params->niod.iod_nr++].rx_idx
				= start/iod->iod_size;
			start += partial;
			rem -= partial;
		}

		/* can't have more than one stripe in a recx entry */
		D_ASSERT(rem > 0);
		while (rem) {
			if (rem <= (uint64_t)len * k) {
				params->niod.iod_recxs[params->niod.iod_nr].
				rx_nr = rem/iod->iod_size;
				params->niod.iod_recxs[params->niod.iod_nr++].
				rx_idx = start/iod->iod_size;
				rem = 0;
			} else {
				params->niod.iod_recxs[params->niod.iod_nr].
				rx_nr = ss / iod->iod_size;
				params->niod.iod_recxs[params->niod.iod_nr++].
				rx_idx = start/iod->iod_size;
				start += ss;
				rem -= ss;
			}
		}
	}
	return rc;
}

int
ec_split_recxs(tse_task_t *task, struct daos_oclass_attr *oca)
{
	daos_obj_fetch_t	*args = dc_task_get_args(task);
	struct ec_fetch_params	*head = NULL;
	struct ec_fetch_params	*current = NULL;
	unsigned int		 i;
	int			 rc = 0;

	for (i = 0; i < args->nr; i++) {
		daos_iod_t	*iod = &args->iods[i];
		unsigned int	 stripe_cnt = ec_iod_stripe_cnt(iod, oca->u.ec);

		if (stripe_cnt > iod->iod_nr) {
			struct ec_fetch_params *params;

			D_ALLOC_PTR(params);
			if (params == NULL) {
				rc = -DER_NOMEM;
				break;
			}
			params->niod            = *iod;
			params->niod.iod_recxs  = NULL;
			params->niod.iod_nr     = 0;
			if (head == NULL) {
				head = params;
				current = head;
				rc = ec_set_head_fetch_params(head, args->iods,
							      args->nr, i);
				if (rc != 0)
					break;
			} else {
				current->next = params;
				current = params;
			}
			rc = ec_update_fetch_params(params, iod, oca->u.ec,
						    stripe_cnt);
				head->iods[i] = params->niod;
				D_ASSERT(head->nr == i);
				head->nr++;
		} else if (head != NULL) {
			head->iods[i] = *iod;
			D_ASSERT(head->nr == i);
			head->nr++;
		}
	}
	if (rc != 0 && head != NULL) {
		ec_free_fetch_params(head);
	} else if (head != NULL) {
		args->iods = head->iods;
		tse_task_register_comp_cb(task, ec_free_fetch_params_cb, &head,
					  sizeof(head));
	}
	return rc;
}


/* Iterates over the IODs in the update, encoding all full stripes contained
 * within each recx.
 */
int
ec_obj_update_encode(tse_task_t *task, daos_obj_id_t oid,
		     struct daos_oclass_attr *oca, uint64_t *tgt_set)
{
	daos_obj_update_t	*args = dc_task_get_args(task);
	struct ec_params	*head = NULL;
	struct ec_params	*current = NULL;
	unsigned int		 i, j;
	int			 rc = 0;

	for (i = 0; i < args->nr; i++) {
		d_sg_list_t	*sgl = &args->sgls[i];
		daos_iod_t	*iod = &args->iods[i];

		if (ec_has_full_or_mult_stripe(iod, oca, tgt_set)) {
			struct ec_params *params;

			if (ec_has_parity_cli(iod)) {
				/* retry of update, don't want to add parity
				 * again
				 */
				return rc;
			}
			D_ALLOC_PTR(params);
			if (params == NULL) {
				rc = -DER_NOMEM;
				break;
			}
			ec_init_params(params, iod, sgl);
			if (head == NULL) {
				head = params;
				current = head;
				rc = ec_set_head_params(head, args, i);
				if (rc != 0)
					break;
			} else {
				current->next = params;
				current = params;
			}
			if (args->iods[i].iod_type == DAOS_IOD_ARRAY) {
				unsigned int sg_idx = 0;
				size_t sg_off = 0;

				for (j = 0; j < iod->iod_nr; j++) {
					rc = ec_array_encode(params, oid, iod,
							     sgl, oca, j,
							     &sg_idx, &sg_off);
					if (rc != 0) {
						break;
					}
				}
				rc = ec_update_params(params, iod, sgl,
						      oca->u.ec);
				head->iods[i] = params->niod;
				head->sgls[i] = params->nsgl;
				D_ASSERT(head->nr == i);
				head->nr++;
			} else {
				D_ASSERT(iod->iod_type ==
					 DAOS_IOD_SINGLE);
				/* Encode single value */
			}
		} else if (head != NULL) {
		/* } else if (head != NULL && &(head->sgls[i]) != NULL) { */
			/* Add sgls[i] and iods[i] to head. Since we're
			 * adding ec parity (head != NULL) and thus need to
			 * replace the arrays in the update struct.
			 */
			D_ASSERT((&head->sgls[i]) != NULL);
			head->iods[i] = *iod;
			head->sgls[i] = *sgl;
			D_ASSERT(head->nr == i);
			head->nr++;
		}
	}

	if (*tgt_set != 0) {
		/* tgt_set == 0 means send to all forwarding targets
		 * from leader. If it's not zero here, it means that
		 * ec_object_update encoded a full stripe. Hence
		 * the update should go to all targets.
		 */
		*tgt_set = 0;
	} else if (head) {
		/* Called for updates with no full stripes.
		 * Builds a bit map only if forwarding targets are
		 * a proper subset. Sets tgt_set to zero if all targets
		 * are addressed.
		 */
		ec_get_tgt_set(head->iods, args->nr, oca, true, tgt_set);
	} else {
		ec_get_tgt_set(args->iods, args->nr, oca, true, tgt_set);
	}

	if (rc != 0 && head != NULL) {
		ec_free_params(head);
	} else if (head != NULL) {
		args->iods = head->iods;
		args->sgls = head->sgls;
		tse_task_register_comp_cb(task, ec_free_params_cb, &head,
					  sizeof(head));
	}
	return rc;
}

bool
ec_mult_data_targets(uint32_t fw_cnt, daos_obj_id_t oid)
{
	struct daos_oclass_attr *oca = daos_oclass_attr_find(oid);

	if (oca->ca_resil == DAOS_RES_EC && fw_cnt > oca->u.ec.e_p)
		return true;
	return false;
}
