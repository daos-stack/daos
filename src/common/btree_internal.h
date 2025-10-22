/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_COMMON_BTREE_INTERNAL__
#define __DAOS_COMMON_BTREE_INTERNAL__

#include <daos/checker.h>

/**
 * Tree node types.
 * NB: a node can be both root and leaf.
 */
enum btr_node_type {
	BTR_NODE_LEAF = (1 << 0),
	BTR_NODE_ROOT = (1 << 1),
};

int
dbtree_open_inplace_ex_internal(struct btr_root *root, struct umem_attr *uma, daos_handle_t coh,
				void *priv, struct checker *ck, daos_handle_t *toh);

#endif /** __DAOS_COMMON_BTREE_INTERNAL__ */
