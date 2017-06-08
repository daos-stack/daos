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
 * Object related API and structures
 * Includes:
 * -- VOS object cache API and structures for use by VOS object
 * -- VOS object index structures for internal use by object cache
 * vos/vos_obj.h
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */

#ifndef __VOS_OBJ_H__
#define __VOS_OBJ_H__

#include <daos/btree.h>
#include <daos/lru.h>
#include "vos_layout.h"

#define OT_BTREE_ORDER 20
#define LRU_CACHE_BITS 16

/**
 * VOS object index
 * in-place btree for object table
 * can have other parameters in future
 * like object statistics associated with
 * it.
 */
struct vos_object_index {
	struct btr_root	obtable;
};

/**
 * Reference of a cached object.
 * NB: DRAM data structure.
 */
struct vos_obj_ref;

/* Internal container handle structure */
struct vc_hdl;

/**
 * Find an object in the cache \a occ and take its reference. If the object is
 * not in cache, this function will load it from PMEM pool or create it, then
 * add it to the cache.
 *
 * \param occ	[IN]	Object cache, it could be a percpu data structure.
 * \param coh	[IN]	Container open handle.
 * \param oid	[IN]	VOS object ID.
 * \param oref_p [OUT]	Returned object cache reference.
 */
int
vos_obj_ref_hold(struct daos_lru_cache *occ, daos_handle_t coh,
		 daos_unit_oid_t oid, struct vos_obj_ref **oref_p);

/**
 * Release the object cache reference.
 *
 * \param oref	[IN]	Reference to be released.
 */
void
vos_obj_ref_release(struct daos_lru_cache *occ,
		    struct vos_obj_ref *oref);

/**
 * Varify if the object reference is still valid, and refresh it if it's
 * invalide (evicted)
 */
int vos_obj_ref_revalidate(struct daos_lru_cache *occ,
			   struct vos_obj_ref **oref_p);

/** Evict an object reference from the cache */
void vos_obj_ref_evict(struct vos_obj_ref *oref);

/** Check if an object reference has been evicted from the cache */
bool vos_obj_ref_evicted(struct vos_obj_ref *oref);

/**
 * Create an object cache.
 *
 * \param cache_size	[IN]	Cache size
 * \param occ_p		[OUT]	Newly created cache.
 */
int
vos_obj_cache_create(int32_t cache_size,
		     struct daos_lru_cache **occ_p);

/**
 * Destroy an object cache, and release all cached object references.
 *
 * \param occ	[IN]	Cache to be destroyed.
 */
void
vos_obj_cache_destroy(struct daos_lru_cache *occ);

/** evict cached objects for the specified container */
void vos_obj_cache_evict(struct daos_lru_cache *occ, struct vc_hdl *cont);

/**
 * Return object cache for the current thread.
 */
struct daos_lru_cache *vos_obj_cache_current(void);

/**
 * Object Index API and handles
 * For internal use by object cache
 */


/**
 * VOS object index update metadata
 * Add a new object ID entry in the object index table
 * Creates an empty tree for the object
 *
 * \param coh	[IN]	Container handle
 * \param oid	[IN]	DAOS object ID
 *			TODO: Additional arguments
 *			to support metadata storage for SR
 *
 * \return		0 on success and negative on
 *			failure
 */
int
vos_oi_update_metadata(daos_handle_t coh, daos_unit_oid_t oid);

/**
 * Lookup an object by @oid within the OI table.
 * If the object is not found, create a new object for the @oid and return
 * the direct pointer of the new allocated object.
 *
 * \param coh	[IN]	Container handle
 * \param oid	[IN]	DAOS object ID
 * \param obj	[OUT]	Direct pointer to VOS object
 *
 * \return		0 on success and negative on
 *			failure
 */
int
vos_oi_find_alloc(struct vc_hdl *co_hdl, daos_unit_oid_t oid,
		  struct vos_obj **obj);

/**
 * Find an enty in the obj_index by @oid
 * Created to us in tests for checking sanity of obj index
 * after deletion
 *
 * \param coh	[IN]	Container handle
 * \param oid	[IN]	DAOS object ID
 * \param obj	[OUT]	Direct pointer to VOS object
 *
 * \return		0 on success and negative on
 *			failure
 */
int
vos_oi_find(struct vc_hdl *co_hdl, daos_unit_oid_t oid,
	    struct vos_obj **obj);

#endif
