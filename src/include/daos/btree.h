/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of daos
 *
 * src/include/daos/daos_btree.h
 */

#ifndef __DAOS_BTREE_H__
#define __DAOS_BTREE_H__

#include <daos/common.h>
#include <daos_types.h>
#include <daos/mem.h>

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

/** btree attributes returned by query function. */
struct btr_attr {
	/** tree order */
	unsigned int			ba_order;
	/** tree depth */
	unsigned int			ba_depth;
	unsigned int			ba_class;
	uint64_t			ba_feats;
	/** memory class, pmem pool etc */
	struct umem_attr		ba_uma;
};

/** btree statistics returned by query function. */
struct btr_stat {
	/** total number of tree nodes */
	uint64_t			bs_node_nr;
	/** total number of records in the tree */
	uint64_t			bs_rec_nr;
	/** total number of bytes of all keys */
	uint64_t			bs_key_sum;
	/** max key size */
	uint64_t			bs_key_max;
	/** total number of bytes of all values */
	uint64_t			bs_val_sum;
	/** max value size */
	uint64_t			bs_val_max;
};

struct btr_rec_stat {
	/** record key size */
	uint64_t			rs_ksize;
	/** record value size */
	uint64_t			rs_vsize;
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
	 * Fetch value or both key & value of a record.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param rec	[IN]	Record to be read from.
	 * \param key	[OUT]	Optional, sink buffer for the returned key,
	 *			or key address.
	 * \param val	[OUT]	Sink buffer for the returned value or the
	 *			value address.
	 */
	int		(*to_rec_fetch)(struct btr_instance *tins,
					struct btr_record *rec,
					daos_iov_t *key, daos_iov_t *val);
	/**
	 * Update value of a record, the new value should be stored in the
	 * current rec::rec_mmid.
	 *
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param rec	[IN]	Record to be read update.
	 * \param val	[IN]	New value to be stored for the record.
	 * \a return	0	success.
	 *		-DER_NO_PERM
	 *			cannot make inplace change, should call
	 *			rec_free() to release the original record
	 *			and rec_alloc() to create a new record.
	 *		-ve	error code
	 */
	int		(*to_rec_update)(struct btr_instance *tins,
					 struct btr_record *rec,
					 daos_iov_t *key, daos_iov_t *val);
	/**
	 * Optional:
	 * Return key and value size of the record.
	 * \param tins	[IN]	Tree instance which contains the root mmid
	 *			and memory class etc.
	 * \param rec	[IN]	Record to get size from.
	 * \param rstat	[OUT]	Returned key & value size.
	 */
	int		(*to_rec_stat)(struct btr_instance *tins,
				       struct btr_record *rec,
				       struct btr_rec_stat *rstat);
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

typedef enum {
	/**
	 * Public probe opcodes
	 */
	/** the first record in the tree */
	BTR_PROBE_FIRST		= 1,
	/** the last record in the tree */
	BTR_PROBE_LAST		= 2,
	/** probe the record whose key equals to the provide key */
	BTR_PROBE_EQ		= 0x100,
	/** probe the record whose key is great/equal to the provide key */
	BTR_PROBE_GE		= BTR_PROBE_EQ | 1,
	/** probe the record whose key is little/equal to the provide key */
	BTR_PROBE_LE		= BTR_PROBE_EQ | 2,
	/**
	 * private probe opcodes, don't pass them into APIs
	 */
	/** probe the record for update */
	BTR_PROBE_UPDATE	= BTR_PROBE_EQ | 3,
} dbtree_probe_opc_t;

int  dbtree_class_register(unsigned int tree_class, uint64_t tree_feats,
			   btr_ops_t *ops);

int  dbtree_create(unsigned int tree_class, uint64_t tree_feats,
		   unsigned int tree_order, struct umem_attr *uma,
		   TMMID(struct btr_root) *root_mmidp, daos_handle_t *toh);
int  dbtree_create_inplace(unsigned int tree_class, uint64_t tree_feats,
			   unsigned int tree_order, struct umem_attr *uma,
			   struct btr_root *root, daos_handle_t *toh);
int  dbtree_open(TMMID(struct btr_root) root_mmid, struct umem_attr *uma,
		 daos_handle_t *toh);
int  dbtree_open_inplace(struct btr_root *root, struct umem_attr *uma,
			 daos_handle_t *toh);
int  dbtree_close(daos_handle_t toh);
int  dbtree_destroy(daos_handle_t toh);
int  dbtree_update(daos_handle_t toh, daos_iov_t *key, daos_iov_t *val);
int  dbtree_fetch(daos_handle_t toh, dbtree_probe_opc_t opc,
		  daos_iov_t *key, daos_iov_t *key_out, daos_iov_t *val_out);
int  dbtree_lookup(daos_handle_t toh, daos_iov_t *key, daos_iov_t *val_out);
int  dbtree_delete(daos_handle_t toh, daos_iov_t *key);
int  dbtree_query(daos_handle_t toh, struct btr_attr *attr,
		  struct btr_stat *stat);
int  dbtree_is_empty(daos_handle_t toh);

/******* iterator API ******************************************************/

enum {
	/**
	 * Use the embedded iterator of the open handle.
	 * It can reduce memory consumption, but state of iterator can be
	 * overwritten by other tree operation.
	 */
	BTR_ITER_EMBEDDED	= (1 << 0),
};

int dbtree_iter_prepare(daos_handle_t toh, unsigned int options,
			daos_handle_t *ih);
int dbtree_iter_finish(daos_handle_t ih);
int dbtree_iter_probe(daos_handle_t ih, dbtree_probe_opc_t opc,
		      daos_iov_t *key, daos_hash_out_t *anchor);
int dbtree_iter_next(daos_handle_t ih);
int dbtree_iter_prev(daos_handle_t ih);
int dbtree_iter_fetch(daos_handle_t ih, daos_iov_t *key,
		      daos_iov_t *val, daos_hash_out_t *anchor);
int dbtree_iter_delete(daos_handle_t ih);

/**
 * Prototype of dbtree_iterate() callbacks. When a callback returns an rc,
 *
 *   - if rc == 0, dbtree_iterate() continues;
 *   - if rc == 1, dbtree_iterate() stops and returns 0;
 *   - otherwise, dbtree_iterate() stops and returns rc.
 */
typedef int (*dbtree_iterate_cb_t)(daos_iov_t *key, daos_iov_t *val, void *arg);
int dbtree_iterate(daos_handle_t toh, bool backward, dbtree_iterate_cb_t cb,
		   void *arg);

enum {
	DBTREE_VOS_BEGIN	= 10,
	DBTREE_VOS_END		= DBTREE_VOS_BEGIN + 9,
	DBTREE_DSM_BEGIN	= 20,
	DBTREE_DSM_END		= DBTREE_DSM_BEGIN + 9,
};

#endif /* __DAOS_BTREE_H__ */
