/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * VOS Object/Key incarnation log
 * vos/ilog.c
 *
 * Author: Jeff Olivier <jeffrey.v.olivier@intel.com>
 */
#define D_LOGFAC DD_FAC(vos)
#include <daos/common.h>
#include <daos_srv/vos.h>
#include <daos/btree.h>
#include "vos_internal.h"
#include "vos_layout.h"
#include "vos_ts.h"
#include "ilog.h"

#define ILOG_TREE_ORDER 11

enum {
	ILOG_ITER_NONE,
	ILOG_ITER_INIT,
	ILOG_ITER_READY,
	ILOG_ITER_FINI,
};

/** The ilog is split into two parts.   If there is one entry, the ilog
 *  is embedded into the root df struct.   If not, a b+tree is used.
 *  The tree is used more like a set where only the key is used.
 */

struct ilog_tree {
	umem_off_t	it_root;
	uint64_t	it_embedded;
};

struct ilog_array {
	/** Current length of array */
	uint32_t	ia_len;
	/** Allocated length of array */
	uint32_t	ia_max_len;
	/** Pad to 16 bytes */
	uint64_t	ia_pad;
	/** Entries in array */
	struct ilog_id	ia_id[0];
};

struct ilog_array_cache {
	/** Pointer to entries */
	struct ilog_id		*ac_entries;
	/** Pointer to array, if applicable */
	struct ilog_array	*ac_array;
	/** Number of entries */
	uint32_t		 ac_nr;
};

struct ilog_root {
	union {
		struct ilog_id		lr_id;
		struct ilog_tree	lr_tree;
	};
	uint32_t			lr_ts_idx;
	uint32_t			lr_magic;
};

struct ilog_context {
	/** Root pointer */
	struct ilog_root		*ic_root;
	/** Cache the callbacks */
	struct ilog_desc_cbs		 ic_cbs;
	/** umem offset of root pointer */
	umem_off_t			 ic_root_off;
	/** umem instance */
	struct umem_instance		 ic_umm;
	/** ref count for iterator */
	uint32_t			 ic_ref;
	/** In pmdk transaction marker */
	bool				 ic_in_txn;
	/** version needs incrementing */
	bool				 ic_ver_inc;
};

D_CASSERT(sizeof(struct ilog_id) == sizeof(struct ilog_tree));
D_CASSERT(sizeof(struct ilog_root) == sizeof(struct ilog_df));

/**
 * Customized functions for btree.
 */
static inline int
ilog_is_same_tx(struct ilog_context *lctx, const struct ilog_id *id, bool *same)
{
	struct ilog_desc_cbs	*cbs = &lctx->ic_cbs;

	*same = true;

	if (!cbs->dc_is_same_tx_cb)
		return 0;

	return cbs->dc_is_same_tx_cb(&lctx->ic_umm, id->id_tx_id, id->id_epoch, same,
				     cbs->dc_is_same_tx_args);
}

static int
ilog_status_get(struct ilog_context *lctx, const struct ilog_id *id, uint32_t intent)
{
	struct ilog_desc_cbs	*cbs = &lctx->ic_cbs;
	int			 rc;

	if (id->id_tx_id == UMOFF_NULL)
		return ILOG_COMMITTED;

	if (!cbs->dc_log_status_cb)
		return ILOG_COMMITTED;

	rc = cbs->dc_log_status_cb(&lctx->ic_umm, id->id_tx_id, id->id_epoch, intent,
				   cbs->dc_log_status_args);

	if ((intent == DAOS_INTENT_UPDATE || intent == DAOS_INTENT_PUNCH)
	    && rc == -DER_INPROGRESS)
		return ILOG_UNCOMMITTED;

	return rc;
}

static inline int
ilog_log_add(struct ilog_context *lctx, struct ilog_id *id)
{
	struct ilog_desc_cbs	*cbs = &lctx->ic_cbs;
	int			 rc;

	if (!cbs->dc_log_add_cb)
		return 0;

	rc = cbs->dc_log_add_cb(&lctx->ic_umm, lctx->ic_root_off, &id->id_tx_id,
				id->id_epoch, cbs->dc_log_add_args);
	if (rc != 0) {
		D_ERROR("Failed to register incarnation log entry: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	D_DEBUG(DB_TRACE, "Registered ilog="DF_X64" epoch="DF_X64" lid=%d\n",
		lctx->ic_root_off, id->id_epoch, id->id_tx_id);

	return 0;
}

static inline int
ilog_log_del(struct ilog_context *lctx, const struct ilog_id *id,
	     bool deregister)
{
	struct ilog_desc_cbs	*cbs = &lctx->ic_cbs;
	int			 rc;

	if (!cbs->dc_log_del_cb || !id->id_tx_id)
		return 0;

	rc = cbs->dc_log_del_cb(&lctx->ic_umm, lctx->ic_root_off, id->id_tx_id,
				id->id_epoch, deregister, cbs->dc_log_del_args);
	if (rc != 0) {
		D_ERROR("Failed to deregister incarnation log entry: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	D_DEBUG(DB_TRACE, "%s ilog="DF_X64" epoch="DF_X64
		" lid=%d\n", deregister ? "Deregistered" : "Removed",
		lctx->ic_root_off, id->id_epoch, id->id_tx_id);

	return 0;
}

int
ilog_init(void)
{
	return 0;
}

/* 4 bit magic number + version */
#define ILOG_MAGIC		0x00000006
#define ILOG_MAGIC_BITS		4
#define ILOG_MAGIC_MASK		((1 << ILOG_MAGIC_BITS) - 1)
#define ILOG_VERSION_INC	(1 << ILOG_MAGIC_BITS)
#define ILOG_VERSION_MASK	~(ILOG_VERSION_INC - 1)
#define ILOG_MAGIC_VALID(magic)	(((magic) & ILOG_MAGIC_MASK) == ILOG_MAGIC)

static inline uint32_t
ilog_mag2ver(uint32_t magic) {
	if (!ILOG_MAGIC_VALID(magic))
		return 0;

	return (magic & ILOG_VERSION_MASK) >> ILOG_MAGIC_BITS;
}

/** Increment the version of the log.   The object tree in particular can
 *  benefit from cached state of the tree.  In order to detect when to
 *  update the case, we keep a version.
 */
static inline uint32_t
ilog_ver_inc(struct ilog_context *lctx)
{
	uint32_t        magic = lctx->ic_root->lr_magic;

	D_ASSERT(ILOG_MAGIC_VALID(magic));

	if ((magic & ILOG_VERSION_MASK) == ILOG_VERSION_MASK)
		magic = (magic & ~ILOG_VERSION_MASK) + ILOG_VERSION_INC;
	else
		magic += ILOG_VERSION_INC;

	/* This is only called when we will persist the new version so no need
	* to update the version when finishing the transaction.
	*/
	lctx->ic_ver_inc = false;

	return magic;
}

/** Called when we know a txn is needed.  Subsequent calls are a noop. */
static inline int
ilog_tx_begin(struct ilog_context *lctx)
{
	int	rc = 0;

	if (lctx->ic_in_txn)
		return 0;

	rc = umem_tx_begin(&lctx->ic_umm, NULL);
	if (rc != 0)
		return rc;

	lctx->ic_in_txn = true;
	lctx->ic_ver_inc = true;
	return 0;
}

/** Only invokes transaction end if we've started a txn */
static inline int
ilog_tx_end(struct ilog_context *lctx, int rc)
{
	if (!lctx->ic_in_txn)
		return rc;

	if (rc != 0)
		goto done;

	if (lctx->ic_ver_inc) {
		rc = umem_tx_add_ptr(&lctx->ic_umm, &lctx->ic_root->lr_magic,
				     sizeof(lctx->ic_root->lr_magic));
		if (rc != 0) {
			D_ERROR("Failed to add to undo log: "DF_RC"\n",
				DP_RC(rc));
			goto done;
		}

		lctx->ic_root->lr_magic = ilog_ver_inc(lctx);
	}

done:
	lctx->ic_in_txn = false;
	return umem_tx_end(&lctx->ic_umm, rc);
}

static inline bool
ilog_empty(struct ilog_root *root)
{
	return !root->lr_tree.it_embedded &&
		root->lr_tree.it_root == UMOFF_NULL;
}

static void
ilog_addref(struct ilog_context *lctx)
{
	lctx->ic_ref++;
}

static void
ilog_decref(struct ilog_context *lctx)
{
	lctx->ic_ref--;
	if (lctx->ic_ref == 0)
		D_FREE(lctx);
}

static int
ilog_ctx_create(struct umem_instance *umm, struct ilog_root *root,
		const struct ilog_desc_cbs *cbs, struct ilog_context **lctxp)
{
	D_ALLOC_PTR(*lctxp);
	if (*lctxp == NULL) {
		D_ERROR("Could not allocate memory for open incarnation log\n");
		return -DER_NOMEM;
	}

	(*lctxp)->ic_root = root;
	(*lctxp)->ic_root_off = umem_ptr2off(umm, root);
	(*lctxp)->ic_umm = *umm;
	(*lctxp)->ic_cbs = *cbs;
	ilog_addref(*lctxp);
	return 0;
}

static daos_handle_t
ilog_lctx2hdl(struct ilog_context *lctx)
{
	daos_handle_t	hdl;

	hdl.cookie = (uint64_t)lctx;

	return hdl;
}

static struct ilog_context *
ilog_hdl2lctx(daos_handle_t hdl)
{
	struct ilog_context	*lctx;

	if (daos_handle_is_inval(hdl))
		return NULL;

	lctx = (struct ilog_context *)hdl.cookie;

	if (!ILOG_MAGIC_VALID(lctx->ic_root->lr_magic))
		return NULL;

	return lctx;
}

static int
ilog_ptr_set_full(struct ilog_context *lctx, void *dest, const void *src,
		  size_t len)
{
	int	rc = 0;

	rc = ilog_tx_begin(lctx);
	if (rc != 0) {
		D_ERROR("Failed to start PMDK transaction: rc = %s\n",
			d_errstr(rc));
		goto done;
	}

	rc = umem_tx_add_ptr(&lctx->ic_umm, dest, len);
	if (rc != 0) {
		D_ERROR("Failed to add to undo log\n");
		goto done;
	}

	memcpy(dest, src, len);
done:
	return rc;
}

#define ilog_ptr_set(lctx, dest, src)	\
	ilog_ptr_set_full(lctx, dest, src, sizeof(*(src)))

int
ilog_create(struct umem_instance *umm, struct ilog_df *root)
{
	struct ilog_context	lctx = {
		.ic_root = (struct ilog_root *)root,
		.ic_root_off = umem_ptr2off(umm, root),
		.ic_umm = *umm,
		.ic_ref = 0,
		.ic_in_txn = 0,
	};
	struct ilog_root	tmp = {0};
	int			rc = 0;

	tmp.lr_magic = ILOG_MAGIC + ILOG_VERSION_INC;

	rc = ilog_ptr_set(&lctx, root, &tmp);
	lctx.ic_ver_inc = false;

	rc = ilog_tx_end(&lctx, rc);
	return rc;
}

#define ILOG_ASSERT_VALID(root_df)					\
	do {								\
		struct ilog_root	*_root;				\
									\
		_root = (struct ilog_root *)(root_df);			\
		D_ASSERTF((_root != NULL) &&				\
			  ILOG_MAGIC_VALID(_root->lr_magic),		\
			  "Invalid ilog root detected %p magic=%#x\n",	\
			  _root, _root == NULL ? 0 : _root->lr_magic);	\
	} while (0)

int
ilog_open(struct umem_instance *umm, struct ilog_df *root,
	  const struct ilog_desc_cbs *cbs, daos_handle_t *loh)
{
	struct ilog_context	*lctx;
	int			 rc;

	ILOG_ASSERT_VALID(root);

	rc = ilog_ctx_create(umm, (struct ilog_root *)root, cbs, &lctx);
	if (rc != 0)
		return rc;

	*loh = ilog_lctx2hdl(lctx);

	return 0;
}

int
ilog_close(daos_handle_t loh)
{
	struct ilog_context *lctx = ilog_hdl2lctx(loh);

	D_ASSERTF(lctx != NULL,
		  "Trying to close invalid incarnation log handle\n");
	if (lctx == NULL)
		return -DER_INVAL;

	ilog_decref(lctx);

	return 0;
}

static void
ilog_log2cache(struct ilog_context *lctx, struct ilog_array_cache *cache)
{
	struct ilog_array	*array;

	if (ilog_empty(lctx->ic_root)) {
		cache->ac_entries = NULL;
		cache->ac_array = NULL;
		cache->ac_nr = 0;
	} else if (!lctx->ic_root->lr_tree.it_embedded) {
		array = umem_off2ptr(&lctx->ic_umm, lctx->ic_root->lr_tree.it_root);
		cache->ac_array = array;
		cache->ac_entries = &array->ia_id[0];
		cache->ac_nr = array->ia_len;
	} else {
		cache->ac_entries = &lctx->ic_root->lr_id;
		cache->ac_nr = 1;
		cache->ac_array = NULL;
	}
}


int
ilog_destroy(struct umem_instance *umm,
	     struct ilog_desc_cbs *cbs, struct ilog_df *root)
{
	struct ilog_context	lctx = {
		.ic_root = (struct ilog_root *)root,
		.ic_root_off = umem_ptr2off(umm, root),
		.ic_umm = *umm,
		.ic_ref = 1,
		.ic_cbs = *cbs,
		.ic_in_txn = 0,
	};
	uint32_t		 tmp = 0;
	int			 i;
	int			 rc = 0;
	struct ilog_array_cache	 cache = {0};

	ILOG_ASSERT_VALID(root);

	rc = ilog_tx_begin(&lctx);
	if (rc != 0) {
		D_ERROR("Failed to start PMDK transaction: rc = %s\n",
			d_errstr(rc));
		return rc;
	}

	/* No need to update the version on destroy */
	lctx.ic_ver_inc = false;

	rc = ilog_ptr_set(&lctx, &lctx.ic_root->lr_magic, &tmp);
	if (rc != 0)
		goto fail;

	ilog_log2cache(&lctx, &cache);

	for (i = 0; i < cache.ac_nr; i++) {
		rc = ilog_log_del(&lctx, &cache.ac_entries[i], true);
		if (rc != 0)
			goto fail;
	}

	if (cache.ac_nr > 1)
		rc = umem_free(umm, lctx.ic_root->lr_tree.it_root);

fail:
	rc = ilog_tx_end(&lctx, rc);

	return rc;
}

#define ILOG_ARRAY_INIT_NR	3
#define ILOG_ARRAY_APPEND_NR	4
#define ILOG_ARRAY_CHUNK_SIZE	64
D_CASSERT(sizeof(struct ilog_array) + sizeof(struct ilog_id) * ILOG_ARRAY_INIT_NR ==
	  ILOG_ARRAY_CHUNK_SIZE);
D_CASSERT(sizeof(struct ilog_id) * ILOG_ARRAY_APPEND_NR == ILOG_ARRAY_CHUNK_SIZE);

static int
ilog_root_migrate(struct ilog_context *lctx, const struct ilog_id *id_in)
{
	struct ilog_root	 tmp = {0};
	umem_off_t		 tree_root;
	struct ilog_root	*root;
	struct ilog_array	*array;
	int			 rc = 0;
	int			 idx;

	root = lctx->ic_root;

	rc = ilog_tx_begin(lctx);
	if (rc != 0) {
		D_ERROR("Failed to start PMDK transaction: rc = %s\n",
			d_errstr(rc));
		return rc;
	}

	tree_root = umem_zalloc(&lctx->ic_umm, ILOG_ARRAY_CHUNK_SIZE);

	if (tree_root == UMOFF_NULL)
		return lctx->ic_umm.umm_nospc_rc;

	array = umem_off2ptr(&lctx->ic_umm, tree_root);

	lctx->ic_ver_inc = true;

	if (root->lr_id.id_epoch > id_in->id_epoch)
		idx = 1;
	else
		idx = 0;

	array->ia_id[idx].id_value = root->lr_id.id_value;
	array->ia_id[idx].id_epoch = root->lr_id.id_epoch;

	idx = 1 - idx;
	array->ia_id[idx].id_value = id_in->id_value;
	array->ia_id[idx].id_epoch = id_in->id_epoch;
	array->ia_len = 2;
	array->ia_max_len = ILOG_ARRAY_INIT_NR;

	rc = ilog_log_add(lctx, &array->ia_id[idx]);
	if (rc != 0)
		return rc;

	tmp.lr_tree.it_root = tree_root;
	tmp.lr_tree.it_embedded = 0;
	tmp.lr_magic = ilog_ver_inc(lctx);
	tmp.lr_ts_idx = root->lr_ts_idx;

	return ilog_ptr_set(lctx, root, &tmp);
}

static int
check_equal(struct ilog_context *lctx, struct ilog_id *id_out, const struct ilog_id *id_in,
	    bool update, bool *is_equal)
{
	int	rc;

	*is_equal = false;

	if (id_in->id_epoch != id_out->id_epoch)
		return 0;

	if (update) {
		rc = ilog_is_same_tx(lctx, id_out, is_equal);
		if (rc != 0)
			return rc;
	} else if (id_in->id_tx_id == id_out->id_tx_id) {
		*is_equal = true;
	}

	if (!*is_equal) {
		if (!update) {
			D_DEBUG(DB_IO, "No entry found, done\n");
			return 0;
		}
		if (id_in->id_tx_id == DTX_LID_COMMITTED) {
			/** Need to differentiate between updates that are
			 * overwrites and others that are conflicts.  Return
			 * a different error code in this case if the result
			 * would be the same (e.g. not mixing update with
			 * punch
			 */
			if (id_in->id_punch_minor_eph &&
			    id_out->id_punch_minor_eph >
			    id_out->id_update_minor_eph)
				return -DER_ALREADY;

			if (id_in->id_update_minor_eph &&
			    id_out->id_update_minor_eph >
			    id_out->id_punch_minor_eph)
				return -DER_ALREADY;
		}
		D_DEBUG(DB_IO, "Access of incarnation log from multiple DTX"
			" at same time is not allowed: rc=DER_TX_RESTART\n");
		return -DER_TX_RESTART;
	}

	return 0;
}

enum {
	ILOG_OP_UPDATE,
	ILOG_OP_PERSIST,
	ILOG_OP_ABORT,
};

static int
update_inplace(struct ilog_context *lctx, struct ilog_id *id_out, const struct ilog_id *id_in,
	       int opc, bool *is_equal)
{
	struct ilog_id	saved_id;
	int		rc;

	rc = check_equal(lctx, id_out, id_in, opc == ILOG_OP_UPDATE, is_equal);
	if (rc != 0 || !*is_equal || opc == ILOG_OP_ABORT)
		return rc;

	saved_id.id_value = id_out->id_value;
	if (opc == ILOG_OP_PERSIST) {
		D_DEBUG(DB_TRACE, "Setting "DF_X64" to persistent\n",
			id_in->id_epoch);
		saved_id.id_tx_id = 0;
		goto set_id;
	}

	if (saved_id.id_punch_minor_eph > saved_id.id_update_minor_eph &&
	    id_in->id_punch_minor_eph)
		return 0; /** Already a punch */
	if (saved_id.id_update_minor_eph > saved_id.id_punch_minor_eph &&
	    id_in->id_update_minor_eph)
		return 0; /** Already an update */

	if (saved_id.id_punch_minor_eph < id_in->id_punch_minor_eph)
		saved_id.id_punch_minor_eph = id_in->id_punch_minor_eph;
	else if (saved_id.id_update_minor_eph < id_in->id_update_minor_eph)
		saved_id.id_update_minor_eph = id_in->id_update_minor_eph;

	if (saved_id.id_value == id_out->id_value)
		return 0; /* Nothing to do */

	/* New operation has a new minor epoch.  Update the old entry
	 * accordingly.
	 */
	D_DEBUG(DB_TRACE, "Updating "DF_X64
		" lid=%d punch=(%d->%d) update=(%d-%d)\n", id_in->id_epoch,
		id_out->id_tx_id, id_out->id_punch_minor_eph,
		saved_id.id_punch_minor_eph, id_out->id_update_minor_eph,
		saved_id.id_update_minor_eph);

set_id:
	if (saved_id.id_update_minor_eph == saved_id.id_punch_minor_eph) {
		D_ERROR("Matching punch/update minor epoch not allowed\n");
		return -DER_NO_PERM;
	}
	return ilog_ptr_set(lctx, &id_out->id_value, &saved_id.id_value);
}

static int
reset_root(struct ilog_context *lctx, struct ilog_array_cache *cache, int i)
{
	struct ilog_root	 tmp = {0};
	umem_off_t		 tree = UMOFF_NULL;
	int			 rc;

	rc = ilog_tx_begin(lctx);
	if (rc != 0)
		return rc;

	tmp.lr_magic = ilog_ver_inc(lctx);
	if (cache->ac_nr >= 2)
		tree = lctx->ic_root->lr_tree.it_root;


	if (i != -1) {
		tmp.lr_id.id_value = cache->ac_entries[i].id_value;
		tmp.lr_id.id_epoch = cache->ac_entries[i].id_epoch;
		tmp.lr_ts_idx = lctx->ic_root->lr_ts_idx;
	}

	rc = ilog_ptr_set(lctx, lctx->ic_root, &tmp);
	if (rc != 0)
		return rc;

	if (tree != UMOFF_NULL)
		return umem_free(&lctx->ic_umm, tree);

	return 0;
}

static int
remove_entry(struct ilog_context *lctx, struct ilog_array_cache *cache, int i)
{
	struct ilog_array	*array;
	int			 rc = 0;
	uint32_t		 new_len;

	D_ASSERT(i >= 0);

	if (cache->ac_nr == 1) {
		return reset_root(lctx, cache, -1);
	} else if (cache->ac_nr == 2) {
		/** 1 - i will keep the other entry */
		return reset_root(lctx, cache, 1 - i);
	}

	rc = ilog_tx_begin(lctx);
	if (rc != 0)
		return rc;

	/** Just remove the entry at i */
	array = cache->ac_array;
	if (i + 1 != cache->ac_nr) {
		rc = umem_tx_add_ptr(&lctx->ic_umm, &array->ia_id[i],
				     sizeof(array->ia_id[0]) * (cache->ac_nr - i));
		if (rc != 0)
			return rc;
		memmove(&array->ia_id[i], &array->ia_id[i + 1],
		       sizeof(array->ia_id[0]) * (cache->ac_nr - i));
	}

	new_len = cache->ac_nr - 1;
	return ilog_ptr_set(lctx, &array->ia_len, &new_len);
}

static int
ilog_tree_modify(struct ilog_context *lctx, const struct ilog_id *id_in,
		 const daos_epoch_range_t *epr, int opc)
{
	struct ilog_root	*root;
	daos_epoch_t		 epoch = id_in->id_epoch;
	struct ilog_id		 id = *id_in;
	struct ilog_id		*id_out;
	bool			 is_equal;
	int			 visibility = ILOG_COMMITTED;
	uint32_t		 new_len;
	size_t			 new_size;
	umem_off_t		 new_array;
	struct ilog_array	*array;
	struct ilog_array_cache	 cache;
	int			 rc = 0;
	int			 i;

	root = lctx->ic_root;

	ilog_log2cache(lctx, &cache);

	for (i = cache.ac_nr - 1; i >= 0; i--) {
		if (cache.ac_entries[i].id_epoch <= epoch)
			break;
	}

	if (i < 0) {
		if (opc != ILOG_OP_UPDATE) {
			D_DEBUG(DB_TRACE, "No entry found, done\n");
			return 0;
		}
		goto insert;
	}

	id_out = &cache.ac_entries[i];

	visibility = ILOG_UNCOMMITTED;

	if (id_out->id_epoch <= epr->epr_hi &&
	    id_out->id_epoch >= epr->epr_lo) {
		visibility = ilog_status_get(lctx, id_out, DAOS_INTENT_UPDATE);
		if (visibility < 0)
			return visibility;
	}

	rc = update_inplace(lctx, id_out, id_in, opc, &is_equal);
	if (rc != 0)
		return rc;

	if (is_equal) {
		if (opc != ILOG_OP_ABORT)
			return 0;

		return remove_entry(lctx, &cache, i);
	}

	if (opc != ILOG_OP_UPDATE) {
		D_DEBUG(DB_TRACE, "No entry found, done\n");
		return 0;
	}

	if (id_in->id_punch_minor_eph == 0 && visibility != ILOG_UNCOMMITTED &&
	    id_out->id_update_minor_eph > id_out->id_punch_minor_eph)
		return 0;
insert:
	rc = ilog_tx_begin(lctx);
	if (rc != 0)
		return rc;

	id.id_value = id_in->id_value;
	id.id_epoch = id_in->id_epoch;
	rc = ilog_log_add(lctx, &id);
	if (rc != 0)
		return rc;

	D_ASSERT(id.id_punch_minor_eph == id_in->id_punch_minor_eph);
	D_ASSERT(id.id_update_minor_eph == id_in->id_update_minor_eph);

	/* We want to insert after 'i', so just increment it */
	i++;
	if (cache.ac_nr == cache.ac_array->ia_max_len) {
		new_len = (cache.ac_nr + 1) * 2 - 1;
		new_size = sizeof(*cache.ac_array) + sizeof(cache.ac_entries[0]) * new_len;
		D_ASSERT((new_size & (ILOG_ARRAY_CHUNK_SIZE - 1)) == 0);
		new_array = umem_zalloc(&lctx->ic_umm, new_size);
		if (new_array == UMOFF_NULL)
			return lctx->ic_umm.umm_nospc_rc;

		array = umem_off2ptr(&lctx->ic_umm, new_array);
		array->ia_len = cache.ac_nr + 1;
		array->ia_max_len = new_len;
		if (i != 0) {
			/* Copy the entries before i */
			memcpy(&array->ia_id[0], &cache.ac_array->ia_id[0],
			       sizeof(array->ia_id[0]) * i);
		}

		if (i != cache.ac_nr) {
			/* Copy the entries after i */
			memcpy(&array->ia_id[i + 1], &cache.ac_array->ia_id[i],
			       sizeof(array->ia_id[0]) * (cache.ac_nr - i));
		}

		array->ia_id[i].id_value = id.id_value;
		array->ia_id[i].id_epoch = id.id_epoch;

		rc = ilog_ptr_set(lctx, &root->lr_tree.it_root, &new_array);
		if (rc != 0)
			return rc;

		return umem_free(&lctx->ic_umm, umem_ptr2off(&lctx->ic_umm, cache.ac_array));
	}

	array = cache.ac_array;
	rc = umem_tx_add_ptr(&lctx->ic_umm, &array->ia_id[i],
			     sizeof(array->ia_id[0]) * (cache.ac_nr - i + 1));
	if (rc != 0)
		return rc;

	if (i != cache.ac_nr) {
		/* Copy the entries after i */
		memmove(&array->ia_id[i + 1], &array->ia_id[i],
		       sizeof(array->ia_id[0]) * (cache.ac_nr - i));
	}

	array->ia_id[i].id_value = id.id_value;
	array->ia_id[i].id_epoch = id.id_epoch;

	new_len = cache.ac_nr + 1;
	return ilog_ptr_set(lctx, &array->ia_len, &new_len);
}

const char *opc_str[] = {
	"Update",
	"Persist",
	"Abort",
};

static int
ilog_modify(daos_handle_t loh, const struct ilog_id *id_in,
	    const daos_epoch_range_t *epr, int opc)
{
	struct ilog_context	*lctx;
	struct ilog_root	*root;
	struct ilog_root	 tmp = {0};
	int			 rc = 0;
	int			 visibility = ILOG_UNCOMMITTED;
	uint32_t		 version;

	lctx = ilog_hdl2lctx(loh);
	if (lctx == NULL) {
		D_ERROR("Invalid log handle\n");
		return -DER_INVAL;
	}

	D_ASSERT(!lctx->ic_in_txn);

	root = lctx->ic_root;

	version = ilog_mag2ver(root->lr_magic);

	D_DEBUG(DB_TRACE, "%s in incarnation log: log:"DF_X64 " epoch:" DF_X64
		" tree_version: %d\n", opc_str[opc], lctx->ic_root_off,
		id_in->id_epoch, version);

	if (root->lr_tree.it_embedded && root->lr_id.id_epoch <= epr->epr_hi
	    && root->lr_id.id_epoch >= epr->epr_lo) {
		visibility = ilog_status_get(lctx, &root->lr_id, DAOS_INTENT_UPDATE);
		if (visibility < 0) {
			rc = visibility;
			goto done;
		}
	}

	if (ilog_empty(root)) {
		if (opc != ILOG_OP_UPDATE) {
			D_DEBUG(DB_TRACE, "ilog entry "DF_X64" not found\n",
				id_in->id_epoch);
			goto done;
		}

		D_DEBUG(DB_TRACE, "Inserting "DF_X64" at ilog root\n",
			id_in->id_epoch);
		tmp.lr_magic = ilog_ver_inc(lctx);
		tmp.lr_ts_idx = root->lr_ts_idx;
		tmp.lr_id = *id_in;
		rc = ilog_ptr_set(lctx, root, &tmp);
		if (rc != 0)
			goto done;
		rc = ilog_log_add(lctx, &root->lr_id);
		if (rc != 0)
			goto done;
	} else if (root->lr_tree.it_embedded) {
		bool	is_equal;

		rc = update_inplace(lctx, &root->lr_id, id_in,
				    opc, &is_equal);
		if (rc != 0)
			goto done;

		if (is_equal) {
			if (opc == ILOG_OP_ABORT) {
				D_DEBUG(DB_TRACE, "Removing "DF_X64
					" from ilog root\n", id_in->id_epoch);
				tmp.lr_magic = ilog_ver_inc(lctx);
				rc = ilog_ptr_set(lctx, root, &tmp);
			}
			goto done;
		}

		if (opc != ILOG_OP_UPDATE) {
			D_DEBUG(DB_TRACE, "Entry "DF_X64" not found in ilog\n",
				id_in->id_epoch);
			goto done;
		}

		if (id_in->id_punch_minor_eph == 0 &&
		    root->lr_id.id_punch_minor_eph <
		    root->lr_id.id_update_minor_eph &&
		    id_in->id_epoch > root->lr_id.id_epoch &&
		    visibility == ILOG_COMMITTED) {
			D_DEBUG(DB_TRACE, "No update needed\n");
			goto done;
		}
		/* Either this entry is earlier or prior entry is uncommitted
		 * or either entry is a punch
		 */
		rc = ilog_root_migrate(lctx, id_in);
	} else {
		/** Ok, we have a tree.  Do the operation in the tree */
		rc = ilog_tree_modify(lctx, id_in, epr, opc);
	}
done:
	rc = ilog_tx_end(lctx, rc);
	D_DEBUG(DB_TRACE, "%s in incarnation log "DF_X64
		" status: rc=%s tree_version: %d\n",
		opc_str[opc], id_in->id_epoch, d_errstr(rc),
		ilog_mag2ver(lctx->ic_root->lr_magic));

	if (rc == 0 && version != ilog_mag2ver(lctx->ic_root->lr_magic) &&
	    (opc == ILOG_OP_PERSIST || opc == ILOG_OP_ABORT)) {
		/** If we persisted or aborted an entry successfully,
		 *  invoke the callback, if applicable but without
		 *  deregistration
		 */
		ilog_log_del(lctx, id_in, false);
	}
	return rc;
}

int
ilog_update(daos_handle_t loh, const daos_epoch_range_t *epr,
	    daos_epoch_t major_eph, uint16_t minor_eph, bool punch)
{
	daos_epoch_range_t	 range = {0, DAOS_EPOCH_MAX};
	struct ilog_id		 id = {
		.id_tx_id = 0,
		.id_epoch = major_eph,
	};

	D_ASSERT(minor_eph != 0);

	if (punch) {
		id.id_punch_minor_eph = minor_eph;
		id.id_update_minor_eph = 0;
	} else {
		id.id_punch_minor_eph = 0;
		id.id_update_minor_eph = minor_eph;
	}

	if (epr)
		range = *epr;

	return ilog_modify(loh, &id, &range, ILOG_OP_UPDATE);

}

/** Makes a specific update to the incarnation log permanent and
 *  removes redundant entries
 */
int
ilog_persist(daos_handle_t loh, const struct ilog_id *id)
{
	daos_epoch_range_t	 range = {id->id_epoch, id->id_epoch};
	int	rc;

	rc = ilog_modify(loh, id, &range, ILOG_OP_PERSIST);

	return rc;
}

/** Removes a specific entry from the incarnation log if it exists */
int
ilog_abort(daos_handle_t loh, const struct ilog_id *id)
{
	daos_epoch_range_t	 range = {0, DAOS_EPOCH_MAX};

	D_DEBUG(DB_IO, "Aborting ilog entry %d "DF_X64"\n", id->id_tx_id,
		id->id_epoch);
	return ilog_modify(loh, id, &range, ILOG_OP_ABORT);
}

#define NUM_EMBEDDED 8

struct ilog_priv {
	/** Embedded context for current log root */
	struct ilog_context	 ip_lctx;
	/** Array marking removed entries */
	uint32_t		*ip_removals;
	/** Version of log from prior fetch */
	int32_t			 ip_log_version;
	/** Intent for prior fetch */
	uint32_t		 ip_intent;
	/** Number of status entries allocated */
	uint32_t		 ip_alloc_size;
	/** Cached return code for fetch operation */
	int			 ip_rc;
	/** Embedded status entries */
	uint32_t		 ip_embedded[NUM_EMBEDDED];
};
D_CASSERT(sizeof(struct ilog_priv) <= ILOG_PRIV_SIZE);

static inline struct ilog_priv *
ilog_ent2priv(struct ilog_entries *entries)
{
	return (struct ilog_priv *)&entries->ie_priv[0];
}

void
ilog_fetch_init(struct ilog_entries *entries)
{
	struct ilog_priv	*priv = ilog_ent2priv(entries);

	D_ASSERT(entries != NULL);
	memset(entries, 0, sizeof(*entries));
	entries->ie_statuses = &priv->ip_embedded[0];
}

static void
ilog_status_refresh(struct ilog_context *lctx, uint32_t intent,
		    struct ilog_entries *entries)
{
	struct ilog_priv	*priv = ilog_ent2priv(entries);
	struct ilog_entry	 entry;
	int32_t			 status;
	bool			 same_intent = (intent == priv->ip_intent);

	priv->ip_intent = intent;
	priv->ip_rc = 0;
	ilog_foreach_entry(entries, &entry) {
		if (same_intent &&
		    (entry.ie_status == ILOG_COMMITTED ||
		     entry.ie_status == ILOG_REMOVED))
			continue;
		status = ilog_status_get(lctx, &entry.ie_id, intent);
		if (status < 0) {
			priv->ip_rc = status;
			return;
		}
		entries->ie_statuses[entry.ie_idx] = status;
	}
}

static bool
ilog_fetch_cached(struct umem_instance *umm, struct ilog_root *root,
		  const struct ilog_desc_cbs *cbs, uint32_t intent,
		  struct ilog_entries *entries)
{
	struct ilog_priv	*priv = ilog_ent2priv(entries);
	struct ilog_context	*lctx = &priv->ip_lctx;

	D_ASSERT(entries->ie_statuses != NULL);
	D_ASSERT(priv->ip_alloc_size != 0 ||
		 entries->ie_statuses == &priv->ip_embedded[0]);

	if (priv->ip_lctx.ic_root != root ||
	    priv->ip_log_version != ilog_mag2ver(root->lr_magic)) {
		goto reset;
	}

	if (priv->ip_rc == -DER_NONEXIST)
		return true;

	D_ASSERT(entries->ie_ids != NULL);
	ilog_status_refresh(&priv->ip_lctx, intent, entries);

	return true;
reset:
	lctx->ic_root = root;
	lctx->ic_root_off = umem_ptr2off(umm, root);
	lctx->ic_umm = *umm;
	lctx->ic_cbs = *cbs;
	lctx->ic_ref = 0;
	lctx->ic_in_txn = false;
	lctx->ic_ver_inc = false;

	entries->ie_num_entries = 0;
	priv->ip_intent = intent;
	priv->ip_log_version = ilog_mag2ver(lctx->ic_root->lr_magic);
	priv->ip_rc = 0;

	return false;
}

static int
prepare_entries(struct ilog_entries *entries, struct ilog_array_cache *cache)
{
	struct ilog_priv	*priv = ilog_ent2priv(entries);
	uint32_t		*statuses;

	/** Ensure removals gets reallocated, if necessary */
	D_FREE(priv->ip_removals);

	if (cache->ac_nr <= NUM_EMBEDDED)
		goto done;

	if (cache->ac_nr <= priv->ip_alloc_size)
		goto done;

	D_ALLOC_ARRAY(statuses, cache->ac_nr);
	if (statuses == NULL)
		return -DER_NOMEM;

	if (entries->ie_statuses != &priv->ip_embedded[0])
		D_FREE(entries->ie_statuses);

	entries->ie_statuses = statuses;
	priv->ip_alloc_size = cache->ac_nr;

done:
	entries->ie_ids = cache->ac_entries;

	return 0;
}
static int
set_entry(struct ilog_entries *entries, int i, int status)
{
	struct ilog_priv	*priv = ilog_ent2priv(entries);

	D_ASSERT(i < NUM_EMBEDDED || i < priv->ip_alloc_size);
	D_ASSERT(i == entries->ie_num_entries);
	entries->ie_statuses[entries->ie_num_entries++] = status;

	return 0;
}

int
ilog_fetch(struct umem_instance *umm, struct ilog_df *root_df,
	   const struct ilog_desc_cbs *cbs, uint32_t intent,
	   struct ilog_entries *entries)
{
	struct ilog_context	*lctx;
	struct ilog_root	*root;
	struct ilog_id		*id;
	struct ilog_priv	*priv = ilog_ent2priv(entries);
	struct ilog_array_cache	 cache;
	int			 i;
	int			 status;
	int			 rc = 0;

	ILOG_ASSERT_VALID(root_df);

	root = (struct ilog_root *)root_df;

	if (ilog_fetch_cached(umm, root, cbs, intent, entries)) {
		if (priv->ip_rc == -DER_INPROGRESS ||
		    priv->ip_rc == -DER_NONEXIST)
			return priv->ip_rc;
		if (priv->ip_rc < 0) {
			/* Don't cache error return codes */
			rc = priv->ip_rc;
			priv->ip_rc = 0;
			priv->ip_log_version = ILOG_MAGIC;
			return rc;
		}
		return 0;
	}

	lctx = &priv->ip_lctx;
	if (ilog_empty(root))
		D_GOTO(out, rc = 0);

	ilog_log2cache(lctx, &cache);

	rc = prepare_entries(entries, &cache);
	if (rc != 0)
		goto fail;

	for (i = 0; i < cache.ac_nr; i++) {
		id = &cache.ac_entries[i];
		status = ilog_status_get(lctx, id, intent);
		if (status != -DER_INPROGRESS && status < 0)
			D_GOTO(fail, rc = status);
		set_entry(entries, i, status);
		if (rc != 0)
			goto fail;
	}

out:
	D_ASSERT(rc != -DER_NONEXIST);
	if (entries->ie_num_entries == 0)
		rc = -DER_NONEXIST;

	priv->ip_rc = rc;

	return rc;
fail:
	/* fetch again next time */
	priv->ip_log_version = ILOG_MAGIC;

	return rc;
}

void
ilog_fetch_finish(struct ilog_entries *entries)
{
	struct ilog_priv	*priv = ilog_ent2priv(entries);

	D_ASSERT(entries != NULL);
	if (priv->ip_alloc_size)
		D_FREE(entries->ie_statuses);
	D_FREE(priv->ip_removals);
}

static int
remove_ilog_entry(struct ilog_context *lctx, struct ilog_entries *entries,
		  int idx, int *removed)
{
	const struct ilog_id	*id = &entries->ie_ids[idx];
	struct ilog_priv	*priv = ilog_ent2priv(entries);
	int			 rc;

	rc = ilog_tx_begin(lctx);
	if (rc != 0)
		return rc;
	D_DEBUG(DB_TRACE, "Removing ilog entry at "DF_X64"\n",
		id->id_epoch);

	rc = ilog_log_del(lctx, id, true);
	if (rc != 0) {
		D_ERROR("Could not remove entry from tree: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}
	D_DEBUG(DB_TRACE, "Removed ilog entry at "DF_X64"\n",
		id->id_epoch);

	priv->ip_removals[idx] = true;

	(*removed)++;

	return 0;
}

struct agg_arg {
	const daos_epoch_range_t	*aa_epr;
	int32_t				 aa_prev;
	int32_t				 aa_prior_punch;
	daos_epoch_t			 aa_punched;
	bool				 aa_discard;
	uint16_t			 aa_punched_minor;
};

enum {
	AGG_RC_DONE,
	AGG_RC_NEXT,
	AGG_RC_REMOVE,
	AGG_RC_REMOVE_PREV,
	AGG_RC_ABORT,
};

static bool
entry_punched(const struct ilog_entry *entry, const struct agg_arg *agg_arg)
{
	uint16_t	minor_epc = MAX(entry->ie_id.id_punch_minor_eph,
					entry->ie_id.id_update_minor_eph);

	if (entry->ie_id.id_epoch > agg_arg->aa_punched)
		return false;

	if (entry->ie_id.id_epoch < agg_arg->aa_punched)
		return true;

	return minor_epc <= agg_arg->aa_punched_minor;

}

static int
check_agg_entry(const struct ilog_entries *entries, const struct ilog_entry *entry,
		struct agg_arg *agg_arg)
{
	int			rc;
	bool			parent_punched = false;
	struct ilog_entry	tmp;
	uint16_t		minor_epc = MAX(entry->ie_id.id_punch_minor_eph,
						entry->ie_id.id_update_minor_eph);

	if (D_LOG_ENABLED(DB_TRACE)) {
		daos_epoch_t		prev_epc = 0;
		daos_epoch_t		prev_punch_epc = 0;

		if (agg_arg->aa_prev != -1) {
			ilog_cache_entry(entries, &tmp, agg_arg->aa_prev);
			prev_epc = tmp.ie_id.id_epoch;
		}
		if (agg_arg->aa_prior_punch != -1) {
			ilog_cache_entry(entries, &tmp, agg_arg->aa_prior_punch);
			prev_punch_epc = tmp.ie_id.id_epoch;
		}
		D_DEBUG(DB_TRACE, "Entry "DF_X64".%d punch=%s prev="DF_X64" prior_punch="DF_X64"\n",
			entry->ie_id.id_epoch, minor_epc, ilog_is_punch(entry) ? "yes" : "no",
			prev_epc, prev_punch_epc);
	}

	if (entry->ie_id.id_epoch > agg_arg->aa_epr->epr_hi)
		D_GOTO(done, rc = AGG_RC_DONE);

	/* Abort ilog aggregation on hitting any uncommitted entry */
	if (entry->ie_status == ILOG_UNCOMMITTED)
		D_GOTO(done, rc = AGG_RC_ABORT);

	parent_punched = entry_punched(entry, agg_arg);
	if (entry->ie_id.id_epoch < agg_arg->aa_epr->epr_lo) {
		if (parent_punched) {
			/* Skip entries outside of the range and
			 * punched by the parent
			 */
			D_GOTO(done, rc = AGG_RC_NEXT);
		}
		if (ilog_is_punch(entry)) {
			/* Just save the prior punch entry */
			agg_arg->aa_prior_punch = entry->ie_idx;
		} else {
			/* A create covers the prior punch */
			agg_arg->aa_prior_punch = -1;
		}
		D_GOTO(done, rc = AGG_RC_NEXT);
	}

	/* With purge set, there should not be uncommitted entries */
	D_ASSERT(entry->ie_status != ILOG_UNCOMMITTED);

	if (agg_arg->aa_discard || entry->ie_status == ILOG_REMOVED ||
	    parent_punched) {
		/* Remove stale entry or punched entry */
		D_GOTO(done, rc = AGG_RC_REMOVE);
	}

	if (agg_arg->aa_prev != -1) {
		bool			 punch;

		ilog_cache_entry(entries, &tmp, agg_arg->aa_prev);
		punch = ilog_is_punch(&tmp);

		if (!punch) {
			/* punched by outer level */
			punch = entry_punched(&tmp, agg_arg);
		}
		if (ilog_is_punch(entry) == punch) {
			/* Remove redundant entry */
			D_GOTO(done, rc = AGG_RC_REMOVE);
		}
	}

	if (!ilog_is_punch(entry)) {
		/* Create is needed for now */
		D_GOTO(done, rc = AGG_RC_NEXT);
	}

	if (agg_arg->aa_prev == -1) {
		/* No punched entry to remove */
		D_GOTO(done, rc = AGG_RC_REMOVE);
	}

	if (tmp.ie_id.id_epoch < agg_arg->aa_epr->epr_lo) {
		/** Data punched is not in range */
		agg_arg->aa_prior_punch = entry->ie_idx;
		D_GOTO(done, rc = AGG_RC_NEXT);
	}

	D_ASSERT(!ilog_is_punch(&tmp));

	/* Punch is redundant or covers nothing.  Remove it. */
	rc = AGG_RC_REMOVE_PREV;
done:
	return rc;
}

static int
collapse_tree(struct ilog_context *lctx, struct ilog_array_cache *cache, struct ilog_priv *priv,
	      int removed)
{
	struct ilog_id		*dest;
	struct ilog_array	*array;
	int			 rc;
	int			 nr = 0;
	int			 i;

	if (removed == 0)
		return 0;

	if (cache->ac_nr == removed)
		return reset_root(lctx, cache, -1);

	if (cache->ac_nr == removed + 1) {
		/** all but one entry removed, move it to root */
		for (i = 0; i < cache->ac_nr; i++) {
			if (!priv->ip_removals[i])
				return reset_root(lctx, cache, i);
		}
		D_ASSERT(0);
	}

	array = cache->ac_array;

	rc = umem_tx_add_ptr(&lctx->ic_umm, array,
			     sizeof(array) + sizeof(array->ia_id[0]) * (cache->ac_nr - removed));
	if (rc != 0)
		return rc;

	dest = &array->ia_id[0];

	for (i = 0; i < cache->ac_nr; i++) {
		if (priv->ip_removals[i])
			continue;

		dest->id_value = cache->ac_entries[i].id_value;
		dest->id_epoch = cache->ac_entries[i].id_epoch;
		nr++;
		dest++;
	}

	D_ASSERT(nr == cache->ac_nr - removed);
	array->ia_len = nr;

	return 0;
}

int
ilog_aggregate(struct umem_instance *umm, struct ilog_df *ilog,
	       const struct ilog_desc_cbs *cbs, const daos_epoch_range_t *epr,
	       bool discard, daos_epoch_t punched_major, uint16_t punched_minor,
	       struct ilog_entries *entries)
{
	struct ilog_priv	*priv = ilog_ent2priv(entries);
	struct ilog_context	*lctx;
	struct ilog_entry	 entry;
	struct agg_arg		 agg_arg;
	struct ilog_root	*root;
	struct ilog_array_cache	 cache;
	bool			 empty = false;
	int			 rc = 0;
	int			 removed = 0;

	D_ASSERT(epr != NULL);
	D_ASSERT(punched_major <= epr->epr_hi);

	D_DEBUG(DB_TRACE, "%s incarnation log: epr: "DF_X64"-"DF_X64" punched="
		DF_X64".%d\n", discard ? "Discard" : "Aggregate", epr->epr_lo,
		epr->epr_hi, punched_major, punched_minor);

	/* This can potentially be optimized but using ilog_fetch gets some code
	 * reuse.
	 */
	rc = ilog_fetch(umm, ilog, cbs, DAOS_INTENT_PURGE, entries);
	if (rc == -DER_NONEXIST) {
		D_DEBUG(DB_TRACE, "log is empty\n");
		/* Log is empty */
		return 1;
	}

	lctx = &priv->ip_lctx;

	root = lctx->ic_root;

	ILOG_ASSERT_VALID(root);

	D_ASSERT(!ilog_empty(root)); /* ilog_fetch should have failed */

	ilog_log2cache(lctx, &cache);

	if (priv->ip_removals == NULL) {
		D_ALLOC_ARRAY(priv->ip_removals, cache.ac_nr);
		if (priv->ip_removals == NULL)
			return -DER_NOMEM;
	}

	agg_arg.aa_epr = epr;
	agg_arg.aa_prev = -1;
	agg_arg.aa_prior_punch = -1;
	agg_arg.aa_punched = punched_major;
	agg_arg.aa_punched_minor = punched_minor;
	agg_arg.aa_discard = discard;

	ilog_foreach_entry(entries, &entry) {
		D_ASSERT(entry.ie_idx < cache.ac_nr);
		priv->ip_removals[entry.ie_idx] = false;
		rc = check_agg_entry(entries, &entry, &agg_arg);

		switch (rc) {
		case AGG_RC_DONE:
			goto collapse;
		case AGG_RC_NEXT:
			agg_arg.aa_prev = entry.ie_idx;
			break;
		case AGG_RC_REMOVE_PREV:
			rc = remove_ilog_entry(lctx, entries, agg_arg.aa_prev, &removed);
			if (rc != 0)
				goto done;

			agg_arg.aa_prev = agg_arg.aa_prior_punch;
			/* Fall through */
		case AGG_RC_REMOVE:
			rc = remove_ilog_entry(lctx, entries, entry.ie_idx, &removed);
			if (rc != 0)
				goto done;
			break;
		case AGG_RC_ABORT:
			rc = -DER_TX_BUSY;
			goto done;
		default:
			/* Unknown return code */
			D_ASSERT(0);
		}
	}

collapse:
	rc = collapse_tree(lctx, &cache, priv, removed);

	empty = ilog_empty(root);
done:
	rc = ilog_tx_end(lctx, rc);
	D_DEBUG(DB_TRACE, "%s in incarnation log epr:"DF_X64"-"DF_X64
		" status: "DF_RC", removed %d entries\n",
		discard ? "Discard" : "Aggregation", epr->epr_lo,
		epr->epr_hi, DP_RC(rc), removed);
	if (rc)
		return rc;

	return empty;
}

uint32_t *
ilog_ts_idx_get(struct ilog_df *ilog_df)
{
	struct ilog_root	*root;

	/** No validity check as index is just a constant offset */
	root = (struct ilog_root *)ilog_df;

	return &root->lr_ts_idx;
}

uint32_t
ilog_version_get(daos_handle_t loh)
{
	struct ilog_context	*lctx;

	lctx = ilog_hdl2lctx(loh);
	if (lctx == NULL) {
		D_ERROR("Invalid log handle\n");
		return 0;
	}

	return ilog_mag2ver(lctx->ic_root->lr_magic);
}
