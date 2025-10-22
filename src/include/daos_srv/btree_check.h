/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_BTREE_CHECK__
#define __DAOS_BTREE_CHECK__

#include <daos_types.h>
#include <daos/btree.h>
#include <daos/checker.h>
#include <daos/mem.h>

/**
 * XXX
 */
int
dbtree_open_inplace_ck(struct btr_root *root, struct umem_attr *uma, daos_handle_t coh, void *priv,
		       struct checker *ck, daos_handle_t *toh);

/**
 * Validate the integrity of the btree node.
 *
 * \param[in] nd	Node to check.
 * \param[in] nd_off	Node's offset.
 * \param[in] dp	DLCK print utility.
 *
 * \retval DER_SUCCESS	The node is correct.
 * \retval -DER_NOTYPE	The node is malformed.
 */
int
btr_node_check(struct btr_node *nd, umem_off_t nd_off, struct checker *ck);

#endif /** __DAOS_BTREE_CHECK__ */
