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
 * (C) Copyright 2016 Intel Corporation.
 */
/*
 * dsms: Metadata Storage Format
 *
 * This header assembles (hopefully) everything related to the persistent
 * storage format of pool, container, and target metadata used by dsms.
 *
 * On one storage node, all metadata belonging to the same DAOS pool are stored
 * in one libpmemobj pool, called an mpool in the code. In an mpool, the
 * metadata are stored in a number of dbtree-based key-value stores (KVSs) that
 * form one tree structure. The root object of the mpool acts as the
 * superblock, from which one can find the compatibility stuff and the root KVS.
 *
 * Each KVS is of a particular dbtree class. Classes have names like KVS_NV,
 * KVS_UV, etc. They are listed in this header and implemented in
 * dsms_storage.c.
 *
 * With "regular" KVs ignored, the tree of KVSs in an mpool looks like:
 *
 *   Superblock:
 *     * -> Root KVS (KVS_NV):
 *            * -> Pool handle KVS (KVS_UV)
 *            * -> Container index KVS (KVS_UV):
 *                   Container KVS (KVS_NV):
 *                     HCE KVS (KVS_EC)
 *                     LRE KVS (KVS_EC)
 *                     LHE KVS (KVS_EC)
 *                     Snapshot KVS (KVS_EC)
 *                     Container handle KVS (KVS_UV)
 *                   Container KVS (KVS_NV):
 *                     HCE KVS (KVS_EC)
 *                     LRE KVS (KVS_EC)
 *                     LHE KVS (KVS_EC)
 *                     Snapshot KVS (KVS_EC)
 *                     Container handle KVS (KVS_UV)
 *                   ...
 *
 *   ("* ->" means "a value pointing to another KVS".)
 *
 * TODO: Update all "*"s (umem_id_t values pointing to KVSs) to use in-place
 * btr_root objects. See dbtree_create_inplace().
 *
 * The root KVS stores pool, container, and target attributes that do not
 * require a dedicated KVS. The definitions of its attribute names are divided
 * into pool, container, and target sections in this header.
 */

#ifndef __DSM_STORAGE_H__
#define __DSM_STORAGE_H__

#include <stdint.h>
#include <uuid/uuid.h>
#include <daos/daos_mem.h>

/* Bootstrapping **************************************************************/

/* These are for pmemobj_create() and/or pmemobj_open(). */
#define MPOOL_LAYOUT	"dsms_metadata"
#define MPOOL_SIZE	(1 << 26)	/* 64 MB */

/*
 * Superblock (pmemobj_root)
 *
 * Because the pool and target UUIDs are important and constant, they are
 * stored redundantly in the path names, Pool and Target KVSs, and Superblock.
 *
 * sp_root points to the root KVS.
 *
 * TODO: Add compatibility and checksum stuff.
 */
struct superblock {
	uint64_t	s_magic;
	uuid_t		s_pool_uuid;
	uuid_t		s_target_uuid;
	umem_id_t	s_root;
	uint64_t	s_padding;
};

/* superblock::s_magic */
#define SUPERBLOCK_MAGIC	0x8120da0367913ef9

/*
 * KVS dbtree classes
 *
 * TODO: Arbitrary numbers used. What is the official way of assigning these
 * numbers?
 */
#define KVS_NV	222	/* name-value: hash-ordered keys */
#define KVS_UV	223	/* uuid_t-value: unordered keys */
#define KVS_EC	224	/* epoch-count: ordered keys */

/* Pool Metadata **************************************************************/

/*
 * Root KVS (KVS_NV): pool attributes
 *
 * The pool map, which is a tree of domains (internal nodes) and targets (leave
 * nodes), is serialized by a breadth-first traversal. For each node
 * encountered during the traversal, if it is a domain, the node, including the
 * number of its children, shall be appended to the POOL_MAP_DOMAINS array, and
 * if it is a target, the node shall be appended to the POOL_MAP_TARGETS array.
 */
#define POOL_UUID		"pool_uuid"		/* uuid_t */
#define POOL_UID		"pool_uid"		/* uint32_t */
#define POOL_GID		"pool_gid"		/* uint32_t */
#define POOL_MODE		"pool_mode"		/* uint32_t */
#define POOL_MAP_VERSION	"pool_map_version"	/* uint64_t */
#define POOL_MAP_NTARGETS	"pool_map_ntargets"	/* uint32_t */
#define POOL_MAP_NDOMAINS	"pool_map_ndomains"	/* uint32_t */
#define POOL_MAP_TARGETS	"pool_map_targets"	/* pool_map_target[] */
#define POOL_MAP_DOMAINS	"pool_map_domains"	/* pool_map_domain[] */
#define POOL_HANDLES		"pool_handles"		/* umem_id_t (pool */
							/* handle KVS) */

struct pool_map_target {
	uuid_t		mt_uuid;
	uint64_t	mt_version;
	uint64_t	mt_fseq;
	uint16_t	mt_ncpus;
	uint8_t		mt_status;
	uint8_t		mt_padding[5];
};

struct pool_map_domain {
	uint64_t	md_version;
	uint32_t	md_nchildren;
	uint32_t	md_padding;
};

/* Pool handle KVS (KVS_UV) */
struct pool_handle {
	uint64_t	ph_capas;
};

/* Container Metadata *********************************************************/

/* Root KVS (KVS_NV): container attributes */
#define CONTAINERS	"containers"	/* umem_id_t (container index KVS) */

/*
 * Container index KVS (KVS_UV)
 *
 * This maps container UUIDs (uuid_t) to container KVSs (btr_root).
 */

/*
 * Container KVS (KVS_NV)
 *
 * This also stores container attributes of upper layers.
 */
#define CONT_HCES	"container_hces"	/* btr_root (HCE KVS) */
#define CONT_LRES	"container_lres"	/* btr_root (LRE KVS) */
#define CONT_LHES	"container_lhes"	/* btr_root (LHE KVS) */
#define CONT_SNAPSHOTS	"container_snapshots"	/* btr_root (snapshot KVS) */
#define CONT_HANDLES	"container_handles"	/* btr_root (container handle */
						/* KVS) */

/*
 * HCE, LRE, and LHE KVSs (KVS_EC)
 *
 * A key is an epoch number. A value is an epoch_count. These epoch-sorted KVSs
 * enable us to quickly retrieve the minimum and maximum HCEs, LREs, and LHEs.
 */
struct epoch_count {
	uint64_t	ec_epoch;
	uint32_t	ec_count;
	uint32_t	ec_padding;
};

/*
 * Snapshot KVS (KVS_EC)
 *
 * This KVS stores an ordered list of snapshotted epochs. The values are unused
 * and empty.
 */

/* Container handle KVS (KVS_UV) */
struct container_hdl {
	uint64_t	ch_hce;
	uint64_t	ch_lre;
	uint64_t	ch_lhe;
	uint32_t	ch_flags;
	uint32_t	ch_padding;
};

/* container_hdl::ch_flags */
#define CONT_HDL_RO	1
#define CONT_HDL_RW	2

/* Target Metadata ************************************************************/

/* Root KVS: target attributes */
#define TARGET_UUID		"target_uuid"		/* uuid_t */

#endif /* __DSM_STORAGE_H__ */
