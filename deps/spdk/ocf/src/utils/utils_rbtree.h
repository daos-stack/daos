/*
 * Copyright(c) 2020-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __UTILS_RBTREE_H__
#define __UTILS_RBTREE_H__

#include "../ocf_priv.h"

struct ocf_rb_node {
	bool red;
	struct ocf_rb_node *left;
	struct ocf_rb_node *right;
	struct ocf_rb_node *parent;
	struct list_head list;
};

typedef int (*ocf_rb_tree_node_cmp_cb)(struct ocf_rb_node *n1,
		struct ocf_rb_node *n2);

typedef struct ocf_rb_node *(*ocf_rb_tree_list_find_cb)(
		struct list_head *node_list);

struct ocf_rb_tree {
	struct ocf_rb_node *root;
	ocf_rb_tree_node_cmp_cb cmp;
	ocf_rb_tree_list_find_cb find;
};

void ocf_rb_tree_init(struct ocf_rb_tree *tree, ocf_rb_tree_node_cmp_cb cmp,
		ocf_rb_tree_list_find_cb find);

void ocf_rb_tree_insert(struct ocf_rb_tree *tree, struct ocf_rb_node *node);

void ocf_rb_tree_remove(struct ocf_rb_tree *tree, struct ocf_rb_node *node);

bool ocf_rb_tree_can_update(struct ocf_rb_tree *tree,
                struct ocf_rb_node *node, struct ocf_rb_node *new_node);

struct ocf_rb_node *ocf_rb_tree_find(struct ocf_rb_tree *tree,
		struct ocf_rb_node *node);

#endif /* __UTILS_RBTREE_H__ */
