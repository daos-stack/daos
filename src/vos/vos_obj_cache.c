/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Object cache for VOS OI table.
 * Object index is in Persistent memory. This cache in DRAM
 * maintains an LRU which is accessible in the I/O path. The object
 * index API defined for PMEM are used here by the cache..
 *
 * LRU cache implementation:
 * Simple LRU based object cache for Object index table
 * Uses a hashtable and a doubly linked list to set and get
 * entries. The size of both hashtable and linked list are
 * fixed length.
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#define D_LOGFAC	DD_FAC(vos)

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <vos_obj.h>
#include <vos_internal.h>
#include <daos_errno.h>

void
vos_obj_release(struct vos_object *obj)
{
	D_DEBUG(DB_TRACE, "Release "DF_UOID"\n", DP_UOID(obj->obj_id));

	vos_ilog_fetch_finish(&obj->obj_ilog_info);
	if (obj->obj_cont != NULL)
		vos_cont_decref(obj->obj_cont);

	obj_tree_fini(obj);
	D_FREE(obj);
}

int
vos_obj_hold(struct vos_container *cont, daos_unit_oid_t oid,
	     daos_epoch_range_t *epr, daos_epoch_t bound, uint64_t flags,
	     uint32_t intent, struct vos_object **obj_p,
	     struct vos_ts_set *ts_set)
{
	struct vos_object	*obj;
	int			 rc = 0;
	uint32_t		 cond_mask = 0;
	bool			 create;
	bool			 visible_only;

	D_ASSERT(cont != NULL);
	D_ASSERT(cont->vc_pool);

	*obj_p = NULL;

	if (cont->vc_pool->vp_dying)
		return -DER_SHUTDOWN;

	create = flags & VOS_OBJ_CREATE;
	visible_only = flags & VOS_OBJ_VISIBLE;

	D_DEBUG(DB_TRACE, "Try to hold cont="DF_UUID", obj="DF_UOID
		" create=%s epr="DF_X64"-"DF_X64"\n",
		DP_UUID(cont->vc_id), DP_UOID(oid),
		create ? "true" : "false", epr->epr_lo, epr->epr_hi);


	D_ALLOC_PTR(obj);
	if (obj == NULL)
		return -DER_NOMEM;

	obj->obj_id	= oid;
	obj->obj_cont	= cont;
	vos_cont_addref(cont);
	vos_ilog_fetch_init(&obj->obj_ilog_info);

	obj->obj_sync_epoch = 0;
	if (!create) {
		rc = vos_oi_find(cont, oid, &obj->obj_df, ts_set);
		if (rc == -DER_NONEXIST) {
			D_DEBUG(DB_TRACE, "non exist oid "DF_UOID"\n",
				DP_UOID(oid));
			goto failed;
		}
	} else {

		rc = vos_oi_find_alloc(cont, oid, epr->epr_hi, false,
				       &obj->obj_df, ts_set);
		D_ASSERT(rc || obj->obj_df);
	}

	if (rc != 0)
		goto failed;

	if (!obj->obj_df) {
		D_DEBUG(DB_TRACE, "nonexistent obj "DF_UOID"\n",
			DP_UOID(oid));
		D_GOTO(failed, rc = -DER_NONEXIST);
	}

	if (intent == DAOS_INTENT_KILL || intent == DAOS_INTENT_PUNCH)
		goto out;

	if (!create) {
		rc = vos_ilog_fetch(vos_cont2umm(cont), vos_cont2hdl(cont),
				    intent, &obj->obj_df->vo_ilog, epr->epr_hi,
				    bound, NULL, NULL, &obj->obj_ilog_info);
		if (rc != 0) {
			if (vos_has_uncertainty(ts_set, &obj->obj_ilog_info,
						epr->epr_hi, bound))
				rc = -DER_TX_RESTART;
			D_DEBUG(DB_TRACE, "Object "DF_UOID" not found at "
				DF_U64"\n", DP_UOID(oid), epr->epr_hi);
			goto failed;
		}

		rc = vos_ilog_check(&obj->obj_ilog_info, epr, epr,
				    visible_only);
		if (rc != 0) {
			D_DEBUG(DB_TRACE, "Object "DF_UOID" not visible at "
				DF_U64"-"DF_U64"\n", DP_UOID(oid), epr->epr_lo,
				epr->epr_hi);
			if (!vos_has_uncertainty(ts_set, &obj->obj_ilog_info,
						 epr->epr_hi, bound))
				goto failed;

			/** If the creation is uncertain, go ahead and fall
			 *  through as if the object exists so we can do
			 *  actual uncertainty check.
			 */
		}
		goto out;
	}

	/** If it's a conditional update, we need to preserve the -DER_NONEXIST
	 *  for the caller.
	 */
	if (ts_set && ts_set->ts_flags & VOS_COND_UPDATE_OP_MASK)
		cond_mask = VOS_ILOG_COND_UPDATE;
	rc = vos_ilog_update(cont, &obj->obj_df->vo_ilog, epr, bound, NULL,
			     &obj->obj_ilog_info, cond_mask, ts_set);
	if (rc == -DER_TX_RESTART)
		goto failed;
	if (rc == -DER_NONEXIST && cond_mask)
		goto out;
	if (rc != 0) {
		VOS_TX_LOG_FAIL(rc, "Could not update object "DF_UOID" at "
				DF_U64 ": "DF_RC"\n", DP_UOID(oid), epr->epr_hi,
				DP_RC(rc));
		goto failed;
	}

out:
	if (obj->obj_df != NULL)
		obj->obj_sync_epoch = obj->obj_df->vo_sync;

	if (obj->obj_df != NULL && epr->epr_hi <= obj->obj_sync_epoch &&
	    vos_dth_get() != NULL &&
	    (intent == DAOS_INTENT_PUNCH || intent == DAOS_INTENT_UPDATE)) {
		/* If someone has synced the object against the
		 * obj->obj_sync_epoch, then we do not allow to modify the
		 * object with old epoch. Let's ask the caller to retry with
		 * newer epoch.
		 *
		 * For rebuild case, the @dth will be NULL.
		 */
		D_ASSERT(obj->obj_sync_epoch > 0);

		D_INFO("Refuse %s obj "DF_UOID" because of the epoch "DF_U64
		       " is not newer than the sync epoch "DF_U64"\n",
		       intent == DAOS_INTENT_PUNCH ? "punch" : "update",
		       DP_UOID(oid), epr->epr_hi, obj->obj_sync_epoch);
		D_GOTO(failed, rc = -DER_TX_RESTART);
	}

	*obj_p = obj;

	return 0;
failed:
	vos_obj_release(obj);
	VOS_TX_LOG_FAIL(rc, "failed to hold object, rc="DF_RC"\n", DP_RC(rc));
	return	rc;
}
