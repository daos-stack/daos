/**
 * (C) Copyright 2016 Intel Corporation.
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
struct vos_obj;
struct vos_cookie_index;
struct vos_cookie_entry;
struct vos_epoch_index;
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
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_obj);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_cookie_index);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_cookie_entry);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_krec);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_irec);
POBJ_LAYOUT_END(vos_pool_layout);


/**
 * VOS container index
 * PMEM container index in each pool
 */
struct vos_container_index {
	struct btr_root		ci_btree;
};

/**
 * VOS cookie index
 * In-memory BTR index to hold
 * all cookies and max epoch updated
 */
struct vos_cookie_index {
	struct btr_root		cookie_btr;
	struct umem_attr	cookie_btr_attr;
};

/**
 *  VOS cookie table
 *  Data structure to store max90
 */
struct vos_cookie_entry {
	daos_epoch_t	vce_max_epoch;
};



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


struct vos_epoch_index {
	struct btr_root		ehtable;
};

/* VOS Container Value */
struct vos_container {
	uuid_t				vc_id;
	vos_co_info_t			vc_info;
	TMMID(struct vos_object_index)	vc_obtable;
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
