/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * common helper functions for object
 */
#define DDSUBSYS	DDFAC(object)

#include <daos_types.h>
#include "obj_internal.h"

static daos_size_t
daos_iod_len(daos_iod_t *iod)
{
	daos_size_t	len;
	int		i;

	if (iod->iod_size == DAOS_REC_ANY)
		return -1; /* unknown size */

	len = 0;

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		len += iod->iod_size;
	} else {
		if (iod->iod_recxs == NULL)
			return 0;

		for (i = 0, len = 0; i < iod->iod_nr; i++)
			len += iod->iod_size * iod->iod_recxs[i].rx_nr;
	}

	return len;
}

daos_size_t
daos_iods_len(daos_iod_t *iods, int nr)
{
	daos_size_t iod_length = 0;
	int	    i;

	for (i = 0; i < nr; i++) {
		daos_size_t len = daos_iod_len(&iods[i]);

		if (len == (daos_size_t)-1) /* unknown */
			return -1;

		iod_length += len;
	}
	return iod_length;
}

int
daos_iod_copy(daos_iod_t *dst, daos_iod_t *src)
{
	int rc;

	rc = daos_iov_copy(&dst->iod_name, &src->iod_name);
	if (rc)
		return rc;

	dst->iod_type	= src->iod_type;
	dst->iod_size	= src->iod_size;
	dst->iod_nr	= src->iod_nr;
	dst->iod_recxs	= src->iod_recxs;

	return 0;
}

void
daos_iods_free(daos_iod_t *iods, int nr, bool need_free)
{
	int i;

	for (i = 0; i < nr; i++) {
		daos_iov_free(&iods[i].iod_name);

		if (iods[i].iod_recxs)
			D_FREE(iods[i].iod_recxs);
	}

	if (need_free)
		D_FREE(iods);
}

struct recx_rec {
	daos_recx_t	*rr_recx;
};

static int
recx_key_cmp(struct btr_instance *tins, struct btr_record *rec, d_iov_t *key)
{
	struct recx_rec	*r = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	daos_recx_t	*key_recx = (daos_recx_t *)(key->iov_buf);

	D_ASSERT(key->iov_len == sizeof(*key_recx));

	if (DAOS_RECX_PTR_OVERLAP(r->rr_recx, key_recx)) {
		D_ERROR("recx overlap between ["DF_U64", "DF_U64"], "
			"["DF_U64", "DF_U64"].\n", r->rr_recx->rx_idx,
			r->rr_recx->rx_nr, key_recx->rx_idx, key_recx->rx_nr);
		return BTR_CMP_ERR;
	}

	/* will never return BTR_CMP_EQ */
	D_ASSERT(r->rr_recx->rx_idx != key_recx->rx_idx);
	if (r->rr_recx->rx_idx < key_recx->rx_idx)
		return BTR_CMP_LT;

	return BTR_CMP_GT;
}

static int
recx_rec_alloc(struct btr_instance *tins, d_iov_t *key, d_iov_t *val,
	     struct btr_record *rec, d_iov_t *val_out)
{
	struct recx_rec	*r;
	umem_off_t	roff;
	daos_recx_t	*key_recx = (daos_recx_t *)(key->iov_buf);

	if (key_recx == NULL || key->iov_len != sizeof(*key_recx))
		return -DER_INVAL;

	roff = umem_zalloc(&tins->ti_umm, sizeof(*r));
	if (UMOFF_IS_NULL(roff))
		return tins->ti_umm.umm_nospc_rc;

	r = umem_off2ptr(&tins->ti_umm, roff);
	r->rr_recx = key_recx;
	rec->rec_off = roff;

	return 0;
}

static int
recx_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	umem_free(&tins->ti_umm, rec->rec_off);
	return 0;
}

static int
recx_rec_update(struct btr_instance *tins, struct btr_record *rec,
		d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	D_ASSERTF(0, "recx_rec_update should never be called.\n");
	return 0;
}

static int
recx_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	       d_iov_t *key, d_iov_t *val)
{
	D_ASSERTF(0, "recx_rec_fetch should never be called.\n");
	return 0;
}

static void
recx_key_encode(struct btr_instance *tins, d_iov_t *key,
	daos_anchor_t *anchor)
{
	D_ASSERTF(0, "recx_key_encode should never be called.\n");
}

static void
recx_key_decode(struct btr_instance *tins, d_iov_t *key,
	daos_anchor_t *anchor)
{
	D_ASSERTF(0, "recx_key_decode should never be called.\n");
}

static char *
recx_rec_string(struct btr_instance *tins, struct btr_record *rec, bool leaf,
		char *buf, int buf_len)
{
	struct recx_rec	*r = NULL;
	daos_recx_t	*recx;

	if (!leaf) {
		/* no record body on intermediate node */
		snprintf(buf, buf_len, "--");
	} else {
		r = (struct recx_rec *)umem_off2ptr(&tins->ti_umm,
						   rec->rec_off);
		recx = r->rr_recx;
		snprintf(buf, buf_len, "rx_idx - "DF_U64" : rx_nr - "DF_U64,
			 recx->rx_idx, recx->rx_nr);
	}

	return buf;
}

static btr_ops_t recx_btr_ops = {
	.to_key_cmp	= recx_key_cmp,
	.to_rec_alloc	= recx_rec_alloc,
	.to_rec_free	= recx_rec_free,
	.to_rec_fetch	= recx_rec_fetch,
	.to_rec_update	= recx_rec_update,
	.to_rec_string	= recx_rec_string,
	.to_key_encode	= recx_key_encode,
	.to_key_decode	= recx_key_decode
};

void
obj_coll_disp_init(uint32_t tgt_nr, uint32_t max_tgt_size, uint32_t inline_size,
		   uint32_t start, uint32_t max_width, struct obj_coll_disp_cursor *ocdc)
{
	if (max_width == 0) {
		/*
		 * Guarantee that the targets information (to be dispatched) can be packed
		 * inside the RPC body instead of via bulk transfer.
		 */
		max_width = (inline_size + max_tgt_size) / DAOS_BULK_LIMIT + 1;
		if (max_width < COLL_DISP_WIDTH_DEF)
			max_width = COLL_DISP_WIDTH_DEF;
	}

	if (tgt_nr - start > max_width) {
		ocdc->grp_nr = max_width;
		ocdc->cur_step = (tgt_nr - start) / max_width;
		if ((tgt_nr - start) % max_width != 0) {
			ocdc->cur_step++;
			ocdc->fixed_step = 0;
		} else {
			ocdc->fixed_step = 1;
		}
	} else {
		ocdc->grp_nr = tgt_nr - start;
		ocdc->cur_step = 1;
		ocdc->fixed_step = 1;
	}

	ocdc->pending_grps = ocdc->grp_nr;
	ocdc->tgt_nr = tgt_nr;
	ocdc->cur_pos = start;
}

void
obj_coll_disp_dest(struct obj_coll_disp_cursor *ocdc, struct daos_coll_target *tgts,
		   crt_endpoint_t *tgt_ep)
{
	struct daos_coll_target		*dct = &tgts[ocdc->cur_pos];
	struct daos_coll_target		 tmp;
	unsigned long			 rand = 0;
	uint32_t			 size;
	int				 pos;
	int				 i;

	if (ocdc->cur_step > 2) {
		rand = d_rand();
		/* Randomly choose an engine as the relay one for load balance. */
		pos = rand % (ocdc->tgt_nr - ocdc->cur_pos) + ocdc->cur_pos;
		if (pos != ocdc->cur_pos) {
			memcpy(&tmp, &tgts[pos], sizeof(tmp));
			memcpy(&tgts[pos], dct, sizeof(tmp));
			memcpy(dct, &tmp, sizeof(tmp));
		}
	}

	size = dct->dct_bitmap_sz << 3;

	/* Randomly choose a XS as the local leader on target engine for load balance. */
	for (i = 0, pos = (rand != 0 ? rand : d_rand()) % dct->dct_tgt_nr; i < size; i++) {
		if (isset(dct->dct_bitmap, i)) {
			pos -= dct->dct_shards[i].dcs_nr;
			if (pos < 0)
				break;
		}
	}

	D_ASSERT(i < size);

	tgt_ep->ep_tag = i;
	tgt_ep->ep_rank = dct->dct_rank;
}

void
obj_coll_disp_move(struct obj_coll_disp_cursor *ocdc)
{
	ocdc->cur_pos += ocdc->cur_step;

	/* The last one. */
	if (--(ocdc->pending_grps) == 0) {
		D_ASSERTF(ocdc->cur_pos == ocdc->tgt_nr,
			  "COLL disp cursor trouble (1): "
			  "grp_nr %u, pos %u, step %u (%s), tgt_nr %u\n",
			  ocdc->grp_nr, ocdc->cur_pos, ocdc->cur_step,
			  ocdc->fixed_step ? "fixed" : "vary", ocdc->tgt_nr);
		return;
	}

	D_ASSERTF(ocdc->tgt_nr - ocdc->cur_pos >= ocdc->pending_grps,
		  "COLL disp cursor trouble (2): "
		  "pos %u, step %u (%s), tgt_nr %u, grp_nr %u, pending_grps %u\n",
		  ocdc->cur_pos, ocdc->cur_step, ocdc->fixed_step ? "fixed" : "vary",
		  ocdc->tgt_nr, ocdc->grp_nr, ocdc->pending_grps);

	if (ocdc->fixed_step) {
		D_ASSERTF(ocdc->cur_pos + ocdc->cur_step <= ocdc->tgt_nr,
			  "COLL disp cursor trouble (3): "
			  "pos %u, step %u (%s), tgt_nr %u, grp_nr %u, pending_grps %u\n",
			  ocdc->cur_pos, ocdc->cur_step, ocdc->fixed_step ? "fixed" : "vary",
			  ocdc->tgt_nr, ocdc->grp_nr, ocdc->pending_grps);
		return;
	}

	ocdc->cur_step = (ocdc->tgt_nr - ocdc->cur_pos) / ocdc->pending_grps;
	if ((ocdc->tgt_nr - ocdc->cur_pos) % ocdc->pending_grps != 0)
		ocdc->cur_step++;
	else
		ocdc->fixed_step = 1;
}

int
obj_utils_init(void)
{
	int	rc;

	rc = dbtree_class_register(DBTREE_CLASS_RECX, BTR_FEAT_DIRECT_KEY,
				   &recx_btr_ops);
	if (rc != 0 && rc != -DER_EXIST) {
		D_ERROR("failed to register DBTREE_CLASS_RECX: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(failed, rc);
	}
	return 0;
failed:
	D_ERROR("Failed to initialize DAOS object utilities\n");
	return rc;
}

void
obj_utils_fini(void)
{
}
