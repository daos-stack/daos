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
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2015, 2016 Intel Corporation.
 */

#ifndef __VOS_TYPES_H__
#define __VOS_TYPES_H__

#include <daos/daos_types.h>

/**
 * pool attributes returned to query
 */
typedef struct {
	/** # of containers in this pool */
	unsigned int		pif_ncos;
	/** # of objects in this pool */
	unsigned int		pif_nobjs;
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
	/** iterate extents and epoch validities of these extents */
	VOS_ITER_BA,
	/** iterate KVs and their epoch validities */
	VOS_ITER_KV,
} vos_iter_type_t;

/**
 * Parameters for initialising VOS iterator
 */
typedef struct {
	/** type of iterator */
	vos_iter_type_t		ic_type;
	/** pool connection handle or container open handle */
	daos_handle_t		ic_hdl;
	/**
	 * Optional, object ID for iterator, it is required only for
	 * VOS_ITER_BA and VOS_ITER_KV
	 */
	daos_obj_id_t		ic_oid;
	/** epoch validity range for the iterator */
	daos_epoch_range_t	ic_epr;
} vos_iter_cond_t;

/**
 * Position anchor of a VOS iterator
 */
typedef struct {
	/** type of iterator */
	vos_iter_type_t        ip_type;
	union {
		/** container UUID enumeration */
		struct vos_ip_uuid{
			uuid_t			uuid;
		} ip_couuid;
		/** object ID enumeration */
		struct vos_ip_obj {
			daos_obj_id_t		oid;
		} ip_obj;
		/** KV enumeration */
		struct vos_ip_kv {
			daos_hash_out_t		hout;
		} ip_kv;
		/** key range enumeration */
		struct vos_ip_ba {
			daos_rec_index_t	ridx;
			daos_epoch_range_t	per;
		} ip_ba;
	} u;
} vos_iter_pos_t;

/**
 * Returned entry of a VOS iterator
 */
typedef struct {
	/** type of iterator */
	vos_iter_type_t		ie_type;
	/**
	 * Returned epoch range. It is ignored for container iteration for the
	 * time being.
	 */
	daos_epoch_range_t	ie_epr;
	union {
		/** Returned entry for container UUID iterator */
		struct vos_ie_uuid {
			uuid_t			uuid;
		} ie_couuid;
		/** Returned entry for object ID iterator */
		struct vos_ie_obj {
			daos_obj_id_t		oid;
			vos_obj_md_t		osmd;
		} ie_obj;
		/** Returned entry for KV iterator */
		struct vos_ie_kv {
			daos_key_t		key;
			daos_iov_t		val;
		} ie_kv;
		/** Returned entry for key range iterator */
		struct vos_ie_kr {
			daos_rec_index_t	ridx;
		} ie_ba;
	} u;
} vos_iter_entry_t;

#endif /* __VOS_TYPES_H__ */
