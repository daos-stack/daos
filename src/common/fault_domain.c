/**
 * (C) Copyright 2021-2023 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(common)

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
 * A compressed domain tree starts with values that encode metadata about the tree.
 */
#define FD_TREE_MD_LEN          1

/**
 * The compressed tree must contain metadata and at least a root node.
 */
#define FD_TREE_MIN_LEN         (FD_TREE_TUPLE_LEN + FD_TREE_MD_LEN)

/**
 * The fault domain tree level numbering begins at 0 (rank level) and increases
 * as you approach the root. The level above the ranks will always have the same
 * number.
 *
 * This assumes the tree has a uniform depth throughout.
 */
#define NODE_DOMAIN_LEVEL       1

/**
 * The root node always has a static ID number.
 */
#define ROOT_ID                 1

int
d_fd_tree_init(struct d_fd_tree *tree, const uint32_t *compressed, const uint32_t compressed_len)
{
	if (compressed == NULL) {
		D_ERROR("null compressed fd tree\n");
		return -DER_INVAL;
	}

	if (compressed_len < FD_TREE_MIN_LEN) {
		D_ERROR("compressed len=%u, less than minimum %u\n", compressed_len,
			FD_TREE_MIN_LEN);
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

static bool
tree_has_fault_domain(struct d_fd_tree *tree)
{
	return (tree->fdt_compressed[0] & D_FD_TREE_HAS_FAULT_DOMAIN) != 0;
}

static bool
tree_has_perf_domain(struct d_fd_tree *tree)
{
	return (tree->fdt_compressed[0] & D_FD_TREE_HAS_PERF_DOMAIN) != 0;
}

static bool
need_perf_dom(struct d_fd_tree *tree)
{
	return tree_has_perf_domain(tree) && tree->fdt_perf_dom_level < 0;
}

static bool
need_fault_dom(struct d_fd_tree *tree)
{
	return tree_has_fault_domain(tree) && tree->fdt_fault_dom_level < 0;
}

static bool
domain_is_root(struct d_fault_domain *dom)
{
	return dom->fd_id == ROOT_ID;
}

static bool
domain_is_node(struct d_fault_domain *dom)
{
	return dom->fd_level == NODE_DOMAIN_LEVEL;
}

static bool
domain_is_fault(struct d_fd_tree *tree, struct d_fault_domain *dom)
{
	/* Performance domain must be higher in tree than fault domain */
	if (need_perf_dom(tree))
		return false;

	return need_fault_dom(tree) || tree->fdt_fault_dom_level == dom->fd_level;
}

static bool
domain_is_perf(struct d_fd_tree *tree, struct d_fault_domain *dom)
{
	return need_perf_dom(tree) || tree->fdt_perf_dom_level == dom->fd_level;
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
	if (domain_is_root(fd)) {
		next->fdn_type = D_FD_NODE_TYPE_ROOT;
	} else if (domain_is_node(fd)) {
		next->fdn_type = D_FD_NODE_TYPE_NODE;
	} else if (domain_is_fault(tree, fd)) {
		next->fdn_type = D_FD_NODE_TYPE_FAULT_DOM;
		if (need_fault_dom(tree))
			tree->fdt_fault_dom_level = fd->fd_level;
	} else if (domain_is_perf(tree, fd)) {
		next->fdn_type = D_FD_NODE_TYPE_PERF_DOM;
		if (need_perf_dom(tree))
			tree->fdt_perf_dom_level = fd->fd_level;
	} else {
		D_ERROR("fault domain tree has a node of unknown type (level=%d, id=%d, "
			"children=%d)",
			fd->fd_level, fd->fd_id, fd->fd_children_nr);
		return -DER_INVAL;
	}
	next->fdn_val.dom = fd;
	tree->fdt_domains_found++;

	/*
	 * At the node level, we expect the children to be ranks,
	 * not domains.
	 */
	if (domain_is_node(fd))
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

	tree->fdt_idx              = FD_TREE_MD_LEN; /* skip the metadata values */
	tree->fdt_domains_expected = 1; /* at least the root */
	tree->fdt_domains_found = 0;
	tree->fdt_ranks_expected = 0;
	tree->fdt_ranks_found = 0;
	tree->fdt_perf_dom_level   = -1;
	tree->fdt_fault_dom_level  = -1;

	return 0;
}

int
d_fd_get_exp_num_domains(uint32_t compressed_len, uint32_t exp_num_ranks,
			 uint32_t *result)
{
	uint32_t min_len;
	uint32_t domain_len;

	/* Minimal tree must contain at least the root domain */
	min_len = FD_TREE_MD_LEN + FD_TREE_TUPLE_LEN + exp_num_ranks;

	if (compressed_len < min_len) {
		D_ERROR("len = %u, needed minimum = %u)\n", compressed_len,
			min_len);
		return -DER_INVAL;
	}

	domain_len = compressed_len - exp_num_ranks - FD_TREE_MD_LEN;
	if (domain_len % FD_TREE_TUPLE_LEN != 0) {
		D_ERROR("domain_len = %u is not a multiple of %u\n",
			domain_len, FD_TREE_TUPLE_LEN);
		return -DER_INVAL;
	}

	*result = domain_len / FD_TREE_TUPLE_LEN;
	return 0;
}

const char *
d_fd_get_node_type_str(enum d_fd_node_type node_type)
{
	switch (node_type) {
	case D_FD_NODE_TYPE_RANK:
		return "rank";
	case D_FD_NODE_TYPE_NODE:
		return "node";
	case D_FD_NODE_TYPE_FAULT_DOM:
		return "fault domain";
	case D_FD_NODE_TYPE_PERF_DOM:
		return "perf domain";
	case D_FD_NODE_TYPE_ROOT:
		return "root";
	default:
		return "unknown";
	}
}
