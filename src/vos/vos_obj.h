/**
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include "vos_ilog.h"
#include "vos_ts.h"

#define LRU_CACHE_BITS 16

/* Internal container handle structure */
struct vos_container;

/**
 * A cached object (DRAM data structure).
 */
struct vos_object {
	/** llink for daos lru cache */
	struct daos_llink		obj_llink;
	/** Cache of incarnation log */
	struct vos_ilog_info		obj_ilog_info;
	/** Key for searching, object ID within a container */
	daos_unit_oid_t			obj_id;
	/** dkey tree open handle of the object */
	daos_handle_t			obj_toh;
	/** btree iterator handle */
	daos_handle_t			obj_ih;
	/** The latest sync epoch */
	daos_epoch_t			obj_sync_epoch;
	/** Persistent memory address of the object */
	struct vos_obj_df		*obj_df;
	/** backref to container */
	struct vos_container		*obj_cont;
	/** nobody should access this object */
	bool				obj_zombie;
	/** Object is held for discard */
	uint32_t                         obj_discard : 1,
	    /** If non-zero, object is held for aggregation */
	    obj_aggregate                            : 1;
};

enum {
	/** Only return the object if it's visible */
	VOS_OBJ_VISIBLE = (1 << 0),
	/** Create the object if it doesn't exist */
	VOS_OBJ_CREATE = (1 << 1),
	/** Hold for discard */
	VOS_OBJ_DISCARD = (1 << 2),
	/** Hold for VOS or EC aggregation */
	VOS_OBJ_AGGREGATE = (1 << 3),
};

/**
 * Find an object in the cache \a occ and take its reference. If the object is
 * not in cache, this function will load it from PMEM pool, then add it to the
 * cache. If the object doesn't exist on PMEM pool, a negative cache entry will
 * be returned when VOS_OBJ_CREATE flag is specified, otherwise, -DER_NONEXIST
 * will be returned. This function should be called outside of local transaction.
 *
 * \param cont		[IN]		Open container.
 * \param oid		[IN]		VOS object ID.
 * \param epr		[IN]		Epoch range.   High epoch should be set
 *					to requested epoch.   The lower epoch
 *					can be 0 or bounded.
 * \param bound		[IN]		Epoch uncertainty bound
 * \param flags		[IN]		Object flags
 * \param intent	[IN]		The request intent.
 * \param obj_p		[OUT]		Returned object cache reference.
 * \param ts_set	[IN]		Timestamp set
 *
 * \return	0			The object is visible or, if
 *					\p VOS_OBJ_VISIBLE is not set, it has
 *					punched data or is entirely empty.
 * \return	-DER_NONEXIST		The conditions for success don't apply
 *		-DER_INPROGRESS		The local target doesn't have the
 *					definitive state of the object.
 *		other			Another error occurred
 */
int
vos_obj_hold(struct vos_container *cont, daos_unit_oid_t oid, daos_epoch_range_t *epr,
	     daos_epoch_t bound, uint64_t flags, uint32_t intent, struct vos_object **obj_p,
	     struct vos_ts_set *ts_set);

/**
 * Release the object cache reference.
 *
 * \param obj	[IN]	Reference to be released.
 */
void
vos_obj_release(struct vos_object *obj, uint64_t flags, bool evict);

/** Evict an object reference from the cache */
void vos_obj_evict(struct vos_object *obj);

int vos_obj_evict_by_oid(struct vos_container *cont, daos_unit_oid_t oid);

/**
 * Create an object cache.
 *
 * \param cache_size	[IN]	Cache size
 * \param occ_p		[OUT]	Newly created cache.
 */
int
vos_obj_cache_create(int32_t cache_size, struct daos_lru_cache **occ_p);

/**
 * Destroy an object cache, and release all cached object references.
 *
 * \param occ	[IN]	Cache to be destroyed.
 */
void
vos_obj_cache_destroy(struct daos_lru_cache *occ);

/** evict cached objects for the specified container */
void vos_obj_cache_evict(struct vos_container *cont);

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
 * \param cont	[IN]	Open container
 * \param oid	[IN]	DAOS object ID
 * \param epoch [IN]	Epoch for the lookup
 * \param log   [IN]	Add entry to ilog
 * \param obj	[OUT]	Direct pointer to VOS object
 * \param ts_set[IN]	Timestamp sets
 *
 * \return		0 on success and negative on
 *			failure
 */
int
vos_oi_find_alloc(struct vos_container *cont, daos_unit_oid_t oid,
		  daos_epoch_t epoch, bool log, struct vos_obj_df **obj,
		  struct vos_ts_set *ts_set);

/**
 * Find an entry in the obj_index by @oid
 * Created to us in tests for checking sanity of obj index
 * after deletion
 *
 * \param cont	[IN]	Open container
 * \param oid	[IN]	DAOS object ID
 * \param obj	[OUT]	Direct pointer to VOS object
 * \param ts_set[IN]	Timestamp sets
 *
 * \return		0 on success and negative on
 *			failure
 */
int
vos_oi_find(struct vos_container *cont, daos_unit_oid_t oid,
	    struct vos_obj_df **obj, struct vos_ts_set *ts_set);

/**
 * Create a new object for the @oid and return the direct pointer of the
 * new allocated object.
 *
 * \param cont	[IN]	Open container
 * \param oid	[IN]	DAOS object ID
 * \param epoch [IN]	Epoch for the lookup
 * \param obj	[OUT]	Direct pointer to VOS object
 * \param ts_set[IN]	Timestamp sets
 *
 * \return		0 on success and negative on
 *			failure
 */
int
vos_oi_alloc(struct vos_container *cont, daos_unit_oid_t oid, daos_epoch_t epoch,
	     struct vos_obj_df **obj_p, struct vos_ts_set *ts_set);

/**
 * Punch an object from the OI table
 */
int
vos_oi_punch(struct vos_container *cont, daos_unit_oid_t oid,
	     daos_epoch_t epoch, daos_epoch_t bound, uint64_t flags,
	     struct vos_obj_df *obj, struct vos_ilog_info *info,
	     struct vos_ts_set *ts_set);


/** delete an object from OI table */
int
vos_oi_delete(struct vos_container *cont, daos_unit_oid_t oid, bool only_delete_entry);

/**
 * When the passed in 'obj' is a negative cache entry, create object on PMEM pool
 * and associate it with the negative entry, otherwise, perform some update check
 * only. This function should be called within local transaction.
 *
 * \param obj		[IN]		Object cache entry
 * \param epr		[IN]		Epoch range.   High epoch should be set
 *					to requested epoch.   The lower epoch
 *					can be 0 or bounded.
 * \param bound		[IN]		Epoch uncertainty bound
 * \param flags		[IN]		Object flags
 * \param intent	[IN]		The request intent.
 * \param ts_set	[IN]		Timestamp set
 *
 * \return	0			Object is successfully incarnated.
 * \return	-DER_NONEXIST		The conditions for success don't apply
 *		-DER_INPROGRESS		The local target doesn't have the
 *					definitive state of the object.
 *		other			Another error occurred
 */
int
vos_obj_incarnate(struct vos_object *obj, daos_epoch_range_t *epr, daos_epoch_t bound,
		  uint64_t flags, uint32_t intent, struct vos_ts_set *ts_set);

/**
 * Check if an operation will be conflicting with other ongoing operations over the
 * same object. (aggregate, discard, obj discard, update, etc.)
 *
 * FIXME: This function could be deprecated once the obj discard is deprecated.
 *
 * \param cont		[IN]		VOS container
 * \param oid		[IN]		VOS object ID.
 * \param flags		[IN]		Object flags for the operation to be checked
 *
 * \return	0			No conflicting operations
 * \return	-DER_BUSY		Read is conflicting with ongoing operations
 *		-DER_UPDATE_AGAIN	Write is conflicting with ongoing operations
 */
int
vos_obj_check_discard(struct vos_container *cont, daos_unit_oid_t oid, uint64_t flags);

#endif
