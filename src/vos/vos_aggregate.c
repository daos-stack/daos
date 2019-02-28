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
  * Implementation for aggregation and discard
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos_srv/vos.h>
#include "vos_internal.h"

#define VOS_AGG_CREDITS_MAX	10000
struct vos_agg_param {
	struct umem_instance	*ap_umm;
	uint32_t	ap_credits_max; /* # of tight loops to yield */
	uint32_t	ap_credits;	/* # of tight loops */
	bool		ap_discard;	/* discard or not */
	unsigned int	ap_sub_tree_empty:1,
			ap_sv_tree_empty:1,
			ap_ev_tree_empty:1;
	union {
		/* SV tree: Max epoch in specified iterate epoch range */
		daos_epoch_t	ap_max_epoch;
	};
};

static int
agg_del_entry(daos_handle_t ih, struct umem_instance *umm,
	      vos_iter_entry_t *entry, unsigned int *acts)
{
	int	rc;
	bool	nvme_blk_free = false;

	D_ASSERT(umm != NULL);
	D_ASSERT(acts != NULL);

	rc = umem_tx_begin(umm, NULL);
	if (rc)
		return rc;

	if (entry->ie_biov.bi_addr.ba_type == DAOS_MEDIA_NVME)
		nvme_blk_free = true;

	rc = vos_iter_delete(ih, NULL);
	if (rc != 0)
		rc = umem_tx_abort(umm, rc);
	else
		rc = umem_tx_commit(umm);

	if (rc) {
		D_ERROR("Failed to delete entry: %d\n", rc);
		return rc;
	}

	*acts |= VOS_ITER_CB_DELETE;
	if (nvme_blk_free)
		*acts |= VOS_ITER_CB_YIELD;

	return rc;
}

static int
agg_discard_parent(daos_handle_t ih, vos_iter_entry_t *entry,
		   struct vos_agg_param *agg_param, unsigned int *acts)
{
	int	rc;

	D_ASSERT(agg_param && agg_param->ap_discard);
	D_ASSERT(acts != NULL);

	if (!agg_param->ap_sub_tree_empty)
		return 0;

	/*
	 * All entries in sub-tree were deleted during the nested sub-tree
	 * iteration, then vos_iterate() re-probed the key in outer iteration
	 * to delete it.
	 *
	 * Since there can be at most 1 discard/aggregation ULT for each
	 * container at any given time, the key won't be deleted by others even
	 * if current ULT yield in sub-tree iteration, and re-probe will find
	 * the exact matched key.
	 */
	agg_param->ap_sub_tree_empty = 0;
	rc = agg_del_entry(ih, agg_param->ap_umm, entry, acts);
	if (rc) {
		D_ERROR("Failed to delete key entry: %d\n", rc);
	} else if (vos_iter_empty(ih) == 1) {
		agg_param->ap_sub_tree_empty = 1;
		/* Trigger re-probe in outer iteration */
		*acts |= VOS_ITER_CB_YIELD;
	}

	return rc;
}

static int
vos_agg_obj(daos_handle_t ih, vos_iter_entry_t *entry,
	    struct vos_agg_param *agg_param, unsigned int *acts)
{
	int	rc;

	D_ASSERT(agg_param != NULL);

	if (agg_param->ap_discard) {
		rc = agg_discard_parent(ih, entry, agg_param, acts);
		agg_param->ap_sub_tree_empty = 0;
		return rc;
	}

	return 0;
}

static int
vos_agg_dkey(daos_handle_t ih, vos_iter_entry_t *entry,
	     struct vos_agg_param *agg_param, unsigned int *acts)
{
	D_ASSERT(agg_param != NULL);

	if (agg_param->ap_discard)
		return agg_discard_parent(ih, entry, agg_param, acts);

	return 0;
}

static int
vos_agg_akey(daos_handle_t ih, vos_iter_entry_t *entry,
	     struct vos_agg_param *agg_param, unsigned int *acts)
{
	D_ASSERT(agg_param != NULL);

	if (agg_param->ap_discard) {
		/* TODO: handle ap_ev_tree_empty */
		if (!agg_param->ap_sv_tree_empty)
			return 0;

		agg_param->ap_sv_tree_empty = 0;
		agg_param->ap_sub_tree_empty = 1;
		return agg_discard_parent(ih, entry, agg_param, acts);
	}

	/* Reset the max epoch for low-level SV tree iteration */
	agg_param->ap_max_epoch = 0;
	return 0;
}

static int
vos_agg_sv(daos_handle_t ih, vos_iter_entry_t *entry,
	   struct vos_agg_param *agg_param, unsigned int *acts)
{
	int	rc;

	D_ASSERT(agg_param != NULL);
	D_ASSERT(entry->ie_epoch != 0);

	/* Discard */
	if (agg_param->ap_discard)
		goto delete;

	/*
	 * Aggregate: preserve the first recx which has highest epoch, because
	 * of re-probe, the highest epoch could be iterated multiple times.
	 */
	if (agg_param->ap_max_epoch == 0 ||
	    agg_param->ap_max_epoch == entry->ie_epoch) {
		agg_param->ap_max_epoch = entry->ie_epoch;
		return 0;
	}

	D_ASSERTF(entry->ie_epoch < agg_param->ap_max_epoch,
		  "max:"DF_U64", cur:"DF_U64"\n",
		  agg_param->ap_max_epoch, entry->ie_epoch);

delete:
	rc = agg_del_entry(ih, agg_param->ap_umm, entry, acts);
	if (rc) {
		D_ERROR("Failed to delete SV entry: %d\n", rc);
	} else if (vos_iter_empty(ih) == 1 && agg_param->ap_discard) {
		agg_param->ap_sv_tree_empty = 1;
		/* Trigger re-probe in akey iteration */
		*acts |= VOS_ITER_CB_YIELD;
	}

	return rc;
}

static int
vos_agg_ev(daos_handle_t ih, vos_iter_entry_t *entry,
	   struct vos_agg_param *agg_param, unsigned int *acts)
{
	/* TODO */
	return 0;
}

static int
vos_aggregate_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		 vos_iter_type_t type, vos_iter_param_t *param,
		 void *cb_arg, unsigned int *acts)
{
	struct vos_agg_param	*agg_param = cb_arg;
	struct vos_container	*cont;
	int			 rc;

	switch (type) {
	case VOS_ITER_OBJ:
		rc = vos_agg_obj(ih, entry, agg_param, acts);
		break;
	case VOS_ITER_DKEY:
		rc = vos_agg_dkey(ih, entry, agg_param, acts);
		break;
	case VOS_ITER_AKEY:
		rc = vos_agg_akey(ih, entry, agg_param, acts);
		break;
	case VOS_ITER_SINGLE:
		rc = vos_agg_sv(ih, entry, agg_param, acts);
		break;
	case VOS_ITER_RECX:
		rc = vos_agg_ev(ih, entry, agg_param, acts);
		break;
	default:
		D_ASSERTF(false, "Invalid iter type\n");
		rc = -DER_INVAL;
		break;
	}

	if (rc < 0) {
		D_ERROR("VOS aggregation failed: %d\n", rc);
		return rc;
	}

	cont = vos_hdl2cont(param->ip_hdl);
	if (cont->vc_abort_aggregation) {
		D_DEBUG(DB_EPC, "VOS aggregation aborted\n");
		cont->vc_abort_aggregation = 0;
		cont->vc_in_aggregation = 0;
		return 1;
	}

	if (*acts & VOS_ITER_CB_YIELD)
		agg_param->ap_credits = 0;
	else
		agg_param->ap_credits++;

	if (agg_param->ap_credits > agg_param->ap_credits_max ||
	    (DAOS_FAIL_CHECK(DAOS_VOS_AGG_RANDOM_YIELD) && (rand() % 2))) {
		agg_param->ap_credits = 0;
		*acts |= VOS_ITER_CB_YIELD;
		bio_yield();
	}

	return 0;
}

static int
aggregate_enter(struct vos_container *cont, bool discard)
{
	if (cont->vc_in_aggregation) {
		D_ERROR(DF_CONT": Already in ggregation. discard:%d\n",
			DP_CONT(cont->vc_pool->vp_id, cont->vc_id), discard);

		/*
		 * The container will be eventually aggregated on next time
		 * when the aggregation being triggered by metadata server.
		 *
		 * TODO: This can be improved by tracking the new requested
		 * aggregation epoch range in vos_container, and start new
		 * aggregation immediately after current one is done.
		 */
		return -DER_BUSY;
	}

	cont->vc_in_aggregation = 1;
	return 0;
}

static void
aggregate_exit(struct vos_container *cont, bool discard)
{
	D_ASSERT(cont->vc_in_aggregation);
	cont->vc_in_aggregation = 0;
}

int
vos_aggregate(daos_handle_t coh, daos_epoch_range_t *epr)
{
	struct vos_container	*cont = vos_hdl2cont(coh);
	vos_iter_param_t	 iter_param = { 0 };
	struct vos_agg_param	 agg_param = { 0 };
	struct vos_iter_anchors	 anchors = { 0 };
	int			 rc;

	D_ASSERT(epr != NULL);
	D_ASSERTF(epr->epr_lo < epr->epr_hi && epr->epr_hi != DAOS_EPOCH_MAX,
		  "epr_lo:"DF_U64", epr_hi:"DF_U64"\n",
		  epr->epr_lo, epr->epr_hi);

	rc = aggregate_enter(cont, false);
	if (rc)
		return rc;

	/* Set iteration parameters */
	iter_param.ip_hdl = coh;
	iter_param.ip_epr = *epr;
	/*
	 * Iterate in epoch reserve order for SV tree, so that we can know for
	 * sure the first returned recx in SV tree has highest epoch and can't
	 * be aggregated.
	 */
	iter_param.ip_epc_expr = VOS_IT_EPC_RR;

	/* Set aggregation parameters */
	agg_param.ap_umm = &cont->vc_pool->vp_umm;
	agg_param.ap_credits_max = VOS_AGG_CREDITS_MAX;
	agg_param.ap_credits = 0;
	agg_param.ap_discard = false;

	iter_param.ip_flags |= VOS_IT_FOR_PURGE;
	rc = vos_iterate(&iter_param, VOS_ITER_OBJ, true, &anchors,
			 vos_aggregate_cb, &agg_param);
	if (rc != 0)
		goto exit;

	/*
	 * Update LAE, when aggregating for snapshot deletion, the
	 * @epr->epr_hi could be smaller than the LAE
	 */
	if (cont->vc_cont_df->cd_hae < epr->epr_hi)
		cont->vc_cont_df->cd_hae = epr->epr_hi;
exit:
	aggregate_exit(cont, false);
	return rc;
}

int
vos_discard(daos_handle_t coh, daos_epoch_range_t *epr)
{
	struct vos_container	*cont = vos_hdl2cont(coh);
	vos_iter_param_t	 iter_param = { 0 };
	struct vos_agg_param	 agg_param = { 0 };
	struct vos_iter_anchors	 anchors = { 0 };
	int			 rc;

	D_ASSERT(epr != NULL);
	D_ASSERTF(epr->epr_lo <= epr->epr_hi,
		  "epr_lo:"DF_U64", epr_hi:"DF_U64"\n",
		  epr->epr_lo, epr->epr_hi);

	rc = aggregate_enter(cont, true);
	if (rc != 0)
		return rc;

	D_DEBUG(DB_EPC, "Discard epr "DF_U64"-"DF_U64"\n",
		epr->epr_lo, epr->epr_hi);

	/* Set iteration parameters */
	iter_param.ip_hdl = coh;
	iter_param.ip_epr = *epr;
	if (epr->epr_lo == epr->epr_hi)
		iter_param.ip_epc_expr = VOS_IT_EPC_EQ;
	else if (epr->epr_hi != DAOS_EPOCH_MAX)
		iter_param.ip_epc_expr = VOS_IT_EPC_RR;
	else
		iter_param.ip_epc_expr = VOS_IT_EPC_GE;

	/* Set aggregation parameters */
	agg_param.ap_umm = &cont->vc_pool->vp_umm;
	agg_param.ap_credits_max = VOS_AGG_CREDITS_MAX;
	agg_param.ap_credits = 0;
	agg_param.ap_discard = true;

	iter_param.ip_flags |= VOS_IT_FOR_PURGE;
	rc = vos_iterate(&iter_param, VOS_ITER_OBJ, true, &anchors,
			 vos_aggregate_cb, &agg_param);

	aggregate_exit(cont, true);
	return rc;
}
