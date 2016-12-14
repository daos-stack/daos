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
 * vos/vos_discard.c
 */
#define DD_SUBSYS	DD_FAC(vos)

#include <daos/btree.h>
#include <daos_srv/vos.h>
#include <vos_internal.h>

/** context of epoch discard */
struct discard_context {
	/** reference on the object to be checked */
	struct vos_obj_ref	*dc_obj;
	/** the current iterator type */
	vos_iter_type_t		 dc_type;
	/** cookie to discard */
	uuid_t			 dc_cookie;
	/** recursive iterator parameters */
	vos_iter_param_t	 dc_param;
};

#define dcx_name(dcx)		vos_iter_type2name((dcx)->dc_type)

static int
discard_ctx_init(struct discard_context *dcx, vos_iter_entry_t *ent)
{
	vos_iter_param_t *param = &dcx->dc_param;
	int		  rc	= 0;

	switch (dcx->dc_type) {
	default:
	case VOS_ITER_RECX:
		D_ASSERT(0);
	case VOS_ITER_NONE:
		dcx->dc_type = VOS_ITER_OBJ;
		break;

	case VOS_ITER_OBJ:
		rc = vos_obj_ref_hold(vos_obj_cache_current(), param->ip_hdl,
				      ent->ie_oid, &dcx->dc_obj);
		if (rc != 0)
			break;

		param->ip_oid = ent->ie_oid;
		daos_iov_set(&param->ip_dkey, NULL, 0);
		daos_iov_set(&param->ip_akey, NULL, 0);
		dcx->dc_type = VOS_ITER_DKEY;
		break;

	case VOS_ITER_DKEY:
		param->ip_dkey = ent->ie_key;
		daos_iov_set(&param->ip_akey, NULL, 0);
		dcx->dc_type = VOS_ITER_AKEY;
		break;

	case VOS_ITER_AKEY:
		param->ip_akey = ent->ie_key;
		dcx->dc_type = VOS_ITER_RECX;
		break;

	}
	D_DEBUG(DB_EPC, "Initialized %s iterator context: %d.\n",
		dcx_name(dcx), rc);
	return rc;
}

static void
discard_ctx_fini(struct discard_context *dcx, int rc)
{
	D_DEBUG(DB_EPC, "Finalize %s iterator context: %d.\n",
		dcx_name(dcx), rc);

	switch (dcx->dc_type) {
	default:
		D_ASSERT(0);
	case VOS_ITER_OBJ:
		dcx->dc_type = VOS_ITER_NONE;
		return;

	case VOS_ITER_DKEY:
		D_ASSERT(dcx->dc_obj != NULL);
		vos_obj_ref_release(vos_obj_cache_current(), dcx->dc_obj);
		dcx->dc_obj  = NULL;
		dcx->dc_type = VOS_ITER_OBJ;
		return;

	case VOS_ITER_AKEY:
		dcx->dc_type = VOS_ITER_DKEY;
		return;

	case VOS_ITER_RECX:
		dcx->dc_type = VOS_ITER_AKEY;
		return;
	}
}

enum { /* iterator operation code */
	ITR_NEXT,
	ITR_PROBE_FIRST,
	ITR_PROBE_ANCHOR,
};

/**
 * Core function of discard, it can recursively enter different trees, and
 * delete the leaf record, or empty subtree.
 */
static int
epoch_discard(struct discard_context *dcx, int *empty_ret)
{
	daos_handle_t	ih;
	daos_hash_out_t	anchor;
	int		found;
	int		discarded;
	int		opc;
	int		rc;

	D_DEBUG(DB_EPC, "Enter %s iterator\n", dcx_name(dcx));

	rc = vos_iter_prepare(dcx->dc_type, &dcx->dc_param, &ih);
	if (rc == -DER_NONEXIST) { /* btree is uninitialized */
		D_DEBUG(DB_EPC, "Exit from empty %s.\n", dcx_name(dcx));
		return 0;
	}

	if (rc != 0) {
		D_ERROR("Failed to create %s iterator: %d\n",
			dcx_name(dcx), rc);
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
			D_DEBUG(DB_EPC, "Finish %s iteration\n", dcx_name(dcx));
			rc = 0;
			break;
		}

		if (rc != 0) {
			D_ERROR("%s iterator failed to %s: %d\n",
				dcx_name(dcx), opstr, rc);
			D_GOTO(out, rc);
		}

		found++;
		if (dcx->dc_type == VOS_ITER_RECX) { /* the last level tree */
			empty = !uuid_compare(ent.ie_cookie, dcx->dc_cookie);
		} else {
			/* prepare the context for the subtree */
			rc = discard_ctx_init(dcx, &ent);
			if (rc != 0) {
				D_DEBUG(DB_EPC, "%s context enter failed: %d\n",
					dcx_name(dcx), rc);
				D_GOTO(out, rc);
			}
			/* enter the subtree */
			rc = epoch_discard(dcx, &empty);
			/* exit from the context of subtree */
			discard_ctx_fini(dcx, rc);
			if (rc != 0)
				D_GOTO(out, rc);
		}

		if (!empty) { /* subtree or record is not empty */
			opc = ITR_NEXT;
			continue;
		}

		rc = vos_iter_delete(ih);
		D_ASSERT(rc == 0 || rc != -DER_NONEXIST);
		if (rc != 0) {
			D_DEBUG(DB_EPC, "Failed to delete empty %s: %d\n",
				dcx_name(dcx), rc);
			D_GOTO(out, rc);
		}
		discarded++;
		/* need to probe again after the delete */
		opc = ITR_PROBE_ANCHOR;
	}
	D_DEBUG(DB_EPC, "Discard %d of %d %s(s)\n",
		discarded, found, dcx_name(dcx));

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
	struct discard_context	dcx;
	daos_epoch_t		max_epoch;
	int			rc;

	D_DEBUG(DB_EPC, "Epoch discard for "DF_UUID" ["DF_U64", "DF_U64"]\n",
		DP_UUID(cookie), epr->epr_lo, epr->epr_hi);

	if (epr->epr_hi != DAOS_EPOCH_MAX && epr->epr_hi != epr->epr_lo) {
		D_DEBUG(DB_EPC, "Cannot support epoch range\n");
		return -DER_INVAL;
	}

	rc = vos_cookie_find_update(vos_coh2cih(coh), cookie, epr->epr_lo,
				    false, &max_epoch);
	if (rc)
		return rc == -DER_NONEXIST ? 0 : rc;

	D_DEBUG(DB_EPC, "Max epoch of "DF_UUID" is "DF_U64"\n",
		DP_UUID(cookie), max_epoch);

	/** If this is the max epoch skip discard */
	if (max_epoch < epr->epr_lo) {
		D_DEBUG(DB_EPC, "Max Epoch < epr_lo.. skip discard\n");
		return 0;
	}

	memset(&dcx, 0, sizeof(dcx));
	dcx.dc_type	    = VOS_ITER_NONE;
	dcx.dc_param.ip_hdl = coh;
	dcx.dc_param.ip_epr = *epr;
	uuid_copy(dcx.dc_cookie, cookie);

	/**
	 * Setting appropriate epoch logic expression for recx iterator.
	 *
	 *  -- VOS_IT_EPC_EQ gurantees to probe and fetch only
	 *     records updated in this \a epr::epr_lo.
	 *
	 *  -- VOS_IT_EPC_GE on the other hand probes and fetches
	 *     all records from epr::epr_lo till DAOS_EPOCH_MAX.
	 *
	 *  -- probe and fetching of arbitrary ranges not natively
	 *     supported in iterater, so such ranges are not
	 *     supported in discard.
	 *
	 * Example:
	 * epr.lo = 1 epr.hi = 1 dicards epoch 1
	 * epr.lo = 1 epr.hi = DAOS_MAX_EPOCH, discards all
	 * obj records 1 -> DAOS_MAX_EPOCH
	 */
	if (epr->epr_lo == epr->epr_hi)
		dcx.dc_param.ip_epc_expr = VOS_IT_EPC_EQ;
	else
		dcx.dc_param.ip_epc_expr = VOS_IT_EPC_GE;

	rc = discard_ctx_init(&dcx, NULL);
	D_ASSERT(rc == 0);

	rc = epoch_discard(&dcx, NULL);
	discard_ctx_fini(&dcx, rc);
	return rc;
}
