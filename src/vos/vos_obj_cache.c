/**
 * (C) Copyright 2016-2024 Intel Corporation.
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
#include "vos_obj.h"
#include "vos_internal.h"
#include <daos_errno.h>

/**
 * Local type for VOS LRU key
 * VOS LRU key must consist of
 * Object ID and container UUID
 */
struct obj_lru_key {
	/* container the object belongs to */
	struct vos_container	*olk_cont;
	/* Object ID */
	daos_unit_oid_t		 olk_oid;
};

struct vos_obj_cache {
	/** The main object cache */
	struct daos_lru_cache *oc_lru;
	/** Temporary scratch space for holding an object before caching it. */
	struct vos_object      oc_scratch;
	/** For objects that are not cached with an expected short life such as
	 *  for rebuild or aggregation where we are working on one at at a time
	 *  and post many operations for the same object without a yield. Avoid
	 *  polluting the object cache for user I/O.
	 */
	struct vos_object     *oc_ephemeral;
	/** container for ephemeral object (for same oid case) */
	struct vos_container  *oc_eph_cont;
	/** Set this flag when using the ephemeral entry.  Due to the yield at
	 * vos_tx_end, another ult may need to drop the ephemeral entry from cache and
	 * notify the writer via a flag to free it.
	 */
	bool                   oc_eph_in_use;
};

static inline void
init_object(struct vos_object *obj, daos_unit_oid_t oid, struct vos_container *cont)
{
	/**
	 * Saving a copy of oid to avoid looking up in vos_obj_df, which
	 * is a direct pointer to pmem data structure
	 */
	obj->obj_id	= oid;
	obj->obj_cont	= cont;
	vos_cont_addref(cont);
	vos_ilog_fetch_init(&obj->obj_ilog_info);
}

static int
obj_lop_alloc(void *key, unsigned int ksize, void *args,
	      struct daos_llink **llink_p)
{
	struct vos_object	*obj;
	struct obj_lru_key	*lkey;
	struct vos_container	*cont;
	struct vos_tls		*tls;
	int			 rc;

	cont = (struct vos_container *)args;
	D_ASSERT(cont != NULL);

	tls = vos_tls_get(cont->vc_pool->vp_sysdb);
	lkey = (struct obj_lru_key *)key;
	D_ASSERT(lkey != NULL);

	D_DEBUG(DB_TRACE, "cont="DF_UUID", obj="DF_UOID"\n",
		DP_UUID(cont->vc_id), DP_UOID(lkey->olk_oid));

	D_ALLOC_PTR(obj);
	if (!obj)
		D_GOTO(failed, rc = -DER_NOMEM);

	init_object(obj, lkey->olk_oid, cont);
	d_tm_inc_gauge(tls->vtl_obj_cnt, 1);
	*llink_p = &obj->obj_llink;
	rc = 0;
failed:
	return rc;
}

static bool
obj_lop_cmp_key(const void *key, unsigned int ksize, struct daos_llink *llink)
{
	struct vos_object	*obj;
	struct obj_lru_key	*lkey = (struct obj_lru_key *)key;

	D_ASSERT(ksize == sizeof(struct obj_lru_key));

	obj = container_of(llink, struct vos_object, obj_llink);
	return lkey->olk_cont == obj->obj_cont &&
	       !memcmp(&lkey->olk_oid, &obj->obj_id, sizeof(obj->obj_id));
}

static uint32_t
obj_lop_rec_hash(struct daos_llink *llink)
{
	struct obj_lru_key	 lkey;
	struct vos_object	*obj;

	obj = container_of(llink, struct vos_object, obj_llink);

	/* Create the key for obj cache */
	lkey.olk_cont = obj->obj_cont;
	lkey.olk_oid  = obj->obj_id;

	return d_hash_string_u32((const char *)&lkey, sizeof(lkey));
}

static inline void
clean_object(struct vos_object *obj)
{
	vos_ilog_fetch_finish(&obj->obj_ilog_info);
	if (obj->obj_cont != NULL)
		vos_cont_decref(obj->obj_cont);

	obj_tree_fini(obj);
}

static void
obj_lop_free(struct daos_llink *llink)
{
	struct vos_object	*obj;
	struct vos_tls		*tls;

	D_DEBUG(DB_TRACE, "lru free callback for vos_obj_cache\n");

	obj = container_of(llink, struct vos_object, obj_llink);
	tls = vos_tls_get(obj->obj_cont->vc_pool->vp_sysdb);
	d_tm_dec_gauge(tls->vtl_obj_cnt, 1);
	clean_object(obj);
	D_FREE(obj);
}

static void
obj_lop_print_key(void *key, unsigned int ksize)
{
	struct obj_lru_key	*lkey = (struct obj_lru_key *)key;
	struct vos_container	*cont = lkey->olk_cont;

	D_DEBUG(DB_TRACE, "pool="DF_UUID" cont="DF_UUID", obj="DF_UOID"\n",
		DP_UUID(cont->vc_pool->vp_id),
		DP_UUID(cont->vc_id), DP_UOID(lkey->olk_oid));
}

static struct daos_llink_ops obj_lru_ops = {
	.lop_free_ref	= obj_lop_free,
	.lop_alloc_ref	= obj_lop_alloc,
	.lop_cmp_keys	= obj_lop_cmp_key,
	.lop_rec_hash	= obj_lop_rec_hash,
	.lop_print_key	= obj_lop_print_key,
};

int
vos_obj_cache_create(int32_t cache_size, struct vos_obj_cache **occp)
{
	int	rc;
	struct vos_obj_cache *occ = NULL;

	D_ASSERT(occp != NULL);

	D_ALLOC_PTR(occ);
	if (occ == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_DEBUG(DB_TRACE, "Creating an object cache %d\n", (1 << cache_size));
	rc = daos_lru_cache_create(cache_size, D_HASH_FT_NOLOCK, &obj_lru_ops, &occ->oc_lru);
	if (rc) {
		D_ERROR("Error in creating lru cache: "DF_RC"\n", DP_RC(rc));
		D_FREE(occ);
	}
out:
	*occp = occ;

	return rc;
}

void
vos_obj_cache_destroy(struct vos_obj_cache *occ)
{
	D_ASSERT(occ != NULL);
	daos_lru_cache_destroy(occ->oc_lru);
	if (occ->oc_eph_cont != NULL) {
		occ->oc_ephemeral->obj_cont = NULL;
		clean_object(occ->oc_ephemeral);
		D_FREE(occ->oc_ephemeral);
	}
	D_FREE(occ);
}

static bool
obj_cache_evict_cond(struct daos_llink *llink, void *args)
{
	struct vos_container	*cont = (struct vos_container *)args;
	struct vos_object	*obj;

	if (cont == NULL)
		return true;

	obj = container_of(llink, struct vos_object, obj_llink);
	return obj->obj_cont == cont;
}

void
vos_obj_cache_evict(struct vos_obj_cache *cache, struct vos_container *cont)
{
	daos_lru_cache_evict(cache->oc_lru, obj_cache_evict_cond, cont);
}

/**
 * Return object cache for the current IO.
 */
struct vos_obj_cache *
vos_obj_cache_current(bool standalone)
{
	return vos_obj_cache_get(standalone);
}

void
vos_obj_release(struct vos_obj_cache *occ, struct vos_object *obj, bool evict)
{
	D_ASSERT((occ != NULL) && (obj != NULL));

	if (obj == &occ->oc_scratch) {
		clean_object(obj);
		memset(obj, 0, sizeof(*obj));
		return;
	}

	if (obj->obj_dropped) {
		D_FREE(obj);
		return;
	}

	if (obj->obj_ephemeral) {
		if (evict) {
			clean_object(obj);
			D_FREE(obj);
			occ->oc_eph_cont = NULL;
		}
		occ->oc_eph_in_use = false;
		return;
	}

	if (evict)
		daos_lru_ref_evict(occ->oc_lru, &obj->obj_llink);

	daos_lru_ref_release(occ->oc_lru, &obj->obj_llink);
}

int
vos_obj_discard_hold(struct vos_obj_cache *occ, struct vos_container *cont, daos_unit_oid_t oid,
		     struct vos_object **objp)
{
	struct vos_object	*obj = NULL;
	daos_epoch_range_t	 epr = {0, DAOS_EPOCH_MAX};
	int			 rc;

	rc = vos_obj_hold(occ, cont, oid, &epr, 0, VOS_OBJ_DISCARD,
			  DAOS_INTENT_DISCARD, &obj, NULL);
	if (rc != 0)
		return rc;

	D_ASSERTF(!obj->obj_discard, "vos_obj_hold should return an error if already in discard\n");

	obj->obj_discard = 1;
	*objp = obj;

	return 0;
}

void
vos_obj_discard_release(struct vos_obj_cache *occ, struct vos_object *obj)
{
	obj->obj_discard = 0;

	vos_obj_release(occ, obj, false);
}

enum {
	VOS_OBJ_EPH_NONE = 0,
	VOS_OBJ_EPH_DROP = 1,
	VOS_OBJ_EPH_FREE = 2,
};

static inline void
move_object(struct vos_object *obj_new, struct vos_object *obj_old, int action)
{
	/* This object should not be cached */
	D_ASSERT(obj_new->obj_df == NULL);

	vos_ilog_fetch_move(&obj_new->obj_ilog_info, &obj_old->obj_ilog_info);
	obj_new->obj_toh        = obj_old->obj_toh;
	obj_new->obj_ih         = obj_old->obj_ih;
	obj_new->obj_sync_epoch = obj_old->obj_sync_epoch;
	obj_new->obj_df         = obj_old->obj_df;
	obj_new->obj_zombie     = obj_old->obj_zombie;
	obj_old->obj_toh        = DAOS_HDL_INVAL;
	obj_old->obj_ih         = DAOS_HDL_INVAL;
	clean_object(obj_old);
	switch (action) {
	default:
		D_ASSERTF(0, "Invalid action: %d\n", action);
	case VOS_OBJ_EPH_NONE:
		memset(obj_old, 0, sizeof(*obj_old));
		break;
	case VOS_OBJ_EPH_DROP:
		obj_old->obj_dropped = 1;
		break;
	case VOS_OBJ_EPH_FREE:
		D_FREE(obj_old);
	}
}

/** Move an object to the lru cache */
static inline int
cache_object(struct vos_obj_cache *occ, bool ephemeral, struct vos_object *obj_old,
	     struct vos_object **objp)
{
	struct vos_object       *obj_new = NULL;
	struct daos_llink	*lret;
	struct obj_lru_key	 lkey;
	struct vos_object       *old_eph = NULL;
	int                      rc      = 0;
	int                      action  = VOS_OBJ_EPH_NONE;

	*objp = NULL;

	D_ASSERT(obj_old->obj_cont != NULL);

	if (obj_old->obj_ephemeral) {
		/* If it's in use, we are taking it over, otherwise, we are
		 * replacing it and need to free the old entry
		 */
		action = occ->oc_eph_in_use ? VOS_OBJ_EPH_DROP : VOS_OBJ_EPH_FREE;
	} else if (occ->oc_eph_cont != NULL) {
		old_eph = occ->oc_ephemeral; /* If we replace, we will need to clean this one */
	}

	if (ephemeral) {
		D_ALLOC_PTR(obj_new);
		if (obj_new == NULL)
			rc = -DER_NOMEM;
		else
			init_object(obj_new, obj_old->obj_id, obj_old->obj_cont);
	} else {
		lkey.olk_cont = obj_old->obj_cont;
		lkey.olk_oid  = obj_old->obj_id;

		rc = daos_lru_ref_hold(occ->oc_lru, &lkey, sizeof(lkey), obj_old->obj_cont, &lret);
		if (rc == 0) {
			/** Object is in cache */
			obj_new = container_of(lret, struct vos_object, obj_llink);
		}
	}

	if (rc != 0) {
		if (!obj_old->obj_ephemeral) {
			clean_object(obj_old);
			memset(obj_old, 0, sizeof(*obj_old));
		}
		return rc;
	}

	move_object(obj_new, obj_old, action);

	if (ephemeral) {
		if (old_eph) {
			clean_object(old_eph);
			if (occ->oc_eph_in_use)
				old_eph->obj_dropped = 1;
			else
				D_FREE(old_eph);
		}
		occ->oc_eph_in_use     = true;
		occ->oc_eph_cont       = obj_new->obj_cont;
		obj_new->obj_ephemeral = 1;
		occ->oc_ephemeral      = obj_new;

	} else if (obj_old == occ->oc_ephemeral) {
		occ->oc_eph_cont       = NULL;
		obj_new->obj_ephemeral = 0;
	}

	*objp = obj_new;

	return 0;
}

int
vos_obj_hold(struct vos_obj_cache *occ, struct vos_container *cont, daos_unit_oid_t oid,
	     daos_epoch_range_t *epr, daos_epoch_t bound, uint64_t flags, uint32_t intent,
	     struct vos_object **obj_p, struct vos_ts_set *ts_set)
{
	struct vos_object	*obj;
	struct daos_llink	*lret;
	struct obj_lru_key	 lkey;
	int			 rc = 0;
	int			 tmprc;
	uint32_t		 cond_mask = 0;
	bool			 create;
	bool                     ephemeral;
	void			*create_flag = NULL;
	bool			 visible_only;

	D_ASSERT(cont != NULL);
	D_ASSERT(cont->vc_pool);

	*obj_p = NULL;

	if (cont->vc_pool->vp_dying)
		return -DER_SHUTDOWN;

	ephemeral    = flags & VOS_OBJ_EPHEMERAL;
	create = flags & VOS_OBJ_CREATE;
	visible_only = flags & VOS_OBJ_VISIBLE;
	/** ephemeral can only be used for non-conditional writes */
	D_ASSERT((ephemeral && create) || !ephemeral);

	/** Pass NULL as the create_args if we are not creating the object or the current
	 *  object is ephemeral so we avoid evicting an entry until we need to.
	 */
	if (create && !ephemeral)
		create_flag = cont;

	D_DEBUG(DB_TRACE, "Try to hold cont="DF_UUID", obj="DF_UOID
		" layout %u create=%s epr="DF_X64"-"DF_X64"\n",
		DP_UUID(cont->vc_id), DP_UOID(oid), oid.id_layout_ver,
		create ? "true" : "false", epr->epr_lo, epr->epr_hi);

	/* Create the key for obj cache */
	lkey.olk_cont = cont;
	lkey.olk_oid = oid;

	/** Note: Ephemeral entries may only be used by writers for
	 * unconditional operations.  Writes only hold the object for a single
	 * VOS transaction. If the ephemeral entry is in use here, it means that
	 * the writer has yielded during or after the commit phase of the
	 * transaction.  At this point, the writer is actually done with the
	 * object and can free it when done with the transaction.  For
	 * simplicity, any time there is sharing of the same object, the new ULT
	 * will allocate a new cache entry, ephemeral or otherwise, take over
	 * the contents of the object, and inform the user to free it.
	 */
	if (occ->oc_ephemeral != NULL && occ->oc_eph_cont == cont &&
	    !memcmp(&occ->oc_ephemeral->obj_id, &oid, sizeof(oid))) {
		if (ephemeral) {
			obj = occ->oc_ephemeral;
			if (occ->oc_eph_in_use) {
				rc = cache_object(occ, true, occ->oc_ephemeral, &obj);
				if (rc != 0)
					goto failed_2;
			}
			occ->oc_eph_in_use = true;
			goto ilog;
		}

		/** If new change is not ephemeral, let's just take over the entry and
		 * put it in the cache */
		rc = cache_object(occ, false, occ->oc_ephemeral, &obj);
		if (rc != 0)
			goto failed_2;
		goto check;
	}

	rc = daos_lru_ref_hold(occ->oc_lru, &lkey, sizeof(lkey), create_flag, &lret);
	if (rc == -DER_NONEXIST) {
		D_ASSERT(occ->oc_scratch.obj_cont == NULL);
		obj = &occ->oc_scratch;
		init_object(obj, oid, cont);
	} else if (rc != 0) {
		D_GOTO(failed_2, rc);
	} else {
		/** Object is in cache */
		obj = container_of(lret, struct vos_object, obj_llink);
	}

	if (obj->obj_zombie)
		D_GOTO(failed, rc = -DER_AGAIN);

check:
	if (intent == DAOS_INTENT_KILL && !(flags & VOS_OBJ_KILL_DKEY)) {
		if (!obj->obj_ephemeral && obj != &occ->oc_scratch) {
			if (!daos_lru_is_last_user(&obj->obj_llink))
				D_GOTO(failed, rc = -DER_BUSY);

			vos_obj_evict(occ, obj);
		}
		/* no one else can hold it */
		obj->obj_zombie = 1;
		if (obj->obj_df)
			goto out; /* Ok to delete */
	}

ilog:
	if (obj->obj_df) {
		D_DEBUG(DB_TRACE, "looking up object ilog");
		if (create || intent == DAOS_INTENT_PUNCH)
			vos_ilog_ts_ignore(vos_obj2umm(obj),
					   &obj->obj_df->vo_ilog);
		tmprc = vos_ilog_ts_add(ts_set, &obj->obj_df->vo_ilog,
					&oid, sizeof(oid));
		D_ASSERT(tmprc == 0); /* Non-zero only valid for akey */
		goto check_object;
	}

	 /* newly cached object */
	D_DEBUG(DB_TRACE, "%s Got empty obj "DF_UOID" epr="DF_X64"-"DF_X64"\n",
		create ? "find/create" : "find", DP_UOID(oid), epr->epr_lo,
		epr->epr_hi);

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

check_object:
	if (obj->obj_discard && (create || (flags & VOS_OBJ_DISCARD) != 0)) {
		/** Cleanup before assert so unit test that triggers doesn't corrupt the state */
		vos_obj_release(occ, obj, false);
		/* Update request will retry with this error */
		rc = -DER_UPDATE_AGAIN;
		goto failed_2;
	}

	if ((flags & VOS_OBJ_DISCARD) || intent == DAOS_INTENT_KILL || intent == DAOS_INTENT_PUNCH)
		goto out;

	if (!create) {
		rc = vos_ilog_fetch(vos_cont2umm(cont), vos_cont2hdl(cont),
				    intent, &obj->obj_df->vo_ilog, epr->epr_hi,
				    bound, false, /* has_cond: no object level condition. */
				    NULL, NULL, &obj->obj_ilog_info);
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
	    vos_dth_get(obj->obj_cont->vc_pool->vp_sysdb) != NULL &&
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

	if (obj == &occ->oc_scratch) {
		/** Ok, it's successful, go ahead and cache the object. */
		rc = cache_object(occ, ephemeral, obj, &obj);
		if (rc != 0)
			goto failed_2;
	}

	*obj_p = obj;

	return 0;
failed:
	vos_obj_release(occ, obj, true);
failed_2:
	VOS_TX_LOG_FAIL(rc, "failed to hold object, rc="DF_RC"\n", DP_RC(rc));

	return	rc;
}

void
vos_obj_evict(struct vos_obj_cache *occ, struct vos_object *obj)
{
	if (obj == &occ->oc_scratch)
		return;
	daos_lru_ref_evict(occ->oc_lru, &obj->obj_llink);
}

int
vos_obj_evict_by_oid(struct vos_obj_cache *occ, struct vos_container *cont, daos_unit_oid_t oid)
{
	struct obj_lru_key	 lkey;
	struct daos_llink	*lret;
	int			 rc;

	lkey.olk_cont = cont;
	lkey.olk_oid = oid;

	rc = daos_lru_ref_hold(occ->oc_lru, &lkey, sizeof(lkey), NULL, &lret);
	if (rc == 0) {
		daos_lru_ref_evict(occ->oc_lru, lret);
		daos_lru_ref_release(occ->oc_lru, lret);
	}

	return rc == -DER_NONEXIST ? 0 : rc;
}
