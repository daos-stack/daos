/**
 * (C) Copyright 2016-2022 Intel Corporation.
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
	int			 rc;

	cont = (struct vos_container *)args;
	D_ASSERT(cont != NULL);

	lkey = (struct obj_lru_key *)key;
	D_ASSERT(lkey != NULL);

	D_DEBUG(DB_TRACE, "cont="DF_UUID", obj="DF_UOID"\n",
		DP_UUID(cont->vc_id), DP_UOID(lkey->olk_oid));

	D_ALLOC_PTR(obj);
	if (!obj)
		D_GOTO(failed, rc = -DER_NOMEM);

	init_object(obj, lkey->olk_oid, cont);

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

	D_DEBUG(DB_TRACE, "lru free callback for vos_obj_cache\n");

	obj = container_of(llink, struct vos_object, obj_llink);
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
vos_obj_cache_create(int32_t cache_size, struct daos_lru_cache **occ)
{
	int	rc;

	D_DEBUG(DB_TRACE, "Creating an object cache %d\n", (1 << cache_size));
	rc = daos_lru_cache_create(cache_size, D_HASH_FT_NOLOCK,
				   &obj_lru_ops, occ);
	if (rc)
		D_ERROR("Error in creating lru cache: "DF_RC"\n", DP_RC(rc));
	return rc;
}

void
vos_obj_cache_destroy(struct daos_lru_cache *occ)
{
	D_ASSERT(occ != NULL);
	daos_lru_cache_destroy(occ);
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
vos_obj_cache_evict(struct daos_lru_cache *cache, struct vos_container *cont)
{
	daos_lru_cache_evict(cache, obj_cache_evict_cond, cont);
}

/**
 * Return object cache for the current IO.
 */
struct daos_lru_cache *
vos_obj_cache_current(bool standalone)
{
	return vos_obj_cache_get(standalone);
}

static __thread struct vos_object	 obj_local = {0};

void
vos_obj_release(struct daos_lru_cache *occ, struct vos_object *obj, bool evict)
{

	if (obj == &obj_local) {
		clean_object(obj);
		memset(obj, 0, sizeof(*obj));
		return;
	}

	D_ASSERT((occ != NULL) && (obj != NULL));

	if (evict)
		daos_lru_ref_evict(occ, &obj->obj_llink);

	daos_lru_ref_release(occ, &obj->obj_llink);
}

int
vos_obj_discard_hold(struct daos_lru_cache *occ, struct vos_container *cont, daos_unit_oid_t oid,
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

	obj->obj_discard = true;
	*objp = obj;

	return 0;
}

void
vos_obj_discard_release(struct daos_lru_cache *occ, struct vos_object *obj)
{
	obj->obj_discard = false;

	vos_obj_release(occ, obj, false);
}

/** Move local object to the lru cache */
static inline int
cache_object(struct daos_lru_cache *occ, struct vos_object **objp)
{
	struct vos_object	*obj_new;
	struct daos_llink	*lret;
	struct obj_lru_key	 lkey;
	int			 rc;

	*objp = NULL;

	D_ASSERT(obj_local.obj_cont != NULL);

	lkey.olk_cont = obj_local.obj_cont;
	lkey.olk_oid = obj_local.obj_id;

	rc = daos_lru_ref_hold(occ, &lkey, sizeof(lkey), obj_local.obj_cont, &lret);
	if (rc != 0) {
		clean_object(&obj_local);
		memset(&obj_local, 0, sizeof(obj_local));
		return rc; /* Can't cache new object */
	}

	/** Object is in cache */
	obj_new = container_of(lret, struct vos_object, obj_llink);
	/* This object should not be cached */
	D_ASSERT(obj_new->obj_df == NULL);

	vos_ilog_fetch_move(&obj_new->obj_ilog_info, &obj_local.obj_ilog_info);
	obj_new->obj_toh = obj_local.obj_toh;
	obj_new->obj_ih = obj_local.obj_ih;
	obj_new->obj_sync_epoch = obj_local.obj_sync_epoch;
	obj_new->obj_df = obj_local.obj_df;
	obj_new->obj_zombie = obj_local.obj_zombie;
	obj_local.obj_toh = DAOS_HDL_INVAL;
	obj_local.obj_ih = DAOS_HDL_INVAL;
	clean_object(&obj_local);
	memset(&obj_local, 0, sizeof(obj_local));

	*objp = obj_new;

	return 0;
}

int
vos_obj_hold(struct daos_lru_cache *occ, struct vos_container *cont,
	     daos_unit_oid_t oid, daos_epoch_range_t *epr, daos_epoch_t bound,
	     uint64_t flags, uint32_t intent, struct vos_object **obj_p,
	     struct vos_ts_set *ts_set)
{
	struct vos_object	*obj;
	struct daos_llink	*lret;
	struct obj_lru_key	 lkey;
	int			 rc = 0;
	int			 tmprc;
	uint32_t		 cond_mask = 0;
	bool			 create;
	void			*create_flag = NULL;
	bool			 visible_only;

	D_ASSERT(cont != NULL);
	D_ASSERT(cont->vc_pool);

	*obj_p = NULL;

	if (cont->vc_pool->vp_dying)
		return -DER_SHUTDOWN;

	create = flags & VOS_OBJ_CREATE;
	visible_only = flags & VOS_OBJ_VISIBLE;
	/** Pass NULL as the create_args if we are not creating the object so we avoid
	 *  evicting an entry until we need to
	 */
	if (create)
		create_flag = cont;

	D_DEBUG(DB_TRACE, "Try to hold cont="DF_UUID", obj="DF_UOID
		" layout %u create=%s epr="DF_X64"-"DF_X64"\n",
		DP_UUID(cont->vc_id), DP_UOID(oid), oid.id_layout_ver,
		create ? "true" : "false", epr->epr_lo, epr->epr_hi);

	/* Create the key for obj cache */
	lkey.olk_cont = cont;
	lkey.olk_oid = oid;

	rc = daos_lru_ref_hold(occ, &lkey, sizeof(lkey), create_flag, &lret);
	if (rc == -DER_NONEXIST) {
		D_ASSERT(obj_local.obj_cont == NULL);
		obj = &obj_local;
		init_object(obj, oid, cont);
	} else if (rc != 0) {
		D_GOTO(failed_2, rc);
	} else {
		/** Object is in cache */
		obj = container_of(lret, struct vos_object, obj_llink);
	}

	if (obj->obj_zombie)
		D_GOTO(failed, rc = -DER_AGAIN);

	if (intent == DAOS_INTENT_KILL && !(flags & VOS_OBJ_KILL_DKEY)) {
		if (obj != &obj_local) {
			if (vos_obj_refcount(obj) > 2)
				D_GOTO(failed, rc = -DER_BUSY);

			vos_obj_evict(occ, obj);
		}
		/* no one else can hold it */
		obj->obj_zombie = true;
		if (obj->obj_df)
			goto out; /* Ok to delete */
	}

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

	if (obj == &obj_local) {
		/** Ok, it's successful, go ahead and cache the object. */
		rc = cache_object(occ, &obj);
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
vos_obj_evict(struct daos_lru_cache *occ, struct vos_object *obj)
{
	if (obj == &obj_local)
		return;
	daos_lru_ref_evict(occ, &obj->obj_llink);
}

int
vos_obj_evict_by_oid(struct daos_lru_cache *occ, struct vos_container *cont,
		     daos_unit_oid_t oid)
{
	struct obj_lru_key	 lkey;
	struct daos_llink	*lret;
	int			 rc;

	lkey.olk_cont = cont;
	lkey.olk_oid = oid;

	rc = daos_lru_ref_hold(occ, &lkey, sizeof(lkey), NULL, &lret);
	if (rc == 0) {
		daos_lru_ref_evict(occ, lret);
		daos_lru_ref_release(occ, lret);
	}

	return rc == -DER_NONEXIST ? 0 : rc;
}
