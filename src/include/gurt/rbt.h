/*
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */


#ifndef __GURT_RBT_H__
#define __GURT_RBT_H__

#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

/** @addtogroup GURT
 * @{
 */
/******************************************************************************
 * Generic Red Black Tree APIs / data structures
 ******************************************************************************/

typedef struct d_rbt d_rbt_t;

struct d_rbt_node {
	void *rn_key;
	void *rn_data;
};

typedef struct d_rbt_node d_rbt_node_t;

/**
 * Create a new RBT.
 *
 * \param[in] cmp_key	Comparaison function of RBT nodes keys
 * \param[in] free_node	Free function of the key and the data holding by an RBT node
 * \param[out] rbt	Created RBT
 *
 * \return		0 on success, negative value on error
 */
int
d_rbt_create(int (*cmp_key)(const void *, const void *), void (*free_node)(d_rbt_node_t *),
             d_rbt_t **rbt);

/**
 * Destroy an RBT with recursively destroying all its nodes.
 *
 * if \p free_node is true the content of the rbt node is freed.
 *
 * \param[in,out] rbt	The RBT to destroy
 * \param[in] free_node	If true, the content of the node will be freed
 */
void
d_rbt_destroy(d_rbt_t *rbt, bool free_node);

/**
 * lookup \p key in the RBT, the found node is returned on success.
 *
 * \param[in] rbt	The RBT to lookup
 * \param[in] key	The key to look at
 *
 * \return the searched RBT node or NULL if key does not belong to the RBT
 */
d_rbt_node_t *
d_rbt_find(const d_rbt_t *rbt_t, const void *key);

/**
 * Insert a new node into the RBT with the given \p key and \p data.
 *
 * If \p overwrite is true and the RBT already contains a node with the same \p key value, then the
 * content of this node is overwriten and the old content is freed.  Otherwise this function returns
 * a negative value.
 *
 * \param[in,out] rbt	The RBT to update
 * \param[in] key	The key to insert
 * \param[in] data	The data to insert
 * \param[in] overwrite	If true, overwrite the content of a node with the same key value
 *
 * \return		0 on success, negative value on error
 * */
int
d_rbt_insert(d_rbt_t *rbt, void *key, void *data, bool overwrite);

/**
 * Lookup \p key in the RBT, if there is a matched node, it should be returned.  Otherwise a new
 * node will be inserted into the RBT, and \p node_new will holds a pointer on this new node.
 *
 * \param[in,out] rbt	The RBT to lookup and update
 * \param[in] key	The key to find or insert
 * \param[in] data	The data to find or insert
 * \param[in] node_new	Pointer on the found node if it exists
 *
 * \return		0 on success, negative value on error
 */
int
d_rbt_find_insert(d_rbt_t *rbt, void *key, void *data, d_rbt_node_t **node_new);

/**
 * Delete the node identified by \p key from the RBT
 *
 * \param[in,out] rbt	The RBT to update
 * \param[in] key	The key to delete
 * \param[in] free_node	If true, the content of the node will be freed
 *
 * \return		0 on success, negative value on error
 */
int
d_rbt_delete(d_rbt_t *rbt, const void *key, bool free_node);

/**
 * Return the node with the min key value.
 *
 * \param[in] rbt	The RBT to lookup
 *
 * \return		Node with min key value
 */
d_rbt_node_t *
d_rbt_get_first_node(const d_rbt_t *rbt);

/**
 * Return the node with the max key value.
 *
 * \param[in] rbt	The RBT to lookup
 *
 * \return		Node with max key value
 */
d_rbt_node_t *
d_rbt_get_last_node(const d_rbt_t *rbt);

/**
 * Return the next node of an RBT or NULL if \p node is the last node.
 *
 * \param[in] rbt	The RBT to lookup
 * \param[in] node	The RBT node to lookup
 *
 * \return		Node following \p node in the RBT \rbt or NULL if \p node is the last node
 */
d_rbt_node_t *
d_rbt_node_next(const d_rbt_t *rbt, const d_rbt_node_t *node);

/**
 * Return the preceding node of an RBT or NULL if \p node is the first node.
 *
 * \param[in] rbt	The RBT to lookup
 * \param[in] node	The RBT node to lookup
 *
 * \return		Node preceding \p node in the RBT \rbt or NULL if \p node is the first node
 */
d_rbt_node_t *
d_rbt_node_prev(const d_rbt_t *rbt, const d_rbt_node_t *node);

/**
 * Return the minimal depth of an RBT.
 *
 * \param[in] rbt	The RBT to lookup
 *
 * \return		Minimal depth of \p rbt
 */
size_t
d_rbt_get_depth_min(const d_rbt_t *rbt);

/**
 * Return the maximal depth of an RBT.
 *
 * \param[in] rbt	The RBT to lookup
 *
 * \return		Maximal depth of \p rbt
 */
size_t
d_rbt_get_depth_max(const d_rbt_t *rbt);

/**
 * Check of an RBT is properly sorted.
 *
 * \param[in] rbt	The RBT to ckeck
 *
 * \return		True iff \p rbt is sorted
 */
bool
d_rbt_is_sorted(const d_rbt_t *rbt);

/**
 * Return the number of black height of an RBT.
 *
 * \param[in] rbt	The RBT to lookup
 *
 * \return		Number of black height
 */
size_t
d_rbt_get_black_height(const d_rbt_t *rbt);

/**
 * Pretty print the content and stats of an RBT.
 *
 * \param[in] rbt	        The RBT to print
 * \param[in] print_node	Function printing the content of an RBT node on stdout
 */
void
d_rbt_print(const d_rbt_t *rbt, void (*print_node)(const d_rbt_node_t *));

/** @}
*/

#if defined(__cplusplus)
}
#endif

#endif /* __GURT_RBT_H__ */
