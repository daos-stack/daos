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

#include <daos/btree.h>
#include <daos_srv/vos.h>
#include <vos_internal.h>

/*
 * DISCARD ERR HANDLE macro
 * {error-code, error-string, exit-label}
 * Common error handing, for iter cursor operations
 * made to a macro to reduce code-duplication
 */
#define DISCARD_ERR_HANDLE(rc, str, exit) {\
	if (rc == -DER_NONEXIST) { \
		D_DEBUG(DF_VOS3, "%s returned %d\n", str, rc);\
		D_GOTO(exit, rc = 0); \
	} \
	if (rc != 0) {	\
		D_ERROR("%s failed with error: %d\n", str, rc);\
		D_GOTO(exit, rc);\
	} \
}

static int
delete_and_probe(daos_handle_t ih, daos_hash_out_t *anchor)
{
	int	rc = 0;

	rc = vos_iter_delete(ih);
	if (rc != 0)
		return rc;
	/**
	 * Set the iterator cursor to next iterator
	 * as now iterator would search for
	 * GE
	 */
	rc = vos_iter_probe(ih, anchor);
	return rc;
}


static int
recx_iterate_and_discard(struct vos_obj_ref *oref, vos_iter_param_t *param,
			 uuid_t cookie)
{
	daos_handle_t	ih;
	int		rc = 0;

	rc = vos_iter_prepare(VOS_ITER_RECX, param, &ih);
	if (rc != 0) {
		D_ERROR("Failed to create recx iterator: %d\n", rc);
		return rc;
	}

	rc = vos_iter_probe(ih, NULL);
	DISCARD_ERR_HANDLE(rc, "recx_iter_probe", out)

	while (rc == 0) {
		vos_iter_entry_t	ent;
		daos_hash_out_t		anchor;
		struct daos_uuid	f_cookie;

		rc = vos_iter_fetch_cookie(ih, &ent, &f_cookie,
					   &anchor);
		DISCARD_ERR_HANDLE(rc, "recx_iter_fetch_cookie", out);

		/**
		 * If the recx cookie is the cookie to be discarded
		 * delete and probe to the next recx entry
		 */
		if (uuid_compare(f_cookie.uuid, cookie) == 0) {
			D_DEBUG(DF_VOS3,
				"del rid:%u, cookie:"DF_UUIDF", e: "DF_U64"\n",
			       (unsigned int)ent.ie_recx.rx_idx,
			       DP_UUID(f_cookie.uuid),
			       ent.ie_epr.epr_lo);

			rc = delete_and_probe(ih, &anchor);
			DISCARD_ERR_HANDLE(rc, "recx delete and probe",
					   out);
			continue;
		}
		rc = vos_iter_next(ih);
		DISCARD_ERR_HANDLE(rc, "recx_iter_next", out);
	}
out:
	vos_iter_finish(ih);
	return rc;
}

static int
akey_iterate_and_discard(struct vos_obj_ref *oref, vos_iter_param_t *param,
			 uuid_t cookie, daos_handle_t vec_toh)
{
	daos_handle_t	ih;
	daos_handle_t	toh = DAOS_HDL_INVAL;
	int		rc  = 0;

	rc = vos_iter_prepare(VOS_ITER_AKEY, param, &ih);
	if (rc != 0) {
		D_ERROR("Failed to create akey iterator: %d\n", rc);
		return rc;
	}

	rc = vos_iter_probe(ih, NULL);
	DISCARD_ERR_HANDLE(rc, "akey_iter_probe", out);

	while (rc == 0) {
		vos_iter_entry_t	ent;
		daos_hash_out_t		anchor;

		/** reset handle for next akey */
		if (!daos_handle_is_inval(toh)) {
			tree_release(toh);
			toh = DAOS_HDL_INVAL;
		}

		rc = vos_iter_fetch(ih, &ent, &anchor);
		DISCARD_ERR_HANDLE(rc, "akey_iter_fetch", out);

		/**
		 * Open the recx subtree, used for determining
		 * condition for deletion
		 */
		rc = tree_prepare(oref, vec_toh, &ent.ie_key, true, &toh);
		if (rc != 0)
			D_GOTO(out, rc);

		/** Set current akey */
		param->ip_akey = ent.ie_key;

		/** Iterate through all recx within this akey */
		rc = recx_iterate_and_discard(oref, param, cookie);
		if (rc != 0)
			D_GOTO(out, rc);

		/**
		 * If current recx sub-tree is empty delete this akey-node
		 * and probe to the next akey.
		 */
		if (vos_subtree_is_empty(toh)) {
			D_DEBUG(DF_VOS3, "Recx Tree empty delete akey: %s\n",
			       (char *)param->ip_akey.iov_buf);

			rc = delete_and_probe(ih, &anchor);
			DISCARD_ERR_HANDLE(rc, "akey_delete_and_probe", out);

			continue;
		}
		rc = vos_iter_next(ih);
		DISCARD_ERR_HANDLE(rc, "akey_iter_next", out)
	}
out:
	if (!daos_handle_is_inval(toh))
		tree_release(toh);
	vos_iter_finish(ih);
	return rc;
}

static int
dkey_iterate_and_discard(struct vos_obj_ref *oref, vos_iter_param_t *param,
			 uuid_t cookie)
{
	daos_handle_t	ih;
	daos_handle_t	vec_toh = DAOS_HDL_INVAL;
	int		rc	= 0;


	rc = vos_iter_prepare(VOS_ITER_DKEY, param, &ih);
	if (rc != 0) {
		D_ERROR("Failed to create dkey iterator: %d\n", rc);
		return rc;
	}

	rc = vos_iter_probe(ih, NULL);
	DISCARD_ERR_HANDLE(rc, "dkey_iter_probe", out);

	while (rc == 0) {
		vos_iter_entry_t  ent;
		daos_hash_out_t	  anchor;

		/** reset handle for next dkey */
		if (!daos_handle_is_inval(vec_toh)) {
			tree_release(vec_toh);
			vec_toh = DAOS_HDL_INVAL;
		}

		rc = vos_iter_fetch(ih, &ent, &anchor);
		DISCARD_ERR_HANDLE(rc, "dkey_iter_fetch", out);

		/**
		 * Open the akey subtree, used for determining
		 * condition for deletion
		 */
		rc = tree_prepare(oref, oref->or_toh, &ent.ie_key, true,
				  &vec_toh);
		if (rc != 0)
			D_GOTO(out, rc);

		/** Set next dkey */
		param->ip_dkey = ent.ie_key;

		/** Reset akey for next dkey  */
		daos_iov_set(&param->ip_akey, NULL, 0);

		/** Iterate through all akey within this dkey */
		rc = akey_iterate_and_discard(oref, param, cookie, vec_toh);
		if (rc != 0)
			D_GOTO(out, rc);

		/**
		 * If akey subtree is empty delete this dkey node and
		 * probe to the next node
		 */
		if (vos_subtree_is_empty(vec_toh)) {

			D_DEBUG(DF_VOS3, "akey Tree empty delete dkey: %s\n",
			       (char *)param->ip_dkey.iov_buf);

			rc  = delete_and_probe(ih, &anchor);
			DISCARD_ERR_HANDLE(rc, "dkey_delete_and_probe", out);

			continue;
		}
		rc = vos_iter_next(ih);
		DISCARD_ERR_HANDLE(rc, "dkey_iter_next", out);
	}
out:
	if (!daos_handle_is_inval(vec_toh))
		tree_release(vec_toh);
	vos_iter_finish(ih);
	return rc;
}

int
vos_epoch_discard(daos_handle_t coh, daos_epoch_range_t *epr,
		  uuid_t cookie)
{

	daos_handle_t		ih;
	daos_handle_t		cih	  = vos_coh2cih(coh);
	daos_epoch_t		max_epoch = 0;
	struct vos_obj_ref	*oref	  = NULL;
	int			rc	  = 0;
	vos_iter_param_t	param;

	if (epr->epr_hi != DAOS_EPOCH_MAX && epr->epr_hi != epr->epr_lo) {
		D_DEBUG(DF_VOS1, "Cannot support range discard lo="DF_U64
				 "hi="DF_U64".\n", epr->epr_lo, epr->epr_hi);
		return -DER_INVAL;
	}

	rc = vos_cookie_find_update(cih, cookie, epr->epr_lo,
				    false, &max_epoch);
	if (rc)
		return rc == -DER_NONEXIST ? 0 : rc;

	D_DEBUG(DF_VOS2, "Max epoch: "DF_U64", epoch_low: "DF_U64", cookie "
		DF_UUID"\n", max_epoch, epr->epr_lo, DP_UUID(cookie));

	/** If this is the max epoch skip discard */
	if (max_epoch < epr->epr_lo) {
		D_DEBUG(DF_VOS2, "Max Epoch < epr_lo.. skip discard\n");
		return 0;
	}

	D_DEBUG(DF_VOS2, "Epoch discard for epoch range high: %u, low: %u\n",
		(unsigned int)epr->epr_hi, (unsigned int) epr->epr_lo);

	memset(&param, 0, sizeof(param));
	param.ip_hdl		= coh;
	param.ip_epr		= *epr;

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
		param.ip_epc_expr = VOS_IT_EPC_EQ;
	else
		param.ip_epc_expr = VOS_IT_EPC_GE;

	rc = vos_iter_prepare(VOS_ITER_OBJ, &param, &ih);
	if (rc != 0) {
		D_ERROR("Failed to prepare VOS obj iter\n");
		return rc;
	}

	rc = vos_iter_probe(ih, NULL);
	DISCARD_ERR_HANDLE(rc, "oid_iter_probe", exit);

	while (rc == 0) {
		vos_iter_entry_t	ent;
		daos_hash_out_t		anchor;
		bool			obj_new = false;

		if (oref != NULL)
			vos_obj_ref_release(vos_obj_cache_current(), oref);

		/**
		 *  Oref may or may not be free'd on release
		 *  depending on refcount.
		 *  reset oref for next oid iteration.
		 */
		oref = NULL;

		rc = vos_iter_fetch(ih, &ent, &anchor);
		DISCARD_ERR_HANDLE(rc, "oid_iter_fetch", exit);

		D_DEBUG(DF_VOS3, "Object ID: "DF_UOID"\n", DP_UOID(ent.ie_oid));

		rc = vos_obj_ref_hold(vos_obj_cache_current(), coh, ent.ie_oid,
				      &oref);
		if (rc != 0)
			D_GOTO(exit, rc);

		if (vos_obj_is_new(oref->or_obj)) {
			D_DEBUG(DF_VOS2, "New obj, nothing to iterate..\n");
			obj_new = true;
		}

		/** If obj is new skip and move to next oid */
		if (!obj_new) {

			/** Set current valid OID */
			param.ip_oid = ent.ie_oid;

			/** Reset Akey and dkey for new object */
			daos_iov_set(&param.ip_dkey, NULL, 0);
			daos_iov_set(&param.ip_akey, NULL, 0);

			rc = vos_obj_tree_init(oref);
			if (rc != 0)
				D_GOTO(exit, rc);

			/** Iterate through all akey within this dkey **/
			rc = dkey_iterate_and_discard(oref, &param, cookie);
			if (rc != 0)
				D_GOTO(exit, rc);

			/**
			 * If dkey tree is empty delete oid and probe to the
			 * next oid
			 */
			if (vos_subtree_is_empty(oref->or_toh)) {
				rc  = delete_and_probe(ih, &anchor);
				DISCARD_ERR_HANDLE(rc, "oid_delete_probe", exit)
				/**
				 * If probe is successful continue to next
				 * iteration
				 */
				continue;
			}
		}
		rc = vos_iter_next(ih);
		DISCARD_ERR_HANDLE(rc, "oid_iter_next", exit);
	}
exit:
	if (oref != NULL)
		vos_obj_ref_release(vos_obj_cache_current(), oref);

	vos_iter_finish(ih);
	return rc;
}
