/*
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(mem)

#include <stdbool.h>
#include <gurt/common.h>
#include <gurt/rbt.h>


#define RBT_HEAD(rbt) (&((struct rbt_priv *)(rbt))->rbt_head)
#define RBT_LEAF(rbt) (&((struct rbt_priv *)(rbt))->rbt_leaf)
#define RBT_ROOT(rbt) (((struct rbt_priv *)(rbt))->rbt_head.rn_left)
#define RBT_CMP_KEY(rbt, x, y) (((const struct rbt_priv *)(rbt))->rbt_cmp_key((x), (y)))
#define RBT_FREE_NODE(rbt, x) (((const struct rbt_priv *)(rbt))->rbt_free_node((x)))

/** List of RBT node color */
enum { RBT_RED, RBT_BLACK };

/** RBT node color */
typedef char rbt_color_t;

/** Node of an RBT */
struct rbt_node_priv {
	/** Public fields of the RBT node */
	d_rbt_node_t          rn_node_pub;
	/** Color of the node: Red or Black */
	rbt_color_t           rn_color;
	/** Parent of this node */
	struct rbt_node_priv *rn_parent;
	/** Left node with a key lower than this node */
	struct rbt_node_priv *rn_left;
	/** Right node with a key greater than this node */
	struct rbt_node_priv *rn_right;
};

/** RBT structure */
struct rbt_priv {
	/** Comparaison function of RBT nodes keys */
	int (*rbt_cmp_key)(const void *, const void *);
	/** Free the key and the data holding by an RBT node */
	void (*rbt_free_node)(d_rbt_node_t *);

	/** Dummy node use as the head of the RBT */
	struct rbt_node_priv rbt_head;
	/** Dummy node used as a leaf of the RBT */
	struct rbt_node_priv rbt_leaf;
};

static inline struct rbt_node_priv *
node_pub2priv(d_rbt_node_t *node)
{
	D_ASSERT(node != NULL);

	return container_of(node, struct rbt_node_priv, rn_node_pub);
}

static inline d_rbt_node_t *
node_priv2pub(const struct rbt_priv *rbt, struct rbt_node_priv *node)
{
	D_ASSERT(rbt != NULL);
	D_ASSERT(node != NULL);
	D_ASSERT(node != RBT_HEAD(rbt));

	if (node == RBT_LEAF(rbt))
		return NULL;

	return &node->rn_node_pub;
}

static struct rbt_node_priv *
rbt_node_next_priv(const struct rbt_priv *rbt, struct rbt_node_priv *node)
{
	struct rbt_node_priv *next;

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
		next = RBT_LEAF(rbt);
	return next;
}

static struct rbt_node_priv *
rbt_node_prev_priv(const struct rbt_priv *rbt, struct rbt_node_priv *node)
{
	struct rbt_node_priv *prev;

	D_ASSERT(rbt != NULL);
	D_ASSERT(node != NULL);

	prev = node->rn_left;
	if (prev != RBT_LEAF(rbt)) {
		while(prev->rn_right != RBT_LEAF(rbt))
		      prev = prev->rn_right;
		return prev;
	}

	prev = node->rn_parent;
	while (prev != RBT_HEAD(rbt) && node == prev->rn_left) {
		node = prev;
		prev = prev->rn_parent;
	}
	if (prev == RBT_HEAD(rbt))
		prev = RBT_LEAF(rbt);
	return prev;
}

int
d_rbt_create(int (*cmp_key)(const void *, const void *), void (*free_node)(d_rbt_node_t *),
             d_rbt_t **rbt)
{
	struct rbt_priv *rbt_tmp;

	D_ASSERT(rbt != NULL);
	D_ASSERT(cmp_key != NULL);
	D_ASSERT(free_node != NULL);

	D_ALLOC_PTR(rbt_tmp);
	if (rbt_tmp == NULL)
		return -DER_NOMEM;

	rbt_tmp->rbt_cmp_key   = cmp_key;
	rbt_tmp->rbt_free_node = free_node;

	rbt_tmp->rbt_head.rn_node_pub.rn_key  = NULL;
	rbt_tmp->rbt_head.rn_node_pub.rn_data = NULL;
	rbt_tmp->rbt_head.rn_color            = RBT_BLACK;
	rbt_tmp->rbt_head.rn_parent           = &rbt_tmp->rbt_leaf;
	rbt_tmp->rbt_head.rn_left             = &rbt_tmp->rbt_leaf;
	rbt_tmp->rbt_head.rn_right            = &rbt_tmp->rbt_leaf;

	rbt_tmp->rbt_leaf.rn_node_pub.rn_key  = NULL;
	rbt_tmp->rbt_leaf.rn_node_pub.rn_data = NULL;
	rbt_tmp->rbt_leaf.rn_color            = RBT_BLACK;
	rbt_tmp->rbt_leaf.rn_parent           = &rbt_tmp->rbt_leaf;
	rbt_tmp->rbt_leaf.rn_left             = &rbt_tmp->rbt_leaf;
	rbt_tmp->rbt_leaf.rn_right            = &rbt_tmp->rbt_leaf;

	*rbt = (d_rbt_t *)rbt_tmp;

	return -DER_SUCCESS;

}

static void
rbt_destroy_rec(struct rbt_priv *rbt, struct rbt_node_priv *node, bool destroy_record)
{
	D_ASSERT(rbt != NULL);

	if (node == NULL || node == RBT_LEAF(rbt))
		return;

	rbt_destroy_rec(rbt, node->rn_left, destroy_record);
	node->rn_left = NULL;

	rbt_destroy_rec(rbt, node->rn_right, destroy_record);
	node->rn_right = NULL;

	if (destroy_record)
		rbt->rbt_free_node(&node->rn_node_pub);
	node->rn_node_pub.rn_key  = NULL;
	node->rn_node_pub.rn_data = NULL;

	node->rn_parent = NULL;

	D_FREE(node);
}

void
d_rbt_destroy(d_rbt_t *rbt, bool destroy_record)
{
	D_ASSERT(rbt != NULL);

	rbt_destroy_rec((struct rbt_priv *)rbt, RBT_ROOT(rbt), destroy_record);
	D_FREE(rbt);
}

static void
rbt_rotate_left(struct rbt_priv *rbt, struct rbt_node_priv *node)
{
	struct rbt_node_priv *tmp;

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
rbt_rotate_right(struct rbt_priv *rbt, struct rbt_node_priv *node)
{
	struct rbt_node_priv *tmp;

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
rbt_insert_balance(struct rbt_priv *rbt, struct rbt_node_priv *node)
{
	D_ASSERT(rbt != NULL);
	D_ASSERT(node != NULL);
	D_ASSERT(node->rn_parent != NULL);

	do {
		struct rbt_node_priv *parent;
		struct rbt_node_priv *grand_parent;

		parent = node->rn_parent;
		grand_parent = parent->rn_parent;
		if (parent == grand_parent->rn_left) {
			struct rbt_node_priv *uncle;

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
			struct rbt_node_priv *uncle;

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

d_rbt_node_t *
d_rbt_find(const d_rbt_t *rbt, const void *key)
{
	struct rbt_node_priv *node;

	D_ASSERT(rbt != NULL);
	D_ASSERT(key != NULL);

	node = RBT_ROOT(rbt);
	while (node != RBT_LEAF(rbt)) {
		int cmp;

		cmp = RBT_CMP_KEY(rbt, key, node->rn_node_pub.rn_key);
		if (cmp == 0) {
			return &node->rn_node_pub;
		}

		node = (cmp < 0)?node->rn_left:node->rn_right;
	}

	return NULL;
}

static struct rbt_node_priv *
rbt_add(struct rbt_priv *rbt, void *key, void *data, struct rbt_node_priv *parent)
{
	struct rbt_node_priv *node;

	D_ALLOC_PTR(node);
	if (node == NULL)
		return NULL;
	node->rn_node_pub.rn_key    = key;
	node->rn_node_pub.rn_data   = data;
	node->rn_color              = RBT_RED;
	node->rn_parent             = parent;
	node->rn_left               = RBT_LEAF(rbt);
	node->rn_right              = RBT_LEAF(rbt);

	if (parent == RBT_HEAD(rbt) || rbt->rbt_cmp_key(key, parent->rn_node_pub.rn_key) < 0)
		parent->rn_left = node;
	else
		parent->rn_right = node;

	if (parent->rn_color == RBT_RED)
		rbt_insert_balance(rbt, node);

	RBT_ROOT(rbt)->rn_color = RBT_BLACK;

	return node;
}

int
d_rbt_insert(d_rbt_t *rbt, void *key, void *data, bool overwrite)
{
	struct rbt_node_priv *node;
	struct rbt_node_priv *parent;
	int                   rc;

	D_ASSERT(rbt != NULL);
	D_ASSERT(key != NULL);

	parent = RBT_HEAD(rbt);
	node   = RBT_ROOT(rbt);
	while (node != RBT_LEAF(rbt)) {
		int cmp;

		cmp = RBT_CMP_KEY(rbt, key, node->rn_node_pub.rn_key);
		if (cmp == 0) {
			if (!overwrite)
				D_GOTO(out, rc = -DER_EXIST);

			RBT_FREE_NODE(rbt, &node->rn_node_pub);
			node->rn_node_pub.rn_key  = key;
			node->rn_node_pub.rn_data = data;

			D_GOTO(out, rc = -DER_SUCCESS);
		}

		parent = node;
		node = (cmp < 0)?node->rn_left:node->rn_right;
	}

	rc = -DER_SUCCESS;
	if (rbt_add((struct rbt_priv *)rbt, key, data, parent) == NULL)
		rc = -DER_NOMEM;

out:
	return rc;
}

int
d_rbt_find_insert(d_rbt_t *rbt, void *key, void *data, d_rbt_node_t **node)
{
	struct rbt_node_priv *node_tmp;
	struct rbt_node_priv *parent;
	int                   rc;

	D_ASSERT(rbt != NULL);
	D_ASSERT(key != NULL);

	parent   = RBT_HEAD(rbt);
	node_tmp = RBT_ROOT(rbt);
	while (node_tmp != RBT_LEAF(rbt)) {
		int cmp;

		cmp = RBT_CMP_KEY(rbt, key, node_tmp->rn_node_pub.rn_key);
		if (cmp == 0) {
			*node = (d_rbt_node_t *)node_tmp;
			D_GOTO(out, rc = -DER_EXIST);
		}

		parent = node_tmp;
		node_tmp = (cmp < 0)?node_tmp->rn_left:node_tmp->rn_right;
	}

	rc = -DER_SUCCESS;
	node_tmp = rbt_add((struct rbt_priv *)rbt, key, data, parent);
	if (node_tmp != NULL)
		*node = (d_rbt_node_t *)node_tmp;
	else
		rc = -DER_NOMEM;

out:
	return rc;
}

static void
rbt_delete_balance(struct rbt_priv *rbt, struct rbt_node_priv *node)
{
	do {
		if (node == node->rn_parent->rn_left) {
			struct rbt_node_priv *sibling;

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
			struct rbt_node_priv *sibling;

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
d_rbt_delete(d_rbt_t *rbt, const void *key, bool destroy)
{
	struct rbt_node_priv *node;
	struct rbt_node_priv *child;
	d_rbt_node_t          node_tmp;

	D_ASSERT(rbt != NULL);
	D_ASSERT(key != NULL);

	node = RBT_ROOT(rbt);
	while (node != RBT_LEAF(rbt)) {
		int cmp;

		cmp = RBT_CMP_KEY(rbt, key, node->rn_node_pub.rn_key);
		if (cmp == 0)
			break;

		node = (cmp < 0)?node->rn_left:node->rn_right;
	}

	if (node == RBT_LEAF(rbt))
 	       return -DER_NONEXIST;

	node_tmp.rn_key  = node->rn_node_pub.rn_key;
	node_tmp.rn_data = node->rn_node_pub.rn_data;
	if (node->rn_left != RBT_LEAF(rbt) && node->rn_right != RBT_LEAF(rbt)) {
		struct rbt_node_priv *next;

		next = rbt_node_next_priv((struct rbt_priv *)rbt, node);
		D_ASSERT(next != RBT_LEAF(rbt));
		node->rn_node_pub.rn_key  = next->rn_node_pub.rn_key;
		node->rn_node_pub.rn_data = next->rn_node_pub.rn_data;
		node = next;
	}
	child = (node->rn_left == RBT_LEAF(rbt))?node->rn_right:node->rn_left;

	if (node->rn_color == RBT_BLACK) {
		if (child->rn_color == RBT_RED)
			child->rn_color = RBT_BLACK;
		else if (node != RBT_ROOT(rbt))
			rbt_delete_balance((struct rbt_priv *)rbt, node);
	}

	if (child != RBT_LEAF(rbt))
		child->rn_parent = node->rn_parent;

	if (node == node->rn_parent->rn_left)
		node->rn_parent->rn_left = child;
	else
		node->rn_parent->rn_right = child;

	if (destroy)
		RBT_FREE_NODE(rbt, &node_tmp);
	D_FREE(node);

	return -DER_SUCCESS;
}

d_rbt_node_t *
d_rbt_get_first_node(const d_rbt_t *rbt)
{
	struct rbt_node_priv *node;

	D_ASSERT(rbt != NULL);

	node = RBT_ROOT(rbt);
	while (node->rn_left != RBT_LEAF(rbt))
		node = node->rn_left;
	return node_priv2pub((const struct rbt_priv*)rbt, node);
}

d_rbt_node_t *
d_rbt_get_last_node(const d_rbt_t *rbt)
{
	struct rbt_node_priv *node;

	D_ASSERT(rbt != NULL);

	node = RBT_ROOT(rbt);
	while (node->rn_right != RBT_LEAF(rbt))
		node = node->rn_right;
	return node_priv2pub((const struct rbt_priv*)rbt, node);
}

d_rbt_node_t *
d_rbt_node_next(const d_rbt_t *rbt, const d_rbt_node_t *node)
{
	struct rbt_node_priv *node_tmp;

	D_ASSERT(rbt != NULL);
	D_ASSERT(node != NULL);

	node_tmp = node_pub2priv((d_rbt_node_t *)node);
	node_tmp = rbt_node_next_priv((const struct rbt_priv *)rbt, node_tmp);
	return node_priv2pub((const struct rbt_priv*)rbt, node_tmp);
}

d_rbt_node_t *
d_rbt_node_prev(const d_rbt_t *rbt, const d_rbt_node_t *node)
{
	struct rbt_node_priv *node_tmp;

	D_ASSERT(rbt != NULL);
	D_ASSERT(node != NULL);

	node_tmp = node_pub2priv((d_rbt_node_t *)node);
	node_tmp = rbt_node_prev_priv((const struct rbt_priv *)rbt, node_tmp);
	return node_priv2pub((const struct rbt_priv*)rbt, node_tmp);
}

static size_t
rbt_get_depth_min_rec(const struct rbt_priv *rbt,
                      const struct rbt_node_priv *node,
                      size_t node_depth)
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
d_rbt_get_depth_min(const d_rbt_t *rbt)
{
	return rbt_get_depth_min_rec((struct rbt_priv *)rbt, RBT_ROOT(rbt), 0);
}

static size_t
rbt_get_depth_max_rec(const struct rbt_priv *rbt,
                      const struct rbt_node_priv *node,
                      size_t node_depth)
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
d_rbt_get_depth_max(const d_rbt_t *rbt)
{
	return rbt_get_depth_max_rec((struct rbt_priv *)rbt, RBT_ROOT(rbt), 0);
}

static bool
rbt_is_sorted_rec(const struct rbt_priv *rbt,
                   const struct rbt_node_priv *node,
		   void *key_min,
		   void *key_max)
{
	if (node == RBT_LEAF(rbt))
		return true;

	if (key_min != NULL && rbt->rbt_cmp_key(node->rn_node_pub.rn_key, key_min) <= 0)
		return false;

	if (key_max != NULL && rbt->rbt_cmp_key(node->rn_node_pub.rn_key, key_max) >= 0)
		return false;

	return rbt_is_sorted_rec(rbt, node->rn_left, key_min, node->rn_node_pub.rn_key) &&
		rbt_is_sorted_rec(rbt, node->rn_right, node->rn_node_pub.rn_key, key_max);

}

bool
d_rbt_is_sorted(const d_rbt_t *rbt)
{
	return rbt_is_sorted_rec((const struct rbt_priv *)rbt, RBT_ROOT(rbt), NULL, NULL);
}

static size_t
rbt_get_black_height_rec(const struct rbt_priv *rbt, const struct rbt_node_priv *node)
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
d_rbt_get_black_height(const d_rbt_t *rbt)
{
	if (RBT_HEAD(rbt)->rn_color == RBT_RED)
		return 0;
	if (RBT_ROOT(rbt)->rn_color == RBT_RED)
		return 0;
	if (RBT_LEAF(rbt)->rn_color == RBT_RED)
		return 0;

	return rbt_get_black_height_rec((const struct rbt_priv *)rbt, RBT_ROOT(rbt));
}

static void
rbt_print_tree_rec(const struct rbt_priv *rbt, const struct rbt_node_priv *node,
      void (*print_node)(const d_rbt_node_t *), size_t depth, char *orient)
{
	if (node == RBT_LEAF(rbt))
		return;

	rbt_print_tree_rec(rbt, node->rn_right, print_node, depth + 1, "R");
	printf("%*s", 8 * (int)depth, "");
	printf("%s: ", orient);
	print_node(&node->rn_node_pub);
	printf(" (%s)\n", (node->rn_color == RBT_RED)?"r":"b");
	rbt_print_tree_rec(rbt, node->rn_left, print_node, depth + 1, "L");
}

void
d_rbt_print(const d_rbt_t *rbt, void (*print_node)(const d_rbt_node_t *))
{
	printf("# Tree Graph:\n");
	rbt_print_tree_rec((const struct rbt_priv *)rbt, RBT_ROOT(rbt), print_node, 0, "R");
	printf("\n# Tree Stats:\n");
	printf("\t- RBT is sorted: %s\n", d_rbt_is_sorted(rbt)?"true":"false");
	printf("\t- RBT Key range: min=");
	print_node(d_rbt_get_first_node(rbt));
	printf(", max=");
	print_node(d_rbt_get_last_node(rbt));
	printf("\n\t- RBT depth: min=%lu, max=%lu\n",
	       d_rbt_get_depth_min(rbt), d_rbt_get_depth_max(rbt));
	printf("\t- RBT Black Height: %lu\n", d_rbt_get_black_height(rbt));
}
