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
 * common/btree.c
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#include <daos_errno.h>
#include <daos/btree.h>

/**
 * Tree node types.
 * NB: a node can be both root and leaf.
 */
typedef enum {
	BTR_NODE_LEAF		= (1 << 0),
	BTR_NODE_ROOT		= (1 << 1),
} btr_node_type_t;

/**
 * Btree class definition.
 */
struct btr_class {
	/** class feature bits, e.g. hash type for the key */
	uint64_t			 tc_feats;
	/** customized function table */
	btr_ops_t			*tc_ops;
};

/**
 * Scratch buffer to store a record, this struct can be put in stack,
 * whereas btr_record cannot because rec_key is zero size.
 */
union btr_rec_buf {
	struct btr_record		rb_rec;
	struct {
		struct btr_record	rec;
		char			key[DAOS_HKEY_MAX];
	}				rb_buf;
};

/** internal state of iterator */
enum {
	BTR_ITR_NONE	= 0,
	/** initialized */
	BTR_ITR_INIT,
	/** ready to iterate */
	BTR_ITR_READY,
	/** no record or reach the end of iteration */
	BTR_ITR_FINI,
};

/**
 * btree iterator, it is embedded in btr_context.
 */
struct btr_iterator {
	/** state of the iterator */
	unsigned short			 it_state;
	/** private iterator */
	bool				 it_private;
	/**
	 * Reserved for hash collision:
	 * collisions happened on current hkey.
	 */
	unsigned int			 it_collisions;
};

/**
 * Trace for tree search.
 */
struct btr_trace {
	/** pointer to a tree node */
	TMMID(struct btr_node)		tr_node;
	/** child/record index within this node */
	unsigned int			tr_at;
};

/** backtrace depth */
#define BTR_TRACE_MAX		40

/**
 * Context for btree operations.
 * NB: object cache will retain this data structure.
 */
struct btr_context {
	/** Tree domain: root pointer, memory pool and memory class etc */
	struct btr_instance		 tc_tins;
	/** embedded iterator */
	struct btr_iterator		 tc_itr;
	/** cached tree order, avoid loading from slow memory */
	short				 tc_order;
	/** cached tree depth, avoid loading from slow memory */
	short				 tc_depth;
	/** refcount, used by iterator */
	unsigned short			 tc_ref;
	/** cached tree class, avoid loading from slow memory */
	unsigned short			 tc_class;
	/** cached feature bits, avoid loading from slow memory */
	uint64_t			 tc_feats;
	/** trace for the tree root */
	struct btr_trace		*tc_trace;
	/** trace buffer */
	struct btr_trace		 tc_traces[BTR_TRACE_MAX];
};

/** size of print buffer */
#define BTR_PRINT_BUF			128

static int btr_class_init(TMMID(struct btr_root) root_mmid,
			  struct btr_root *root, unsigned int tree_class,
			  uint64_t tree_feats, struct umem_attr *uma,
			  struct btr_instance *tins);
static struct btr_record *btr_node_rec_at(struct btr_context *tcx,
					  TMMID(struct btr_node) nd_mmid,
					  unsigned int at);
static int btr_node_insert_rec(struct btr_context *tcx,
			       struct btr_trace *trace,
			       struct btr_record *rec);
static int btr_root_tx_add(struct btr_context *tcx);
static bool btr_probe_prev(struct btr_context *tcx);
static bool btr_probe_next(struct btr_context *tcx);
static bool btr_probe_is_public(dbtree_probe_opc_t opc);

static struct umem_instance *
btr_umm(struct btr_context *tcx)
{
	return &tcx->tc_tins.ti_umm;
}

static bool
btr_has_tx(struct btr_context *tcx)
{
	return umem_has_tx(btr_umm(tcx));
}

#define btr_mmid2ptr(tcx, mmid)			\
	umem_id2ptr_typed(btr_umm(tcx), mmid)

#define BTR_NODE_NULL	TMMID_NULL(struct btr_node)
#define BTR_ROOT_NULL	TMMID_NULL(struct btr_root)

/**
 * Tree context functions
 */

/** create handle for the tree context */
static daos_handle_t
btr_tcx2hdl(struct btr_context *tcx)
{
	daos_handle_t hdl;

	/* XXX use handle table */
	hdl.cookie = (uint64_t)tcx;
	return hdl;
}

/** find the tree context of the handle */
static struct btr_context *
btr_hdl2tcx(daos_handle_t toh)
{
	/* XXX use handle table */
	return (struct btr_context *)toh.cookie;
}

void
btr_context_addref(struct btr_context *tcx)
{
	tcx->tc_ref++;
}

/** release refcount on btree context (in volatile memory) */
static void
btr_context_decref(struct btr_context *tcx)
{
	D_ASSERT(tcx->tc_ref > 0);
	tcx->tc_ref--;
	if (tcx->tc_ref == 0)
		D_FREE_PTR(tcx);
}

/**
 * Create a btree context (in volatile memory).
 *
 * \param root_mmid	MMID of root.
 * \param tree_class	Tree class ID.
 * \param tree_feats	Tree features (the same tree class may have different
 *			features for different library versions).
 * \param tree_order	Tree order.
 * \param uma		Memory class attributes.
 * \param tcxp		Returned context.
 */
static int
btr_context_create(TMMID(struct btr_root) root_mmid, struct btr_root *root,
		   unsigned int tree_class, uint64_t tree_feats,
		   unsigned int tree_order, struct umem_attr *uma,
		   struct btr_context **tcxp)
{
	struct btr_context	*tcx;
	int			 rc;

	D_ALLOC_PTR(tcx);
	if (tcx == NULL)
		return -DER_NOMEM;

	rc = btr_class_init(root_mmid, root, tree_class, tree_feats, uma,
			    &tcx->tc_tins);
	if (rc != 0)
		goto failed;

	root = tcx->tc_tins.ti_root;
	if (root == NULL || root->tr_class == 0) { /* tree creation */
		tcx->tc_class	= tree_class;
		tcx->tc_feats	= tree_feats;
		tcx->tc_order	= tree_order;
		D_DEBUG(DF_MISC, "Create context for a new tree\n");

	} else {
		tcx->tc_class	= root->tr_class;
		tcx->tc_feats	= root->tr_feats;
		tcx->tc_order	= root->tr_order;
		tcx->tc_depth	= root->tr_depth;
		D_DEBUG(DF_MISC, "Load tree context from "TMMID_PF"\n",
			TMMID_P(root_mmid));
	}

	tcx->tc_trace = &tcx->tc_traces[BTR_TRACE_MAX - tcx->tc_depth];
	tcx->tc_ref = 1;
	*tcxp = tcx;
	return 0;

 failed:
	D_DEBUG(DF_MISC, "Failed to create tree context: %d\n", rc);
	btr_context_decref(tcx);
	return rc;
}

static int
btr_context_clone(struct btr_context *tcx, struct btr_context **tcx_p)
{
	struct umem_attr uma;
	int		 rc;

	umem_attr_get(&tcx->tc_tins.ti_umm, &uma);
	rc = btr_context_create(tcx->tc_tins.ti_root_mmid,
				tcx->tc_tins.ti_root, -1, -1, -1, &uma, tcx_p);
	return rc;
}

/**
 * Set trace for the specified level, it will increase depth and set trace
 * for the new root if \a level is -1.
 */
static void
btr_trace_set(struct btr_context *tcx, int level,
	      TMMID(struct btr_node) nd_mmid, int at)
{
	D_ASSERT(at >= 0 && at < tcx->tc_order);

	if (level == -1) { /* add the new root to the trace */
		level = 0;
		tcx->tc_trace--;
		tcx->tc_depth++;
	}
	D_ASSERT(tcx->tc_depth > 0);

	D_ASSERT(level >= 0 && level < tcx->tc_depth);
	D_ASSERT(&tcx->tc_trace[level] < &tcx->tc_traces[BTR_TRACE_MAX]);

	D_DEBUG(DF_MISC, "trace[%d] "TMMID_PF"/%d\n",
		level, TMMID_P(nd_mmid), at);

	tcx->tc_trace[level].tr_node = nd_mmid;
	tcx->tc_trace[level].tr_at = at;
}

/** fetch the record of the specified trace level */
static struct btr_record *
btr_trace2rec(struct btr_context *tcx, int level)
{
	struct btr_trace *trace;

	D_ASSERT(tcx->tc_depth > 0);
	D_ASSERT(tcx->tc_depth > level);

	trace = &tcx->tc_trace[level];
	D_ASSERT(!TMMID_IS_NULL(trace->tr_node));

	return btr_node_rec_at(tcx, trace->tr_node, trace->tr_at);
}

#define									\
btr_trace_debug(tcx, trace, format, ...)				\
do {									\
	TMMID(struct btr_node) __mmid = (trace)->tr_node;		\
	int __level = (int)((trace) - (tcx)->tc_trace);			\
									\
	D_DEBUG(DF_MISC,						\
		"node="TMMID_PF" (l=%d k=%d at=%d): " format,		\
		TMMID_P(__mmid), __level,				\
		btr_mmid2ptr((tcx), __mmid)->tn_keyn,			\
		(trace)->tr_at,	## __VA_ARGS__);			\
} while (0)

/**
 * Wrapper for customized tree functions
 */

static inline btr_ops_t *
btr_ops(struct btr_context *tcx)
{
	return tcx->tc_tins.ti_ops;
}

static int
btr_hkey_size(struct btr_context *tcx)
{
	int size = btr_ops(tcx)->to_hkey_size(&tcx->tc_tins);

	D_ASSERT(size <= DAOS_HKEY_MAX);
	return size;
}

static void
btr_hkey_gen(struct btr_context *tcx, daos_iov_t *key, void *hkey)
{
	return btr_ops(tcx)->to_hkey_gen(&tcx->tc_tins, key, hkey);
}

static int
btr_hkey_cmp(struct btr_context *tcx, struct btr_record *rec, void *hkey)
{
	if (btr_ops(tcx)->to_hkey_cmp)
		return btr_ops(tcx)->to_hkey_cmp(&tcx->tc_tins, rec, hkey);
	else
		return memcmp(&rec->rec_hkey[0], hkey, btr_hkey_size(tcx));
}

static int
btr_key_cmp(struct btr_context *tcx, struct btr_record *rec, daos_iov_t *key)
{
	if (btr_ops(tcx)->to_key_cmp)
		return btr_ops(tcx)->to_key_cmp(&tcx->tc_tins, rec, key);
	else
		return 0;
}

static int
btr_rec_alloc(struct btr_context *tcx, daos_iov_t *key, daos_iov_t *val,
	       struct btr_record *rec)
{
	return btr_ops(tcx)->to_rec_alloc(&tcx->tc_tins, key, val, rec);
}

static void
btr_rec_free(struct btr_context *tcx, struct btr_record *rec)
{
	if (!UMMID_IS_NULL(rec->rec_mmid))
		btr_ops(tcx)->to_rec_free(&tcx->tc_tins, rec);
}

/**
 * Fetch key and value of the record, key is optional, both key and value
 * are output parameters
 */
static int
btr_rec_fetch(struct btr_context *tcx, struct btr_record *rec,
	      daos_iov_t *key, daos_iov_t *val)
{
	return btr_ops(tcx)->to_rec_fetch(&tcx->tc_tins, rec, key, val);
}

static int
btr_rec_update(struct btr_context *tcx, struct btr_record *rec,
		daos_iov_t *key, daos_iov_t *val)
{
	return btr_ops(tcx)->to_rec_update(&tcx->tc_tins, rec, key, val);
}

static char *
btr_rec_string(struct btr_context *tcx, struct btr_record *rec,
	       bool leaf, char *buf, int buf_len)
{
	D_ASSERT(buf_len > 1);

	if (btr_ops(tcx)->to_rec_string == NULL) {
		buf[0] = '?';
		buf[1] = '\0';
		return buf;
	}
	return btr_ops(tcx)->to_rec_string(&tcx->tc_tins, rec, leaf, buf,
					   buf_len);
}

static inline int
btr_rec_size(struct btr_context *tcx)
{
	return btr_hkey_size(tcx) + sizeof(struct btr_record);
}

static struct btr_record *
btr_rec_at(struct btr_context *tcx, struct btr_record *rec, int at)
{
	char	*buf = (char *)rec;

	return (struct btr_record *)&buf[at * btr_rec_size(tcx)];
}

static inline int
btr_node_size(struct btr_context *tcx)
{
	return sizeof(struct btr_node) + tcx->tc_order * btr_rec_size(tcx);
}

static int
btr_node_alloc(struct btr_context *tcx, TMMID(struct btr_node) *nd_mmid_p)
{
	struct btr_node		*nd;
	TMMID(struct btr_node)	 nd_mmid;
	int			 rc;

	if (btr_ops(tcx)->to_node_alloc) {
		rc = btr_ops(tcx)->to_node_alloc(&tcx->tc_tins, &nd_mmid);
		if (rc != 0)
			return rc;
	} else {
		nd_mmid = umem_zalloc_typed(btr_umm(tcx), struct btr_node,
					    btr_node_size(tcx));
		if (TMMID_IS_NULL(nd_mmid))
			return -DER_NOMEM;
	}

	D_DEBUG(DF_MISC, "Allocate new node "TMMID_PF"\n", TMMID_P(nd_mmid));
	nd = btr_mmid2ptr(tcx, nd_mmid);
	nd->tn_child = BTR_NODE_NULL;

	*nd_mmid_p = nd_mmid;
	return 0;
}

static void
btr_node_free(struct btr_context *tcx, TMMID(struct btr_node) nd_mmid)
{
	if (btr_ops(tcx)->to_node_free)
		btr_ops(tcx)->to_node_free(&tcx->tc_tins, nd_mmid);
	else
		umem_free_typed(btr_umm(tcx), nd_mmid);
}

static int
btr_node_tx_add(struct btr_context *tcx, TMMID(struct btr_node) nd_mmid)
{
	int	rc;

	if (btr_ops(tcx)->to_node_tx_add) {
		rc = btr_ops(tcx)->to_node_tx_add(&tcx->tc_tins, nd_mmid);
	} else {
		rc = umem_tx_add_typed(btr_umm(tcx), nd_mmid,
				       btr_node_size(tcx));
	}
	return rc;
}

/* helper functions */

static struct btr_record *
btr_node_rec_at(struct btr_context *tcx, TMMID(struct btr_node) nd_mmid,
		unsigned int at)
{
	struct btr_node *nd = btr_mmid2ptr(tcx, nd_mmid);
	char		*addr = (char *)&nd[1];

	return (struct btr_record *)&addr[btr_rec_size(tcx) * at];
}

static umem_id_t
btr_node_mmid_at(struct btr_context *tcx, TMMID(struct btr_node) nd_mmid,
		 unsigned int at)
{
	struct btr_node	  *nd = btr_mmid2ptr(tcx, nd_mmid);
	struct btr_record *rec;

	/* NB: non-leaf node has +1 children than nkeys */
	if (!(nd->tn_flags & BTR_NODE_LEAF)) {
		if (at == 0)
			return umem_id_t2u(nd->tn_child);
		at--;
	}
	rec = btr_node_rec_at(tcx, nd_mmid, at);
	return rec->rec_mmid;
}

static inline bool
btr_node_is_full(struct btr_context *tcx, TMMID(struct btr_node) nd_mmid)
{
	struct btr_node *nd = btr_mmid2ptr(tcx, nd_mmid);

	D_ASSERT(nd->tn_keyn < tcx->tc_order);
	return nd->tn_keyn == tcx->tc_order - 1;
}

static inline void
btr_node_set(struct btr_context *tcx, TMMID(struct btr_node) nd_mmid,
	     unsigned int bits)
{
	struct btr_node *nd = btr_mmid2ptr(tcx, nd_mmid);

	nd->tn_flags |= bits;
}

static inline void
btr_node_unset(struct btr_context *tcx, TMMID(struct btr_node) nd_mmid,
	       unsigned int bits)
{
	struct btr_node *nd = btr_mmid2ptr(tcx, nd_mmid);

	nd->tn_flags &= ~bits;
}

static inline bool
btr_node_is_set(struct btr_context *tcx, TMMID(struct btr_node) nd_mmid,
		unsigned int bits)
{
	struct btr_node *nd = btr_mmid2ptr(tcx, nd_mmid);

	return nd->tn_flags & bits;
}

static inline bool
btr_node_is_leaf(struct btr_context *tcx, TMMID(struct btr_node) nd_mmid)
{
	return btr_node_is_set(tcx, nd_mmid, BTR_NODE_LEAF);
}

static inline bool
btr_node_is_root(struct btr_context *tcx, TMMID(struct btr_node) nd_mmid)
{
	return btr_node_is_set(tcx, nd_mmid, BTR_NODE_ROOT);
}

static inline bool
btr_node_is_equal(struct btr_context *tcx, TMMID(struct btr_node) mmid1,
		  TMMID(struct btr_node) mmid2)
{
	return umem_id_equal_typed(btr_umm(tcx), mmid1, mmid2);
}

static bool
btr_root_empty(struct btr_context *tcx)
{
	struct btr_root *root = tcx->tc_tins.ti_root;

	return root == NULL || TMMID_IS_NULL(root->tr_node);
}

static void
btr_root_free(struct btr_context *tcx)
{
	struct btr_instance *tins = &tcx->tc_tins;

	if (TMMID_IS_NULL(tins->ti_root_mmid)) {
		struct btr_root *root = tins->ti_root;

		if (root == NULL)
			return;

		D_DEBUG(DF_MISC, "Destroy inplace created tree root\n");
		if (btr_has_tx(tcx))
			btr_root_tx_add(tcx);

		memset(root, 0, sizeof(*root));
	} else {
		D_DEBUG(DF_MISC, "Destroy tree root\n");
		if (btr_ops(tcx)->to_root_free)
			btr_ops(tcx)->to_root_free(tins);
		else
			umem_free_typed(btr_umm(tcx), tins->ti_root_mmid);
	}

	tins->ti_root_mmid = TMMID_NULL(struct btr_root);
	tins->ti_root = NULL;
}

static int
btr_root_init(struct btr_context *tcx, struct btr_root *root)
{
	struct btr_instance *tins = &tcx->tc_tins;
	int		     rc;

	tins->ti_root = root;
	if (TMMID_IS_NULL(tins->ti_root_mmid) && btr_has_tx(tcx)) {
		/* externally allocated root and has transaction */
		rc = btr_root_tx_add(tcx);
		if (rc != 0)
			return rc;
	}

	root->tr_class	= tcx->tc_class;
	root->tr_feats	= tcx->tc_feats;
	root->tr_order	= tcx->tc_order;
	root->tr_node	= BTR_NODE_NULL;

	return 0;
}

static int
btr_root_alloc(struct btr_context *tcx)
{
	struct btr_instance	*tins = &tcx->tc_tins;
	struct btr_root		*root;
	int			 rc;

	if (btr_ops(tcx)->to_root_alloc) {
		rc = btr_ops(tcx)->to_root_alloc(tins, tcx->tc_feats,
						 tcx->tc_order);
		if (rc != 0)
			return rc;

		D_ASSERT(!TMMID_IS_NULL(tins->ti_root_mmid));
	} else {
		tins->ti_root_mmid = umem_znew_typed(btr_umm(tcx),
						     struct btr_root);
		if (TMMID_IS_NULL(tins->ti_root_mmid))
			return -DER_NOMEM;
	}

	root = btr_mmid2ptr(tcx, tins->ti_root_mmid);
	return btr_root_init(tcx, root);
}

static int
btr_root_tx_add(struct btr_context *tcx)
{
	struct btr_instance	*tins = &tcx->tc_tins;
	int			 rc = 0;

	if (btr_ops(tcx)->to_root_tx_add) {
		rc = btr_ops(tcx)->to_root_tx_add(tins);

	} else if (!TMMID_IS_NULL(tins->ti_root_mmid)) {
		rc = umem_tx_add_mmid_typed(btr_umm(tcx),
					    tcx->tc_tins.ti_root_mmid);
	} else {
		rc = umem_tx_add_ptr(btr_umm(tcx), tcx->tc_tins.ti_root,
				     sizeof(struct btr_root));
	}
	return rc;
}

/**
 * Create btr_node for the empty root, insert the first \a rec into it.
 */
int
btr_root_start(struct btr_context *tcx, struct btr_record *rec)
{
	struct btr_root		*root;
	struct btr_record	*rec_dst;
	TMMID(struct btr_node)	 nd_mmid;
	int			 rc;

	root = tcx->tc_tins.ti_root;

	D_ASSERT(TMMID_IS_NULL(root->tr_node));
	D_ASSERT(root->tr_depth == 0);

	rc = btr_node_alloc(tcx, &nd_mmid);
	if (rc != 0) {
		D_DEBUG(DF_MISC, "Failed to allocate new root\n");
		return rc;
	}

	/* root is also leaf, records are stored in root */
	btr_node_set(tcx, nd_mmid, BTR_NODE_ROOT | BTR_NODE_LEAF);
	btr_mmid2ptr(tcx, nd_mmid)->tn_keyn = 1;

	rec_dst = btr_node_rec_at(tcx, nd_mmid, 0);
	memcpy(rec_dst, rec, btr_rec_size(tcx));

	if (btr_has_tx(tcx))
		btr_root_tx_add(tcx); /* XXX check error */

	root->tr_node = nd_mmid;
	root->tr_depth = 1;

	/* add the new root to the backtrace, -1 means it is a new root. */
	btr_trace_set(tcx, -1, nd_mmid, 0);
	return 0;
}

/**
 * Add a new root to the tree, then insert \a rec to the new root.
 *
 * \param tcx	[IN]	Tree operation context.
 * \param mmid_left [IN]
 *			the original root, it is left child for the new root.
 * \param rec	[IN]	The record to be inserted to the new root.
 */
int
btr_root_grow(struct btr_context *tcx, TMMID(struct btr_node) mmid_left,
	      struct btr_record *rec)
{
	struct btr_root		*root;
	struct btr_node		*nd;
	struct btr_record	*rec_dst;
	TMMID(struct btr_node)	 nd_mmid;
	int			 at;
	int			 rc;

	root = tcx->tc_tins.ti_root;
	D_ASSERT(root->tr_depth != 0);

	D_DEBUG(DF_MISC, "Grow the tree depth to %d\n", root->tr_depth + 1);

	rc = btr_node_alloc(tcx, &nd_mmid);
	if (rc != 0) {
		D_DEBUG(DF_MISC, "Failed to allocate new root\n");
		return rc;
	}

	/* the left child is the old root */
	D_ASSERT(btr_node_is_root(tcx, mmid_left));
	btr_node_unset(tcx, mmid_left, BTR_NODE_ROOT);

	btr_node_set(tcx, nd_mmid, BTR_NODE_ROOT);
	rec_dst = btr_node_rec_at(tcx, nd_mmid, 0);
	memcpy(rec_dst, rec, btr_rec_size(tcx));

	nd = btr_mmid2ptr(tcx, nd_mmid);
	nd->tn_child	= mmid_left;
	nd->tn_keyn	= 1;

	/* replace the root mmid, increase tree level */
	if (btr_has_tx(tcx))
		btr_root_tx_add(tcx); /* XXX check error */

	root->tr_node = nd_mmid;
	root->tr_depth++;

	at = !btr_node_is_equal(tcx, mmid_left, tcx->tc_trace->tr_node);
	/* add the new root to the backtrace, -1 means it is a new root. */
	btr_trace_set(tcx, -1, nd_mmid, at);
	return 0;
}

static void
btr_node_insert_rec_only(struct btr_context *tcx, struct btr_trace *trace,
			 struct btr_record *rec)
{
	struct btr_record *rec_a;
	struct btr_record *rec_b;
	struct btr_node   *nd;
	bool		   leaf;
	char		   sbuf[BTR_PRINT_BUF];

	/* NB: assume trace->tr_node has been added to TX */
	D_ASSERT(!btr_node_is_full(tcx, trace->tr_node));

	leaf = btr_node_is_leaf(tcx, trace->tr_node);
	btr_trace_debug(tcx, trace, "insert %s now\n",
			btr_rec_string(tcx, rec, leaf, sbuf, BTR_PRINT_BUF));

	rec_a = btr_node_rec_at(tcx, trace->tr_node, trace->tr_at);
	rec_b = btr_node_rec_at(tcx, trace->tr_node, trace->tr_at + 1);

	nd = btr_mmid2ptr(tcx, trace->tr_node);
	if (trace->tr_at != nd->tn_keyn) {
		memmove(rec_b, rec_a,
			btr_rec_size(tcx) * (nd->tn_keyn - trace->tr_at));
	}

	memcpy(rec_a, rec, btr_rec_size(tcx));
	nd->tn_keyn++;
}

/**
 * Where I should split a node.
 */
static int
btr_split_at(struct btr_context *tcx, int level,
	     TMMID(struct btr_node) mmid_left,
	     TMMID(struct btr_node) mmid_right)
{
	struct btr_trace *trace = &tcx->tc_trace[level];
	int		  order = tcx->tc_order;
	int		  split_at;
	bool		  left;

	split_at = order / 2;

	left = (trace->tr_at < split_at);
	if (!btr_node_is_leaf(tcx, mmid_left))
		split_at -= left;

	btr_trace_debug(tcx, trace, "split_at %d, insert to the %s node\n",
			split_at, left ? "left" : "right");
	if (left)
		btr_trace_set(tcx, level, mmid_left, trace->tr_at);
	else
		btr_trace_set(tcx, level, mmid_right, trace->tr_at - split_at);

	return split_at;
}

/**
 * split a tree node at level \a level
 */
static int
btr_node_split_and_insert(struct btr_context *tcx, struct btr_trace *trace,
			  struct btr_record *rec)
{
	struct btr_record	*rec_src;
	struct btr_record	*rec_dst;
	struct btr_node		*nd_left;
	struct btr_node		*nd_right;
	int			 split_at;
	int			 rec_size;
	int			 level;
	int			 rc;
	bool			 leaf;
	bool			 right;
	char			 hkey_buf[DAOS_HKEY_MAX];
	TMMID(struct btr_node)	 mmid_left;
	TMMID(struct btr_node)	 mmid_right;

	D_ASSERT(trace >= tcx->tc_trace);
	level = trace - tcx->tc_trace;
	mmid_left = trace->tr_node;

	rc = btr_node_alloc(tcx, &mmid_right);
	if (rc != 0)
		return rc;

	leaf = btr_node_is_leaf(tcx, mmid_left);
	if (leaf)
		btr_node_set(tcx, mmid_right, BTR_NODE_LEAF);

	split_at = btr_split_at(tcx, level, mmid_left, mmid_right);

	rec_src = btr_node_rec_at(tcx, mmid_left, split_at);
	rec_dst = btr_node_rec_at(tcx, mmid_right, 0);

	nd_left	 = btr_mmid2ptr(tcx, mmid_left);
	nd_right = btr_mmid2ptr(tcx, mmid_right);

	nd_right->tn_keyn = nd_left->tn_keyn - split_at;
	nd_left->tn_keyn  = split_at;

	rec_size = btr_rec_size(tcx);
	if (leaf) {
		D_DEBUG(DF_MISC, "Splitting leaf node\n");

		memcpy(rec_dst, rec_src, rec_size * nd_right->tn_keyn);
		btr_node_insert_rec_only(tcx, trace, rec);
		/* insert the right node and the first key of the right
		 * node to its parent
		 */
		memcpy(&rec->rec_hkey[0], &rec_dst->rec_hkey[0],
		       btr_hkey_size(tcx));
		goto bubble_up;
	}
	/* non-leaf */

	right = btr_node_is_equal(tcx, trace->tr_node, mmid_right);
	if (trace->tr_at == 0 && right) {
		/* the new record is the first one on the right node */
		D_DEBUG(DF_MISC, "Bubble up the new key\n");
		nd_right->tn_child = umem_id_u2t(rec->rec_mmid,
						 struct btr_node);
		memcpy(rec_dst, rec_src, rec_size * nd_right->tn_keyn);
		goto bubble_up;
	}

	D_DEBUG(DF_MISC, "Bubble up the 1st key of the right node\n");

	nd_right->tn_child = umem_id_u2t(rec_src->rec_mmid, struct btr_node);
	/* btr_split_at should ensure the right node has more than one record,
	 * because the first record of the right node will bubble up.
	 * (@src_rec[0] is this record at this point)
	 */
	D_ASSERT(nd_right->tn_keyn > 1 || right);
	nd_right->tn_keyn--;
	/* insertion point has to be shifted if the new record is going to
	 * be inserted to the right node.
	 */
	trace->tr_at -= right;

	/* Copy from @rec_src[1] because @rec_src[0] will bubble up.
	 * NB: call btr_rec_at instead of using array index, see btr_record.
	 */
	memcpy(rec_dst, btr_rec_at(tcx, rec_src, 1),
	       rec_size * nd_right->tn_keyn);

	/* backup it because the below btr_node_insert_rec_only may
	 * overwrite it.
	 */
	memcpy(&hkey_buf[0], &rec_src->rec_hkey[0], btr_hkey_size(tcx));

	btr_node_insert_rec_only(tcx, trace, rec);
	memcpy(&rec->rec_hkey[0], &hkey_buf[0], btr_hkey_size(tcx));

 bubble_up:
	D_DEBUG(DF_MISC, "left keyn %d, right keyn %d\n",
		nd_left->tn_keyn, nd_right->tn_keyn);

	rec->rec_mmid = umem_id_t2u(mmid_right);
	if (level == 0)
		rc = btr_root_grow(tcx, mmid_left, rec);
	else
		rc = btr_node_insert_rec(tcx, trace - 1, rec);

	return rc;
}

static int
btr_node_insert_rec(struct btr_context *tcx, struct btr_trace *trace,
		    struct btr_record *rec)
{
	int	rc = 0;

	if (btr_node_is_full(tcx, trace->tr_node))
		rc = btr_node_split_and_insert(tcx, trace, rec);
	else
		btr_node_insert_rec_only(tcx, trace, rec);
	return rc;
}

enum btr_probe_rc {
	/** not found */
	PROBE_RC_NONE,
	/** found the equivalent key */
	PROBE_RC_EQ,
	/** found something but not the provided key */
	PROBE_RC_OK,
};

/**
 * Try to find \a key within a btree, it will store the searching path in
 * tcx::tc_traces.
 *
 * \return	see btr_probe_rc
 */
static enum btr_probe_rc
btr_probe(struct btr_context *tcx, int opc, daos_iov_t *key,
	  daos_hash_out_t *anchor)
{
	struct btr_record	*rec;
	int			 start;
	int			 end;
	int			 at;
	int			 cmp;
	int			 level;
	bool			 nextl;
	char			 hkey_buf[DAOS_HKEY_MAX];
	char			*hkey = &hkey_buf[0];
	TMMID(struct btr_node)	 nd_mmid;

	memset(&tcx->tc_traces[0], 0,
	       sizeof(tcx->tc_traces[0]) * BTR_TRACE_MAX);

	if (btr_root_empty(tcx)) { /* empty tree */
		D_DEBUG(DF_MISC, "Empty tree\n");
		return PROBE_RC_NONE;
	}

	if (opc & BTR_PROBE_EQ) {
		if (key != NULL) {
			btr_hkey_gen(tcx, key, hkey);

		} else {
			D_ASSERT(anchor != NULL);
			D_ASSERT(opc != BTR_PROBE_UPDATE);

			memcpy(hkey, &anchor->body[0], btr_hkey_size(tcx));
		}
	}

	nd_mmid = tcx->tc_tins.ti_root->tr_node;
	start = end = 0;

	for (nextl = true, level = 0 ;;) {
		umem_id_t ummid;

		if (nextl) { /* search a new level of the tree */
			nextl	= false;
			start	= 0;
			end	= btr_mmid2ptr(tcx, nd_mmid)->tn_keyn - 1;

			D_DEBUG(DF_MISC,
				"Probe level %d, node "TMMID_PF" keyn %d\n",
				level, TMMID_P(nd_mmid), end + 1);
		}

		if (opc & BTR_PROBE_EQ) {
			/* binary search */
			at = (start + end) / 2;
			rec = btr_node_rec_at(tcx, nd_mmid, at);
			cmp = btr_hkey_cmp(tcx, rec, hkey);

			D_DEBUG(DF_MISC, "compared record at %d, cmp %d\n",
				at, cmp);

		} else if (opc == BTR_PROBE_FIRST) {
			at = start = end = 0;
			cmp = 1;

		} else {
			D_ASSERT(opc ==  BTR_PROBE_LAST);
			at = start = end;
			cmp = -1;
		}

		if (start < end && cmp != 0) {
			/* continue the binary search in current level */
			if (cmp < 0)
				start = at + 1;
			else
				end = at - 1;
			continue;
		}

		if (btr_node_is_leaf(tcx, nd_mmid))
			break;

		/* NB: cmp <= 0 means search the record in the right child,
		 * otherwise it is in the left child.
		 */
		at += (cmp <= 0);
		btr_trace_set(tcx, level, nd_mmid, at);
		btr_trace_debug(tcx, &tcx->tc_trace[level], "probe child\n");

		/* Search the next level. */
		ummid = btr_node_mmid_at(tcx, nd_mmid, at);
		nd_mmid = umem_id_u2t(ummid, struct btr_node);
		nextl = true;
		level++;
	}
	/* leaf node */
	D_ASSERT(level == tcx->tc_depth - 1);
	D_ASSERT(!TMMID_IS_NULL(nd_mmid));

	if (cmp == 0 && key != NULL) {
		rec = btr_node_rec_at(tcx, nd_mmid, at);
		cmp = btr_key_cmp(tcx, rec, key);
		if (cmp != 0)
			D_ERROR("Hash collision is not well handled\n");
	}

	if (opc == BTR_PROBE_UPDATE)
		at += (cmp < 0);

	btr_trace_set(tcx, level, nd_mmid, at);
	btr_trace_debug(tcx, &tcx->tc_trace[level], "probe finished\n");

	switch (opc) {
	default:
		D_ASSERT(0);
	case BTR_PROBE_FIRST:
	case BTR_PROBE_LAST:
		return PROBE_RC_OK;

	case BTR_PROBE_UPDATE:
	case BTR_PROBE_EQ:
		return cmp == 0 ? PROBE_RC_EQ : PROBE_RC_NONE;

	case BTR_PROBE_GE:
		if (cmp == 0)
			return PROBE_RC_EQ;
		if (cmp > 0)
			return PROBE_RC_OK;

		return btr_probe_next(tcx) ? PROBE_RC_OK : PROBE_RC_NONE;

	case BTR_PROBE_LE:
		if (cmp == 0)
			return PROBE_RC_EQ;
		if (cmp < 0)
			return PROBE_RC_OK;

		return btr_probe_prev(tcx) ? PROBE_RC_OK : PROBE_RC_NONE;
	}
}

static bool
btr_probe_next(struct btr_context *tcx)
{
	struct btr_trace	*trace;
	struct btr_node		*nd;
	TMMID(struct btr_node)	 nd_mmid;

	if (btr_root_empty(tcx)) /* empty tree */
		return false;

	trace = &tcx->tc_trace[tcx->tc_depth - 1];

	btr_trace_debug(tcx, trace, "Probe the next\n");
	while (1) {
		bool leaf;

		nd_mmid = trace->tr_node;
		leaf = btr_node_is_leaf(tcx, nd_mmid);

		nd = btr_mmid2ptr(tcx, nd_mmid);

		if (btr_node_is_root(tcx, nd_mmid) &&
		    trace->tr_at == nd->tn_keyn - leaf) {
			D_ASSERT(trace == tcx->tc_trace);
			D_DEBUG(DF_MISC, "End\n");
			return false; /* done */
		}

		if (trace->tr_at == nd->tn_keyn - leaf) {
			/* finish current level */
			trace--;
			continue;
		}

		trace->tr_at++;
		btr_trace_debug(tcx, trace, "trace back\n");
		break;
	}

	while (trace < &tcx->tc_trace[tcx->tc_depth - 1]) {
		umem_id_t ummid;

		ummid = btr_node_mmid_at(tcx, trace->tr_node, trace->tr_at);
		trace++;
		trace->tr_at = 0;
		trace->tr_node = umem_id_u2t(ummid, struct btr_node);
	}

	btr_trace_debug(tcx, trace, "is the next\n");
	return true;
}

static bool
btr_probe_prev(struct btr_context *tcx)
{
	struct btr_trace	*trace;
	struct btr_node		*nd;
	TMMID(struct btr_node)	 nd_mmid;

	if (btr_root_empty(tcx)) /* empty tree */
		return false;

	trace = &tcx->tc_trace[tcx->tc_depth - 1];

	btr_trace_debug(tcx, trace, "Probe the prev\n");
	while (1) {
		nd_mmid = trace->tr_node;

		nd = btr_mmid2ptr(tcx, nd_mmid);

		if (btr_node_is_root(tcx, nd_mmid) && trace->tr_at == 0) {
			D_ASSERT(trace == tcx->tc_trace);
			D_DEBUG(DF_MISC, "End\n");
			return false; /* done */
		}

		if (trace->tr_at == 0) {
			/* finish current level */
			trace--;
			continue;
		}

		trace->tr_at--;
		btr_trace_debug(tcx, trace, "trace back\n");
		break;
	}

	while (trace < &tcx->tc_trace[tcx->tc_depth - 1]) {
		umem_id_t ummid;
		bool	  leaf;

		ummid = btr_node_mmid_at(tcx, trace->tr_node, trace->tr_at);

		trace++;
		trace->tr_node = umem_id_u2t(ummid, struct btr_node);
		leaf = btr_node_is_leaf(tcx, trace->tr_node);

		nd = btr_mmid2ptr(tcx, trace->tr_node);

		D_ASSERT(nd->tn_keyn != 0);
		trace->tr_at = nd->tn_keyn - leaf;
	}

	btr_trace_debug(tcx, trace, "is the prev\n");
	return true;
}

/**
 * Search the provided \a key and fetch its value (and key if the matched key
 * is different with the input key). This function can support advanced range
 * search operation based on \a opc.
 *
 * If \a key_out and \a val_out provide sink buffers, then key and value will
 * be copied into them. Otherwise if buffer address in \a key_out or/and
 * \a val_out is/are NULL, then addresses of key or/and value of the current
 * record will be returned.
 *
 * \param toh	[IN]		Tree open handle.
 * \param opc	[IN]		Probe opcode, see dbtree_probe_opc_t for the
 *				details.
 * \param key	[IN]		Key to search
 * \param key_out [OUT]		Return the actual matched key if \a opc is
 *				not BTR_PROBE_EQ.
 * \param val_out [OUT]		Returned value address, or sink buffer to
 *				store returned value.
 *
 * \return		0	found
 *			-ve	error code
 */
int
dbtree_fetch(daos_handle_t toh, dbtree_probe_opc_t opc, daos_iov_t *key,
	     daos_iov_t *key_out, daos_iov_t *val_out)
{
	struct btr_record  *rec;
	struct btr_context *tcx;
	int		    rc;

	if (!btr_probe_is_public(opc))
		return -DER_INVAL;

	tcx = btr_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	rc = btr_probe(tcx, opc, key, NULL);
	if (rc == PROBE_RC_NONE) {
		D_DEBUG(DF_MISC, "Cannot find key\n");
		return -DER_NONEXIST;
	}

	rec = btr_trace2rec(tcx, tcx->tc_depth - 1);
	if (rc == PROBE_RC_EQ)
		key_out = NULL; /* don't need to return the same key */

	return btr_rec_fetch(tcx, rec, key_out, val_out);
}

/**
 * Search the provided \a key and return its value to \a val_out.
 * If \a val_out provides sink buffer, then this function will copy record
 * value into the buffer, otherwise it only returns address of value of the
 * current record.
 *
 * \param toh		[IN]	Tree open handle.
 * \param key		[IN]	Key to search.
 * \param val		[OUT]	Returned value address, or sink buffer to
 *				store returned value.
 *
 * \return		0	found
 *			-ve	error code
 */
int
dbtree_lookup(daos_handle_t toh, daos_iov_t *key, daos_iov_t *val_out)
{
	return dbtree_fetch(toh, BTR_PROBE_EQ, key, NULL, val_out);
}

static int
btr_update_only(struct btr_context *tcx, daos_iov_t *key, daos_iov_t *val)
{
	struct btr_record *rec;
	int		   rc;
	char		   sbuf[BTR_PRINT_BUF];

	rec = btr_trace2rec(tcx, tcx->tc_depth - 1);

	D_DEBUG(DF_MISC, "Update record %s\n",
		btr_rec_string(tcx, rec, true, sbuf, BTR_PRINT_BUF));

	rc = btr_rec_update(tcx, rec, key, val);
	if (rc != 0) { /* failed */
		D_DEBUG(DF_MISC, "Failed to update record: %d\n", rc);
		return rc;
	}
	return 0;
}

/**
 * create a new record, insert it into tree leaf node.
 */
static int
btr_insert(struct btr_context *tcx, daos_iov_t *key, daos_iov_t *val)
{
	struct btr_record *rec;
	char		  *rec_str;
	char		   str[BTR_PRINT_BUF];
	union btr_rec_buf  rec_buf;
	int		   rc;

	rec = &rec_buf.rb_rec;
	btr_hkey_gen(tcx, key, &rec->rec_hkey[0]);

	rc = btr_rec_alloc(tcx, key, val, rec);
	if (rc != 0) {
		D_DEBUG(DF_MISC, "Failed to create new record: %d\n", rc);
		return rc;
	}

	rec_str = btr_rec_string(tcx, rec, true, str, BTR_PRINT_BUF);

	if (tcx->tc_depth != 0) {
		struct btr_trace *trace;

		/* trace for the leaf */
		trace = &tcx->tc_trace[tcx->tc_depth - 1];
		btr_trace_debug(tcx, trace, "try to insert\n");

		if (btr_has_tx(tcx))
			btr_node_tx_add(tcx, trace->tr_node);

		rc = btr_node_insert_rec(tcx, trace, rec);
		if (rc != 0) {
			D_DEBUG(DF_MISC,
				"Failed to insert record to leaf: %d\n", rc);
			goto failed;
		}

	} else {
		/* empty tree */
		D_DEBUG(DF_MISC, "Add record %s to an empty tree\n", rec_str);

		rc = btr_root_start(tcx, rec);
		if (rc != 0) {
			D_DEBUG(DF_MISC, "Failed to start the tree: %d\n", rc);
			goto failed;
		}
	}
	return 0;
 failed:
	btr_rec_free(tcx, rec);
	return rc;
}

static int
btr_update(struct btr_context *tcx, daos_iov_t *key, daos_iov_t *val)
{
	int	rc;

	rc = btr_probe(tcx, BTR_PROBE_UPDATE, key, NULL);
	if (rc == PROBE_RC_EQ) {
		rc = btr_update_only(tcx, key, val);
	} else {
		D_ASSERT(rc == PROBE_RC_NONE);
		rc = btr_insert(tcx, key, val);
	}
	return rc;
}

static int
btr_tx_update(struct btr_context *tcx, daos_iov_t *key, daos_iov_t *val)
{
#if DAOS_HAS_NVML
	struct umem_instance *umm = btr_umm(tcx);
	int		      rc = 0;

	TX_BEGIN(umm->umm_u.pmem_pool) {
		rc = btr_update(tcx, key, val);
		if (rc != 0)
			umem_tx_abort(btr_umm(tcx), rc);
	} TX_ONABORT {
		D_DEBUG(DF_MISC, "dbtree_udpate failed: %d", rc);

	} TX_FINALLY {
		D_DEBUG(DF_MISC, "dbtree_update tx exited\n");
	} TX_END

	return rc;
#else
	D_ASSERT(0);
	return -DER_NO_PERM;
#endif
}

/**
 * Update value of the provided key.
 *
 * \param tcx		[IN]	Tree operation context.
 * \param key		[IN]	Key to search.
 * \param val		[IN]	New value for the key, it will punch the
 *				original value if \val is NULL.
 *
 * \return		0	success
 *			-ve	error code
 */
int
dbtree_update(daos_handle_t toh, daos_iov_t *key, daos_iov_t *val)
{
	struct btr_context *tcx;
	int		    rc = 0;

	tcx = btr_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	if (btr_has_tx(tcx))
		rc = btr_tx_update(tcx, key, val);
	else
		rc = btr_update(tcx, key, val);

	return rc;
}

int
dbtree_delete(daos_handle_t toh, daos_iov_t *key)
{
	/* XXX TODO */
	D_ASSERT(0);
	return -DER_NO_PERM;
}

static int
btr_tree_alloc(struct btr_context *tcx)
{
	int	rc;

	rc = btr_root_alloc(tcx);
	D_DEBUG(DF_MISC, "Allocate tree root: %d\n", rc);

	return rc;
}

static int
btr_tx_tree_alloc(struct btr_context *tcx)
{
#if DAOS_HAS_NVML
	struct umem_instance *umm = btr_umm(tcx);
	int		      rc = 0;

	TX_BEGIN(umm->umm_u.pmem_pool) {
		rc = btr_tree_alloc(tcx);
		if (rc != 0)
			umem_tx_abort(btr_umm(tcx), rc);
	} TX_ONABORT {
		D_DEBUG(DF_MISC, "Failed to create tree root: %d\n", rc);

	} TX_FINALLY {
		D_DEBUG(DF_MISC, "dbtree_create tx exited\n");
	} TX_END

	return rc;
#else
	D_ASSERT(0);
	return -DER_NO_PERM;
#endif
}

/**
 * Create an empty tree.
 *
 * \param tree_class	[IN]	Class ID of the tree.
 * \param tree_feats	[IN]	Feature bits of the tree.
 * \param tree_order	[IN]	Btree order, value >= 3.
 * \param uma		[IN]	Memory class attributes.
 * \param root_mmidp	[OUT]	Returned root MMID.
 * \param toh		[OUT]	Returned tree open handle.
 */
int
dbtree_create(unsigned int tree_class, uint64_t tree_feats,
	      unsigned int tree_order, struct umem_attr *uma,
	      TMMID(struct btr_root) *root_mmidp, daos_handle_t *toh)
{
	struct btr_context *tcx;
	int		    rc;

	if (tree_order < BTR_ORDER_MIN || tree_order > BTR_ORDER_MAX) {
		D_DEBUG(DF_MISC, "Order (%d) should be between %d and %d\n",
			tree_order, BTR_ORDER_MIN, BTR_ORDER_MAX);
		return -DER_INVAL;
	}

	rc = btr_context_create(BTR_ROOT_NULL, NULL, tree_class, tree_feats,
				tree_order, uma, &tcx);
	if (rc != 0)
		return rc;

	if (btr_has_tx(tcx))
		rc = btr_tx_tree_alloc(tcx);
	else
		rc = btr_tree_alloc(tcx);

	if (rc != 0)
		goto failed;

	*root_mmidp = tcx->tc_tins.ti_root_mmid;
	*toh = btr_tcx2hdl(tcx);
	return 0;
 failed:
	btr_context_decref(tcx);
	return rc;
}

static int
btr_tree_init(struct btr_context *tcx, struct btr_root *root)
{
	memset(root, 0, sizeof(*root));
	return btr_root_init(tcx, root);
}

static int
btr_tx_tree_init(struct btr_context *tcx, struct btr_root *root)
{
#if DAOS_HAS_NVML
	struct umem_instance *umm = btr_umm(tcx);
	int		      rc = 0;

	TX_BEGIN(umm->umm_u.pmem_pool) {
		rc = btr_tree_init(tcx, root);
		if (rc != 0)
			umem_tx_abort(btr_umm(tcx), rc);
	} TX_ONABORT {
		D_DEBUG(DF_MISC, "Failed to init tree root: %d\n", rc);

	} TX_FINALLY {
		D_DEBUG(DF_MISC, "dbtree_create_inplace tx exited\n");
	} TX_END

	return rc;
#else
	D_ASSERT(0);
	return -DER_NO_PERM;
#endif
}

int
dbtree_create_inplace(unsigned int tree_class, uint64_t tree_feats,
		      unsigned int tree_order, struct umem_attr *uma,
		      struct btr_root *root, daos_handle_t *toh)
{
	struct btr_context *tcx;
	int		    rc;

	if (tree_order < BTR_ORDER_MIN || tree_order > BTR_ORDER_MAX) {
		D_DEBUG(DF_MISC, "Order (%d) should be between %d and %d\n",
			tree_order, BTR_ORDER_MIN, BTR_ORDER_MAX);
		return -DER_INVAL;
	}

	if (root->tr_class != 0) {
		D_DEBUG(DF_MISC,
			"Tree existed, c=%d, o=%d, d=%d, f="DF_U64"\n",
			root->tr_class, root->tr_order, root->tr_depth,
			root->tr_feats);
		return -DER_NO_PERM;
	}

	rc = btr_context_create(BTR_ROOT_NULL, root, tree_class, tree_feats,
				tree_order, uma, &tcx);
	if (rc != 0)
		return rc;

	if (btr_has_tx(tcx))
		rc = btr_tx_tree_init(tcx, root);
	else
		rc = btr_tree_init(tcx, root);

	if (rc != 0)
		goto failed;

	*toh = btr_tcx2hdl(tcx);
	return 0;
 failed:
	btr_context_decref(tcx);
	return rc;
}

/**
 * Open a btree.
 *
 * \param root_mmid	[IN]	MMID of the tree root.
 * \param uma		[IN]	Memory class attributes.
 * \param toh		[OUT]	Returned tree open handle.
 */
int
dbtree_open(TMMID(struct btr_root) root_mmid, struct umem_attr *uma,
	    daos_handle_t *toh)
{
	struct btr_context *tcx;
	int		    rc;

	rc = btr_context_create(root_mmid, NULL, -1, -1, -1, uma, &tcx);
	if (rc != 0)
		return rc;

	*toh = btr_tcx2hdl(tcx);
	return 0;
}

/**
 * Open a btree from the root address.
 *
 * \param root		[IN]	Address of the tree root.
 * \param uma		[IN]	Memory class attributes.
 * \param toh		[OUT]	Returned tree open handle.
 */
int
dbtree_open_inplace(struct btr_root *root, struct umem_attr *uma,
		    daos_handle_t *toh)
{
	struct btr_context *tcx;
	int		    rc;

	if (root->tr_class == 0) {
		D_DEBUG(DF_MISC, "Tree class is zero\n");
		return -DER_INVAL;
	}

	rc = btr_context_create(BTR_ROOT_NULL, root, -1, -1, -1, uma, &tcx);
	if (rc != 0)
		return rc;

	*toh = btr_tcx2hdl(tcx);
	return 0;
}

/**
 * Close an opened tree.
 *
 * \param toh	[IN]	Tree open handle.
 */
int
dbtree_close(daos_handle_t toh)
{
	struct btr_context *tcx;

	tcx = btr_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	btr_context_decref(tcx);
	return 0;
}

/** Destroy a tree node and all its children recursively. */
static void
btr_node_destroy(struct btr_context *tcx, TMMID(struct btr_node) nd_mmid)
{
	struct btr_node *nd	= btr_mmid2ptr(tcx, nd_mmid);
	bool		 leaf	= btr_node_is_leaf(tcx, nd_mmid);
	int		 i;

	/* NB: don't need to call TX_ADD_RANGE(nd_mmid, ...) because I never
	 * change it so nothing to undo on transaction failure, I may destroy
	 * it later by calling TX_FREE which is transactional safe.
	 */
	D_DEBUG(DF_MISC, "Destroy tree %s "TMMID_PF", keyn %d\n",
		leaf ? "leaf" : "node", TMMID_P(nd_mmid), nd->tn_keyn);

	if (leaf) {
		for (i = 0; i < nd->tn_keyn; i++) {
			struct btr_record *rec;

			rec = btr_node_rec_at(tcx, nd_mmid, i);
			btr_rec_free(tcx, rec);
		}
		return;
	}

	for (i = 0; i <= nd->tn_keyn; i++) {
		umem_id_t ummid;

		ummid = btr_node_mmid_at(tcx, nd_mmid, i);
		btr_node_destroy(tcx, umem_id_u2t(ummid, struct btr_node));
	}
	btr_node_free(tcx, nd_mmid);
}

/** destroy all tree nodes and records, then release the root */
static int
btr_tree_destroy(struct btr_context *tcx)
{
	struct btr_root *root;

	D_DEBUG(DF_MISC, "Destroy "TMMID_PF", order %d\n",
		TMMID_P(tcx->tc_tins.ti_root_mmid), tcx->tc_order);

	root = tcx->tc_tins.ti_root;
	if (!TMMID_IS_NULL(root->tr_node)) {
		/* destroy the root and all descendants */
		btr_node_destroy(tcx, root->tr_node);
	}

	btr_root_free(tcx);
	return 0;
}

static int
btr_tx_tree_destroy(struct btr_context *tcx)
{
#if DAOS_HAS_NVML
	struct umem_instance *umm = btr_umm(tcx);
	int		      rc = 0;

	TX_BEGIN(umm->umm_u.pmem_pool) {
		rc = btr_tree_destroy(tcx);
		if (rc != 0)
			umem_tx_abort(btr_umm(tcx), rc);
	} TX_ONABORT {
		D_DEBUG(DF_MISC, "Failed to destroy the tree: %d\n", rc);

	} TX_FINALLY {
		D_DEBUG(DF_MISC, "dbtree_destroy tx exited\n");
	} TX_END

	return rc;
#else
	D_ASSERT(0);
	return -DER_NO_PERM;
#endif
}

/**
 * Destroy a btree.
 * The tree open handle is invalid after the destroy.
 *
 * \param toh	[IN]	Tree open handle.
 */
int
dbtree_destroy(daos_handle_t toh)
{
	struct btr_context *tcx;
	int		    rc;

	tcx = btr_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	if (btr_has_tx(tcx))
		rc = btr_tx_tree_destroy(tcx);
	else
		rc = btr_tree_destroy(tcx);

	btr_context_decref(tcx);
	return rc;
}

/**** Iterator APIs *********************************************************/

static bool
btr_probe_is_public(dbtree_probe_opc_t opc)
{
	return opc == BTR_PROBE_FIRST ||
	       opc == BTR_PROBE_LAST ||
	       opc == BTR_PROBE_EQ ||
	       opc == BTR_PROBE_GE ||
	       opc == BTR_PROBE_LE;
}

/**
 * Initialise iterator.
 *
 * \param toh		[IN]	Tree open handle
 * \param options	[IN]	Options for the iterator.
 *				BTR_ITER_EMBEDDED:
 *				if this bit is set, then this function will
 *				return the iterator embedded in the tree open
 *				handle. It will reduce memory consumption,
 *				but state of iterator could be overwritten
 *				by any other tree operation.
 *
 * \param ih		[OUT]	Returned iterator handle.
 */
int
dbtree_iter_prepare(daos_handle_t toh, unsigned int options, daos_handle_t *ih)
{
	struct btr_iterator *itr;
	struct btr_context  *tcx;
	int		     rc;

	tcx = btr_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	if (options & BTR_ITER_EMBEDDED) {
		/* use the iterator embedded in btr_context */
		if (tcx->tc_ref != 1) { /* don't screw up others */
			D_DEBUG(DF_MISC,
				"The embedded iterator is in using\n");
			return -DER_BUSY;
		}

		itr = &tcx->tc_itr;
		D_ASSERT(itr->it_state == BTR_ITR_NONE);

		btr_context_addref(tcx);
		*ih = toh;

	} else { /* create a private iterator */
		rc = btr_context_clone(tcx, &tcx);
		if (rc != 0)
			return rc;

		itr = &tcx->tc_itr;
		*ih = btr_tcx2hdl(tcx);
	}

	itr->it_state = BTR_ITR_INIT;
	return 0;
}

/**
 * Finalise iterator.
 */
int
dbtree_iter_finish(daos_handle_t ih)
{
	struct btr_context  *tcx;
	struct btr_iterator *itr;

	tcx = btr_hdl2tcx(ih);
	if (tcx == NULL)
		return -DER_NO_HDL;

	itr = &tcx->tc_itr;
	itr->it_state = BTR_ITR_NONE;

	btr_context_decref(tcx);
	return 0;
}

/**
 * Based on the \a opc, this function can do various things:
 * - set the cursor of the iterator to the first or the last record.
 * - find the record for the provided key.
 * - find the first record whose key is greater than or equal to the key.
 * - find the first record whose key is less than or equal to the key.
 *
 * This function must be called after dbtree_iter_prepare, it can be called
 * for arbitrary times for the same iterator.
 *
 * \param opc	[IN]	Probe opcode, see dbtree_probe_opc_t for the details.
 * \param key	[IN]	The key to probe, it will be ignored if opc is
 *			BTR_PROBE_FIRST or BTR_PROBE_LAST.
 * \param anchor [IN]	the anchor point to probe, it will be ignored if
 *			\a key is provided.
 */
int
dbtree_iter_probe(daos_handle_t ih, dbtree_probe_opc_t opc,
		  daos_iov_t *key, daos_hash_out_t *anchor)
{
	struct btr_iterator *itr;
	struct btr_context  *tcx;
	int		     rc;

	D_DEBUG(DF_MISC, "probe(%d) key or anchor\n", opc);

	if (!btr_probe_is_public(opc))
		return -DER_INVAL;

	tcx = btr_hdl2tcx(ih);
	if (tcx == NULL)
		return -DER_NO_HDL;

	itr = &tcx->tc_itr;
	if (itr->it_state < BTR_ITR_INIT)
		return -DER_NO_HDL;

	rc = btr_probe(tcx, opc, key, anchor);
	if (rc == PROBE_RC_NONE) {
		itr->it_state = BTR_ITR_FINI;
		return -DER_NONEXIST;
	}

	itr->it_state = BTR_ITR_READY;
	return 0;
}

static int
btr_iter_move(daos_handle_t ih, bool forward)
{
	struct btr_context  *tcx;
	struct btr_iterator *itr;
	bool		     found;

	tcx = btr_hdl2tcx(ih);
	if (tcx == NULL)
		return -DER_NO_HDL;

	itr = &tcx->tc_itr;
	switch (itr->it_state) {
	default:
		D_ASSERT(0);
	case BTR_ITR_NONE:
	case BTR_ITR_INIT:
		return -DER_NO_PERM;
	case BTR_ITR_READY:
		break;
	case BTR_ITR_FINI:
		return -DER_NONEXIST;
	}

	found = forward ? btr_probe_next(tcx) : btr_probe_prev(tcx);
	if (!found) {
		itr->it_state = BTR_ITR_FINI;
		return -DER_NONEXIST;
	}

	itr->it_state = BTR_ITR_READY;
	return 0;
}

int
dbtree_iter_next(daos_handle_t ih)
{
	return btr_iter_move(ih, true);
}

int
dbtree_iter_prev(daos_handle_t ih)
{
	return btr_iter_move(ih, false);
}

/**
 * Fetch the key and value of current record, if \a key and \a val provide
 * sink buffers, then key and value will be copied into them. If buffer
 * address in \a key or/and \a val is/are NULL, then this function only
 * returns addresses of key or/and value of the current record.
 *
 * \parma ih	[IN]	Iterator open handle.
 * \param key	[OUT]	Sink buffer for the returned key, the key address is
 *			returned if buffer address is NULL.
 * \param val	[OUT]	Sink buffer for the returned value, the value address
 *			is returned if buffer address is NULL.
 * \param anchor [OUT]	Returned hash anchor.
 */
int
dbtree_iter_fetch(daos_handle_t ih, daos_iov_t *key,
		  daos_iov_t *val, daos_hash_out_t *anchor)
{
	struct btr_iterator *itr;
	struct btr_context  *tcx;
	struct btr_record   *rec;
	int		     rc;

	D_DEBUG(DF_MISC, "Current iterator\n");

	tcx = btr_hdl2tcx(ih);
	if (tcx == NULL)
		return -DER_NO_HDL;

	itr = &tcx->tc_itr;
	switch (itr->it_state) {
	default:
		D_ASSERT(0);
	case BTR_ITR_NONE:
	case BTR_ITR_INIT:
		return -DER_NO_PERM;
	case BTR_ITR_READY:
		break;
	case BTR_ITR_FINI:
		return -DER_NONEXIST;
	}

	rec = btr_trace2rec(tcx, tcx->tc_depth - 1);
	if (rec == NULL)
		return -DER_AGAIN; /* invalid cursor */

	rc = btr_rec_fetch(tcx, rec, key, val);
	if (rc == 0 && anchor != NULL) {
		memset(anchor, 0, sizeof(*anchor));
		memcpy(&anchor->body[0], &rec->rec_hkey[0],
		       btr_hkey_size(tcx));
	}
	return 0;
}

#define BTR_TYPE_MAX	1024

static struct btr_class btr_class_registered[BTR_TYPE_MAX];

/**
 * Intialise a tree instance from a registerd tree class.
 */
static int
btr_class_init(TMMID(struct btr_root) root_mmid, struct btr_root *root,
	       unsigned int tree_class, uint64_t tree_feats,
	       struct umem_attr *uma, struct btr_instance *tins)
{
	struct btr_class *tc;
	int		  rc;

	memset(tins, 0, sizeof(*tins));
	rc = umem_class_init(uma, &tins->ti_umm);
	if (rc != 0)
		return rc;

	if (!TMMID_IS_NULL(root_mmid)) {
		tins->ti_root_mmid = root_mmid;
		if (root == NULL)
			root = umem_id2ptr_typed(&tins->ti_umm, root_mmid);
	}
	tins->ti_root = root;

	if (root != NULL && root->tr_class != 0) {
		tree_class = root->tr_class;
		tree_feats = root->tr_feats;
	}

	/* XXX should be multi-thread safe */
	if (tree_class >= BTR_TYPE_MAX) {
		D_DEBUG(DF_MISC, "Invalid class id: %d\n", tree_class);
		return -DER_INVAL;
	}

	tc = &btr_class_registered[tree_class];
	if (tc->tc_ops == NULL) {
		D_DEBUG(DF_MISC, "Unregistered class id %d\n", tree_class);
		return -DER_NONEXIST;
	}

	if ((tree_feats & tc->tc_feats) != tree_feats) {
		D_ERROR("Unsupported features "DF_U64"/"DF_U64"\n",
			tree_feats, tc->tc_feats);
		return -DER_PROTO;
	}

	tins->ti_ops = tc->tc_ops;
	return rc;
}

/**
 * Register a new tree class.
 *
 * \param tree_class	[IN]	ID for this class
 * \param tree_feats	[IN]	Feature bits, e.g. hash type
 * \param ops		[IN]	Customized function table
 */
int
dbtree_class_register(unsigned int tree_class, uint64_t tree_feats,
		      btr_ops_t *ops)
{
	if (tree_class >= BTR_TYPE_MAX || tree_class == 0)
		return -DER_INVAL;

	/* XXX should be multi-thread safe */
	if (btr_class_registered[tree_class].tc_ops != NULL)
		return -DER_EXIST;

	/* These are mandatory functions */
	D_ASSERT(ops != NULL);
	D_ASSERT(ops->to_hkey_gen != NULL);
	D_ASSERT(ops->to_hkey_size != NULL);
	D_ASSERT(ops->to_rec_fetch != NULL);
	D_ASSERT(ops->to_rec_update != NULL);
	D_ASSERT(ops->to_rec_alloc != NULL);
	D_ASSERT(ops->to_rec_free != NULL);

	btr_class_registered[tree_class].tc_ops = ops;
	btr_class_registered[tree_class].tc_feats = tree_feats;

	return 0;
}
