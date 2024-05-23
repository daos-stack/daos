/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

	if (recxs->oer_pbufs[0] != NULL)
		D_FREE(recxs->oer_pbufs[0]);

	for (i = 0; i < recxs->oer_p; i++)
		recxs->oer_pbufs[i] = NULL;
}

void
obj_ec_recxs_fini(struct obj_ec_recx_array *recxs)
{
	if (recxs == NULL)
		return;

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
	if (sorter == NULL)
		return;

	D_FREE(sorter->ess_tgts);
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
			if ((idx > 0) &&
			    ((sgl->sg_iovs[idx - 1].iov_buf +
			      sgl->sg_iovs[idx - 1].iov_len) ==
			     seg->oes_iov.iov_buf)) {
				sgl->sg_iovs[idx - 1].iov_len +=
					seg->oes_iov.iov_len;
				sgl->sg_iovs[idx - 1].iov_buf_len =
					sgl->sg_iovs[idx - 1].iov_len;
			} else {
				sgl->sg_iovs[idx++] = seg->oes_iov;
			}
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

static int
obj_ec_recov_tgt_recx_nrs(struct dc_object *obj, struct obj_reasb_req *reasb_req,
			  uint64_t dkey_hash, uint32_t *tgt_recx_nrs)
{
	struct obj_ec_fail_info	*fail_info = reasb_req->orr_fail;
	struct daos_oclass_attr	*oca = reasb_req->orr_oca;
	uint32_t		tgt_nr = 0;
	uint32_t		tgt;
	int			i;
	int			rc = 0;

	D_ASSERT(fail_info != NULL);
	if (fail_info->efi_ntgts > obj_ec_parity_tgt_nr(oca)) {
		rc = -DER_DATA_LOSS;
		D_ERROR(DF_OID" efi_ntgts %d > parity_tgt_nr %d, "DF_RC"\n",
			DP_OID(reasb_req->orr_oid), fail_info->efi_ntgts,
			obj_ec_parity_tgt_nr(oca), DP_RC(rc));
		goto out;
	}

	for (i = 0, tgt = obj_ec_shard_idx(obj, dkey_hash, 0);
	     i < obj_ec_tgt_nr(oca); i++, tgt = (tgt + 1) % obj_ec_tgt_nr(oca)) {
		if (obj_ec_tgt_in_err(fail_info->efi_tgt_list, fail_info->efi_ntgts, tgt)) {
			D_DEBUG(DB_TRACE, "tgt %ui not available\n", tgt);
			continue;
		}
		tgt_recx_nrs[i]++;
		tgt_nr++;
		if (tgt_nr == obj_ec_data_tgt_nr(oca))
			break;
	}
	D_ASSERTF(tgt_nr == obj_ec_data_tgt_nr(oca), "%d != %d",
		  tgt_nr, obj_ec_data_tgt_nr(oca));

out:
	return rc;
}

/** scan the iod to find the full_stripe recxs and some help info */
static int
obj_ec_recx_scan(struct dc_object *obj, daos_iod_t *iod, d_sg_list_t *sgl,
		 uint64_t dkey_hash, struct obj_reasb_req *reasb_req,
		 uint32_t iod_idx, bool update)
{
	uint8_t				*tgt_bitmap = reasb_req->tgt_bitmap;
	struct obj_ec_recx_array	*ec_recx_array;
	struct obj_ec_recx		*ec_recx = NULL;
	struct daos_oclass_attr		*oca = obj_get_oca(obj);
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
	int				 i, j, idx, rc = 0;

	if (reasb_req->orr_size_fetched)
		return 0;

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
				if (!frag_seg_counted && sgl) {
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
			if (reasb_req->orr_recov)
				rc = obj_ec_recov_tgt_recx_nrs(obj, reasb_req, dkey_hash,
							       tgt_recx_nrs);
			else
				ec_data_tgt_recx_nrs(oca, tgt_recx_nrs, j);
			if (rc)
				goto out;
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
			if (!frag_seg_counted && sgl) {
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
			setbit(tgt_bitmap,
			       obj_ec_shard_idx(obj, dkey_hash, i));
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
	if (!punch && sgl != NULL) {
		rc = d_sgl_init(&reasb_req->orr_sgls[iod_idx],
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
	bool				 with_padding = false;
	int				 i, c_idx = 0;
	int				 rc = 0;

	if (iod->iod_type == DAOS_IOD_SINGLE)
		obj_ec_singv_local_sz(iod->iod_size, oca, k - 1, &loc, true);

	for (i = 0; i < k; i++) {
		c_data[i] = NULL;
		/* for singv the last data target may need padding of zero */
		if (i == k - 1) {
			len = cell_bytes - loc.esl_bytes_pad;
			D_ASSERT(len > 0 && len <= cell_bytes);
			with_padding = (loc.esl_bytes_pad > 0);
		}
		if (daos_iov_left(sgl, iov_idx, iov_off) >= len &&
		    !with_padding) {
			from = (unsigned char *)sgl->sg_iovs[iov_idx].iov_buf;
			data[i] = &from[iov_off];
			daos_sgl_move(sgl, iov_idx, iov_off, len);
		} else {
			uint64_t copied = 0;

			D_ALLOC(c_data[c_idx], cell_bytes);
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

int
obj_ec_encode_buf(daos_obj_id_t oid, struct daos_oclass_attr *oca, daos_size_t iod_size,
		  unsigned char *buffer, unsigned char *p_bufs[])
{
	daos_size_t		cell_bytes = obj_ec_cell_rec_nr(oca) * iod_size;
	unsigned int		k = obj_ec_data_tgt_nr(oca);
	unsigned int		p = obj_ec_parity_tgt_nr(oca);
	struct obj_ec_codec	*codec;
	unsigned char		*data[k];
	int			i;

	codec = obj_ec_codec_get(daos_obj_id2class(oid));
	D_ASSERT(codec != NULL);

	for (i = 0; i < p && p_bufs[i] == NULL; i++) {
		D_ALLOC(p_bufs[i], cell_bytes);
		if (p_bufs[i] == NULL)
			return -DER_NOMEM;
	}

	for (i = 0; i < k; i++)
		data[i] = buffer + i * cell_bytes;

	ec_encode_data((int)cell_bytes, k, p, codec->ec_gftbls, data, p_bufs);
	return 0;
}

static struct obj_ec_codec *
codec_get(struct obj_reasb_req *reasb_req, daos_obj_id_t oid)
{
	if (reasb_req->orr_codec != NULL)
		return reasb_req->orr_codec;

	reasb_req->orr_codec = obj_ec_codec_get(daos_obj_id2class(oid));
	if (reasb_req->orr_codec == NULL) {
		D_ERROR("failed to get ec codec, oid "DF_OID".\n", DP_OID(oid));
		return NULL;
	}
	return reasb_req->orr_codec;
}

/**
 * Encode the data in full stripe recx_array, the result parity stored in
 * struct obj_ec_recx_array::oer_pbufs.
 */
static int
obj_ec_recx_encode(struct obj_ec_codec *codec, struct daos_oclass_attr *oca,
		   daos_iod_t *iod, d_sg_list_t *sgl,
		   struct obj_ec_recx_array *recx_array)
{
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
	int			 rc = 0;

	if (recx_array->oer_stripe_total == 0)
		D_GOTO(out, rc = 0);
	if (iod->iod_size == DAOS_REC_ANY) /* punch case */
		D_GOTO(out, rc = 0);
	singv = (iod->iod_type == DAOS_IOD_SINGLE);
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
	obj_ec_idx_daos2vos(idx, stripe_rec_nr, cell_rec_nr)

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

	EC_TRACE("adding recx idx "DF_U64", nr "DF_U64", add_parity %d."
		"cell/stripe "DF_U64"/"DF_U64"\n", recx->rx_idx, recx->rx_nr,
		add_parity, cell_rec_nr, stripe_rec_nr);

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
 * \param[in]		iod_size	recorded size
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

	D_ASSERT(iod_size > 0);
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
	D_ASSERTF(iov_nr <= iov_capa, "%d > %d, iod_size "DF_U64"\n",
		  iov_nr, iov_capa, iod_size);
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
		start = obj_ec_idx_vos2daos(recx->rx_idx, stripe_rec_nr,
				obj_ec_cell_rec_nr(oca), tgt);
		D_PRINT(" ["DF_U64", "DF_U64"]", start, recx->rx_nr);
	} else {
		if (recx->rx_idx & PARITY_INDICATOR) {
			tmp_idx = recx->rx_idx & (~PARITY_INDICATOR);
			start = obj_ec_idx_vos2daos(tmp_idx, stripe_rec_nr,
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
	D_PRINT("iod, akey "DF_KEY", iod_size "DF_U64", iod_nr %d\n",
		DP_KEY(&iod->iod_name), iod->iod_size,
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

	D_PRINT("\noiod, oiod_nr %d, oiod_flags %#x\n",
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

static void
ec_recov_recx_seg_add(struct dc_object *obj, struct obj_reasb_req *reasb_req,
		      uint64_t dkey_hash, daos_recx_t *recx, daos_recx_t *r_recx,
		      uint32_t *r_idx, uint32_t *start_idx, daos_size_t iod_size,
		      d_sg_list_t *sgl, struct obj_ec_seg_sorter *sorter)
{
	struct obj_ec_fail_info	*fail_info = reasb_req->orr_fail;
	struct daos_oclass_attr	*oca = reasb_req->orr_oca;
	uint64_t		 stripe_rec_nr = obj_ec_stripe_rec_nr(oca);
	uint64_t		 cell_rec_nr = obj_ec_cell_rec_nr(oca);
	uint64_t		 stripe_total_sz, cell_sz, recx_idx, recx_nr;
	uint32_t		 i, tgt, tgt_nr, stripe_nr;
	void			*buf_sgl, *buf_stripe, *buf;
	d_iov_t			 iov;

	D_ASSERT(fail_info != NULL);
	D_ASSERT(sgl->sg_nr == 1);
	D_ASSERT(recx->rx_nr % stripe_rec_nr == 0);

	buf_sgl = sgl->sg_iovs[0].iov_buf;
	cell_sz = obj_ec_cell_rec_nr(oca) * iod_size;
	stripe_total_sz = cell_sz * obj_ec_tgt_nr(oca);
	stripe_nr = recx->rx_nr / stripe_rec_nr;
	recx_nr = recx->rx_nr / obj_ec_data_tgt_nr(oca);

	for (i = 0, tgt_nr = 0, tgt = obj_ec_shard_idx(obj, dkey_hash, 0);
	     i < obj_ec_tgt_nr(oca); i++, tgt = (tgt + 1) % obj_ec_tgt_nr(oca)) {
		int j;

		if (obj_ec_tgt_in_err(fail_info->efi_tgt_list, fail_info->efi_ntgts,
				      tgt))
			continue;

		recx_idx = ec_vos_idx(recx->rx_idx);
		if (i >= obj_ec_data_tgt_nr(oca))
			recx_idx |= PARITY_INDICATOR;
		ec_recx_add(r_recx, r_idx, start_idx, i, recx_idx, recx_nr);

		for (j = 0; j < stripe_nr; j++) {
			buf_stripe = buf_sgl + j * stripe_total_sz;
			buf = buf_stripe + i * cell_sz;
			d_iov_set(&iov, buf, cell_sz);
			obj_ec_seg_insert(sorter, i, &iov, 1);
		}

		tgt_nr++;
		if (tgt_nr == obj_ec_data_tgt_nr(oca))
			break;
	}
	D_ASSERT(tgt_nr == obj_ec_data_tgt_nr(oca));
}

#define EC_INLINE_IOVS		(16)
/**
 * Reassemble iod/sgl/recx for EC.
 * Input user \a iod, \a sgl, and \a recx_array,
 * Output reassembled \a riod, \a rsgl and \a oiod.
 */
static int
obj_ec_recx_reasb(struct dc_object *obj, daos_iod_t *iod, d_sg_list_t *sgl,
		  uint64_t dkey_hash, struct obj_reasb_req *reasb_req, uint32_t iod_idx,
		  bool update)
{
	struct obj_ec_recx_array	*ec_recx_array =
						&reasb_req->orr_recxs[iod_idx];
	daos_iod_t			*riod = &reasb_req->orr_iods[iod_idx];
	d_sg_list_t			*rsgl = &reasb_req->orr_sgls[iod_idx];
	struct obj_io_desc		*oiod = &reasb_req->orr_oiods[iod_idx];
	struct daos_oclass_attr		*oca = obj_get_oca(obj);
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
	struct obj_ec_recx		*full_ec_recx = NULL;
	uint32_t			 tidx[OBJ_EC_MAX_M] = {0};
	uint32_t			 ridx[OBJ_EC_MAX_M] = {0};
	d_iov_t				 iov_inline[EC_INLINE_IOVS];
	daos_recx_t			*recx, *full_recx, tmp_recx;
	d_iov_t				*iovs = NULL;
	uint32_t			 i, j, k, idx, last;
	uint32_t			 tgt_nr, empty_nr;
	uint32_t			 iov_idx = 0, iov_nr = 0;
	uint64_t			 iov_off = 0, recx_end, full_end;
	uint64_t			 rec_nr, iod_size = iod->iod_size;
	bool				 with_full_stripe;
	bool				 punch;
	int				 rc = 0;

	D_ASSERT(cell_rec_nr > 0);
	if (sgl != NULL) {
		iov_nr = sgl->sg_nr;
		if (iov_nr <= EC_INLINE_IOVS) {
			iovs = iov_inline;
		} else {
			D_ALLOC_ARRAY(iovs, iov_nr);
			if (iovs == NULL)
				return -DER_NOMEM;
		}
	}
	punch = (update && iod->iod_size == DAOS_REC_ANY);

	for (i = 0; i < iod->iod_nr; i++) {
		recx = &iod->iod_recxs[i];
		with_full_stripe = recx_with_full_stripe(i, ec_recx_array,
							 &full_ec_recx);
		if (punch || !with_full_stripe || !update) {
			if (reasb_req->orr_recov) {
				D_ASSERT(!update);
				D_ASSERT(iod->iod_nr == 1);
				ec_recov_recx_seg_add(obj, reasb_req, dkey_hash, recx,
						      riod->iod_recxs, ridx, tgt_recx_idxs,
						      iod_size, sgl, sorter);
				continue;
			}
			if (!reasb_req->orr_size_fetched)
				ec_data_recx_add(recx, riod->iod_recxs, ridx,
						 tgt_recx_idxs, oca, update);
			if (punch)
				continue;
			if (!reasb_req->orr_size_fetch) {
				/* After size query, server returns as zero
				 * iod_size (Empty tree or all holes, DAOS array
				 * API relies on zero iod_size to see if an
				 * array cell is empty). In this case, set
				 * iod_size as 1 to make sgl be splittable,
				 * server will not really transfer data back.
				 */
				if (iod_size == 0) {
					D_ASSERT(reasb_req->orr_size_fetched);
					iod_size = 1;
				}
				ec_data_seg_add(recx, iod_size, sgl, &iov_idx,
						&iov_off, oca, iovs, iov_nr,
						sorter, update);
			}
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
			ec_data_seg_add(&tmp_recx, iod_size,
					sgl, &iov_idx, &iov_off, oca,
					iovs, iov_nr, sorter, true);
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

	if (update && !punch) {
		for (i = 0; i < ec_recx_array->oer_nr; i++) {
			full_ec_recx = &ec_recx_array->oer_recxs[i];
			full_recx = &full_ec_recx->oer_recx;
			ec_parity_recx_add(full_recx, riod->iod_recxs, ridx,
					   tgt_recx_idxs, oca);
		}
		ec_parity_seg_add(ec_recx_array, iod, oca, sorter);
	}

	if (!punch && !reasb_req->orr_size_fetch)
		obj_ec_seg_pack(sorter, rsgl);

	/* generate the oiod/siod */
	tgt_nr = (update || reasb_req->orr_recov == 1) ?
		 obj_ec_tgt_nr(oca) : obj_ec_data_tgt_nr(oca);
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
		siod->siod_tgt_idx = obj_ec_shard_idx(obj, dkey_hash, i);
		siod->siod_idx = tgt_recx_idxs[i];
		siod->siod_nr = tgt_recx_nrs[i];
		EC_TRACE("i %d tgt %u idx %u nr %u, start "DF_U64
			" tgt_recx %u/%u\n", i, siod->siod_tgt_idx, siod->siod_idx,
			siod->siod_nr, obj_ec_shard_idx(obj, dkey_hash, 0),
			tgt_recx_idxs[i], tgt_recx_nrs[i]);
		siod->siod_off = rec_nr * iod_size;
		for (idx = last; idx < tgt_recx_idxs[i] + tgt_recx_nrs[i]; idx++)
			rec_nr += riod->iod_recxs[idx].rx_nr;
		last = tgt_recx_idxs[i] + tgt_recx_nrs[i];
	}

#if EC_DEBUG
	obj_reasb_req_dump(reasb_req, sgl, oca, stripe_rec_nr, iod_idx);
#endif

	if (iovs != NULL && iovs != iov_inline)
		D_FREE(iovs);
	return rc;
}

/* Get one parity idx within the group, but skip the err list & current existing bitmap.*/
int
obj_ec_fail_info_parity_get(struct dc_object *obj, struct obj_reasb_req *reasb_req,
			    uint64_t dkey_hash, uint32_t *parity_tgt_idx, uint8_t *cur_bitmap)
{
	uint16_t		 p = obj_ec_parity_tgt_nr(reasb_req->orr_oca);
	uint16_t		 grp_size = obj_ec_tgt_nr(reasb_req->orr_oca);
	struct obj_ec_fail_info *fail_info = reasb_req->orr_fail;
	uint32_t		*err_list = NULL;
	uint32_t		 nerrs = 0;
	uint32_t		 parity_start;
	int			 i;

	parity_start = obj_ec_parity_start(obj, dkey_hash);
	if (fail_info == NULL) {
		*parity_tgt_idx = parity_start;
		return 0;
	}

	err_list = fail_info->efi_tgt_list;
	nerrs = fail_info->efi_ntgts;
	for (i = 0; i < p; i++) {
		uint32_t parity = (parity_start + i) % grp_size;

		if (!obj_ec_tgt_in_err(err_list, nerrs, parity) &&
		    (cur_bitmap == NIL_BITMAP || isclr(cur_bitmap, parity))) {
			*parity_tgt_idx = parity;
			break;
		}
	}

	if (nerrs > p || i == p) {
		D_ERROR(DF_OID" %d failure, not recoverable.\n",
			DP_OID(reasb_req->orr_oid), nerrs);
		for (i = 0; i < nerrs; i++)
			D_ERROR("fail tgt: %u\n", err_list[i]);

		return -DER_DATA_LOSS;
	}

	return 0;
}

/* Insert fail_tgt into the fail_info list, return DATA_LOSS if fail tgts are
 * more than parity targets.
 **/
int
obj_ec_fail_info_insert(struct obj_reasb_req *reasb_req, uint16_t fail_tgt)
{
	uint16_t		grp_size = obj_ec_tgt_nr(reasb_req->orr_oca);
	struct obj_ec_fail_info	*fail_info;
	uint32_t		*err_list;
	uint32_t		nerrs;
	int			i;

	D_ASSERT(fail_tgt < grp_size);
	fail_info = obj_ec_fail_info_get(reasb_req, true, grp_size);
	if (fail_info == NULL)
		return -DER_NOMEM;

	if (obj_ec_tgt_in_err(fail_info->efi_tgt_list, fail_info->efi_ntgts, fail_tgt))
		return 0;

	err_list = fail_info->efi_tgt_list;
	nerrs = fail_info->efi_ntgts;
	err_list[nerrs] = fail_tgt;
	fail_info->efi_ntgts++;
	D_DEBUG(DB_IO, DF_OID" insert fail_tgt %u fail num %u\n", DP_OID(reasb_req->orr_oid),
		fail_tgt, fail_info->efi_ntgts);
	if (fail_info->efi_ntgts > obj_ec_parity_tgt_nr(reasb_req->orr_oca)) {
		D_ERROR(DF_OID" %d failure, not recoverable.\n", DP_OID(reasb_req->orr_oid),
			fail_info->efi_ntgts);
		for (i = 0; i <= nerrs; i++)
			D_ERROR("fail tgt: %u\n", err_list[i]);

		return -DER_DATA_LOSS;
	}

	return 0;
}

int
obj_ec_singv_split(daos_unit_oid_t oid, uint16_t layout_ver, struct daos_oclass_attr *oca,
		   uint64_t dkey_hash, daos_size_t iod_size, d_sg_list_t *sgl)
{
	uint64_t c_bytes = obj_ec_singv_cell_bytes(iod_size, oca);
	uint32_t tgt_off = obj_ec_shard_off_by_layout_ver(layout_ver, dkey_hash, oca, oid.id_shard);
	char	*data = sgl->sg_iovs[0].iov_buf;

	D_ASSERT(iod_size != DAOS_REC_ANY);
	if (tgt_off > 0)
		memmove(data, data + tgt_off * c_bytes, c_bytes);

	sgl->sg_iovs[0].iov_len = c_bytes;
	return 0;
}

static int
obj_ec_singv_encode(struct obj_ec_codec *codec, struct daos_oclass_attr *oca,
		    daos_iod_t *iod, d_sg_list_t *sgl,
		    struct obj_ec_recx_array *recxs)
{
	uint64_t c_bytes;
	int	rc;

	D_ASSERT(iod->iod_size != DAOS_REC_ANY);
	c_bytes = obj_ec_singv_cell_bytes(iod->iod_size, oca);
	rc = obj_ec_pbufs_init(recxs, c_bytes);
	if (rc)
		D_GOTO(out, rc);

	rc = obj_ec_recx_encode(codec, oca, iod, sgl, recxs);
	if (rc) {
		D_ERROR("obj_ec_recx_encode failed %d.\n", rc);
		D_GOTO(out, rc);
	}
out:
	return rc;
}

int
obj_ec_singv_encode_buf(daos_unit_oid_t oid, uint16_t layout_ver, struct daos_oclass_attr *oca,
			uint64_t dkey_hash, daos_iod_t *iod, d_sg_list_t *sgl,
			d_iov_t *e_iov)
{
	struct obj_ec_recx_array recxs = { 0 };
	struct obj_ec_codec *codec;
	int p_tgt_off; /* parity shard */
	int idx;
	int rc;

	/* calculated the parity */
	rc = obj_ec_recxs_init(&recxs, 1);
	if (rc)
		return rc;

	recxs.oer_k = obj_ec_data_tgt_nr(oca);
	recxs.oer_p = obj_ec_parity_tgt_nr(oca);
	recxs.oer_stripe_total = 1;

	codec = obj_ec_codec_get(daos_obj_id2class(oid.id_pub));
	rc = obj_ec_singv_encode(codec, oca, iod, sgl, &recxs);
	if (rc)
		D_GOTO(out, rc);

	p_tgt_off = obj_ec_shard_off_by_layout_ver(layout_ver, dkey_hash, oca, oid.id_shard);
	D_ASSERT(p_tgt_off >= obj_ec_data_tgt_nr(oca));
	idx = p_tgt_off - obj_ec_data_tgt_nr(oca);
	D_ASSERT(e_iov->iov_buf_len >=
		 obj_ec_singv_cell_bytes(iod->iod_size, oca));
	e_iov->iov_len = obj_ec_singv_cell_bytes(iod->iod_size, oca);
	memcpy(e_iov->iov_buf, recxs.oer_pbufs[idx], e_iov->iov_len);
out:
	obj_ec_recxs_fini(&recxs);
	return rc;
}

#define obj_ec_set_all_bitmaps(tgt_bitmap, oca)				\
	do {								\
		int i;							\
		for (i = 0; i <= obj_ec_tgt_nr(oca); i++)		\
			setbit(tgt_bitmap, i);				\
	} while (0)

#define obj_ec_set_data_bitmaps(tgt_bitmap, dkey_hash, obj)		\
	do {								\
		int data_idx = obj_ec_shard_idx(obj, dkey_hash, 0);	\
		int i;							\
										\
		for (idx = data_idx, i = 0; i < obj_ec_data_tgt_nr(&obj->cob_oca); \
		     i++, idx = (idx + 1) % obj_ec_tgt_nr(&obj->cob_oca))	\
			setbit(tgt_bitmap, idx);				\
	} while (0)

#define obj_ec_set_parity_bitmaps(tgt_bitmap, dkey_hash, obj)		\
	do {								\
		int parity_idx = obj_ec_parity_start(obj, dkey_hash);	\
		int i;							\
										\
		for (idx = parity_idx, i = 0; i < obj_ec_parity_tgt_nr(&obj->cob_oca);	\
		     i++, idx = (idx + 1) % obj_ec_tgt_nr(oca))			\
			setbit(tgt_bitmap, idx);				\
	} while (0)

static int
obj_ec_singv_req_reasb(struct dc_object *obj, uint64_t dkey_hash, daos_iod_t *iod, d_sg_list_t *sgl,
		       struct obj_reasb_req *reasb_req, uint32_t iod_idx, bool update)
{
	struct obj_ec_recx_array	*ec_recx_array;
	uint8_t				*tgt_bitmap = reasb_req->tgt_bitmap;
	struct daos_oclass_attr		*oca = obj_get_oca(obj);
	d_sg_list_t			*r_sgl;
	bool				 punch, singv_parity = false;
	uint64_t			 cell_bytes;
	uint32_t			 idx = 0, tgt_nr;
	int				 rc = 0;

	if (reasb_req->orr_size_fetched)
		return 0;

	ec_recx_array = &reasb_req->orr_recxs[iod_idx];
	punch = (update && iod->iod_size == DAOS_REC_ANY);

	ec_recx_array->oer_k = oca->u.ec.e_k;
	ec_recx_array->oer_p = oca->u.ec.e_p;
	if (obj_ec_singv_one_tgt(iod->iod_size, sgl, oca)) {
		/* small singv stores on one target and replicates to all
		 * parity targets.
		 */
		if (reasb_req->orr_recov) {
			rc = obj_ec_fail_info_parity_get(obj, reasb_req, dkey_hash, &idx,
							 NIL_BITMAP);
			if (rc) {
				D_ERROR(DF_OID " can not get parity failed, " DF_RC "\n",
					DP_OID(reasb_req->orr_oid), DP_RC(rc));
				goto out;
			}
		} else {
			idx = obj_ec_singv_small_idx(obj, dkey_hash, iod);
		}
		setbit(tgt_bitmap, idx);
		tgt_nr = 1;
		if (update) {
			obj_ec_set_parity_bitmaps(tgt_bitmap, dkey_hash, obj);
			tgt_nr += obj_ec_parity_tgt_nr(oca);
		}
	} else {
		struct dcs_layout	*singv_lo;

		singv_lo = &reasb_req->orr_singv_los[iod_idx];
		singv_lo->cs_even_dist = 1;
		if (iod->iod_size != DAOS_REC_ANY)
			singv_lo->cs_bytes = obj_ec_singv_cell_bytes(iod->iod_size, oca);

		/* large singv evenly distributed to all data targets */
		if (update) {
			tgt_nr = obj_ec_tgt_nr(oca);
			singv_lo->cs_nr = tgt_nr;
			obj_ec_set_all_bitmaps(tgt_bitmap, oca);
			if (!punch)
				singv_parity = true;
		} else {
			if (reasb_req->orr_recov) {
				struct obj_ec_fail_info	*fail_info = reasb_req->orr_fail;
				int	i;

				if (fail_info->efi_ntgts > obj_ec_parity_tgt_nr(oca)) {
					rc = -DER_DATA_LOSS;
					D_ERROR(DF_OID" efi_ntgts %d > parity_tgt_nr %d, "DF_RC"\n",
						DP_OID(reasb_req->orr_oid), fail_info->efi_ntgts,
						obj_ec_parity_tgt_nr(oca), DP_RC(rc));
					goto out;
				}

				tgt_nr = 0;
				for (i = 0, idx = obj_ec_shard_idx(obj, dkey_hash, 0);
				     i < obj_ec_tgt_nr(oca);
				     idx = (idx + 1) % obj_ec_tgt_nr(oca), i++) {
					if (obj_ec_tgt_in_err(fail_info->efi_tgt_list,
							      fail_info->efi_ntgts, idx))
						continue;
					setbit(tgt_bitmap, idx);
					tgt_nr++;
					if (tgt_nr == obj_ec_data_tgt_nr(oca))
						break;
				}
			} else {
				tgt_nr = obj_ec_data_tgt_nr(oca);
				obj_ec_set_data_bitmaps(tgt_bitmap, dkey_hash, obj);
			}
			singv_lo->cs_nr = tgt_nr;
		}
	}

	reasb_req->orr_iods[iod_idx].iod_nr = 1;
	rc = obj_io_desc_init(&reasb_req->orr_oiods[iod_idx], tgt_nr,
			      OBJ_SIOD_SINGV);
	if (rc)
		goto out;

	r_sgl = &reasb_req->orr_sgls[iod_idx];
	if (singv_parity) {
		uint32_t	iov_nr = 0, iov_idx = 0, iov_off = 0;
		struct obj_ec_codec *codec;

		/* encode the EC parity for evenly distributed singv update */
		ec_recx_array->oer_stripe_total = 1;
		codec = codec_get(reasb_req, obj->cob_md.omd_id);
		if (codec == NULL) {
			D_ERROR(DF_OID" can not get codec.\n", DP_OID(obj->cob_md.omd_id));
			D_GOTO(out, rc = -DER_INVAL);
		}

		rc = obj_ec_singv_encode(codec, oca, iod, sgl, ec_recx_array);
		if (rc != 0)
			D_GOTO(out, rc);

		cell_bytes = obj_ec_singv_cell_bytes(iod->iod_size, oca);
		/* reassemble the sgl */
		rc = d_sgl_init(r_sgl, sgl->sg_nr + obj_ec_parity_tgt_nr(oca));
		if (rc)
			goto out;

		/* take singv size as input sgl possibly with more buffer */
		daos_sgl_consume(sgl, iov_idx, iov_off, iod->iod_size,
				 r_sgl->sg_iovs, iov_nr);
		D_ASSERT(iov_nr > 0 && iov_nr <= sgl->sg_nr);
		for (idx = 0; idx < obj_ec_parity_tgt_nr(oca); idx++)
			d_iov_set(&r_sgl->sg_iovs[iov_nr + idx],
				  ec_recx_array->oer_pbufs[idx], cell_bytes);
		r_sgl->sg_nr = iov_nr + obj_ec_parity_tgt_nr(oca);
	} else {
		if (sgl != NULL) {
			/* copy the sgl */
			rc = d_sgl_init(r_sgl, sgl->sg_nr);
			if (rc)
				goto out;
			memcpy(r_sgl->sg_iovs, sgl->sg_iovs,
			       sizeof(*sgl->sg_iovs) * sgl->sg_nr);
		} else {
			r_sgl->sg_iovs = NULL;
			r_sgl->sg_nr = 0;
		}
	}

#if EC_DEBUG
	obj_reasb_req_dump(reasb_req, sgl, oca, 0, iod_idx);
#endif

out:
	return rc;
}

int
obj_ec_encode(struct obj_reasb_req *reasb_req)
{
	struct obj_ec_codec *codec;
	uint32_t	i;
	int		rc;

	if (reasb_req->orr_usgls == NULL) /* punch case */
		return 0;

	codec = codec_get(reasb_req, reasb_req->orr_oid);
	if (codec == NULL) {
		D_ERROR(DF_OID" can not get codec.\n",
			DP_OID(reasb_req->orr_oid));
		return -DER_INVAL;
	}

	for (i = 0; i < reasb_req->orr_iod_nr; i++) {
		rc = obj_ec_recx_encode(codec,
					reasb_req->orr_oca,
					&reasb_req->orr_uiods[i],
					&reasb_req->orr_usgls[i],
					&reasb_req->orr_recxs[i]);
		if (rc) {
			D_ERROR(DF_OID" obj_ec_recx_encode failed %d.\n",
				DP_OID(reasb_req->orr_oid), rc);
			return rc;
		}
	}

	return 0;
}

int
obj_ec_req_reasb(struct dc_object *obj, daos_iod_t *iods, uint64_t dkey_hash, d_sg_list_t *sgls,
		 struct obj_reasb_req *reasb_req, uint32_t iod_nr, bool update)
{
	bool	singv_only = true;
	int	i, j, rc = 0;
	int	data_tgt_nr = 0;

	reasb_req->orr_oid = obj->cob_md.omd_id;
	reasb_req->orr_iod_nr = iod_nr;
	if (!reasb_req->orr_size_fetch) {
		reasb_req->orr_uiods = iods;
		reasb_req->orr_usgls = sgls;
	}

	/* If any array iod with unknown rec_size, firstly send a size_fetch
	 * request to server to query it, and then retry the IO request to do
	 * the real fetch. If only with single-value, need not size_fetch ahead.
	 */
	if (reasb_req->orr_size_fetched) {
		reasb_req->orr_size_fetch = 0;
		iods = reasb_req->orr_uiods;
	} else if (!update) {
		for (i = 0; i < iod_nr; i++) {
			if (iods[i].iod_size == DAOS_REC_ANY)
				reasb_req->orr_size_fetch = 1;
			if (iods[i].iod_type == DAOS_IOD_ARRAY)
				singv_only = false;
		}
		/* if only with single-value, need not size_fetch */
		if (singv_only && sgls != NULL)
			reasb_req->orr_size_fetch = 0;
	}

	for (i = 0; i < iod_nr; i++) {
		if (iods[i].iod_type == DAOS_IOD_SINGLE) {
			rc = obj_ec_singv_req_reasb(obj, dkey_hash, &iods[i],
						    sgls ? &sgls[i] : NULL,
						    reasb_req, i, update);
			if (rc) {
				D_ERROR(DF_OID" singv_req_reasb failed %d.\n",
					DP_OID(obj->cob_md.omd_id), rc);
				goto out;
			}
			continue;
		}

		singv_only = false;
		/* For array EC obj, scan/encode/reasb for each iod */
		rc = obj_ec_recx_scan(obj, &iods[i], sgls ? &sgls[i] : NULL,
				      dkey_hash, reasb_req, i, update);
		if (rc) {
			D_ERROR(DF_OID" obj_ec_recx_scan failed %d.\n",
				DP_OID(obj->cob_md.omd_id), rc);
			goto out;
		}

		rc = obj_ec_recx_reasb(obj, &iods[i], sgls ? &sgls[i] : NULL,
				       dkey_hash, reasb_req, i, update);
		if (rc) {
			D_ERROR(DF_OID" obj_ec_recx_reasb failed %d.\n",
				DP_OID(obj->cob_md.omd_id), rc);
			goto out;
		}
	}

	for (i = 0; !reasb_req->orr_size_fetched && i < obj_ec_tgt_nr(obj_get_oca(obj)); i++) {
		if (isset(reasb_req->tgt_bitmap, i)) {
			reasb_req->orr_tgt_nr++;
			if (is_ec_data_shard(obj, dkey_hash, i) || reasb_req->orr_recov)
				data_tgt_nr++;
		}
	}

	if (data_tgt_nr == 1) {
		struct obj_io_desc	*oiod;
		struct obj_shard_iod	*siod;

		/* if with single data target, zero the offset as each target start from same sgl
		 * (user original input sgl).
		 */
		for (i = 0; i < iod_nr; i++) {
			oiod = &reasb_req->orr_oiods[i];
			if (oiod->oiod_siods == NULL)
				continue;
			for (j = 0; j < oiod->oiod_nr; j++) {
				siod = &oiod->oiod_siods[j];
				siod->siod_off = 0;
			}
		}
	}
	reasb_req->orr_single_tgt = data_tgt_nr == 1;
	reasb_req->orr_singv_only = singv_only;
	rc = obj_ec_encode(reasb_req);
	if (rc) {
		D_ERROR(DF_OID" obj_ec_encode failed %d.\n", DP_OID(obj->cob_md.omd_id), rc);
		goto out;
	}

	if (!update) {
		uint32_t	start_tgt;

		if (reasb_req->tgt_oiods != NULL) {
			/* re-init the tgt_oiods to re-calculate the oto_offs
			 * after iod_size known.
			 */
			D_ASSERT(reasb_req->orr_size_fetched);
			obj_ec_tgt_oiod_fini(reasb_req->tgt_oiods);
			reasb_req->tgt_oiods = NULL;
		}

		start_tgt = obj_ec_shard_idx(obj, dkey_hash, 0);
		reasb_req->tgt_oiods =
			obj_ec_tgt_oiod_init(reasb_req->orr_oiods, iod_nr, reasb_req->tgt_bitmap,
					     obj_ec_tgt_nr(obj_get_oca(obj)) - 1,
					     reasb_req->orr_tgt_nr, start_tgt, obj_get_oca(obj));
		if (reasb_req->tgt_oiods == NULL) {
			D_ERROR(DF_OID" obj_ec_tgt_oiod_init failed.\n",
				DP_OID(obj->cob_md.omd_id));
			rc = -DER_NOMEM;
			goto out;
		}
	}

out:
	return rc;
}

void
obj_ec_update_iod_size(struct obj_reasb_req *reasb_req, uint32_t iod_nr)
{
	daos_iod_t		*u_iods = reasb_req->orr_uiods;
	daos_iod_t		*re_iods = reasb_req->orr_iods;
	struct obj_ec_fail_info *fail_info = reasb_req->orr_fail;
	int i;

	if (re_iods == NULL || u_iods == re_iods)
		return;

	for (i = 0; i < iod_nr; i++) {
		D_ASSERT(re_iods[i].iod_type == u_iods[i].iod_type);
		u_iods[i].iod_size = re_iods[i].iod_size;
	}

	/* Set back the size if it is recovery task */
	if (unlikely(fail_info != NULL)) {
		for (i = 0; i < fail_info->efi_recov_ntasks; i++) {
			if (fail_info->efi_recov_tasks[i].ert_iod.iod_type !=
							DAOS_IOD_SINGLE)
				continue;
			fail_info->efi_recov_tasks[i].ert_oiod->iod_size =
				fail_info->efi_recov_tasks[i].ert_iod.iod_size;
		}
	}
}

static daos_size_t
obj_ec_recx_size(daos_iod_t *iod, struct obj_shard_iod *siod,
		 daos_size_t iod_size)
{
	uint32_t	 i;
	daos_size_t	 size = 0;

	for (i = 0; i < siod->siod_nr; i++)
		size += iod->iod_recxs[siod->siod_idx + i].rx_nr;

	return size * iod_size;
}

void
obj_ec_fetch_set_sgl(struct dc_object *obj, struct obj_reasb_req *reasb_req,
		     uint64_t dkey_hash, uint32_t iod_nr)
{
	daos_iod_t			*uiods, *uiod, *riods, *riod;
	d_sg_list_t			*usgls, *usgl;
	daos_recx_t			*recx;
	struct daos_oclass_attr		*oca = reasb_req->orr_oca;
	struct obj_tgt_oiod		*toiod;
	struct obj_io_desc		*oiod;
	daos_size_t			 data_size, recx_size;
	daos_size_t			 size_in_iod, tail_hole_size;
	uint64_t			 stripe_rec_nr;
	uint64_t			 cell_rec_nr;
	int				 last_tgt, start, end, i, j;
	bool				 recheck;

	stripe_rec_nr = obj_ec_stripe_rec_nr(oca);
	cell_rec_nr = obj_ec_cell_rec_nr(oca);
	uiods = reasb_req->orr_uiods;
	usgls = reasb_req->orr_usgls;
	riods = reasb_req->orr_iods;
	for (i = 0; i < iod_nr; i++) {
		uiod = &uiods[i];
		riod = &riods[i];
		usgl = &usgls[i];
		usgl->sg_nr_out = 0;
		tail_hole_size = 0;
		size_in_iod =  daos_iods_len(uiod, 1);
		if (uiod->iod_size == 0)
			continue;
		if (uiod->iod_type == DAOS_IOD_SINGLE) {
			dc_sgl_out_set(usgl, uiod->iod_size);
			continue;
		}
		recx = &uiod->iod_recxs[uiod->iod_nr - 1];
		last_tgt = obj_ec_tgt_of_recx_idx(
				recx->rx_idx + recx->rx_nr - 1,
				stripe_rec_nr, cell_rec_nr);
		D_ASSERT(last_tgt < obj_ec_tgt_nr(oca));
		start = last_tgt;
		end = 0;
		recheck = false;
tgt_check:
		for (j = start; j >= end; j--) {
			uint32_t tgt_idx = obj_ec_shard_idx(obj, dkey_hash, j);

			toiod = obj_ec_tgt_oiod_get(reasb_req->tgt_oiods,
						    reasb_req->orr_tgt_nr, tgt_idx);
			if (toiod == NULL)
				continue;

			D_ASSERT(iod_nr == toiod->oto_iod_nr);
			oiod = &toiod->oto_oiods[i];
			recx_size = obj_ec_recx_size(riod, oiod->oiod_siods,
						     uiod->iod_size);
			data_size = *(reasb_req->orr_data_sizes +
				      toiod->oto_orig_tgt_idx * iod_nr + i);
			if (data_size == 0) {
				tail_hole_size += recx_size;
				continue;
			}

			D_ASSERT(data_size <= recx_size);
			tail_hole_size += recx_size - data_size;
			D_ASSERT(tail_hole_size <= size_in_iod);

			/**
			 * During EC data recovery, data size might be shorter than
			 * the real data size, because the tail degraded sgl might
			 * be truncated by ioc_trim_tail_holes. so the sgl buf size
			 * is set by obj_ec_recov_fill_back().
			 */
			if (!reasb_req->orr_recov_data ||
			    (size_in_iod - tail_hole_size) > daos_sgl_data_len(usgl))
				dc_sgl_out_set(usgl, size_in_iod - tail_hole_size);

			return;
		}
		if (!recheck) {
			recheck = true;
			start = obj_ec_tgt_nr(oca) - 1;
			end = last_tgt + 1;
			if (start > end)
				goto tgt_check;
		}
	}
}

static struct obj_ec_recov_codec *
obj_ec_recov_codec_alloc(struct daos_oclass_attr *oca)
{
	struct obj_ec_recov_codec	*recov;
	unsigned short			 k = obj_ec_data_tgt_nr(oca);
	unsigned short			 p = obj_ec_parity_tgt_nr(oca);
	void				*buf, *tmp_ptr;
	size_t				 struct_size, tbl_size, matrix_size;
	size_t				 idx_size, list_size, err_size;

	struct_size = roundup(sizeof(struct obj_ec_recov_codec), 8);
	tbl_size = k * p * 32;
	matrix_size = roundup((k + p) * k, 8);
	idx_size = roundup(sizeof(uint32_t) * k, 8);
	list_size = roundup(sizeof(uint32_t) * p, 8);
	err_size = roundup(sizeof(bool) * (k + p), 8);

	D_ALLOC(buf, struct_size + tbl_size + 3 * matrix_size + idx_size +
		     list_size + err_size);
	if (buf == NULL)
		return NULL;

	tmp_ptr = buf;
	recov = buf;
	tmp_ptr += struct_size;
	recov->er_gftbls = tmp_ptr;
	tmp_ptr += tbl_size;
	recov->er_de_matrix = tmp_ptr;
	tmp_ptr += matrix_size;
	recov->er_inv_matrix = tmp_ptr;
	tmp_ptr += matrix_size;
	recov->er_b_matrix = tmp_ptr;
	tmp_ptr += matrix_size;
	recov->er_dec_idx = tmp_ptr;
	tmp_ptr += idx_size;
	recov->er_err_list = tmp_ptr;
	tmp_ptr += list_size;
	recov->er_in_err = tmp_ptr;

	return recov;
}

int
obj_ec_recov_add(struct obj_reasb_req *reasb_req,
		 struct daos_recx_ep_list *recx_lists, unsigned int nr)
{
	struct daos_recx_ep_list	*recov_lists;
	struct daos_recx_ep_list	*dst_list = NULL;
	struct daos_recx_ep_list	*src_list;
	uint32_t			 i, j;
	int				 rc;

	if (recx_lists == NULL || nr == 0)
		return 0;

	D_MUTEX_LOCK(&reasb_req->orr_mutex);
	recov_lists = reasb_req->orr_fail->efi_recx_lists;
	if (recov_lists == NULL) {
		D_ALLOC_ARRAY(recov_lists, nr);
		if (recov_lists == NULL)
			return -DER_NOMEM;
		reasb_req->orr_fail->efi_recx_lists = recov_lists;
		reasb_req->orr_fail->efi_nrecx_lists = nr;
	} else {
		D_ASSERT(reasb_req->orr_fail->efi_nrecx_lists == nr);
	}

	for (i = 0; i < nr; i++) {
		dst_list = &recov_lists[i];
		src_list = &recx_lists[i];
		dst_list->re_snapshot = src_list->re_snapshot;
		D_ASSERT(daos_recx_ep_list_ep_valid(src_list));
		for (j = 0; j < src_list->re_nr; j++) {
			rc = daos_recx_ep_add(dst_list, &src_list->re_items[j]);
			if (rc)
				return rc;
		}
	}
	daos_recx_ep_list_set(recov_lists, nr, 0, false);
	D_MUTEX_UNLOCK(&reasb_req->orr_mutex);
	return 0;
}

/**
 * In EC data recovery, need to check if different parity shards' parity exts'
 * epochs are match. To deal with the race condition between EC aggregation and
 * degraded fetch. In EC aggregation, for some cases need to generate new
 * parity exts, those new parity exts will be written to different remote parity
 * shards asynchronously, so possibly that in EC data recovery can get the
 * parity on some parity shards but cannot on other parity shard. Here check
 * different parity shards replied parity ext are match (with same epoch), if
 * mismatch then need to redo the degraded fetch from beginning.
 */
int
obj_ec_parity_check(struct obj_reasb_req *reasb_req,
		    struct daos_recx_ep_list *recx_lists, unsigned int nr)
{
	struct daos_recx_ep_list	*parity_lists;
	int				 rc = 0;

	if (recx_lists == NULL || nr == 0)
		return 0;

	D_MUTEX_LOCK(&reasb_req->orr_mutex);
	parity_lists = reasb_req->orr_parity_lists;
	if (parity_lists == NULL) {
		reasb_req->orr_parity_lists =
			daos_recx_ep_lists_dup(recx_lists, nr);
		reasb_req->orr_parity_list_nr = nr;
		parity_lists = reasb_req->orr_parity_lists;
		if (parity_lists == NULL)
			rc = -DER_NOMEM;
		goto out;
	}

	if (unlikely(DAOS_FAIL_CHECK(DAOS_FAIL_PARITY_EPOCH_DIFF))) {
		rc = -DER_FETCH_AGAIN;
		D_ERROR("simulate parity list mismatch, "DF_RC"\n", DP_RC(rc));
	} else {
		rc = obj_ec_parity_lists_match(parity_lists, recx_lists, nr);
		if (rc) {
			D_ERROR("got different parity lists, "DF_RC"\n", DP_RC(rc));
			daos_recx_ep_list_dump(parity_lists, nr);
			daos_recx_ep_list_dump(recx_lists, nr);
			goto out;
		}
	}

out:
	D_MUTEX_UNLOCK(&reasb_req->orr_mutex);
	return rc;
}

static void
obj_ec_recov_codec_free(struct obj_reasb_req *reasb_req)
{
	if (reasb_req->orr_fail)
		D_FREE(reasb_req->orr_fail->efi_recov_codec);
}

struct obj_ec_fail_info *
obj_ec_fail_info_get(struct obj_reasb_req *reasb_req, bool create, uint16_t nr)
{
	struct obj_ec_fail_info *fail_info = reasb_req->orr_fail;

	if (fail_info != NULL || !create)
		return fail_info;

	D_ASSERT(nr <= OBJ_EC_MAX_M);
	D_ALLOC_PTR(fail_info);
	if (fail_info == NULL)
		return NULL;
	reasb_req->orr_fail = fail_info;
	reasb_req->orr_fail_alloc = 1;

	D_ALLOC_ARRAY(fail_info->efi_tgt_list, nr);
	if (fail_info->efi_tgt_list == NULL)
		return NULL;

	return fail_info;
}

void
obj_ec_fail_info_reset(struct obj_reasb_req *reasb_req)
{
	struct obj_ec_fail_info		*fail_info = reasb_req->orr_fail;
	struct daos_recx_ep_list	*recx_lists;

	if (fail_info == NULL)
		return;

	obj_ec_recov_codec_free(reasb_req);
	recx_lists = fail_info->efi_recx_lists;
	if (recx_lists == NULL)
		return;
	daos_recx_ep_list_free(fail_info->efi_recx_lists,
			       fail_info->efi_nrecx_lists);
	daos_recx_ep_list_free(fail_info->efi_stripe_lists,
			       fail_info->efi_nrecx_lists);
	fail_info->efi_recx_lists = NULL;
	fail_info->efi_stripe_lists = NULL;
	fail_info->efi_nrecx_lists = 0;
	daos_recx_ep_list_free(reasb_req->orr_parity_lists,
			       reasb_req->orr_parity_list_nr);
	reasb_req->orr_parity_lists = NULL;
	reasb_req->orr_parity_list_nr = 0;
}

static bool
obj_ec_err_match(uint32_t nerrs, uint32_t *err_list1, uint32_t *err_list2)
{
	uint32_t	i;

	for (i = 0; i < nerrs; i++) {
		if (err_list1[i] != err_list2[i])
			return false;
	}
	return true;
}

static int
obj_ec_recov_codec_init(struct dc_object *obj, struct obj_reasb_req *reasb_req,
			uint64_t dkey_hash, uint32_t nerrs, uint32_t *err_list)
{
	struct daos_oclass_attr		*oca = reasb_req->orr_oca;
	struct obj_ec_fail_info		*fail_info = reasb_req->orr_fail;
	struct obj_ec_codec		*codec;
	struct obj_ec_recov_codec	*recov;
	unsigned char			s;
	uint32_t			i, j, r, k, p;
	uint32_t			err_tgt_off;
	int				rc;

	D_ASSERT(fail_info != NULL);
	k = obj_ec_data_tgt_nr(oca);
	p = obj_ec_parity_tgt_nr(oca);
	D_ASSERT(nerrs > 0 && err_list != NULL);
	if (nerrs > p) {
		rc = -DER_DATA_LOSS;
		D_ERROR(DF_OID " nerrs %d > p %d, " DF_RC "\n", DP_OID(obj->cob_md.omd_id), nerrs,
			p, DP_RC(rc));
		return rc;
	}

	if (fail_info->efi_recov_codec == NULL) {
		fail_info->efi_recov_codec = obj_ec_recov_codec_alloc(oca);
		if (fail_info->efi_recov_codec == NULL)
			return -DER_NOMEM;
	}
	recov = fail_info->efi_recov_codec;

	if (recov->er_nerrs == nerrs &&
	    obj_ec_err_match(nerrs, err_list, recov->er_err_list))
		return 0;

	codec = codec_get(reasb_req, obj->cob_md.omd_id);
	if (codec == NULL)
		return -DER_INVAL;

	/* init the err status */
	recov->er_nerrs = nerrs;
	recov->er_data_nerrs = 0;
	memset(recov->er_in_err, 0, sizeof(bool) * (k + p));
	for (i = 0; i < nerrs; i++) {
		err_tgt_off = obj_ec_shard_off(obj, dkey_hash, err_list[i]);

		D_ASSERT(err_list[i] < k + p);
		recov->er_err_list[i] = err_tgt_off;
		recov->er_in_err[err_tgt_off] = true;
		if (err_tgt_off < k)
			recov->er_data_nerrs++;
	}

	/* if all parity targets failed, just reuse the encode gftbls */
	if (recov->er_data_nerrs == 0 && recov->er_nerrs == p) {
		memcpy(recov->er_gftbls, codec->ec_gftbls, k * p * 32);
		D_DEBUG(DB_IO, "all parity tgts failed, reuse enc gftbls.\n");
		return 0;
	}

	/* Construct matrix b by removing error rows */
	for (i = 0, r = 0; i < k; i++, r++) {
		while (recov->er_in_err[r])
			r++;
		for (j = 0; j < k; j++)
			recov->er_b_matrix[k * i + j] =
				codec->ec_en_matrix[k * r + j];
		recov->er_dec_idx[i] = r;
	}

	/* Cauchy matrix is always invertible, should not fail */
	rc = gf_invert_matrix(recov->er_b_matrix, recov->er_inv_matrix, k);
	D_ASSERT(rc == 0);

	/* Generate decode matrix (err_list from invert matrix) */
	for (i = 0; i < recov->er_data_nerrs; i++) {
		err_tgt_off = obj_ec_shard_off(obj, dkey_hash, err_list[i]);
		for (j = 0; j < k; j++)
			recov->er_de_matrix[k * i + j] =
				recov->er_inv_matrix[k * err_tgt_off + j];
	}
	/* err_list from encode_matrix * invert matrix, for parity decoding */
	for (p = recov->er_data_nerrs; p < recov->er_nerrs; p++) {
		err_tgt_off = obj_ec_shard_off(obj, dkey_hash, err_list[p]);
		for (i = 0; i < k; i++) {
			s = 0;
			for (j = 0; j < k; j++)
				s ^= gf_mul(recov->er_inv_matrix[j * k + i],
					    codec->ec_en_matrix[k * err_tgt_off + j]);

			recov->er_de_matrix[k * p + i] = s;
		}
	}

	ec_init_tables(k, recov->er_nerrs, recov->er_de_matrix,
		       recov->er_gftbls);

	return 0;
}

static int
obj_ec_stripe_list_add(struct daos_recx_ep_list *stripe_list,
		       struct daos_recx_ep *stripe_recx)
{
	struct daos_recx_ep	*recx_ep;
	uint64_t		 start;
	uint32_t		 i;

	D_ASSERT(stripe_recx->re_type == DRT_SHADOW);
	for (i = 0; i < stripe_list->re_nr; i++) {
		recx_ep = &stripe_list->re_items[i];
		D_ASSERT(recx_ep->re_type == DRT_SHADOW);
		if (!DAOS_RECX_PTR_OVERLAP(&recx_ep->re_recx,
					   &stripe_recx->re_recx)) {
			if (recx_ep->re_ep != stripe_recx->re_ep)
				continue;
			/* merge adjacent stripe for same shadow ep */
			if (recx_ep->re_recx.rx_idx + recx_ep->re_recx.rx_nr ==
			    stripe_recx->re_recx.rx_idx) {
				recx_ep->re_recx.rx_nr +=
					stripe_recx->re_recx.rx_nr;
				return 0;
			} else if (stripe_recx->re_recx.rx_idx +
				 stripe_recx->re_recx.rx_nr ==
				 recx_ep->re_recx.rx_idx) {
				recx_ep->re_recx.rx_idx =
					stripe_recx->re_recx.rx_idx;
				recx_ep->re_recx.rx_nr +=
					stripe_recx->re_recx.rx_nr;
				return 0;
			}
			continue;
		}
		if (recx_ep->re_ep != stripe_recx->re_ep) {
			D_DEBUG(DB_IO, "overlapped recx with different shadow epoch, "
				"["DF_U64", "DF_U64"]@"DF_X64", "
				"["DF_U64", "DF_U64"]@"DF_X64"\n",
				recx_ep->re_recx.rx_idx, recx_ep->re_recx.rx_nr,
				recx_ep->re_ep, stripe_recx->re_recx.rx_idx,
				stripe_recx->re_recx.rx_nr, stripe_recx->re_ep);
			/* It is possible that different shard fetches go to different parity
			 * shards, they got recov_lists with different parity epoch in the case
			 * vos aggregation happens asynchronously on different parity shards
			 * that may merge adjacent parity exts to lower epoch. So here can
			 * just take higher epoch for data recovery.
			 */
			recx_ep->re_ep = max(recx_ep->re_ep, stripe_recx->re_ep);
		}
		start = min(recx_ep->re_recx.rx_idx, stripe_recx->re_recx.rx_idx);
		recx_ep->re_recx.rx_nr =
			max(recx_ep->re_recx.rx_idx + recx_ep->re_recx.rx_nr,
			    stripe_recx->re_recx.rx_idx +
			    stripe_recx->re_recx.rx_nr) - start;
		recx_ep->re_recx.rx_idx = start;
		return 0;
	}

	return daos_recx_ep_add(stripe_list, stripe_recx);
}

/** Generates the full-stripe recx lists that covers the input recx_lists. */
static int
obj_ec_stripe_list_init(struct obj_reasb_req *reasb_req)
{
	struct obj_ec_fail_info		*fail_info = reasb_req->orr_fail;
	struct daos_recx_ep_list	*recx_lists = fail_info->efi_recx_lists;
	struct daos_oclass_attr		*oca = reasb_req->orr_oca;
	daos_iod_t			*iods = reasb_req->orr_iods;
	daos_iod_t			*iod;
	struct daos_recx_ep_list	*stripe_lists;
	struct daos_recx_ep_list	*stripe_list, *recx_list;
	uint32_t			 iod_nr = fail_info->efi_nrecx_lists;
	struct daos_recx_ep		 stripe_recx, *tmp_recx;
	uint64_t			 start, end, stripe_rec_nr;
	uint32_t			 i, j;
	int				 rc = 0;

	if (reasb_req->orr_singv_only)
		return 0;

	stripe_rec_nr = obj_ec_stripe_rec_nr(oca);
	D_ALLOC_ARRAY(stripe_lists, iod_nr);
	if (stripe_lists == NULL)
		return -DER_NOMEM;

	for (i = 0; i < iod_nr; i++) {
		stripe_list = &stripe_lists[i];
		recx_list = &recx_lists[i];
		D_ASSERTF(recx_list->re_ep_valid, "recx_list %p", recx_list);
		stripe_list->re_ep_valid = 1;
		stripe_list->re_snapshot = recx_list->re_snapshot;
		iod = &iods[i];
		if (iod->iod_type == DAOS_IOD_SINGLE) {
			if (recx_list->re_nr == 0)
				continue;
			rc = obj_ec_stripe_list_add(stripe_list,
						    recx_list->re_items);
			if (rc) {
				D_ERROR("failed to add stripe "DF_RC"\n",
					DP_RC(rc));
				goto out;
			}
		}
		for (j = 0; j < recx_list->re_nr; j++) {
			tmp_recx = &recx_list->re_items[j];
			if (tmp_recx->re_type != DRT_SHADOW)
				continue;
			start = rounddown(tmp_recx->re_recx.rx_idx,
					  stripe_rec_nr);
			end = roundup(tmp_recx->re_recx.rx_idx +
				      tmp_recx->re_recx.rx_nr, stripe_rec_nr);
			stripe_recx = *tmp_recx;
			stripe_recx.re_recx.rx_idx = start;
			stripe_recx.re_recx.rx_nr = end - start;
			rc = obj_ec_stripe_list_add(stripe_list, &stripe_recx);
			if (rc) {
				D_ERROR("failed to add stripe "DF_RC"\n",
					DP_RC(rc));
				goto out;
			}
		}
	}

out:
	if (rc == 0)
		fail_info->efi_stripe_lists = stripe_lists;
	else
		daos_recx_ep_list_free(stripe_lists, iod_nr);
	return rc;
}

static void
obj_ec_recov_task_fini(struct obj_reasb_req *reasb_req)
{
	struct obj_ec_fail_info		*fail_info = reasb_req->orr_fail;
	uint32_t			 i;

	for (i = 0; i < fail_info->efi_stripe_sgls_nr; i++)
		d_sgl_fini(&fail_info->efi_stripe_sgls[i], true);
	D_FREE(fail_info->efi_stripe_sgls);

	for (i = 0; i < fail_info->efi_recov_ntasks; i++) {
		d_sgl_fini(&fail_info->efi_recov_tasks[i].ert_sgl, false);
		if (daos_handle_is_valid(fail_info->efi_recov_tasks[i].ert_th))
			dc_tx_local_close(fail_info->efi_recov_tasks[i].ert_th);
	}
	D_FREE(fail_info->efi_recov_tasks);
}

static int
obj_ec_recov_task_init(struct obj_reasb_req *reasb_req, daos_iod_t *iods, uint32_t iod_nr)
{
	struct obj_ec_fail_info		*fail_info = reasb_req->orr_fail;
	struct daos_oclass_attr		*oca = reasb_req->orr_oca;
	uint64_t			 stripe_total_sz, buf_sz, buf_off;
	uint64_t			 stripe_rec_nr =
						obj_ec_stripe_rec_nr(oca);
	struct daos_recx_ep_list	*stripe_lists =
						fail_info->efi_stripe_lists;
	struct daos_recx_ep_list	*stripe_list;
	struct daos_recx_ep		*recx_ep;
	struct obj_ec_recov_task	*rtask;
	daos_iod_t			*iod;
	d_sg_list_t			*sgl;
	void				*buf;
	uint32_t			 recx_ep_nr, stripe_nr;
	uint32_t			 i, j, tidx = 0;
	int				 rc = 0;

	D_ALLOC_ARRAY(fail_info->efi_stripe_sgls, iod_nr);
	if (fail_info->efi_stripe_sgls == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	fail_info->efi_stripe_sgls_nr = iod_nr;

	recx_ep_nr = 0;
	for (i = 0; i < iod_nr; i++) {
		stripe_list = &stripe_lists[i];
		if (!reasb_req->orr_singv_only && stripe_list->re_nr == 0)
			continue;

		iod = &iods[i];
		if (iod->iod_size == 0) {
			/* If IOD size has be reset to 0 in the initial try,
			 * let's reset it to the original iod_size to
			 * satisfy the iod/sgl valid check in recover task.
			 */
			iod->iod_size = reasb_req->orr_uiods[i].iod_size;
		}

		if (iod->iod_type == DAOS_IOD_SINGLE) {
			buf_sz = daos_sgl_buf_size(&reasb_req->orr_usgls[i]);
			buf_sz = max(iod->iod_size, buf_sz);
			buf_sz = obj_ec_singv_cell_bytes(buf_sz, oca) *
				 obj_ec_tgt_nr(oca);
			stripe_total_sz = buf_sz;
			recx_ep_nr++;
		} else {
			stripe_total_sz = obj_ec_tgt_nr(oca) *
					  obj_ec_cell_rec_nr(oca) *
					  iod->iod_size;
			stripe_nr = 0;
			for (j = 0; j < stripe_list->re_nr; j++) {
				recx_ep =  &stripe_list->re_items[j];
				D_ASSERT(recx_ep->re_recx.rx_nr %
					 stripe_rec_nr == 0);
				recx_ep_nr++;
				stripe_nr += recx_ep->re_recx.rx_nr /
					     stripe_rec_nr;
			}
			buf_sz = stripe_total_sz * stripe_nr;
		}
		sgl = &fail_info->efi_stripe_sgls[i];
		rc = d_sgl_init(sgl, 1);
		if (rc)
			goto out;
		D_ALLOC(buf, buf_sz);
		if (buf == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		d_iov_set(&sgl->sg_iovs[0], buf, buf_sz);
	}

	D_ALLOC_ARRAY(fail_info->efi_recov_tasks, recx_ep_nr);
	if (fail_info->efi_recov_tasks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	fail_info->efi_recov_ntasks = recx_ep_nr;

	for (i = 0; i < iod_nr; i++) {
		uint32_t	recx_nr = 0;

		stripe_list = &stripe_lists[i];
		if (!reasb_req->orr_singv_only && stripe_list->re_nr == 0)
			continue;
		iod = &iods[i];
		if (iod->iod_type == DAOS_IOD_SINGLE) {
			buf_sz = daos_sgl_buf_size(&reasb_req->orr_usgls[i]);
			buf_sz = max(iod->iod_size, buf_sz);
			buf_sz = obj_ec_singv_cell_bytes(buf_sz, oca) *
				 obj_ec_tgt_nr(oca);
			stripe_total_sz = buf_sz;
			D_ASSERT(reasb_req->orr_singv_only ||
				 stripe_list->re_nr == 1);
			recx_nr = 1;
		} else {
			stripe_total_sz = obj_ec_tgt_nr(oca) *
					  obj_ec_cell_rec_nr(oca) *
					  iod->iod_size;
			recx_nr = stripe_list->re_nr;
		}
		sgl = &fail_info->efi_stripe_sgls[i];
		buf_off = 0;
		for (j = 0; j < recx_nr; j++) {
			D_ASSERT(tidx < fail_info->efi_recov_ntasks);
			rtask = &fail_info->efi_recov_tasks[tidx++];
			if (reasb_req->orr_singv_only) {
				recx_ep = NULL;
			} else {
				recx_ep =  &stripe_list->re_items[j];
				rtask->ert_snapshot = stripe_list->re_snapshot;
			}
			if (iod->iod_type == DAOS_IOD_SINGLE) {
				stripe_nr = 1;
			} else {
				stripe_nr = recx_ep->re_recx.rx_nr /
					    stripe_rec_nr;
			}
			rtask->ert_oiod = iod;
			rtask->ert_uiod = &reasb_req->orr_uiods[i];
			rtask->ert_iod.iod_name = iod->iod_name;
			rtask->ert_iod.iod_type = iod->iod_type;
			rtask->ert_iod.iod_size = recx_ep == NULL ?
						  iod->iod_size :
						  recx_ep->re_rec_size;
			rtask->ert_iod.iod_nr = 1;
			rtask->ert_iod.iod_recxs = recx_ep == NULL ?
						   NULL :
						   &recx_ep->re_recx;
			rtask->ert_epoch = recx_ep == NULL ?
					   reasb_req->orr_epoch.oe_value :
					   recx_ep->re_ep;
			rc = d_sgl_init(&rtask->ert_sgl, 1);
			if (rc)
				goto out;
			buf_sz = stripe_nr * stripe_total_sz;
			D_ASSERT(buf_off + buf_sz <=
				 sgl->sg_iovs[0].iov_buf_len);
			d_iov_set(&rtask->ert_sgl.sg_iovs[0],
				  sgl->sg_iovs[0].iov_buf + buf_off, buf_sz);
			buf_off += buf_sz;
		}
	}

out:
	return rc;
}

void
obj_ec_fail_info_free(struct obj_reasb_req *reasb_req)
{
	struct obj_ec_fail_info *fail_info = reasb_req->orr_fail;

	if (fail_info == NULL || reasb_req->orr_fail_alloc == 0)
		return;

	obj_ec_recov_task_fini(reasb_req);
	obj_ec_recov_codec_free(reasb_req);
	daos_recx_ep_list_free(fail_info->efi_recx_lists,
			       fail_info->efi_nrecx_lists);
	daos_recx_ep_list_free(fail_info->efi_stripe_lists,
			       fail_info->efi_nrecx_lists);
	daos_recx_ep_list_free(reasb_req->orr_parity_lists,
			       reasb_req->orr_parity_list_nr);
	D_FREE(fail_info->efi_tgt_list);
	D_FREE(fail_info);
	reasb_req->orr_fail = NULL;
	reasb_req->orr_fail_alloc = 0;
	reasb_req->orr_parity_lists = NULL;
	reasb_req->orr_parity_list_nr = 0;
}

int
obj_ec_recov_prep(struct dc_object *obj, struct obj_reasb_req *reasb_req,
		  uint64_t dkey_hash, daos_iod_t *iods, uint32_t iod_nr)
{
	struct obj_ec_fail_info	*fail_info = reasb_req->orr_fail;
	int			 rc;

	D_ASSERT(reasb_req->orr_singv_only ||
		 iod_nr == fail_info->efi_nrecx_lists);
	/* when new target failed in recovery, the efi_stripe_lists and
	 * efi_recov_tasks already initialized.
	 */
	if (fail_info->efi_stripe_sgls == NULL) {
		rc = obj_ec_stripe_list_init(reasb_req);
		if (rc)
			goto out;

		rc = obj_ec_recov_task_init(reasb_req, iods, iod_nr);
		if (rc)
			goto out;
	}

	rc = obj_ec_recov_codec_init(obj, reasb_req, dkey_hash, fail_info->efi_ntgts,
				     fail_info->efi_tgt_list);
	if (rc)
		goto out;

out:
	if (rc)
		D_ERROR(DF_OID " obj_ec_recov_prep failed, " DF_RC "\n", DP_OID(obj->cob_md.omd_id),
			DP_RC(rc));
	return rc;
}

static void
obj_ec_recov_stripe(struct obj_ec_recov_codec *codec,
		    struct daos_oclass_attr *oca, void *buf_stripe,
		    uint64_t cell_sz)
{
	unsigned char	*buf_src[OBJ_EC_MAX_K];
	unsigned char	*buf_err[OBJ_EC_MAX_P];
	uint32_t	 i, k;

	k = obj_ec_data_tgt_nr(oca);
	for (i = 0; i < k; i++)
		buf_src[i] = buf_stripe + codec->er_dec_idx[i] * cell_sz;
	for (i = 0; i < codec->er_nerrs; i++)
		buf_err[i] = buf_stripe + codec->er_err_list[i] * cell_sz;

	ec_encode_data(cell_sz, k, codec->er_nerrs, codec->er_gftbls,
		       buf_src, buf_err);
}

struct oes_copy_arg {
	void		*buf;
	uint64_t	 size;
	uint64_t	 copied;
	d_sg_list_t	*sgl;
	struct daos_sgl_idx *sgl_idx;
};

static int
oes_copy(uint8_t *buf, size_t len, void *data)
{
	struct oes_copy_arg	*arg = data;
	d_sg_list_t		*sgl = arg->sgl;
	struct daos_sgl_idx	*idx = arg->sgl_idx;

	D_ASSERT(arg->copied + len <= arg->size);
	memcpy(buf, arg->buf + arg->copied, len);
	arg->copied += len;

	if (idx->iov_offset == 0) {
		D_ASSERT(idx->iov_idx > 0);
		sgl->sg_iovs[idx->iov_idx - 1].iov_len =
		     sgl->sg_iovs[idx->iov_idx - 1].iov_buf_len;
	} else {
		sgl->sg_iovs[idx->iov_idx].iov_len =
			max(sgl->sg_iovs[idx->iov_idx].iov_len, idx->iov_offset);
		D_ASSERTF(sgl->sg_iovs[idx->iov_idx].iov_len <=
			  sgl->sg_iovs[idx->iov_idx].iov_buf_len,
			  "iov_idx %u %p/%p offset %zd + len %zd > buf_len %zd",
			  idx->iov_idx, sgl, sgl->sg_iovs[idx->iov_idx].iov_buf,
			  idx->iov_offset, len, sgl->sg_iovs[idx->iov_idx].iov_buf_len);
	}

	return 0;
}

static void
obj_ec_sgl_copy(d_sg_list_t *sgl, uint64_t off, void *buf, uint64_t size)
{
	struct daos_sgl_idx	sgl_idx = {0};
	struct oes_copy_arg	arg;
	int			rc;

	/* to skip the sgl to offset - off */
	if (off != 0) {
		rc = daos_sgl_processor(sgl, true, &sgl_idx, off, NULL, NULL);
		D_ASSERT(rc == 0);
	}

	arg.buf = buf;
	arg.size = size;
	arg.copied = 0;
	arg.sgl = sgl;
	arg.sgl_idx = &sgl_idx;
	/* to copy data from [buf, buf + size) to sgl */
	rc = daos_sgl_processor(sgl, true, &sgl_idx, size, oes_copy, &arg);
	D_ASSERT(rc == 0);
}

/* copy the recovered data back to missed (to be recovered) recx list */
static void
obj_ec_recov_fill_back(daos_iod_t *iod, d_sg_list_t *sgl,
		       struct daos_recx_ep_list *recov_list,
		       struct daos_recx_ep_list *stripe_list,
		       d_sg_list_t *stripe_sgl, uint64_t stripe_total_sz,
		       uint64_t stripe_rec_nr)
{
	void		*stripe_buf;
	daos_recx_t	 recov_recx, stripe_recx, iod_recx, ovl = {0};
	uint64_t	 stripe_off, iod_off, tmp_off, rec_nr;
	uint64_t	 stripe_total_nr, stripe_nr;
	uint64_t	 iod_size = iod->iod_size;
	bool		 overlapped;
	uint32_t	 i, j, k;

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		stripe_buf = stripe_sgl->sg_iovs[0].iov_buf;
		obj_ec_sgl_copy(sgl, 0, stripe_buf, iod_size);
		return;
	}

	for (i = 0; i < recov_list->re_nr; i++) {
		recov_recx = recov_list->re_items[i].re_recx;
again:
		/* calculate the offset of to-be-recovered recx in original
		 * user iod/sgl.
		 */
		rec_nr = 0;
		overlapped = false;
		for (j = 0; j < iod->iod_nr; j++) {
			iod_recx = iod->iod_recxs[j];
			if (!DAOS_RECX_PTR_OVERLAP(&recov_recx, &iod_recx)) {
				rec_nr += iod_recx.rx_nr;
				continue;
			}
			overlapped = true;
			D_ASSERT(recov_recx.rx_idx >= iod_recx.rx_idx);
			ovl.rx_idx = recov_recx.rx_idx;
			ovl.rx_nr = min(recov_recx.rx_idx + recov_recx.rx_nr,
					iod_recx.rx_idx + iod_recx.rx_nr) -
				    ovl.rx_idx;
			rec_nr += recov_recx.rx_idx - iod_recx.rx_idx;
			break;
		}

		if (!overlapped)
			continue;

		iod_off = rec_nr * iod_size;

		/* break the to-be-recovered recx per stripe, can copy
		 * corresponding data in recovered full stripe to original
		 * user sgl.
		 */
		stripe_total_nr = 0;
		for (j = 0; j < stripe_list->re_nr; j++) {
			stripe_recx = stripe_list->re_items[j].re_recx;
			D_ASSERT(stripe_recx.rx_nr % stripe_rec_nr == 0);
			stripe_nr = stripe_recx.rx_nr / stripe_rec_nr;
			stripe_recx.rx_nr = stripe_rec_nr;
			for (k = 0; k < stripe_nr; k++) {
				stripe_off = stripe_total_nr * stripe_total_sz;
				stripe_buf = stripe_sgl->sg_iovs[0].iov_buf +
					     stripe_off;
				if (DAOS_RECX_PTR_OVERLAP(&ovl, &stripe_recx)) {
					D_ASSERT(ovl.rx_idx >=
						 stripe_recx.rx_idx);
					rec_nr = min(ovl.rx_idx + ovl.rx_nr,
						     stripe_recx.rx_idx +
						     stripe_recx.rx_nr) -
						 ovl.rx_idx;
					tmp_off = iod_size *
					    (ovl.rx_idx - stripe_recx.rx_idx);
					obj_ec_sgl_copy(sgl, iod_off,
							stripe_buf + tmp_off,
							rec_nr * iod_size);
					ovl.rx_idx += rec_nr;
					ovl.rx_nr -= rec_nr;
					if (ovl.rx_nr == 0)
						goto next;
				}
				stripe_recx.rx_idx += stripe_rec_nr;
				stripe_total_nr++;
			}
		}

next:
		D_ASSERT(ovl.rx_nr == 0);
		D_ASSERT(ovl.rx_idx <= recov_recx.rx_idx + recov_recx.rx_nr);
		if (ovl.rx_idx < recov_recx.rx_idx + recov_recx.rx_nr) {
			recov_recx.rx_nr = recov_recx.rx_idx + recov_recx.rx_nr
					   - ovl.rx_idx;
			recov_recx.rx_idx = ovl.rx_idx;
			goto again;
		}
	}
}

void
obj_ec_recov_data(struct obj_reasb_req *reasb_req, uint32_t iod_nr)
{
	daos_iod_t			*iods = reasb_req->orr_uiods;
	d_sg_list_t			*sgls = reasb_req->orr_usgls;
	struct obj_ec_fail_info		*fail_info = reasb_req->orr_fail;
	struct obj_ec_recov_codec	*codec = fail_info->efi_recov_codec;
	struct daos_oclass_attr		*oca = reasb_req->orr_oca;
	struct daos_recx_ep_list	*stripe_list, *recov_list;
	struct daos_recx_ep_list	*stripe_lists =
						fail_info->efi_stripe_lists;
	struct daos_recx_ep_list	*recov_lists =
						fail_info->efi_recx_lists;
	d_sg_list_t			*stripe_sgls =
						fail_info->efi_stripe_sgls;
	d_sg_list_t			*stripe_sgl, *sgl;
	daos_iod_t			*iod;
	void				*buf_stripe;
	uint32_t			 i, j, sidx, stripe_nr, recx_nr;
	uint64_t			 cell_sz, stripe_total_sz;
	uint64_t			 stripe_rec_nr =
						obj_ec_stripe_rec_nr(oca);
	struct daos_recx_ep		*recx_ep;
	bool				 singv;

	for (i = 0; i < iod_nr; i++) {
		if (!reasb_req->orr_singv_only) {
			stripe_list = &stripe_lists[i];
			recov_list = &recov_lists[i];
			if (recov_list->re_nr == 0 || stripe_list->re_nr == 0) {
				D_ASSERT(recov_list->re_nr == 0 &&
					 stripe_list->re_nr == 0);
				continue;
			}
		} else {
			stripe_list = NULL;
			recov_list = NULL;
		}

		iod = &iods[i];
		sgl = &sgls[i];
		singv = (iod->iod_type == DAOS_IOD_SINGLE);
		stripe_sgl = &stripe_sgls[i];
		iod->iod_size = reasb_req->orr_iods[i].iod_size;
		cell_sz = singv ? obj_ec_singv_cell_bytes(iod->iod_size, oca) :
				  obj_ec_cell_rec_nr(oca) * iod->iod_size;
		stripe_total_sz = cell_sz * obj_ec_tgt_nr(oca);
		buf_stripe = stripe_sgl->sg_iovs[0].iov_buf;
		recx_nr = singv ? 1 : stripe_list->re_nr;
		for (j = 0; j < recx_nr; j++) {
			if (singv) {
				stripe_nr = 1;
				if (obj_ec_singv_one_tgt(iod->iod_size,
							 sgl, oca))
					continue;
			} else {
				recx_ep = &stripe_list->re_items[j];
				stripe_nr = recx_ep->re_recx.rx_nr /
					    stripe_rec_nr;
			}
			for (sidx = 0; sidx < stripe_nr; sidx++) {
				obj_ec_recov_stripe(codec, oca, buf_stripe,
						    cell_sz);
				buf_stripe += stripe_total_sz;
			}
		}
		obj_ec_recov_fill_back(iod, sgl, recov_list, stripe_list,
				       stripe_sgl, stripe_total_sz,
				       stripe_rec_nr);
		reasb_req->orr_recov_data = 1;
	}
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
		     uint8_t *tgt_bitmap, uint32_t tgt_max_idx, uint32_t tgt_nr,
		     uint32_t start_tgt, struct daos_oclass_attr *oca)
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
		while (isclr(tgt_bitmap, idx) && idx < OBJ_EC_MAX_M)
			idx++;
		if (idx > tgt_max_idx)
			break;
		tgt_oiod = &tgt_oiods[i];
		tgt_oiod->oto_iod_nr = iod_nr;
		tgt_oiod->oto_tgt_idx = idx;
		tgt_oiod->oto_orig_tgt_idx = idx;
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
				oiod->oiod_tgt_idx =
					obj_ec_shard_off_by_start(tgt_oiod->oto_tgt_idx,
								  oca, start_tgt);
				oiod->oiod_siods = NULL;
			}
			continue;
		}
		for (j = 0; j < r_oiod->oiod_nr; j++) {
			r_siod = &r_oiod->oiod_siods[j];
			tgt = r_siod->siod_tgt_idx;
			if (isclr(tgt_bitmap, tgt))
				continue;
			tgt_oiod = obj_ec_tgt_oiod_get(tgt_oiods, tgt_nr, tgt);
			D_ASSERT(tgt_oiod && tgt_oiod->oto_tgt_idx == tgt);
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

/* Get all of recxs of the specific target from the daos offset */
int
obj_recx_ec2_daos(struct daos_oclass_attr *oca, uint32_t tgt_off,
		  daos_recx_t **recxs_p, daos_epoch_t **recx_ephs_p,
		  unsigned int *nr, bool convert_parity)
{
	int		cell_nr = obj_ec_cell_rec_nr(oca);
	int		stripe_nr = obj_ec_stripe_rec_nr(oca);
	daos_recx_t	*recxs = *recxs_p;
	daos_recx_t	*tgt_recxs;
	daos_epoch_t	*recx_ephs = NULL;
	unsigned int	total;
	int		idx;
	int		i;

	if (oca->ca_resil == DAOS_RES_REPL)
		return 0;

	/* parity shard conversion */
	if (is_ec_parity_shard_by_tgt_off(tgt_off, oca)) {
		for (i = 0; i < *nr; i++) {
			daos_off_t offset = recxs[i].rx_idx;

			if (!(offset & PARITY_INDICATOR))
				continue;

			offset &= ~PARITY_INDICATOR;
			D_ASSERTF(offset % cell_nr == 0, DF_RECX"\n", DP_RECX(recxs[i]));
			D_ASSERTF(recxs[i].rx_nr % cell_nr == 0, DF_RECX"\n", DP_RECX(recxs[i]));
			offset = obj_ec_idx_parity2daos(offset, cell_nr, stripe_nr);
			recxs[i].rx_idx = convert_parity ? offset : PARITY_INDICATOR | offset;
			recxs[i].rx_nr *= obj_ec_data_tgt_nr(oca);
		}
		return 0;
	}

	/* data shard conversion */
	for (i = 0, total = 0; i < *nr; i++) {
		daos_off_t offset = recxs[i].rx_idx;
		daos_off_t end = recxs[i].rx_idx + recxs[i].rx_nr;

		total += (roundup(end, cell_nr) - rounddown(offset, cell_nr)) /
			 cell_nr;
	}

	D_ALLOC_ARRAY(tgt_recxs, total);
	if (tgt_recxs == NULL)
		return -DER_NOMEM;
	if (recx_ephs_p != NULL) {
		D_ALLOC_ARRAY(recx_ephs, total);
		if (recx_ephs == NULL) {
			D_FREE(tgt_recxs);
			return -DER_NOMEM;
		}
	}

	for (i = 0, idx = 0; i < *nr; i++) {
		daos_off_t offset = recxs[i].rx_idx;
		daos_off_t size = recxs[i].rx_nr;

		while (size > 0) {
			daos_size_t daos_size;
			daos_off_t  daos_off;

			daos_off = obj_ec_idx_vos2daos(offset, stripe_nr,
						       cell_nr, tgt_off);
			daos_size = min(roundup(offset + 1, cell_nr) - offset,
					size);
			D_ASSERTF(idx < total, "idx %d total %u "DF_RECX"\n",
				  idx, total, DP_RECX(recxs[i]));
			tgt_recxs[idx].rx_idx = daos_off;
			tgt_recxs[idx].rx_nr = daos_size;
			if (recx_ephs != NULL)
				recx_ephs[idx] = (*recx_ephs_p)[i];
			offset += daos_size;
			size -= daos_size;
			idx++;
		}
	}

	if (recx_ephs_p) {
		D_FREE(*recx_ephs_p);
		*recx_ephs_p = recx_ephs;
	}

	D_FREE(*recxs_p);
	*recxs_p = tgt_recxs;
	*nr = total;
	return 0;
}

/* Convert DAOS offset to specific data target daos offset */
int
obj_recx_ec_daos2shard(struct daos_oclass_attr *oca, uint32_t tgt_off,
		       daos_recx_t **recxs_p, daos_epoch_t **recx_ephs_p,
		       unsigned int *iod_nr)
{
	daos_recx_t	*recx = *recxs_p;
	daos_epoch_t	*new_ephs = NULL;
	int		nr = *iod_nr;
	int		cell_nr = obj_ec_cell_rec_nr(oca);
	int		stripe_nr = obj_ec_stripe_rec_nr(oca);
	daos_recx_t	*tgt_recxs;
	int		total;
	int		idx;
	int		i;

	D_ASSERT(tgt_off < obj_ec_data_tgt_nr(oca));
	for (i = 0, total = 0; i < nr; i++) {
		uint64_t offset = recx[i].rx_idx & ~PARITY_INDICATOR;
		uint64_t end = offset + recx[i].rx_nr;

		while (offset < end) {
			daos_off_t shard_start = rounddown(offset, stripe_nr) +
						 tgt_off * cell_nr;
			daos_off_t shard_end = shard_start + cell_nr;

			/* Intersect with the shard cell */
			if (max(shard_start, offset) < min(shard_end, end))
				total++;

			offset = roundup(offset + 1, stripe_nr);
		}
	}

	if (total == 0) {
		D_FREE(*recxs_p);
		if (recx_ephs_p) {
			D_FREE(*recx_ephs_p);
			*recx_ephs_p = NULL;
		}
		*iod_nr = 0;
		return 0;
	}

	D_ALLOC_ARRAY(tgt_recxs, total);
	if (tgt_recxs == NULL)
		return -DER_NOMEM;

	if (recx_ephs_p != NULL) {
		D_ALLOC_ARRAY(new_ephs, total);
		if (new_ephs == NULL) {
			D_FREE(tgt_recxs);
			return -DER_NOMEM;
		}
	}

	for (i = 0, idx = 0; i < nr; i++) {
		uint64_t parity_indicator = recx[i].rx_idx & PARITY_INDICATOR;
		uint64_t offset = recx[i].rx_idx & ~PARITY_INDICATOR;
		uint64_t end = offset + recx[i].rx_nr;

		while (offset < end) {
			daos_off_t shard_start = rounddown(offset, stripe_nr) +
						 tgt_off * cell_nr;
			daos_off_t shard_end = shard_start + cell_nr;

			if (max(shard_start, offset) >= min(shard_end, end)) {
				offset = roundup(offset + 1, stripe_nr);
				continue;
			}

			/* Intersect with the shard cell */
			D_ASSERT(idx < total);
			tgt_recxs[idx].rx_idx = max(shard_start, offset);
			tgt_recxs[idx].rx_nr = min(shard_end, end) - tgt_recxs[idx].rx_idx;
			tgt_recxs[idx].rx_idx |= parity_indicator;
			if (new_ephs)
				new_ephs[idx] = (*recx_ephs_p)[i];
			idx++;
			offset = roundup(offset + 1, stripe_nr);
		}
	}

	D_FREE(*recxs_p);
	*recxs_p = tgt_recxs;
	if (recx_ephs_p != NULL) {
		D_FREE(*recx_ephs_p);
		*recx_ephs_p = new_ephs;
	}
	*iod_nr = total;

	return 0;
}
