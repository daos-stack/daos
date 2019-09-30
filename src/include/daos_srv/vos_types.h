/**
 * (C) Copyright 2015-2019 Intel Corporation.
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
#include <daos_srv/bio.h>
#include <daos_srv/vea.h>
#include <daos/object.h>
#include <daos/dtx.h>

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

/**
 * pool attributes returned to query
 */
typedef struct {
	/** # of containers in this pool */
	uint64_t		pif_cont_nr;
	/** Total SCM space in bytes */
	daos_size_t		pif_scm_sz;
	/** Total NVMe space in bytes */
	daos_size_t		pif_nvme_sz;
	/** Current SCM free space in bytes */
	daos_size_t		pif_scm_free;
	/** Current NVMe free space in bytes */
	daos_size_t		pif_nvme_free;
	/** NVMe block allocator attributes */
	struct vea_attr		pif_vea_attr;
	/** NVMe block allocator statistics */
	struct vea_stat		pif_vea_stat;
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
	/** replay punch (underwrite) */
	VOS_OF_REPLAY_PC	= (1 << 0),
};

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
};

/**
 * Parameters for initialising VOS iterator
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
	/** epoch validity range for the iterator (standalone only) */
	daos_epoch_range_t	ip_epr;
	/** epoch logic expression for the iterator. */
	vos_it_epc_expr_t	ip_epc_expr;
	/** flags for for iterator */
	uint32_t		ip_flags;
} vos_iter_param_t;

enum {
	/** It is unknown if the extent is covered or visible */
	VOS_RECX_FLAG_UNKNOWN = 0,
	/** The extent is not visible at at the requested epoch (epr_hi) */
	VOS_RECX_FLAG_COVERED = (1 << 0),
	/** The extent is not visible at at the requested epoch (epr_hi) */
	VOS_RECX_FLAG_VISIBLE = (1 << 1),
	/** The extent represents only a portion of the in-tree extent */
	VOS_RECX_FLAG_PARTIAL = (1 << 2),
	/** In sorted iterator, marks final entry */
	VOS_RECX_FLAG_LAST    = (1 << 3),
};

/**
 * Returned entry of a VOS iterator
 */
typedef struct {
	/** Returned epoch. It is ignored for container iteration. */
	daos_epoch_t				ie_epoch;
	union {
		/** Returned earliest update epoch for a key */
		daos_epoch_t			ie_earliest;
		/** record size */
		daos_size_t			ie_rsize;
	};
	union {
		/** Returned entry for container UUID iterator */
		uuid_t				ie_couuid;
		/** dkey or akey */
		daos_key_t			ie_key;
		struct {
			/** The DTX identifier. */
			struct dtx_id		ie_xid;
			/** oid */
			daos_unit_oid_t		ie_oid;
			/* The DTX dkey hash for DTX iteration. */
			uint64_t		ie_dtx_hash;
			/* The DTX intent for DTX iteration. */
			uint32_t		ie_dtx_intent;
		};
		struct {
			/** record extent */
			daos_recx_t		ie_recx;
			/* original in-tree extent */
			daos_recx_t		ie_orig_recx;
			/** biov to return address for single value or recx */
			struct bio_iov		ie_biov;
			/** checksum */
			daos_csum_buf_t		ie_csum;
			/** pool map version */
			uint32_t		ie_ver;
			/** Flags to describe the extent */
			uint32_t		ie_recx_flags;
		};
	};
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
	unsigned int	ia_reprobe_obj:1,
			ia_reprobe_dkey:1,
			ia_reprobe_akey:1,
			ia_reprobe_sv:1,
			ia_reprobe_ev:1;
};

/* Ignores DTX as they are transient records.   Add VEA overheads later */
enum VOS_TREE_CLASS {
	VOS_TC_CONTAINER,
	VOS_TC_OBJECT,
	VOS_TC_DKEY,
	VOS_TC_AKEY,
	VOS_TC_SV,
	VOS_TC_ARRAY,
};

#endif /* __VOS_TYPES_H__ */
