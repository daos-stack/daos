/**
 * (C) Copyright 2019 Intel Corporation.
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

struct ilog_root {
	union {
		struct ilog_id		lr_id;
		struct ilog_tree	lr_tree;
	};
	bool				lr_punch;
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

#define DF_ID		"epoch:"DF_U64" tx_id:0x"DF_X64
#define DP_ID(id)	(id).id_epoch, (id).id_tx_id
#define DF_VAL		"punch:%s"
#define DP_VAL(punch)	punch ? " true" : "false"

struct prec {
	bool	p_punch;
	int32_t	p_magic;
};

static inline struct prec *
rec2prec(struct btr_record *rec)
{
	return (struct prec *)&rec->rec_off;
}

static inline bool *
rec2punch(struct btr_record *rec)
{
	return &rec2prec(rec)->p_punch;
}

D_CASSERT(sizeof(struct ilog_id) == sizeof(struct ilog_tree));
D_CASSERT(sizeof(struct ilog_root) == sizeof(struct ilog_df));
/** We hijack the value offset to store the actual value inline */
D_CASSERT(sizeof(struct prec) <= sizeof(((struct btr_record *)0)->rec_off));

/**
 * Customized functions for btree.
 */

/** size of hashed-key */
static int
ilog_hkey_size(void)
{
	return sizeof(struct ilog_id);
}

static int
ilog_rec_msize(int alloc_overhead)
{
	/** No extra allocation for ilog entries */
	return 0;
}

/** generate hkey */
static void
ilog_hkey_gen(struct btr_instance *tins, d_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(struct ilog_id));
	memcpy(hkey, key_iov->iov_buf, sizeof(struct ilog_id));
}

/** compare the hashed key */
static int
ilog_hkey_cmp(struct btr_instance *tins, struct btr_record *rec, void *hkey)
{
	struct ilog_id	*k1 = (struct ilog_id *)&rec->rec_hkey[0];
	struct ilog_id	*k2 = (struct ilog_id *)hkey;

	if (k1->id_epoch < k2->id_epoch)
		return BTR_CMP_LT;

	if (k1->id_epoch > k2->id_epoch)
		return BTR_CMP_GT;

	return BTR_CMP_EQ;
}

/** create a new key-record, or install an externally allocated key-record */
static int
ilog_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov,
	      d_iov_t *val_iov, struct btr_record *rec)
{
	struct prec *prec = rec2prec(rec);

	D_ASSERT(val_iov->iov_len == sizeof(bool));
	/** Note the D_CASSERT above ensures that rec_off is large enough
	 *  to fit the value without allocating new memory.
	 */
	prec->p_punch = *(bool *)val_iov->iov_buf;
	/** Generic btree code will not call ilog_rec_free if rec->rec_off is
	 * UMOFF_NULL.   Since we are using that field to store the data and
	 * the data (p_punch) can be 0, set this flag to force rec_free to be
	 * called.
	 */
	prec->p_magic = 1;

	return 0;
}

static inline int
ilog_is_same_tx(struct ilog_context *lctx, umem_off_t tx_id, bool *same)
{
	struct ilog_desc_cbs	*cbs = &lctx->ic_cbs;

	*same = true;

	if (!cbs->dc_is_same_tx_cb)
		return 0;

	return cbs->dc_is_same_tx_cb(&lctx->ic_umm, tx_id, same,
				     cbs->dc_is_same_tx_args);
}

static int
ilog_status_get(struct ilog_context *lctx, umem_off_t tx_id,
		uint32_t intent)
{
	struct ilog_desc_cbs	*cbs = &lctx->ic_cbs;
	int			 rc;

	if (tx_id == UMOFF_NULL)
		return ILOG_COMMITTED;

	if (!cbs->dc_log_status_cb)
		return ILOG_COMMITTED;

	rc = cbs->dc_log_status_cb(&lctx->ic_umm, tx_id, intent,
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
				cbs->dc_log_add_args);
	if (rc != 0) {
		D_ERROR("Failed to register incarnation log entry: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	D_DEBUG(DB_IO, "Registered ilog="DF_X64" epoch="DF_U64" tx_id="
		DF_U64"\n", lctx->ic_root_off, id->id_epoch,
		id->id_tx_id);

	return 0;
}

static inline int
ilog_log_del(struct ilog_context *lctx, const struct ilog_id *id)
{
	struct ilog_desc_cbs	*cbs = &lctx->ic_cbs;
	int			 rc;

	if (!cbs->dc_log_del_cb || !id->id_tx_id)
		return 0;

	rc = cbs->dc_log_del_cb(&lctx->ic_umm, lctx->ic_root_off, id->id_tx_id,
				cbs->dc_log_del_args);
	if (rc != 0) {
		D_ERROR("Failed to deregister incarnation log entry: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	D_DEBUG(DB_IO, "De-registered ilog="DF_X64" epoch="DF_U64" tx_id="
		DF_U64"\n", lctx->ic_root_off, id->id_epoch,
		id->id_tx_id);

	return 0;
}

static int
ilog_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct ilog_id		*id = (struct ilog_id *)&rec->rec_hkey[0];
	struct ilog_context	*lctx = args;

	if (lctx == NULL)
		return 0;

	/* For current DTX, we need to remove the forward reference.  I think
	 * eventually, this callback will go away as we will undo the operation
	 * on the key rather than specifically the incarnation log record.
	 */
	return ilog_log_del(lctx, id);
}

static int
ilog_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	      d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct ilog_id	*key = (struct ilog_id *)&rec->rec_hkey[0];
	bool		*punch = rec2punch(rec);

	if (key_iov != NULL) {
		if (key_iov->iov_buf == NULL) {
			d_iov_set(key_iov, key, sizeof(*key));
		} else {
			D_ASSERT(sizeof(*key) <= key_iov->iov_buf_len);
			memcpy(key_iov->iov_buf, key, sizeof(*key));
			key_iov->iov_len = sizeof(*key);
		}
	}
	if (val_iov != NULL) {
		if (val_iov->iov_buf == NULL) {
			d_iov_set(val_iov, punch, sizeof(*punch));
		} else {
			D_ASSERT(sizeof(*punch) <= val_iov->iov_buf_len);
			memcpy(val_iov->iov_buf, punch, sizeof(*punch));
			val_iov->iov_len = sizeof(*punch);
		}
	}
	return 0;
}

static btr_ops_t ilog_btr_ops = {
	.to_rec_msize		= ilog_rec_msize,
	.to_hkey_size		= ilog_hkey_size,
	.to_hkey_gen		= ilog_hkey_gen,
	.to_hkey_cmp		= ilog_hkey_cmp,
	.to_rec_alloc		= ilog_rec_alloc,
	.to_rec_free		= ilog_rec_free,
	.to_rec_fetch		= ilog_rec_fetch,
};

int
ilog_init(void)
{
	int	rc;

	rc = dbtree_class_register(VOS_BTR_ILOG, 0, &ilog_btr_ops);
	if (rc != 0)
		D_ERROR("Failed to register incarnation log btree class: %s\n",
			d_errstr(rc));

	return rc;
}

/* 4 bit magic number + version */
#define ILOG_MAGIC		0x60000000
#define ILOG_MAGIC_MASK		0xf0000000
#define ILOG_VERSION_MASK	~(ILOG_MAGIC_MASK)
#define ILOG_MAGIC_VALID(magic)	(((magic) & ILOG_MAGIC_MASK) == ILOG_MAGIC)

static inline int
ilog_mag2ver(uint32_t magic) {
	if (!ILOG_MAGIC_VALID(magic))
		return -DER_INVAL;
	return (magic & ILOG_VERSION_MASK);
}

/** Increment the version of the log.   The object tree in particular can
 *  benefit from cached state of the tree.  In order to detect when to
 *  update the case, we keep a version.
 */
static inline uint32_t
ilog_ver_inc(struct ilog_context *lctx)
{
	uint32_t        magic = lctx->ic_root->lr_magic;
	uint32_t        next = (magic & ILOG_VERSION_MASK) + 1;

	D_ASSERT(ILOG_MAGIC_VALID(magic));

	if (next > ILOG_VERSION_MASK)
		next = 1; /* Wrap around */

	/* This is only called when we will persist the new version so no need
	* to update the version when finishing the transaction.
	*/
	lctx->ic_ver_inc = false;

	return ILOG_MAGIC | next;
}

/** Called when we know a txn is needed.  Subsequent calls are a noop. */
static inline int
ilog_tx_begin(struct ilog_context *lctx)
{
	int	rc = 0;

	if (lctx->ic_in_txn)
		return 0;

	rc = vos_tx_begin(&lctx->ic_umm);
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
	return vos_tx_end(&lctx->ic_umm, rc);
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

	tmp.lr_magic = ILOG_MAGIC + 1;

	rc = ilog_ptr_set(&lctx, root, &tmp);
	lctx.ic_ver_inc = false;

	rc = ilog_tx_end(&lctx, rc);
	return rc;
}

#define ILOG_ASSERT_VALID(root_df)				\
	do {							\
		struct ilog_root	*__root;		\
								\
		__root = (struct ilog_root *)(root_df);		\
		D_ASSERT((__root != NULL) &&			\
			 ILOG_MAGIC_VALID(__root->lr_magic));	\
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
	daos_handle_t		 toh = DAOS_HDL_INVAL;
	struct umem_attr	 uma;
	struct ilog_id		 saved_id = {0};
	uint32_t		 tmp = 0;
	int			 rc = 0;

	ILOG_ASSERT_VALID(root);

	rc = ilog_tx_begin(&lctx);
	if (rc != 0) {
		D_ERROR("Failed to start PMDK transaction: rc = %s\n",
			d_errstr(rc));
		return rc;
	}

	/* No need to update the version on destroy */
	lctx.ic_ver_inc = false;

	if (!ilog_empty(lctx.ic_root) && !lctx.ic_root->lr_tree.it_embedded) {
		umem_attr_get(umm, &uma);
		rc = dbtree_open(lctx.ic_root->lr_tree.it_root, &uma, &toh);
		if (rc != 0) {
			D_ERROR("Could not open incarnation log tree:"
				" rc = %s\n", d_errstr(rc));
			goto fail;
		}

		rc = dbtree_destroy(toh, &lctx);
		if (rc != 0) {
			D_ERROR("Could not destroy incarnation log tree:"
				" rc = %s\n", d_errstr(rc));
			goto fail;
		}
	} else if (lctx.ic_root->lr_tree.it_embedded) {
		D_DEBUG(DB_IO, "Removing destroyed entry "DF_U64" in root\n",
			lctx.ic_root->lr_id.id_epoch);
		saved_id = lctx.ic_root->lr_id;
	}

	rc = ilog_ptr_set(&lctx, &lctx.ic_root->lr_magic, &tmp);

	if (rc == 0)
		rc = ilog_log_del(&lctx, &saved_id);
fail:
	rc = ilog_tx_end(&lctx, rc);

	return rc;
}

static int
ilog_root_migrate(struct ilog_context *lctx, daos_epoch_t epoch, bool new_punch)
{
	struct ilog_root	*root;
	struct ilog_root	 tmp = {0};
	d_iov_t			 key_iov;
	d_iov_t			 val_iov;
	struct ilog_id		 key;
	umem_off_t		 tree_root;
	daos_handle_t		 toh = DAOS_HDL_INVAL;
	struct umem_attr	 uma;
	struct ilog_id		 id = {0, epoch};
	bool			 punch;
	int			 rc = 0;

	root = lctx->ic_root;

	rc = ilog_tx_begin(lctx);
	if (rc != 0) {
		D_ERROR("Failed to start PMDK transaction: rc = %s\n",
			d_errstr(rc));
		goto done;
	}

	umem_attr_get(&lctx->ic_umm, &uma);
	rc = dbtree_create(VOS_BTR_ILOG, 0, ILOG_TREE_ORDER,
			   &uma, &tree_root, &toh);
	if (rc != 0) {
		D_ERROR("Failed to create an incarnation log tree: rc = %s\n",
			d_errstr(rc));
		goto done;
	}

	lctx->ic_ver_inc = true;
	d_iov_set(&key_iov, &key, sizeof(key));
	d_iov_set(&val_iov, &punch, sizeof(punch));

	key = root->lr_id;
	punch = root->lr_punch;

	rc = dbtree_update(toh, &key_iov, &val_iov);
	if (rc != 0) {
		D_ERROR("Failed to add entry to incarnation log: %s\n",
			d_errstr(rc));
		goto done;
	}

	rc = ilog_log_add(lctx, &id);
	if (rc != 0)
		goto done;

	key = id;
	punch = new_punch;

	rc = dbtree_update(toh, &key_iov, &val_iov);
	if (rc != 0) {
		D_ERROR("Failed to add entry to incarnation log: %s\n",
			d_errstr(rc));
		goto done;
	}

	tmp.lr_tree.it_root = tree_root;
	tmp.lr_tree.it_embedded = 0;
	tmp.lr_magic = ilog_ver_inc(lctx);

	rc = ilog_ptr_set(lctx, root, &tmp);
done:
	if (!daos_handle_is_inval(toh))
		dbtree_close(toh);

	return rc;
}

static int
check_equal(struct ilog_context *lctx, struct ilog_id *id_out,
	    const struct ilog_id *id_in, bool update, bool *is_equal)
{
	int	rc;

	*is_equal = false;

	if (id_in->id_epoch != id_out->id_epoch)
		return 0;

	if (update) {
		rc = ilog_is_same_tx(lctx, id_out->id_tx_id, is_equal);
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
		D_DEBUG(DB_IO, "Access of incarnation log from multiple DTX"
			" at same time is not allowed: rc=DER_AGAIN\n");
		return -DER_AGAIN;
	}

	return 0;
}

enum {
	ILOG_OP_UPDATE,
	ILOG_OP_PERSIST,
	ILOG_OP_ABORT,
};

static int
update_inplace(struct ilog_context *lctx, struct ilog_id *id_out,
	       bool *punch_out, const struct ilog_id *id_in, int opc,
	       bool punch_in, bool *is_equal)
{
	umem_off_t	null_off = UMOFF_NULL;
	int		rc;

	rc = check_equal(lctx, id_out, id_in, opc == ILOG_OP_UPDATE, is_equal);
	if (rc != 0 || !*is_equal || opc == ILOG_OP_ABORT)
		return rc;

	if (opc == ILOG_OP_PERSIST) {
		D_DEBUG(DB_IO, "Setting "DF_U64" to persistent\n",
			id_in->id_epoch);
		return ilog_ptr_set(lctx, &id_out->id_tx_id, &null_off);
	}

	if (*punch_out || !punch_in)
		return 0;

	/* New operation in old DTX is a punch.  Update the old entry
	 * accordingly.
	 */
	D_DEBUG(DB_IO, "Updating "DF_U64" to a punch\n", id_in->id_epoch);
	return ilog_ptr_set(lctx, punch_out, &punch_in);
}

static int
collapse_tree(struct ilog_context *lctx, daos_handle_t *toh)
{
	struct ilog_root	*root = lctx->ic_root;
	int			 rc;
	struct ilog_root	 tmp = {0};
	struct ilog_id		 key = {0};
	struct btr_attr		 attr;
	d_iov_t			 key_iov;
	d_iov_t			 val_iov;
	bool			 punch;

	rc = dbtree_query(*toh, &attr, NULL);
	if (attr.ba_count > 1)
		return 0;

	d_iov_set(&val_iov, &punch, sizeof(punch));
	d_iov_set(&key_iov, &key, sizeof(key));
	rc = dbtree_fetch(*toh, BTR_PROBE_GT, DAOS_INTENT_DEFAULT, &key_iov,
			  &key_iov, &val_iov);
	if (rc == -DER_NONEXIST) {
		rc = 0;
		key.id_epoch = 0;
		key.id_tx_id = 0;
		punch = 0;
		goto set;
	}

	if (rc != 0) {
		D_ERROR("dbtree_fetch failed: rc = %s\n", d_errstr(rc));
		goto done;
	}
set:
	rc = dbtree_destroy(*toh, NULL);
	if (rc != 0) {
		D_ERROR("Could not destroy table: rc = %s\n", d_errstr(rc));
		goto done;
	}
	*toh = DAOS_HDL_INVAL;

	tmp.lr_magic = ilog_ver_inc(lctx);
	tmp.lr_id = key;
	tmp.lr_punch = punch;
	rc = ilog_ptr_set(lctx, root, &tmp);
done:
	return rc;
}

static int
consolidate_tree(struct ilog_context *lctx, const daos_epoch_range_t *epr,
		 daos_handle_t *toh, int opc, const struct ilog_id *id_in,
		 bool is_punch)
{
	int			 rc = 0;

	D_ASSERT(opc == ILOG_OP_ABORT);

	rc = dbtree_delete(*toh, BTR_PROBE_BYPASS, NULL, NULL);
	if (rc != 0)
		return rc;

	return collapse_tree(lctx, toh);
}

static int
ilog_tree_modify(struct ilog_context *lctx, const struct ilog_id *id_in,
		 bool punch, const daos_epoch_range_t *epr, int opc)
{
	struct ilog_root	*root;
	bool			*punchp;
	struct ilog_id		*keyp;
	struct ilog_id		 id = *id_in;
	daos_handle_t		 toh = DAOS_HDL_INVAL;
	d_iov_t			 key_iov_in;
	d_iov_t			 key_iov;
	d_iov_t			 val_iov;
	bool			 is_equal;
	int			 visibility = ILOG_COMMITTED;
	struct umem_attr	 uma;
	int			 rc = 0;

	root = lctx->ic_root;

	umem_attr_get(&lctx->ic_umm, &uma);
	rc = dbtree_open(root->lr_tree.it_root, &uma, &toh);
	if (rc != 0) {
		D_ERROR("Failed to open incarnation log tree: rc = %s\n",
			d_errstr(rc));
		goto done;
	}

	d_iov_set(&key_iov_in, (struct ilog_id *)&id, sizeof(id));
	d_iov_set(&val_iov, NULL, 0);
	d_iov_set(&key_iov, NULL, 0);
	rc = dbtree_fetch(toh, BTR_PROBE_LE, DAOS_INTENT_DEFAULT, &key_iov_in,
			  &key_iov, &val_iov);

	if (rc == -DER_NONEXIST)
		goto insert;

	if (rc != 0) {
		D_ERROR("Fetch of ilog entry failed: rc = %s\n", d_errstr(rc));
		goto done;
	}

	punchp = (bool *)val_iov.iov_buf;
	keyp = (struct ilog_id *)key_iov.iov_buf;

	visibility = ILOG_UNCOMMITTED;

	if (keyp->id_epoch <= epr->epr_hi &&
	    keyp->id_epoch >= epr->epr_lo) {
		visibility = ilog_status_get(lctx, keyp->id_tx_id,
					     DAOS_INTENT_UPDATE);
		if (visibility < 0) {
			rc = visibility;
			goto done;
		}
	}

	rc = update_inplace(lctx, keyp, punchp, id_in, opc, punch,
			    &is_equal);
	if (rc != 0)
		goto done;

	if (is_equal) {
		if (opc != ILOG_OP_ABORT)
			goto done;

		rc = consolidate_tree(lctx, epr, &toh, opc,
				      id_in, *punchp);

		goto done;
	}

	if (opc != ILOG_OP_UPDATE) {
		D_DEBUG(DB_IO, "No entry found, done\n");
		goto done;
	}

	if (!punch && visibility != ILOG_UNCOMMITTED && !*punchp)
		goto done;
insert:

	rc = ilog_tx_begin(lctx);
	if (rc != 0)
		goto done;

	rc = ilog_log_add(lctx, &id);
	if (rc != 0)
		goto done;

	d_iov_set(&val_iov, &punch, sizeof(punch));
	/* Can't use BTR_PROBE_BYPASS because it inserts before existing
	 * entry and we need it to append.   We could modify btree to
	 * support this use case.
	 */
	rc = dbtree_update(toh, &key_iov_in, &val_iov);
	if (rc) {
		D_ERROR("Failed to update incarnation log: rc = %s\n",
			d_errstr(rc));
		goto done;
	}
done:
	if (!daos_handle_is_inval(toh))
		dbtree_close(toh);

	return rc;
}

const char *opc_str[] = {
	"Update",
	"Persist",
	"Abort",
};

static int
ilog_modify(daos_handle_t loh, const struct ilog_id *id_in,
	    bool punch, const daos_epoch_range_t *epr, int opc)
{
	struct ilog_context	*lctx;
	struct ilog_root	*root;
	struct ilog_root	 tmp = {0};
	int			 rc = 0;
	int			 visibility = ILOG_UNCOMMITTED;

	lctx = ilog_hdl2lctx(loh);
	if (lctx == NULL) {
		D_ERROR("Invalid log handle\n");
		return -DER_INVAL;
	}

	D_ASSERT(!lctx->ic_in_txn);

	root = lctx->ic_root;

	D_DEBUG(DB_IO, "%s in incarnation log: log:"DF_X64 " epoch:" DF_U64
		" tree_version: %d\n", opc_str[opc], lctx->ic_root_off,
		id_in->id_epoch, ilog_mag2ver(root->lr_magic));

	if (root->lr_tree.it_embedded && root->lr_id.id_epoch <= epr->epr_hi
	    && root->lr_id.id_epoch >= epr->epr_lo) {
		visibility = ilog_status_get(lctx, root->lr_id.id_tx_id,
					     DAOS_INTENT_UPDATE);
		if (visibility < 0) {
			rc = visibility;
			goto done;
		}
	}

	if (ilog_empty(root)) {
		if (opc != ILOG_OP_UPDATE) {
			D_DEBUG(DB_IO, "ilog entry "DF_U64" not found\n",
				id_in->id_epoch);
			goto done;
		}

		D_DEBUG(DB_IO, "Inserting "DF_U64" at ilog root\n",
			id_in->id_epoch);
		tmp.lr_magic = ilog_ver_inc(lctx);
		tmp.lr_id.id_epoch = id_in->id_epoch;
		tmp.lr_punch = punch;
		rc = ilog_ptr_set(lctx, root, &tmp);
		if (rc != 0)
			goto done;
		rc = ilog_log_add(lctx, &root->lr_id);
		if (rc != 0)
			goto done;
	} else if (root->lr_tree.it_embedded) {
		bool	is_equal;

		rc = update_inplace(lctx, &root->lr_id, &root->lr_punch,
				    id_in, opc, punch, &is_equal);
		if (rc != 0)
			goto done;

		if (is_equal) {
			if (opc == ILOG_OP_ABORT) {
				D_DEBUG(DB_IO, "Removing "DF_U64
					" from ilog root\n", id_in->id_epoch);
				tmp.lr_magic = ilog_ver_inc(lctx);
				rc = ilog_ptr_set(lctx, root, &tmp);
			}
			goto done;
		}

		if (opc != ILOG_OP_UPDATE) {
			D_DEBUG(DB_IO, "Entry "DF_U64" not found in ilog\n",
				id_in->id_epoch);
			goto done;
		}

		if (!punch && !root->lr_punch &&
		    id_in->id_epoch > root->lr_id.id_epoch &&
		    visibility == ILOG_COMMITTED) {
			D_DEBUG(DB_IO, "No update needed\n");
			goto done;
		}
		/* Either this entry is earlier or prior entry is uncommitted
		 * or either entry is a punch
		 */
		rc = ilog_root_migrate(lctx, id_in->id_epoch, punch);
	} else {
		/** Ok, we have a tree.  Do the operation in the tree */
		rc = ilog_tree_modify(lctx, id_in, punch, epr, opc);
	}
done:
	rc = ilog_tx_end(lctx, rc);
	D_DEBUG(DB_IO, "%s in incarnation log "DF_U64
		" status: rc=%s tree_version: %d\n",
		opc_str[opc], id_in->id_epoch, d_errstr(rc),
		ilog_mag2ver(lctx->ic_root->lr_magic));
	return rc;
}

int
ilog_update(daos_handle_t loh, const daos_epoch_range_t *epr,
	    daos_epoch_t epoch, bool punch)
{
	daos_epoch_range_t	 range = {0, DAOS_EPOCH_MAX};
	struct ilog_id		 id = {0, epoch};

	if (epr)
		range = *epr;

	return ilog_modify(loh, &id, punch, &range, ILOG_OP_UPDATE);

}

/** Makes a specific update to the incarnation log permanent and
 *  removes redundant entries
 */
int
ilog_persist(daos_handle_t loh, const struct ilog_id *id)
{
	daos_epoch_range_t	 range = {id->id_epoch, id->id_epoch};

	return ilog_modify(loh, id, false, &range, ILOG_OP_PERSIST);
}

/** Removes a specific entry from the incarnation log if it exists */
int
ilog_abort(daos_handle_t loh, const struct ilog_id *id)
{
	daos_epoch_range_t	 range = {0, DAOS_EPOCH_MAX};

	return ilog_modify(loh, id, false, &range, ILOG_OP_ABORT);
}

int
ilog_aggregate(struct umem_instance *umm, umem_off_t root_off,
	       const struct ilog_desc_cbs *cbs, const daos_epoch_range_t *epr,
	       bool discard)
{
	struct ilog_context	lctx_stack = {
		.ic_root = umem_off2ptr(umm, root_off),
		.ic_root_off = root_off,
		.ic_umm = *umm,
		.ic_ref = 1,
		.ic_cbs = *cbs,
		.ic_in_txn = 0,
	};
	struct ilog_context	*lctx = &lctx_stack;
	struct ilog_root	*root;
	struct ilog_root	 tmp = {0};
	struct ilog_id		 old_id = {0};
	struct ilog_id		 id = {0};
	int			 rc = 0;
	int			 empty = 0;
	bool			 punch_found = false;
	daos_epoch_t		 epoch;
	int			 visibility;
	bool			*punchp;
	struct ilog_id		*keyp;
	daos_handle_t		 toh = DAOS_HDL_INVAL;
	daos_handle_t		 ih = DAOS_HDL_INVAL;
	d_iov_t			 key_iov;
	d_iov_t			 val_iov;
	struct umem_attr	 uma;

	root = lctx->ic_root;

	ILOG_ASSERT_VALID(root);

	if (epr == NULL)
		return -DER_INVAL;

	D_DEBUG(DB_IO, "%s incarnation log: epr: "DF_U64"-"DF_U64"\n",
		discard ? "discard" : "aggregate", epr->epr_lo, epr->epr_hi);

	if (ilog_empty(root))
		return 1; /* indicate empty tree */

	if (root->lr_tree.it_embedded) {
		visibility = ilog_status_get(lctx, root->lr_id.id_tx_id,
					     DAOS_INTENT_PURGE);
		if (visibility == ILOG_COMMITTED) {
			if (!root->lr_punch && !discard)
				return 0;
			epoch = root->lr_id.id_epoch;
			if (epoch < epr->epr_lo || epoch > epr->epr_hi)
				return 0;
		}

		old_id = root->lr_id;
		tmp.lr_magic = ilog_ver_inc(lctx);
		rc = ilog_ptr_set(lctx, root, &tmp);
		if (rc != 0)
			goto done;

		empty = 1;

		rc = ilog_log_del(lctx, &old_id);

		goto done;
	}

	umem_attr_get(&lctx->ic_umm, &uma);
	rc = dbtree_open(root->lr_tree.it_root, &uma, &toh);
	if (rc != 0) {
		D_ERROR("Failed to open incarnation log tree: rc = %s\n",
			d_errstr(rc));
		goto done;
	}

	rc = dbtree_iter_prepare(toh, BTR_ITER_EMBEDDED, &ih);
	if (rc != 0) {
		D_ERROR("Could not prepare iterator: rc = %s\n", d_errstr(rc));
		return rc;
	}

	id.id_epoch = epr->epr_hi;
reprobe:
	d_iov_set(&key_iov, (struct ilog_id *)&id, sizeof(id));

	rc = dbtree_iter_probe(ih, BTR_PROBE_LE, DAOS_INTENT_DEFAULT,
			       &key_iov, NULL);
	if (rc == -DER_NONEXIST) {
		rc = 0;
		goto collapse;
	}

	if (rc != 0) {
		D_ERROR("Could not probe iterator: rc = %s\n",
			d_errstr(rc));
		goto done;
	}

	for (;;) {
		d_iov_set(&key_iov, NULL, 0);
		d_iov_set(&val_iov, NULL, 0);

		rc = dbtree_iter_fetch(ih, &key_iov, &val_iov, NULL);
		if (rc != 0) {
			D_ERROR("Could not fetch from iterator: rc = %s\n",
				d_errstr(rc));
			goto done;
		}

		punchp = (bool *)val_iov.iov_buf;
		keyp = (struct ilog_id *)key_iov.iov_buf;

		if (keyp->id_epoch < epr->epr_lo)
			break;

		if (!punch_found) {
			visibility = ilog_status_get(lctx, keyp->id_tx_id,
						     DAOS_INTENT_PURGE);
			if (visibility == ILOG_COMMITTED) {
				punch_found = *punchp;
				if (!punch_found)
					goto next;
			}
		}

		id.id_epoch = keyp->id_epoch - 1;

		rc = ilog_tx_begin(lctx);
		if (rc != 0)
			goto done;

		rc = dbtree_iter_delete(ih, lctx);
		if (rc != 0)
			goto done;

		if (id.id_epoch < epr->epr_lo)
			break;
		goto reprobe;
next:
		rc = dbtree_iter_prev(ih);
		if (rc == -DER_NONEXIST) {
			rc = 0;
			break;
		}
		if (rc != 0)
			goto done;
	}

collapse:
	rc = collapse_tree(lctx, &toh);

	empty = ilog_empty(root);
done:
	if (!daos_handle_is_inval(ih))
		dbtree_iter_finish(ih);
	if (!daos_handle_is_inval(toh))
		dbtree_close(toh);

	rc = ilog_tx_end(lctx, rc);
	D_DEBUG(DB_IO, "Aggregation in incarnation log epr:"DF_U64"-"DF_U64
		" status: "DF_RC"\n", epr->epr_lo, epr->epr_hi, DP_RC(rc));
	if (rc)
		return rc;

	return empty;
}

#define NUM_EMBEDDED 3

struct ilog_priv {
	/** Embedded context for current log root */
	struct ilog_context	 ip_lctx;
	/** dbtree iterator for prior fetch */
	daos_handle_t		 ip_ih;
	/** Version of log from prior fetch */
	int32_t			 ip_log_version;
	/** Intent for prior fetch */
	uint32_t		 ip_intent;
	/** Number of entries allocated */
	uint32_t		 ip_alloc_size;
	/** Cached return code for fetch operation */
	int			 ip_rc;
	/** Embedded entries */
	struct ilog_entry	 ip_embedded[NUM_EMBEDDED];
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
	entries->ie_entries = &priv->ip_embedded[0];
	priv->ip_ih = DAOS_HDL_INVAL;
}

static void
ilog_status_refresh(struct ilog_context *lctx, uint32_t intent,
		    struct ilog_entries *entries)
{
	struct ilog_priv	*priv = ilog_ent2priv(entries);
	struct ilog_entry	*entry;
	int32_t			 status;
	bool			 same_intent = (intent == priv->ip_intent);

	priv->ip_intent = intent;
	priv->ip_rc = 0;
	ilog_foreach_entry(entries, entry) {
		if (same_intent &&
		    (entry->ie_status == ILOG_COMMITTED ||
		     entry->ie_status == ILOG_REMOVED))
			continue;
		status = ilog_status_get(lctx, entry->ie_id.id_tx_id, intent);
		if (status < 0) {
			priv->ip_rc = status;
			return;
		}
		entry->ie_status = status;
	}
}

static bool
ilog_fetch_cached(struct umem_instance *umm, struct ilog_root *root,
		  const struct ilog_desc_cbs *cbs, uint32_t intent,
		  struct ilog_entries *entries)
{
	struct ilog_priv	*priv = ilog_ent2priv(entries);
	struct ilog_context	*lctx = &priv->ip_lctx;

	D_ASSERT(entries->ie_entries != NULL);
	D_ASSERT(priv->ip_alloc_size != 0 ||
		 entries->ie_entries == &priv->ip_embedded[0]);

	if (priv->ip_lctx.ic_root != root ||
	    priv->ip_log_version != ilog_mag2ver(root->lr_magic)) {
		goto reset;
	}

	if (priv->ip_rc == -DER_NONEXIST)
		return true;

	ilog_status_refresh(&priv->ip_lctx, intent, entries);

	return true;
reset:
	memset(lctx, 0, sizeof(*lctx));
	lctx->ic_root = root;
	lctx->ic_root_off = umem_ptr2off(umm, root);
	lctx->ic_umm = *umm;
	lctx->ic_cbs = *cbs;

	if (!daos_handle_is_inval(priv->ip_ih)) {
		dbtree_iter_finish(priv->ip_ih);
		priv->ip_ih = DAOS_HDL_INVAL;
	}
	entries->ie_num_entries = 0;
	priv->ip_intent = intent;
	priv->ip_log_version = ilog_mag2ver(lctx->ic_root->lr_magic);
	priv->ip_rc = 0;

	return false;
}

static int
open_tree_iterator(struct ilog_context *lctx, daos_handle_t *ih)
{
	struct ilog_root	*root;
	struct umem_attr	 uma;
	int			 rc;
	daos_handle_t		 toh;

	D_ASSERTF(daos_handle_is_inval(*ih), "Unexpected valid tree handle\n");

	root = lctx->ic_root;

	umem_attr_get(&lctx->ic_umm, &uma);
	rc = dbtree_open(root->lr_tree.it_root, &uma, &toh);
	if (rc != 0) {
		D_ERROR("Failed to open ilog tree: rc = %s\n", d_errstr(rc));
		return rc;
	}

	rc = dbtree_iter_prepare(toh, BTR_ITER_EMBEDDED, ih);
	if (rc != 0)
		D_ERROR("Failed to open ilog iterator: rc = %s\n",
			d_errstr(rc));

	dbtree_close(toh);

	return rc;
}

static struct ilog_entry *
alloc_entry(struct ilog_entries *entries)
{
	struct ilog_entry	*new_data;
	struct ilog_priv	*priv = ilog_ent2priv(entries);
	struct ilog_entry	*item;
	bool			 dealloc;
	size_t			 old_count;
	size_t			 new_count;

	if (entries->ie_num_entries < NUM_EMBEDDED)
		goto out;

	if (entries->ie_num_entries < priv->ip_alloc_size)
		goto out;

	if (priv->ip_alloc_size) {
		old_count = priv->ip_alloc_size;
		dealloc = true;
	} else {
		old_count = NUM_EMBEDDED;
		dealloc = false;
	}
	new_count = old_count * 2;

	D_ALLOC_ARRAY(new_data, new_count);
	if (new_data == NULL) {
		D_ERROR("No memory available for iterating ilog\n");
		return NULL;
	}

	memcpy(new_data, entries->ie_entries,
	       sizeof(*new_data) * old_count);
	if (dealloc)
		D_FREE(entries->ie_entries);

	entries->ie_entries = new_data;
	priv->ip_alloc_size = new_count;
out:
	item = &entries->ie_entries[entries->ie_num_entries++];

	return item;
}

static int
set_entry(struct ilog_entries *entries, const struct ilog_id *id, bool punch,
	  int status)
{
	struct ilog_entry *entry;

	entry = alloc_entry(entries);
	if (entry == NULL)
		return -DER_NOMEM;

	entry->ie_id = *id;
	entry->ie_punch = punch;
	entry->ie_status = status;

	return 0;
}

int
ilog_fetch(struct umem_instance *umm, struct ilog_df *root_df,
	   const struct ilog_desc_cbs *cbs, uint32_t intent,
	   struct ilog_entries *entries)
{
	struct ilog_context	*lctx;
	struct ilog_root	*root;
	struct ilog_id		*id_out;
	struct ilog_id		 id = {0};
	struct ilog_priv	*priv = ilog_ent2priv(entries);
	d_iov_t			 id_iov;
	d_iov_t			 val_iov;
	bool			 in_progress = false;
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

	if (root->lr_tree.it_embedded) {
		status = ilog_status_get(lctx, root->lr_id.id_tx_id, intent);
		if (status == -DER_INPROGRESS)
			in_progress = true;
		else if (status < 0)
			D_GOTO(fail, rc = status);
		rc = set_entry(entries, &root->lr_id, root->lr_punch, status);

		if (rc != 0)
			goto fail;
		goto out;
	}

	rc = open_tree_iterator(lctx, &priv->ip_ih);
	if (rc != 0)
		goto fail;

	d_iov_set(&id_iov, &id, sizeof(id));
	id.id_epoch = 0;

	rc = dbtree_iter_probe(priv->ip_ih, BTR_PROBE_GE,
			       DAOS_INTENT_DEFAULT, &id_iov, NULL);
	if (rc == -DER_NONEXIST)
		D_GOTO(out, rc = 0);

	if (rc != 0) {
		D_ERROR("Error probing ilog: "DF_RC"\n", DP_RC(rc));
		goto fail;
	}

	for (;;) {
		d_iov_set(&id_iov, NULL, 0);
		d_iov_set(&val_iov, NULL, 0);
		rc = dbtree_iter_fetch(priv->ip_ih, &id_iov, &val_iov, NULL);
		if (rc != 0) {
			D_ERROR("Error fetching ilog entry from tree:"
				" rc = %s\n", d_errstr(rc));
			goto fail;
		}

		id_out = (struct ilog_id *)id_iov.iov_buf;

		status = ilog_status_get(lctx, id_out->id_tx_id, intent);
		if (status == -DER_INPROGRESS)
			in_progress = true;
		else if (status < 0)
			D_GOTO(fail, rc = status);
		rc = set_entry(entries, id_out, *(bool *)val_iov.iov_buf,
			       status);
		if (rc != 0)
			goto fail;

		rc = dbtree_iter_next(priv->ip_ih);
		if (rc == -DER_NONEXIST)
			D_GOTO(out, rc = 0);
		if (rc != 0)
			goto fail;
	}
out:
	/* We don't exit loop early with -DER_INPROGRESS so we cache the while
	 * log for future updates.
	 */
	if (in_progress)
		rc = -DER_INPROGRESS;

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
		D_FREE(entries->ie_entries);

	if (!daos_handle_is_inval(priv->ip_ih))
		dbtree_iter_finish(priv->ip_ih);
}
