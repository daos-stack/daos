/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2015, 2016 Intel Corporation.
 */
/**
 * DAOS Sharding & Resilience APIs
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 *
 * Version 0.3
 */

#ifndef __DSR_API_H__
#define __DSR_API_H__

#include <daos_types.h>
#include <daos_errno.h>
#include <daos_ev.h>

/**
 * DAOS APIs can run either in non-blocking mode or in blocking mode:
 *
 * - Non-blocking mode
 *   If input event(daos_event_t) of API is not NULL, it will run in
 *   non-blocking mode and return immediately after submitting API request
 *   to underlying stack.
 *   Returned value of API is zero on success, or negative error code only if
 *   there is an invalid parameter or other failure which can be detected
 *   without calling into server stack.
 *   Error codes for all other failures will be returned by event::ev_error.
 *
 * - Blocking mode
 *   If input event of API is NULL, it will run in blocking mode and return
 *   after completing of operation. Error codes for all failure cases should
 *   be returned by return value of API.
 */

/**
 * DAOS pool APIs
 */

/**
 * Create a DAOS pool on targets within \a grp. Caller can also create the
 * pool on a subset of \a grp by providing \a ranks_included, or exclude
 * some targets from \a grp by providing \a ranks_excluded.
 *
 * \param grp	[IN]	Process group descriptor.
 * \param uuid [IN]	UUID of the new DAOS pool.
 * \param ranks_included [IN]
 *			Optional, if this parameter is provided, the pool is
 *			only created on the included targets. \a ranks_included
 *			and \a ranks_excluded are mutually exclusive.
 * \param ranks_excluded [IN]
 *			Optional, if this parameter is provided, the pool is
 *			created on targets except targets in \a ranks_excluded.
 * \param ranks_failed	[OUT]
 *			Optional, buffer to store faulty targets on failure.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EXIST	Pool uuid already existed
 */
int
dsr_pool_create(daos_group_t *grp, uuid_t uuid,
		daos_rank_list_t *ranks_included,
		daos_rank_list_t *ranks_excluded,
		daos_rank_list_t *ranks_failed,
		daos_event_t *ev);

/**
 * Destroy a DAOS pool on targets within \a grp.
 *
 * \param grp	[IN]	Process group descriptor.
 * \param uuid	[IN]	Pool uuid.
 * \param force	[IN]	Pool destroy will return failure is the pool is
 * 			still busy (still have openers), this parameter will
 * 			force the destroy to proceed even there is opener.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NONEXIST	Pool is nonexistent
 *			-DER_BUSY	Pool is busy
 */
int
dsr_pool_destroy(daos_group_t *grp, uuid_t uuid, bool force, daos_event_t *ev);

/**
 * Connect to the DAOS pool identified by UUID \a uuid.
 *
 * \param uuid [IN]	UUID to identify a pool.
 * \param grp	[IN]	Process group descriptor.
 * \param mode	[IN]	Connect mode: read-only, read-write
 * \param ranks_failed [OUT]
 *			Optional, buffer to store faulty targets on failure.
 * \param poh	[OUT]	Returned open handle.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_PERM	Permission denied
 *			-DER_NONEXIST	Pool is nonexistent
 */
int
dsr_pool_connect(uuid_t uuid, daos_group_t *grp, unsigned int mode,
		 daos_rank_list_t *ranks_failed, daos_handle_t *poh,
		 daos_event_t *ev);

/**
 * Disconnect from the DAOS pool. It should revoke all the container open
 * handles of this pool.
 *
 * \param poh	[IN]	Pool connection handle
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid pool handle
 */
int
dsr_pool_disconnect(daos_handle_t poh, daos_event_t *ev);

/**
 * Extend the pool to more targets. If \a ranks is NULL, this function
 * will extend the pool to all the targets in the group, otherwise it will
 * only extend the pool to the included targets.
 *
 * NB: Doubling storage targets in the pool can have better performance than
 * arbitrary targets adding.
 *
 * \param poh	[IN]	Pool connection handle.
 * \param ransk [IN]	Optional, only extend the pool to included targets.
 * \param ranks_failed [OUT]
 *			Optional, buffer to store faulty targets on failure.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid pool handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_PERM	Permission denied
 *			-DER_NONEXIST	Storage target is nonexistent
 */
int
dsr_pool_extend(daos_handle_t poh, daos_rank_list_t *ranks,
		daos_rank_list_t *ranks_failed, daos_event_t *ev);

/**
 * Exclude a set of storage targets from a pool.
 *
 * \param poh	[IN]	Pool connection handle.
 * \param ranks	[IN]	Target rank array to be excluded from the pool.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid pool handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_PERM	Permission denied
 *			-DER_NONEXIST	Storage target is nonexistent
 */
int
dsr_pool_exclude(daos_handle_t poh, daos_rank_list_t *ranks, daos_event_t *ev);

/**
 * Replace pool targets identified by \a ranks_old with targets identified
 * by \a ranks_new. \a ranks_old::rl_rankn and \a ranks_new::rl_rankn must
 * be same, targets in these two parameters should not have overlap.
 *
 * NB: There could be an upper limit for number of targets being replaced
 *
 * \param poh	[IN]	Pool connection handle.
 * \param ranks_old [IN]
 *			Targets to be replaced.
 * \param ranks_new [IN]
 *			Targets to replace the old targets.
 * \param ranks_failed [OUT]
 *			Optional, buffer to store faulty targets on failure.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid pool handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_PERM	Permission denied
 *			-DER_NONEXIST	Storage target is nonexistent
 */
int
dsr_pool_replace(daos_handle_t poh,
		 daos_rank_list_t *ranks_old,
		 daos_rank_list_t *ranks_new,
		 daos_rank_list_t *ranks_failed,
		 daos_event_t *ev);

/**
 * Query pool information. User should provide at least one of \a info and
 * \a ranks as output buffer.
 *
 * \param poh	[IN]	Pool connection handle.
 * \param ranks	[OUT]	Optional, returned storage targets in this pool.
 * \param info	[OUT]	Optional, returned pool information.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid pool handle
 */
int
dsr_pool_query(daos_handle_t poh, daos_rank_list_t *ranks,
	       daos_pool_info_t *info, daos_event_t *ev);

/**
 * Query information of storage targets within a DAOS pool.
 *
 * \param poh	[IN]	Pool connection handle.
 * \param ranks	[IN]	A list of target to query.
 * \param ranks_failed [OUT]
 *			Optional, buffer to store faulty targets on failure.
 * \param info_list [OUT]
 * 			Returned storage information of \a ranks, it is an array
 *			and array size must equal to ranks::rl_llen.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_HDL	Invalid pool handle
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NONEXIST	No pool on specified targets
 */
int
dsr_pool_target_query(daos_handle_t poh, daos_rank_list_t *ranks,
		      daos_rank_list_t *ranks_failed,
		      daos_target_info_t *info_list,
		      daos_event_t *ev);

/**
 * Container APIs
 */

/**
 * Create a new container with uuid \a uuid on the storage pool connected
 * by \a grp.
 *
 * \param poh  [IN]	Pool connection handle.
 * \param uuid [IN]	UUID of the new Container.
 * \param mode [IN]	Open mode: read-only, read-write or exclusive.
 * \param ranks_failed [OUT]
 *			Optional, buffer to store faulty targets on failure.
 * \param ev   [IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_PERM	Permission denied
 *			-DER_UNREACH	network is unreachable
 *			-DER_EXIST	Container uuid already existed
 *			-DER_NONEXIST	Storage target is nonexistent
 */
int
dsr_co_create(daos_handle_t poh, uuid_t uuid, unsigned int mode,
	      daos_rank_list_t *ranks_failed, daos_event_t *ev);

/**
 * Open an existent container identified by UUID \a uuid.
 *
 * \param poh [IN]	Pool connection handle.
 * \param uuid [IN]	UUID to identify container.
 * \param mode	[IN]	Open mode: read-only, read-write or exclusive.
 * \param ranks_failed [OUT]
 *			Optional, buffer to store faulty targets on failure.
 * \param coh	[OUT]	Returned open handle.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_PERM	Permission denied
 *			-DER_NONEXIST	Container is nonexistent
 */
int
dsr_co_open(daos_handle_t poh, uuid_t uuid, unsigned int mode,
	    daos_rank_list_t *ranks_failed, daos_handle_t *coh,
	    daos_event_t *ev);

/**
 * Close an opened container.
 *
 * \param coh	[IN]	Container open handle.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid container handle
 */
int
dsr_co_close(daos_handle_t coh, daos_event_t *ev);

/**
 * Destroy a container identfied by \a uuid, all objects within this
 * container will be destroyed as well.
 *
 * \param poh [IN]	Pool connection handle.
 * \param uuid [IN]	Container UUID.
 * \param force	[IN]	Container destroy will return failure is the container
 *			is still busy (still have openers), this parameter will
 * 			force the destroy to proceed even there is opener.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NONEXIST	Container is nonexistent
 *			-DER_BUSY	Pool is busy
 */
int
dsr_co_destroy(daos_handle_t poh, uuid_t uuid, bool force, daos_event_t *ev);

/**
 * Query container information. User should provide at least one of
 * \a info and \a grp as output buffer.
 *
 * \param coh	[IN]	Container open handle.
 * \param info	[OUT]	Returned container information.
 *			If \a info::ci_snapshots is not NULL, epochs of
 *			snapshots will be stored in it.
 *			If \a info::ci_snapshots is NULL, number of snaphots
 *			will be returned by \a info::ci_nsnapshots.
 * \param ev   [IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid container handle
 */
int
dsr_co_query(daos_handle_t coh, daos_co_info_t *info, daos_event_t *ev);

/**
 * Enumerate all object IDs in a container for a particular epoch.
 *
 * \param coh	[IN]	Container open handle.
 * \param epoch	[IN]	Epoch to list object.
 * \param oidl	[OUT]	Sink buffer for returned OIDs. Number of actually
 *			returned OIDs is returned to \a oidl::ol_oidn.
 * \param anchor [IN/OUT]
 *			Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls. Caller should check returned
 *			anchor because -1 indicates the end of enumeration.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_UNREACH	Network is unreachable
 *			-DER_INVAL	Invalid parameter
 */
int
dsr_co_list_obj(daos_handle_t coh, daos_epoch_t epoch, daos_oid_list_t *oidl,
		daos_hash_out_t *anchor, daos_event_t *ev);


/**
 * Object common data structures and APIs.
 */

/* TODO: move structs to dsr_types.h */

typedef uint16_t		dsr_oclass_id_t;

enum {
	/** use private class for the object */
	DSR_OCLASS_NONE		= 0,
};

typedef enum {
	DSR_OBJ_KV,		/**< KV store */
	DSR_OBJ_ARR,		/**< byte array */
	DSR_OBJ_SEG_ARR,	/**< 2-dimensional array object */
} dsr_obj_type_t;

typedef enum {
	DSR_OS_SINGLE,		/**< single stripe object */
	DSR_OS_STRIPED,		/**< fix striped object */
	DSR_OS_DYN_STRIPED,	/**< dynamically striped object */
	DSR_OS_DYN_CHUNKED,	/**< dynamically chunked object */
} dsr_obj_schema_t;

typedef enum {
	DSR_RES_EC,		/**< erasure code */
	DSR_RES_REPL,		/**< replication */
} dsr_obj_resil_t;;

/** Object class attributes */
typedef struct dsr_oclass_attr {
	/** Object placement schema */
	dsr_obj_schema_t		 ca_schema;
	/**
	 * TODO: define HA degrees for object placement
	 * - performance oriented
	 * - high availability oriented
	 * ......
	 */
	unsigned int			 ca_resil_degree;
	/** Resilience method, replication or erasure code */
	dsr_obj_resil_t			 ca_resil;
	/** Initial # stripe count, unnecessary for some schemas */
	unsigned int			 ca_nstripes;
	union {
		/** replication attributes */
		struct dsr_repl_attr {
			/** Method of replicating */
			unsigned int	 r_method;
			/** Number of replicas */
			unsigned int	 r_num;
			/** TODO: add members to describe */
		} repl;

		/** Erasure coding attributes */
		struct dsr_ec_attr {
			/** Type of EC */
			unsigned int	 e_type;
			/** EC group size */
			unsigned int	 e_grp_size;
			/**
			 * TODO: add members to describe erasure coding
			 * attributes
			 */
		} ec;
	} u;
	/** TODO: add more attributes */
} dsr_oclass_attr_t;

/**
 * Register a new object class.
 * A object class cannot be unregistered for the time being.
 *
 * \param coh	[IN]	Container open handle.
 * \param id	[IN]	ID for the new object class.
 * \param cattr	[IN]	Attributes for the new object class.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EXIST	Object class ID already existed
 */
int
dsr_oclass_register(daos_handle_t coh, dsr_oclass_id_t id,
		    dsr_oclass_attr_t *cattr, daos_event_t *ev);

/**
 * Query attributes of object class by its ID.
 *
 * \param coh	[IN]	Container open handle.
 * \param id	[IN]	Class ID to query.
 * \param cattr	[OUT]	Returned attributes of the object class.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NONEXIST	nonexistent class ID
 */
int
dsr_oclass_query(daos_handle_t coh, dsr_oclass_id_t id,
		 daos_oclass_attr_t *cattr, daos_event_ *ev);


/**
 * Object attributes (metadata).
 * \a oa_class and \a oa_oa are mutually exclusive.
 */
typedef struct {
	/** Object type */
	dsr_obj_type_t		 oa_type;
	/** Pre-defined class ID */
	dsr_oclass_id_t		 oa_class;
	/** Optional, affinity target for the object */
	dtp_rank_t		 oa_rank;
	/** Optional, class attributes of object with private class */
	dsr_oclass_attr_t	*oa_oa;
	/** Optional, explicitly enumerated object layout */
	daos_rank_list_t	*oa_ranks;
} dsr_obj_attr_t;

/**
 * Create a new object based on attributes \a oa.
 *
 * \param coh	[IN]	Container open handle.
 * \param id	[IN/OUT]
 *			object ID, daos may fill reserved bits of object ID.
 * \param epoch	[IN]	Epoch to create object.
 * \param oa	[IN]	Object creation parameters.
 * \param oh	[OUT]	Returned object open handle.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_PERM	Permission denied
 *			-DER_EXIST	Object ID has been used for another
 *					object.
 *			-DER_NONEXIST	Cannot find container on specified
 *					storage target
 *			-DER_NOTYPE	Unknown object type
 *			-DER_NOSCHEMA	Unknown object schema
 *			-DER_EP_RO	Epoch is read-only
 */
int
dsr_obj_create(daos_handle_t coh, daos_obj_id_t *id, daos_epoch_t epoch,
	       dsr_obj_attr_t *oa, daos_handle_t *oh, daos_event_t *ev);

/**
 * Open an existent object.
 *
 * \param coh	[IN]	Container open handle.
 * \param id	[IN]	Object ID.
 * \param epoch	[IN]	Epoch to open object.
 * \param mode	[IN]	Open mode: read-only, read-write.
 * \param oh	[OUT]	Returned object open handle.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_PERM	Permission denied
 *			-DER_NONEXIST	Cannot find object
 *			-DER_EP_OLD	Epoch is too old and has no data for
 *					this object
 */
int
dsr_obj_open(daos_handle_t coh, daos_obj_id_t id, daos_epoch_t epoch,
	     unsigned int mode, daos_handle_t *oh, daos_event_t *ev);

/**
 * Close an opened object.
 *
 * \param oh	[IN]	Object open handle.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 */
int
dsr_obj_close(daos_handle_t oh, daos_event_t *ev);

/**
 * Destroy an object and invalidate object open handle.
 * All writes to the future epochs of a destroyed object will be discarded.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch to destroy object.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Permission denied
 *			-DER_NOEXIST	Nonexistent object ID
 */
int
dsr_obj_destroy(daos_handle_t oh, daos_epoch_t epoch, daos_event_t *ev);

/**
 * Query attributes of an object.
 * Caller should provide at least one of output parameters.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch to query.
 * \param oa	[OUT]	Returned object attributes.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 */
int
dsr_obj_query(daos_handle_t oh, daos_epoch_t epoch, dsr_obj_attr_t *oa,
	      aos_event_t *ev);

/**
 * Key-Value object APIs.
 */

/**
 * Lookup and return values for given keys in \a kvl.
 * Value length of unfound key will be set to zero.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch for lookup. It will be ignored if epoch range
 *			is provided by \a kvl (kvl::kv_epr).
 * \param kvl	[IN/OUT]
 *			Key list to lookup, if value buffers of \a kvl are NULL,
 *			only value lengths will be returned, otherwise found
 *			values will be filled into these buffers.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_KV_K2BIG	Key of KV is too large and can't be
 *					fit into output buffer
 *			-DER_KV_V2BIG	value of KV is too large and can't be
 *					fit into output buffer
 */
int
dsr_kv_lookup(daos_handle_t oh, daos_epoch_t epoch, daos_kv_list_t *kvl,
	      daos_event_t *ev);

/**
 * Update or insert KV pairs in \a kvl.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch for the update, it will be ignored if kvl::kv_eprs
 *			is provided.
 * \param kvl	[IN]	KV list to update or insert
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Epoch is read-only
 */
int
dsr_kv_update(daos_handle_t oh, daos_epoch_t epoch, daos_kv_list_t *kvl,
	      daos_event_t *ev);

/**
 * Locate and punch KV pairs for given keys in \a kvl.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch for the punch. It will be ignored if kvl::kv_eprs
 *			is provided.
 * \param kvl	[IN]	KV list to punch, only keys are required.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Epoch is read-only
 */
int
dsr_kv_punch(daos_handle_t oh, daos_epoch_t epoch, daos_kv_list_t *kvl,
	     daos_event_t *ev);

/**
 * Enumerate KV pairs of a KV object.
 *
 * \param oh	[IN]	Object open handle.
 * \param epr	[IN]	Epoch range for the enumeration.
 * \param kvl	[OUT]	Sink buffer for returned KV list.
 *			If key parts or/and value parts of \a kvs are NULL,
 *			length of key/value will be returned, otherwise they
 *			will be filled with returned KV pairs.
 *			Number of actually returned KVs are stored in
 *			kvl::kv_kvn.
 * \param anchor [IN/OUT]
 *			Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_KV_K2BIG	Key of KV is too large and can't be
 *					fit into output buffer
 *			-DER_KV_V2BIG	Value of KV is too large and can't be
 *					fit into output buffer
 */
int
dsr_kv_list(daos_handle_t oh, daos_epoch_range_t *epr, daos_kv_list_t *kvl,
	    daos_hash_out_t *anchor, daos_event_t *ev);

/**
 * Byte-array object APIs.
 */

/**
 * Read from a byte-array object.
 * Object data extents in \a exl will be copied to buffers in \a sgl.
 * If \a layout is provided by caller, physical extent layout will be stored
 * in it, if \a layout is not sufficient to store all returned extent layouts,
 * then the required size will be returned to \a layout::el_extn.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch [IN]    Epoch for the read. It will be ignored if epoch range
 *			is provided by \a exl (exl::el_epr).
 * \param exl	[IN]    Object extents for the read.
 * \param layout [OUT]	Optional, returned physical extent layouts and
 *			their epoch ranges.
 * \param sgl	[OUT]	Buffer list to store returned object data.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_IO_INVAL	IO buffers can't match object extents
 *					or object extents have overlap
 *			-DER_EP_OLD	Epoch is too old and has no data
 */
int
dsr_ba_read(daos_handle_t oh, daos_epoch_t epoch, daos_ext_list_t *exl,
	    daos_ext_layout_t *layout, daos_sg_list_t *sgl, daos_event_t *ev);

/**
 * Write to a byte-array object.
 * Data blobs in \a sgl will be written to object extents specified by \a exl.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch for the write. It will be ignored if epoch range
 *			is provided by \a exl (exl::el_epr).
 * \param exl	[IN]	Object extents for the write.
 * \param sgl	[OUT]	Write source buffer list.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_IO_INVAL	IO buffers can't match object extents
 *					or object extents have overlap
 *			-DER_EP_RO	Epoch is read-only
 */
int
dsr_ba_write(daos_handle_t oh, daos_epoch_t epoch, daos_ext_list_t *exl,
	     daos_sg_list_t *sgl, daos_event_t *ev);

/**
 * Punch a list of extents \a exl of a byte-array object.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch for the punch. It will be ignored if epoch range
 *			is provided by \a exl (exl::el_epr).
 * \param exl	[IN]	Extents to punch.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_IO_INVAL	IO buffers can't match object extents
 *					or object extents have overlap
 *			-DER_EP_RO	Epoch is read-only
 */
int
dsr_ba_punch(daos_handle_t oh, daos_epoch_t epoch, daos_ext_list_t *exl,
	     daos_event_t *ev);

/************************************************************************
 * TODO: Epoch APIs
 */

#endif /* __DSR_API_H__ */
