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
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * Layout definition for VOS root object
 * vos/vos_layout.h
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#ifndef _VOS_LAYOUT_H
#define _VOS_LAYOUT_H
#include <libpmemobj.h>
#include <daos/btree.h>
#include <daos_srv/vos_types.h>
#include "vos_chash_table.h"

/**
 * VOS metadata structure declarations
 * opaque structure expanded inside implementation
 * Container table for holding container UUIDs
 * Object table for holding object IDs
 * B-Tree for Key Value stores
 * EV-Tree for Byte array stores
 */
struct vos_container_index;
struct vos_container;
struct vos_object_index;
struct vos_epoch_index;
struct vos_kv_index;
struct vos_ba_index;
struct vos_obj;
struct vos_krec;
struct vos_irec;

/**
 * Typed Layout named using Macros from libpmemobj
 * Each structure is assigned a type number internally
 * in the macro libpmemobj has its own pointer type (PMEMoid)
 * and uses named unions to assign types to such pointers.
 * Type-number help identify the PMEMoid  pointer types
 * with an ID. This layout structure is used to consicely
 * represent the types associated with the vos_pool_layout.
 * In this case consecutive typeIDs are assigned to the
 * different pointers in the pool.
 */
POBJ_LAYOUT_BEGIN(vos_pool_layout);

POBJ_LAYOUT_ROOT(vos_pool_layout, struct vos_pool_root);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_container_index);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_container);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_object_index);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_epoch_index);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_kv_index);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_ba_index);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_obj);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_krec);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_irec);

POBJ_LAYOUT_END(vos_pool_layout);

struct vos_pool_root {
	/* Structs stored in LE or BE representation */
	uint32_t				vpr_magic;
	/* Unique PoolID for each VOS pool assigned on creation */
	uuid_t					vpr_pool_id;
	/* Flags for compatibility features */
	uint64_t				vpr_compat_flags;
	/* Flags for incompatibility features */
	uint64_t				vpr_incompat_flags;
	/* Typed PMEMoid pointer for the container index table */
	TOID(struct vos_container_index)	vpr_ci_table;
	/* Pool info of objects, containers, space availability */
	vos_pool_info_t				vpr_pool_info;
};

struct vos_container_index {
	TOID(struct vos_chash_table) chtable;
	/* More items to be added*/
};

struct vos_epoch_index {
	TOID(struct vos_chash_table) ehtable;
	/* More items to be added*/
};

/* VOS Container Value */
struct vos_container {
	uuid_t				vc_id;
	vos_co_info_t			vc_info;
	TOID(struct vos_object_index)	vc_obtable;
	TOID(struct vos_epoch_index)	vc_ehtable;
};

/**
 * Persisted VOS (d)key record, it is referenced by btr_record::rec_mmid
 * of btree VOS_BTR_KEY.
 */
struct vos_krec {
	struct btr_root			kr_btr;
	/** key checksum type */
	uint8_t				kr_cs_type;
	/** key checksum size (in bytes) */
	uint8_t				kr_cs_size;
	/** padding bytes */
	uint16_t			kr_pad_16;
	/** akey length */
	uint32_t			kr_size;
	/** placeholder for the real stuff */
	char				kr_body[0];
};

/**
 * Persisted VOS index & epoch record, it is referenced by btr_record::rec_mmid
 * of btree VOS_BTR_IDX.
 */
struct vos_irec {
	/** reserved for resolving overwrite race */
	uint64_t			ir_cookie;
	/** key checksum type */
	uint8_t				ir_cs_type;
	/** key checksum size (in bytes) */
	uint8_t				ir_cs_size;
	/** padding bytes */
	uint16_t			ir_pad16;
	/** padding bytes */
	uint32_t			ir_pad32;
	/** length of value */
	uint64_t			ir_size;
	/** placeholder for the real stuff */
	char				ir_body[0];
};

/**
 * VOS object, assume all objects are KV store...
 * NB: PMEM data structure.
 */
struct vos_obj {
	daos_unit_oid_t			vo_oid;
	/** VOS object btree root */
	struct btr_root			vo_tree;
};

#endif
