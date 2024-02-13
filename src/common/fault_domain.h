/**
 * (C) Copyright 2021-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_COMMON_FAULT_DOMAIN__
#define __DAOS_COMMON_FAULT_DOMAIN__

#include <daos/common.h>

/**
 * The group domain level indicates which level (if any) represents the "group"
 * domains.
 *
 * TODO DAOS-6353: Remove this when we have an arbitrary number of levels. At that
 * point the concept of "groups" goes away.
 */
#define D_FD_GROUP_DOMAIN_LEVEL	2

/**
 * Possible types of fault domain tree node.
 */
enum d_fd_node_type {
	D_FD_NODE_TYPE_UNKNOWN = 0,
	D_FD_NODE_TYPE_DOMAIN,
	D_FD_NODE_TYPE_RANK,
};

/**
 * Unified structure representing a node in the fault domain tree.
 *
 */
struct d_fd_node {
	enum d_fd_node_type fdn_type; /* indicates which union member to use */
	union {
		const struct d_fault_domain	*dom;
		uint32_t			rank;
	} fdn_val;
};

/**
 * Represents a fault domain at any level above rank.
 */
struct d_fault_domain {
	uint32_t fd_level;	/** level in the fault domain tree >= 1 */
	uint32_t fd_id;		/** unique ID */
	uint32_t fd_children_nr; /** number of children */
};

/**
 * DAOS fault domain tree. This is a wrapper for the compressed integer array
 * format passed down from the control plane.
 * See src/proto/mgmt/pool.proto for details.
 */
struct d_fd_tree {
	/**
	 * This structure should be considered opaque by callers. Access its
	 * contents through the d_fd_tree_* functions.
	 */

	const uint32_t	*fdt_compressed; /** compressed domain array */
	int32_t		fdt_len; /** length of compressed array */

	/** Tree traversal state */
	uint32_t fdt_idx; /** index of the next item in the tree */
	uint32_t fdt_domains_expected; /** counted up to this point */
	uint32_t fdt_domains_found; /** actually traversed */
	uint32_t fdt_ranks_expected; /** counted up to this point */
	uint32_t fdt_ranks_found; /** actually traversed */
};

/**
 * Initializes the fault domain tree structure with the compressed version of
 * the tree.
 *
 * Does not allocate any new memory. Internals are pointers into the original
 * compressed array.
 *
 * \param[in]	compressed	tree in compressed integer format
 * \param[in]	compressed_len	length of compressed array
 * \param[out]	tree		pointer to a new daos_fd_tree structure
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid parameters
 *		-DER_NOMEM	Out of memory
 */
int
d_fd_tree_init(struct d_fd_tree *tree, const uint32_t *compressed,
	       const uint32_t compressed_len);

/**
 * Get the next domain in the breadth-first traversal of the fault domain tree.
 *
 * Does not allocate any new memory. The content of the node is a pointer into
 * the tree's internal representation.
 *
 * \param[in]	tree	Tree to traverse
 * \param[out]	next	Next node in traversal
 *
 * \return	0		Success - next is populated
 *		-DER_INVAL	Invalid inputs
 *		-DER_UNINIT	Tree isn't initialized
 *		-DER_NONEXIST	Traversal is complete
 */
int
d_fd_tree_next(struct d_fd_tree *tree, struct d_fd_node *next);

/**
 * Reset the tree so the next traversal starts at the beginning.
 *
 * \param[in]	tree	Tree to reset
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		-DER_UNINIT	Tree is not initialized
 */
int
d_fd_tree_reset(struct d_fd_tree *tree);

/**
 * Estimate the number of domains in a fault domain tree array based on the
 * length of the array and the expected number of ranks in it.
 *
 * \param[in]  compressed_len	Length of the compressed fault domain array
 * \param[in]  exp_num_ranks	Number of ranks expected to be in the fault
 *				domain tree based on the array
 * \param[out] result		Number of domains calculated
 *
 * \return		0		Success
 *			-DER_INVAL	Invalid input
 */
int
d_fd_get_exp_num_domains(uint32_t compressed_len, uint32_t exp_num_ranks,
			 uint32_t *result);

/**
 * Determine whether the domain tree node is a group component.
 *
 * TODO DAOS-6353: Remove this when we have an arbitrary number of levels. At that
 * point the concept of "groups" goes away.
 *
 * @param[in]	node	Node of a fault domain tree
 *
 * @return	true	if the node is at the right level to be a group component
 *		false	otherwise
 */
bool
d_fd_node_is_group(struct d_fd_node *node);

#endif /* __DAOS_COMMON_FAULT_DOMAIN__ */
