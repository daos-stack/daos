/**
 * (C) Copyright 2015-2018 Intel Corporation.
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
#include <daos_srv/eio.h>

enum vos_oi_attr {
	/** Marks object as failed */
	VOS_OI_FAILED		= (1U << 0),
	/** TODO: Additional attributes to support metadata storage for SR */
};

/**
 * pool attributes returned to query
 */
typedef struct {
	/** # of containers in this pool */
	uint64_t		pif_cont_nr;
	/** Total space available on SCM */
	daos_size_t		pif_scm_sz;
	/** Total space available on NVMe */
	daos_size_t		pif_blob_sz;
	/** Current available space */
	daos_size_t		pif_avail;
	/** TODO */
} vos_pool_info_t;

/**
 * container attributes returned to query
 */
typedef struct {
	/** number of objects */
	unsigned int		pci_nobjs;
	/** used space */
	daos_size_t		pci_used;
	/** aggregated epoch in this container */
	daos_epoch_t		pci_purged_epoch;
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
} vos_iter_type_t;

/** epoch logic expression for the iterator */
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

/**
 * Parameters for returning anchor
 * from aggregation/discard
 */
typedef struct {
	/** anchor status mask */
	unsigned int		pa_mask;
	/** Anchor for obj */
	daos_anchor_t		pa_obj;
	/** Anchor for dkey */
	daos_anchor_t		pa_dkey;
	/** Anchor for akey */
	daos_anchor_t		pa_akey;
	/** Anchor for recx */
	daos_anchor_t		pa_recx;
	/** Anchor for retained recx (max epoch) */
	daos_anchor_t		pa_recx_max;
	/** Save OID for aggregation optimization */
	daos_unit_oid_t		pa_oid;
} vos_purge_anchor_t;

/**
 * Parameters for initialising VOS iterator
 */
typedef struct {
	/** pool connection handle or container open handle */
	daos_handle_t		ip_hdl;
	/** Optional, object ID for VOS_ITER_DKEY */
	daos_unit_oid_t		ip_oid;
	/** distribution key for VOS_ITER_AKEY */
	daos_key_t		ip_dkey;
	/** attribute key for VOS_ITER_DKEY/RECX */
	daos_key_t		ip_akey;
	/** epoch validity range for the iterator */
	daos_epoch_range_t	ip_epr;
	/** epoch logic expression for the iterator */
	vos_it_epc_expr_t	ip_epc_expr;
} vos_iter_param_t;

/**
 * Returned entry of a VOS iterator
 */
typedef struct {
	/** Returned epoch. It is ignored for container iteration. */
	daos_epoch_t		ie_epoch;
	union {
		/** Returned entry for container UUID iterator */
		uuid_t				ie_couuid;
		/** dkey or akey */
		daos_key_t			ie_key;
		/** oid */
		daos_unit_oid_t			ie_oid;
		struct {
			/** record size */
			daos_size_t		ie_rsize;
			daos_recx_t		ie_recx;
			/** iovec to return data or ZC address */
			daos_iov_t		ie_iov;
			/** biov to return address for single value rec */
			struct bio_iov		ie_biov;
			/** update cookie */
			uuid_t			ie_cookie;
			/** checksum */
			daos_csum_buf_t		ie_csum;
			/** pool map version */
			uint32_t		ie_ver;
		};
	};
} vos_iter_entry_t;

#endif /* __VOS_TYPES_H__ */
