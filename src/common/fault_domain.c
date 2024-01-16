/**
 * (C) Copyright 2021-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "fault_domain.h"

/**
 * When domains are passed as a compressed array of integers, the data
 * should be interpreted as a set of tuples in the form:
 *   (level number, ID, number of children)
 * The bottom layer representing ranks is compressed further, in the form:
 *   (rank)
 */
#define FD_TREE_TUPLE_LEN	3

/**
 * The fault domain tree level numbering begins at 0 (rank level) and increases
 * as you approach the root. The level above the ranks will always have the same
 * number.
 *
 * This assumes the tree has a uniform depth throughout.
 */
#define LAST_DOMAIN_LEVEL	1

int
d_fd_tree_init(struct d_fd_tree *tree, const uint32_t *compressed,
	       const uint32_t compressed_len)
{
	if (compressed == NULL) {
		D_ERROR("null compressed fd tree\n");
		return -DER_INVAL;
	}

	if (compressed_len < FD_TREE_TUPLE_LEN) {
		D_ERROR("compressed len=%u, less than minimum %u\n",
			compressed_len, FD_TREE_TUPLE_LEN);
		return -DER_INVAL;
	}

	if (tree == NULL) {
		D_ERROR("null pointer for result\n");
		return -DER_INVAL;
	}

	tree->fdt_compressed = compressed;
	tree->fdt_len = compressed_len;

	return d_fd_tree_reset(tree);
}

static int
get_next_domain(struct d_fd_tree *tree, struct d_fd_node *next)
{
	struct d_fault_domain *fd;

	if ((tree->fdt_idx + FD_TREE_TUPLE_LEN) > tree->fdt_len) {
		D_ERROR("fault domain tree is truncated\n");
		return -DER_TRUNC;
	}

	fd = (struct d_fault_domain *)&tree->fdt_compressed[tree->fdt_idx];
	next->fdn_type = D_FD_NODE_TYPE_DOMAIN;
	next->fdn_val.dom = fd;

	tree->fdt_domains_found++;

	/*
	 * At the final level, we expect the children to be ranks,
	 * not domains.
	 */
	if (fd->fd_level == LAST_DOMAIN_LEVEL)
		tree->fdt_ranks_expected += fd->fd_children_nr;
	else
		tree->fdt_domains_expected += fd->fd_children_nr;

	tree->fdt_idx += FD_TREE_TUPLE_LEN;

	return 0;
}

static void
get_next_rank(struct d_fd_tree *tree, struct d_fd_node *next)
{
	uint32_t cur_idx;

	cur_idx = tree->fdt_idx;
	next->fdn_type = D_FD_NODE_TYPE_RANK;
	next->fdn_val.rank = tree->fdt_compressed[cur_idx];

	tree->fdt_ranks_found++;

	tree->fdt_idx++;
}

static bool
tree_not_initialized(struct d_fd_tree *tree)
{
	return (tree->fdt_compressed == NULL || tree->fdt_len == 0);
}

int
d_fd_tree_next(struct d_fd_tree *tree, struct d_fd_node *next)
{
	bool		domains_done;
	bool		ranks_done;

	if (tree == NULL || next == NULL) {
		D_ERROR("incoming ptr is null\n");
		return -DER_INVAL;
	}

	if (tree_not_initialized(tree)) {
		D_ERROR("fault domain tree not initialized\n");
		return -DER_UNINIT;
	}

	domains_done = (tree->fdt_domains_found >= tree->fdt_domains_expected);
	ranks_done = (tree->fdt_ranks_found >= tree->fdt_ranks_expected);

	if (domains_done && ranks_done)
		return -DER_NONEXIST;

	if (tree->fdt_idx >= tree->fdt_len) {
		D_ERROR("fault domain tree is truncated\n");
		return -DER_TRUNC;
	}

	if (!domains_done)
		return get_next_domain(tree, next);

	get_next_rank(tree, next);

	return 0;
}

int
d_fd_tree_reset(struct d_fd_tree *tree)
{
	if (tree == NULL) {
		D_ERROR("fault domain tree is null\n");
		return -DER_INVAL;
	}

	if (tree_not_initialized(tree)) {
		D_ERROR("fault domain tree is not initialized\n");
		return -DER_UNINIT;
	}

	tree->fdt_idx = 0;
	tree->fdt_domains_expected = 1; /* at least the root */
	tree->fdt_domains_found = 0;
	tree->fdt_ranks_expected = 0;
	tree->fdt_ranks_found = 0;

	return 0;
}

int
d_fd_get_exp_num_domains(uint32_t compressed_len, uint32_t exp_num_ranks,
			 uint32_t *result)
{
	uint32_t min_len;
	uint32_t domain_len;

	/* Minimal tree must contain at least the root domain */
	min_len = FD_TREE_TUPLE_LEN + exp_num_ranks;

	if (compressed_len < min_len) {
		D_ERROR("len = %u, needed minimum = %u)\n", compressed_len,
			min_len);
		return -DER_INVAL;
	}

	domain_len = compressed_len - exp_num_ranks;
	if (domain_len % FD_TREE_TUPLE_LEN != 0) {
		D_ERROR("domain_len = %u is not a multiple of %u\n",
			domain_len, FD_TREE_TUPLE_LEN);
		return -DER_INVAL;
	}

	*result = domain_len / FD_TREE_TUPLE_LEN;
	return 0;
}

bool
d_fd_node_is_group(struct d_fd_node *node)
{
	if (node == NULL)
		return false;

	if (node->fdn_type == D_FD_NODE_TYPE_DOMAIN &&
	    node->fdn_val.dom->fd_level == D_FD_GROUP_DOMAIN_LEVEL)
		return true;

	return false;
}
