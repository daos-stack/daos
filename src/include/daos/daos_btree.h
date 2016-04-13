/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * This file is part of daos
 *
 * src/include/daos/daos_btree.h
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#ifndef __DAOS_BTREE_H__
#define __DAOS_BTREE_H__

#include <daos/daos_types.h>
#include <daos/daos_common.h>
#include <daos/daos_mem.h>

enum {
	BTR_UMEM_TYPE	= 100,
	BTR_UMEM_ROOT	= (BTR_UMEM_TYPE + 0),
	BTR_UMEM_NODE	= (BTR_UMEM_TYPE + 1),
};

struct btr_root;
struct btr_node;

TMMID_DECLARE(struct btr_root, BTR_UMEM_ROOT);
TMMID_DECLARE(struct btr_node, BTR_UMEM_NODE);

/**
 * KV record of the btree.
 *
 * NB: could be PM data structure.
 */
struct btr_record {
	/**
	 * It could either be memory ID for the child node, or body of this
	 * record. The record body could be any of varous things:
	 *
	 * - the value address of KV record.
	 * - a structure includes both the variable-length key and value.
	 * - a complex data structure under this record, e.g. a sub-tree.
	 */
	umem_id_t		rec_mmid;
	/**
	 * Fix-size key can be stored in if it is small enough (DAOS_HKEY_MAX),
	 * or hashed key for variable-length/large key. In the later case,
	 * the hashed key can be used for efficient comparison.
	 */
	char			rec_hkey[0];
};

/**
 * Tree node.
 *
 * NB: could be PM data structure.
 */
struct btr_node {
	/** leaf, root etc */
	uint16_t			tn_flags;
	/** number of keys stored in this node */
	uint16_t			tn_keyn;
	/** padding bytes */
	uint32_t			tn_pad_32;
	/** generation, reserved for COW */
	uint64_t			tn_gen;
	/** the first child, it is unused on leaf node */
	TMMID(struct btr_node)		tn_child;
	/** records in this node */
	struct btr_record		tn_recs[0];
};

enum {
	BTR_ORDER_MIN			= 3,
	BTR_ORDER_MAX			= 4096
};

/**
 * Tree root descriptor, it consits of tree attributes and reference to the
 * actual root node.
 *
 * NB: could be PM data structure.
 */
struct btr_root {
	/** btree order */
	uint16_t			tr_order;
	/** depth of the tree */
	uint16_t			tr_depth;
	/**
	 * ID to find a registered tree class, which provides customized
	 * funtions etc.
	 */
	uint32_t			tr_class;
	/** the actual features of the tree, e.g. hash type, integer key */
	uint64_t			tr_feats;
	/** generation, reserved for COW */
	uint64_t			tr_gen;
	/** pointer to the root node, it is NULL for an empty tree */
	TMMID(struct btr_node)		tr_node;
};

struct btr_instance;

/**
 * Customized tree function table.
 */
typedef struct {
	/**
	 * Generate a fix-size hashed key from the real key.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param key	[IN]	key buffer
	 * \param hkey	[OUT]	hashed key
	 */
	void		(*to_hkey_gen)(struct btr_instance *tins,
				       daos_iov_t *key, void *hkey);
	/**
	 * Size of the hashed key.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 */
	int		(*to_hkey_size)(struct btr_instance *tins);
	/**
	 * Optional:
	 * Comparison of hashed key.
	 *
	 * Absent:
	 * Calls memcmp.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 *
	 * \a return	-ve	hkey of \a rec is smaller than \a hkey
	 *		+ve	hkey of \a rec is larger than \a hkey
	 *		0	hkey of \a rec is equal to \a hkey
	 */
	int		(*to_hkey_cmp)(struct btr_instance *tins,
				       struct btr_record *rec, void *hkey);
	/**
	 * Optional:
	 * Comparison of real key. It can be ignored if there is no hash
	 * for the key and key size is fixed. See \a btr_record for the details.
	 *
	 * Absent:
	 * Skip the function and only check rec::rec_hkey for the search.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param rec	[IN]	Record to be compared with \a key.
	 * \param key	[IN]	Key to be compared with key of \a rec.
	 */
	int		(*to_key_cmp)(struct btr_instance *tins,
				      struct btr_record *rec, daos_iov_t *key);
	/**
	 * Allocate record body for \a key and \a val.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param key	[IN]	Key buffer
	 * \param val	[IN]	Value buffer, it could be either data blob,
	 *			or complex data structure that can be parsed
	 *			by the tree class.
	 * \param rec	[OUT]	Returned record body mmid,
	 *			See \a btr_record for the details.
	 */
	int		(*to_rec_alloc)(struct btr_instance *tins,
					daos_iov_t *key, daos_iov_t *val,
					struct btr_record *rec);
	/**
	 * Free the record body stored in \a rec::rec_mmid
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param rec	[IN]	The record to be destroyed.
	 */
	int		(*to_rec_free)(struct btr_instance *tins,
				       struct btr_record *rec);
	/**
	 * If \a copy is true, copy key and value to buffer of \a key and
	 * \a val, otherwise just return key and value address to them.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param rec	[IN]	Record to be read from.
	 * \param copy	[IN]	Copy the key and value to \a key and \a val,
	 *			or just return their addresses
	 * \param key	[OUT]	Optional, sink buffer for returned key,
	 *			or key address.
	 * \param val	[OUT]	Optional, sink buffer for returned value,
	 *			or value address.
	 */
	int		(*to_rec_fetch)(struct btr_instance *tins,
				      struct btr_record *rec, bool copy,
				      daos_iov_t *key, daos_iov_t *val);
	/**
	 * Update value of a record.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param rec	[IN]	Record to be read update.
	 * \param val	[IN]	New value to be stored for the record.
	 * \a return	0	success.
	 *		-ve	error code
	 */
	int		(*to_rec_update)(struct btr_instance *tins,
					 struct btr_record *rec,
					 daos_iov_t *key, daos_iov_t *val);
	/**
	 * Convert record into readable string and store it in \a buf.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param rec	[IN]	Record to be converted.
	 * \param leaf	[IN]	Both key and value should be converted to
	 *			string if it is true, otherwise only the
	 *			hashed key will be converted to string.
	 *			(record of intermediate node),
	 * \param buf	[OUT]	Buffer to store the returned string.
	 * \param buf_len [IN]	Buffer length.
	 */
	char	       *(*to_rec_string)(struct btr_instance *tins,
					 struct btr_record *rec, bool leaf,
					 char *buf, int buf_len);
	/**
	 * Optional:
	 * Allocate an empty tree. Upper layer can have customized
	 * implementation to manange internal tree node and record cache etc.
	 *
	 * Absent:
	 * The common code will allocate the root and initialise the empty tree.
	 *
	 * \param tins	[IN/OUT]
	 *			Input  : Tree instance which includes memory
	 *				 class and customized tree operations.
	 *			output : root mmid
	 * \param feats	[IN]	Feature bits of this true.
	 * \param order	[IN]	Order of the tree.
	 */
	int		(*to_root_alloc)(struct btr_instance *tins,
					 uint64_t feats, unsigned int order);
	/**
	 * Optional:
	 * Free the empty tree, and internal caches.
	 *
	 * Absent:
	 * The common code will free the root.
	 *
	 * \param tins	[IN]	Tree instance to be destroyed.
	 */
	void		(*to_root_free)(struct btr_instance *tins);
	/**
	 * Optional:
	 * Add tree root to current transaction.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid,
	 *			root address and memory class etc.
	 */
	int		(*to_root_tx_add)(struct btr_instance *tins);
	/**
	 * Optional:
	 * Allocate tree node from internal cache.
	 *
	 * Absent:
	 * The common code will allocate the tree node.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param nd_mmid_p [OUT]
	 *			Returned mmid of the new node.
	 *
	 * Absent:
	 * The common code will allocate a new tree node.
	 */
	int		(*to_node_alloc)(struct btr_instance *tins,
					 TMMID(struct btr_node) *nd_mmid_p);
	/**
	 * Optional:
	 * Release the tree node for the internal cache.
	 *
	 * Absent:
	 * The common code will free the tree node.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param nd_mmid [IN]
	 *			Node mmid to be freed.
	 *
	 * Absent:
	 * The common code will free the tree node.
	 */
	void		(*to_node_free)(struct btr_instance *tins,
					TMMID(struct btr_node) nd_mmid);
	/**
	 * Optional: add \a nd_mmid to the current transaction.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param nd_mmid [IN]
	 *			Node mmid to be added to transaction.
	 */
	int		(*to_node_tx_add)(struct btr_instance *tins,
					  TMMID(struct btr_node) nd_mmid);
} btr_ops_t;

/**
 * Tree instance, it is instantiated while creating or opening a tree.
 */
struct btr_instance {
	/** instance of memory class for the tree */
	struct umem_instance		 ti_umm;
	/** root mmid */
	TMMID(struct btr_root)		 ti_root_mmid;
	/** root pointer */
	struct btr_root			*ti_root;
	/** Customized operations for the tree */
	btr_ops_t			*ti_ops;
};

int  dbtree_class_register(unsigned int tree_class, uint64_t tree_feats,
			   btr_ops_t *ops);

int  dbtree_create(unsigned int tree_class, uint64_t tree_feats,
		   unsigned int tree_order, struct umem_attr *uma,
		   TMMID(struct btr_root) *root_tmmid_p,
		   daos_handle_t *toh);
int  dbtree_create_inplace(unsigned int tree_class, uint64_t tree_feats,
			   unsigned int tree_order, struct umem_attr *uma,
			   struct btr_root *root, daos_handle_t *toh);
int  dbtree_open(TMMID(struct btr_root) root_oid, struct umem_attr *uma,
		 daos_handle_t *toh);
int  dbtree_open_inplace(struct btr_root *root, struct umem_attr *uma,
			 daos_handle_t *toh);
int  dbtree_close(daos_handle_t toh);
int  dbtree_destroy(daos_handle_t toh);
int  dbtree_update(daos_handle_t toh, daos_iov_t *key, daos_iov_t *val);
int  dbtree_lookup(daos_handle_t toh, daos_iov_t *key, bool copy,
		   daos_iov_t *val, umem_id_t *rec_body);
int  dbtree_delete(daos_handle_t toh, daos_iov_t *key);
int  dbtree_is_empty(daos_handle_t toh);

/******* iterator API ******************************************************/

/** iterator entry to store the returned KV record */
struct btr_it_record {
	/** buffer to store returned key */
	daos_iov_t		ir_key;
	/** buffer to store returned value */
	daos_iov_t		ir_val;
	/** returned mmid of the record body */
	umem_id_t		ir_mmid;
};

int dbtree_iter_prepare(daos_handle_t toh, daos_handle_t *ih);
int dbtree_iter_finish(daos_handle_t ih);
int dbtree_iter_move(daos_handle_t ih, bool tell, daos_hash_out_t *anchor);
int dbtree_iter_current(daos_handle_t ih, bool copy,
			struct btr_it_record *irec);

#endif /* __DAOS_BTREE_H__ */
