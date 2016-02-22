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
 * vos/include/vos_layout.h
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */
#ifndef _VOS_LAYOUT_H
#define _VOS_LAYOUT_H

#include <libpmemobj.h>
#include <daos_srv/vos_types.h>

/**
 * VOS metadata structure declarations
 * opaque structure expanded inside implementation
 * Container table for holding container UUIDs
 * Object table for holding object IDs
 * B-Tree for Key Value stores
 * EV-Tree for Byte array stores
 */
struct vos_container_table;
struct vos_object_table;
struct vos_kv_index;
struct vos_ba_index;

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
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_container_table);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_object_table);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_kv_index);
POBJ_LAYOUT_TOID(vos_pool_layout, struct vos_ba_index);
POBJ_LAYOUT_END(vos_pool_layout);

struct vos_pool_root {
	/* Structs stored in LE or BE representation */
	uint32_t	vpr_magic;
	/* Unique PoolID for each VOS pool assigned on creation */
	uuid_t		vos_pool_id;
	/* Flags for compatibility features */
	uint64_t	vpr_compat_flags;
	/* Flags for incompatibility features */
	uint64_t	vpr_incompat_flags;
	/* Typed PMEMoid pointer for the container index table */
	TOID(struct vos_container_table)	container_index_table;
	/*Pool info of objects, containers, space availability */
	vos_pool_info_t	vos_pool_info;
};

/**
 * VOS pool handle
 */
struct vos_pool {
	PMEMobjpool	*ph;
	char		*path;
};

/**
 * Convenience typedefs
 */
typedef struct vos_pool_root vos_pool_root_t;
typedef struct vos_pool vos_pool_t;

#endif

