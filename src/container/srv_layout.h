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
 * ds_cont: Container Server Storage Layout
 *
 * This header assembles (hopefully) everything related to the persistent
 * storage layout of container metadata used by ds_cont.
 *
 * See src/include/daos_srv/pool.h for the overall mpool storage layout. In the
 * "ds_cont root tree", simply called the "root tree" in ds_cont, we have this
 * layout:
 *
 *   Root tree (NV):
 *     Container index tree (UV):
 *       Container tree (NV):
 *         HCE tree (EC)
 *         LRE tree (EC)
 *         LHE tree (EC)
 *         Snapshot tree (EC)
 *         Container handle tree (UV)
 *       ... (more container trees)
 */

#ifndef __CONTAINER_SERVER_LAYOUT_H__
#define __CONTAINER_SERVER_LAYOUT_H__

#include <stdint.h>

/* Root tree (DBTREE_CLASS_NV): container attributes */
#define CONTAINERS	"containers"	/* btr_root (container index tree) */

/*
 * Container index tree (DBTREE_CLASS_UV)
 *
 * This maps container UUIDs (uuid_t) to container trees (btr_root).
 */

/*
 * Container tree (DBTREE_CLASS_NV)
 *
 * This also stores container attributes of upper layers.
 */
#define CONT_GHCE	"ghce"		/* uint64_t */
#define CONT_HCES	"hces"		/* btr_root (HCE tree) */
#define CONT_LRES	"lres"		/* btr_root (LRE tree) */
#define CONT_LHES	"lhes"		/* btr_root (LHE tree) */
#define CONT_SNAPSHOTS	"snapshots"	/* btr_root (snapshot tree) */
#define CONT_HANDLES	"handles"	/* btr_root (container handle */
					/* tree) */

/*
 * HCE, LRE, and LHE trees (DBTREE_CLASS_EC)
 *
 * A key is an epoch number. A value is an epoch_count. These epoch-sorted
 * trees enable us to quickly retrieve the minimum and maximum HCEs, LREs, and
 * LHEs.
 */

/*
 * Snapshot tree (DBTREE_CLASS_EC)
 *
 * This tree stores an ordered list of snapshotted epochs. The values are
 * unused and empty.
 */

/* Container handle tree (DBTREE_CLASS_UV) */
struct container_hdl {
	uint64_t	ch_hce;
	uint64_t	ch_lre;
	uint64_t	ch_lhe;
	uint64_t	ch_capas;
};

/* container_hdl::ch_flags */
#define CONT_HDL_RO	1
#define CONT_HDL_RW	2

#endif /* __CONTAINER_SERVER_LAYOUT_H__ */
