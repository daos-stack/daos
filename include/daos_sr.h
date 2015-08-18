/**
 * (C) Copyright 2015 Intel Corporation.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */
/**
 * DAOS Sharding & Resilience APIs
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 *
 * Version 0.2
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
 * Container APIs
 */

/**
 * Create a new container with uuid \a co_uuid on storage targets identified
 * by \a grp.
 *
 * \param co_uuid [IN]	UUID of new Container.
 * \param grp  [IN]	A group of servers/targets to create container on.
 * \param mode [IN]	Open mode: read-only, read-write.
 * \param coh  [OUT]	Returned open handle.
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
dsr_co_create(uuid_t co_uuid, daos_rank_group_t *grp, unsigned int mode,
	      daos_handle_t *coh, daos_event_t *ev);

/**
 * Open an existent container identified by UUID \a co_uuid.
 *
 * \param co_uuid [IN]	UUID to identify container.
 * \param grp	[IN]	Open hint, it is a group of targets which may have
 *			shards of this container.
 *			\a grp::rg_uuid is mandatory.
 *			\a grp::rg_ranks is optional and it can be NULL.
 *			If it is NULL, open request will be broadcasted to
 *			all storage nodes in the server group identified by
 *			\a grp::rg_uuid.
 * \param mode	[IN]	Open mode: read-only, read-write.
 * \param grp_failed [OUT]
 *			A group of servers/targets that failed to open the
 *			container.
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
dsr_co_open(uuid_t co_uuid, daos_rank_group_t *grp, unsigned int mode,
	    daos_rank_group_t *grp_failed, daos_handle_t *coh,
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
 * Destroy a container identfied by \a co_uuid, all objects within this
 * container will be destroyed as well.
 *
 * \param co_uuid [IN]	Container uuid.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NONEXIST	Container is nonexistent
 */
int
dsr_co_destroy(uuid_t co_uuid, daos_event_t *ev);

/**
 * Query container information. User should provide at least one of
 * \a info and \a grp as output buffer.
 *
 * \param coh	[IN]	Container open handle.
 * \param grp	[IN/OUT]
 *			Optional, returned storage targets in this container:
 *			- If \a grp::rg_uuid is set to a known UUID, for
 *			  example, it is set to UUID of server group of current
 *			  process, then returned ranks are corresponding to
 *			  this server group.
 *			- If \a grp::rg_uuid is not set, then it will be set
 *			  to UUID of this container, and returned ranks are
 *			  corresponding to container UUID.
 *			- If \a grp::rg_ranks is NULL, this function should
 *			  return grp::rg_nranks.
 *			- If \a grp::rg_ranks is not NULL, this function will
 *			  fill target ranks that this container residing in.
 * \param info	[OUT]	Optional, returned container information.
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
dsr_co_query(daos_handle_t coh, daos_rank_group_t *grp, daos_co_info_t *info,
	     daos_event_t *ev);


/**
 * Query information  of storage targets that a container resides on.
 *
 * \param coh	[IN]	Container open handle.
 * \param grp	[IN]	A group of targets, all these targets should belong to
 *			current container, otherwise error will be returned.
 * \param info	[OUT]	Returned storage information of \a grp, it is an array
 *			and array size must equal to grp::rg_nranks.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NONEXIST	No container on specified targets
 */
int
dsr_co_target_query(daos_handle_t coh, daos_rank_group_t *grp,
		    daos_target_info_t *info, daos_event_t *ev);

/**
 * Add a group of storage targets to a container. In some environments,
 * doubling storage targets can have better performance than arbitrary
 * targets adding.
 *
 * NB: This function could be extended and take domain group as parameter?
 *
 * \param coh	[IN]	Container open handle.
 * \param grp	[IN]	A group of targets.
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
 *			-DER_NONEXIST	Storage target is nonexistent
 */
int
dsr_co_extend(daos_handle_t coh, daos_rank_group_t *grp, daos_event_t *ev);

/**
 * Exclude a group of storage targets from a container.
 *
 * NB: This function could be extended and take domain group as parameter?
 *
 * \param coh	[IN]	Container open handle.
 * \param grp	[IN]	A group of targets.
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
 *			-DER_NONEXIST	Storage target is nonexistent
 */
int
dsr_co_exclude(daos_handle_t coh, daos_rank_group_t *grp, daos_event_t *ev);

/**
 * Replace container targets identified by \a grp_old with targets identified
 * by \a grp_new. \a grp_old::rg_nrank and \a grp_new::rg_nrank must be same,
 * targets in these two groups should not have overlap.
 *
 * If \a force is FALSE, this function may return -DER_DOMAIN if domains
 * of new targets cannot match domains of original targets.
 *
 * NB:
 * - This function could be extended and take domain group as parameter?
 * - There could be an upper limit for number of targets being replaced
 *
 * \param coh	[IN]	Container open handle.
 * \param grp_old [IN]	Targets to be replaced.
 * \param grp_new [IN]	Targets to replace the old targets.
 * \param force	[IN]	Force to replace even domain of targets can't match.
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
 *			-DER_NONEXIST	Storage target is nonexistent
 *			-DER_DOMAIN	Domains of new targets cannot match
 *					domains of original targets
 */
int
dsr_co_replace(daos_handle_t coh, daos_rank_group_t *grp_old,
	       daos_rank_group_t *grp_new, bool force, daos_event_t *ev);

/**
 * Object common data structures and APIs.
 */

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

/** Object placement attributes */
typedef struct dsr_obj_pl_attr {
	/** Object placement schema */
	dsr_obj_schema_t		 opa_schema;
	/**
	 * TODO: define HA degrees for object placement
	 * - performance oriented
	 * - high availability oriented
	 * ......
	 */
	unsigned int			 opa_pl_degree;
	/** Resilience method, replication or erasure code */
	dsr_obj_resil_t			 opa_resil;
	/** Initial # stripe count, unnecessary for some schemas */
	unsigned int			 opa_nstripes;
	union {
		struct dsr_pl_repl_args {
			/** Method of replicating */
			unsigned int	 r_method;
			/** Number of replicas */
			unsigned int	 r_num;
			/** TODO: add members to describe */
		} repl;

		/** Erasure coding attributes */
		struct dsr_pl_ec_args {
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
} dsr_obj_pl_attr_t;

/**
 * Create a new object of type \a type.
 *
 * \param coh	[IN]	Container open handle.
 * \param id	[IN]	object ID.
 * \param epoch	[IN]	Epoch to create object.
 * \param type	[IN]	Object type: KV, byte array, segmented array.
 * \param pattr	[IN]	Initial placement attributes of object.
 * \param grp	[IN]	It can either be a group of targets/servers for
 *			explicitly-enumerated object distribution, or one
 *			target as its initial location.
 *			For segmented array(DAOS_OBJ_SEG_ARR), it is mandatory
 *			to be explicitly enumerated object distribution.
 *
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
dsr_obj_create(daos_handle_t coh, daos_obj_id_t id, daos_epoch_t epoch,
	       dsr_obj_type_t type, dsr_obj_pl_attr_t *pattr,
	       daos_rank_group_t *grp, daos_handle_t *oh, daos_event_t *ev);

/**
 * Open an existent object.
 *
 * \param coh	[IN]	Container open handle.
 * \param id	[IN]	Object ID.
 * \param epoch	[IN]	Epoch to open object.
 * \param mode	[IN]	Open mode: read-only, read-write.
 * \param type	[OUT]	Optional, returned object type.
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
	     unsigned int mode, dsr_obj_type_t *type, daos_handle_t *oh,
	     daos_event_t *ev);

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
 * Destroy an object and its ID.
 * All writes to the future epochs of a destroyed object will be discarded.
 *
 * \param coh	[IN]	Container open handle.
 * \param oh	[IN]	Object ID.
 * \param epoch	[IN]	Epoch to destroy object.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Permission denied
 *			-DER_NOEXIST	Nonexistent object ID
 */
int
dsr_obj_destroy(daos_handle_t coh, daos_obj_id_t id, daos_epoch_t epoch,
		daos_event_t *ev);

/**
 * Enumerate all object IDs in a container for a particular epoch.
 *
 * \param coh	[IN]	Container open handle.
 * \param epoch	[IN]	Epoch to list object.
 * \param nobjs	[IN]	array size of \a objs
 * \param objs	[OUT]	output buffer for enumerated object IDs.
 *			Zeroes will be filled if returned object IDs are
 *			less than array size of output buffer.
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
dsr_obj_list(daos_handle_t coh, daos_epoch_t epoch, unsigned int nobjs,
	     daos_obj_id_t *objs, daos_hash_out_t *anchor, daos_event_t *ev);

/**
 * Query attributes of object.
 * Caller should provide at least one of output parameters.
 *
 * \param coh	[IN]	Container open handle.
 * \param id	[IN]	Object ID.
 * \param epoch	[IN]	Epoch to query.
 * \param type	[OUT]	Optional, returned object type.
 * \param grp	[OUT]	Optional, returned object distribution.
 * \param pattr	[OUT]	Optional, returned placement attributes of object.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid container open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_NOEXIST	Nonexistent object ID
 *			-DER_UNREACH	Network is unreachable
 */
int
dsr_obj_query(daos_handle_t coh, daos_obj_id_t id, daos_epoch_t epoch,
	      dsr_obj_type_t *type, daos_rank_group_t *grp,
	      dsr_obj_pl_attr_t *pattr, daos_event_t *ev);

/**
 * Key-Value object APIs.
 */

/**
 * Insert/change/change KV pairs.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch for update.
 * \param nkvs	[IN]	Number of KV pairs.
 * \param kvs	[IN]	An array of KV pairs.
 *			For nonexistent keys, it's KV insertion.
 *			For existent keys, it's KV update or punch:
 *			- update value if kv::kv_val is not NULL
 *			- punch KV if kv::kv_val is NULL
 * \param kvs_p	[OUT]	Optional, it is a pointer array and array size should
 *			also be \a nkvs, updated/punched KVs will be stored in
 *			this array.
 *
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
dsr_obj_kv_update(daos_handle_t oh, daos_epoch_t epoch, unsigned int nkvs,
		  daos_kv_t *kvs, daos_kv_t **kvs_p, daos_event_t *ev);

/**
 * Look up values for an array of keys.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch for lookup.
 * \param nkvs	[IN]	Number of KV pairs.
 * \param kvs	[IN/OUT]
 *			An array of KV pairs, key parts are input parameters,
 *			if kv_va of \a kvs are NULL, length of value will be
 *			returned, otherwise they will be filled with values.
 * \param kvs_p	[OUT]	It is a pointer array and array size should also be
 *			\a nkvs, all found KVs will be stored in this array.
 *			NULLs will be filled for those unfound keys.
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
dsr_obj_kv_lookup(daos_handle_t oh, daos_epoch_t epoch, unsigned int nkvs,
		  daos_kv_t *kvs, daos_kv_t **kvs_p, daos_event_t *ev);

/**
 * Enumerate KV pairs of KV object.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch for enumerate.
 * \param nkvs	[IN]	Number of KV pairs.
 * \param kvs	[OUT]	An array of KV pairs as sink buffer.
 *			if key parts or/and value parts of \a kvs are NULL,
 *			length of key/value will be returned, otherwise
 *			they will be filled with returned KV pairs.
 * \param kvs_p	[OUT]	It is a pointer array and array size should also be
 *			\a nkvs, all enumerated KVs will be stored in this
 *			array.
 *			NULLs will be filled for those unfound keys.
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
dsr_obj_kv_list(daos_handle_t oh, daos_epoch_t epoch, unsigned int nkvs,
		daos_kv_t *kvs, daos_kv_t **kvs_p, daos_hash_out_t *anchor,
		daos_event_t *ev);

/**
 * Byte-array object APIs.
 */

/**
 * Read data extents from a byte-array object.
 *
 * NB: A hole means a non-filled or punched extent of an object.
 *
 * - If \a holes is NULL, cooresponding sink buffers for hole extents
 *   will be filled with zeroes in sink buffers.
 *
 * - If \a holes is not NULL, but sink buffer \a sgl is NULL, then this
 *   function only enumerates holes. Those holes overlapping with \a exts
 *   will be returned.
 *
 *   If there are less holes than number of entries in \a holes, then iov_nob
 *   of the last hole in \a holes is -1.
 *
 *   if \a holes::el_num is zero, then number of holes will be returned
 *
 * - If sink buffer \a sgl is not NULL, hole extents overlapping with \a exts
 *   extents will be stored in \a holes, nothing will be filled into
 *   corresponding sink buffers, user should check \a holes and skip these
 *   hole extents in \a sgl.
 *
 *   If there are equal or more holes than number of entries in \a holes, then
 *   corresponding sink buffers behind the last hole have no valid data.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch for read.
 * \param exts	[IN]	Source extents of the object for read.
 * \param holes	[OUT]	Optional, returned object holes.
 * \param sgl	[OUT]	Optional, sink buffers for read.
 *			If it is NULL, read will only scan holes of object.
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
dsr_obj_read(daos_handle_t oh, daos_epoch_t epoch, daos_ext_list_t *exts,
	     daos_ext_list_t *holes, daos_sg_list_t *sgl, daos_event_t *ev);

/**
 * Write data extents to a byte-array object if \a sgl is not NULL, otherwise
 * discard data extents described by \a exts.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch for write.
 * \param exts	[IN]	Sink extents of the object for write,
 *			or extents to punch if \a sgl is NULL.
 * \param sgl	[IN]	Optional, source buffers of write,
 *			it should be NULL for punch.
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
dsr_obj_update(daos_handle_t oh, daos_epoch_t epoch, daos_ext_list_t *exts,
	       daos_sg_list_t *sgl, daos_event_t *ev);

#if 0

/**
 * segmented array object data structure and APIs
 */

/**
 * Read data extents from a segmented-array object.
 * See \a daos_obj_read for detail description of holes
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch for read.
 * \param uuid	[IN]	Target/server group uuid.
 * \param rank	[IN]	Target/server rank to identify a segment.
 * \param exts	[IN]	Source extents of the object for read.
 * \param holes	[OUT]	Optional, returned object holes.
 * \param sgl	[OUT]	Sink buffers for read.
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
 *			-DER_EP_OLD	epoch is too old and has no data
 */
int
dsr_obj_seg_read(daos_handle_t oh, daos_epoch_t epoch, uuid_t uuid,
		 daos_rank_t rank, daos_ext_list_t *exts,
		 daos_ext_list_t *holes, daos_sg_list_t *sgl,
		 daos_event_t *ev);

/**
 * Write data extents to a segmented-array object.
 *
 * \param oh	[IN]	Object handle.
 * \param epoch	[IN]	Epoch for write.
 * \param uuid	[IN]	Target/server group uuid.
 * \param rank	[IN]	Target/server rank to identify a segment.
 * \param exts	[IN]	Sink extents of the object for write.
 * \param sgl	[IN]	Source buffers of write.
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
dsr_obj_seg_write(daos_handle_t oh, daos_epoch_t epoch, uuid_t uuid,
		  daos_rank_t rank, daos_ext_list_t *exts,
		  daos_sg_list_t *sgl, daos_event_t *ev);

/**
 * Discard object data extents described by \a exts.
 *
 * \param oh	[IN]	Object handle.
 * \param epoch	[IN]	Epoch for punch.
 * \param uuid	[IN]	Target/server group uuid.
 * \param rank	[IN]	Target/server rank to identify a segment of object.
 * \param exts	[IN]	Object extents to punch.
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
 *			-DER_IO_INVAL	Object extents have overlap
 *			-DER_EP_RO	Epoch is read-only
 */
int
dsr_obj_seg_punch(daos_handle_t oh, daos_epoch_t epoch, uuid_t uuid,
		  daos_rank_t rank, daos_ext_list_t *exts, daos_event_t *ev);

#endif

/************************************************************************
 * TODO: Epoch APIs
 */

#endif /* __DSR_API_H__ */
