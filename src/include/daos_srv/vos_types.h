/**
 * (C) Copyright 2015-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __VOS_TYPES_H__
#define __VOS_TYPES_H__

#include <daos_types.h>
#include <daos_pool.h>
#include <daos_srv/bio.h>
#include <daos_srv/vea.h>
#include <daos/object.h>
#include <daos/dtx.h>
#include <daos/checksum.h>

#define VOS_SUB_OP_MAX	((uint16_t)-2)

#define VOS_POOL_DF_2_2 24
#define VOS_POOL_DF_2_4 25

struct dtx_rsrvd_uint {
	void			*dru_scm;
	d_list_t		dru_nvme;
};

enum dtx_cos_flags {
	DCF_SHARED		= (1 << 0),
	/* Some DTX (such as for the distributed transaction across multiple
	 * RDGs, or for EC object modification) need to be committed via DTX
	 * RPC instead of piggyback via other dispatched update/punch RPC.
	 */
	DCF_EXP_CMT		= (1 << 1),
};

enum dtx_stat_flags {
	/* Skip bad DTX entries (such as corruptted ones) when stat. */
	DSF_SKIP_BAD		= (1 << 1),
};

struct dtx_cos_key {
	daos_unit_oid_t		oid;
	uint64_t		dkey_hash;
};

enum dtx_entry_flags {
	/* The DTX is the leader */
	DTE_LEADER		= (1 << 0),
	/* The DTX entry is invalid. */
	DTE_INVALID		= (1 << 1),
	/* If the DTX with this flag is non-committed, then others
	 * will be blocked (retry again and again) when access the
	 * data being modified via this DTX. Currently, it is used
	 * for distributed transaction. It also can be used for EC
	 * object modification via standalone update/punch.
	 */
	DTE_BLOCK		= (1 << 2),
	/* The DTX is corrupted, some participant RDG(s) may be lost. */
	DTE_CORRUPTED		= (1 << 3),
	/* The DTX entry on leader does not exist, then not sure the status. */
	DTE_ORPHAN		= (1 << 4),
	/* Related DTX may be only committed on some participants, but not
	 * on all yet, need to be re-committed.
	 */
	DTE_PARTIAL_COMMITTED	= (1 << 5),
};

struct dtx_entry {
	/** The identifier of the DTX. */
	struct dtx_id			 dte_xid;
	/** The pool map version when the DTX happened. */
	uint32_t			 dte_ver;
	/** The reference count. */
	uint32_t			 dte_refs;
	/** The DAOS targets participating in the DTX. */
	struct dtx_memberships		*dte_mbs;
};

/* The 'dte_mbs' must be the last member of 'dtx_entry'. */
D_CASSERT(sizeof(struct dtx_entry) ==
	  offsetof(struct dtx_entry, dte_mbs) +
	  sizeof(struct dtx_memberships *));

/** Pool open flags (for vos_pool_create and vos_pool_open) */
enum vos_pool_open_flags {
	/** Pool is small (for sys space reservation); implies VOS_POF_EXCL */
	VOS_POF_SMALL	= (1 << 0),
	/** Exclusive (-DER_BUSY if already opened) */
	VOS_POF_EXCL	= (1 << 1),
	/** Ignore the pool uuid passed into vos_pool_open */
	VOS_POF_SKIP_UUID_CHECK = (1 << 2),
	/** Caller does VEA flush periodically */
	VOS_POF_EXTERNAL_FLUSH	= (1 << 3),
	/** RDB pool */
	VOS_POF_RDB	= (1 << 4),
	/** SYS DB pool */
	VOS_POF_SYSDB	= (1 << 5),
};

enum vos_oi_attr {
	/** Marks object as failed */
	VOS_OI_FAILED		= (1U << 0),
	/** Marks object as punched */
	VOS_OI_PUNCHED		= (1U << 1),
	/** Marks object has been (or will be) removed */
	VOS_OI_REMOVED		= (1U << 2),
	/** TODO: Additional attributes to support metadata storage for SR */
};

/**
 * VOS garbage collector statistics
 */
struct vos_gc_stat {
	uint64_t	gs_conts;	/**< GCed containers */
	uint64_t	gs_objs;	/**< GCed objects */
	uint64_t	gs_dkeys;	/**< GCed dkeys */
	uint64_t	gs_akeys;	/**< GCed akeys */
	uint64_t	gs_singvs;	/**< GCed single values */
	uint64_t	gs_recxs;	/**< GCed array values */
};

struct vos_pool_space {
	/** Total & free space */
	struct daos_space	vps_space;
	/** Reserved sys space (for space reclaim, rebuild, etc.) in bytes */
	daos_size_t		vps_space_sys[DAOS_MEDIA_MAX];
	/** NVMe block allocator attributes */
	struct vea_attr		vps_vea_attr;
	/** NVMe block allocator statistics */
	struct vea_stat		vps_vea_stat;
};

#define SCM_TOTAL(vps)	((vps)->vps_space.s_total[DAOS_MEDIA_SCM])
#define SCM_FREE(vps)	((vps)->vps_space.s_free[DAOS_MEDIA_SCM])
#define SCM_SYS(vps)	((vps)->vps_space_sys[DAOS_MEDIA_SCM])
#define NVME_TOTAL(vps)	((vps)->vps_space.s_total[DAOS_MEDIA_NVME])
#define NVME_FREE(vps)	((vps)->vps_space.s_free[DAOS_MEDIA_NVME])
#define NVME_SYS(vps)	((vps)->vps_space_sys[DAOS_MEDIA_NVME])

/**
 * pool attributes returned to query
 */
typedef struct {
	/** # of containers in this pool */
	uint64_t		pif_cont_nr;
	/** Space information */
	struct vos_pool_space	pif_space;
	/** garbage collector statistics */
	struct vos_gc_stat	pif_gc_stat;
	/** TODO */
} vos_pool_info_t;

/**
 * container attributes returned to query
 */
typedef struct {
	/** # of objects in this container */
	uint64_t		ci_nobjs;
	/** Used space by container */
	daos_size_t		ci_used;
	/** Highest (Last) aggregated epoch */
	daos_epoch_t		ci_hae;
	/** TODO */
} vos_cont_info_t;

/**
 * object shard metadata stored in VOS
 */
typedef struct {
	/* TODO: metadata for rebuild */
	char			omd_data[64];
} vos_obj_md_t;

/**
 * VOS iterator types
 */
typedef enum {
	VOS_ITER_NONE,
	/** iterate container UUIDs in a pool */
	VOS_ITER_COUUID,
	/** iterate objects within a container */
	VOS_ITER_OBJ,
	/** iterate all d-keys */
	VOS_ITER_DKEY,
	/** iterate all a-keys */
	VOS_ITER_AKEY,
	/** iterate history of a single value */
	VOS_ITER_SINGLE,
	/** iterate record extents and epoch validities of these extents */
	VOS_ITER_RECX,
	/** iterate VOS active-DTX table */
	VOS_ITER_DTX,
} vos_iter_type_t;

static inline int
vos_iter_type_2pack_type(int vos_type)
{
	switch (vos_type) {
	case VOS_ITER_NONE:
		return OBJ_ITER_NONE;
	case VOS_ITER_OBJ:
		return OBJ_ITER_OBJ;
	case VOS_ITER_DKEY:
		return OBJ_ITER_DKEY;
	case VOS_ITER_AKEY:
		return OBJ_ITER_AKEY;
	case VOS_ITER_SINGLE:
		return OBJ_ITER_SINGLE;
	case VOS_ITER_RECX:
		return OBJ_ITER_RECX;
	default:
		D_ASSERTF(0, "Invalid type %d\n", vos_type);
	}

	return 0;
}

/** epoch logic expression for the single value iterator */
typedef enum {
	VOS_IT_EPC_LE		= 0,
	VOS_IT_EPC_GE,
	/** RE: Range enumeration */
	VOS_IT_EPC_RE,
	/** RR: Reverse Range enum */
	VOS_IT_EPC_RR,
	VOS_IT_EPC_EQ,
} vos_it_epc_expr_t;

typedef enum {
	VOS_ITER_PROC_OP_UNKNOWN = 0,
	VOS_ITER_PROC_OP_DELETE = 1,
	VOS_ITER_PROC_OP_MARK_CORRUPT = 2,
} vos_iter_proc_op_t;

enum {
	/** Conditional Op: Punch key if it exists, fail otherwise */
	VOS_OF_COND_PUNCH		= DAOS_COND_PUNCH,
	/** Conditional Op: Insert dkey if it doesn't exist, fail otherwise */
	VOS_OF_COND_DKEY_INSERT		= DAOS_COND_DKEY_INSERT,
	/** Conditional Op: Update dkey if it exists, fail otherwise */
	VOS_OF_COND_DKEY_UPDATE		= DAOS_COND_DKEY_UPDATE,
	/** Conditional Op: Fetch dkey if it exists, fail otherwise */
	VOS_OF_COND_DKEY_FETCH		= DAOS_COND_DKEY_FETCH,
	/** Conditional Op: Insert akey if it doesn't exist, fail otherwise */
	VOS_OF_COND_AKEY_INSERT		= DAOS_COND_AKEY_INSERT,
	/** Conditional Op: Update akey if it exists, fail otherwise */
	VOS_OF_COND_AKEY_UPDATE		= DAOS_COND_AKEY_UPDATE,
	/** Conditional Op: Fetch akey if it exists, fail otherwise */
	VOS_OF_COND_AKEY_FETCH		= DAOS_COND_AKEY_FETCH,
	/** Indicates akey conditions are specified in iod_flags */
	VOS_OF_COND_PER_AKEY		= DAOS_COND_PER_AKEY,
	/* critical update - skip checks on SCM system/held space */
	VOS_OF_CRIT			= (1 << 8),
	/** Instead of update or punch of extents, remove all extents
	 * under the specified range. Intended for internal use only.
	 */
	VOS_OF_REMOVE			= (1 << 9),
	/* only query iod_size */
	VOS_OF_FETCH_SIZE_ONLY		= (1 << 10),
	/* query recx list */
	VOS_OF_FETCH_RECX_LIST		= (1 << 11),
	/* only set read TS */
	VOS_OF_FETCH_SET_TS_ONLY	= (1 << 12),
	/* check the target (obj/dkey/akey) existence */
	VOS_OF_FETCH_CHECK_EXISTENCE	= (1 << 13),
	/** Set when propagating a punch that results in empty subtree */
	VOS_OF_PUNCH_PROPAGATE		= (1 << 14),
	/** replay punch (underwrite) */
	VOS_OF_REPLAY_PC		= (1 << 15),
	/** Dedup update mode */
	VOS_OF_DEDUP			= (1 << 16),
	/** Dedup update with memcmp verify mode */
	VOS_OF_DEDUP_VERIFY		= (1 << 17),
	/** Ignore fetch only used by shadow fetch to ignore the evt fetch */
	VOS_OF_SKIP_FETCH		= (1 << 18),
	/** Operation on EC object (currently only applies to update) */
	VOS_OF_EC			= (1 << 19),
	/** Update from rebuild */
	VOS_OF_REBUILD			= (1 << 20),
};

enum {
	/** Aggregation optimization is enabled for this pool */
	VOS_POOL_FEAT_AGG_OPT = (1ULL << 0),
	/** Pool check is supported for this pool */
	VOS_POOL_FEAT_CHK = (1ULL << 1),
	/** Dynamic evtree root supported for this pool */
	VOS_POOL_FEAT_DYN_ROOT = (1ULL << 2),
};

/** Mask for any conditionals passed to to the fetch */
#define VOS_COND_FETCH_MASK	\
	(VOS_OF_COND_AKEY_FETCH | VOS_OF_COND_DKEY_FETCH)

/** Mask for akey conditionals passed to to the update */
#define VOS_COND_AKEY_UPDATE_MASK					\
	(VOS_OF_COND_AKEY_UPDATE | VOS_OF_COND_AKEY_INSERT)

/** Mask for dkey conditionals passed to to the update */
#define VOS_COND_DKEY_UPDATE_MASK					\
	(VOS_OF_COND_DKEY_UPDATE | VOS_OF_COND_DKEY_INSERT)

/** Mask for any conditionals passed to to the update */
#define VOS_COND_UPDATE_MASK					\
	(VOS_COND_DKEY_UPDATE_MASK | VOS_COND_AKEY_UPDATE_MASK)

/** Mask for if the update has any conditional update */
#define VOS_COND_UPDATE_OP_MASK					\
	(VOS_OF_COND_DKEY_UPDATE | VOS_OF_COND_AKEY_UPDATE)

D_CASSERT((VOS_OF_REPLAY_PC & DAOS_COND_MASK) == 0);
D_CASSERT((VOS_OF_PUNCH_PROPAGATE & DAOS_COND_MASK) == 0);

/** vos definitions that match daos_obj_key_query flags */
enum {
	/** retrieve the max of dkey, akey, and/or idx of array value */
	VOS_GET_MAX		= DAOS_GET_MAX,
	/** retrieve the min of dkey, akey, and/or idx of array value */
	VOS_GET_MIN		= DAOS_GET_MIN,
	/** retrieve the dkey */
	VOS_GET_DKEY		= DAOS_GET_DKEY,
	/** retrieve the akey */
	VOS_GET_AKEY		= DAOS_GET_AKEY,
	/** retrieve the idx of array value */
	VOS_GET_RECX		= DAOS_GET_RECX,
	/**
	 * Internal flag to indicate retrieve the idx of EC array value,
	 * in that case need to retrieve both normal space and parity space
	 * (parity space with DAOS_EC_PARITY_BIT in the recx index).
	 */
	VOS_GET_RECX_EC		= (1 << 5),
	/** Internal flag to indicate timestamps are used */
	VOS_USE_TIMESTAMPS	= (1 << 6),
};

D_CASSERT((VOS_USE_TIMESTAMPS & (VOS_GET_MAX | VOS_GET_MIN | VOS_GET_DKEY |
				 VOS_GET_AKEY | VOS_GET_RECX)) == 0);

enum {
	/** The absence of any flags means iterate all unsorted extents */
	VOS_IT_RECX_ALL		= 0,
	/** Include visible extents in sorted iteration */
	VOS_IT_RECX_VISIBLE	= (1 << 0),
	/** Include covered extents, implies VOS_IT_RECX_VISIBLE */
	VOS_IT_RECX_COVERED	= (1 << 1) | VOS_IT_RECX_VISIBLE,
	/** Include hole extents in sorted iteration
	 *  Only applicable if VOS_IT_RECX_COVERED is not set
	 */
	VOS_IT_RECX_SKIP_HOLES	= (1 << 2),
	/** When sorted iteration is enabled, iterate in reverse */
	VOS_IT_RECX_REVERSE	= (1 << 3),
	/** The iterator is for purge operation */
	VOS_IT_FOR_PURGE	= (1 << 4),
	/** The iterator is for data migration scan */
	VOS_IT_FOR_MIGRATION	= (1 << 5),
	/** Iterate only show punched records in interval */
	VOS_IT_PUNCHED		= (1 << 6),
	/** Cleanup stale DTX entry. */
	VOS_IT_FOR_DISCARD	= (1 << 7),
	/** Entry is not committed */
	VOS_IT_UNCOMMITTED	= (1 << 8),
	/** Mask for all flags */
	VOS_IT_MASK		= (1 << 9) - 1,
};

typedef struct {
	union {
		/** The object id of the entry */
		daos_unit_oid_t	 id_oid;
		/** The key for the entry */
		d_iov_t		 id_key;
	};
	/** Conservative approximation of last aggregatable write for object or key. */
	daos_epoch_t		 id_agg_write;
	/** Timestamp of latest parent punch, if applicable.  Zero if there is no punch */
	daos_epoch_t		 id_parent_punch;
	/** Type of entry */
	vos_iter_type_t		 id_type;
} vos_iter_desc_t;

/** Probe flags for vos_iter_probe_ex */
enum {
	/** Indicate that we should skip the current entry */
	VOS_ITER_PROBE_NEXT	= (1 << 0),
	/** Indicate that we've already invoked probe for this entry */
	VOS_ITER_PROBE_AGAIN	= (1 << 1),
};

/**
 * Iteration object/key filter callback
 */
typedef int (*vos_iter_filter_cb_t)(daos_handle_t ih, vos_iter_desc_t *desc,
				    void *cb_arg, unsigned int *acts);

/**
 * Parameters for initializing VOS iterator
 */
typedef struct {
	/** pool connection handle or container open handle */
	daos_handle_t		ip_hdl;
	/** standalone prepare:	DAOS_HDL_INVAL
	 *  nested prepare:	parent iterator handle
	 */
	daos_handle_t		ip_ih;
	/** Optional, object ID for VOS_ITER_DKEY */
	daos_unit_oid_t		ip_oid;
	/** distribution key (VOS_ITER_AKEY, standalone only) */
	daos_key_t		ip_dkey;
	/** attribute key (VOS_ITER_DKEY/RECX/SINGLE, standalone only) */
	daos_key_t		ip_akey;
	/** address range (RECX); ip_recx.rx_nr == 0 means entire range */
	daos_recx_t             ip_recx;
	/** epoch validity range for the iterator (standalone only) */
	daos_epoch_range_t	ip_epr;
	/** epoch logic expression for the iterator. */
	vos_it_epc_expr_t	ip_epc_expr;
	/** filter callback for object/key (vos_iterate only) */
	vos_iter_filter_cb_t	ip_filter_cb;
	/** filter callback argument (vos_iterate only) */
	void			*ip_filter_arg;
	/** flags for for iterator */
	uint32_t		ip_flags;
} vos_iter_param_t;

enum {
	/** It is unknown if the extent is covered or visible */
	VOS_VIS_FLAG_UNKNOWN = 0,
	/** The extent is visible at the requested epoch (epr_hi) */
	VOS_VIS_FLAG_VISIBLE = (1 << 0),
	/** The extent is not visible at the requested epoch (epr_hi) */
	VOS_VIS_FLAG_COVERED = (1 << 1),
	/** The extent a remove record (See vos_obj_array_remove) */
	VOS_VIS_FLAG_REMOVE = (1 << 2),
	/** The extent represents only a portion of the in-tree extent */
	VOS_VIS_FLAG_PARTIAL = (1 << 3),
	/** Marks the final entry in sorted iterator */
	VOS_VIS_FLAG_LAST    = (1 << 4),
};

/**
 * Returned entry of a VOS iterator
 */
typedef struct {
	/** Returned epoch. It is ignored for container iteration. */
	daos_epoch_t				ie_epoch;
	union {
		/** Returned entry for container UUID iterator */
		uuid_t				ie_couuid;
		/** Key, object, or DTX entry */
		struct {
			/** Non-zero if punched */
			daos_epoch_t		ie_punch;
			/** If applicable, non-zero if object is punched */
			daos_epoch_t		ie_obj_punch;
			/** Last update timestamp */
			daos_epoch_t		ie_last_update;
			union {
				/** key value */
				daos_key_t	ie_key;
				/** oid */
				daos_unit_oid_t	ie_oid;
			};
		};
		/** Array or SV entry */
		struct {
			/** record size */
			daos_size_t		ie_rsize;
			/** record size for the whole global single record */
			daos_size_t		ie_gsize;
			/** record extent */
			daos_recx_t		ie_recx;
			/* original in-tree extent */
			daos_recx_t		ie_orig_recx;
			/** biov to return address for single value or recx */
			struct bio_iov		ie_biov;
			/** checksum */
			struct dcs_csum_info	ie_csum;
			/** pool map version */
			uint32_t		ie_ver;
			/** Minor epoch of extent */
			uint16_t		ie_minor_epc;
			/** entry dtx state */
			unsigned int		ie_dtx_state;
		};
		/** Active DTX entry. */
		struct {
			/** The DTX identifier. */
			struct dtx_id		ie_dtx_xid;
			/** The OID. */
			daos_unit_oid_t		ie_dtx_oid;
			/** The pool map version when handling DTX on server. */
			uint32_t		ie_dtx_ver;
			/** The DTX entry flags, see dtx_entry_flags. */
			uint16_t		ie_dtx_flags;
			/** DTX mbs flags, see dtx_mbs_flags. */
			uint16_t		ie_dtx_mbs_flags;
			/** DTX tgt count. */
			uint32_t		ie_dtx_tgt_cnt;
			/** DTX modified group count. */
			uint32_t		ie_dtx_grp_cnt;
			/** DTX mbs data size. */
			uint32_t		ie_dtx_mbs_dsize;
			/* The time when create the DTX entry. */
			uint64_t		ie_dtx_start_time;
			/* The hashed dkey if applicable. */
			uint64_t		ie_dkey_hash;
			/** DTX participants information. */
			void			*ie_dtx_mbs;
		};
	};
	/* Flags to describe the entry */
	uint32_t		ie_vis_flags;
	/** Child iterator type */
	vos_iter_type_t		ie_child_type;
} vos_iter_entry_t;

/**
 * Iteration callback function
 */
typedef int (*vos_iter_cb_t)(daos_handle_t ih, vos_iter_entry_t *entry,
			     vos_iter_type_t type, vos_iter_param_t *param,
			     void *cb_arg, unsigned int *acts);

/**
 * Actions performed in iteration callback
 */
enum {
	/** No action */
	VOS_ITER_CB_NONE	= 0,
	/** Delete entry */
	VOS_ITER_CB_DELETE	= (1UL << 0),
	/** Skip entry, don't iterate into next level for current entry */
	VOS_ITER_CB_SKIP	= (1UL << 1),
	/** Abort the current level iterator and restart */
	VOS_ITER_CB_RESTART	= (1UL << 2),
	/** Abort current level iteration */
	VOS_ITER_CB_ABORT	= (1UL << 3),
	/** Yield */
	VOS_ITER_CB_YIELD	= (1UL << 4),
	/** Exit all levels of iterator */
	VOS_ITER_CB_EXIT	= (1UL << 5),
};

/**
 * Anchors for whole iteration, one for each entry type
 */
struct vos_iter_anchors {
	/** Anchor for container */
	daos_anchor_t	ia_co;
	/** Anchor for obj */
	daos_anchor_t	ia_obj;
	/** Anchor for dkey */
	daos_anchor_t	ia_dkey;
	/** Anchor for akey */
	daos_anchor_t	ia_akey;
	/** Anchor for SV tree */
	daos_anchor_t	ia_sv;
	/** Anchor for EV tree */
	daos_anchor_t	ia_ev;
	/** Triggers for re-probe */
	unsigned int    ia_reprobe_co : 1, ia_reprobe_obj : 1, ia_reprobe_dkey : 1,
	    ia_reprobe_akey : 1, ia_reprobe_sv : 1, ia_reprobe_ev : 1;
	unsigned int ia_probe_level;
};

/* Ignores DTX as they are transient records */
enum VOS_TREE_CLASS {
	VOS_TC_CONTAINER,
	VOS_TC_OBJECT,
	VOS_TC_DKEY,
	VOS_TC_AKEY,
	VOS_TC_SV,
	VOS_TC_ARRAY,
	VOS_TC_VEA,
};

#endif /* __VOS_TYPES_H__ */
