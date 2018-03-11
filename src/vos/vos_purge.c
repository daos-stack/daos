/**
 * (C) Copyright 2016 Intel Corporation.
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
 * This file is part of VOS
 *
 * vos/vos_purge.c
 * Implementation for aggregation and discard
 */
#define DDSUBSYS	DDFAC(vos)

#include <daos/btree.h>
#include <daos_srv/vos.h>
#include <vos_internal.h>

/** context of epoch purge (aggregate/discard)*/
struct purge_context {
	/** reference on the object to be checked */
	struct vos_object	*pc_obj;
	/** PMEM pool for transactions */
	PMEMobjpool		*pc_pop;
	/** the current iterator type */
	vos_iter_type_t		 pc_type;
	/** cookie to discard */
	uuid_t			 pc_cookie;
	/** recursive iterator parameters */
	vos_iter_param_t	 pc_param;
};

enum { /* iterator operation code */
	ITR_NEXT		= (1 << 0),
	/** Probe the first node */
	ITR_PROBE_FIRST		= (1 << 1),
	/** Probe a specific anchor */
	ITR_PROBE_ANCHOR	= (1 << 2),
	/** Max iterator probe anchor (recx) */
	ITR_MAX_PROBE_ANCHOR	= (1 << 3),
	/** Reuse iterator (for restarting) */
	ITR_REUSE_ANCHOR	= (1 << 4),
};

enum {
	/** bitmask position for obj anchor */
	OBJ_ANCHOR		= (1 << 0),
	/** bitmask position for dkey anchor */
	DKEY_ANCHOR		= (1 << 1),
	/** bitmask position for akey anchor */
	AKEY_ANCHOR		= (1 << 2),
	/** bitmask position for single value anchor */
	SINGV_ANCHOR		= (1 << 3),
	/** bitmask position for obj scan completion */
	OBJ_SCAN_COMPLETE	= (1 << 4),
	/** bitmask position for dkey scan completion */
	DKEY_SCAN_COMPLETE	= (1 << 5),
	/** bitmask position for akey scan completion */
	AKEY_SCAN_COMPLETE	= (1 << 6),
	/** bitmask position for recx scan completion */
	RECX_SCAN_COMPLETE	= (1 << 7),
};

enum {
	/** Set mask and copy anchor to purge anchor */
	ANCHOR_SET		= 0,
	/** Anchor and mask unset */
	ANCHOR_UNSET,
	/** Copy purge achor to provided anchor */
	ANCHOR_COPY,
};

#define pcx_name(pcx)			vos_iter_type2name((pcx)->pc_type)

static int
purge_ctx_init(struct purge_context *pcx, vos_iter_entry_t *ent)
{
	vos_iter_param_t *param = &pcx->pc_param;
	int		  rc	= 0;

	switch (pcx->pc_type) {
	default:
	case VOS_ITER_SINGLE:
		D__ASSERT(0);
	case VOS_ITER_NONE:
		pcx->pc_type = VOS_ITER_OBJ;
		break;

	case VOS_ITER_OBJ:
		/* TODO:
		 * - aggregation: discard all punched objects between the epoch
		 *   range condition, aggregate the last version within the
		 *   range.
		 * - discard: discard new versions and the punch operations.
		 */
		rc = vos_obj_hold(vos_obj_cache_current(), param->ip_hdl,
				  ent->ie_oid, param->ip_epr.epr_hi, true,
				  &pcx->pc_obj);
		if (rc != 0)
			break;
		param->ip_oid = ent->ie_oid;
		daos_iov_set(&param->ip_dkey, NULL, 0);
		daos_iov_set(&param->ip_akey, NULL, 0);
		pcx->pc_pop  = vos_obj2pop(pcx->pc_obj);
		pcx->pc_type = VOS_ITER_DKEY;
		break;

	case VOS_ITER_DKEY:
		param->ip_dkey = ent->ie_key;
		daos_iov_set(&param->ip_akey, NULL, 0);
		pcx->pc_type = VOS_ITER_AKEY;
		break;

	case VOS_ITER_AKEY:
		param->ip_akey = ent->ie_key;
		pcx->pc_type = VOS_ITER_SINGLE;
		break;

	}
	D__DEBUG(DB_EPC, "Initialized %s iterator context: %d.\n",
		pcx_name(pcx), rc);
	return rc;
}

static void
purge_ctx_fini(struct purge_context *pcx, int rc)
{
	D__DEBUG(DB_EPC, "Finalize %s iterator context: %d.\n",
		pcx_name(pcx), rc);

	switch (pcx->pc_type) {
	default:
		D__ASSERT(0);
	case VOS_ITER_OBJ:
		pcx->pc_type = VOS_ITER_NONE;
		return;

	case VOS_ITER_DKEY:
		D__ASSERT(pcx->pc_obj != NULL);

		/* Evict the object because we might have destroyed the
		 * cached I/O context, or even released the object.
		 */
		vos_obj_evict(pcx->pc_obj);
		vos_obj_release(vos_obj_cache_current(), pcx->pc_obj);
		pcx->pc_obj  = NULL;
		pcx->pc_type = VOS_ITER_OBJ;
		return;

	case VOS_ITER_AKEY:
		pcx->pc_type = VOS_ITER_DKEY;
		return;

	case VOS_ITER_SINGLE:
		pcx->pc_type = VOS_ITER_AKEY;
		return;
	}
}


static inline void
purge_set_iter_expr(struct purge_context *pcx, daos_epoch_range_t *epr)
{
	/**
	 * Setting appropriate epoch logic expression for recx iterator.
	 *
	 *  -- VOS_IT_EPC_EQ gurantees to probe and fetch only
	 *     records updated in this \a epr::epr_lo.
	 *
	 *  -- VOS_IT_EPC_GE on the other hand probes and fetches
	 *     all records from epr::epr_lo till DAOS_EPOCH_MAX.
	 *
	 *  -- VOS_IT_EPC_RR probes and fetches on arbitrary epoch
	 *     ranges in reverse order
	 */
	if (epr->epr_lo == epr->epr_hi)
		pcx->pc_param.ip_epc_expr = VOS_IT_EPC_EQ;
	else if (epr->epr_hi != DAOS_EPOCH_MAX)
		pcx->pc_param.ip_epc_expr = VOS_IT_EPC_RR;
	else
		pcx->pc_param.ip_epc_expr = VOS_IT_EPC_GE;
}

/**
 * Check if a valid anchor was provided
 */
static inline bool
purge_anchor_is_valid(vos_purge_anchor_t *anchor)
{
	if (anchor->pa_mask > 0) {
		if (!((OBJ_ANCHOR  & anchor->pa_mask)  ||
		      (DKEY_ANCHOR & anchor->pa_mask)  ||
		      (AKEY_ANCHOR & anchor->pa_mask)  ||
		      (SINGV_ANCHOR & anchor->pa_mask)  ||
		      (OBJ_SCAN_COMPLETE & anchor->pa_mask) ||
		      (DKEY_SCAN_COMPLETE & anchor->pa_mask) ||
		      (AKEY_SCAN_COMPLETE & anchor->pa_mask) ||
		      (RECX_SCAN_COMPLETE & anchor->pa_mask)))
			return false;
	}
	return true;
}

static bool
purge_oid_is_aggregated(vos_purge_anchor_t *anchor, daos_unit_oid_t oid)
{
	bool res = false;

	if (!(memcmp(&anchor->pa_oid, &oid, sizeof(daos_unit_oid_t)))) {
		if (DKEY_SCAN_COMPLETE & anchor->pa_mask)
			res = true;
	} else {
		/** anchor working on a different OID */
		anchor->pa_oid = oid;
		anchor->pa_mask &= ~DKEY_SCAN_COMPLETE;
		anchor->pa_mask &= ~AKEY_SCAN_COMPLETE;
		anchor->pa_mask &= ~RECX_SCAN_COMPLETE;
	}
	return res;
}
/**
 * Check if an anchor is set for a particular context
 */
static bool
purge_ctx_anchor_is_set(struct purge_context *pcx,
			vos_purge_anchor_t *vp_anchor)
{
	switch (pcx->pc_type) {
	default:
		D__ASSERT(0);
	case VOS_ITER_OBJ:
		return OBJ_ANCHOR & vp_anchor->pa_mask;
	case VOS_ITER_DKEY:
		return DKEY_ANCHOR & vp_anchor->pa_mask;
	case VOS_ITER_AKEY:
		return AKEY_ANCHOR & vp_anchor->pa_mask;
	case VOS_ITER_SINGLE:
		return SINGV_ANCHOR & vp_anchor->pa_mask;
	}
}

/*
 * Toggles between set/unset based on op value
 * also used in copying from purge anchor to a new anchor.
 */
static void
purge_ctx_anchor_ctl(struct purge_context *pcx, vos_purge_anchor_t *vp_anchor,
		     daos_hash_out_t *anchor, int op)
{

	daos_hash_out_t		*purge_anchor;
	unsigned int		bits;

	switch (pcx->pc_type) {
	default:
		D__ASSERT(0);
	case VOS_ITER_OBJ:
		purge_anchor	= &vp_anchor->pa_obj;
		bits		= OBJ_ANCHOR;
		break;
	case VOS_ITER_DKEY:
		purge_anchor	= &vp_anchor->pa_dkey;
		bits		= DKEY_ANCHOR;
		break;
	case VOS_ITER_AKEY:
		purge_anchor	= &vp_anchor->pa_akey;
		bits		= AKEY_ANCHOR;
		break;
	case VOS_ITER_SINGLE:
		purge_anchor	= &vp_anchor->pa_recx;
		bits		= SINGV_ANCHOR;
		break;
	}

	if (op == ANCHOR_SET) {
		memcpy(purge_anchor, anchor, sizeof(*purge_anchor));
		vp_anchor->pa_mask |= bits;

	} else if (op == ANCHOR_UNSET) {
		memset(purge_anchor, 0, sizeof(*purge_anchor));
		vp_anchor->pa_mask &= ~bits;

	} else {
		/** copy case purge_anchor copied to anchor */
		memcpy(anchor, purge_anchor, sizeof(*anchor));
	}
}

static bool
purge_ctx_test_complete(struct purge_context *pcx, bool *finish,
			vos_purge_anchor_t *anchor)
{
	switch (pcx->pc_type) {
	default:
		D__ASSERT(0);
	case VOS_ITER_OBJ:
		/*
		 * Currently nothing
		 * XXX: will be needed when adding credits for
		 * discard
		 */
		break;
	case VOS_ITER_DKEY:
		if (DKEY_SCAN_COMPLETE & anchor->pa_mask) {
			*finish = true;
			return true;
		}
		break;
	case VOS_ITER_AKEY:
		if (AKEY_SCAN_COMPLETE & anchor->pa_mask)
			return true;
		break;
	case VOS_ITER_SINGLE:
		if (RECX_SCAN_COMPLETE & anchor->pa_mask)
			return true;
		break;
	}
	return false;
}

static void
purge_ctx_reset_complete(struct purge_context *pcx,
			 vos_purge_anchor_t *vp_anchor)
{
	switch (pcx->pc_type) {
	default:
		D__ASSERT(0);
	case VOS_ITER_SINGLE:
		break;
	case VOS_ITER_AKEY:
		vp_anchor->pa_mask &= ~RECX_SCAN_COMPLETE;
		break;
	case VOS_ITER_DKEY:
		vp_anchor->pa_mask &= ~AKEY_SCAN_COMPLETE;
		break;
	}
}

static void
purge_ctx_set_complete(struct purge_context *pcx, bool *finish,
		       vos_purge_anchor_t *vp_anchor)
{
	switch (pcx->pc_type) {
	default:
		D__ASSERT(0);
	case VOS_ITER_OBJ:
		/*
		 * Currently nothing
		 * XXX: will be needed when adding credits for
		 * discard
		 */
		break;
	case VOS_ITER_DKEY:
		vp_anchor->pa_mask |= DKEY_SCAN_COMPLETE;
		D__DEBUG(DB_EPC, "Setting DKEY scan completion\n");
		D__ASSERT(finish != NULL);
		*finish = true;
		break;
	case VOS_ITER_AKEY:
		vp_anchor->pa_mask |= AKEY_SCAN_COMPLETE;
		D__DEBUG(DB_EPC, "Setting AKEY scan completion\n");
		break;
	case VOS_ITER_SINGLE:
		vp_anchor->pa_mask |= RECX_SCAN_COMPLETE;
		D__DEBUG(DB_EPC, "Setting RECX scan completion\n");
		break;
	}
}

/**
 * Recx aggregation uses a additional max_iterator which always tracks and
 * retains the max epoch in the {recx, epoch} tree. This approach is used
 * to avoid issues with unsorted cases with EV-Tree. This function probes
 * the max iterators for recx on different scenarios.
 */
/**
 * max-iter probing during different types of iterations:
 * ITR_NEXT requires max ih probe only when recx's are different. In all
 * other case max-iter needs to be probed.
 * FIRST		: to set the max_iter
 * ITR_MAX_PROBE	: max_iter is deleted
 * ITR_PROBE		: need to reset max_iter while setting anchor as
 *			  well as while deleting a record pointed but
 *			  regular iterator max iterator pos is changed.
 *			  NB: If reverse iteration is used
 *			  this additional probe is skipped.
 *
 * ITR_NEXT && entries	: probe max_iter on itr_next only when ent && ent_max
 *			  are pointing to different recx's.
 */
static int
recx_max_iter_probe(int opc, vos_iter_entry_t *ent, vos_iter_entry_t *ent_max,
		    vos_purge_anchor_t *vp_anchor, daos_hash_out_t *anchor,
		    vos_it_epc_expr_t epc, daos_handle_t *ih_max)
{

	int	rc = 0;
	char	*opstr;
	bool	it_reverse_skip;

	it_reverse_skip = ((epc == VOS_IT_EPC_RR) && (opc & ITR_PROBE_ANCHOR));

	if ((opc & ITR_NEXT) || it_reverse_skip)
		return 0;

	if (opc & ITR_REUSE_ANCHOR) {
		opstr = "probe max-iter from max_anchor";
		rc = vos_iter_probe(*ih_max, &vp_anchor->pa_recx_max);
	} else {
		/*
		 * ITR_PROBE_FIRST, ITR_NEXT, ITR_PROBE_MAX_ANCHOR,
		 * ITR_PROBE_ANCHOR
		 */
		opstr  = "probe max-iter hdl";
		rc = vos_iter_probe(*ih_max, anchor);
	}

	/**
	 * No need to check for -DER_NONEXIST, max_iterator
	 * will never overtake regular iterator
	 */
	if (rc == 0) {
		opstr = "fetch max-iter entry";
		rc = vos_iter_fetch(*ih_max, ent_max, &vp_anchor->pa_recx_max);
	}

	if (rc != 0) {
		D__ERROR("%s max-iterator failed to %s: %d\n",
			vos_iter_type2name(VOS_ITER_SINGLE), opstr, rc);
	}
	return rc;
}

/**
 * core function of aggregation, similar to discard recursively enter
 * different trees and delete the leaf record or retain based on the
 * epoch in the epoch-range. recx aggregation is done in recx_epoch_aggregate
 */
int
epoch_aggregate(struct purge_context *pcx, int *empty_ret,
		unsigned int *credits_ret, vos_purge_anchor_t *vp_anchor,
		bool *finish)
{
	int			rc = 0;
	int			aggregated, found;
	int			opc;
	daos_handle_t		ih, ih_max;
	daos_hash_out_t		anchor;
	vos_iter_entry_t	ent_max;
	unsigned int		credits = *credits_ret;
	bool			val_tree = (pcx->pc_type == VOS_ITER_SINGLE);


	D__DEBUG(DB_EPC, "Enter %s iterator with credits: %u\n", pcx_name(pcx),
		*credits_ret);

	/* No credits left to enter this level */
	if (!credits)
		return 0;

	/* if scan already completed at this level exit */
	if (purge_ctx_test_complete(pcx, finish, vp_anchor))
		return 0;

	if (purge_ctx_anchor_is_set(pcx, vp_anchor)) {

		D__DEBUG(DB_EPC, "Probing from existing %s iterator\n",
			pcx_name(pcx));
		opc = ITR_REUSE_ANCHOR;
		purge_ctx_anchor_ctl(pcx, vp_anchor, &anchor, ANCHOR_COPY);
	} else {
		opc = ITR_PROBE_FIRST;
	}

	rc = vos_iter_prepare(pcx->pc_type, &pcx->pc_param, &ih);
	if (rc == -DER_NONEXIST) {
		D__DEBUG(DB_EPC, "Exit from empty :%s\n", pcx_name(pcx));
		return 0;
	}

	if (rc != 0) {
		D__ERROR("Failed to create %s iterator: %d\n",
			pcx_name(pcx), rc);
		return rc;
	}

	if (val_tree) {
		/** prepare the max iterator */
		rc = vos_iter_prepare(pcx->pc_type, &pcx->pc_param, &ih_max);
		if (rc == -DER_NONEXIST) {
			D__DEBUG(DB_EPC, "Exit from empty %s.\n",
				 pcx_name(pcx));
			return 0;
		}

		if (rc != 0) {
			D__ERROR("Failed to create %s max_iterator: %d\n",
				pcx_name(pcx), rc);
			return rc;
		}
		memset(&ent_max, 0, sizeof(vos_iter_entry_t));
	}

	for (aggregated = found = 0; true;) {

		vos_iter_entry_t	ent;
		daos_handle_t		*del_hdl;
		char			*opstr;
		int			empty = 0;
		bool			max_reset = false;
		bool			it_first = (opc & ITR_PROBE_FIRST);
		bool			it_reuse = (opc & ITR_REUSE_ANCHOR);
		bool			it_next  = (opc & ITR_NEXT);
		bool			it_max   = (opc & ITR_MAX_PROBE_ANCHOR);

		if (it_first) {
			opstr = "probe_first";
			rc = vos_iter_probe(ih, NULL);

		} else if (it_next) {
			opstr = "next";
			rc = vos_iter_next(ih);
			/*
			 * Reset recx_completion flag on akey_next
			 * and reset akey_completion flag on dkey_next
			 */
			purge_ctx_reset_complete(pcx, vp_anchor);

		} else {
			/*
			 * ITR_PROBE_ANCHOR, ITR_MAX_PROBE_ANCHOR,
			 * ITR_REUSE_ANCHOR
			 */
			opstr = "probe_anchor";
			rc = vos_iter_probe(ih, &anchor);
		}

		/*
		 * Skip fetch while probing for max iterator
		 * after deleting max_iterator.
		 * Use the entry from previous fetch.
		 */
		if (rc == 0 && !it_max) {
			opstr = "fetch";
			rc = vos_iter_fetch(ih, &ent, &anchor);
		}

		if (rc == -DER_NONEXIST) {
			D__DEBUG(DB_EPC, "Finish %s iteration\n",
				 pcx_name(pcx));
			purge_ctx_anchor_ctl(pcx, vp_anchor, NULL,
					     ANCHOR_UNSET);
			purge_ctx_set_complete(pcx, finish, vp_anchor);
			rc = 0;
			break;
		}

		if (rc != 0) {
			D__ERROR("%s iterator failed to %s: %d\n",
				pcx_name(pcx), opstr, rc);
			D__GOTO(out, rc);
		}

		if (val_tree) {
			rc = recx_max_iter_probe(opc, &ent, &ent_max,
						 vp_anchor, &anchor,
						 pcx->pc_param.ip_epc_expr,
						 &ih_max);
			if (rc != 0)
				D__GOTO(out, rc);
		}

		if (!credits) {
			purge_ctx_anchor_ctl(pcx, vp_anchor, &anchor,
					     ANCHOR_SET);
			D__GOTO(out, rc);
		}

		/* Probing REUSED_ANCHOR should not be counted for credits */
		if (!it_max && !it_reuse) {
			found++;
			credits--;
		}

		if (pcx->pc_type == VOS_ITER_SINGLE) {
			/* Delete the record pointed to by regular iterator */
			empty = (ent_max.ie_epr.epr_lo != ent.ie_epr.epr_lo);
			/* Delete the record pinter by max iterator */
			max_reset = (ent_max.ie_epr.epr_lo < ent.ie_epr.epr_lo);
		} else {
			rc = purge_ctx_init(pcx, &ent);
			if (rc != 0) {
				D__DEBUG(DB_EPC,
					 "%s context enter failed :%d\n",
					 pcx_name(pcx), rc);
				D__GOTO(out, rc);
			}

			/* Enter the next level of tree until recx */
			rc = epoch_aggregate(pcx, &empty, &credits, vp_anchor,
					     NULL);
			purge_ctx_fini(pcx, rc);
			if (rc != 0)
				D__GOTO(out, rc);

			if (!credits) { /* credits used up by subtree return */
				purge_ctx_anchor_ctl(pcx, vp_anchor, &anchor,
						     ANCHOR_SET);
				D__GOTO(out, rc);
			}
		}

		if (!empty) {
			opc = ITR_NEXT;
			continue;
		}

		/*
		 * if current position is greater than max
		 * delete the max iterator and move to the next
		 * rec in epr.
		 *
		 * else: delete current pos if < max and probe.
		 */
		if (max_reset) {
			del_hdl = &ih_max;
			opc = ITR_MAX_PROBE_ANCHOR;
		} else {
			del_hdl = &ih;
			opc = ITR_PROBE_ANCHOR;
		}

		TX_BEGIN(pcx->pc_pop) {
			rc = vos_iter_delete(*del_hdl, NULL);
			if (rc != 0) {
				D__DEBUG(DB_EPC, "Failed to delete %s: %d\n",
					pcx_name(pcx), rc);
				pmemobj_tx_abort(rc);
			}
		} TX_ONABORT {
			rc = umem_tx_errno(rc);
			D__ERROR("failed to delete: %d\n", rc);
		} TX_END

		if (rc != 0)
			D__GOTO(out, rc);

		/* Number of keys aggregated in this tree ctx */
		aggregated++;
	}

	if (rc == 0 && empty_ret != NULL) {
		rc = vos_iter_empty(ih);
		*empty_ret = (rc == 1);
		rc = 0;
	}
out:
	if (rc == 0 && val_tree)
		vos_iter_finish(ih_max);

	*credits_ret = credits;
	D__DEBUG(DB_EPC,
		"aggregated %d, found: %d %s(s) rem credits: %u\n",
		aggregated, found, pcx_name(pcx), *credits_ret);

	vos_iter_finish(ih);
	return rc;
}

/*
 * Core function of discard, it can recursively enter different trees, and
 * delete the leaf record, or empty subtree.
 */
static int
epoch_discard(struct purge_context *pcx, int *empty_ret)
{
	daos_handle_t	ih;
	daos_hash_out_t	anchor;
	int		found;
	int		discarded;
	int		opc;
	int		rc;

	D__DEBUG(DB_EPC, "Enter %s iterator\n", pcx_name(pcx));

	rc = vos_iter_prepare(pcx->pc_type, &pcx->pc_param, &ih);
	if (rc == -DER_NONEXIST) { /* btree is uninitialized */
		D__DEBUG(DB_EPC, "Exit from empty %s.\n", pcx_name(pcx));
		return 0;
	}

	if (rc != 0) {
		D__ERROR("Failed to create %s iterator: %d\n",
			pcx_name(pcx), rc);
		return rc;
	}
	opc = ITR_PROBE_FIRST;

	for (discarded = found = 0; true; ) {
		char		 *opstr;
		int		  empty;
		vos_iter_entry_t  ent;

		if (opc == ITR_PROBE_FIRST) {
			opstr = "probe_first";
			rc = vos_iter_probe(ih, NULL);

		} else if (opc == ITR_PROBE_ANCHOR) {
			opstr = "probe_anchor";
			rc = vos_iter_probe(ih, &anchor);

		} else { /* ITR_NEXT */
			opstr = "next";
			rc = vos_iter_next(ih);
		}

		if (rc == 0) {
			opstr = "fetch";
			rc = vos_iter_fetch(ih, &ent, &anchor);
		}

		if (rc == -DER_NONEXIST) { /* no more entry, done */
			D__DEBUG(DB_EPC, "Finish %s iteration\n",
				 pcx_name(pcx));
			rc = 0;
			break;
		}

		if (rc != 0) {
			D__ERROR("%s iterator failed to %s: %d\n",
				pcx_name(pcx), opstr, rc);
			D__GOTO(out, rc);
		}

		found++;
		if (pcx->pc_type == VOS_ITER_SINGLE) {
			/* the last level tree */
			empty = !uuid_compare(ent.ie_cookie, pcx->pc_cookie);
		} else {
			/* prepare the context for the subtree */
			rc = purge_ctx_init(pcx, &ent);
			if (rc != 0) {
				D__DEBUG(DB_EPC, "%s context enter fail: %d\n",
					pcx_name(pcx), rc);
				D__GOTO(out, rc);
			}

			/* enter the subtree */
			rc = epoch_discard(pcx, &empty);
			/* exit from the context of subtree */
			purge_ctx_fini(pcx, rc);
			if (rc != 0)
				D__GOTO(out, rc);
		}

		if (!empty) { /* subtree or record is not empty */
			opc = ITR_NEXT;
			continue;
		}

		TX_BEGIN(pcx->pc_pop) {
			rc = vos_iter_delete(ih, NULL);
			if (rc != 0) {
				D__DEBUG(DB_EPC,
					"Failed to delete empty %s: %d\n",
					pcx_name(pcx), rc);
				pmemobj_tx_abort(rc);
			}
		} TX_ONABORT {
			rc = umem_tx_errno(rc);
			D__ERROR("failed to delete:%d\n", rc);
		} TX_END

		if (rc != 0)
			D__GOTO(out, rc);

		discarded++;
		/* need to probe again after the delete */
		opc = ITR_PROBE_ANCHOR;
	}
	D__DEBUG(DB_EPC, "Discard %d of %d %s(s)\n",
		discarded, found, pcx_name(pcx));

	if (rc == 0 && empty_ret != NULL) {
		rc = vos_iter_empty(ih);
		*empty_ret = (rc == 1);
		rc = 0; /* ignore the returned value of vos_iter_empty */
	}
out:
	vos_iter_finish(ih);
	return rc;
}

int
vos_epoch_discard(daos_handle_t coh, daos_epoch_range_t *epr, uuid_t cookie)
{
	struct vos_container	*cont = vos_hdl2cont(coh);
	struct purge_context	pcx;
	daos_epoch_t		max_epoch;
	int			rc;

	D__DEBUG(DB_EPC, "Epoch discard for "DF_UUID" ["DF_U64", "DF_U64"]\n",
		DP_UUID(cookie), epr->epr_lo, epr->epr_hi);

	rc = vos_cookie_find_update(cont->vc_pool->vp_cookie_th, cookie,
				    epr->epr_lo, false, &max_epoch);
	if (rc)
		return rc == -DER_NONEXIST ? 0 : rc;

	D__DEBUG(DB_EPC, "Max epoch of "DF_UUID" is "DF_U64"\n",
		DP_UUID(cookie), max_epoch);

	/** If this is the max epoch skip discard */
	if (max_epoch < epr->epr_lo) {
		D__DEBUG(DB_EPC, "Max Epoch < epr_lo.. skip discard\n");
		return 0;
	}

	memset(&pcx, 0, sizeof(pcx));
	pcx.pc_type	    = VOS_ITER_NONE;
	pcx.pc_param.ip_hdl = coh;
	pcx.pc_param.ip_epr = *epr;
	uuid_copy(pcx.pc_cookie, cookie);
	purge_set_iter_expr(&pcx, epr);

	rc = purge_ctx_init(&pcx, NULL);
	D__ASSERT(rc == 0);

	rc = epoch_discard(&pcx, NULL);
	purge_ctx_fini(&pcx, rc);
	return rc;
}

int
vos_epoch_aggregate(daos_handle_t coh, daos_unit_oid_t oid,
		    daos_epoch_range_t *epr, unsigned int *credits,
		    vos_purge_anchor_t *anchor, bool *finished)
{
	int			rc = 0;
	struct purge_context	pcx;
	vos_iter_entry_t	oid_entry;
	vos_cont_info_t		vc_info;

	if (daos_unit_oid_is_null(oid)) {
		vos_cont_set_purged_epoch(coh, epr->epr_hi);
		*finished = true;
		D__DEBUG(DB_EPC, "Setting the epoch in container\n");
		return 0;
	}

	D__DEBUG(DB_EPC, "Epoch aggregate for:"DF_OID" ["DF_U64"->"DF_U64"]\n",
		DP_OID(oid.id_pub), epr->epr_lo, epr->epr_hi);

	if (epr->epr_hi < epr->epr_lo) {
		D__ERROR("range::epr_lo cannot be lesser than range::epr_hi\n");
		return -DER_INVAL;
	}

	if (!purge_anchor_is_valid(anchor)) {
		D__ERROR("Invalid anchor provided\n");
		return -DER_INVAL;
	}

	*finished = false;
	if (purge_oid_is_aggregated(anchor, oid)) {
		*finished = true;
		D__DEBUG(DB_EPC,
			"Aggregation completion detected from anchor\n");
		return 0;
	}

	rc = vos_cont_query(coh, &vc_info);
	if (rc != 0)
		return rc;

	/** Check if this range is already aggregated */
	if (vc_info.pci_purged_epoch >= epr->epr_hi) {
		*finished = true;
		D__DEBUG(DB_EPC,
			"Aggregation completion detected from purge_epoch\n");
		return 0;
	}

	memset(&pcx, 0, sizeof(pcx));
	pcx.pc_type		= VOS_ITER_OBJ;
	pcx.pc_param.ip_hdl	= coh;
	pcx.pc_param.ip_epr	= *epr;
	purge_set_iter_expr(&pcx, epr);

	oid_entry.ie_oid	= oid;
	rc = purge_ctx_init(&pcx, &oid_entry);
	D__ASSERT(rc == 0);

	rc = epoch_aggregate(&pcx, NULL, credits, anchor, finished);
	purge_ctx_fini(&pcx, rc);
	return rc;
}
