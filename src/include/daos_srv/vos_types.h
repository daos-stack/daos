/**
 * (C) Copyright 2015-2020 Intel Corporation.
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

#ifndef __VOS_TYPES_H__
#define __VOS_TYPES_H__

#include <daos_types.h>
#include <daos_pool.h>
#include <daos_srv/bio.h>
#include <daos_srv/vea.h>
#include <daos/object.h>
#include <daos/dtx.h>
#include <daos/checksum.h>

struct dtx_rsrvd_uint {
	void			*dru_scm;
	d_list_t		dru_nvme;
};

enum dtx_cos_flags {
	DCF_SHARED	= (1 << 0),
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
	/** Internal flag to indicate timestamps are used */
	VOS_USE_TIMESTAMPS	= (1 << 5),
};

D_CASSERT((VOS_USE_TIMESTAMPS & (VOS_GET_MAX | VOS_GET_MIN | VOS_GET_DKEY |
				 VOS_GET_AKEY | VOS_GET_RECX)) == 0);

enum {
	/** The absence of any flags means iterate all unsorted extents */
	VOS_IT_RECX_ALL		= 0,
	/** Include visible extents in sorted iteration */
	VOS_IT_RECX_VISIBLE	= (1 << 0),
	/** Include covered extents in sorted iteration */
	VOS_IT_RECX_COVERED	= (1 << 1),
	/** Include hole extents in sorted iteration
	 * Only applicable if VOS_IT_RECX_VISIBLE is set but
	 * VOS_IT_RECX_COVERED is not set
	 */
	VOS_IT_RECX_SKIP_HOLES	= (1 << 2),
	/** When sorted iteration is enabled, iterate in reverse */
	VOS_IT_RECX_REVERSE	= (1 << 3),
	/** The iterator is for purge operation */
	VOS_IT_FOR_PURGE	= (1 << 4),
	/** The iterator is for rebuild scan */
	VOS_IT_FOR_REBUILD	= (1 << 5),
	/** Iterate only show punched records in interval */
	VOS_IT_PUNCHED		= (1 << 6),
	/** Mask for all flags */
	VOS_IT_MASK		= (1 << 7) - 1,
};

/**
 * Parameters for initializing VOS iterator
 */
typedef struct {
	/** standalone prepare:	pool connection handle or container open handle
	 *  nested prepare:	DAOS_HDL_INVAL
	 */
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
	/** flags for for iterator */
	uint32_t		ip_flags;
} vos_iter_param_t;

enum {
	/** It is unknown if the extent is covered or visible */
	VOS_VIS_FLAG_UNKNOWN = 0,
	/** The extent is not visible at at the requested epoch (epr_hi) */
	VOS_VIS_FLAG_COVERED = (1 << 0),
	/** The extent is not visible at at the requested epoch (epr_hi) */
	VOS_VIS_FLAG_VISIBLE = (1 << 1),
	/** The extent represents only a portion of the in-tree extent */
	VOS_VIS_FLAG_PARTIAL = (1 << 2),
	/** In sorted iterator, marks final entry */
	VOS_VIS_FLAG_LAST    = (1 << 3),
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
			union {
				/** key value */
				daos_key_t	ie_key;
				/** oid */
				daos_unit_oid_t	ie_oid;
			};
		};
		/** Array entry */
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
		};
		/** Active DTX entry. */
		struct {
			/** The DTX identifier. */
			struct dtx_id		ie_dtx_xid;
			/** The OID. */
			daos_unit_oid_t		ie_dtx_oid;
			/** The pool map version when handling DTX on server. */
			uint32_t		ie_dtx_ver;
			/* The dkey hash for DTX iteration. */
			uint16_t		ie_dtx_flags;
			/** DTX tgt count. */
			uint32_t		ie_dtx_tgt_cnt;
			/** DTX modified group count. */
			uint32_t		ie_dtx_grp_cnt;
			/** DTX mbs data size. */
			uint32_t		ie_dtx_mbs_dsize;
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
	VOS_ITER_CB_YIELD	= (1UL << 0),	/* Yield */
	VOS_ITER_CB_DELETE	= (1UL << 1),	/* Delete entry */
	VOS_ITER_CB_SKIP	= (1UL << 2),	/* Skip entry */
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
	unsigned int	ia_reprobe_co:1,
			ia_reprobe_obj:1,
			ia_reprobe_dkey:1,
			ia_reprobe_akey:1,
			ia_reprobe_sv:1,
			ia_reprobe_ev:1;
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
