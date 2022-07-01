/*
 * (C) Copyright 2011,2016-2020 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/* GURT heap (bin heap) APIs. */

#ifndef __GURT_HEAP_H__
#define __GURT_HEAP_H__

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <gurt/common.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * \file
 *
 * Binary heap
 *
 * The binary heap is a scalable data structure created using a binary tree. It
 * is capable of maintaining large sets of objects sorted usually by one or
 * more object properties. User is required to register a comparison callback
 * to determine the relevant ordering of any two objects belong to the set.
 *
 * There is no traverse operation, rather the intention is for the object of the
 * lowest priority which will always be at the root of the tree (as this is an
 * implementation of a min-heap) to be removed by users for consumption.
 *
 * Users of the heap should embed a d_binheap_node_t object instance on every
 * object of the set that they wish the binary heap instance to handle, and
 * required to provide a d_binheap_ops::hop_compare() implementation which
 * is used by the heap as the binary predicate during its internal sorting.
 *
 * The implementation provides an optional internal lock supporting, user can
 * select to use its own external lock mechanism as well.
 */

/** @addtogroup GURT
 * @{
 */

/**
 * Binary heap node.
 *
 * Objects of this type are embedded into objects of the ordered set that is to
 * be maintained by a struct d_binheap instance.
 */
struct d_binheap_node {
	/** Index into the binary tree */
	uint32_t chn_idx;
};

#define DBH_SHIFT  (9)
#define DBH_SIZE   (1U << DBH_SHIFT) /* #ptrs per level */
#define DBH_MASK   (DBH_SIZE - 1)
#define DBH_NOB    (DBH_SIZE * sizeof(struct d_binheap_node *))
#define DBH_POISON (0xdeadbeef)

/**
 * Binary heap feature bits.
 */
enum d_bh_feats {
	/**
	 * By default, the binheap is protected by pthread_mutex.
	 */

	/**
	 * The bin heap has no lock, it means the bin heap is protected
	 * by external lock, or only accessed by a single thread.
	 */
	DBH_FT_NOLOCK = (1 << 0),

	/**
	 * It is a read-mostly bin heap, so it is protected by RW lock.
	 */
	DBH_FT_RWLOCK = (1 << 1),
};

struct d_binheap;

/**
 * Binary heap operations.
 */
struct d_binheap_ops {
	/**
	 * Called right before inserting a node into the binary heap.
	 *
	 * Implementing this operation is optional.
	 *
	 * \param[in] h		The heap
	 * \param[in] e		The node
	 *
	 * \return		zero on success, negative value if error
	 */
	int (*hop_enter)(struct d_binheap *h, struct d_binheap_node *e);

	/**
	 * Called right after removing a node from the binary heap.
	 *
	 * Implementing this operation is optional.
	 *
	 * \param[in] h		The heap
	 * \param[in] e		The node
	 *
	 * \return		zero on success, negative value if error
	 */
	int (*hop_exit)(struct d_binheap *h, struct d_binheap_node *e);

	/**
	 * A binary predicate which is called during internal heap sorting, and
	 * used in order to determine the relevant ordering of two heap nodes.
	 *
	 * Implementing this operation is mandatory.
	 *
	 * \param[in] a		The first heap node
	 * \param[in] b		The second heap node
	 *
	 * \return		true if node a < node b,
	 *			false if node a > node b.
	 *
	 * \see d_binheap_bubble() and d_biheap_sink()
	 */
	bool (*hop_compare)(struct d_binheap_node *a, struct d_binheap_node *b);
};

/**
 * Binary heap.
 */
struct d_binheap {
	/** different type of locks based on cbt_feats */
	union {
		pthread_mutex_t  d_bh_mutex;
		pthread_rwlock_t d_bh_rwlock;
	};
	/** feature bits */
	uint32_t                  d_bh_feats;

	/** Triple indirect */
	struct d_binheap_node ****d_bh_nodes3;
	/** double indirect */
	struct d_binheap_node  ***d_bh_nodes2;
	/** single indirect */
	struct d_binheap_node   **d_bh_nodes1;
	/** operations table */
	struct d_binheap_ops     *d_bh_ops;
	/** private data */
	void                     *d_bh_priv;
	/** # elements referenced */
	uint32_t                  d_bh_nodes_cnt;
	/** high water mark */
	uint32_t                  d_bh_hwm;
};

/**
 * Creates and initializes a binary heap instance.
 *
 * \param[in] feats	The heap feats bits
 * \param[in] count	The initial heap capacity in # of nodes
 * \param[in] priv	An optional private argument
 * \param[in] ops	The operations to be used
 * \param[in,out] h	The 2nd level pointer of created binheap
 *
 * \return		zero on success, negative value if error
 */
int
d_binheap_create(uint32_t feats, uint32_t count, void *priv, struct d_binheap_ops *ops,
		 struct d_binheap **h);

/**
 * Creates and initializes a binary heap instance inplace.
 *
 * \param[in] feats	The heap feats bits
 * \param[in] count	The initial heap capacity in # of nodes
 * \param[in] priv	An optional private argument
 * \param[in] ops	The operations to be used
 * \param[in,out] h	The pointer of binheap
 *
 * \return		zero on success, negative value if error
 */
int
d_binheap_create_inplace(uint32_t feats, uint32_t count, void *priv, struct d_binheap_ops *ops,
			 struct d_binheap *h);

/**
 * Releases all resources associated with a binary heap instance.
 *
 * Deallocates memory for all indirection levels and the binary heap object
 * itself.
 *
 * \param[in] h		The binary heap object
 */
void
d_binheap_destroy(struct d_binheap *h);

/**
 * Releases all resources associated with a binary heap instance inplace.
 *
 * Deallocates memory for all indirection levels and clear data in binary heap
 * object as zero.
 *
 * \param[in] h		The binary heap object
 */
void
d_binheap_destroy_inplace(struct d_binheap *h);

/**
 * Obtains a pointer to a heap node, given its index into the binary tree.
 *
 * \param[in] h		The binary heap
 * \param[in] idx	The requested node's index
 *
 * \return		valid-pointer of the requested heap node,
 *			NULL if index is out of bounds
 */
struct d_binheap_node *
d_binheap_find(struct d_binheap *h, uint32_t idx);

/**
 * Sort-inserts a node into the binary heap.
 *
 * \param[in] h		The heap
 * \param[in] e		The node
 *
 * \return		0 if the node inserted successfully
 *			negative value if error
 */
int
d_binheap_insert(struct d_binheap *h, struct d_binheap_node *e);

/**
 * Removes a node from the binary heap.
 *
 * \param[in] h		The heap
 * \param[in] e		The node
 */
void
d_binheap_remove(struct d_binheap *h, struct d_binheap_node *e);

/**
 * Removes the root node from the binary heap.
 *
 * \param[in] h		The heap
 *
 * \return		valid pointer of the removed root node,
 *			or NULL when empty.
 */
struct d_binheap_node *
d_binheap_remove_root(struct d_binheap *h);

/**
 * Queries the size (number of nodes) of the binary heap.
 *
 * \param[in] h		The heap
 *
 * \return		positive value of the size,
 *			or -DER_INVAL for NULL heap.
 */
static inline int
d_binheap_size(struct d_binheap *h)
{
	if (h == NULL) {
		D_ERROR("invalid NULL heap.\n");
		return -DER_INVAL;
	}

	return h->d_bh_nodes_cnt;
}

/**
 * Queries if the binary heap is empty.
 *
 * \param[in] h		The heap
 *
 * \retval		true	heap is empty (or for NULL heap),
 * \retval		false	heap is non-empty.
 */
static inline bool
d_binheap_is_empty(struct d_binheap *h)
{
	if (h == NULL)
		return true;

	return h->d_bh_nodes_cnt == 0;
}

/**
 * Gets back the root node of the binary heap.
 *
 * \param[in] h		The heap
 *
 * \return		valid pointer of the root node, or NULL in error case.
 */
static inline struct d_binheap_node *
d_binheap_root(struct d_binheap *h)
{
	return d_binheap_find(h, 0);
}

#if defined(__cplusplus)
}
#endif

/** @}
 */
#endif /* __GURT_HEAP_H__ */
