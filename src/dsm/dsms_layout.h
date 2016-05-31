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
 * dsms: Metadata Storage Layout
 *
 * This header assembles (hopefully) everything related to the persistent
 * storage layout of pool, container, and target metadata used by dsms.
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
 *     Root KVS (KVS_NV):
 *       Pool handle KVS (KVS_UV)
 *       Container index KVS (KVS_UV):
 *         Container KVS (KVS_NV):
 *           HCE KVS (KVS_EC)
 *           LRE KVS (KVS_EC)
 *           LHE KVS (KVS_EC)
 *           Snapshot KVS (KVS_EC)
 *           Container handle KVS (KVS_UV)
 *         Container KVS (KVS_NV):
 *           HCE KVS (KVS_EC)
 *           LRE KVS (KVS_EC)
 *           LHE KVS (KVS_EC)
 *           Snapshot KVS (KVS_EC)
 *           Container handle KVS (KVS_UV)
 *           ...
 *
 * The root KVS stores pool, container, and target attributes that do not
 * require a dedicated KVS. The definitions of its attribute names are divided
 * into pool, container, and target sections in this header.
 */

#ifndef __DSM_STORAGE_H__
#define __DSM_STORAGE_H__

#include <stdint.h>
#include <uuid/uuid.h>
#include <daos/btree.h>
#include <daos/mem.h>

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
	struct btr_root	s_root;
};

/* superblock::s_magic */
#define SUPERBLOCK_MAGIC	0x8120da0367913ef9

/*
 * KVS dbtree classes
 */
#define KVS_NV	(DBTREE_DSM_BEGIN + 0)	/* name-value: hash-ordered keys */
#define KVS_UV	(DBTREE_DSM_BEGIN + 1)	/* uuid_t-value: unordered keys */
#define KVS_EC	(DBTREE_DSM_BEGIN + 2)	/* epoch-count: ordered keys */

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
#define POOL_HANDLES		"pool_handles"		/* btr_root (pool */
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
struct pool_hdl {
	uint64_t	ph_capas;
};

/* Container Metadata *********************************************************/

/* Root KVS (KVS_NV): container attributes */
#define CONTAINERS	"containers"	/* btr_root (container index KVS) */

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
#define CONT_GHCE	"ghce"		/* uint64_t */
#define CONT_HCES	"hces"		/* btr_root (HCE KVS) */
#define CONT_LRES	"lres"		/* btr_root (LRE KVS) */
#define CONT_LHES	"lhes"		/* btr_root (LHE KVS) */
#define CONT_SNAPSHOTS	"snapshots"	/* btr_root (snapshot KVS) */
#define CONT_HANDLES	"handles"	/* btr_root (container handle */
						/* KVS) */

/*
 * HCE, LRE, and LHE KVSs (KVS_EC)
 *
 * A key is an epoch number. A value is an epoch_count. These epoch-sorted KVSs
 * enable us to quickly retrieve the minimum and maximum HCEs, LREs, and LHEs.
 */

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
	uint64_t	ch_capas;
};

/* container_hdl::ch_flags */
#define CONT_HDL_RO	1
#define CONT_HDL_RW	2

/* Target Metadata ************************************************************/

/* Root KVS: target attributes */
#define TARGET_UUID		"target_uuid"		/* uuid_t */

#endif /* __DSM_STORAGE_H__ */
