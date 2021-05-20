/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Object related API and structures
 * Includes:
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
	/** Cache of incarnation log */
	struct vos_ilog_info		obj_ilog_info;
	/** Key for searching, object ID within a container */
	daos_unit_oid_t			obj_id;
	/** dkey tree open handle of the object */
	daos_handle_t			obj_toh;
	/** btree iterator handle */
	daos_handle_t			obj_ih;
	/** epoch when the object(cache) is initialized */
	daos_epoch_t			obj_epoch;
	/** The latest sync epoch */
	daos_epoch_t			obj_sync_epoch;
	/** cached vos_obj_df::vo_incarnation, for revalidation. */
	uint32_t			obj_incarnation;
	/** nobody should access this object */
	bool				obj_zombie;
	/** Persistent memory address of the object */
	struct vos_obj_df		*obj_df;
	/** backref to container */
	struct vos_container		*obj_cont;
};

enum {
	/** Only return the object if it's visible */
	VOS_OBJ_VISIBLE		= (1 << 0),
	/** Create the object if it doesn't exist */
	VOS_OBJ_CREATE		= (1 << 1),
};

/**
 * Load an object from PMEM pool or create it, load some information about the
 * object
 *
 * \param cont		[IN]		Open container.
 * \param oid		[IN]		VOS object ID.
 * \param epr		[IN,OUT]	Epoch range.   High epoch should be set
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
vos_obj_hold(struct vos_container *cont, daos_unit_oid_t oid,
	     daos_epoch_range_t *epr, daos_epoch_t bound, uint64_t flags,
	     uint32_t intent, struct vos_object **obj_p,
	     struct vos_ts_set *ts_set);

/**
 * Release the object handle
 *
 * \param obj	[IN]	Reference to be released.
 */
void
vos_obj_release(struct vos_object *obj);

/**
 * Object Index API and handles
 * For internal use by object cache
 */

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
 * Punch an object from the OI table
 */
int
vos_oi_punch(struct vos_container *cont, daos_unit_oid_t oid,
	     daos_epoch_t epoch, daos_epoch_t bound, uint64_t flags,
	     struct vos_obj_df *obj, struct vos_ilog_info *info,
	     struct vos_ts_set *ts_set);


/** delete an object from OI table */
int
vos_oi_delete(struct vos_container *cont, daos_unit_oid_t oid);

#endif
