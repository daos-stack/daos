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

static void
obj_query_merge_recx(struct daos_oclass_attr *oca, daos_unit_oid_t oid, daos_key_t *dkey,
		     daos_recx_t *src_recx, daos_recx_t *tgt_recx, bool get_max, bool changed,
		     uint32_t *shard)
{
	daos_recx_t	tmp_recx = *src_recx;
	uint64_t	tmp_end;
	uint32_t	tgt_off;
	bool		from_data_tgt;
	uint64_t	dkey_hash;
	uint64_t	stripe_rec_nr;
	uint64_t	cell_rec_nr;

	if (!daos_oclass_is_ec(oca))
		D_GOTO(out, changed = true);

	dkey_hash = obj_dkey2hash(oid.id_pub, dkey);
	tgt_off = obj_ec_shard_off_by_oca(oid.id_layout_ver, dkey_hash, oca, oid.id_shard);
	from_data_tgt = is_ec_data_shard_by_tgt_off(tgt_off, oca);
	stripe_rec_nr = obj_ec_stripe_rec_nr(oca);
	cell_rec_nr = obj_ec_cell_rec_nr(oca);
	D_ASSERT(!(src_recx->rx_idx & PARITY_INDICATOR));

	/*
	 * Data ext from data shard needs to be converted to daos ext,
	 * replica ext from parity shard needs not to convert.
	 */
	tmp_end = DAOS_RECX_END(tmp_recx);
	D_DEBUG(DB_IO, "shard %d/%u get recx "DF_U64" "DF_U64"\n",
		oid.id_shard, tgt_off, tmp_recx.rx_idx, tmp_recx.rx_nr);

	if (tmp_end > 0 && from_data_tgt) {
		if (get_max) {
			tmp_recx.rx_idx = max(tmp_recx.rx_idx, rounddown(tmp_end - 1, cell_rec_nr));
			tmp_recx.rx_nr = tmp_end - tmp_recx.rx_idx;
		} else {
			tmp_recx.rx_nr = min(tmp_end, roundup(tmp_recx.rx_idx + 1, cell_rec_nr)) -
					 tmp_recx.rx_idx;
		}

		tmp_recx.rx_idx = obj_ec_idx_vos2daos(tmp_recx.rx_idx, stripe_rec_nr, cell_rec_nr,
						      tgt_off);
		tmp_end = DAOS_RECX_END(tmp_recx);
	}

	if ((get_max && DAOS_RECX_END(*tgt_recx) < tmp_end) ||
	    (!get_max && DAOS_RECX_END(*tgt_recx) > tmp_end))
		changed = true;

out:
	if (changed) {
		*tgt_recx = tmp_recx;
		if (shard != NULL)
			*shard = oid.id_shard;
	}
}

static inline void
obj_query_merge_key(uint64_t *tgt_val, uint64_t src_val, bool *changed, bool dkey,
		    uint32_t *tgt_shard, uint32_t src_shard)
{
	D_DEBUG(DB_TRACE, "%s update "DF_U64"->"DF_U64"\n",
		dkey ? "dkey" : "akey", *tgt_val, src_val);

	*tgt_val = src_val;
	/* Set to change akey and recx. */
	*changed = true;
	if (tgt_shard != NULL)
		*tgt_shard = src_shard;
}

int
daos_obj_merge_query_merge(struct obj_query_merge_args *args)
{
	uint64_t	*val;
	uint64_t	*cur;
	uint32_t	 timeout = 0;
	bool		 check = true;
	bool		 changed = false;
	bool		 get_max = (args->flags & DAOS_GET_MAX) ? true : false;
	bool		 first = false;
	int		 rc = 0;

	D_ASSERT(args->oca != NULL);
	args->opc = opc_get(args->opc);

	if (args->ret != 0) {
		if (args->ret == -DER_NONEXIST)
			D_GOTO(set_max_epoch, rc = 0);

		if (args->ret == -DER_INPROGRESS || args->ret == -DER_TX_BUSY ||
		    args->ret == -DER_OVERLOAD_RETRY)
			D_DEBUG(DB_TRACE, "%s query rpc needs retry: "DF_RC"\n",
				args->opc == DAOS_OBJ_RPC_COLL_QUERY ? "Collective" : "Regular",
				DP_RC(args->ret));
		else
			D_ERROR("%s query rpc failed: "DF_RC"\n",
				args->opc == DAOS_OBJ_RPC_COLL_QUERY ? "Collective" : "Regular",
				DP_RC(args->ret));

		if (args->ret == -DER_OVERLOAD_RETRY && args->rpc != NULL) {
			D_ASSERT(args->max_delay != NULL);
			D_ASSERT(args->queue_id != NULL);

			if (args->opc == DAOS_OBJ_RPC_COLL_QUERY) {
				struct obj_coll_query_out	*ocqo = crt_reply_get(args->rpc);

				if (*args->queue_id == 0)
					*args->queue_id = ocqo->ocqo_comm_out.req_out_enqueue_id;
			} else {
				struct obj_query_key_v10_out	*okqo = crt_reply_get(args->rpc);

				if (*args->queue_id == 0)
					*args->queue_id = okqo->okqo_comm_out.req_out_enqueue_id;
			}

			crt_req_get_timeout(args->rpc, &timeout);
			if (timeout > *args->max_delay)
				*args->max_delay = timeout;
		}

		D_GOTO(out, rc = args->ret);
	}

	if (*args->tgt_map_ver < args->src_map_ver)
		*args->tgt_map_ver = args->src_map_ver;

	if (args->flags == 0)
		goto set_max_epoch;

	if (args->tgt_dkey->iov_len == 0)
		first = true;

	if (args->flags & DAOS_GET_DKEY) {
		val = (uint64_t *)args->src_dkey->iov_buf;
		cur = (uint64_t *)args->tgt_dkey->iov_buf;

		D_ASSERT(cur != NULL);

		if (args->src_dkey->iov_len != sizeof(uint64_t)) {
			D_ERROR("Invalid dkey obtained: %d\n", (int)args->src_dkey->iov_len);
			D_GOTO(out, rc = -DER_IO);
		}

		/* For first merge, just set the dkey. */
		if (first) {
			args->tgt_dkey->iov_len = args->src_dkey->iov_len;
			obj_query_merge_key(cur, *val, &changed, true, args->shard,
					    args->oid.id_shard);
		} else if (get_max) {
			if (*val > *cur)
				obj_query_merge_key(cur, *val, &changed, true, args->shard,
						    args->oid.id_shard);
			else if (!daos_oclass_is_ec(args->oca) || *val < *cur)
				/*
				 * No change, don't check akey and recx for replica obj. EC obj
				 * needs to check again as it maybe from different data shards.
				 */
				check = false;
		} else if (args->flags & DAOS_GET_MIN) {
			if (*val < *cur)
				obj_query_merge_key(cur, *val, &changed, true, args->shard,
						    args->oid.id_shard);
			else if (!daos_oclass_is_ec(args->oca))
				check = false;
		} else {
			D_ASSERT(0);
		}
	}

	if (check && args->flags & DAOS_GET_AKEY) {
		val = (uint64_t *)args->src_akey->iov_buf;
		cur = (uint64_t *)args->tgt_akey->iov_buf;

		/* If first merge or dkey changed, set akey. */
		if (first || changed)
			obj_query_merge_key(cur, *val, &changed, false, NULL, args->oid.id_shard);
	}

	if (check && args->flags & DAOS_GET_RECX)
		obj_query_merge_recx(args->oca, args->oid,
				     (args->flags & DAOS_GET_DKEY) ? args->src_dkey : args->in_dkey,
				     args->src_recx, args->tgt_recx, get_max, changed, args->shard);

set_max_epoch:
	if (args->tgt_epoch != NULL && *args->tgt_epoch < args->src_epoch)
		*args->tgt_epoch = args->src_epoch;
out:
	return rc;
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
