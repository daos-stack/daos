/* Copyright (C) 2016 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* CaRT (Collective and RPC Transport) heap (bin heap) APIs. */

#ifndef __CRT_HEAP_H__
#define __CRT_HEAP_H__

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <crt_types.h>
#include <crt_errno.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
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
 * Users of the heap should embed a crt_binheap_node_t object instance on every
 * object of the set that they wish the binary heap instance to handle, and
 * required to provide a crt_binheap_ops::hop_compare() implementation which
 * is used by the heap as the binary predicate during its internal sorting.
 *
 * The implementation provides an optional internal lock supporting, user can
 * select to use its own external lock mechanism as well.
 */

/**
 * Binary heap node.
 *
 * Objects of this type are embedded into objects of the ordered set that is to
 * be maintained by a struct crt_binheap instance.
 */

struct crt_binheap_node {
	/** Index into the binary tree */
	uint32_t	chn_idx;
};

#define CBH_SHIFT	(9)
#define CBH_SIZE	(1U << CBH_SHIFT)	/* #ptrs per level */
#define CBH_MASK	(CBH_SIZE - 1)
#define CBH_NOB		(CBH_SIZE * sizeof(struct crt_binheap_node *))
#define CBH_POISON	(0xdeadbeef)

/**
 * Binary heap feature bits.
 */
enum cbh_feats {
	/**
	 * By default, the binheap is protected by pthread_mutex.
	 */

	/**
	 * The bin heap has no lock, it means the bin heap is protected
	 * by external lock, or only accessed by a single thread.
	 */
	CBH_FT_NOLOCK		= (1 << 0),

	/**
	 * It is a read-mostly bin heap, so it is protected by RW lock.
	 */
	CBH_FT_RWLOCK		= (1 << 1),
};

struct crt_binheap;

/**
 * Binary heap operations.
 */
struct crt_binheap_ops {
	/**
	 * Called right before inserting a node into the binary heap.
	 *
	 * Implementing this operation is optional.
	 *
	 * \param h [IN]	The heap
	 * \param e [IN]	The node
	 *
	 * \return		zero on success, negative value if error
	 */
	int (*hop_enter)(struct crt_binheap *h, struct crt_binheap_node *e);

	/**
	 * Called right after removing a node from the binary heap.
	 *
	 * Implementing this operation is optional.
	 *
	 * \param h [IN]	The heap
	 * \param e [IN]	The node
	 *
	 * \return		zero on success, negative value if error
	 */
	int (*hop_exit)(struct crt_binheap *h, struct crt_binheap_node *e);

	/**
	 * A binary predicate which is called during internal heap sorting, and
	 * used in order to determine the relevant ordering of two heap nodes.
	 *
	 * Implementing this operation is mandatory.
	 *
	 * \param a [IN]	The first heap node
	 * \param b [IN]	The second heap node
	 *
	 * \return		true if node a < node b,
	 *			false if node a > node b.
	 *
	 * \see crt_binheap_bubble() and crt_biheap_sink()
	 */
	bool (*hop_compare)(struct crt_binheap_node *a,
			    struct crt_binheap_node *b);
};

/**
 * Binary heap.
 */
struct crt_binheap {
	/** different type of locks based on cbt_feats */
	union {
		pthread_mutex_t		    cbh_mutex;
		pthread_rwlock_t	    cbh_rwlock;
	};
	/** feature bits */
	uint32_t			    cbh_feats;

	/** Triple indirect */
	struct crt_binheap_node		****cbh_nodes3;
	/** double indirect */
	struct crt_binheap_node		 ***cbh_nodes2;
	/** single indirect */
	struct crt_binheap_node		  **cbh_nodes1;
	/** operations table */
	struct crt_binheap_ops		   *cbh_ops;
	/** private data */
	void				   *cbh_priv;
	/** # elements referenced */
	uint32_t			    cbh_nodes_cnt;
	/** high water mark */
	uint32_t			    cbh_hwm;
};

/**
 * Creates and initializes a binary heap instance.
 *
 * \param feats [IN]	The heap feats bits
 * \param count [IN]	The initial heap capacity in # of nodes
 * \param priv [IN]	An optional private argument
 * \param ops [IN]	The operations to be used
 * \param h [IN/OUT]	The 2nd level pointer of created binheap
 *
 * \return		zero on success, negative value if error
 */
int crt_binheap_create(uint32_t feats, uint32_t count, void *priv,
		       struct crt_binheap_ops *ops, struct crt_binheap **h);

/**
 * Creates and initializes a binary heap instance inplace.
 *
 * \param feats [IN]	The heap feats bits
 * \param count [IN]	The initial heap capacity in # of nodes
 * \param priv [IN]	An optional private argument
 * \param ops [IN]	The operations to be used
 * \param h [IN]	The pointer of binheap
 *
 * \return		zero on success, negative value if error
 */
int crt_binheap_create_inplace(uint32_t feats, uint32_t count, void *priv,
			       struct crt_binheap_ops *ops,
			       struct crt_binheap *h);

/**
 * Releases all resources associated with a binary heap instance.
 *
 * Deallocates memory for all indirection levels and the binary heap object
 * itself.
 *
 * \param h [IN]	The binary heap object
 */
void crt_binheap_destroy(struct crt_binheap *h);

/**
 * Releases all resources associated with a binary heap instance inplace.
 *
 * Deallocates memory for all indirection levels and clear data in binary heap
 * object as zero.
 *
 * \param h [IN]	The binary heap object
 */
void crt_binheap_destroy_inplace(struct crt_binheap *h);

/**
 * Obtains a pointer to a heap node, given its index into the binary tree.
 *
 * \param h [IN]	The binary heap
 * \param idx [IN]	The requested node's index
 *
 * \return		valid-pointer of the requested heap node,
 *			NULL if index is out of bounds
 */
struct crt_binheap_node *crt_binheap_find(struct crt_binheap *h, uint32_t idx);

/**
 * Sort-inserts a node into the binary heap.
 *
 * \param h [IN]	The heap
 * \param e [IN]	The node
 *
 * \return		0 if the node inserted successfully
 *			negative value if error
 */
int crt_binheap_insert(struct crt_binheap *h, struct crt_binheap_node *e);

/**
 * Removes a node from the binary heap.
 *
 * \param h [IN]	The heap
 * \param e [IN]	The node
 */
void crt_binheap_remove(struct crt_binheap *h, struct crt_binheap_node *e);

/**
 * Removes the root node from the binary heap.
 *
 * \param h [IN]	The heap
 *
 * \return		valid pointer of the removed root node,
 *			or NULL when empty.
 */
struct crt_binheap_node *crt_binheap_remove_root(struct crt_binheap *h);

/**
 * Queries the size (number of nodes) of the binary heap.
 *
 * \param h [IN]	The heap
 *
 * \return		positive value of the size,
 *			or -CER_INVAL for NULL heap.
 */
static inline int
crt_binheap_size(struct crt_binheap *h)
{
	if (h == NULL) {
		C_ERROR("invalid NULL heap.\n");
		return -CER_INVAL;
	}

	return h->cbh_nodes_cnt;
}

/**
 * Queries if the binary heap is empty.
 *
 * \param h [IN]	The heap
 *
 * \return		true when empty (or for NULL heap),
 *			false when non-empty.
 */
static inline bool
crt_binheap_is_empty(struct crt_binheap *h)
{
	if (h == NULL)
		return true;

	return h->cbh_nodes_cnt == 0;
}

/**
 * Gets back the root node of the binary heap.
 *
 * \param h [IN]	The heap
 *
 * \return		valid pointer of the root node, or NULL in error case.
 */
static inline struct crt_binheap_node *
crt_binheap_root(struct crt_binheap *h)
{
	return crt_binheap_find(h, 0);
}

#if defined(__cplusplus)
}
#endif

#endif /* __CRT_HEAP_H__ */
