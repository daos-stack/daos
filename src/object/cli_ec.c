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
 * DAOS client erasure-coded object handling.
 *
 * src/object/cli_ec.c
 */
#define D_LOGFAC	DD_FAC(object)

#include <daos/common.h>
#include <daos_task.h>
#include <daos_types.h>
#include "obj_rpc.h"
#include "obj_internal.h"

int
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

void
obj_ec_recxs_fini(struct obj_ec_recx_array *recxs)
{
	if (recxs == NULL)
		return;
	if (recxs->oer_recxs != NULL)
		D_FREE(recxs->oer_recxs);
	recxs->oer_nr = 0;
}

void
obj_ec_pcode_fini(struct obj_ec_pcode *pcode, uint32_t nr)
{
	int	i;

	if (pcode == NULL)
		return;
	if (pcode->oep_ec_recxs != NULL) {
		for (i = 0; i < nr; i++)
			obj_ec_recxs_fini(&pcode->oep_ec_recxs[i]);
		D_FREE(pcode->oep_ec_recxs);
	}
	if (pcode->oep_sgls != NULL) {
		for (i = 0; i < nr; i++)
			daos_sgl_fini(&pcode->oep_sgls[i], true);
		D_FREE(pcode->oep_sgls);
	}
	if (pcode->oep_bulks != NULL) {
		for (i = 0; i < nr; i++)
			crt_bulk_free(&pcode->oep_bulks[i]);
		D_FREE(pcode->oep_bulks);
	}
	memset(pcode, 0, sizeof(*pcode));
}

static int
obj_ec_pcode_init(struct obj_ec_pcode *pcode, uint32_t nr)
{
	if (pcode->oep_ec_recxs != NULL) {
		D_ASSERT(pcode->oep_sgls != NULL);
		D_ASSERT(pcode->oep_bulks != NULL);
		return -DER_ALREADY;
	}
	D_ALLOC_ARRAY(pcode->oep_ec_recxs, nr);
	if (pcode->oep_ec_recxs == NULL)
		return -DER_NOMEM;
	D_ALLOC_ARRAY(pcode->oep_sgls, nr);
	if (pcode->oep_sgls == NULL) {
		obj_ec_pcode_fini(pcode, nr);
		return -DER_NOMEM;
	}
	D_ALLOC_ARRAY(pcode->oep_bulks, nr);
	if (pcode->oep_bulks == NULL) {
		obj_ec_pcode_fini(pcode, nr);
		return -DER_NOMEM;
	}
	return 0;
}

/** scan the iod to find the full_stripe recxs */
static int
obj_ec_recx_scan(daos_iod_t *iod, struct daos_oclass_attr *oca,
		 struct obj_ec_recx_array *ec_recx_array)
{
	struct obj_ec_recx		*ec_recx = NULL;
	daos_recx_t			*recx;
	uint64_t			 stripe_rec_nr;
	uint64_t			 start, end, rec_nr, rec_off;
	int				 i, idx, rc;

	D_ASSERT(iod->iod_type == DAOS_IOD_ARRAY);
	stripe_rec_nr = obj_ec_stripe_rec_nr(iod, oca);

	for (i = 0, idx = 0, rec_off = 0; i < iod->iod_nr; i++) {
		recx = &iod->iod_recxs[i];
		start = roundup(recx->rx_idx, stripe_rec_nr);
		end = rounddown(recx->rx_idx + recx->rx_nr, stripe_rec_nr);
		if (start >= end) {
			rec_off += recx->rx_nr;
			continue;
		}

		if (ec_recx_array->oer_recxs == NULL) {
			rc = obj_ec_recxs_init(ec_recx_array, iod->iod_nr - i);
			if (rc)
				return rc;
			ec_recx = ec_recx_array->oer_recxs;
		}
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
	}

	if (ec_recx_array->oer_recxs != NULL) {
		D_ASSERT(idx > 0 && idx <= iod->iod_nr);
		ec_recx_array->oer_nr = idx;
	} else {
		D_ASSERT(ec_recx_array->oer_nr == 0);
	}

	return 0;
}

/** Encode one full stripe, the result parity buffer will be filled. */
static int
obj_ec_stripe_encode(daos_iod_t *iod, d_sg_list_t *sgl, uint32_t iov_idx,
		     size_t iov_off, struct obj_ec_codec *codec,
		     struct daos_oclass_attr *oca, unsigned char *parity_bufs[])
{
	uint64_t		 len = obj_ec_cell_bytes(iod, oca);
	unsigned int		 k = oca->u.ec.e_k;
	unsigned int		 p = oca->u.ec.e_p;
	unsigned char		*data[k];
	unsigned char		*c_data[k]; /* copied data */
	unsigned char		*from;
	int			 i, c_idx = 0;
	int			 rc = 0;

	for (i = 0; i < k; i++) {
		c_data[i] = NULL;
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

	ec_encode_data(len, k, p, codec->ec_gftbls, data, parity_bufs);

out:
	for (i = 0; i < c_idx; i++)
		D_FREE(c_data[i]);
	return rc;
}

static int
ec_parity_buf_init(unsigned int p, uint64_t parity_len, d_sg_list_t *ec_sgl)
{
	unsigned char	*parity_buf[OBJ_EC_MAX_P] = {0};
	int		 i, rc;

	rc = daos_sgl_init(ec_sgl, p);
	if (rc)
		goto out;
	for (i = 0; i < p; i++) {
		D_ALLOC(parity_buf[i], parity_len);
		if (parity_buf[i] == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		d_iov_set(&ec_sgl->sg_iovs[i], parity_buf[i], parity_len);
	}

out:
	if (rc)
		daos_sgl_fini(ec_sgl, true);
	return rc;
}

/**
 * Encode the data in full stripe recx_array, the result parity stored in
 * ec_sgl. Within the ec_sgl, one parity target with one separate iov for its
 * parity code.
 */
static int
obj_ec_recx_encode(daos_obj_id_t oid, daos_iod_t *iod, d_sg_list_t *sgl,
		   struct daos_oclass_attr *oca,
		   struct obj_ec_recx_array *recx_array, d_sg_list_t *ec_sgl)
{
	struct obj_ec_codec	*codec;
	struct obj_ec_recx	*ec_recx;
	unsigned int		 p = oca->u.ec.e_p;
	unsigned char		*parity_buf[p];
	uint64_t		 cell_bytes, stripe_bytes, parity_len;
	uint32_t		 iov_idx = 0;
	uint64_t		 iov_off = 0, last_off = 0;
	uint32_t		 encoded_nr = 0;
	int			 i, j, m, rc;

	D_ASSERT(recx_array != NULL && ec_sgl != NULL);
	if (recx_array->oer_nr == 0)
		D_GOTO(out, rc = 0);
	D_ASSERT(recx_array->oer_stripe_total > 0);
	D_ASSERT(recx_array->oer_recxs != NULL);
	codec = obj_ec_codec_get(daos_obj_id2class(oid));
	if (codec == NULL) {
		D_ERROR("failed to get ec codec.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	cell_bytes = obj_ec_cell_bytes(iod, oca);
	stripe_bytes = cell_bytes * oca->u.ec.e_k;
	parity_len = recx_array->oer_stripe_total * cell_bytes;
	rc = ec_parity_buf_init(p, parity_len, ec_sgl);
	if (rc)
		goto out;

	/* calculate EC parity for each full_stripe */
	for (i = 0; i < recx_array->oer_nr; i++) {
		ec_recx = &recx_array->oer_recxs[i];
		daos_sgl_move(sgl, iov_idx, iov_off,
			      ec_recx->oer_byte_off - last_off);
		last_off = ec_recx->oer_byte_off;
		for (j = 0; j < ec_recx->oer_stripe_nr; j++) {
			for (m = 0; m < p; m++)
				parity_buf[m] = ec_sgl->sg_iovs[m].iov_buf +
						encoded_nr * cell_bytes;
			/*
			 *D_PRINT("encode %d rec_offset "DF_U64", rec_nr "
			 *	DF_U64".\n", j, iov_off / iod->iod_size,
			 *	stripe_bytes / iod->iod_size);
			 */
			rc = obj_ec_stripe_encode(iod, sgl, iov_idx, iov_off,
						  codec, oca, parity_buf);
			if (rc) {
				D_ERROR("stripe encoding failed rc %d.\n", rc);
				goto out;
			}
			encoded_nr++;
			daos_sgl_move(sgl, iov_idx, iov_off, stripe_bytes);
			last_off += stripe_bytes;
		}
	}

out:
	if (rc)
		daos_sgl_fini(ec_sgl, true);
	return rc;
}

int
obj_ec_update_encode(daos_obj_update_t *args, daos_obj_id_t oid,
		     struct daos_oclass_attr *oca, struct obj_ec_pcode *pcode,
		     uint64_t *tgt_set)
{
	daos_iod_t			*iods = args->iods;
	d_sg_list_t			*sgls = args->sgls;
	uint32_t			 nr = args->nr;
	int				 i, rc;

	rc = obj_ec_pcode_init(pcode, nr);
	if (rc) {
		if (rc != -DER_ALREADY)
			goto out;

		D_DEBUG(DB_TRACE, DF_OID" pcode already inited (retry case).\n",
			DP_OID(oid));
		ec_get_tgt_set(iods, nr, oca, true, tgt_set);
		return 0;
	}

	for (i = 0; i < nr; i++) {
		rc = obj_ec_recx_scan(&iods[i], oca, &pcode->oep_ec_recxs[i]);
		if (rc) {
			D_ERROR(DF_OID" obj_ec_recx_scan failed %d.\n",
				DP_OID(oid), rc);
			goto out;
		}
		/* obj_ec_recx_array_dump(&pcode->oep_ec_recxs[i]); */

		rc = obj_ec_recx_encode(oid, &iods[i], &sgls[i], oca,
					&pcode->oep_ec_recxs[i],
					&pcode->oep_sgls[i]);
		if (rc) {
			D_ERROR(DF_OID" obj_ec_recx_encode failed %d.\n",
				DP_OID(oid), rc);
			goto out;
		}
	}

	ec_get_tgt_set(iods, nr, oca, true, tgt_set);
out:
	if (rc)
		obj_ec_pcode_fini(pcode, nr);
	return rc;
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

/* Determines weather a given IOD contains a recx that is at least a full
 * stripe's worth of data.
 */
static bool
ec_has_full_stripe(daos_iod_t *iod, struct daos_oclass_attr *oca,
		   uint64_t *tgt_set)
{
	unsigned int	ss = oca->u.ec.e_k * oca->u.ec.e_len;
	unsigned int	i;

	for (i = 0; i < iod->iod_nr; i++) {
		if (iod->iod_type == DAOS_IOD_ARRAY) {
			int start = iod->iod_recxs[i].rx_idx * iod->iod_size;
			int length = iod->iod_recxs[i].rx_nr * iod->iod_size;

			if (length < ss) {
				continue;
			} else if (length - (start % ss) >= ss) {
				*tgt_set = ~0UL;
				return true;
			}
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
	unsigned int	 p = oca->u.ec.e_p;
	daos_recx_t     *this_recx = &iod->iod_recxs[recx_idx];
	uint64_t	 recx_start_offset = this_recx->rx_idx * iod->iod_size;
	uint64_t	 recx_end_offset = (this_recx->rx_nr * iod->iod_size) +
					   recx_start_offset;
	unsigned int	 i;
	int		 rc = 0;

	/* s_cur is the index (in bytes) into the recx where a full stripe
	 * begins.
	 */
	s_cur = recx_start_offset + (recx_start_offset % (len * k));
	if (s_cur != recx_start_offset)
		/* if the start of stripe is not at beginning of recx, move
		 * the sgl index to where the stripe begins).
		 */
		ec_move_sgl_cursors(sgl, recx_start_offset % (len * k), sg_idx,
				    sg_off);

	for ( ; s_cur + len*k <= recx_end_offset; s_cur += len*k) {
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
	unsigned int	 i;
	int		 rc = 0;

	D_REALLOC_ARRAY(nrecx, (niod->iod_recxs), (niod->iod_nr + iod->iod_nr));
	if (nrecx == NULL)
		return -DER_NOMEM;
	niod->iod_recxs = nrecx;
	for (i = 0; i < iod->iod_nr; i++) {
		uint64_t rem = iod->iod_recxs[i].rx_nr * iod->iod_size;
		uint64_t start = iod->iod_recxs[i].rx_idx;
		uint32_t stripe_cnt = rem / (len * k);

		if (rem % (len*k)) {
			stripe_cnt++;
		}
		/* can't have more than one stripe in a recx entry */
		if (stripe_cnt > 1) {
			D_REALLOC_ARRAY(nrecx,
					(niod->iod_recxs),
					(niod->iod_nr +
					 iod->iod_nr + stripe_cnt - 1));
			if (nrecx == NULL) {
				D_FREE(niod->iod_recxs);
				return -DER_NOMEM;
			}
			niod->iod_recxs = nrecx;
		}
		while (rem) {
			uint32_t stripe_len = (len * k) / iod->iod_size;

			if (rem <= len * k) {
				niod->iod_recxs[params->niod.iod_nr].
				rx_nr = rem/iod->iod_size;
				niod->iod_recxs[params->niod.iod_nr++].
				rx_idx = start;
				rem = 0;
			} else {
				niod->iod_recxs[params->niod.iod_nr].
				rx_nr = (len * k) / iod->iod_size;
				niod->iod_recxs[params->niod.iod_nr++].
				rx_idx = start;
				start += stripe_len;
				rem -= len * k;
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


/* Call-back that recovers EC allocated memory  */
static int
ec_free_params_cb(tse_task_t *task, void *data)
{
	struct ec_params *head = *((struct ec_params **)data);
	int		  rc = task->dt_result;

	ec_free_params(head);
	return rc;
}

/* Identifies the applicable subset of forwarding targets for non-full-stripe
 * EC updates.
 */
void
ec_get_tgt_set(daos_iod_t *iods, unsigned int nr, struct daos_oclass_attr *oca,
	       bool parity_include, uint64_t *tgt_set)
{
	unsigned int    len = obj_ec_cell_bytes(iods, oca);
	unsigned int    k = oca->u.ec.e_k;
	unsigned int    p = oca->u.ec.e_p;
	uint64_t	ss = k * len;
	uint64_t	par_only = (1UL << p) - 1;
	uint64_t	full;
	unsigned int	i, j;

	if (parity_include) {
		full = (1UL << (k+p)) - 1;
		for (i = 0; i < p; i++)
			*tgt_set |= 1UL << i;
	} else {
		full = (1UL << k) - 1;
	}
	for (i = 0; i < nr; i++) {
		if (iods->iod_type != DAOS_IOD_ARRAY)
			continue;
		for (j = 0; j < iods[i].iod_nr; j++) {
			uint64_t ext_idx;
			uint64_t rs = iods[i].iod_recxs[j].rx_idx *
						iods[i].iod_size;
			uint64_t re = iods[i].iod_recxs[j].rx_nr *
						iods[i].iod_size + rs - 1;

			/* No partial-parity updates, so this function won't be
			 * called if parity is present.
			 */
			D_ASSERT(!(PARITY_INDICATOR & rs));
			for (ext_idx = rs; ext_idx <= re; ext_idx += len) {
				unsigned int cell = (ext_idx % ss)/len;

				*tgt_set |= 1UL << (cell+p);
				if (*tgt_set == full && parity_include) {
					*tgt_set = 0;
					return;
				}
			}
		}
	}
	/* In case it's a single value, set the tgt_set to send to all.
	 * These go everywhere for now.
	 */
	if (*tgt_set == par_only)
		*tgt_set = 0;
}

static inline bool
ec_has_parity_cli(daos_iod_t *iod)
{
	return iod->iod_recxs[0].rx_idx & PARITY_INDICATOR;
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

		if (ec_has_full_stripe(iod, oca, tgt_set)) {
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
					if (rc != 0)
						break;
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
		} else if (head != NULL && &(head->sgls[i]) != NULL) {
			/* Add sgls[i] and iods[i] to head. Since we're
			 * adding ec parity (head != NULL) and thus need to
			 * replace the arrays in the update struct.
			 */
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
	} else {
		/* Called for updates with no full stripes.
		 * Builds a bit map only if forwarding targets are
		 * a proper subset. Sets tgt_set to zero if all targets
		 * are addressed.
		 */
		ec_get_tgt_set(args->iods, args->nr, oca, true, tgt_set);
	}

	if (rc != 0 && head != NULL) {
		ec_free_params(head);
	} else if (head != NULL) {
		for (i = 0; i < head->nr; i++)
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
