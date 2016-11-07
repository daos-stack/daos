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

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <vos_obj.h>
#include <vos_internal.h>
#include <vos_hhash.h>
#include <daos_errno.h>

/**
 * Local type for VOS LRU key
 * VOS LRU key must consist of
 * Object ID and container UUID
 */
struct vos_lru_key {
	/* Container UUID */
	uuid_t		vlk_co_uuid;
	/* Object ID */
	daos_unit_oid_t	vlk_obj_id;
};


int
vos_oref_lru_alloc(void *key, unsigned int ksize,
		   void *args, struct daos_llink **link)
{
	int			rc = 0;
	struct vos_obj_ref	*oref;
	struct vos_obj		*lobj;
	struct vos_lru_key	*lkey;
	struct vc_hdl		*co_hdl;

	D_DEBUG(DF_VOS2, "lru alloc callback for vos_obj_cache\n");

	co_hdl = (struct vc_hdl *)args;
	D_ASSERT(co_hdl != NULL);

	lkey = (struct vos_lru_key *) key;
	D_ASSERT(lkey != NULL);

	/**
	 * Call back called by LRU cache as the reference
	 * was not found in DRAM cache
	 * Looking it up in PMEM Object Index
	 */
	rc = vos_oi_find_alloc(co_hdl, lkey->vlk_obj_id, &lobj);
	if (rc) {
		D_ERROR("Error looking up container handle\n");
		return rc;
	}

	D_ALLOC_PTR(oref);
	if (!oref)
		return -DER_NOMEM;
	/**
	 *  Saving a copy of oid to avoid looking up in
	 *  vos_obj is a direct pointer to pmem data structure
	 */
	oref->or_obj	= lobj;
	oref->or_oid	= lkey->vlk_obj_id;
	oref->or_co	= co_hdl;

	D_DEBUG(DF_VOS2, "oref create_cb co uuid:"DF_UUID"\n",
		CP_UUID(co_hdl->vc_id));
	D_DEBUG(DF_VOS2, "Object Hold of obj_id: "DF_UOID"\n",
		DP_UOID(lkey->vlk_obj_id));

	*link		= &oref->or_llink;

	return 0;
}

bool
vos_oref_cmp_keys(const void *key, unsigned int ksize,
		  struct daos_llink *llink)
{
	struct vos_obj_ref	*oref;
	struct vos_lru_key	*hkey = (struct vos_lru_key *) key;

	D_DEBUG(DF_VOS3, "LRU compare keys\n");
	D_ASSERT(llink);
	D_ASSERT(ksize == sizeof(struct vos_lru_key));

	oref = container_of(llink, struct vos_obj_ref, or_llink);

	return !(memcmp(&hkey->vlk_obj_id, &oref->or_oid,
			sizeof(daos_unit_oid_t)) ||
		 uuid_compare(hkey->vlk_co_uuid, oref->or_co->vc_id));
}

void
vos_oref_lru_free(struct daos_llink *llink)
{
	struct vos_obj_ref	*oref;

	D_DEBUG(DF_VOS3, "lru free callback for vos_obj_cache\n");
	D_ASSERT(llink);

	oref = container_of(llink, struct vos_obj_ref, or_llink);
	vos_obj_tree_fini(oref);
	D_FREE_PTR(oref);
}

void
vos_oref_lru_printkey(void *key, unsigned int ksize)
{
	struct vos_lru_key	*lkey;

	lkey = (struct vos_lru_key *) key;
	D_ASSERT(lkey != NULL);
	D_DEBUG(DF_VOS2, "Container uuid:"DF_UUID"\n",
		CP_UUID(lkey->vlk_co_uuid));
	D_DEBUG(DF_VOS2, "Object id: "DF_UOID"\n",
		DP_UOID(lkey->vlk_obj_id));
}

struct daos_llink_ops vos_oref_llink_ops = {
	.lop_free_ref	=  vos_oref_lru_free,
	.lop_alloc_ref	=  vos_oref_lru_alloc,
	.lop_cmp_keys	=  vos_oref_cmp_keys,
	.lop_print_key	=  vos_oref_lru_printkey,
};

int
vos_obj_cache_create(int32_t cache_size,
		     struct daos_lru_cache **occ)
{

	int			rc = 0;

	D_DEBUG(DF_VOS2, "Creating an object cache %d\n",
		(1 << cache_size));

	rc = daos_lru_cache_create(DHASH_FT_NOLOCK,
				   cache_size, &vos_oref_llink_ops,
				   occ);
	if (rc)
		D_ERROR("Error in creating lru cache\n");

	D_DEBUG(DF_VOS2, "Succesful in creating object cache\n");
	return rc;
}

void
vos_obj_cache_destroy(struct daos_lru_cache *occ)
{
	D_ASSERT(occ != NULL);
	daos_lru_cache_destroy(occ);
}

/**
 * Return object cache for the current thread.
 */
struct daos_lru_cache *vos_obj_cache_current(void)
{
	return vos_get_obj_cache();
}

void
vos_obj_ref_release(struct daos_lru_cache *occ,
		    struct vos_obj_ref *oref)
{

	D_ASSERT((occ != NULL) && (oref != NULL));
	daos_lru_ref_release(occ, &oref->or_llink);
}

int
vos_obj_ref_hold(struct daos_lru_cache *occ, daos_handle_t coh,
		 daos_unit_oid_t oid, struct vos_obj_ref **oref_p)
{

	int			rc = 0;
	struct vos_obj_ref	*lref = NULL;
	struct vos_lru_key	lkey;
	struct daos_llink	*lret = NULL;
	struct vc_hdl		*co_hdl;

	D_ASSERT(occ != NULL);
	D_DEBUG(DF_VOS2, "Object Hold of obj_id: "DF_UOID"\n",
		DP_UOID(oid));
	co_hdl = vos_hdl2co(coh);
	D_ASSERT(co_hdl != NULL);

	/* Create the key for obj cache */
	uuid_copy(lkey.vlk_co_uuid, co_hdl->vc_id);
	lkey.vlk_obj_id = oid;

	rc = daos_lru_ref_hold(occ, &lkey, sizeof(lkey),
			       co_hdl, &lret);
	if (rc) {
		D_ERROR("Error in Holding reference for obj"DF_UOID"\n",
			DP_UOID(oid));
		return	rc;
	}

	lref = container_of(lret, struct vos_obj_ref, or_llink);
	D_DEBUG(DF_VOS2, "Object "DF_UOID" ref hold successful\n",
		DP_UOID(oid));
	D_DEBUG(DF_VOS2, "Container UUID:"DF_UUID"\n",
		CP_UUID(lref->or_co->vc_id));
	*oref_p = lref;

	return	rc;
}
