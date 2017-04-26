/**
 * (C) Copyright 2017 Intel Corporation.
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
 * rdb: Storage Layout
 *
 * Each rdb replica stores its persistent state in a dedicated libpmemobj pool.
 *
 *   Superblock (pmemobj_root):
 *     Attribute tree:
 *       Log tree
 *       Root KVS tree
 */

#ifndef RDB_LAYOUT_H
#define RDB_LAYOUT_H

#include <daos/btree_class.h>

/* For pmemobj_create() and pmemobj_open() */
#define RDB_LAYOUT "rdb_layout"

/* rdb_superblock::dsb_magic */
#define RDB_SB_MAGIC 0x8120da0367913ef9

/* Superblock */
struct rdb_sb {
	uint64_t	dsb_magic;
	uuid_t		dsb_uuid;	/* of database */
	struct btr_root	dsb_attr;	/* attribute tree */
};

/*
 * Attribute tree
 *
 * Flattened together with the Raft attributes to save one tree level. These
 * are defined in rdb_layout.c using RDB_STRING_KEY().
 */
extern daos_iov_t rdb_attr_nreplicas;	/* uint8_t */
extern daos_iov_t rdb_attr_replicas;	/* uint32_t[] */
extern daos_iov_t rdb_attr_term;	/* int */
extern daos_iov_t rdb_attr_vote;	/* int */
extern daos_iov_t rdb_attr_log;		/* btr_root */
extern daos_iov_t rdb_attr_applied;	/* uint64_t */
extern daos_iov_t rdb_attr_root;	/* btr_root */

#endif /* RDB_LAYOUT_H */
