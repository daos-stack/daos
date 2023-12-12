/*
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(mem)

#include <stdbool.h>
#include <gurt/common.h>
#include <gurt/rbt.h>


#define RBT_HEAD(rbt) (&(rbt)->rbt_head)
#define RBT_LEAF(rbt) (&(rbt)->rbt_leaf)
#define RBT_ROOT(rbt) ((rbt)->rbt_head.rn_left)

enum { RBT_RED, RBT_BLACK };

typedef char rbt_color_t;

struct rbt_node {
	void            *rn_key;
	void            *rn_data;
	rbt_color_t      rn_color;
	struct rbt_node *rn_parent;
	struct rbt_node *rn_left;
	struct rbt_node *rn_right;
};

struct d_rbt {
	int (*rbt_cmp_key)(const void *, const void *);
	void (*rbt_free_key)(void *);
	void (*rbt_free_data)(void *);

	struct rbt_node rbt_head;
	struct rbt_node rbt_leaf;
};

int
d_rbt_create(struct d_rbt **rbt,
	     int (*cmp_key)(const void *, const void *),
	     void (*free_key)(void *),
	     void (*free_data)(void *))
{
	D_ASSERT(rbt != NULL);
	D_ASSERT(cmp_key != NULL);
	D_ASSERT(free_key != NULL);
	D_ASSERT(free_data != NULL);

	D_ALLOC(*rbt, sizeof(struct d_rbt));
	if (*rbt == NULL)
		return -DER_NOMEM;

	(*rbt)->rbt_cmp_key   = cmp_key;
	(*rbt)->rbt_free_key  = free_key;
	(*rbt)->rbt_free_data = free_data;

	(*rbt)->rbt_leaf.rn_key    = NULL;
	(*rbt)->rbt_leaf.rn_data   = NULL;
	(*rbt)->rbt_leaf.rn_color  = RBT_BLACK;
	(*rbt)->rbt_leaf.rn_parent = &(*rbt)->rbt_leaf;
	(*rbt)->rbt_leaf.rn_left   = &(*rbt)->rbt_leaf;
	(*rbt)->rbt_leaf.rn_right  = &(*rbt)->rbt_leaf;

	(*rbt)->rbt_head.rn_key    = NULL;
	(*rbt)->rbt_head.rn_data   = NULL;
	(*rbt)->rbt_head.rn_color  = RBT_BLACK;
	(*rbt)->rbt_head.rn_parent = &(*rbt)->rbt_leaf;
	(*rbt)->rbt_head.rn_left   = &(*rbt)->rbt_leaf;
	(*rbt)->rbt_head.rn_right  = &(*rbt)->rbt_leaf;

	return -DER_SUCCESS;

}

static void
rbt_destroy_rec(struct d_rbt *rbt, struct rbt_node *node, bool destroy_record)
{
	D_ASSERT(rbt != NULL);

	if (node == NULL || node == RBT_LEAF(rbt))
		return;

	rbt_destroy_rec(rbt, node->rn_left, destroy_record);
	node->rn_left = NULL;

	rbt_destroy_rec(rbt, node->rn_right, destroy_record);
	node->rn_right = NULL;

	if (destroy_record)
		rbt->rbt_free_key(node->rn_key);
	node->rn_key = NULL;

	if (destroy_record)
		rbt->rbt_free_data(node->rn_data);
	node->rn_data = NULL;

	node->rn_parent = NULL;

	D_FREE(node);
}

void
d_rbt_destroy(struct d_rbt *rbt, bool destroy_record)
{
	D_ASSERT(rbt != NULL);

	rbt_destroy_rec(rbt, RBT_ROOT(rbt), destroy_record);
	D_FREE(rbt);
}

static struct rbt_node*
rbt_next_node(const struct d_rbt *rbt, struct rbt_node *node)
{
	struct rbt_node *next;

	D_ASSERT(rbt != NULL);
	D_ASSERT(node != NULL);

	next = node->rn_right;
	if (next != RBT_LEAF(rbt)) {
		while(next->rn_left != RBT_LEAF(rbt))
		      next = next->rn_left;
		return next;
	}

	next = node->rn_parent;
	while (node == next->rn_right) {
		D_ASSERT(next != RBT_HEAD(rbt));
		node = next;
		next = next->rn_parent;
	}
	if (next == RBT_HEAD(rbt))
		next = NULL;
	return next;
}

static void
rbt_rotate_left(struct d_rbt *rbt, struct rbt_node *node)
{
	struct rbt_node *tmp;

	D_ASSERT(node != NULL);

	tmp = node->rn_right;

	node->rn_right = tmp->rn_left;
	if (node->rn_right != RBT_LEAF(rbt))
		node->rn_right->rn_parent = node;

	tmp->rn_parent = node->rn_parent;
  	if (node == node->rn_parent->rn_left)
    		node->rn_parent->rn_left = tmp;
  	else
    		node->rn_parent->rn_right = tmp;

  	tmp->rn_left = node;
  	node->rn_parent = tmp;
}

static void
rbt_rotate_right(struct d_rbt *rbt, struct rbt_node *node)
{
	struct rbt_node *tmp;

	D_ASSERT(node != NULL);

	tmp = node->rn_left;

	node->rn_left = tmp->rn_right;
	if (node->rn_left != RBT_LEAF(rbt))
		node->rn_left->rn_parent = node;

	tmp->rn_parent = node->rn_parent;
  	if (node == node->rn_parent->rn_left)
    		node->rn_parent->rn_left = tmp;
  	else
    		node->rn_parent->rn_right = tmp;

  	tmp->rn_right = node;
  	node->rn_parent = tmp;
}

static void
rbt_insert_balance(struct d_rbt *rbt, struct rbt_node *node)
{
	D_ASSERT(rbt != NULL);
	D_ASSERT(node != NULL);
	D_ASSERT(node->rn_parent != NULL);

	do {
		struct rbt_node *parent;
		struct rbt_node *grand_parent;

		parent = node->rn_parent;
		grand_parent = parent->rn_parent;
		if (parent == grand_parent->rn_left) {
			struct rbt_node *uncle;

			uncle = grand_parent->rn_right;
			if (uncle->rn_color == RBT_RED) {
				parent->rn_color       = RBT_BLACK;
				uncle->rn_color        = RBT_BLACK;
				grand_parent->rn_color = RBT_RED;
				node = grand_parent;
				continue;
			}

			if (node == parent->rn_right) {
				node = parent;
				rbt_rotate_left(rbt, node);
				parent       = node->rn_parent;
				grand_parent = parent->rn_parent;
			}

			parent->rn_color       = RBT_BLACK;
			grand_parent->rn_color = RBT_RED;
			rbt_rotate_right(rbt, grand_parent);
		} else {
			struct rbt_node *uncle;

			uncle = grand_parent->rn_left;
			if (uncle->rn_color == RBT_RED) {
				parent->rn_color       = RBT_BLACK;
				uncle->rn_color        = RBT_BLACK;
				grand_parent->rn_color = RBT_RED;
				node = grand_parent;
				continue;
			}

			if (node == parent->rn_left) {
				node = parent;
				rbt_rotate_right(rbt, node);
				parent       = node->rn_parent;
				grand_parent = parent->rn_parent;
			}

			parent->rn_color       = RBT_BLACK;
			grand_parent->rn_color = RBT_RED;
			rbt_rotate_left(rbt, grand_parent);
		}
	} while (node->rn_parent->rn_color == RBT_RED);
}

int
d_rbt_insert(struct d_rbt *rbt, void *key, void *data, bool overwrite)
{
	struct rbt_node *node;
	struct rbt_node *parent;

	D_ASSERT(rbt != NULL);
	D_ASSERT(key != NULL);
	D_ASSERT(data != NULL);

	parent = RBT_HEAD(rbt);
	node   = RBT_ROOT(rbt);
	while (node != RBT_LEAF(rbt)) {
		int cmp;

		cmp = rbt->rbt_cmp_key(key, node->rn_key);
		if (cmp == 0) {
			if (!overwrite)
				return -DER_EXIST;

			rbt->rbt_free_key(node->rn_key);
			node->rn_key = key;
			rbt->rbt_free_data(node->rn_data);
			node->rn_data = data;

			return -DER_SUCCESS;
		}

		parent = node;
		node = (cmp < 0)?node->rn_left:node->rn_right;
	}

	D_ALLOC(node, sizeof(struct rbt_node));
	if (node == NULL)
		return -DER_NOMEM;
	node->rn_key    = key;
	node->rn_data   = data;
	node->rn_color  = RBT_RED;
	node->rn_parent = parent;
	node->rn_left   = RBT_LEAF(rbt);
	node->rn_right  = RBT_LEAF(rbt);

	if (parent == RBT_HEAD(rbt) || rbt->rbt_cmp_key(key, parent->rn_key) < 0)
		parent->rn_left = node;
	else
		parent->rn_right = node;

	if (parent->rn_color == RBT_RED)
		rbt_insert_balance(rbt, node);

	RBT_ROOT(rbt)->rn_color = RBT_BLACK;

	return -DER_SUCCESS;
}

int
d_rbt_find(void **data, const struct d_rbt *rbt, const void *key)
{
	struct rbt_node *node;

	D_ASSERT(rbt != NULL);
	D_ASSERT(key != NULL);
	D_ASSERT(data != NULL);

	node = RBT_ROOT(rbt);
	while (node != RBT_LEAF(rbt)) {
		int cmp;

		cmp = rbt->rbt_cmp_key(key, node->rn_key);
		if (cmp == 0) {
			*data = node->rn_data;
			return -DER_SUCCESS;
		}

		node = (cmp < 0)?node->rn_left:node->rn_right;
	}

	return -DER_NONEXIST;
}

static void
rbt_delete_balance(struct d_rbt *rbt, struct rbt_node *node)
{
	do {
		if (node == node->rn_parent->rn_left) {
			struct rbt_node *sibling;

			sibling = node->rn_parent->rn_right;
			if (sibling->rn_color == RBT_RED) {
				sibling->rn_color         = RBT_BLACK;
				node->rn_parent->rn_color = RBT_RED;
				rbt_rotate_left(rbt, node->rn_parent);
				sibling = node->rn_parent->rn_right;
			}
			D_ASSERT(sibling->rn_color == RBT_BLACK);

			if (sibling->rn_left->rn_color == RBT_BLACK
			    && sibling->rn_right->rn_color == RBT_BLACK) {
				sibling->rn_color = RBT_RED;
				if (node->rn_parent->rn_color == RBT_RED) {
					node->rn_parent->rn_color = RBT_BLACK;
					break;
				} else
					node = node->rn_parent;
			} else {
				if (sibling->rn_right->rn_color == RBT_BLACK) {
					sibling->rn_left->rn_color = RBT_BLACK;
					sibling->rn_color          = RBT_RED;
					rbt_rotate_right(rbt, sibling);
					sibling = node->rn_parent->rn_right;
				}

				sibling->rn_color           = node->rn_parent->rn_color;
				node->rn_parent->rn_color   = RBT_BLACK;
				sibling->rn_right->rn_color = RBT_BLACK;
				rbt_rotate_left(rbt, node->rn_parent);
				break;
			}
		} else {
			struct rbt_node *sibling;

			sibling = node->rn_parent->rn_left;
			if (sibling->rn_color == RBT_RED) {
				sibling->rn_color         = RBT_BLACK;
				node->rn_parent->rn_color = RBT_RED;
				rbt_rotate_right(rbt, node->rn_parent);
				sibling = node->rn_parent->rn_left;
			}
			D_ASSERT(sibling->rn_color == RBT_BLACK);

			if (sibling->rn_left->rn_color == RBT_BLACK
			    && sibling->rn_right->rn_color == RBT_BLACK) {
				sibling->rn_color = RBT_RED;
				if (node->rn_parent->rn_color == RBT_RED) {
					node->rn_parent->rn_color = RBT_BLACK;
					break;
				} else
					node = node->rn_parent;
			} else {
				if (sibling->rn_left->rn_color == RBT_BLACK) {
					sibling->rn_right->rn_color = RBT_BLACK;
					sibling->rn_color           = RBT_RED;
					rbt_rotate_left(rbt, sibling);
					sibling = node->rn_parent->rn_left;
				}

				sibling->rn_color = node->rn_parent->rn_color;
				node->rn_parent->rn_color  = RBT_BLACK;
				sibling->rn_left->rn_color = RBT_BLACK;
				rbt_rotate_right(rbt, node->rn_parent);
				break;
			}
		}
	} while (node != RBT_ROOT(rbt));
}

int
d_rbt_delete(struct d_rbt *rbt, const void *key, bool destroy)
{
	struct rbt_node *node;
	struct rbt_node *child;
	void            *key_tmp;
	void            *data_tmp;

	D_ASSERT(rbt != NULL);
	D_ASSERT(key != NULL);

	node = RBT_ROOT(rbt);
	while (node != RBT_LEAF(rbt)) {
		int cmp;

		cmp = rbt->rbt_cmp_key(key, node->rn_key);
		if (cmp == 0)
			break;

		node = (cmp < 0)?node->rn_left:node->rn_right;
	}

	if (node == RBT_LEAF(rbt))
 	       return -DER_NONEXIST;

	key_tmp = node->rn_key;
	data_tmp = node->rn_data;
	if (node->rn_left != RBT_LEAF(rbt) && node->rn_right != RBT_LEAF(rbt)) {
		struct rbt_node *next;

		next = rbt_next_node(rbt, node);
		D_ASSERT(next != NULL);
		node->rn_key  = next->rn_key;
		node->rn_data = next->rn_data;
		node = next;
	}
	child = (node->rn_left == RBT_LEAF(rbt))?node->rn_right:node->rn_left;

	if (node->rn_color == RBT_BLACK) {
		if (child->rn_color == RBT_RED)
			child->rn_color = RBT_BLACK;
		else if (node != RBT_ROOT(rbt))
			rbt_delete_balance(rbt, node);
	}

	if (child != RBT_LEAF(rbt))
		child->rn_parent = node->rn_parent;

	if (node == node->rn_parent->rn_left)
		node->rn_parent->rn_left = child;
	else
		node->rn_parent->rn_right = child;

	D_FREE(node);

	if (destroy) {
		rbt->rbt_free_key(key_tmp);
		rbt->rbt_free_data(data_tmp);
	}

	return -DER_SUCCESS;
}

static size_t
rbt_get_depth_min_rec(const struct d_rbt *rbt, const struct rbt_node *node, size_t node_depth)
{
	size_t min_left_depth;
	size_t min_right_depth;

	if (node == RBT_LEAF(rbt))
		return node_depth;

	if (node->rn_left != RBT_LEAF(rbt) && node->rn_right == RBT_LEAF(rbt)) {
		return rbt_get_depth_min_rec(rbt, node->rn_left, node_depth + 1);
	}

	if (node->rn_left == RBT_LEAF(rbt) && node->rn_right != RBT_LEAF(rbt)) {
		return rbt_get_depth_min_rec(rbt, node->rn_right, node_depth + 1);
	}

	min_left_depth = rbt_get_depth_min_rec(rbt, node->rn_left, node_depth + 1);
	min_right_depth = rbt_get_depth_min_rec(rbt, node->rn_right, node_depth + 1);
	return min(min_left_depth, min_right_depth);
}

size_t
d_rbt_get_depth_min(const struct d_rbt *rbt)
{
	return rbt_get_depth_min_rec(rbt, RBT_ROOT(rbt), 0);
}

static size_t
rbt_get_depth_max_rec(const struct d_rbt *rbt, const struct rbt_node *node, size_t node_depth)
{
	size_t max_left_depth;
	size_t max_right_depth;

	if (node == RBT_LEAF(rbt)) {
		return node_depth;
	}

	if (node->rn_left != RBT_LEAF(rbt) && node->rn_right == RBT_LEAF(rbt)) {
		return rbt_get_depth_max_rec(rbt, node->rn_left, node_depth + 1);
	}

	if (node->rn_left == RBT_LEAF(rbt) && node->rn_right != RBT_LEAF(rbt)) {
		return rbt_get_depth_max_rec(rbt, node->rn_right, node_depth + 1);
	}

	max_left_depth = rbt_get_depth_max_rec(rbt, node->rn_left, node_depth + 1);
	max_right_depth = rbt_get_depth_max_rec(rbt, node->rn_right, node_depth + 1);
	return max(max_left_depth, max_right_depth);
}

size_t
d_rbt_get_depth_max(const struct d_rbt *rbt)
{
	return rbt_get_depth_max_rec(rbt, RBT_ROOT(rbt), 0);
}

void*
d_rbt_get_key_min(const struct d_rbt *rbt)
{
	struct rbt_node *node;
	void            *key;

	node = RBT_ROOT(rbt);
	while (node->rn_left != RBT_LEAF(rbt))
		node = node->rn_left;
	key = node->rn_key;

	return key;
}


void*
d_rbt_get_key_max(const struct d_rbt *rbt)
{
	struct rbt_node *node;
	void            *key;

	node = RBT_ROOT(rbt);
	while (node->rn_right != RBT_LEAF(rbt))
		node = node->rn_right;
	key = node->rn_key;

	return key;
}

static bool
rbt_is_ordered_rec(const struct d_rbt *rbt, const struct rbt_node *node,
		   void *key_min, void *key_max)
{
	if (node == RBT_LEAF(rbt))
		return true;

	if (key_min != NULL && rbt->rbt_cmp_key(node->rn_key, key_min) <= 0)
		return false;

	if (key_max != NULL && rbt->rbt_cmp_key(node->rn_key, key_max) >= 0)
		return false;

	return rbt_is_ordered_rec(rbt, node->rn_left, key_min, node->rn_key) &&
		rbt_is_ordered_rec(rbt, node->rn_right, node->rn_key, key_max);

}

bool
d_rbt_is_ordered(const struct d_rbt *rbt)
{
	return rbt_is_ordered_rec(rbt, RBT_ROOT(rbt), NULL, NULL);
}

static size_t
rbt_get_black_height_rec(const struct d_rbt *rbt, const struct rbt_node *node)
{
	size_t black_height_left;
	size_t black_height_right;

	if (node == RBT_LEAF(rbt))
		return 1;

	if (node->rn_color == RBT_RED) {
	    if (node->rn_left->rn_color == RBT_RED)
		return 0;
	    if (node->rn_right->rn_color == RBT_RED)
		return 0;
	    if (node->rn_parent->rn_color == RBT_RED)
		return 0;
	}

	black_height_left = rbt_get_black_height_rec(rbt, node->rn_left);
	if (black_height_left == 0)
		return 0;

	black_height_right = rbt_get_black_height_rec(rbt, node->rn_right);
	if (black_height_right == 0)
		return 0;

	if (black_height_left != black_height_right)
		return 0;

	return black_height_left + ((node->rn_color == RBT_BLACK)?1:0);
}

size_t
d_rbt_get_black_height(const struct d_rbt *rbt)
{
	if (RBT_HEAD(rbt)->rn_color == RBT_RED)
		return 0;
	if (RBT_ROOT(rbt)->rn_color == RBT_RED)
		return 0;
	if (RBT_LEAF(rbt)->rn_color == RBT_RED)
		return 0;

	return rbt_get_black_height_rec(rbt, RBT_ROOT(rbt));
}

static void
rbt_print_tree_rec(const struct d_rbt *rbt, const struct rbt_node *node,
      void (*print_record)(const void *, const void *), size_t depth, char *orient)
{
	if (node == RBT_LEAF(rbt))
		return;

	rbt_print_tree_rec(rbt, node->rn_right, print_record, depth + 1, "R");
	printf("%*s", 8 * (int)depth, "");
	printf("%s: ", orient);
	print_record(node->rn_key, node->rn_data);
	printf(" (%s)\n", (node->rn_color == RBT_RED)?"r":"b");
	rbt_print_tree_rec(rbt, node->rn_left, print_record, depth + 1, "L");
}

void
d_rbt_print(const struct d_rbt *rbt, void (*print_record)(const void *, const void *))
{
	printf("# Tree Graph:\n");
	rbt_print_tree_rec(rbt, RBT_ROOT(rbt), print_record, 0, "R");
	printf("\n# Tree Stats:\n");
	printf("\t- RBT is ordered: %s\n", d_rbt_is_ordered(rbt)?"true":"false");
	printf("\t- RBT Key range: min=");
	print_record(d_rbt_get_key_min(rbt), NULL);
	printf(", max=");
	print_record(d_rbt_get_key_max(rbt), NULL);
	printf("\n\t- RBT depth: min=%lu, max=%lu\n",
	       d_rbt_get_depth_min(rbt), d_rbt_get_depth_max(rbt));
	printf("\t- RBT Black Height: %lu\n", d_rbt_get_black_height(rbt));
}
