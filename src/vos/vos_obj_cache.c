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
	/**
	 * Saving a copy of oid to avoid looking up in vos_obj_df, which
	 * is a direct pointer to pmem data structure
	 */
	obj->obj_id	= lkey->olk_oid;
	obj->obj_cont	= cont;
	vos_cont_addref(cont);

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

static void
obj_lop_free(struct daos_llink *llink)
{
	struct vos_object	*obj;

	D_DEBUG(DB_TRACE, "lru free callback for vos_obj_cache\n");

	obj = container_of(llink, struct vos_object, obj_llink);
	if (obj->obj_cont != NULL)
		vos_cont_decref(obj->obj_cont);

	obj_tree_fini(obj);
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
	.lop_free_ref	=  obj_lop_free,
	.lop_alloc_ref	=  obj_lop_alloc,
	.lop_cmp_keys	=  obj_lop_cmp_key,
	.lop_print_key	=  obj_lop_print_key,
};

int
vos_obj_cache_create(int32_t cache_size, struct daos_lru_cache **occ)
{
	int	rc;

	D_DEBUG(DB_TRACE, "Creating an object cache %d\n", (1 << cache_size));
	rc = daos_lru_cache_create(cache_size, D_HASH_FT_NOLOCK,
				   &obj_lru_ops, occ);
	if (rc)
		D_ERROR("Error in creating lru cache: %d\n", rc);
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
 * Return object cache for the current thread.
 */
struct daos_lru_cache *
vos_obj_cache_current(void)
{
	return vos_get_obj_cache();
}

void
vos_obj_release(struct daos_lru_cache *occ, struct vos_object *obj)
{

	D_ASSERT((occ != NULL) && (obj != NULL));
	daos_lru_ref_release(occ, &obj->obj_llink);
}


int
vos_obj_hold(struct daos_lru_cache *occ, daos_handle_t coh,
	     daos_unit_oid_t oid, daos_epoch_t epoch,
	     bool no_create, uint32_t intent, struct vos_object **obj_p)
{

	struct vos_object	*obj;
	struct vos_container	*cont;
	struct obj_lru_key	 lkey;
	int			 rc;

	cont = vos_hdl2cont(coh);
	D_ASSERT(cont != NULL);

	D_DEBUG(DB_TRACE, "Try to hold cont="DF_UUID", obj="DF_UOID"\n",
		DP_UUID(cont->vc_id), DP_UOID(oid));

	/* Create the key for obj cache */
	lkey.olk_cont = cont;
	lkey.olk_oid = oid;

	while (1) {
		struct daos_llink *lret;

		rc = daos_lru_ref_hold(occ, &lkey, sizeof(lkey), cont, &lret);
		if (rc)
			D_GOTO(failed, rc);

		obj = container_of(lret, struct vos_object, obj_llink);
		if (obj->obj_epoch == 0) /* new cache element */
			obj->obj_epoch = epoch;

		if (!obj->obj_df) /* new object */
			break;

		if ((!(obj->obj_df->vo_oi_attr & VOS_OI_PUNCHED) ||
		     obj->obj_df->vo_latest >= epoch) &&
		    (obj->obj_df->vo_incarnation == obj->obj_incarnation) &&
		    (obj->obj_epoch <= epoch || obj->obj_incarnation == 0)) {
			struct umem_instance	*umm = &cont->vc_pool->vp_umm;

			rc = vos_dtx_check_availability(umm, coh,
						obj->obj_df->vo_dtx,
						umem_ptr2off(umm, obj->obj_df),
						intent, DTX_RT_OBJ);
			if (rc < 0) {
				vos_obj_release(occ, obj);
				D_GOTO(failed, rc);
			}

			if (rc != ALB_UNAVAILABLE) {
				if (obj->obj_incarnation == 0)
					obj->obj_epoch = epoch;

				goto out;
			}
		}

		D_DEBUG(DB_IO, "Evict obj ["DF_U64" -> "DF_U64"]\n",
			obj->obj_epoch, epoch);

		/* NB: we don't expect user wants to access many versions
		 * of the same object at the same time, so just evict the
		 * unmatched version from the cache, then populate the cache
		 * with the demanded versoin.
		 */
		vos_obj_evict(obj);
		vos_obj_release(occ, obj);
	}

	D_DEBUG(DB_TRACE, "%s Got empty obj "DF_UOID" in epoch="DF_U64"\n",
		no_create ? "find" : "find/create", DP_UOID(oid), epoch);

	if (no_create) {
		rc = vos_oi_find(cont, oid, epoch, intent, &obj->obj_df);
		if (rc == -DER_NONEXIST) {
			D_DEBUG(DB_TRACE, "non exist oid "DF_UOID"\n",
				DP_UOID(oid));
			rc = 0;
		}
	} else {
		rc = vos_oi_find_alloc(cont, oid, epoch, intent, &obj->obj_df);
		D_ASSERT(rc || obj->obj_df);
	}

	if (rc) {
		vos_obj_release(occ, obj);
		goto failed;
	}

	if (!obj->obj_df) {
		D_DEBUG(DB_TRACE, "nonexistent obj "DF_UOID"\n",
			DP_UOID(oid));
		goto out;
	}

	D_ASSERTF((obj->obj_df->vo_oi_attr & VOS_OI_PUNCHED) == 0 ||
		  epoch <= obj->obj_df->vo_latest,
		  "e="DF_U64", p="DF_U64"\n", epoch,
		  obj->obj_df->vo_latest);

	obj->obj_incarnation = obj->obj_df->vo_incarnation;
out:
	*obj_p = obj;
	return 0;
failed:
	D_ERROR("failed to hold object, rc=%d\n", rc);
	return	rc;
}

void
vos_obj_evict(struct vos_object *obj)
{
	daos_lru_ref_evict(&obj->obj_llink);
}

bool
vos_obj_evicted(struct vos_object *obj)
{
	return daos_lru_ref_evicted(&obj->obj_llink);
}

int
vos_obj_revalidate(struct daos_lru_cache *occ, daos_epoch_t epoch,
		   struct vos_object **obj_p)
{
	struct vos_object *obj = *obj_p;
	int		   rc;

	if (!vos_obj_evicted(obj))
		return 0;

	rc = vos_obj_hold(occ, vos_cont2hdl(obj->obj_cont), obj->obj_id,
			  epoch, !obj->obj_df, DAOS_INTENT_UPDATE, obj_p);
	if (rc == 0) {
		D_ASSERT(*obj_p != obj);
		vos_obj_release(occ, obj);
	}
	return rc;
}
