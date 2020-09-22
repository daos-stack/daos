/**
 * (C) Copyright 2017-2020 Intel Corporation.
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
 * DAOS task-based API
 */

#ifndef __DAOS_TASK_H__
#define __DAOS_TASK_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <daos_types.h>
#include <daos_obj.h>
#include <daos_kv.h>
#include <daos_array.h>
#include <daos_errno.h>
#include <daos_prop.h>
#include <daos_cont.h>
#include <daos_pool.h>
#include <daos_mgmt.h>
#include <daos/tse.h>

/** DAOS operation codes for task creation */
typedef enum {
	DAOS_OPC_INVALID	= -1,

	/** Management APIs */
	DAOS_OPC_SVC_RIP = 0,
	DAOS_OPC_POOL_CREATE,
	DAOS_OPC_POOL_DESTROY,
	DAOS_OPC_POOL_EXTEND,
	DAOS_OPC_POOL_EVICT,
	DAOS_OPC_SET_PARAMS,
	DAOS_OPC_POOL_ADD_REPLICAS,
	DAOS_OPC_POOL_REMOVE_REPLICAS,
	DAOS_OPC_MGMT_LIST_POOLS,

	/** Pool APIs */
	DAOS_OPC_POOL_CONNECT,
	DAOS_OPC_POOL_DISCONNECT,
	DAOS_OPC_POOL_EXCLUDE,
	DAOS_OPC_POOL_EXCLUDE_OUT,
	DAOS_OPC_POOL_ADD,
	DAOS_OPC_POOL_QUERY,
	DAOS_OPC_POOL_QUERY_TARGET,
	DAOS_OPC_POOL_LIST_ATTR,
	DAOS_OPC_POOL_GET_ATTR,
	DAOS_OPC_POOL_SET_ATTR,
	DAOS_OPC_POOL_DEL_ATTR,
	DAOS_OPC_POOL_STOP_SVC,
	DAOS_OPC_POOL_LIST_CONT,

	/** Container APIs */
	DAOS_OPC_CONT_CREATE,
	DAOS_OPC_CONT_OPEN,
	DAOS_OPC_CONT_CLOSE,
	DAOS_OPC_CONT_DESTROY,
	DAOS_OPC_CONT_QUERY,
	DAOS_OPC_CONT_SET_PROP,
	DAOS_OPC_CONT_UPDATE_ACL,
	DAOS_OPC_CONT_DELETE_ACL,
	DAOS_OPC_CONT_AGGREGATE,
	DAOS_OPC_CONT_ROLLBACK,
	DAOS_OPC_CONT_SUBSCRIBE,
	DAOS_OPC_CONT_LIST_ATTR,
	DAOS_OPC_CONT_GET_ATTR,
	DAOS_OPC_CONT_SET_ATTR,
	DAOS_OPC_CONT_DEL_ATTR,
	DAOS_OPC_CONT_ALLOC_OIDS,
	DAOS_OPC_CONT_LIST_SNAP,
	DAOS_OPC_CONT_CREATE_SNAP,
	DAOS_OPC_CONT_DESTROY_SNAP,

	/** Transaction APIs */
	DAOS_OPC_TX_OPEN,
	DAOS_OPC_TX_COMMIT,
	DAOS_OPC_TX_ABORT,
	DAOS_OPC_TX_OPEN_SNAP,
	DAOS_OPC_TX_CLOSE,
	DAOS_OPC_TX_RESTART,

	/** Object APIs */
	DAOS_OPC_OBJ_REGISTER_CLASS,
	DAOS_OPC_OBJ_QUERY_CLASS,
	DAOS_OPC_OBJ_LIST_CLASS,
	DAOS_OPC_OBJ_OPEN,
	DAOS_OPC_OBJ_CLOSE,
	DAOS_OPC_OBJ_PUNCH,
	DAOS_OPC_OBJ_PUNCH_DKEYS,
	DAOS_OPC_OBJ_PUNCH_AKEYS,
	DAOS_OPC_OBJ_QUERY,
	DAOS_OPC_OBJ_QUERY_KEY,
	DAOS_OPC_OBJ_SYNC,
	DAOS_OPC_OBJ_FETCH,
	DAOS_OPC_OBJ_UPDATE,
	DAOS_OPC_OBJ_LIST_DKEY,
	DAOS_OPC_OBJ_LIST_AKEY,
	DAOS_OPC_OBJ_LIST_RECX,
	DAOS_OPC_OBJ_LIST_OBJ,

	/** Array APIs */
	DAOS_OPC_ARRAY_CREATE,
	DAOS_OPC_ARRAY_OPEN,
	DAOS_OPC_ARRAY_CLOSE,
	DAOS_OPC_ARRAY_DESTROY,
	DAOS_OPC_ARRAY_READ,
	DAOS_OPC_ARRAY_WRITE,
	DAOS_OPC_ARRAY_PUNCH,
	DAOS_OPC_ARRAY_GET_SIZE,
	DAOS_OPC_ARRAY_SET_SIZE,

	/** HL APIs */
	DAOS_OPC_KV_GET,
	DAOS_OPC_KV_PUT,
	DAOS_OPC_KV_REMOVE,
	DAOS_OPC_KV_LIST,

	DAOS_OPC_MAX
} daos_opc_t;

/** svc rip params */
typedef struct {
	/** Process set name of the DAOS servers managing the pool */
	const char		*grp;
	/** rank to kill */
	d_rank_t		rank;
	/** Abrupt shutdown, no cleanup */
	bool			force;
} daos_svc_rip_t;

/** mgmt set params */
typedef struct {
	/** Process set name of the DAOS servers managing the pool */
	const char		*grp;
	/** Ranks to set parameter. -1 means setting on all servers */
	d_rank_t		rank;
	/** key ID of the parameter */
	uint32_t		key_id;
	/**  value of the parameter */
	uint64_t		value;
	/** optional extra value to set the fail */
	uint64_t		value_extra;
} daos_set_params_t;

/** pool create params */
typedef struct {
	/** Capabilities permitted for the pool. */
	uint32_t		mode;
	/** User owning the pool */
	uid_t			uid;
	/** Group owning the pool */
	gid_t			gid;
	/** Process set name of the DAOS servers managing the pool. */
	const char		*grp;
	/** Optional, allocate targets on this list of ranks. */
	const d_rank_list_t	*tgts;
	/** String identifying the target devices to use. */
	const char		*dev;
	/** Target SCM (Storage Class Memory) size in bytes. */
	daos_size_t		scm_size;
	/** Target NVMe (Non-Volatile Memory express) size in bytes. */
	daos_size_t		nvme_size;
	/** Optional, pool properties. */
	daos_prop_t		*prop;
	/** Number of desired pool service replicas. */
	d_rank_list_t		*svc;
	/** UUID of the pool created */
	unsigned char		*uuid;
} daos_pool_create_t;

/** pool destroy args */
typedef struct {
	/** UUID of the pool to destroy. */
	const uuid_t		uuid;
	/** Process set name of the DAOS servers managing the pool */
	const char		*grp;
	/** Force destruction even if there are active connections */
	int			force;
} daos_pool_destroy_t;

/** pool extend args */
typedef struct {
	/** UUID of the pool to extend. */
	const uuid_t		uuid;
	/** Process set name of the DAOS servers managing the pool. */
	const char		*grp;
	/**  Optional, only extend the pool to included targets. */
	d_rank_list_t		*tgts;
	/** Optional, buffer to store faulty targets on failure. */
	d_rank_list_t		*failed;
} daos_pool_extend_t;

/** pool evict args */
typedef struct {
	/** UUID of the pool. */
	const uuid_t		uuid;
	/** Process set name of the DAOS servers managing the pool. */
	const char		*grp;
	/** list of pool service ranks. */
	d_rank_list_t		*svc;
} daos_pool_evict_t;

/** pool connect args */
typedef struct {
	/** UUID of the pool. */
	const uuid_t		uuid;
	/** Process set name of the DAOS servers managing the pool. */
	const char		*grp;
	/** Pool service replica ranks. */
	const d_rank_list_t	*svc;
	/** Connect mode represented by the DAOS_PC_ bits. */
	unsigned int		flags;
	/** Returned open handle. */
	daos_handle_t		*poh;
	/** Optional, returned pool information. */
	daos_pool_info_t	*info;
} daos_pool_connect_t;

/** poo disconnect args */
typedef struct {
	/** Pool open handle. */
	daos_handle_t		poh;
} daos_pool_disconnect_t;

/** pool target update (add/exclude) args */
typedef struct {
	/** UUID of the pool. */
	const uuid_t		uuid;
	/** Process set name of the DAOS servers managing the pool */
	const char		*grp;
	/** Pool service replica ranks. */
	d_rank_list_t		*svc;
	/** Target array */
	struct d_tgt_list	*tgts;
} daos_pool_update_t;

/** pool query args */
typedef struct {
	/** Pool open handle. */
	daos_handle_t		poh;
	/** Optional, returned storage targets in this pool. */
	d_rank_list_t		*tgts;
	/** Optional, returned pool information. */
	daos_pool_info_t	*info;
	/** Optional, returned pool properties. */
	daos_prop_t		*prop;
} daos_pool_query_t;

/** pool target query args */
typedef struct {
	/** Pool open handle. */
	daos_handle_t		poh;
	/** Array of targets to query. */
	d_rank_list_t		*tgts;
	/** Optional, buffer to store faulty targets on failure. */
	d_rank_list_t		*failed;
	/** Returned storage information of targets. */
	daos_target_info_t	*info_list;
} daos_pool_query_target_t;

/** pool container list args */
typedef struct {
	/** Pool open handle. */
	daos_handle_t			 poh;
	/** [in] length of \a cont_buf. [out] num of containers in the pool. */
	daos_size_t			*ncont;
	/** Array of container structures. */
	struct daos_pool_cont_info	*cont_buf;
} daos_pool_list_cont_t;

/** pool list attributes args */
typedef struct {
	/** Pool open handle. */
	daos_handle_t		poh;
	/** Buffer containing concatenation of all attribute names. */
	char			*buf;
	/** [in]: Buffer size. [out]: Aggregate size of all attribute names */
	size_t			*size;
} daos_pool_list_attr_t;

/** pool get attributes args */
typedef struct {
	/** Pool open handle. */
	daos_handle_t		poh;
	/** Number of attributes. */
	int			n;
	/** Array of \a n null-terminated attribute names. */
	char    const *const	*names;
	/** Array of \a n buffers to store attribute values. */
	void   *const		*values;
	/** [in]: Array of \a n buf sizes. [out]: Array of actual sizes. */
	size_t			*sizes;
} daos_pool_get_attr_t;

/** pool set attributes args */
typedef struct {
	/** Pool open handle. */
	daos_handle_t		poh;
	/** Number of attributes. */
	int			n;
	/** Array of \a n null-terminated attribute names. */
	char   const *const	*names;
	/** Array of \a n attribute values. */
	void   const *const	*values;
	/** Array of \a n elements containing the sizes of attribute values. */
	size_t const		*sizes;
} daos_pool_set_attr_t;

/** pool del attributes args */
typedef struct {
	/** Pool open handle. */
	daos_handle_t		poh;
	/** Number of attributes. */
	int			n;
	/** Array of \a n null-terminated attribute names. */
	char   const *const	*names;
} daos_pool_del_attr_t;

/** pool add/remove replicas args */
typedef struct {
	/** UUID of the pool. */
	const uuid_t		uuid;
	/** Name of DAOS server process set managing the service. */
	const char		*group;
	/** List of service ranks. */
	d_rank_list_t		*svc;
	/** Ranks of the replicas to be added/removed. */
	d_rank_list_t		*targets;
	/** Optional, list of ranks which could not be added/removed. */
	d_rank_list_t		*failed;
} daos_pool_replicas_t;

/** pool management pool list args */
typedef struct {
	/** Process set name of the DAOS servers managing the pool */
	const char		*grp;
	/** Array of pool mgmt information structures. */
	daos_mgmt_pool_info_t	*pools;
	/** length of array */
	daos_size_t		*npools;
} daos_mgmt_list_pools_t;

/** pool service stop args */
typedef struct {
	/** Pool open handle. */
	daos_handle_t		poh;
} daos_pool_stop_svc_t;

/** Container create args */
typedef struct {
	/** Pool open handle. */
	daos_handle_t		poh;
	/** Container UUID. */
	const uuid_t		uuid;
	/** Optional container properties. */
	daos_prop_t		*prop;
} daos_cont_create_t;

/** Container open args */
typedef struct {
	/** Pool open handle. */
	daos_handle_t		poh;
	/** Container UUID. */
	const uuid_t		uuid;
	/** Open mode, represented by the DAOS_COO_ bits.*/
	unsigned int		flags;
	/** Returned container open handle. */
	daos_handle_t		*coh;
	/** Optional, return container information. */
	daos_cont_info_t	*info;
} daos_cont_open_t;

/** Container close args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
} daos_cont_close_t;

/** Container destroy args */
typedef struct {
	/** Pool open handle. */
	daos_handle_t		poh;
	/** Container UUID. */
	const uuid_t		uuid;
	/** Force destroy even if there is outstanding open handles. */
	int			force;
} daos_cont_destroy_t;

/** Container query args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Returned container information. */
	daos_cont_info_t	*info;
	/** Optional, returned container properties. */
	daos_prop_t		*prop;
} daos_cont_query_t;

/** Container set properties args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Property entries to set/update. */
	daos_prop_t		*prop;
} daos_cont_set_prop_t;

/** Container ACL update args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** ACL containing new/updated entries. */
	struct daos_acl		*acl;
} daos_cont_update_acl_t;

/** Container ACL delete args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Principal type to be removed. */
	uint8_t			type;
	/** Name of principal to be removed. */
	d_string_t		name;
} daos_cont_delete_acl_t;

/** Container aggregate args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Epoch to be aggregated to. Current time if 0.*/
	daos_epoch_t		epoch;
} daos_cont_aggregate_t;

/** Container rollback args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Epoch of a persistent snapshot to rollback to. */
	daos_epoch_t		epoch;
} daos_cont_rollback_t;

/** Container subscribe args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/*
	 * [in]: epoch of snapshot to wait for.
	 * [out]: epoch of persistent snapshot taken.
	 */
	daos_epoch_t		*epoch;
} daos_cont_subscribe_t;

/** Container attribute list args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Buffer containing concatenation of all attribute names. */
	char			*buf;
	/** [in]: Buffer size. [out]: Aggregate size of all attribute names. */
	size_t			*size;
} daos_cont_list_attr_t;

/** Container attribute get args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Number of attributes. */
	int			n;
	/** Array of \a n null-terminated attribute names. */
	char    const *const	*names;
	/** Array of \a n buffers to store attribute values. */
	void   *const		*values;
	/**[in]: Array of \a n buffer sizes. [out]: Array of actual sizes */
	size_t			*sizes;
} daos_cont_get_attr_t;

/** Container attribute set args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Number of attributes. */
	int			n;
	/** Array of \a n null-terminated attribute names. */
	char   const *const	*names;
	/** Array of \a n attribute values. */
	void   const *const	*values;
	/** Array of \a n elements containing the sizes of attribute values. */
	size_t const		*sizes;
} daos_cont_set_attr_t;

/** Container attribute del args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Number of attributes. */
	int			n;
	/** Array of \a n null-terminated attribute names. */
	char   const *const	*names;
} daos_cont_del_attr_t;

/** Container Object ID allocation args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Number of unique IDs requested. */
	daos_size_t		num_oids;
	/** starting oid that was allocated up to oid + num_oids. */
	uint64_t		*oid;
} daos_cont_alloc_oids_t;

/** Container snapshot listing args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/*
	 * [in]: Number of snapshots in epochs and names.
	 * [out]: Actual number of snapshots returned
	 */
	int			*nr;
	/** preallocated array of epochs to store snapshots. */
	daos_epoch_t		*epochs;
	/** preallocated array of names of the snapshots. */
	char			**names;
	/** Hash anchor for the next call. */
	daos_anchor_t		*anchor;
} daos_cont_list_snap_t;

/** Container snapshot creation args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Returned epoch of persistent snapshot taken. */
	daos_epoch_t		*epoch;
	/** Optional null terminated name for snapshot. */
	char			*name;
} daos_cont_create_snap_t;

/** Container snapshot destroy args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Epoch range of snapshots to destroy. */
	daos_epoch_range_t	epr;
} daos_cont_destroy_snap_t;

/** Transaction Open args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Returned transaction open handle. */
	daos_handle_t		*th;
	/** Transaction flags. */
	uint64_t		flags;
} daos_tx_open_t;

/** Transaction commit args */
typedef struct {
	/** Transaction open handle. */
	daos_handle_t		th;
	/** Control commit behavior, such as retry. */
	uint32_t		flags;
} daos_tx_commit_t;

/** Transaction abort args */
typedef struct {
	/** Transaction open handle. */
	daos_handle_t		th;
} daos_tx_abort_t;

/** Transaction snapshot open args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Epoch of persistent snapshot to read from. */
	daos_epoch_t		epoch;
	/** Returned transaction open handle. */
	daos_handle_t		*th;
} daos_tx_open_snap_t;

/** Transaction close args */
typedef struct {
	/** Transaction open handle. */
	daos_handle_t		th;
} daos_tx_close_t;

/** Transaction restart args */
typedef struct {
	/** Transaction open handle. */
	daos_handle_t		th;
} daos_tx_restart_t;

/** Object class register args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Object class ID. */
	daos_oclass_id_t	cid;
	/** Object class attributes. */
	struct daos_oclass_attr	*cattr;
} daos_obj_register_class_t;

/** Object class query args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Object class ID. */
	daos_oclass_id_t	cid;
	/** Object class attributes. */
	struct daos_oclass_attr	*cattr;
} daos_obj_query_class_t;

/** Object class list args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Sink buffer for returned class list. */
	struct daos_oclass_list	*clist;
	/** Hash anchor for the next call. */
	daos_anchor_t		*anchor;
} daos_obj_list_class_t;

/** Object open args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Object ID. */
	daos_obj_id_t		oid;
	/** Object open mode. */
	unsigned int		mode;
	/** Returned object handle. */
	daos_handle_t		*oh;
} daos_obj_open_t;

/** Object close args */
typedef struct {
	/** Object open handle */
	daos_handle_t		oh;
} daos_obj_close_t;

/*
 * Object & Object Key Punch args.
 * NB:
 * - If @dkey is NULL, it is parameter for object punch.
 * - If @akeys is NULL, it is parameter for dkey punch.
 * - API allows user to punch multiple dkeys, in this case, client module needs
 *   to allocate multiple instances of this data structure.
 */
typedef struct {
	/** Transaction open handle. */
	daos_handle_t		th;
	/** Object open handle */
	daos_handle_t		oh;
	/** Distribution Key. */
	daos_key_t		*dkey;
	/** Array of attribute keys. */
	daos_key_t		*akeys;
	/** Operation flags. */
	uint64_t		flags;
	/** Number of akeys in \a akeys. */
	unsigned int		akey_nr;
} daos_obj_punch_t;

/** Object query args */
typedef struct {
	/** Object open handle */
	daos_handle_t		oh;
	/** Transaction open handle. */
	daos_handle_t		th;
	/** Returned object attributes. */
	struct daos_obj_attr	*oa;
	/** Ordered list of ranks where the object is stored. */
	d_rank_list_t		*ranks;
} daos_obj_query_t;

/** Object key query args */
typedef struct {
	/** Object open handle */
	daos_handle_t		oh;
	/** Transaction open handle. */
	daos_handle_t		th;
	/*
	 * [in]: allocated integer dkey.
	 * [out]: max or min dkey (if flag includes dkey query).
	 */
	daos_key_t		*dkey;
	/*
	 * [in]: allocated integer akey.
	 * [out]: max or min akey (if flag includes akey query).
	 */
	daos_key_t		*akey;
	/** max or min offset in key, and size of the extent at the offset. */
	daos_recx_t		*recx;
	/** Operation flags. */
	uint64_t		flags;
} daos_obj_query_key_t;

/** Object fetch/update args */
typedef struct {
	/** Transaction open handle. */
	daos_handle_t		th;
	/** Object open handle */
	daos_handle_t		oh;
	/** API flags. */
	uint64_t		flags;
	/** Distribution Key. */
	daos_key_t		*dkey;
	/** Number of elements in \a iods and \a sgls. */
	uint32_t		nr;
	/** Internal flags. */
	uint32_t		extra_flags;
	/** IO descriptor describing IO layout in the object. */
	daos_iod_t		*iods;
	/** Scatter / gather list for a memory descriptor. */
	d_sg_list_t		*sgls;
	/** IO Map - only valid for fetch. */
	daos_iom_t		*ioms;
	/** extra arguments, for example obj_ec_fail_info for DIOF_EC_RECOV */
	void			*extra_arg;
} daos_obj_rw_t;

/** fetch args struct */
typedef daos_obj_rw_t		daos_obj_fetch_t;
/** update args struct */
typedef daos_obj_rw_t		daos_obj_update_t;

/** Object sync args */
struct daos_obj_sync_args {
	/** Object open handle */
	daos_handle_t		oh;
	/** epoch. */
	daos_epoch_t		epoch;
	/** epochp. */
	daos_epoch_t		**epochs_p;
	/** nr. */
	int			*nr;
};

/** Object list args */
typedef struct {
	/** Object open handle */
	daos_handle_t		oh;
	/** Transaction open handle. */
	daos_handle_t		th;
	/** Distribution key. */
	daos_key_t		*dkey;
	/** Attribute key. */
	daos_key_t		*akey;
	/** number of dkeys/akeys/kds entries */
	uint32_t		*nr;
	/** Key descriptors holding enumerated keys. */
	daos_key_desc_t		*kds;
	/** Scatter gather list for memory buffer. */
	d_sg_list_t		*sgl;
	/** total buf size for sgl buf, in case it uses bulk transfer. */
	daos_size_t		*size;
	/** type of value. */
	daos_iod_type_t		type;
	/** record extents. */
	daos_recx_t		*recxs;
	/** epoch ranges */
	daos_epoch_range_t	*eprs;
	/* anchors for obj list -
	 * list_dkey uses dkey_anchor,
	 * list_akey uses akey_anchor,
	 * list_recx uses anchor,
	 * list_obj uses all the 3 anchors.
	 */
	daos_anchor_t		*anchor;
	/** anchor for list_dkey. */
	daos_anchor_t		*dkey_anchor;
	/** anchor for list_akey. */
	daos_anchor_t		*akey_anchor;
	/** versions. */
	uint32_t		*versions;
	/** Serialized checksum info for enumerated keys and data in sgl.
	 * (for internal use only)
	 */
	d_iov_t			*csum;
	/** order. */
	bool			incr_order;
} daos_obj_list_t;

/**
 * parameter subset for list_dkey -
 * daos_handle_t	oh;
 * daos_handle_t	th;
 * uint32_t		*nr;
 * daos_key_desc_t	*kds;
 * d_sg_list_t		*sgl;
 * daos_anchor_t	*dkey_anchor;
*/
typedef daos_obj_list_t		daos_obj_list_dkey_t;

/**
 * parameter subset for list_akey -
 * daos_handle_t	oh;
 * daos_handle_t	th;
 * daos_key_t		*dkey;
 * uint32_t		*nr;
 * daos_key_desc_t	*kds;
 * d_sg_list_t		*sgl;
 * daos_anchor_t	*akey_anchor;
*/
typedef daos_obj_list_t		daos_obj_list_akey_t;

/**
 * parameter subset for list_recx -
 * daos_handle_t	oh;
 * daos_handle_t	th;
 * daos_key_t		*dkey;
 * daos_key_t		*akey;
 * daos_size_t		*size;
 * daos_iod_type_t	type;
 * uint32_t		*nr;
 * daos_recx_t		*recxs;
 * daos_epoch_range_t	*eprs;
 * daos_anchor_t	*anchor;
 * uint32_t		*versions;
 * bool			incr_order;
*/
typedef daos_obj_list_t		daos_obj_list_recx_t;

/**
 * parameter subset for list_obj -
 * daos_handle_t	oh;
 * daos_handle_t	th;
 * daos_key_t		*dkey;
 * daos_key_t		*akey;
 * daos_size_t		*size;
 * uint32_t		*nr;
 * daos_key_desc_t	*kds;
 * daos_recx_t		*recxs;
 * daos_epoch_range_t	*eprs;
 * d_sg_list_t		*sgl;
 * daos_anchor_t	*anchor;
 * daos_anchor_t	*dkey_anchor;
 * daos_anchor_t	*akey_anchor;
 * uint32_t		*versions;
 * bool			incr_order;
*/
typedef daos_obj_list_t		daos_obj_list_obj_t;

/** Array create args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Array ID. */
	daos_obj_id_t		oid;
	/** Transaction open handle. */
	daos_handle_t		th;
	/** Size of array records. */
	daos_size_t		cell_size;
	/** Number of records stored under 1 dkey. */
	daos_size_t		chunk_size;
	/** Returned array open handle */
	daos_handle_t		*oh;
} daos_array_create_t;

/** Array open args */
typedef struct {
	/** Container open handle. */
	daos_handle_t		coh;
	/** Array ID, */
	daos_obj_id_t		oid;
	/** Transaction open handle. */
	daos_handle_t		th;
	/** Open mode. */
	unsigned int		mode;
	/** flag whether cell and chunk size are user provided. */
	unsigned int		open_with_attr;
	/** Size of array records. */
	daos_size_t		*cell_size;
	/** Number if records stored under 1 dkey. */
	daos_size_t		*chunk_size;
	/** Returned Array open handle */
	daos_handle_t		*oh;
} daos_array_open_t;

/** Array close args */
typedef struct {
	/** Array open handle. */
	daos_handle_t		oh;
} daos_array_close_t;

/** Array read/write args */
typedef struct {
	/** Array open handle. */
	daos_handle_t		oh;
	/** Transaction open handle. */
	daos_handle_t		th;
	/** Array IO descriptos. */
	daos_array_iod_t	*iod;
	/** memory descriptors. */
	d_sg_list_t		*sgl;
} daos_array_io_t;

/** Array get size args */
typedef struct {
	/** Array open handle. */
	daos_handle_t		oh;
	/** Transaction open handle. */
	daos_handle_t		th;
	/** Returned array size in number of records. */
	daos_size_t		*size;
} daos_array_get_size_t;

/** Array set size args */
typedef struct {
	/** Array open handle. */
	daos_handle_t		oh;
	/** Transaction open handle. */
	daos_handle_t		th;
	/** truncate size of the array. */
	daos_size_t		size;
} daos_array_set_size_t;

/** Array destroy args */
typedef struct {
	/** Array open handle. */
	daos_handle_t		oh;
	/** Transaction open handle. */
	daos_handle_t		th;
} daos_array_destroy_t;

/** KV get args */
typedef struct {
	/** KV open handle. */
	daos_handle_t		oh;
	/** Transaction open handle. */
	daos_handle_t		th;
	/** Operation flags. */
	uint64_t		flags;
	/** Key. */
	const char		*key;
	/** Value buffer size. */
	daos_size_t		*buf_size;
	/** Value buffer. */
	void			*buf;
} daos_kv_get_t;

/** KV put args */
typedef struct {
	/** KV open handle. */
	daos_handle_t		oh;
	/** Transaction open handle. */
	daos_handle_t		th;
	/** Operation flags. */
	uint64_t		flags;
	/** Key. */
	const char		*key;
	/** Value size. */
	daos_size_t		buf_size;
	/** Value buffer. */
	const void		*buf;
} daos_kv_put_t;

/** KV remove args */
typedef struct {
	/** KV open handle. */
	daos_handle_t		oh;
	/** Transaction open handle. */
	daos_handle_t		th;
	/** Operation flags. */
	uint64_t		flags;
	/** Key. */
	const char		*key;
} daos_kv_remove_t;

/** KV list args */
typedef struct {
	/** KV open handle. */
	daos_handle_t		oh;
	/** Transaction open handle. */
	daos_handle_t		th;
	/*
	 * [in]: number of key descriptors in \a kds.
	 * [out]: number of returned key descriptors.
	 */
	uint32_t		*nr;
	/** key descriptors. */
	daos_key_desc_t		*kds;
	/** memory descriptors. */
	d_sg_list_t		*sgl;
	/** Hash anchor for the next call. */
	daos_anchor_t		*anchor;
} daos_kv_list_t;

/**
 * Create an asynchronous task and associate it with a daos client operation.
 * For synchronous operations please use the specific API for that operation.
 * Typically this API is used for use cases where a list of daos operations need
 * to be queued into the DAOS async engines with specific dependencies for order
 * of execution between those tasks. For example, a user can create task to open
 * an object then update that object with a dependency inserted on the update
 * to the open task.
 * For a simpler workflow, users can use the event based API instead of tasks.
 *
 * \param opc	[IN]	Operation Code to identify the daos op to associate with
 *			the task,
 * \param sched	[IN]	Scheduler / Engine this task will be added to.
 * \param num_deps [IN]	Number of tasks this task depends on before it gets
 *			scheduled. No tasks can be in progress.
 * \param dep_tasks [IN]
 *			Array of tasks that new task will wait on completion
 *			before it's scheduled.
 * \param taskp	[OUT]	Pointer to task to be created/initialized with the op.
 *
 * \return		0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NOSYS	Unsupported opc
 */
int
daos_task_create(daos_opc_t opc, tse_sched_t *sched,
		 unsigned int num_deps, tse_task_t *dep_tasks[],
		 tse_task_t **taskp);

/**
 * Return a pointer to the DAOS task argument structure. This is called to set
 * the arguments for the task before being scheduled, typically after it's
 * created or in its prepare cb. The task must be created with
 * daos_task_create() and a valid DAOS opc.
 *
 * \param task	[IN]	Task to retrieve the struct from.
 *
 * \return		Success: Pointer to arguments for the DAOS task
 */
void *
daos_task_get_args(tse_task_t *task);

/**
 * Return a pointer to the DAOS task private state. If no private state has
 * been set (via daos_task_get_priv()), NULL is returned.
 *
 * \param task	[IN]	Task to retrieve the private state from
 *
 * \return		Pointer to the private state
 */
void *
daos_task_get_priv(tse_task_t *task);

/**
 * Set a pointer to the DAOS task private state.
 *
 * \param task	[IN]	Task to retrieve the private state from
 * \param priv	[IN]	Pointer to the private state
 *
 * \return		private state set by the previous call
 */
void *
daos_task_set_priv(tse_task_t *task, void *priv);

/**
 * Make progress on the RPC context associated with the scheduler and schedule
 * tasks that are ready. Also check if the scheduler has any tasks.
 *
 * \param sched	[IN]	Scheduler to make progress on.
 * \param timeout [IN]	How long is caller going to wait (micro-second)
 *			if \a timeout > 0,
 *			it can also be DAOS_EQ_NOWAIT, DAOS_EQ_WAIT
 * \param is_empty [OUT]
 *			flag to indicate whether the scheduler is empty or not.
 *
 * \return		0 if Success, negative DER if failed.
 */
int
daos_progress(tse_sched_t *sched, int64_t timeout, bool *is_empty);

#if defined(__cplusplus)
}
#endif

#endif /*  __DAOS_TASK_H__ */
