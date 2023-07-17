/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * common/btree.c
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#define D_LOGFAC	DD_FAC(tree)

#include <daos_errno.h>
#include <daos/btree.h>
#include <daos/dtx.h>

/**
 * Tree node types.
 * NB: a node can be both root and leaf.
 */
enum btr_node_type {
	BTR_NODE_LEAF		= (1 << 0),
	BTR_NODE_ROOT		= (1 << 1),
};

enum btr_probe_rc {
	PROBE_RC_UNKNOWN,
	PROBE_RC_NONE,
	PROBE_RC_OK,
	PROBE_RC_ERR,
	PROBE_RC_INPROGRESS,
	PROBE_RC_DATA_LOSS,
	PROBE_RC_UNAVAILABLE	= PROBE_RC_UNKNOWN,
};

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
	umem_off_t			tr_node;
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
	/** cached configured tree order */
	uint16_t			 tc_order;
	/** cached tree depth, avoid loading from slow memory */
	uint16_t			 tc_depth;
	/** credits for drain, see dbtree_drain */
	uint32_t                         tc_creds    : 30;
	/**
	 * credits is turned on, \a tcx::tc_creds should be checked
	 * while draining the tree
	 */
	uint32_t                         tc_creds_on : 1;
	/**
	 * returned value of the probe, it should be reset after upsert
	 * or delete because the probe path could have been changed.
	 */
	int				 tc_probe_rc;
	/** refcount, used by iterator */
	int				 tc_ref;
	/** cached tree class, avoid loading from slow memory */
	int				 tc_class;
	/** cached feature bits, avoid loading from slow memory */
	uint64_t			 tc_feats;
	/** trace for the tree root */
	struct btr_trace		*tc_trace;
	/** trace buffer */
	struct btr_trace		 tc_traces[BTR_TRACE_MAX];
};

/** size of print buffer */
#define BTR_PRINT_BUF			128

static int btr_class_init(umem_off_t root_off,
			  struct btr_root *root, unsigned int tree_class,
			  uint64_t *tree_feats, struct umem_attr *uma,
			  daos_handle_t coh, void *priv,
			  struct btr_instance *tins);
static struct btr_record *btr_node_rec_at(struct btr_context *tcx,
					  umem_off_t nd_off,
					  unsigned int at);
static int btr_node_insert_rec(struct btr_context *tcx,
			       struct btr_trace *trace,
			       struct btr_record *rec);
static int btr_node_destroy(struct btr_context *tcx, umem_off_t nd_off,
			    void *args, bool *empty_rc);
static int btr_root_tx_add(struct btr_context *tcx);
static bool btr_probe_prev(struct btr_context *tcx);
static bool btr_probe_next(struct btr_context *tcx);

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

#define BTR_IS_DIRECT_KEY(feats) ((feats) & BTR_FEAT_DIRECT_KEY)

static bool
btr_is_direct_key(struct btr_context *tcx)
{
	return BTR_IS_DIRECT_KEY(tcx->tc_feats);
}

#define BTR_IS_UINT_KEY(feats) ((feats) & BTR_FEAT_UINT_KEY)

static bool
btr_is_int_key(struct btr_context *tcx)
{
	return BTR_IS_UINT_KEY(tcx->tc_feats);
}

static bool
btr_has_collision(struct btr_context *tcx)
{
	return !btr_is_direct_key(tcx) && !btr_is_int_key(tcx);
}

#define btr_off2ptr(tcx, off)			\
	umem_off2ptr(btr_umm(tcx), off)

#define BTR_NODE_NULL	UMOFF_NULL
#define BTR_ROOT_NULL	UMOFF_NULL

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

struct umem_instance *
btr_hdl2umm(daos_handle_t toh)
{
	struct btr_context *tcx = btr_hdl2tcx(toh);

	return tcx != NULL ? &tcx->tc_tins.ti_umm : NULL;
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
		D_FREE(tcx);
}

static void
btr_context_set_depth(struct btr_context *tcx, unsigned int depth)
{
	tcx->tc_depth = depth;
	tcx->tc_trace = &tcx->tc_traces[BTR_TRACE_MAX - depth];
}

static inline btr_ops_t *
btr_ops(struct btr_context *tcx)
{
	return tcx->tc_tins.ti_ops;
}

/**
 * Create a btree context (in volatile memory).
 *
 * \param root_off	umem offset of root.
 * \param tree_class	Tree class ID.
 * \param tree_feats	Tree features (the same tree class may have different
 *			features for different library versions).
 * \param tree_order	Tree order.
 * \param uma		Memory class attributes.
 * \param coh		The container open handle.
 * \param priv		Private information from user
 * \param tcxp		Returned context.
 */
static int
btr_context_create(umem_off_t root_off, struct btr_root *root,
		   unsigned int tree_class, uint64_t tree_feats,
		   unsigned int tree_order, struct umem_attr *uma,
		   daos_handle_t coh, void *priv, struct btr_context **tcxp)
{
	struct btr_context	*tcx;
	unsigned int		 depth;
	int			 rc;

	D_ALLOC_PTR(tcx);
	if (tcx == NULL)
		return -DER_NOMEM;

	tcx->tc_ref = 1; /* for the caller */
	rc = btr_class_init(root_off, root, tree_class, &tree_feats, uma,
			    coh, priv, &tcx->tc_tins);
	if (rc != 0) {
		D_ERROR("Failed to setup mem class %d: "DF_RC"\n", uma->uma_id,
			DP_RC(rc));
		D_GOTO(failed, rc);
	}

	root = tcx->tc_tins.ti_root;
	if (root == NULL || root->tr_class == 0) { /* tree creation */
		tcx->tc_class		= tree_class;
		tcx->tc_feats		= tree_feats;
		tcx->tc_order		= tree_order;
		depth			= 0;
		D_DEBUG(DB_TRACE, "Create context for a new tree\n");

	} else {
		tcx->tc_class		= root->tr_class;
		tcx->tc_feats		= root->tr_feats;
		tcx->tc_order		= root->tr_order;
		depth			= root->tr_depth;
		D_DEBUG(DB_TRACE, "Load tree context from "DF_X64"\n",
			root_off);
	}

	btr_context_set_depth(tcx, depth);
	*tcxp = tcx;
	return 0;

 failed:
	D_DEBUG(DB_TRACE, "Failed to create tree context: "DF_RC"\n",
		DP_RC(rc));
	btr_context_decref(tcx);
	return rc;
}

static int
btr_context_clone(struct btr_context *tcx, struct btr_context **tcx_p)
{
	struct umem_attr uma = {0};
	int		 rc;

	umem_attr_get(&tcx->tc_tins.ti_umm, &uma);
	rc = btr_context_create(tcx->tc_tins.ti_root_off,
				tcx->tc_tins.ti_root, -1, -1, -1, &uma,
				tcx->tc_tins.ti_coh,
				tcx->tc_tins.ti_priv, tcx_p);
	return rc;
}

/**
 * Set trace for the specified level, it will increase depth and set trace
 * for the new root if \a level is -1.
 */
static void
btr_trace_set(struct btr_context *tcx, int level,
	      umem_off_t nd_off, int at)
{
	D_ASSERT(at >= 0 && at < tcx->tc_order);
	D_ASSERT(tcx->tc_depth > 0);
	D_ASSERT(level >= 0 && level < tcx->tc_depth);
	D_ASSERT(&tcx->tc_trace[level] < &tcx->tc_traces[BTR_TRACE_MAX]);

	D_DEBUG(DB_TRACE, "trace[%d] "DF_X64"/%d\n", level, nd_off, at);

	D_ASSERT(nd_off != UMOFF_NULL);
	tcx->tc_trace[level].tr_node = nd_off;
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
	D_ASSERT(!UMOFF_IS_NULL(trace->tr_node));

	return btr_node_rec_at(tcx, trace->tr_node, trace->tr_at);
}

#define									\
btr_trace_debug(tcx, trace, format, ...)				\
do {									\
	umem_off_t	__off = (trace)->tr_node;			\
	int		__level = (int)((trace) - (tcx)->tc_trace);	\
									\
	D_DEBUG(DB_TRACE,						\
		"node="DF_X64" (l=%d k=%d at=%d): " format,		\
		__off, __level,						\
		((struct btr_node *)btr_off2ptr((tcx), __off))->tn_keyn,\
		(trace)->tr_at,	## __VA_ARGS__);			\
} while (0)

void
hkey_common_gen(d_iov_t *key_iov, void *hkey)
{
	struct ktr_hkey	*kkey  = (struct ktr_hkey *)hkey;

	if (key_iov->iov_len <= KH_INLINE_MAX) {
		kkey->kh_hash[0] = 0;
		kkey->kh_hash[1] = 0;

		/** Set the lowest bit for inline key */
		kkey->kh_inline_len = (key_iov->iov_len << 2) | 1;
		memcpy(&kkey->kh_inline[0], key_iov->iov_buf, key_iov->iov_len);
		D_ASSERT(kkey->kh_len & 1);
		return;
	}

	kkey->kh_murmur64 = d_hash_murmur64(key_iov->iov_buf, key_iov->iov_len,
					    BTR_MUR_SEED);
	kkey->kh_str32 = d_hash_string_u32(key_iov->iov_buf, key_iov->iov_len);
	/** Lowest bit is clear for hashed key */
	kkey->kh_len = key_iov->iov_len << 2;

	D_ASSERT(!(kkey->kh_inline_len & 1));
}

int
hkey_common_cmp(struct ktr_hkey *k1, struct ktr_hkey *k2)
{
	/** Since the low bit is set for inline keys, there will never be
	 *  a conflict between an inline key and a hashed key so we can
	 *  simply compare as if they are hashed.  Order doesn't matter
	 *  as long as it's consistent.
	 */
	if (k1->kh_hash[0] < k2->kh_hash[0])
		return BTR_CMP_LT;

	if (k1->kh_hash[0] > k2->kh_hash[0])
		return BTR_CMP_GT;

	if (k1->kh_hash[1] < k2->kh_hash[1])
		return BTR_CMP_LT;

	if (k1->kh_hash[1] > k2->kh_hash[1])
		return BTR_CMP_GT;

	return BTR_CMP_EQ;
}

void
hkey_int_gen(d_iov_t *key,  void *hkey)
{
	/* Use key directory as unsigned integer in lieu of hkey */
	D_ASSERT(key->iov_len == sizeof(uint64_t));
	/* NB: This works for little endian architectures.  An
	 * alternative would be explicit casting based on iov_len but
	 * this is a little nicer to read.
	 */
	*(uint64_t *)hkey = 0;
	memcpy(hkey, key->iov_buf, key->iov_len);
}

static inline uint32_t
btr_hkey_size_const(btr_ops_t *ops, uint64_t feats)
{
	uint32_t size;

	if (BTR_IS_DIRECT_KEY(feats))
		return sizeof(umem_off_t);

	if (BTR_IS_UINT_KEY(feats))
		return sizeof(uint64_t);

	size = ops->to_hkey_size();

	D_ASSERT(size <= DAOS_HKEY_MAX);
	return size;
}

/**
 * Wrapper for customized tree functions
 */
static uint32_t
btr_hkey_size(struct btr_context *tcx)
{
	return btr_hkey_size_const(btr_ops(tcx), tcx->tc_feats);
}

static inline int
btr_verify_key(struct btr_context *tcx, d_iov_t *key)
{
	if (btr_is_int_key(tcx) && key->iov_len != sizeof(uint64_t)) {
		D_ERROR("invalid integer key, expected: %lu, got: "DF_U64"\n",
			sizeof(uint64_t), key->iov_len);
		return -DER_INVAL;
	}

	return 0;
}

static void
btr_hkey_gen(struct btr_context *tcx, d_iov_t *key, void *hkey)
{
	if (btr_is_direct_key(tcx)) {
		/* We store umem offset to record when bubbling up */
		return;
	}
	if (btr_is_int_key(tcx)) {
		hkey_int_gen(key, hkey);
		return;
	}
	btr_ops(tcx)->to_hkey_gen(&tcx->tc_tins, key, hkey);
}

static void
btr_hkey_copy(struct btr_context *tcx, char *dst_key, char *src_key)
{
	memcpy(dst_key, src_key, btr_hkey_size(tcx));
}

static int
btr_hkey_cmp(struct btr_context *tcx, struct btr_record *rec, void *hkey)
{
	D_ASSERT(!btr_is_direct_key(tcx));

	if (btr_is_int_key(tcx)) {
		uint64_t a = rec->rec_ukey[0];
		uint64_t b = *(uint64_t *)hkey;

		return (a < b) ? BTR_CMP_LT :
				 ((a > b) ? BTR_CMP_GT : BTR_CMP_EQ);
	}
	if (btr_ops(tcx)->to_hkey_cmp)
		return btr_ops(tcx)->to_hkey_cmp(&tcx->tc_tins, rec, hkey);
	else
		return dbtree_key_cmp_rc(
			memcmp(&rec->rec_hkey[0], hkey, btr_hkey_size(tcx)));
}

static void
btr_key_encode(struct btr_context *tcx, d_iov_t *key, daos_anchor_t *anchor)
{
	D_ASSERT(btr_ops(tcx)->to_key_encode);
	btr_ops(tcx)->to_key_encode(&tcx->tc_tins, key, anchor);
}

static void
btr_key_decode(struct btr_context *tcx, d_iov_t *key, daos_anchor_t *anchor)
{
	D_ASSERT(btr_ops(tcx)->to_key_decode);
	btr_ops(tcx)->to_key_decode(&tcx->tc_tins, key, anchor);
}

static int
btr_key_cmp(struct btr_context *tcx, struct btr_record *rec, d_iov_t *key)
{
	if (btr_ops(tcx)->to_key_cmp)
		return btr_ops(tcx)->to_key_cmp(&tcx->tc_tins, rec, key);
	else
		return BTR_CMP_EQ;
}

static int
btr_rec_alloc(struct btr_context *tcx, d_iov_t *key, d_iov_t *val,
	       struct btr_record *rec, d_iov_t *val_out)
{
	if (btr_is_direct_key(tcx) && (key->iov_len > EMBEDDED_KEY_MAX)) {
		D_ERROR("Key size (%zd) > Anchor size (%u)\n",
			key->iov_len, EMBEDDED_KEY_MAX);
		return -DER_KEY2BIG;
	}

	return btr_ops(tcx)->to_rec_alloc(&tcx->tc_tins, key, val, rec, val_out);
}

static int
btr_rec_free(struct btr_context *tcx, struct btr_record *rec, void *args)
{
	int	rc;

	if (UMOFF_IS_NULL(rec->rec_off))
		return 0;

	rc = btr_ops(tcx)->to_rec_free(&tcx->tc_tins, rec, args);
	if (rc != 0)
		D_ERROR("Failed to free rec: rc = %d\n", rc);

	return rc;
}

/**
 * Fetch key and value of the record, key is optional, both key and value
 * are output parameters
 */
static int
btr_rec_fetch(struct btr_context *tcx, struct btr_record *rec,
	      d_iov_t *key, d_iov_t *val)
{
	return btr_ops(tcx)->to_rec_fetch(&tcx->tc_tins, rec, key, val);
}

static int
btr_rec_update(struct btr_context *tcx, struct btr_record *rec,
	       d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	if (!btr_ops(tcx)->to_rec_update)
		return -DER_NO_PERM;

	return btr_ops(tcx)->to_rec_update(&tcx->tc_tins, rec, key, val, val_out);
}

static int
btr_rec_stat(struct btr_context *tcx, struct btr_record *rec,
	     struct btr_rec_stat *stat)
{
	if (!btr_ops(tcx)->to_rec_stat)
		return -DER_NOSYS;

	return btr_ops(tcx)->to_rec_stat(&tcx->tc_tins, rec, stat);
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

static inline uint32_t
btr_rec_size(struct btr_context *tcx)
{
	return btr_hkey_size(tcx) + sizeof(struct btr_record);
}

static struct btr_record *
btr_rec_at(struct btr_context *tcx, struct btr_record *rec, int at)
{
	char	*buf = (char *)rec;

	buf += at * btr_rec_size(tcx); /* NB: at can be negative */
	return (struct btr_record *)buf;
}

static void
btr_rec_copy(struct btr_context *tcx, struct btr_record *dst_rec,
	     struct btr_record *src_rec, int rec_nr)
{
	memcpy(dst_rec, src_rec, rec_nr * btr_rec_size(tcx));
}

static void
btr_rec_move(struct btr_context *tcx, struct btr_record *dst_rec,
	     struct btr_record *src_rec, int rec_nr)
{
	memmove(dst_rec, src_rec, rec_nr * btr_rec_size(tcx));
}

static void
btr_rec_copy_hkey(struct btr_context *tcx, struct btr_record *dst_rec,
		  struct btr_record *src_rec)
{
	btr_hkey_copy(tcx, &dst_rec->rec_hkey[0], &src_rec->rec_hkey[0]);
}

static inline uint32_t
btr_node_size(struct btr_context *tcx)
{
	return sizeof(struct btr_node) +
		tcx->tc_tins.ti_root->tr_node_size * btr_rec_size(tcx);
}

static int
btr_node_alloc(struct btr_context *tcx, umem_off_t *nd_off_p)
{
	struct btr_node		*nd;
	umem_off_t		 nd_off;

	if (btr_ops(tcx)->to_node_alloc != NULL)
		nd_off = btr_ops(tcx)->to_node_alloc(&tcx->tc_tins,
						     btr_node_size(tcx));
	else
		nd_off = umem_zalloc(btr_umm(tcx), btr_node_size(tcx));

	if (UMOFF_IS_NULL(nd_off))
		return btr_umm(tcx)->umm_nospc_rc;

	D_DEBUG(DB_TRACE, "Allocate new node "DF_X64"\n", nd_off);
	nd = btr_off2ptr(tcx, nd_off);
	nd->tn_child = BTR_NODE_NULL;

	*nd_off_p = nd_off;
	return 0;
}

static int
btr_node_free(struct btr_context *tcx, umem_off_t nd_off)
{
	int	rc;
	D_DEBUG(DB_TRACE, "Free node "DF_X64"\n", nd_off);
	rc = umem_free(btr_umm(tcx), nd_off);
	if (rc != 0)
		D_ERROR("Failed to free node: %s\n", strerror(errno));

	return rc;
}

static int
btr_node_tx_add(struct btr_context *tcx, umem_off_t nd_off)
{
	return umem_tx_add(btr_umm(tcx), nd_off, btr_node_size(tcx));
}

/* helper functions */

static struct btr_record *
btr_node_rec_at(struct btr_context *tcx, umem_off_t nd_off,
		unsigned int at)
{
	struct btr_node *nd = btr_off2ptr(tcx, nd_off);
	char		*addr = (char *)&nd[1];

	return (struct btr_record *)&addr[btr_rec_size(tcx) * at];
}

static umem_off_t
btr_node_child_at(struct btr_context *tcx, umem_off_t nd_off,
		  unsigned int at)
{
	struct btr_node	  *nd = btr_off2ptr(tcx, nd_off);
	struct btr_record *rec;

	D_ASSERT(!(nd->tn_flags & BTR_NODE_LEAF));
	/* NB: non-leaf node has +1 children than number of keys */
	if (at == 0)
		return nd->tn_child;

	rec = btr_node_rec_at(tcx, nd_off, at - 1);
	return rec->rec_off;
}

static inline bool
btr_node_is_full(struct btr_context *tcx, umem_off_t nd_off)
{
	struct btr_node *nd = btr_off2ptr(tcx, nd_off);

	D_ASSERT(nd->tn_keyn < tcx->tc_order);
	return nd->tn_keyn == tcx->tc_order - 1;
}

static inline void
btr_node_set(struct btr_context *tcx, umem_off_t nd_off,
	     unsigned int bits)
{
	struct btr_node *nd = btr_off2ptr(tcx, nd_off);

	nd->tn_flags |= bits;
}

static inline void
btr_node_unset(struct btr_context *tcx, umem_off_t nd_off,
	       unsigned int bits)
{
	struct btr_node *nd = btr_off2ptr(tcx, nd_off);

	nd->tn_flags &= ~bits;
}

static inline bool
btr_node_is_set(struct btr_context *tcx, umem_off_t nd_off,
		unsigned int bits)
{
	struct btr_node *nd = btr_off2ptr(tcx, nd_off);

	return nd->tn_flags & bits;
}

static inline bool
btr_node_is_leaf(struct btr_context *tcx, umem_off_t nd_off)
{
	return btr_node_is_set(tcx, nd_off, BTR_NODE_LEAF);
}

static inline bool
btr_node_is_root(struct btr_context *tcx, umem_off_t nd_off)
{
	return btr_node_is_set(tcx, nd_off, BTR_NODE_ROOT);
}

static inline bool
btr_node_is_equal(struct btr_context *tcx, umem_off_t off1,
		  umem_off_t off2)
{
	return off1 == off2;
}

static bool
btr_root_empty(struct btr_context *tcx)
{
	struct btr_root *root = tcx->tc_tins.ti_root;

	return root == NULL || UMOFF_IS_NULL(root->tr_node);
}

static int
btr_root_free(struct btr_context *tcx)
{
	struct btr_instance	*tins = &tcx->tc_tins;
	int			 rc;

	if (UMOFF_IS_NULL(tins->ti_root_off)) {
		struct btr_root *root = tins->ti_root;

		if (root == NULL)
			return 0;

		D_DEBUG(DB_TRACE, "Destroy inplace created tree root\n");
		if (btr_has_tx(tcx)) {
			rc = btr_root_tx_add(tcx);
			if (rc != 0) {
				D_ERROR("Failed to add root into TX: %s\n",
					strerror(errno));
				return rc;
			}
		}

		memset(root, 0, sizeof(*root));
	} else {
		D_DEBUG(DB_TRACE, "Destroy tree root\n");
		rc = umem_free(btr_umm(tcx), tins->ti_root_off);
		if (rc != 0) {
			D_ERROR("Failed to free tree root: %s\n",
				strerror(errno));
			return rc;
		}
	}

	tins->ti_root_off = BTR_ROOT_NULL;
	tins->ti_root = NULL;

	return 0;
}

static int
btr_root_init(struct btr_context *tcx, struct btr_root *root, bool in_place)
{
	struct btr_instance *tins = &tcx->tc_tins;
	int		     rc;

	tins->ti_root = root;
	if (UMOFF_IS_NULL(tins->ti_root_off) && btr_has_tx(tcx)) {
		/* externally allocated root and has transaction */
		rc = btr_root_tx_add(tcx);
		if (rc != 0)
			return rc;
	}

	if (in_place)
		memset(root, 0, sizeof(*root));
	root->tr_class		= tcx->tc_class;
	root->tr_feats		= tcx->tc_feats;
	root->tr_order		= tcx->tc_order;
	if (tcx->tc_feats & BTR_FEAT_DYNAMIC_ROOT)
		root->tr_node_size	= 1;
	else
		root->tr_node_size	= tcx->tc_order;
	root->tr_node		= BTR_NODE_NULL;

	return 0;
}

static int
btr_root_alloc(struct btr_context *tcx)
{
	struct btr_instance	*tins = &tcx->tc_tins;
	struct btr_root		*root;

	tins->ti_root_off = umem_zalloc(btr_umm(tcx),
					sizeof(struct btr_root));
	if (UMOFF_IS_NULL(tins->ti_root_off))
		return btr_umm(tcx)->umm_nospc_rc;

	root = btr_off2ptr(tcx, tins->ti_root_off);
	return btr_root_init(tcx, root, false);
}

static int
btr_root_tx_add(struct btr_context *tcx)
{
	struct btr_instance	*tins = &tcx->tc_tins;
	int			 rc;

	if (!UMOFF_IS_NULL(tins->ti_root_off)) {
		rc = umem_tx_add(btr_umm(tcx), tcx->tc_tins.ti_root_off,
				 sizeof(struct btr_root));
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
	struct btr_node		*nd;
	umem_off_t		 nd_off;
	int			 rc;

	root = tcx->tc_tins.ti_root;

	D_ASSERT(UMOFF_IS_NULL(root->tr_node));
	D_ASSERT(root->tr_depth == 0);

	rc = btr_node_alloc(tcx, &nd_off);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "Failed to allocate new root\n");
		return rc;
	}

	/* root is also leaf, records are stored in root */
	btr_node_set(tcx, nd_off, BTR_NODE_ROOT | BTR_NODE_LEAF);
	nd = btr_off2ptr(tcx, nd_off);
	nd->tn_keyn = 1;

	rec_dst = btr_node_rec_at(tcx, nd_off, 0);
	btr_rec_copy(tcx, rec_dst, rec, 1);

	if (btr_has_tx(tcx)) {
		rc = btr_root_tx_add(tcx);
		if (rc != 0) {
			D_ERROR("Failed to add root into TX: %s\n",
				strerror(errno));
			return rc;
		}
	}

	root->tr_node = nd_off;
	root->tr_depth = 1;
	btr_context_set_depth(tcx, root->tr_depth);

	btr_trace_set(tcx, 0, nd_off, 0);
	return 0;
}

/**
 * Add a new root to the tree, then insert \a rec to the new root.
 *
 * \param tcx	[IN]	Tree operation context.
 * \param off_left [IN]
 *			the original root, it is left child for the new root.
 * \param rec	[IN]	The record to be inserted to the new root.
 */
int
btr_root_grow(struct btr_context *tcx, umem_off_t off_left,
	      struct btr_record *rec)
{
	struct btr_root		*root;
	struct btr_node		*nd;
	struct btr_record	*rec_dst;
	umem_off_t	 nd_off;
	int			 at;
	int			 rc;

	root = tcx->tc_tins.ti_root;
	D_ASSERT(root->tr_depth != 0);

	D_DEBUG(DB_TRACE, "Grow the tree depth to %d\n", root->tr_depth + 1);

	rc = btr_node_alloc(tcx, &nd_off);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "Failed to allocate new root\n");
		return rc;
	}

	/* the left child is the old root */
	D_ASSERT(btr_node_is_root(tcx, off_left));
	btr_node_unset(tcx, off_left, BTR_NODE_ROOT);

	btr_node_set(tcx, nd_off, BTR_NODE_ROOT);
	rec_dst = btr_node_rec_at(tcx, nd_off, 0);
	btr_rec_copy(tcx, rec_dst, rec, 1);

	nd = btr_off2ptr(tcx, nd_off);
	nd->tn_child	= off_left;
	nd->tn_keyn	= 1;

	at = !btr_node_is_equal(tcx, off_left, tcx->tc_trace->tr_node);

	/* replace the root offset, increase tree level */
	if (btr_has_tx(tcx)) {
		rc = btr_root_tx_add(tcx); /* XXX check error */
		if (rc != 0) {
			D_ERROR("Failed to add root into TX: %s\n",
				strerror(errno));
			return rc;
		}
	}

	root->tr_node = nd_off;
	root->tr_depth++;

	btr_context_set_depth(tcx, root->tr_depth);
	btr_trace_set(tcx, 0, nd_off, at);
	return 0;
}

struct btr_check_alb {
	umem_off_t	nd_off;
	int			at;
	uint32_t		intent;
};

static int
btr_check_availability(struct btr_context *tcx, struct btr_check_alb *alb)
{
	struct btr_record	*rec;
	int			 rc;

	if (btr_ops(tcx)->to_check_availability == NULL)
		return PROBE_RC_OK;

	if (UMOFF_IS_NULL(alb->nd_off)) { /* compare the leaf trace */
		struct btr_trace *trace = &tcx->tc_traces[BTR_TRACE_MAX - 1];

		alb->nd_off = trace->tr_node;
		alb->at = trace->tr_at;
	}

	if (!btr_node_is_leaf(tcx, alb->nd_off))
		return PROBE_RC_OK;

	rec = btr_node_rec_at(tcx, alb->nd_off, alb->at);
	rc = btr_ops(tcx)->to_check_availability(&tcx->tc_tins, rec,
						 alb->intent);
	if (rc == -DER_INPROGRESS) /* Uncertain */
		return PROBE_RC_INPROGRESS;

	if (rc == -DER_DATA_LOSS)
		return PROBE_RC_DATA_LOSS;

	if (rc < 0) /* Failure */
		return PROBE_RC_ERR;

	switch (rc) {
	case ALB_AVAILABLE_DIRTY:
		/* XXX: This case is mainly used for purge operation.
		 *	There are some uncommitted modifications that
		 *	may belong to some old crashed operations. We
		 *	hope that the caller can make further check
		 *	about whether can remove them from the system
		 *	or not.
		 *
		 *	Currently, the main caller with purge intent
		 *	is the aggregation. We need more handling
		 *	for the case in the new aggregation logic.
		 *	But before that, just make it fall through.
		 */
	case ALB_AVAILABLE_ABORTED:
		/** NB: Entry is aborted flag set and we can purge it */
	case ALB_AVAILABLE_CLEAN:
		return PROBE_RC_OK;
	case ALB_UNAVAILABLE:
	default:
		/* Unavailable */
		return PROBE_RC_UNAVAILABLE;
	}
}

static int
btr_node_insert_rec_only(struct btr_context *tcx, struct btr_trace *trace,
			 struct btr_record *rec)
{
	struct btr_record *rec_a;
	struct btr_node   *nd;
	bool		   leaf;
	bool		   reuse = false;
	char		   sbuf[BTR_PRINT_BUF];
	int		   rc;

	/* NB: assume trace->tr_node has been added to TX */
	D_ASSERT(!btr_node_is_full(tcx, trace->tr_node));

	leaf = btr_node_is_leaf(tcx, trace->tr_node);
	btr_trace_debug(tcx, trace, "insert %s now size %d\n",
			btr_rec_string(tcx, rec, leaf, sbuf, BTR_PRINT_BUF),
			btr_rec_size(tcx));

	nd = btr_off2ptr(tcx, trace->tr_node);
	if (nd->tn_keyn > 0) {
		struct btr_check_alb	alb;

		if (trace->tr_at != nd->tn_keyn)
			alb.at = trace->tr_at;
		else
			alb.at = trace->tr_at - 1;

		alb.nd_off = trace->tr_node;
		alb.intent = DAOS_INTENT_CHECK;
		rc = btr_check_availability(tcx, &alb);
		if (rc == PROBE_RC_UNAVAILABLE) {
			reuse = true;
			btr_trace_debug(tcx, trace, "reuse at %d for insert\n",
					alb.at);
			if (trace->tr_at == nd->tn_keyn)
				trace->tr_at -= 1;
		}
	}

	rec_a = btr_node_rec_at(tcx, trace->tr_node, trace->tr_at);

	if (reuse) {
		rc = btr_rec_free(tcx, rec_a, NULL);
		if (rc)
			return rc;
	} else {
		if (trace->tr_at != nd->tn_keyn) {
			struct btr_record *rec_b;

			rec_b = btr_node_rec_at(tcx, trace->tr_node,
						trace->tr_at + 1);
			btr_rec_move(tcx, rec_b, rec_a,
				     nd->tn_keyn - trace->tr_at);
		}
		nd->tn_keyn++;
	}

	btr_rec_copy(tcx, rec_a, rec, 1);
	return 0;
}

/**
 * Where I should split a node.
 */
static int
btr_split_at(struct btr_context *tcx, int level,
	     umem_off_t off_left,
	     umem_off_t off_right)
{
	struct btr_trace *trace = &tcx->tc_trace[level];
	int		  order = tcx->tc_order;
	int		  split_at;
	bool		  left;

	split_at = order / 2;

	left = (trace->tr_at < split_at);
	if (!btr_node_is_leaf(tcx, off_left))
		split_at -= left;

	btr_trace_debug(tcx, trace, "split_at %d, insert to the %s node\n",
			split_at, left ? "left" : "right");
	if (left)
		btr_trace_set(tcx, level, off_left, trace->tr_at);
	else
		btr_trace_set(tcx, level, off_right, trace->tr_at - split_at);

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
	umem_off_t		 off_left;
	umem_off_t		 off_right;
	char			 hkey_buf[DAOS_HKEY_MAX];
	int			 split_at;
	int			 level;
	int			 rc;
	bool			 leaf;
	bool			 right;

	D_ASSERT(trace >= tcx->tc_trace);
	level = trace - tcx->tc_trace;
	off_left = trace->tr_node;

	rc = btr_node_alloc(tcx, &off_right);
	if (rc != 0)
		return rc;

	leaf = btr_node_is_leaf(tcx, off_left);
	if (leaf)
		btr_node_set(tcx, off_right, BTR_NODE_LEAF);

	split_at = btr_split_at(tcx, level, off_left, off_right);

	rec_src = btr_node_rec_at(tcx, off_left, split_at);
	rec_dst = btr_node_rec_at(tcx, off_right, 0);

	nd_left	 = btr_off2ptr(tcx, off_left);
	nd_right = btr_off2ptr(tcx, off_right);

	nd_right->tn_keyn = nd_left->tn_keyn - split_at;
	nd_left->tn_keyn  = split_at;

	if (leaf) {
		D_DEBUG(DB_TRACE, "Splitting leaf node\n");

		btr_rec_copy(tcx, rec_dst, rec_src, nd_right->tn_keyn);
		rc = btr_node_insert_rec_only(tcx, trace, rec);
		if (rc)
			return rc;

		/* insert the right node and the first key of the right
		 * node to its parent
		 */
		if (btr_is_direct_key(tcx))
			rec->rec_node[0] = off_right;
		else
			btr_rec_copy_hkey(tcx, rec, rec_dst);
		goto bubble_up;
	}
	/* non-leaf */

	right = btr_node_is_equal(tcx, trace->tr_node, off_right);
	if (trace->tr_at == 0 && right) {
		/* the new record is the first one on the right node */
		D_DEBUG(DB_TRACE, "Bubble up the new key\n");
		nd_right->tn_child = rec->rec_off;
		btr_rec_copy(tcx, rec_dst, rec_src, nd_right->tn_keyn);
		goto bubble_up;
	}

	D_DEBUG(DB_TRACE, "Bubble up the 1st key of the right node\n");

	nd_right->tn_child = rec_src->rec_off;
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
	btr_rec_copy(tcx, rec_dst, btr_rec_at(tcx, rec_src, 1),
		     nd_right->tn_keyn);

	/* backup it because the below btr_node_insert_rec_only may
	 * overwrite it.
	 */
	btr_hkey_copy(tcx, &hkey_buf[0], &rec_src->rec_hkey[0]);

	rc = btr_node_insert_rec_only(tcx, trace, rec);
	if (rc)
		return rc;

	btr_hkey_copy(tcx, &rec->rec_hkey[0], &hkey_buf[0]);

 bubble_up:
	D_DEBUG(DB_TRACE, "left keyn %d, right keyn %d\n",
		nd_left->tn_keyn, nd_right->tn_keyn);

	rec->rec_off = off_right;
	if (level == 0)
		rc = btr_root_grow(tcx, off_left, rec);
	else
		rc = btr_node_insert_rec(tcx, trace - 1, rec);

	return rc;
}

static inline bool
btr_root_resize_needed(struct btr_context *tcx)
{
	struct btr_root	*root = tcx->tc_tins.ti_root;
	struct btr_node *nd;

	if (tcx->tc_order == root->tr_node_size)
		return false; /* As big as it can get */

	nd = btr_off2ptr(tcx, root->tr_node);

	if (nd->tn_keyn != root->tr_node_size)
		return false;

	return true;
}

static int
btr_root_resize(struct btr_context *tcx, struct btr_trace *trace,
		bool *node_alloc)
{
	struct btr_root	*root = tcx->tc_tins.ti_root;
	umem_off_t	 old_node = root->tr_node;
	struct btr_node	*nd = btr_off2ptr(tcx, old_node);
	daos_size_t	 old_size = btr_node_size(tcx);
	int		 new_order;
	umem_off_t	 nd_off;
	int		 rc = 0;

	D_ASSERT(root->tr_depth == 1);

	if (btr_has_tx(tcx)) {
		rc = btr_root_tx_add(tcx);
		if (rc != 0) {
			D_ERROR("Failed to add btr_root to transaction\n");
			return rc;
		}
	}

	new_order = MIN(root->tr_node_size * 2 + 1, tcx->tc_order);

	D_DEBUG(DB_TRACE, "Root node size increase from %d to %d\n",
		root->tr_node_size, new_order);

	root->tr_node_size = new_order;

	rc = btr_node_alloc(tcx, &nd_off);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "Failed to allocate new root\n");
		return rc;
	}
	trace->tr_node = root->tr_node = nd_off;
	memcpy(btr_off2ptr(tcx, nd_off), nd, old_size);
	/* NB: Both of the following routines can fail but neither presently
	 * returns an error code.   For now, ignore this fact.   DAOS-2577
	 */
	btr_node_free(tcx, old_node);
	*node_alloc = true;

	return 0;
}

static int
btr_node_insert_rec(struct btr_context *tcx, struct btr_trace *trace,
		    struct btr_record *rec)
{
	int	rc = 0;
	bool	node_alloc = false;

	if (btr_root_resize_needed(tcx)) {
		rc = btr_root_resize(tcx, trace, &node_alloc);
		if (rc != 0) {
			D_ERROR("Failed to resize root node: %s\n", d_errstr(rc));
			goto done;
		}
	}

	if (!node_alloc && btr_has_tx(tcx)) {
		rc = btr_node_tx_add(tcx, trace->tr_node);
		if (rc != 0) {
			D_ERROR("Failed to add node to txn record: %s\n", d_errstr(rc));
			goto done;
		}
	}

	if (btr_node_is_full(tcx, trace->tr_node))
		rc = btr_node_split_and_insert(tcx, trace, rec);
	else
		rc = btr_node_insert_rec_only(tcx, trace, rec);
done:
	return rc;
}

static int
btr_cmp(struct btr_context *tcx, umem_off_t nd_off,
	int at, char *hkey, d_iov_t *key)
{
	struct btr_record *rec;
	int		   cmp;

	if (UMOFF_IS_NULL(nd_off)) { /* compare the leaf trace */
		struct btr_trace *trace = &tcx->tc_traces[BTR_TRACE_MAX - 1];

		nd_off = trace->tr_node;
		at = trace->tr_at;
	}

	rec = btr_node_rec_at(tcx, nd_off, at);
	if (btr_is_direct_key(tcx)) {
		/* For direct keys, resolve the offset in the record */
		if (!btr_node_is_leaf(tcx, nd_off))
			rec = btr_node_rec_at(tcx, rec->rec_node[0], 0);

		cmp = btr_key_cmp(tcx, rec, key);
	} else {
		if (hkey) {
			cmp = btr_hkey_cmp(tcx, rec, hkey);
		} else {
			D_ASSERT(key != NULL);
			cmp = btr_key_cmp(tcx, rec, key);
		}
	}
	D_ASSERT((cmp & (BTR_CMP_LT | BTR_CMP_GT)) != 0 ||
		  cmp == BTR_CMP_EQ || cmp == BTR_CMP_ERR);
	D_ASSERT((cmp & (BTR_CMP_LT | BTR_CMP_GT)) !=
		 (BTR_CMP_LT | BTR_CMP_GT));

	D_DEBUG(DB_TRACE, "compared record at %d, cmp %d\n", at, cmp);
	return cmp;
}

bool
btr_probe_valid(dbtree_probe_opc_t opc)
{
	if (opc == BTR_PROBE_FIRST || opc == BTR_PROBE_LAST ||
	    opc == BTR_PROBE_EQ)
		return true;

	return (opc == BTR_PROBE_GT || opc == BTR_PROBE_LT ||
		opc == BTR_PROBE_GE || opc == BTR_PROBE_LE);
}

/**
 * Try to find \a key within a btree, it will store the searching path in
 * tcx::tc_traces.
 *
 * \return	see btr_probe_rc
 */
static enum btr_probe_rc
btr_probe(struct btr_context *tcx, dbtree_probe_opc_t probe_opc,
	  uint32_t intent, d_iov_t *key, char hkey[DAOS_HKEY_MAX])
{
	int			 start;
	int			 end;
	int			 at;
	int			 rc;
	int			 cmp;
	int			 level = -1;
	int			 saved = -1;
	bool			 next_level;
	struct btr_node		*nd;
	struct btr_check_alb	 alb;
	umem_off_t		 nd_off;

	if (!btr_probe_valid(probe_opc)) {
		rc = PROBE_RC_ERR;
		goto out;
	}

	memset(&tcx->tc_traces[0], 0,
	       sizeof(tcx->tc_traces[0]) * BTR_TRACE_MAX);

	/* depth could be changed by dbtree_delete/dbtree_iter_delete from
	 * a different btr_context, so we always reinitialize both depth
	 * and start point of trace for the context.
	 */
	btr_context_set_depth(tcx, tcx->tc_tins.ti_root->tr_depth);

	if (btr_root_empty(tcx)) { /* empty tree */
		D_DEBUG(DB_TRACE, "Empty tree\n");
		rc = PROBE_RC_NONE;
		goto out;
	}

	nd_off = tcx->tc_tins.ti_root->tr_node;

	for (start = end = 0, level = 0, next_level = true ;;) {
		if (next_level) { /* search a new level of the tree */
			next_level = false;
			start	= 0;
			nd	= btr_off2ptr(tcx, nd_off);
			end	= nd->tn_keyn - 1;

			D_DEBUG(DB_TRACE,
				"Probe level %d, node "DF_X64" keyn %d\n",
				level, nd_off, end + 1);
		}

		if (probe_opc == BTR_PROBE_FIRST) {
			at = start = end = 0;
			cmp = BTR_CMP_GT;

		} else if (probe_opc == BTR_PROBE_LAST) {
			at = start = end;
			cmp = BTR_CMP_LT;
		} else {
			D_ASSERT(probe_opc & BTR_PROBE_SPEC);
			/* binary search */
			at = (start + end) / 2;
			cmp = btr_cmp(tcx, nd_off, at, hkey, key);
		}

		if (cmp == BTR_CMP_ERR) {
			D_DEBUG(DB_TRACE, "compared record at %d, got "
				"BTR_CMP_ERR, return PROBE_RC_ERR.", at);
			rc = PROBE_RC_ERR;
			goto out;
		}

		if (cmp != BTR_CMP_EQ && start < end) {
			/* continue the binary search in current level */
			if (cmp & BTR_CMP_LT)
				start = at + 1;
			else
				end = at - 1;
			continue;
		}

		if (btr_node_is_leaf(tcx, nd_off))
			break;

		/* NB: cmp is BTR_CMP_LT or BTR_CMP_EQ means search the record
		 * in the right child, otherwise it is in the left child.
		 */
		at += !(cmp & BTR_CMP_GT);
		btr_trace_set(tcx, level, nd_off, at);
		btr_trace_debug(tcx, &tcx->tc_trace[level], "probe child\n");

		/* Search the next level. */
		nd_off = btr_node_child_at(tcx, nd_off, at);
		next_level = true;
		level++;
	}
	/* leaf node */
	D_ASSERT(cmp != BTR_CMP_UNKNOWN);
	D_ASSERT(level == tcx->tc_depth - 1);
	D_ASSERT(!UMOFF_IS_NULL(nd_off));

	btr_trace_set(tcx, level, nd_off, at);

	if (cmp == BTR_CMP_EQ && key && btr_has_collision(tcx)) {
		cmp = btr_cmp(tcx, nd_off, at, NULL, key);
		if (cmp == BTR_CMP_ERR) {
			rc = PROBE_RC_ERR;
			goto out;
		}
		D_ASSERTF(cmp == BTR_CMP_EQ, "Hash collision is unsupported\n");
	}

	alb.nd_off = nd_off;
	alb.at = at;
	alb.intent = intent;

again:
	switch (probe_opc) {
	default:
		D_ASSERT(0);
	case BTR_PROBE_FIRST:
		do {
			alb.nd_off = tcx->tc_trace[level].tr_node;
			alb.at = tcx->tc_trace[level].tr_at;
			rc = btr_check_availability(tcx, &alb);
		} while (rc == PROBE_RC_UNAVAILABLE && btr_probe_next(tcx));

		if (rc == PROBE_RC_UNAVAILABLE)
			rc = PROBE_RC_NONE;
		goto out;

	case BTR_PROBE_LAST:
		do {
			alb.nd_off = tcx->tc_trace[level].tr_node;
			alb.at = tcx->tc_trace[level].tr_at;
			rc = btr_check_availability(tcx, &alb);
		} while (rc == PROBE_RC_UNAVAILABLE && btr_probe_prev(tcx));

		if (rc == PROBE_RC_UNAVAILABLE)
			rc = PROBE_RC_NONE;
		goto out;

	case BTR_PROBE_EQ:
		if (cmp == BTR_CMP_EQ) {
			rc = btr_check_availability(tcx, &alb);
			if (rc != PROBE_RC_UNAVAILABLE)
				goto out;

			/* The record for current pos is unavailable, can be
			 * reused for the follow-on insert if applicable.
			 */
		} else {
			/* Point at the first key which is larger than the
			 * probed one, this if for the follow-on insert if
			 * applicable.
			 */
			btr_trace_set(tcx, level, nd_off,
				      at + !(cmp & BTR_CMP_GT));
		}

		rc = PROBE_RC_NONE;
		goto out;

	case BTR_PROBE_GE:
		if (cmp == BTR_CMP_EQ) {
			rc = btr_check_availability(tcx, &alb);
			if (rc != PROBE_RC_UNAVAILABLE)
				goto out;

			/* The record for current pos is unavailable, can be
			 * reused for the follow-on insert if applicable.
			 */
			if (saved == -1)
				saved = at;
		}
		/* fall through */
	case BTR_PROBE_GT:
		if (cmp & BTR_CMP_GT) {
			/* Check availability if the target matched or it is
			 * for non-modification related operation.
			 */
			rc = btr_check_availability(tcx, &alb);
			if (rc != PROBE_RC_UNAVAILABLE) {
				if (rc == PROBE_RC_OK)
					break;

				goto out;
			}

			/* The record for current pos is unavailable, can be
			 * reused for the follow-on insert if applicable.
			 */
			if (saved == -1)
				saved = at;
		} else {
			/* Point at the next position in the current leaf node,
			 * this is for the follow-on insert if applicable.
			 */
			if (saved == -1)
				saved = at + 1;
		}

		if (btr_probe_next(tcx)) {
			cmp = BTR_CMP_UNKNOWN;
			break;
		}

		btr_trace_set(tcx, level, nd_off, saved);
		rc = PROBE_RC_NONE;
		goto out;

	case BTR_PROBE_LE:
		if (cmp == BTR_CMP_EQ) {
			rc = btr_check_availability(tcx, &alb);
			if (rc != PROBE_RC_UNAVAILABLE)
				goto out;
		}
		/* fall through */
	case BTR_PROBE_LT:
		if (cmp & BTR_CMP_LT) {
			/* Check availability if the target matched or it is
			 * for non-modification related operation.
			 */
			rc = btr_check_availability(tcx, &alb);
			if (rc != PROBE_RC_UNAVAILABLE) {
				if (rc == PROBE_RC_OK)
					break;

				goto out;
			}
		}

		if (btr_probe_prev(tcx)) {
			cmp = BTR_CMP_UNKNOWN;
			break;
		}

		rc = PROBE_RC_NONE;
		goto out;
	}

	if (cmp == BTR_CMP_UNKNOWN) {/* position changed, compare again */
		cmp = btr_cmp(tcx, BTR_NODE_NULL, -1, hkey, key);
		alb.nd_off = BTR_NODE_NULL;
		alb.at = -1;
		goto again;
	}

	D_ASSERT(cmp != BTR_CMP_EQ);
	/* GT/GE/LT/LE */
	rc = PROBE_RC_OK;
 out:
	tcx->tc_probe_rc = rc;
	if (rc == PROBE_RC_ERR)
		D_ERROR("Failed to probe: rc = %d\n", tcx->tc_probe_rc);
	else if (level >= 0)
		btr_trace_debug(tcx, &tcx->tc_trace[level], "\n");

	return rc;
}

static enum btr_probe_rc
btr_probe_key(struct btr_context *tcx, dbtree_probe_opc_t probe_opc,
	      uint32_t intent, d_iov_t *key)
{
	char hkey[DAOS_HKEY_MAX];

	btr_hkey_gen(tcx, key, hkey);
	return btr_probe(tcx, probe_opc, intent, key, hkey);
}

static bool
btr_probe_next(struct btr_context *tcx)
{
	struct btr_trace	*trace;
	struct btr_node		*nd;
	umem_off_t	 nd_off;

	if (btr_root_empty(tcx)) /* empty tree */
		return false;

	trace = &tcx->tc_trace[tcx->tc_depth - 1];

	btr_trace_debug(tcx, trace, "Probe the next\n");
	while (1) {
		bool leaf;

		nd_off = trace->tr_node;
		leaf = btr_node_is_leaf(tcx, nd_off);

		nd = btr_off2ptr(tcx, nd_off);

		/* NB: trace->tr_at might be larger than key number because
		 * split can happen between two calls.
		 */
		if (btr_node_is_root(tcx, nd_off) &&
		    trace->tr_at >= nd->tn_keyn - leaf) {
			D_ASSERT(trace == tcx->tc_trace);
			D_DEBUG(DB_TRACE, "End\n");
			return false; /* done */
		}

		if (trace->tr_at >= nd->tn_keyn - leaf) {
			/* finish current level */
			trace--;
			continue;
		}

		trace->tr_at++;
		btr_trace_debug(tcx, trace, "trace back\n");
		break;
	}

	while (trace < &tcx->tc_trace[tcx->tc_depth - 1]) {
		umem_off_t tmp;

		tmp = btr_node_child_at(tcx, trace->tr_node, trace->tr_at);
		trace++;
		trace->tr_at = 0;
		D_ASSERT(tmp != UMOFF_NULL);
		trace->tr_node = tmp;
	}

	btr_trace_debug(tcx, trace, "is the next\n");
	return true;
}

static bool
btr_probe_prev(struct btr_context *tcx)
{
	struct btr_trace	*trace;
	struct btr_node		*nd;
	umem_off_t		 nd_off;

	if (btr_root_empty(tcx)) /* empty tree */
		return false;

	trace = &tcx->tc_trace[tcx->tc_depth - 1];

	btr_trace_debug(tcx, trace, "Probe the prev\n");
	while (1) {
		nd_off = trace->tr_node;

		nd = btr_off2ptr(tcx, nd_off);

		if (btr_node_is_root(tcx, nd_off) && trace->tr_at == 0) {
			D_ASSERT(trace == tcx->tc_trace);
			D_DEBUG(DB_TRACE, "End\n");
			return false; /* done */
		}

		if (trace->tr_at == 0) {
			/* finish current level */
			trace--;
			continue;
		}

		trace->tr_at--;
		/* might split between two calls */
		if (trace->tr_at >= nd->tn_keyn)
			trace->tr_at = nd->tn_keyn - 1;

		btr_trace_debug(tcx, trace, "trace back\n");
		break;
	}

	while (trace < &tcx->tc_trace[tcx->tc_depth - 1]) {
		umem_off_t	tmp;
		bool			leaf;

		tmp = btr_node_child_at(tcx, trace->tr_node, trace->tr_at);

		trace++;
		D_ASSERT(tmp != UMOFF_NULL);
		trace->tr_node = tmp;
		leaf = btr_node_is_leaf(tcx, trace->tr_node);

		nd = btr_off2ptr(tcx, trace->tr_node);

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
 * \param intent [IN]		The operation intent.
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
dbtree_fetch(daos_handle_t toh, dbtree_probe_opc_t opc, uint32_t intent,
	     d_iov_t *key, d_iov_t *key_out, d_iov_t *val_out)
{
	struct btr_record  *rec;
	struct btr_context *tcx;
	int		    rc;

	tcx = btr_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	rc = btr_verify_key(tcx, key);
	if (rc)
		return rc;

	rc = btr_probe_key(tcx, opc, intent, key);
	switch (rc) {
	case PROBE_RC_INPROGRESS:
		D_DEBUG(DB_TRACE, "Target is in some uncommitted DTX.\n");
		return -DER_INPROGRESS;
	case PROBE_RC_DATA_LOSS:
		D_DEBUG(DB_TRACE, "Fetch hit some corrupted transaction.\n");
		return -DER_DATA_LOSS;
	case PROBE_RC_NONE:
		D_DEBUG(DB_TRACE, "Key does not exist.\n");
		return -DER_NONEXIST;
	case PROBE_RC_ERR:
		D_DEBUG(DB_TRACE, "Cannot find key: %d\n", tcx->tc_probe_rc);
		return -DER_NONEXIST;
	default:
		break;
	}

	rec = btr_trace2rec(tcx, tcx->tc_depth - 1);

	return btr_rec_fetch(tcx, rec, key_out, val_out);
}

/**
 * Fetch on current trace position.
 *
 * \param toh     [IN]		Tree open handle.
 * \param key_out [OUT]		Return the key
 * \param val_out [OUT]		Returned value address, or sink buffer to
 *				store returned value.
 *
 * \return		0	Key exists on current pos
 *			-ve	Error code
 */
int
dbtree_fetch_cur(daos_handle_t toh, d_iov_t *key_out, d_iov_t *val_out)
{
	struct btr_record	*rec;
	struct btr_context	*tcx;
	struct btr_trace	*trace;
	struct btr_node		*nd;
	int			rc;

	tcx = btr_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	if (btr_root_empty(tcx)) /* empty tree */
		return -DER_NONEXIST;

	rc = btr_verify_key(tcx, key_out);
	if (rc)
		return rc;

	D_ASSERT(tcx->tc_depth > 0);
	trace = &tcx->tc_trace[tcx->tc_depth - 1];

	nd = btr_off2ptr(tcx, trace->tr_node);
	D_ASSERT(trace->tr_at <= nd->tn_keyn);
	if (nd->tn_keyn == 0 || trace->tr_at == nd->tn_keyn)
		return -DER_NONEXIST;

	rec = btr_trace2rec(tcx, tcx->tc_depth - 1);
	return btr_rec_fetch(tcx, rec, key_out, val_out);
}

/**
 * Fetch sibling of current trace position.
 *
 * \param toh     [IN]		Tree open handle.
 * \param key_out [OUT]		Return the key
 * \param val_out [OUT]		Returned value address, or sink buffer to
 *				store returned value.
 * \param next    [IN]		Fetch next or prev sibling
 * \param move    [IN]		Move trace position or not
 *
 * \return		0	Key exists in current pos
 *			-ve	Error code
 */
static int
fetch_sibling(daos_handle_t toh, d_iov_t *key_out, d_iov_t *val_out, bool next, bool move)
{
	struct btr_record	*rec;
	struct btr_context	*tcx;
	struct btr_trace	*orig_trace;
	struct btr_trace	 orig_traces[BTR_TRACE_MAX];
	bool			 found;
	int			 rc;

	tcx = btr_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	rc = btr_verify_key(tcx, key_out);
	if (rc)
		return rc;

	/* Save original trace */
	if (!move) {
		orig_trace = tcx->tc_trace;
		memcpy(&orig_traces[0], &tcx->tc_traces[0],
		       sizeof(tcx->tc_traces[0]) * BTR_TRACE_MAX);
	}

	found = next ? btr_probe_next(tcx) : btr_probe_prev(tcx);
	if (!found) {
		rc = -DER_NONEXIST;
		goto out;
	}

	rec = btr_trace2rec(tcx, tcx->tc_depth - 1);
	rc = btr_rec_fetch(tcx, rec, key_out, val_out);
out:
	/* Restore original trace */
	if (!move) {
		tcx->tc_trace = orig_trace;
		memcpy(&tcx->tc_traces[0], &orig_traces[0],
		       sizeof(tcx->tc_traces[0]) * BTR_TRACE_MAX);
	}

	return rc;
}

int
dbtree_fetch_prev(daos_handle_t toh, d_iov_t *key_out, d_iov_t *val_out, bool move)
{
	return fetch_sibling(toh, key_out, val_out, false, move);
}

int
dbtree_fetch_next(daos_handle_t toh, d_iov_t *key_out, d_iov_t *val_out, bool move)
{
	return fetch_sibling(toh, key_out, val_out, true, move);
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
dbtree_lookup(daos_handle_t toh, d_iov_t *key, d_iov_t *val_out)
{
	return dbtree_fetch(toh, BTR_PROBE_EQ, DAOS_INTENT_DEFAULT, key, NULL,
			    val_out);
}

static int
btr_update(struct btr_context *tcx, d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	struct btr_record *rec;
	int		   rc;
	char		   sbuf[BTR_PRINT_BUF];

	rec = btr_trace2rec(tcx, tcx->tc_depth - 1);

	D_DEBUG(DB_TRACE, "Update record %s\n",
		btr_rec_string(tcx, rec, true, sbuf, BTR_PRINT_BUF));

	rc = btr_rec_update(tcx, rec, key, val, val_out);
	if (rc == -DER_NO_PERM) { /* cannot make inplace change */
		struct btr_trace *trace = &tcx->tc_trace[tcx->tc_depth - 1];

		if (btr_has_tx(tcx)) {
			rc = btr_node_tx_add(tcx, trace->tr_node);
			if (rc != 0)
				goto out;
		}

		D_DEBUG(DB_TRACE, "Replace the original record\n");
		rc = btr_rec_free(tcx, rec, NULL);
		if (rc)
			goto out;
		rc = btr_rec_alloc(tcx, key, val, rec, val_out);
	}
out:
	if (rc != 0) { /* failed */
		D_DEBUG(DB_TRACE, "Failed to update record: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}
	return 0;
}

/**
 * create a new record, insert it into tree leaf node.
 */
static int
btr_insert(struct btr_context *tcx, d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	struct btr_record *rec;
	char		  *rec_str = NULL;
	char		   str[BTR_PRINT_BUF];
	union btr_rec_buf  rec_buf = {0};
	int		   rc;

	rec = &rec_buf.rb_rec;
	btr_hkey_gen(tcx, key, &rec->rec_hkey[0]);

	rc = btr_rec_alloc(tcx, key, val, rec, val_out);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "Failed to create new record: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	if (D_LOG_ENABLED(DB_TRACE))
		rec_str = btr_rec_string(tcx, rec, true, str, BTR_PRINT_BUF);

	if (tcx->tc_depth != 0) {
		struct btr_trace *trace;

		/* trace for the leaf */
		trace = &tcx->tc_trace[tcx->tc_depth - 1];
		btr_trace_debug(tcx, trace, "try to insert\n");

		rc = btr_node_insert_rec(tcx, trace, rec);
		if (rc != 0) {
			D_DEBUG(DB_TRACE,
				"Failed to insert record to leaf: "DF_RC"\n",
					DP_RC(rc));
			return rc;
		}

	} else {
		/* empty tree */
		D_DEBUG(DB_TRACE, "Add record %s to an empty tree\n", rec_str);

		rc = btr_root_start(tcx, rec);
		if (rc != 0) {
			D_DEBUG(DB_TRACE, "Failed to start the tree: "DF_RC"\n",
				DP_RC(rc));
			return rc;
		}
	}
	return 0;
}

static int
btr_upsert(struct btr_context *tcx, dbtree_probe_opc_t probe_opc,
	   uint32_t intent, d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	int	 rc;


	if (probe_opc == BTR_PROBE_BYPASS)
		rc = tcx->tc_probe_rc; /* trust previous probe... */
	else
		rc = btr_probe_key(tcx, probe_opc, intent, key);

	switch (rc) {
	default:
		D_ASSERTF(false, "unknown returned value: "DF_RC"\n",
			DP_RC(rc));
		break;

	case PROBE_RC_OK:
		rc = btr_update(tcx, key, val, val_out);
		break;

	case PROBE_RC_NONE:
		rc = btr_insert(tcx, key, val, val_out);
		break;

	case PROBE_RC_UNKNOWN:
		rc = -DER_NO_PERM;
		break;

	case PROBE_RC_ERR:
		D_DEBUG(DB_TRACE, "btr_probe got PROBE_RC_ERR, probably due to "
			"key_cmp returned BTR_CMP_ERR, treats it as invalid "
			"operation.\n");
		rc = -DER_INVAL;
		break;
	case PROBE_RC_INPROGRESS:
		D_DEBUG(DB_TRACE, "The target is in some uncommitted DTX.");
		return -DER_INPROGRESS;
	case PROBE_RC_DATA_LOSS:
		D_DEBUG(DB_TRACE, "Upsert hit some corrupted transaction.\n");
		return -DER_DATA_LOSS;
	}

	tcx->tc_probe_rc = PROBE_RC_UNKNOWN; /* path changed */
	return rc;
}

static int
btr_tx_begin(struct btr_context *tcx)
{
	if (!btr_has_tx(tcx))
		return 0;

	return umem_tx_begin(btr_umm(tcx), NULL);
}

static int
btr_tx_end(struct btr_context *tcx, int rc)
{
	if (!btr_has_tx(tcx))
		return rc;

	if (rc != 0)
		return umem_tx_abort(btr_umm(tcx), rc);

	rc = umem_tx_commit(btr_umm(tcx));
	if (rc != 0)
		D_ERROR("Failed to commit the transaction: %d\n", rc);

	return rc;
}

/**
 * Update value of the provided key.
 *
 * \param toh		[IN]	Tree open handle.
 * \param key		[IN]	Key to search.
 * \param val		[IN]	New value for the key, it will punch the
 *				original value if \val is NULL.
 *
 * \return		0	success
 *			-ve	error code
 */
int
dbtree_update(daos_handle_t toh, d_iov_t *key, d_iov_t *val)
{
	struct btr_context *tcx;
	int		    rc;

	tcx = btr_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	rc = btr_verify_key(tcx, key);
	if (rc)
		return rc;

	rc = btr_tx_begin(tcx);
	if (rc != 0)
		return rc;

	rc = btr_upsert(tcx, BTR_PROBE_EQ, DAOS_INTENT_UPDATE, key, val, NULL);

	return btr_tx_end(tcx, rc);
}

/**
 * Set the tree feats.
 *
 * \param root[in]	Tree root
 * \param umm[in]	umem instance
 * \param feats[in]	feats to set
 *
 * \return 0 on success
 */
int
dbtree_feats_set(struct btr_root *root, struct umem_instance *umm, uint64_t feats)
{
	int			 rc = 0;
	bool                     end = false;

	if (root->tr_feats == feats)
		return 0;

	if ((root->tr_feats & BTR_FEAT_MASK) != (feats & BTR_FEAT_MASK)) {
		D_ERROR("Attempt to set internal features "DF_X64" denied\n", feats);
		return -DER_INVAL;
	}

#ifdef DAOS_PMEM_BUILD
	if (!umem_tx_inprogress(umm)) {
		rc = umem_tx_begin(umm, NULL);
		if (rc != 0)
			return rc;
		end = true;
	}
	rc = umem_tx_xadd_ptr(umm, &root->tr_feats, sizeof(root->tr_feats), UMEM_XADD_NO_SNAPSHOT);
#endif

	if (rc == 0)
		root->tr_feats = feats;

#ifdef DAOS_PMEM_BUILD
	if (end)
		rc = umem_tx_end(umm, rc);
#endif

	(void)end; /* less code than else blocks */

	return rc;
}

/**
 * Update the value of the provided key, or insert it as a new key if
 * there is no match.
 *
 * \param toh		[IN]	Tree open handle.
 * \param opc		[IN]	Probe opcode, see dbtree_probe_opc_t for the
 *				details.
 * \param key		[IN]	Key to search.
 * \param val		[IN]	New value for the key, it will punch the
 *				original value if \val is NULL.
 * \param val_out	[OUT]	Return value address
 *
 * \return		0	success
 *			-ve	error code
 */
int
dbtree_upsert(daos_handle_t toh, dbtree_probe_opc_t opc, uint32_t intent,
	      d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	struct btr_context *tcx;
	int		    rc = 0;

	tcx = btr_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	rc = btr_verify_key(tcx, key);
	if (rc)
		return rc;

	rc = btr_tx_begin(tcx);
	if (rc != 0)
		return rc;
	rc = btr_upsert(tcx, opc, intent, key, val, val_out);

	return btr_tx_end(tcx, rc);
}

/**
 * Delete the leaf record pointed by @cur_tr from the current node, then fill
 * the deletion gap by shifting remainded records on the specified direction.
 *
 * NB: this function can delete the last record in the node, it means that
 * caller should be responsible for deleting this node.
 */
static int
btr_node_del_leaf_only(struct btr_context *tcx, struct btr_trace *trace,
		       bool shift_left, void *args)
{
	struct btr_record *rec;
	struct btr_node   *nd;
	int		   rc;

	nd = btr_off2ptr(tcx, trace->tr_node);
	D_ASSERT(nd->tn_keyn > 0 && nd->tn_keyn > trace->tr_at);

	rec = btr_node_rec_at(tcx, trace->tr_node, trace->tr_at);
	rc = btr_rec_free(tcx, rec, args);
	if (rc != 0)
		return rc;

	nd->tn_keyn--;
	if (shift_left && trace->tr_at != nd->tn_keyn) {
		/* shift left records which are on the right side of the
		 * deleted record.
		 */
		btr_rec_move(tcx, rec, btr_rec_at(tcx, rec, 1),
			     nd->tn_keyn - trace->tr_at);

	} else if (!shift_left && trace->tr_at != 0) {
		/* shift right records which are on the left side of the
		 * deleted record.
		 */
		rec = btr_node_rec_at(tcx, trace->tr_node, 0);
		btr_rec_move(tcx, btr_rec_at(tcx, rec, 1), rec,
			     trace->tr_at);
	}

	return 0;
}

/**
 * Delete the leaf record pointed by @cur_tr from the current node, then grab
 * a leaf record from the sibling node @sib_off and add this record to the
 * current node. Because of the records movement between sibling nodes, this
 * function also needs to update the hashed key stored in the parent record
 * pointed by @par_tr.
 *
 * NB: this function only grab one record from the sibling node, although we
 * might want to grab multiple records in the future.
 *
 * \param tcx		[IN]	Tree operation context.
 * \param par_tr	[IN]	Probe trace of the current node in the parent
 *				node.
 * \param cur_tr	[IN]	Probe trace of the record being deleted in the
 *				current node.
 * \param sib_off	[IN]	umem offset of the sibling node.
 * \param sib_on_right	[IN]	The sibling node is on the right/left side of
 *				the current node:
 *				TRUE	= right
 *				FALSE	= left
 */
static int
btr_node_del_leaf_rebal(struct btr_context *tcx,
			struct btr_trace *par_tr, struct btr_trace *cur_tr,
			umem_off_t sib_off, bool sib_on_right,
			void *args)
{
	struct btr_node		*cur_nd;
	struct btr_node		*sib_nd;
	struct btr_record	*par_rec;
	struct btr_record	*src_rec;
	struct btr_record	*dst_rec;
	int			 rc;

	cur_nd = btr_off2ptr(tcx, cur_tr->tr_node);
	sib_nd = btr_off2ptr(tcx, sib_off);
	D_ASSERT(sib_nd->tn_keyn > 1);

	rc = btr_node_del_leaf_only(tcx, cur_tr, sib_on_right, args);
	if (rc != 0)
		return rc;

	D_DEBUG(DB_TRACE, "Grab records from the %s sibling, cur:sib=%d:%d\n",
		sib_on_right ? "right" : "left", cur_nd->tn_keyn,
		sib_nd->tn_keyn);

	if (sib_on_right) {
		/* grab the first record from the right sibling */
		src_rec = btr_node_rec_at(tcx, sib_off, 0);
		dst_rec = btr_node_rec_at(tcx, cur_tr->tr_node,
					  cur_nd->tn_keyn);
		btr_rec_copy(tcx, dst_rec, src_rec, 1);
		/* shift left remainded record on the sibling */
		btr_rec_move(tcx, src_rec, btr_rec_at(tcx, src_rec, 1),
			     sib_nd->tn_keyn - 1);

		/* copy the first hkey of the right sibling node to the
		 * parent node.
		 */
		par_rec = btr_node_rec_at(tcx, par_tr->tr_node, par_tr->tr_at);

		/* NB: Direct key of parent already points here */
		if (!btr_is_direct_key(tcx))
			btr_rec_copy_hkey(tcx, par_rec, src_rec);
	} else {
		/* grab the last record from the left sibling */
		src_rec = btr_node_rec_at(tcx, sib_off, sib_nd->tn_keyn - 1);
		dst_rec = btr_node_rec_at(tcx, cur_tr->tr_node, 0);
		btr_rec_copy(tcx, dst_rec, src_rec, 1);
		/* copy the first record key of the current node to the
		 * parent node.
		 */
		par_rec = btr_node_rec_at(tcx, par_tr->tr_node,
					  par_tr->tr_at - 1);
		/* NB: Direct key of parent already points to this leaf */
		if (!btr_is_direct_key(tcx))
			btr_rec_copy_hkey(tcx, par_rec, dst_rec);
	}
	cur_nd->tn_keyn++;
	sib_nd->tn_keyn--;

	return 0;
}

/**
 * Delete the leaf record pointed by @cur_tr from the current node, then either
 * merge the current node to its left sibling, or merge the right sibling to
 * the current node. It means that caller should always delete the node on the
 * right side, which will be stored in @par_tr, after returning from this
 * function.
 *
 * NB: See \a btr_node_del_leaf_rebal for the details of parameters.
 * NB: This function should be called only if btr_node_del_leaf_rebal() cannot
 * be called (cannot rebalance the current node and its sibling).
 */
static int
btr_node_del_leaf_merge(struct btr_context *tcx,
			struct btr_trace *par_tr, struct btr_trace *cur_tr,
			umem_off_t sib_off, bool sib_on_right,
			void *args)
{
	struct btr_node		*src_nd;
	struct btr_node		*dst_nd;
	struct btr_record	*src_rec;
	struct btr_record	*dst_rec;
	int			 rc;

	/* NB: always left shift because it is easier for the following
	 * operations.
	 */
	rc = btr_node_del_leaf_only(tcx, cur_tr, true, args);
	if (rc != 0)
		return rc;

	if (sib_on_right) {
		/* move all records from the right sibling node to the
		 * current node.
		 */

		src_nd = btr_off2ptr(tcx, sib_off);
		dst_nd = btr_off2ptr(tcx, cur_tr->tr_node);

		D_DEBUG(DB_TRACE,
			"Merge the right sibling to current node, "
			"cur:sib=%d:%d\n", dst_nd->tn_keyn, src_nd->tn_keyn);

		src_rec = btr_node_rec_at(tcx, sib_off, 0);
		dst_rec = btr_node_rec_at(tcx, cur_tr->tr_node,
					  dst_nd->tn_keyn);

	} else {
		/* move all records from the current node to the left
		 * sibling node.
		 */
		src_nd = btr_off2ptr(tcx, cur_tr->tr_node);
		dst_nd = btr_off2ptr(tcx, sib_off);

		D_DEBUG(DB_TRACE,
			"Merge the current node to left sibling, "
			"cur:sib=%d:%d\n", src_nd->tn_keyn, dst_nd->tn_keyn);

		if (src_nd->tn_keyn != 0) {
			src_rec = btr_node_rec_at(tcx, cur_tr->tr_node, 0);
			dst_rec = btr_node_rec_at(tcx, sib_off,
						  dst_nd->tn_keyn);
		} else { /* current node is empty */
			src_rec = dst_rec = NULL;
		}
	}

	if (src_rec != NULL) {
		btr_rec_copy(tcx, dst_rec, src_rec, src_nd->tn_keyn);

		dst_nd->tn_keyn += src_nd->tn_keyn;
		D_ASSERT(dst_nd->tn_keyn < tcx->tc_order);
		src_nd->tn_keyn = 0;
	}

	/* point at the node that needs be removed from the parent */
	par_tr->tr_at += sib_on_right;

	return 0;
}

/**
 * Delete the specified leaf record from the current node:
 * - if the current node has more than one record, just delete and return.
 * - if the current node only has one leaf record, and the sibling has more
 *   than one leaf records, grab one record from the sibling node after
 *   the deletion.
 * - if the current node only has one leaf record, and the sibling has one
 *   leaf record as well, merge the current node with the sibling after
 *   the deletion.
 *
 * \return	0	need to bubble up to upper level tree.
 * \return	+1	NOT need to bubble up to upper level tree.
 * \return	-ev	for failure cases.
 */
static int
btr_node_del_leaf(struct btr_context *tcx,
		  struct btr_trace *par_tr, struct btr_trace *cur_tr,
		  umem_off_t sib_off, bool sib_on_right, void *args)
{
	struct btr_node *sib_nd;
	int		 rc;

	if (UMOFF_IS_NULL(sib_off)) {
		/* don't need to rebalance or merge */
		rc = btr_node_del_leaf_only(tcx, cur_tr, true, args);
		if (rc != 0)
			return rc;

		if (tcx->tc_feats & BTR_FEAT_SKIP_LEAF_REBAL) {
			struct btr_node	*nd;

			nd = btr_off2ptr(tcx, cur_tr->tr_node);
			/* Current leaf node become empty,
			 * will be removed from parent node.
			 */
			if (nd->tn_keyn == 0)
				return 0;
		}

		return 1;
	}

	sib_nd = btr_off2ptr(tcx, sib_off);
	if (sib_nd->tn_keyn > 1) {
		/* grab a record from the sibling */
		rc = btr_node_del_leaf_rebal(tcx, par_tr, cur_tr, sib_off,
					     sib_on_right, args);
		if (rc != 0)
			return rc;

		return 1;
	}

	/* the sibling can't give record to the current node, merge them */
	rc = btr_node_del_leaf_merge(tcx, par_tr, cur_tr, sib_off, sib_on_right,
				     args);
	return rc;
}

/**
 * Delete the child record (non-leaf) pointed by @cur_tr from the current node,
 * then fill the deletion gap by shifting remainded records on the specified
 * direction. In addition, caller should guarantee the child being deleted
 * is already empty.
 *
 * NB: This function may leave the node in an intermediate state if it only
 * has one key (and two children). In this case, after returning from this
 * function, caller should either grab a child from a sibling node, or move
 * the only child of this node to a sibling node, then free this node.
 */
static int
btr_node_del_child_only(struct btr_context *tcx, struct btr_trace *trace,
			bool shift_left)
{
	struct btr_node		*nd;
	struct btr_record	*rec;
	umem_off_t		 off;
	int			 rc;

	nd = btr_off2ptr(tcx, trace->tr_node);
	D_ASSERT(nd->tn_keyn > 0 && nd->tn_keyn >= trace->tr_at);

	/* free the child node being deleted */
	off = btr_node_child_at(tcx, trace->tr_node, trace->tr_at);

	/* NB: we always delete record/node from the bottom to top, so it is
	 * unnecessary to do cascading free anymore (btr_node_destroy).
	 */
	rc = btr_node_free(tcx, off);
	if (rc != 0)
		return rc;

	nd->tn_keyn--;
	if (shift_left) {
		/* shift left those records that are on the right side of the
		 * deleted record.
		 */
		if (trace->tr_at == 0) {
			nd->tn_child = nd->tn_recs[0].rec_off;
		} else {
			trace->tr_at -= 1;
		}

		if (trace->tr_at != nd->tn_keyn) {
			rec = btr_node_rec_at(tcx, trace->tr_node,
					      trace->tr_at);
			btr_rec_move(tcx, rec, btr_rec_at(tcx, rec, 1),
				     nd->tn_keyn - trace->tr_at);
		}

	} else {
		/* shift right those records that are on the left side of the
		 * deleted record.
		 */
		if (trace->tr_at != 0) {
			rec = btr_node_rec_at(tcx, trace->tr_node, 0);
			if (trace->tr_at > 1) {
				btr_rec_move(tcx, btr_rec_at(tcx, rec, 1), rec,
					     trace->tr_at - 1);
			}
			rec->rec_off = nd->tn_child;
		}
	}

	return 0;
}

/**
 * Delete the child node pointed by @cur_tr, then grab a child node from the
 * sibling node @sib_off and insert this record to the current node. Because
 * of the record/node movement, this function also needs to updates the hashed
 * key stored in the parent record pointed by @par_tr.
 *
 * NB: we only grab one child from the sibling node, although we might want
 *     to grab more in the future.
 * NB: see \a btr_node_del_leaf_rebal for the details of parameters
 */
static int
btr_node_del_child_rebal(struct btr_context *tcx,
			 struct btr_trace *par_tr, struct btr_trace *cur_tr,
			 umem_off_t sib_off, bool sib_on_right,
			 void *args)
{
	struct btr_node		*cur_nd;
	struct btr_node		*sib_nd;
	struct btr_record	*par_rec;
	struct btr_record	*src_rec;
	struct btr_record	*dst_rec;
	int			 rc;

	cur_nd = btr_off2ptr(tcx, cur_tr->tr_node);
	sib_nd = btr_off2ptr(tcx, sib_off);
	D_ASSERT(sib_nd->tn_keyn > 1);

	rc = btr_node_del_child_only(tcx, cur_tr, sib_on_right);
	if (rc != 0)
		return rc;

	D_DEBUG(DB_TRACE, "Grab children from the %s sibling, cur:sib=%d:%d\n",
		sib_on_right ? "right" : "left", cur_nd->tn_keyn,
		sib_nd->tn_keyn);

	if (sib_on_right) {
		/* grab the first child from the right sibling */
		src_rec = btr_node_rec_at(tcx, sib_off, 0);
		dst_rec = btr_node_rec_at(tcx, cur_tr->tr_node,
					  cur_nd->tn_keyn);
		par_rec = btr_node_rec_at(tcx, par_tr->tr_node, par_tr->tr_at);

		dst_rec->rec_off = sib_nd->tn_child;

		btr_rec_copy_hkey(tcx, dst_rec, par_rec);
		btr_rec_copy_hkey(tcx, par_rec, src_rec);

		sib_nd->tn_child = src_rec->rec_off;
		btr_rec_move(tcx, src_rec, btr_rec_at(tcx, src_rec, 1),
			     sib_nd->tn_keyn - 1);

	} else {
		/* grab the last child from the left sibling */
		src_rec = btr_node_rec_at(tcx, sib_off, sib_nd->tn_keyn - 1);
		dst_rec = btr_node_rec_at(tcx, cur_tr->tr_node, 0);
		par_rec = btr_node_rec_at(tcx, par_tr->tr_node,
					  par_tr->tr_at - 1);

		btr_rec_copy_hkey(tcx, dst_rec, par_rec);
		btr_rec_copy_hkey(tcx, par_rec, src_rec);

		cur_nd->tn_child = src_rec->rec_off;
	}
	cur_nd->tn_keyn++;
	sib_nd->tn_keyn--;

	return 0;
}

/**
 * Delete the child node pointed by @cur_tr from the current node, then either
 * merge the current node to its left sibling, or merge the right sibling to
 * the current node. It means that caller should always delete the node on the
 * right side, which will be stored in @par_tr, after returning from this
 * function.
 *
 * NB: see \a btr_node_del_leaf_rebal for the details of parameters
 */
static int
btr_node_del_child_merge(struct btr_context *tcx,
			 struct btr_trace *par_tr, struct btr_trace *cur_tr,
			 umem_off_t sib_off, bool sib_on_right, void *args)
{
	struct btr_node		*src_nd;
	struct btr_node		*dst_nd;
	struct btr_record	*par_rec;
	struct btr_record	*src_rec;
	struct btr_record	*dst_rec;
	int			 rc;

	/* NB: always left shift because it is easier for the following
	 * operations.
	 */
	rc = btr_node_del_child_only(tcx, cur_tr, true);
	if (rc != 0)
		return rc;

	if (sib_on_right) {
		/* move children from the right sibling to the current node. */
		src_nd = btr_off2ptr(tcx, sib_off);
		dst_nd = btr_off2ptr(tcx, cur_tr->tr_node);

		D_DEBUG(DB_TRACE,
			"Merge the right sibling to current node, "
			"cur:sib=%d:%d\n", dst_nd->tn_keyn, src_nd->tn_keyn);

		src_rec = btr_node_rec_at(tcx, sib_off, 0);
		dst_rec = btr_node_rec_at(tcx, cur_tr->tr_node,
					  dst_nd->tn_keyn);
		par_rec = btr_node_rec_at(tcx, par_tr->tr_node, par_tr->tr_at);

		dst_rec->rec_off = src_nd->tn_child;

	} else {
		/* move children of the current node to the left sibling. */
		src_nd = btr_off2ptr(tcx, cur_tr->tr_node);
		dst_nd = btr_off2ptr(tcx, sib_off);

		D_DEBUG(DB_TRACE,
			"Merge the current node to left sibling, "
			"cur:sib=%d:%d\n", src_nd->tn_keyn, dst_nd->tn_keyn);

		dst_rec = btr_node_rec_at(tcx, sib_off, dst_nd->tn_keyn);
		par_rec = btr_node_rec_at(tcx, par_tr->tr_node,
					  par_tr->tr_at - 1);

		dst_rec->rec_off = src_nd->tn_child;
		src_rec = src_nd->tn_keyn == 0 ?
			  NULL : btr_node_rec_at(tcx, cur_tr->tr_node, 0);
	}
	btr_rec_copy_hkey(tcx, dst_rec, par_rec);

	if (src_rec != NULL) {
		dst_rec = btr_rec_at(tcx, dst_rec, 1); /* the next record */
		btr_rec_copy(tcx, dst_rec, src_rec, src_nd->tn_keyn);
	}

	/* NB: destination got an extra key from the parent, and an extra
	 * child pointer from src_nd::tn_child.
	 */
	dst_nd->tn_keyn += src_nd->tn_keyn + 1;
	D_ASSERT(dst_nd->tn_keyn < tcx->tc_order);
	src_nd->tn_keyn = 0;

	/* point at the node that needs be removed from the parent */
	par_tr->tr_at += sib_on_right;

	return 0;
}

/**
 * Delete the specified child node from the current node:
 * - if the current node has more than two children, just delete and return
 * - if the current node only has two children, and the sibling has more than
 *   two children, grab one child from the sibling node after the deletion.
 * - if the current node only has two children, and the sibling has two
 *   children as well, merge the current node with the sibling after the
 *   deletion.
 *
 * \return	0	need to bubble up to upper level tree.
 * \return	+1	NOT need to bubble up to upper level tree.
 * \return	-ev	for failure cases.
 */
static int
btr_node_del_child(struct btr_context *tcx,
		   struct btr_trace *par_tr, struct btr_trace *cur_tr,
		   umem_off_t sib_off, bool sib_on_right, void *args)
{
	struct btr_node *sib_nd;
	int		 rc;

	if (UMOFF_IS_NULL(sib_off)) {
		/* don't need to rebalance or merge */
		rc = btr_node_del_child_only(tcx, cur_tr, true);
		if (rc != 0)
			return rc;

		return 1;
	}

	sib_nd = btr_off2ptr(tcx, sib_off);
	if (sib_nd->tn_keyn > 1) {
		/* grab a child from the sibling */
		rc = btr_node_del_child_rebal(tcx, par_tr, cur_tr, sib_off,
					      sib_on_right, args);
		if (rc != 0)
			return rc;

		return 1;
	}

	/* the sibling can't give any record to the current node, merge them */
	rc = btr_node_del_child_merge(tcx, par_tr, cur_tr, sib_off,
				      sib_on_right, args);
	return rc;
}

/**
 * Delete the child node or leaf record pointed by @cur_tr from the current
 * node, if the deletion generates a new empty node (the current node or its
 * sibling node), then the deletion needs to bubble up.
 *
 * \param par_tr	[IN/OUT]
 *				Probe trace of the current node in the parent
 *				node. If the deletion generates a new empty
 *				node, this new empty node will be stored in
 *				@par_tr as well.
 * \param cur_tr	[IN]	Probe trace of the record being deleted in the
 *				current node
 */
static int
btr_node_del_rec(struct btr_context *tcx, struct btr_trace *par_tr,
		 struct btr_trace *cur_tr, void *args)
{
	struct btr_node		*par_nd;
	struct btr_node		*cur_nd;
	struct btr_node		*sib_nd;
	bool			 is_leaf;
	bool			 sib_on_right;
	umem_off_t		 sib_off;

	is_leaf = btr_node_is_leaf(tcx, cur_tr->tr_node);

	cur_nd = btr_off2ptr(tcx, cur_tr->tr_node);
	par_nd = btr_off2ptr(tcx, par_tr->tr_node);
	D_ASSERT(par_nd->tn_keyn > 0);

	D_DEBUG(DB_TRACE, "Delete %s from the %s node, key_nr = %d\n",
		is_leaf ? "record" : "child", is_leaf ? "leaf" : "non-leaf",
		cur_nd->tn_keyn);

	if (cur_nd->tn_keyn > 1 ||
	    (is_leaf && tcx->tc_feats & BTR_FEAT_SKIP_LEAF_REBAL)) {
		/* OK to delete record without doing any extra work */
		D_DEBUG(DB_TRACE, "Straight away deletion, no rebalance.\n");
		sib_off	= BTR_NODE_NULL;
		sib_on_right = false; /* whatever... */
	} else { /* needs to rebalance or merge nodes */
		D_DEBUG(DB_TRACE, "Parent trace at=%d, key_nr=%d\n",
			par_tr->tr_at, par_nd->tn_keyn);

		if (par_tr->tr_at == 0) {
			/* only has sibling on the right side */
			sib_off = btr_node_child_at(tcx, par_tr->tr_node, 1);
			sib_on_right = true;

		} else if (par_tr->tr_at == par_nd->tn_keyn) {
			/* only has sibling on the left side */
			sib_off = btr_node_child_at(tcx, par_tr->tr_node,
						     par_tr->tr_at - 1);
			sib_on_right = false;
		} else {
			sib_off = btr_node_child_at(tcx, par_tr->tr_node,
						     par_tr->tr_at + 1);
			sib_nd = btr_off2ptr(tcx, sib_off);
			D_ASSERT(sib_nd->tn_keyn > 0);

			if (sib_nd->tn_keyn > 1) {
				/* sufficient records on the right sibling */
				sib_on_right = true;
			} else {
				/* try the left sibling */
				sib_off = btr_node_child_at(tcx,
							    par_tr->tr_node,
							    par_tr->tr_at - 1);
				sib_on_right = false;
			}
		}
		D_DEBUG(DB_TRACE, "Delete and rebalance with the %s sibling.\n",
			sib_on_right ? "right" : "left");
	}

	if (btr_has_tx(tcx)) {
		int	rc;

		rc = btr_node_tx_add(tcx, cur_tr->tr_node);
		if (rc != 0)
			return rc;

		/* if sib_off isn't NULL, it means rebalance/merge will happen
		 * and the sibling and parent nodes will be changed.
		 */
		if (!UMOFF_IS_NULL(sib_off)) {
			rc = btr_node_tx_add(tcx, sib_off);
			if (rc != 0)
				return rc;

			rc = btr_node_tx_add(tcx, par_tr->tr_node);
			if (rc != 0)
				return rc;
		}
	}

	if (is_leaf)
		return btr_node_del_leaf(tcx, par_tr, cur_tr, sib_off,
					 sib_on_right, args);

	return btr_node_del_child(tcx, par_tr, cur_tr, sib_off,
				  sib_on_right, args);
}

/**
 * Deleted the record/child pointed by @trace from the root node.
 *
 * - If the root node is also a leaf, and the root is empty after the deletion,
 *   then the root node will be freed as well.
 * - If the root node is a non-leaf node, then the corresponding child node
 *   will be deleted as well. If there is only one child left, then that child
 *   will become the new root, the original root node will be freed.
 */
static int
btr_root_del_rec(struct btr_context *tcx, struct btr_trace *trace, void *args)
{
	struct btr_node		*node;
	struct btr_root		*root;
	int			 rc = 0;

	root = tcx->tc_tins.ti_root;
	node = btr_off2ptr(tcx, trace->tr_node);

	D_DEBUG(DB_TRACE, "Delete record/child from tree root, depth=%d\n",
		root->tr_depth);

	if (btr_node_is_leaf(tcx, trace->tr_node)) {
		D_DEBUG(DB_TRACE, "Delete leaf from the root, key_nr=%d.\n",
			node->tn_keyn);

		/* the root is also a leaf node */
		if (node->tn_keyn > 1) {
			/* have more than one record, simply remove the record
			 * to be deleted.
			 */
			if (btr_has_tx(tcx)) {
				rc = btr_node_tx_add(tcx, trace->tr_node);
				if (rc != 0)
					return rc;
			}

			rc = btr_node_del_leaf_only(tcx, trace, true, args);
		} else {

			rc = btr_node_destroy(tcx, trace->tr_node, args, NULL);
			if (rc != 0)
				return rc;

			if (btr_has_tx(tcx)) {
				rc = btr_root_tx_add(tcx);
				if (rc != 0)
					return rc;
			}

			root->tr_depth	= 0;
			root->tr_node	= BTR_NODE_NULL;

			btr_context_set_depth(tcx, 0);
			D_DEBUG(DB_TRACE, "Tree is empty now.\n");
		}
	} else {
		/* non-leaf node */
		D_DEBUG(DB_TRACE, "Delete child from the root, key_nr=%d.\n",
			node->tn_keyn);

		if (btr_has_tx(tcx)) {
			rc = btr_node_tx_add(tcx, trace->tr_node);
			if (rc != 0)
				return rc;
		}

		rc = btr_node_del_child_only(tcx, trace, true);
		if (rc != 0)
			return rc;

		if (node->tn_keyn == 0) {
			/* only has zero key and one child left, reduce
			 * the tree depth by using the only child node
			 * to replace the current node.
			 */
			if (btr_has_tx(tcx)) {
				rc = btr_root_tx_add(tcx);
				if (rc != 0)
					return rc;
			}

			root->tr_depth--;
			D_ASSERT(node->tn_child != UMOFF_NULL);
			root->tr_node = node->tn_child;

			btr_context_set_depth(tcx, root->tr_depth);
			btr_node_set(tcx, node->tn_child, BTR_NODE_ROOT);
			rc = btr_node_free(tcx, trace->tr_node);

			D_CDEBUG(rc != 0, DLOG_ERR, DB_TRACE,
				 "Shrink tree depth to %d: rc = %d\n",
				 tcx->tc_depth, rc);
		}
	}

	return rc;
}

static int
btr_delete(struct btr_context *tcx, void *args)
{
	struct btr_trace	*par_tr;
	struct btr_trace	*cur_tr;
	int			 rc = 0;

	for (cur_tr = &tcx->tc_trace[tcx->tc_depth - 1];; cur_tr = par_tr) {
		if (cur_tr == tcx->tc_trace) { /* root */
			rc = btr_root_del_rec(tcx, cur_tr, args);
			break;
		}

		par_tr = cur_tr - 1;
		rc = btr_node_del_rec(tcx, par_tr, cur_tr, args);
		if (rc != 0) {
			if (rc > 0)
				rc = 0;
			break;
		}
	}

	D_CDEBUG(rc != 0, DLOG_ERR, DB_TRACE, "Deletion done: rc = %d\n", rc);
	return rc;
}

static int
btr_tx_delete(struct btr_context *tcx, void *args)
{
	int		      rc = 0;

	rc = btr_tx_begin(tcx);
	if (rc != 0)
		return rc;
	rc = btr_delete(tcx, args);

	return btr_tx_end(tcx, rc);
}

/**
 * Delete the @key and the corresponding value from the btree.
 *
 * \param toh		[IN]	Tree open handle.
 * \param key		[IN]	The key to be deleted.
 * \param args		[IN/OUT]
 *				Optional: buffer to provide
 *				args to handle special cases(if any)
 */
int
dbtree_delete(daos_handle_t toh, dbtree_probe_opc_t opc, d_iov_t *key,
	      void *args)
{
	struct btr_context *tcx;
	int		    rc;

	tcx = btr_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	rc = btr_verify_key(tcx, key);
	if (rc)
		return rc;

	if (opc == BTR_PROBE_BYPASS)
		goto delete;

	rc = btr_probe_key(tcx, opc, DAOS_INTENT_PUNCH, key);
	if (rc == PROBE_RC_INPROGRESS) {
		D_DEBUG(DB_TRACE, "Target is in some uncommitted DTX.\n");
		return -DER_INPROGRESS;
	}

	if (rc == PROBE_RC_DATA_LOSS) {
		D_DEBUG(DB_TRACE, "Delete hit some corrupted transaction.\n");
		return -DER_DATA_LOSS;
	}

	if (rc != PROBE_RC_OK) {
		D_DEBUG(DB_TRACE, "Cannot find key\n");
		return -DER_NONEXIST;
	}

delete:
	rc = btr_tx_delete(tcx, args);

	tcx->tc_probe_rc = PROBE_RC_UNKNOWN;
	return rc;
}

/** gather statistics from a tree node and all its children recursively. */
static void
btr_node_stat(struct btr_context *tcx, umem_off_t nd_off,
	      struct btr_stat *stat)
{
	struct btr_node *nd	= btr_off2ptr(tcx, nd_off);
	bool		 leaf	= btr_node_is_leaf(tcx, nd_off);
	int		 rc;
	int		 i;

	D_DEBUG(DB_TRACE, "Stat tree %s "DF_X64", keyn %d\n",
		leaf ? "leaf" : "node", nd_off, nd->tn_keyn);

	if (!leaf) {
		stat->bs_node_nr += nd->tn_keyn + 1;
		for (i = 0; i <= nd->tn_keyn; i++) {
			umem_off_t	child_off;

			child_off = btr_node_child_at(tcx, nd_off, i);
			btr_node_stat(tcx, child_off, stat);
		}
		return;
	}
	/* leaf */
	stat->bs_rec_nr += nd->tn_keyn;
	for (i = 0; i < nd->tn_keyn; i++) {
		struct btr_record	*rec;
		struct btr_rec_stat	 rs;

		rec = btr_node_rec_at(tcx, nd_off, i);
		rc = btr_rec_stat(tcx, rec, &rs);
		if (rc != 0)
			continue;

		stat->bs_key_sum += rs.rs_ksize;
		stat->bs_val_sum += rs.rs_vsize;

		if (stat->bs_key_max < rs.rs_ksize)
			stat->bs_key_max = rs.rs_ksize;
		if (stat->bs_val_max < rs.rs_vsize)
			stat->bs_val_max = rs.rs_vsize;
	}
}

/** scan all tree nodes and records, gather their stats */
static int
btr_tree_stat(struct btr_context *tcx, struct btr_stat *stat)
{
	struct btr_root *root;

	memset(stat, 0, sizeof(*stat));

	root = tcx->tc_tins.ti_root;
	if (!UMOFF_IS_NULL(root->tr_node)) {
		/* stat the root and all descendants */
		stat->bs_node_nr = 1;
		btr_node_stat(tcx, root->tr_node, stat);
	}
	return 0;
}

/* Estimates the number of elements in a btree.  If the depth is 1,
 * the count is exact.  Otherwise, it's an estimate based on the
 * depth of the tree.  The primary existing use case is to know
 * when to collapse the incarnation log so we can reduce space
 * usage when only 1 entry exists.
 */
static int
btr_tree_count(struct btr_context *tcx, struct btr_root *root)
{
	int	i;
	int	total;

	if (root->tr_depth == 0)
		return 0;
	if (root->tr_depth == 1) {
		struct btr_node *node = btr_off2ptr(tcx, root->tr_node);

		return node->tn_keyn;
	}

	total = 1;
	for (i = 0; i < root->tr_depth; i++)
		total *= root->tr_order;
	return total / 2;
}

/**
 * Query attributes and/or gather nodes and records statistics of btree.
 *
 * \param toh	[IN]	The tree open handle.
 * \param attr	[OUT]	Optional, returned tree attributes.
 * \param stat	[OUT]	Optional, returned nodes and records statistics.
 */
int
dbtree_query(daos_handle_t toh, struct btr_attr *attr, struct btr_stat *stat)
{
	struct btr_context *tcx;

	tcx = btr_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	if (attr != NULL) {
		struct btr_root *root = tcx->tc_tins.ti_root;

		attr->ba_order	= root->tr_order;
		attr->ba_depth	= root->tr_depth;
		attr->ba_class	= root->tr_class;
		attr->ba_feats	= root->tr_feats;
		umem_attr_get(&tcx->tc_tins.ti_umm, &attr->ba_uma);
		attr->ba_count = btr_tree_count(tcx, root);
	}

	if (stat != NULL)
		btr_tree_stat(tcx, stat);

	return 0;
}

/**
 * Is the btree empty or not
 *
 * \return	0	Not empty
 *		1	Empty
 *		-ve	error code
 */
int
dbtree_is_empty(daos_handle_t toh)
{
	struct btr_context *tcx;

	tcx = btr_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	return tcx->tc_tins.ti_root->tr_depth == 0;
}

static int
btr_tree_alloc(struct btr_context *tcx)
{
	int	rc;

	rc = btr_root_alloc(tcx);
	D_DEBUG(DB_TRACE, "Allocate tree root: "DF_RC"\n", DP_RC(rc));

	return rc;
}

static int
btr_tx_tree_alloc(struct btr_context *tcx)
{
	int		      rc = 0;

	rc = btr_tx_begin(tcx);
	if (rc != 0)
		return rc;

	rc = btr_tree_alloc(tcx);

	return btr_tx_end(tcx, rc);
}

/**
 * Create an empty tree.
 *
 * \param tree_class	[IN]	Class ID of the tree.
 * \param tree_feats	[IN]	Feature bits of the tree.
 * \param tree_order	[IN]	Btree order, value >= 3.
 * \param uma		[IN]	Memory class attributes.
 * \param root_offp	[OUT]	Returned root umem offset.
 * \param toh		[OUT]	Returned tree open handle.
 */
int
dbtree_create(unsigned int tree_class, uint64_t tree_feats,
	      unsigned int tree_order, struct umem_attr *uma,
	      umem_off_t *root_offp, daos_handle_t *toh)
{
	struct btr_context *tcx;
	int		    rc;

	if (tree_order < BTR_ORDER_MIN || tree_order > BTR_ORDER_MAX) {
		D_DEBUG(DB_TRACE, "Order (%d) should be between %d and %d\n",
			tree_order, BTR_ORDER_MIN, BTR_ORDER_MAX);
		return -DER_INVAL;
	}

	rc = btr_context_create(BTR_ROOT_NULL, NULL, tree_class, tree_feats,
				tree_order, uma, DAOS_HDL_INVAL, NULL, &tcx);
	if (rc != 0)
		return rc;

	rc = btr_tx_tree_alloc(tcx);

	if (rc != 0)
		goto failed;

	if (root_offp)
		*root_offp = tcx->tc_tins.ti_root_off;

	*toh = btr_tcx2hdl(tcx);
	return 0;
 failed:
	btr_context_decref(tcx);
	return rc;
}

static int
btr_tree_init(struct btr_context *tcx, struct btr_root *root)
{
	return btr_root_init(tcx, root, true);
}

static int
btr_tx_tree_init(struct btr_context *tcx, struct btr_root *root)
{
	int		      rc = 0;

	rc = btr_tx_begin(tcx);
	if (rc != 0)
		return rc;

	rc = btr_tree_init(tcx, root);

	return btr_tx_end(tcx, rc);
}

int
dbtree_create_inplace(unsigned int tree_class, uint64_t tree_feats,
		      unsigned int tree_order, struct umem_attr *uma,
		      struct btr_root *root, daos_handle_t *toh)
{
	return dbtree_create_inplace_ex(tree_class, tree_feats, tree_order,
					uma, root, DAOS_HDL_INVAL, NULL, toh);
}

int
dbtree_create_inplace_ex(unsigned int tree_class, uint64_t tree_feats,
			 unsigned int tree_order, struct umem_attr *uma,
			 struct btr_root *root, daos_handle_t coh,
			 void *priv, daos_handle_t *toh)
{
	struct btr_context *tcx;
	int		    rc;

	if (tree_order < BTR_ORDER_MIN || tree_order > BTR_ORDER_MAX) {
		D_DEBUG(DB_TRACE, "Order (%d) should be between %d and %d\n",
			tree_order, BTR_ORDER_MIN, BTR_ORDER_MAX);
		return -DER_INVAL;
	}

	if (root->tr_class != 0) {
		D_DEBUG(DB_TRACE,
			"Tree existed, c=%d, o=%d, d=%d, f="DF_U64"\n",
			root->tr_class, root->tr_order, root->tr_depth,
			root->tr_feats);
		return -DER_NO_PERM;
	}

	rc = btr_context_create(BTR_ROOT_NULL, root, tree_class, tree_feats,
				tree_order, uma, coh, priv, &tcx);
	if (rc != 0)
		return rc;

	rc = btr_tx_tree_init(tcx, root);

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
 * \param root_off	[IN]	umem offset of the tree root.
 * \param uma		[IN]	Memory class attributes.
 * \param toh		[OUT]	Returned tree open handle.
 */
int
dbtree_open(umem_off_t root_off, struct umem_attr *uma,
	    daos_handle_t *toh)
{
	struct btr_context *tcx;
	int		    rc;

	rc = btr_context_create(root_off, NULL, -1, -1, -1, uma,
				DAOS_HDL_INVAL, NULL, &tcx);
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
 * \param coh		[IN]	The container open handle.
 * \param priv		[IN]	Private data for tree opener
 * \param toh		[OUT]	Returned tree open handle.
 */
int
dbtree_open_inplace_ex(struct btr_root *root, struct umem_attr *uma,
		       daos_handle_t coh, void *priv, daos_handle_t *toh)
{
	struct btr_context *tcx;
	int		    rc;

	if (root->tr_order == 0) {
		D_DEBUG(DB_TRACE, "Nonexistent tree\n");
		return -DER_NONEXIST;
	}

	rc = btr_context_create(BTR_ROOT_NULL, root, -1, -1, -1, uma,
				coh, priv, &tcx);
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
	return dbtree_open_inplace_ex(root, uma, DAOS_HDL_INVAL, NULL, toh);
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
static int
btr_node_destroy(struct btr_context *tcx, umem_off_t nd_off,
		 void *args, bool *empty_rc)
{
	struct btr_node *nd	= btr_off2ptr(tcx, nd_off);
	bool		 leaf	= btr_node_is_leaf(tcx, nd_off);
	bool		 empty	= true;
	int		 rc;
	int		 i;

	/* NB: don't need to call TX_ADD_RANGE(nd_off, ...) because I never
	 * change it so nothing to undo on transaction failure, I may destroy
	 * it later by calling TX_FREE which is transactional safe.
	 */
	D_DEBUG(DB_TRACE, "Destroy tree %s "DF_X64", keyn %d\n",
		leaf ? "leaf" : "node", nd_off, nd->tn_keyn);

	if (leaf) {
		for (i = nd->tn_keyn - 1; i >= 0; i--) {
			struct btr_record *rec;

			rec = btr_node_rec_at(tcx, nd_off, i);
			rc = btr_rec_free(tcx, rec, args);
			if (rc != 0)
				return rc;

			if (!tcx->tc_creds_on)
				continue;

			/* NB: only leaf record consumes user credits */
			D_ASSERT(tcx->tc_creds > 0);
			tcx->tc_creds--;
			if (tcx->tc_creds == 0) {
				empty = (i == 0);
				break;
			}
		}
	} else { /* non-leaf */
		for (i = nd->tn_keyn; i >= 0; i--) {
			umem_off_t	child_off;

			child_off = btr_node_child_at(tcx, nd_off, i);
			rc = btr_node_destroy(tcx, child_off, args, &empty);
			if (rc != 0)
				return rc;

			if (!tcx->tc_creds_on || tcx->tc_creds > 0) {
				D_ASSERT(empty);
				continue;
			}
			D_ASSERT(tcx->tc_creds == 0);

			/* current child is empty, other children are not */
			if (empty && i > 0) {
				empty = false;
				i--;
			}
			break;
		}
	}

	if (empty) {
		rc = btr_node_free(tcx, nd_off);
		if (rc != 0)
			return rc;
	} else {
		if (btr_has_tx(tcx)) {
			rc = btr_node_tx_add(tcx, nd_off);
			if (rc != 0)
				return rc;
		}

		/* NB: i can be zero for non-leaf node */
		D_ASSERT(i >= 0);
		nd->tn_keyn = i;
	}

	if (empty_rc)
		*empty_rc = empty;

	return 0;
}

/** destroy all tree nodes and records, then release the root */
static int
btr_tree_destroy(struct btr_context *tcx, void *args, bool *destroyed)
{
	struct btr_root *root;
	bool		 empty = true;
	int		 rc = 0;

	D_DEBUG(DB_TRACE, "Destroy "DF_X64", order %d\n",
		tcx->tc_tins.ti_root_off, tcx->tc_order);

	root = tcx->tc_tins.ti_root;
	if (root && !UMOFF_IS_NULL(root->tr_node)) {
		/* destroy the root and all descendants */
		rc = btr_node_destroy(tcx, root->tr_node, args, &empty);
	}
	*destroyed = empty;
	if (!rc && empty)
		rc = btr_root_free(tcx);

	return rc;
}

static int
btr_tx_tree_destroy(struct btr_context *tcx, void *args, bool *destroyed)
{
	int      rc = 0;

	rc = btr_tx_begin(tcx);
	if (rc != 0)
		return rc;
	rc = btr_tree_destroy(tcx, args, destroyed);

	return btr_tx_end(tcx, rc);
}

/**
 * Destroy a btree.
 * The tree open handle is invalid after the destroy.
 *
 * \param toh	[IN]	Tree open handle.
 * \param args	[IN]	user parameter for btr_ops_t::to_rec_free
 */
int
dbtree_destroy(daos_handle_t toh, void *args)
{
	struct btr_context *tcx;
	bool		    destroyed;
	int		    rc;

	tcx = btr_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	D_ASSERT(!tcx->tc_creds_on);
	rc = btr_tx_tree_destroy(tcx, args, &destroyed);
	D_ASSERT(rc || destroyed);

	btr_context_decref(tcx);
	return rc;
}

/**
 * This function drains key/values from the tree, each time it deletes a KV
 * pair, it consumes a @credits, which is input parameter of this function.
 * It returns if all input credits are consumed, or the tree is empty, in
 * the later case, it also destroys the btree.
 *
 * \param toh		[IN]	 Tree open handle.
 * \param credits	[IN/OUT] Input and returned drain credits
 * \param args		[IN]	 user parameter for btr_ops_t::to_rec_free
 * \param destroy	[OUT]	 Tree is empty and destroyed
 */
int
dbtree_drain(daos_handle_t toh, int *credits, void *args, bool *destroyed)
{
	struct btr_context *tcx;
	int		    rc;

	tcx = btr_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	D_ASSERT(!tcx->tc_creds_on);
	if (credits) {
		if (*credits <= 0) {
			rc = -DER_INVAL;
			goto failed;
		}
		tcx->tc_creds = *credits;
		tcx->tc_creds_on = 1;
	}

	rc = btr_tx_tree_destroy(tcx, args, destroyed);
	if (rc)
		goto failed;

	if (tcx->tc_creds_on)
		*credits = tcx->tc_creds;
failed:
	tcx->tc_creds_on = 0;
	tcx->tc_creds = 0;
	return rc;
}

/**** Iterator APIs *********************************************************/

/**
 * Initialize iterator.
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
			D_DEBUG(DB_TRACE,
				"The embedded iterator is in use\n");
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
 * Finalize iterator.
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
 * \param ih	[IN]	The iterator handle.
 * \param opc	[IN]	Probe opcode, see dbtree_probe_opc_t for the details.
 * \param intent [IN]	The operation intent.
 * \param key	[IN]	The key to probe, it will be ignored if opc is
 *			BTR_PROBE_FIRST or BTR_PROBE_LAST.
 * \param anchor [IN]	the anchor point to probe, it will be ignored if
 *			\a key is provided.
 * \note		If opc is not BTR_PROBE_FIRST or BTR_PROBE_LAST,
 *			key or anchor is required.
 */
int
dbtree_iter_probe(daos_handle_t ih, dbtree_probe_opc_t opc, uint32_t intent,
		  d_iov_t *key, daos_anchor_t *anchor)
{
	struct btr_iterator *itr;
	struct btr_context  *tcx;
	int		     rc;

	D_DEBUG(DB_TRACE, "probe(%d) key or anchor\n", opc);

	tcx = btr_hdl2tcx(ih);
	if (tcx == NULL)
		return -DER_NO_HDL;

	if (key) {
		rc = btr_verify_key(tcx, key);
		if (rc)
			return rc;
	}

	itr = &tcx->tc_itr;
	if (itr->it_state < BTR_ITR_INIT)
		return -DER_NO_HDL;

	if (opc == BTR_PROBE_FIRST || opc == BTR_PROBE_LAST)
		rc = btr_probe(tcx, opc, intent, NULL, NULL);
	else if (btr_is_direct_key(tcx)) {
		D_ASSERT(key != NULL || anchor != NULL);
		if (key)
			rc = btr_probe(tcx, opc, intent, key, NULL);
		else {
			d_iov_t direct_key;

			btr_key_decode(tcx, &direct_key, anchor);
			rc = btr_probe(tcx, opc, intent, &direct_key, NULL);
		}
	} else {
		D_ASSERT(key != NULL || anchor != NULL);
		char hkey[DAOS_HKEY_MAX];

		if (key)
			btr_hkey_gen(tcx, key, hkey);
		else
			btr_hkey_copy(tcx, hkey, (char *)&anchor->da_buf[0]);
		rc = btr_probe(tcx, opc, intent, key, hkey);
	}

	switch (rc) {
	case PROBE_RC_INPROGRESS:
		itr->it_state = BTR_ITR_FINI;
		return -DER_INPROGRESS;
	case PROBE_RC_DATA_LOSS:
		itr->it_state = BTR_ITR_FINI;
		return -DER_DATA_LOSS;
	case PROBE_RC_NONE:
	case PROBE_RC_ERR:
		itr->it_state = BTR_ITR_FINI;
		return -DER_NONEXIST;
	default:
		break;
	}

	itr->it_state = BTR_ITR_READY;
	return 0;
}

static int
btr_iter_is_ready(struct btr_iterator *iter)
{

	if (likely(iter->it_state == BTR_ITR_READY))
		return 0;

	switch (iter->it_state) {
	case BTR_ITR_NONE:
	case BTR_ITR_INIT:
		return -DER_NO_PERM;
	case BTR_ITR_FINI:
		return -DER_NONEXIST;
	default:
		D_ASSERT(0);
	}
	return 0;
}

static int
btr_iter_move(daos_handle_t ih, bool forward)
{
	struct btr_context  *tcx;
	struct btr_iterator *itr;
	bool		     found;
	int		     rc;

	tcx = btr_hdl2tcx(ih);
	if (tcx == NULL)
		return -DER_NO_HDL;

	itr = &tcx->tc_itr;
	rc = btr_iter_is_ready(itr);
	if (rc != 0)
		return rc;

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
 * \param ih	[IN]	Iterator open handle.
 * \param key	[OUT]	Sink buffer for the returned key, the key address is
 *			returned if buffer address is NULL.
 * \param val	[OUT]	Sink buffer for the returned value, the value address
 *			is returned if buffer address is NULL.
 * \param anchor [OUT]	Returned iteration anchor.
 */
int
dbtree_iter_fetch(daos_handle_t ih, d_iov_t *key,
		  d_iov_t *val, daos_anchor_t *anchor)
{
	struct btr_context  *tcx;
	struct btr_record   *rec;
	int		     rc;

	tcx = btr_hdl2tcx(ih);
	if (tcx == NULL)
		return -DER_NO_HDL;

	rc = btr_iter_is_ready(&tcx->tc_itr);
	if (rc != 0)
		return rc;

	rec = btr_trace2rec(tcx, tcx->tc_depth - 1);
	if (rec == NULL)
		return -DER_AGAIN; /* invalid cursor */

	rc = btr_rec_fetch(tcx, rec, key, val);
	if (rc)
		return rc;

	if (!anchor)
		return 0;

	if (btr_is_direct_key(tcx)) {
		btr_key_encode(tcx, key, anchor);
		anchor->da_type = DAOS_ANCHOR_TYPE_KEY;

	} else {
		btr_hkey_copy(tcx, (char *)&anchor->da_buf[0],
			      &rec->rec_hkey[0]);
		anchor->da_type = DAOS_ANCHOR_TYPE_HKEY;
	}

	return 0;
}


/** Encode the anchor from a known key
 *
 * \param[in]	toh	Tree open handle
 * \param[in]	key	The key to encode
 * \param[out]	anchor	Encoded anchor
 *
 * \return	0 on success, error otherwise
 */
int
dbtree_key2anchor(daos_handle_t toh, d_iov_t *key, daos_anchor_t *anchor)
{
	char hkey[DAOS_HKEY_MAX];
	struct btr_context  *tcx;
	int rc;

	D_ASSERT(key != NULL);
	D_ASSERT(anchor != NULL);

	tcx = btr_hdl2tcx(toh);
	if (tcx == NULL)
		return -DER_NO_HDL;

	rc = btr_verify_key(tcx, key);
	if (rc)
		return rc;

	if (btr_is_direct_key(tcx)) {
		btr_key_encode(tcx, key, anchor);
		anchor->da_type = DAOS_ANCHOR_TYPE_KEY;

	} else {
		btr_hkey_gen(tcx, key, hkey);
		btr_hkey_copy(tcx, (char *)&anchor->da_buf[0], &hkey[0]);
		anchor->da_type = DAOS_ANCHOR_TYPE_HKEY;
	}

	return 0;
}

/**
 * Delete the record pointed by the current iterating cursor. This function
 * will reset iterator before return, it means that caller should call
 * dbtree_iter_probe() again to reinitialize the iterator.
 *
 * \param ih		[IN]	Iterator open handle.
 * \param value_out	[OUT]	Optional, buffer to preserve value while
 *				deleting btree node.
 */
int
dbtree_iter_delete(daos_handle_t ih, void *args)
{
	struct btr_iterator *itr;
	struct btr_context  *tcx;
	int		     rc;

	D_DEBUG(DB_TRACE, "Current iterator\n");

	tcx = btr_hdl2tcx(ih);
	if (tcx == NULL)
		return -DER_NO_HDL;

	itr = &tcx->tc_itr;
	rc = btr_iter_is_ready(itr);
	if (rc != 0)
		return rc;

	rc = btr_tx_delete(tcx, args);

	/* reset iterator */
	itr->it_state = BTR_ITR_INIT;
	return rc;
}

/**
 * Is the btree iterator empty or not
 *
 * \return	0	Not empty
 *		1	Empty
 *		-ve	error code
 */
int
dbtree_iter_empty(daos_handle_t ih)
{
	struct btr_context *tcx;

	tcx = btr_hdl2tcx(ih);
	if (tcx == NULL)
		return -DER_NO_HDL;

	return tcx->tc_tins.ti_root->tr_depth == 0;
}

/**
 * Helper function to iterate a dbtree, either from the first record forward
 * (\a backward == false) or from the last record backward (\a backward ==
 * true). \a cb will be called with \a arg for each record. See also
 * dbtree_iterate_cb_t.
 *
 * \param toh		[IN]	Tree open handle
 * \param intent	[IN]	The operation intent
 * \param backward	[IN]	If true, iterate from last to first
 * \param cb		[IN]	Callback function (see dbtree_iterate_cb_t)
 * \param arg		[IN]	Callback argument
 */
int
dbtree_iterate(daos_handle_t toh, uint32_t intent, bool backward,
	       dbtree_iterate_cb_t cb, void *arg)
{
	daos_handle_t	ih;
	int		niterated = 0;
	int		rc;

	rc = dbtree_iter_prepare(toh, 0 /* options */, &ih);
	if (rc != 0) {
		D_ERROR("failed to prepare tree iterator: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = dbtree_iter_probe(ih, backward ? BTR_PROBE_LAST : BTR_PROBE_FIRST,
			       intent, NULL /* key */, NULL /* anchor */);
	if (rc == -DER_NONEXIST) {
		D_GOTO(out_iter, rc = 0);
	} else if (rc != 0) {
		D_ERROR("failed to initialize iterator: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_iter, rc);
	}

	for (;;) {
		d_iov_t	key;
		d_iov_t	val;

		d_iov_set(&key, NULL /* buf */, 0 /* size */);
		d_iov_set(&val, NULL /* buf */, 0 /* size */);

		rc = dbtree_iter_fetch(ih, &key, &val, NULL /* anchor */);
		if (rc != 0) {
			D_ERROR("failed to fetch iterator: "DF_RC"\n",
				DP_RC(rc));
			break;
		}

		/*
		 * Might want to allow cb() to end the iteration without
		 * returning an error in the future.
		 */
		rc = cb(ih, &key, &val, arg);
		niterated++;
		if (rc != 0) {
			if (rc == 1)
				/* Stop without errors. */
				rc = 0;
			break;
		}

		if (backward)
			rc = dbtree_iter_prev(ih);
		else
			rc = dbtree_iter_next(ih);
		if (rc == -DER_NONEXIST) {
			rc = 0;
			break;
		} else if (rc != 0) {
			D_ERROR("failed to move iterator: "DF_RC"\n",
				DP_RC(rc));
			break;
		}
	}

out_iter:
	dbtree_iter_finish(ih);
out:
	D_DEBUG(DB_TRACE, "iterated %d records: "DF_RC"\n", niterated,
		DP_RC(rc));
	return rc;
}

#define BTR_TYPE_MAX	1024

static struct btr_class btr_class_registered[BTR_TYPE_MAX];

/**
 * Initialize a tree instance from a registered tree class.
 */
static int
btr_class_init(umem_off_t root_off, struct btr_root *root,
	       unsigned int tree_class, uint64_t *tree_feats,
	       struct umem_attr *uma, daos_handle_t coh, void *priv,
	       struct btr_instance *tins)
{
	struct btr_class	*tc;
	uint64_t		 special_feat;
	int			 rc;

	memset(tins, 0, sizeof(*tins));
	rc = umem_class_init(uma, &tins->ti_umm);
	if (rc != 0)
		return rc;

	tins->ti_priv = priv;
	tins->ti_coh = coh;
	tins->ti_root_off = UMOFF_NULL;

	if (!UMOFF_IS_NULL(root_off)) {
		tins->ti_root_off = root_off;
		if (root == NULL)
			root = umem_off2ptr(&tins->ti_umm, root_off);
	}
	tins->ti_root = root;

	if (root != NULL && root->tr_class != 0) {
		tree_class = root->tr_class;
		*tree_feats = root->tr_feats;
	}

	/* XXX should be multi-thread safe */
	if (tree_class >= BTR_TYPE_MAX) {
		D_DEBUG(DB_TRACE, "Invalid class id: %d\n", tree_class);
		return -DER_INVAL;
	}

	tc = &btr_class_registered[tree_class];
	if (tc->tc_ops == NULL) {
		D_DEBUG(DB_TRACE, "Unregistered class id %d\n", tree_class);
		return -DER_NONEXIST;
	}

	/* If no hkey callbacks are supplied, only special key types are
	 * supported.  Rather than flagging an error just set the
	 * appropriate flag.
	 */
	special_feat = tc->tc_feats & (BTR_FEAT_UINT_KEY | BTR_FEAT_DIRECT_KEY);
	if (!(special_feat & *tree_feats) &&
	    (tc->tc_ops->to_hkey_gen == NULL ||
	     tc->tc_ops->to_hkey_size == NULL)) {
		D_DEBUG(DB_TRACE, "Setting feature "DF_X64" required"
			" by tree class %d", special_feat, tree_class);
		*tree_feats |= special_feat;
	}

	if (tc->tc_feats & BTR_FEAT_DYNAMIC_ROOT)
		*tree_feats |= BTR_FEAT_DYNAMIC_ROOT;

	if (tc->tc_feats & BTR_FEAT_SKIP_LEAF_REBAL)
		*tree_feats |= BTR_FEAT_SKIP_LEAF_REBAL;

	/** Only check btree managed bits */
	if ((*tree_feats & tc->tc_feats) != (*tree_feats & BTR_FEAT_MASK)) {
		D_ERROR("Unsupported features "DF_X64"/"DF_X64"\n",
			*tree_feats, tc->tc_feats);
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
	if (btr_class_registered[tree_class].tc_ops != NULL) {
		if (btr_class_registered[tree_class].tc_ops != ops ||
		    btr_class_registered[tree_class].tc_feats != tree_feats)
			return -DER_EXIST;
		return 0;
	}

	/* These are mandatory functions */
	D_ASSERT(ops != NULL);
	if (!(tree_feats & (BTR_FEAT_UINT_KEY | BTR_FEAT_DIRECT_KEY))) {
		D_ASSERT(ops->to_hkey_gen != NULL);
		D_ASSERT(ops->to_hkey_size != NULL);
	}
	if (tree_feats & BTR_FEAT_DIRECT_KEY) {
		D_ASSERT(ops->to_key_cmp != NULL);
		D_ASSERT(ops->to_key_encode != NULL);
		D_ASSERT(ops->to_key_decode != NULL);
	}
	D_ASSERT(ops->to_rec_fetch != NULL);
	D_ASSERT(ops->to_rec_alloc != NULL);
	D_ASSERT(ops->to_rec_free != NULL);

	btr_class_registered[tree_class].tc_ops = ops;
	btr_class_registered[tree_class].tc_feats = tree_feats;

	return 0;
}

int
dbtree_overhead_get(int alloc_overhead, unsigned int tclass, uint64_t otype,
		    int tree_order, struct daos_tree_overhead *ovhd)
{
	btr_ops_t		*ops;
	struct btr_class	*btr_class;
	size_t			 hkey_size;
	size_t			 btr_size;
	int			 order_idx;
	int			 order;

	if (ovhd == NULL) {
		D_ERROR("Invalid ovhd argument\n");
		return -DER_INVAL;
	}

	if (tclass >= BTR_TYPE_MAX) {
		D_ERROR("Invalid class id: %d\n", tclass);
		return -DER_INVAL;
	}

	btr_class = &btr_class_registered[tclass];
	ops = btr_class->tc_ops;

	if (ops->to_rec_msize == NULL) {
		D_ERROR("No record meta size callback for tree class: %d\n",
			tclass);
		return -DER_INVAL;
	}

	hkey_size = btr_hkey_size_const(ops, otype);
	btr_size = sizeof(struct btr_record) + hkey_size;

	ovhd->to_record_msize = ops->to_rec_msize(alloc_overhead);
	ovhd->to_node_rec_msize = btr_size;

	ovhd->to_leaf_overhead.no_order = tree_order;
	ovhd->to_leaf_overhead.no_size = alloc_overhead +
		sizeof(struct btr_node) + btr_size * tree_order;
	ovhd->to_int_node_size = ovhd->to_leaf_overhead.no_size;

	order_idx = 0;

	if ((btr_class->tc_feats & BTR_FEAT_DYNAMIC_ROOT) == 0)
		goto done;

	order = 1;
	while (order != tree_order) {
		ovhd->to_dyn_overhead[order_idx].no_order = order;
		ovhd->to_dyn_overhead[order_idx].no_size = alloc_overhead +
			sizeof(struct btr_node) + btr_size * order;
		order_idx++;
		order = MIN(order * 2 + 1, tree_order);
	}

done:
	ovhd->to_dyn_count = order_idx;

	return 0;
}

