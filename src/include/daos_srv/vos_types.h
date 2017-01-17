/**
 * (C) Copyright 2015, 2016 Intel Corporation.
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

/**
 * pool attributes returned to query
 */
typedef struct {
	/** # of containers in this pool */
	uint64_t		pif_cont_nr;
	/** Total space available */
	daos_size_t		pif_size;
	/** Current vailable space */
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
	/** TODO */
} vos_co_info_t;

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
	/** iterate record extents and epoch validities of these extents */
	VOS_ITER_RECX,
} vos_iter_type_t;

/** epoch logic expression for the iterator */
typedef enum {
	VOS_IT_EPC_LE		= 0,
	VOS_IT_EPC_GE,
	VOS_IT_EPC_EQ,
} vos_it_epc_expr_t;

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
	/**
	 * Returned epoch range. It is ignored for container & obj
	 * iteration for the time being.
	 */
	daos_epoch_range_t	ie_epr;
	union {
		/** Returned entry for container UUID iterator */
		uuid_t				ie_couuid;
		/** dkey or akey */
		daos_key_t			ie_key;
		/** oid */
		daos_unit_oid_t			ie_oid;
		struct {
			daos_recx_t		ie_recx;
			/** iovec to return data or ZC address */
			daos_iov_t		ie_iov;
			/** update cookie */
			uuid_t			ie_cookie;
		};
	};
} vos_iter_entry_t;

#endif /* __VOS_TYPES_H__ */
