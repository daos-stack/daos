/**
 * (C) Copyright 2019-2020 Intel Corporation.
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

union prec {
	struct {
		/** Transaction id */
		uint32_t	p_tx_id;
		/** Punch minor epoch */
		uint16_t	p_punch_minor_eph;
		/** Update minor epoch */
		uint16_t	p_update_minor_eph;
	};
	uint64_t		p_value;
};

static inline void
id2prec(union prec *prec, const struct ilog_id *id)
{
	D_ASSERT(id->id_update_minor_eph != id->id_punch_minor_eph);
	prec->p_value = id->id_value;
}

static inline void
prec2id(struct ilog_id *id, const union prec *prec)
{
	D_ASSERT(prec->p_update_minor_eph != prec->p_punch_minor_eph);
	id->id_value = prec->p_value;
}


static inline union prec *
rec2prec(struct btr_record *rec)
{
	return (union prec *)&rec->rec_off;
}

D_CASSERT(sizeof(struct ilog_id) == sizeof(struct ilog_tree));
D_CASSERT(sizeof(struct ilog_root) == sizeof(struct ilog_df));
/** We hijack the value offset to store the actual value inline */
D_CASSERT(sizeof(union prec) <= sizeof(((struct btr_record *)0)->rec_off));
D_CASSERT(sizeof(union prec) == sizeof(((struct ilog_id *)0)->id_value));

/**
 * Customized functions for btree.
 */

static int
ilog_rec_msize(int alloc_overhead)
{
	/** No extra allocation for ilog entries */
	return 0;
}

/** create a new key-record, or install an externally allocated key-record */
static int
ilog_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov,
	       d_iov_t *val_iov, struct btr_record *rec)
{
	union prec		*prec = rec2prec(rec);

	D_ASSERT(val_iov->iov_len == sizeof(*prec));
	*prec = *(union prec *)val_iov->iov_buf;

	return 0;
}

static inline int
ilog_is_same_tx(struct ilog_context *lctx, uint32_t tx_id, daos_epoch_t epoch,
		bool *same)
{
	struct ilog_desc_cbs	*cbs = &lctx->ic_cbs;

	*same = true;

	if (!cbs->dc_is_same_tx_cb)
		return 0;

	return cbs->dc_is_same_tx_cb(&lctx->ic_umm, tx_id, epoch, same,
				     cbs->dc_is_same_tx_args);
}

static int
ilog_status_get(struct ilog_context *lctx, uint32_t tx_id,
		daos_epoch_t epoch, uint32_t intent)
{
	struct ilog_desc_cbs	*cbs = &lctx->ic_cbs;
	int			 rc;

	if (tx_id == UMOFF_NULL)
		return ILOG_COMMITTED;

	if (!cbs->dc_log_status_cb)
		return ILOG_COMMITTED;

	rc = cbs->dc_log_status_cb(&lctx->ic_umm, tx_id, epoch, intent,
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

	D_DEBUG(DB_TRACE, "Registered ilog="DF_X64" epoch="DF_X64" tx_id=%d\n",
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
		" tx_id=%d\n", deregister ? "Deregistered" : "Removed",
		lctx->ic_root_off, id->id_epoch, id->id_tx_id);

	return 0;
}

static inline void
ilog_rec2id(struct ilog_id *id, struct btr_record *rec)
{
	union prec	*prec = rec2prec(rec);

	id->id_epoch = rec->rec_ukey[0];
	prec2id(id, prec);
}

static int
ilog_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct ilog_id		id;
	struct ilog_context	*lctx = args;

	if (lctx == NULL)
		return 0;

	ilog_rec2id(&id, rec);

	/* For current DTX, we need to remove the forward reference.  I think
	 * eventually, this callback will go away as we will undo the operation
	 * on the key rather than specifically the incarnation log record.
	 */
	return ilog_log_del(lctx, &id, true);
}

static int
ilog_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	       d_iov_t *key_iov, d_iov_t *val_iov)
{
	daos_epoch_t	*epoch = (daos_epoch_t *)&rec->rec_ukey[0];
	union prec	*prec = rec2prec(rec);

	D_ASSERT(key_iov != NULL);
	D_ASSERT(val_iov != NULL);

	if (key_iov->iov_buf == NULL) {
		d_iov_set(key_iov, epoch, sizeof(*epoch));
	} else {
		D_ASSERT(sizeof(*epoch) == key_iov->iov_buf_len);

		memcpy(key_iov->iov_buf, epoch, sizeof(*epoch));
		key_iov->iov_len = sizeof(*epoch);
	}
	if (val_iov->iov_buf == NULL) {
		d_iov_set(val_iov, prec, sizeof(*prec));
	} else {
		D_ASSERT(sizeof(*prec) == val_iov->iov_buf_len);

		memcpy(val_iov->iov_buf, prec, sizeof(*prec));
		val_iov->iov_len = sizeof(*prec);
	}

	return 0;
}

static btr_ops_t ilog_btr_ops = {
	.to_rec_msize		= ilog_rec_msize,
	.to_rec_alloc		= ilog_rec_alloc,
	.to_rec_free		= ilog_rec_free,
	.to_rec_fetch		= ilog_rec_fetch,
};

int
ilog_init(void)
{
	int	rc;

	rc = dbtree_class_register(VOS_BTR_ILOG, BTR_FEAT_UINT_KEY,
				   &ilog_btr_ops);
	if (rc != 0)
		D_ERROR("Failed to register incarnation log btree class: %s\n",
			d_errstr(rc));

	return rc;
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
		D_DEBUG(DB_TRACE, "Removing destroyed entry "DF_X64" in root\n",
			lctx.ic_root->lr_id.id_epoch);
		saved_id = lctx.ic_root->lr_id;
	}

	rc = ilog_ptr_set(&lctx, &lctx.ic_root->lr_magic, &tmp);

	if (rc == 0)
		rc = ilog_log_del(&lctx, &saved_id, true);
fail:
	rc = ilog_tx_end(&lctx, rc);

	return rc;
}

static int
ilog_root_migrate(struct ilog_context *lctx, const struct ilog_id *id_in)
{
	union prec		 prec;
	daos_epoch_t		 epoch;
	struct ilog_root	*root;
	struct ilog_root	 tmp = {0};
	d_iov_t			 key_iov;
	d_iov_t			 val_iov;
	umem_off_t		 tree_root;
	daos_handle_t		 toh = DAOS_HDL_INVAL;
	struct umem_attr	 uma;
	struct ilog_id		 id = *id_in;
	int			 rc = 0;

	root = lctx->ic_root;

	rc = ilog_tx_begin(lctx);
	if (rc != 0) {
		D_ERROR("Failed to start PMDK transaction: rc = %s\n",
			d_errstr(rc));
		goto done;
	}

	umem_attr_get(&lctx->ic_umm, &uma);
	rc = dbtree_create(VOS_BTR_ILOG, BTR_FEAT_UINT_KEY, ILOG_TREE_ORDER,
			   &uma, &tree_root, &toh);
	if (rc != 0) {
		D_ERROR("Failed to create an incarnation log tree: rc = %s\n",
			d_errstr(rc));
		goto done;
	}

	lctx->ic_ver_inc = true;
	d_iov_set(&key_iov, &epoch, sizeof(epoch));
	d_iov_set(&val_iov, &prec, sizeof(prec));

	epoch = root->lr_id.id_epoch;
	id2prec(&prec, &root->lr_id);

	rc = dbtree_update(toh, &key_iov, &val_iov);
	if (rc != 0) {
		D_ERROR("Failed to add entry to incarnation log: %s\n",
			d_errstr(rc));
		goto done;
	}

	rc = ilog_log_add(lctx, &id);
	if (rc != 0)
		goto done;

	epoch = id.id_epoch;
	prec.p_value = id.id_value;

	rc = dbtree_update(toh, &key_iov, &val_iov);
	if (rc != 0) {
		D_ERROR("Failed to add entry to incarnation log: %s\n",
			d_errstr(rc));
		goto done;
	}

	tmp.lr_tree.it_root = tree_root;
	tmp.lr_tree.it_embedded = 0;
	tmp.lr_magic = ilog_ver_inc(lctx);
	tmp.lr_ts_idx = root->lr_ts_idx;

	rc = ilog_ptr_set(lctx, root, &tmp);

done:
	if (!daos_handle_is_inval(toh))
		dbtree_close(toh);

	return rc;
}

static int
check_equal(struct ilog_context *lctx, daos_epoch_t *epoch_out,
	    union prec *prec_out, const struct ilog_id *id_in, bool update,
	    bool *is_equal)
{
	int	rc;

	*is_equal = false;

	if (id_in->id_epoch != *epoch_out)
		return 0;

	if (update) {
		rc = ilog_is_same_tx(lctx, prec_out->p_tx_id, *epoch_out,
				     is_equal);
		if (rc != 0)
			return rc;
	} else if (id_in->id_tx_id == prec_out->p_tx_id) {
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
			    prec_out->p_punch_minor_eph >
			    prec_out->p_update_minor_eph)
				return -DER_ALREADY;

			if (id_in->id_update_minor_eph &&
			    prec_out->p_update_minor_eph >
			    prec_out->p_punch_minor_eph)
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
update_inplace(struct ilog_context *lctx, daos_epoch_t *epoch_out,
	       union prec *prec_out, const struct ilog_id *id_in, int opc,
	       bool *is_equal)
{
	union prec	saved_prec;
	int		rc;

	rc = check_equal(lctx, epoch_out, prec_out, id_in,
			 opc == ILOG_OP_UPDATE, is_equal);
	if (rc != 0 || !*is_equal || opc == ILOG_OP_ABORT)
		return rc;

	saved_prec.p_value = prec_out->p_value;
	if (opc == ILOG_OP_PERSIST) {
		D_DEBUG(DB_TRACE, "Setting "DF_X64" to persistent\n",
			id_in->id_epoch);
		saved_prec.p_tx_id = 0;
		goto set_prec;
	}

	if (saved_prec.p_punch_minor_eph > saved_prec.p_update_minor_eph &&
	    id_in->id_punch_minor_eph)
		return 0; /** Already a punch */
	if (saved_prec.p_update_minor_eph > saved_prec.p_punch_minor_eph &&
	    id_in->id_update_minor_eph)
		return 0; /** Already an update */

	if (saved_prec.p_punch_minor_eph < id_in->id_punch_minor_eph)
		saved_prec.p_punch_minor_eph = id_in->id_punch_minor_eph;
	else if (saved_prec.p_update_minor_eph < id_in->id_update_minor_eph)
		saved_prec.p_update_minor_eph = id_in->id_update_minor_eph;

	if (saved_prec.p_value == prec_out->p_value)
		return 0; /* Nothing to do */

	/* New operation has a new minor epoch.  Update the old entry
	 * accordingly.
	 */
	D_DEBUG(DB_TRACE, "Updating "DF_X64
		" lid=%d punch=(%d->%d) update=(%d-%d)\n", id_in->id_epoch,
		prec_out->p_tx_id, prec_out->p_punch_minor_eph,
		saved_prec.p_punch_minor_eph, prec_out->p_update_minor_eph,
		saved_prec.p_update_minor_eph);

set_prec:
	if (saved_prec.p_update_minor_eph == saved_prec.p_punch_minor_eph) {
		D_ERROR("Matching punch/update minor epoch not allowed\n");
		return -DER_NO_PERM;
	}
	return ilog_ptr_set(lctx, prec_out, &saved_prec);
}

static int
collapse_tree(struct ilog_context *lctx, daos_handle_t *toh)
{
	struct ilog_root	*root = lctx->ic_root;
	struct ilog_root	 tmp = {0};
	union prec		 prec = {0};
	daos_epoch_t		 epoch = 0;
	struct btr_attr		 attr;
	d_iov_t			 key_iov;
	d_iov_t			 val_iov;
	int			 rc;

	rc = dbtree_query(*toh, &attr, NULL);
	if (attr.ba_count > 1)
		return 0;

	d_iov_set(&key_iov, &epoch, sizeof(epoch));
	d_iov_set(&val_iov, &prec, sizeof(prec));
	rc = dbtree_fetch(*toh, BTR_PROBE_GT, DAOS_INTENT_DEFAULT, &key_iov,
			  &key_iov, &val_iov);
	if (rc != 0 && rc != -DER_NONEXIST) {
		D_ERROR("dbtree_fetch failed: rc = %s\n", d_errstr(rc));
		return rc;
	}

	rc = dbtree_destroy(*toh, NULL);
	if (rc != 0) {
		D_ERROR("Could not destroy table: rc = %s\n", d_errstr(rc));
		return rc;
	}

	*toh = DAOS_HDL_INVAL;

	tmp.lr_magic = ilog_ver_inc(lctx);
	tmp.lr_id.id_epoch = epoch;
	tmp.lr_id.id_value = prec.p_value;
	tmp.lr_ts_idx = root->lr_ts_idx;

	rc = ilog_ptr_set(lctx, root, &tmp);

	return rc;
}

static int
consolidate_tree(struct ilog_context *lctx, const daos_epoch_range_t *epr,
		 daos_handle_t *toh, int opc, const struct ilog_id *id_in)
{
	int			 rc = 0;

	rc = ilog_tx_begin(lctx);
	if (rc != 0)
		return rc;

	D_ASSERT(opc == ILOG_OP_ABORT);

	rc = dbtree_delete(*toh, BTR_PROBE_BYPASS, NULL, NULL);
	if (rc != 0)
		return rc;

	return collapse_tree(lctx, toh);
}

static int
ilog_tree_modify(struct ilog_context *lctx, const struct ilog_id *id_in,
		 const daos_epoch_range_t *epr, int opc)
{
	struct ilog_root	*root;
	union prec		*prec_out;
	daos_epoch_t		*epoch_out;
	daos_epoch_t		 epoch = id_in->id_epoch;
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

	d_iov_set(&key_iov_in, &epoch, sizeof(epoch));
	d_iov_set(&key_iov, NULL, 0);
	d_iov_set(&val_iov, NULL, 0);
	rc = dbtree_fetch(toh, BTR_PROBE_LE, DAOS_INTENT_DEFAULT, &key_iov_in,
			  &key_iov, &val_iov);

	if (rc == -DER_NONEXIST)
		goto insert;

	if (rc != 0) {
		D_ERROR("Fetch of ilog entry failed: rc = %s\n", d_errstr(rc));
		goto done;
	}

	epoch_out = key_iov.iov_buf;
	prec_out = val_iov.iov_buf;

	visibility = ILOG_UNCOMMITTED;

	if (*epoch_out <= epr->epr_hi &&
	    *epoch_out >= epr->epr_lo) {
		visibility = ilog_status_get(lctx, prec_out->p_tx_id,
					     *epoch_out, DAOS_INTENT_UPDATE);
		if (visibility < 0) {
			rc = visibility;
			goto done;
		}
	}

	rc = update_inplace(lctx, epoch_out, prec_out, id_in, opc, &is_equal);
	if (rc != 0)
		goto done;

	if (is_equal) {
		if (opc != ILOG_OP_ABORT)
			goto done;

		rc = consolidate_tree(lctx, epr, &toh, opc, id_in);

		goto done;
	}

	if (opc != ILOG_OP_UPDATE) {
		D_DEBUG(DB_TRACE, "No entry found, done\n");
		goto done;
	}

	if (id_in->id_punch_minor_eph == 0 && visibility != ILOG_UNCOMMITTED &&
	    prec_out->p_update_minor_eph > prec_out->p_punch_minor_eph)
		goto done;
insert:
	rc = ilog_tx_begin(lctx);
	if (rc != 0)
		goto done;

	rc = ilog_log_add(lctx, &id);
	if (rc != 0)
		goto done;

	D_ASSERT(id.id_punch_minor_eph == id_in->id_punch_minor_eph);
	D_ASSERT(id.id_update_minor_eph == id_in->id_update_minor_eph);

	d_iov_set(&val_iov, &id.id_value, sizeof(id.id_value));
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
	    const daos_epoch_range_t *epr, int opc)
{
	struct ilog_context	*lctx;
	struct ilog_root	*root;
	union prec		*prec;
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
		visibility = ilog_status_get(lctx, root->lr_id.id_tx_id,
					     root->lr_id.id_epoch,
					     DAOS_INTENT_UPDATE);
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

		prec = (union prec *)&root->lr_id.id_value;
		rc = update_inplace(lctx, &root->lr_id.id_epoch, prec, id_in,
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

	return ilog_modify(loh, id, &range, ILOG_OP_PERSIST);
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
		status = ilog_status_get(lctx, entry->ie_id.id_tx_id,
					 entry->ie_id.id_epoch, intent);
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
	lctx->ic_root = root;
	lctx->ic_root_off = umem_ptr2off(umm, root);
	lctx->ic_umm = *umm;
	lctx->ic_cbs = *cbs;
	lctx->ic_ref = 0;
	lctx->ic_in_txn = false;
	lctx->ic_ver_inc = false;

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
set_entry(struct ilog_entries *entries, const struct ilog_id *id, int status)
{
	struct ilog_entry *entry;

	entry = alloc_entry(entries);
	if (entry == NULL)
		return -DER_NOMEM;

	entry->ie_id = *id;
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
	struct ilog_id		 id = {0};
	struct ilog_priv	*priv = ilog_ent2priv(entries);
	d_iov_t			 key_iov;
	d_iov_t			 val_iov;
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
		status = ilog_status_get(lctx, root->lr_id.id_tx_id,
					 root->lr_id.id_epoch, intent);
		if (status != -DER_INPROGRESS && status < 0)
			D_GOTO(fail, rc = status);
		rc = set_entry(entries, &root->lr_id, status);
		if (rc != 0)
			goto fail;

		goto out;
	}

	rc = open_tree_iterator(lctx, &priv->ip_ih);
	if (rc != 0)
		goto fail;

	d_iov_set(&key_iov, &id.id_epoch, sizeof(id.id_epoch));

	rc = dbtree_iter_probe(priv->ip_ih, BTR_PROBE_GE,
			       DAOS_INTENT_DEFAULT, &key_iov, NULL);
	if (rc == -DER_NONEXIST)
		D_GOTO(out, rc = 0);

	if (rc != 0) {
		D_ERROR("Error probing ilog: "DF_RC"\n", DP_RC(rc));
		goto fail;
	}

	for (;;) {
		d_iov_set(&key_iov, &id.id_epoch, sizeof(id.id_epoch));
		d_iov_set(&val_iov, &id.id_value, sizeof(id.id_value));
		rc = dbtree_iter_fetch(priv->ip_ih, &key_iov, &val_iov, NULL);
		if (rc != 0) {
			D_ERROR("Error fetching ilog entry from tree:"
				" rc = %s\n", d_errstr(rc));
			goto fail;
		}

		status = ilog_status_get(lctx, id.id_tx_id,
					 id.id_epoch, intent);
		if (status != -DER_INPROGRESS && status < 0)
			D_GOTO(fail, rc = status);
		rc = set_entry(entries, &id, status);
		if (rc != 0)
			goto fail;

		rc = dbtree_iter_next(priv->ip_ih);
		if (rc == -DER_NONEXIST)
			D_GOTO(out, rc = 0);
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
		D_FREE(entries->ie_entries);

	if (!daos_handle_is_inval(priv->ip_ih))
		dbtree_iter_finish(priv->ip_ih);
}

static int
remove_ilog_entry(struct ilog_context *lctx, daos_handle_t *toh,
		  const struct ilog_entry *entry, int *removed)
{
	struct ilog_id	id = entry->ie_id;
	d_iov_t		iov;
	int		rc;

	rc = ilog_tx_begin(lctx);
	if (rc != 0)
		return rc;
	D_DEBUG(DB_TRACE, "Removing ilog entry at "DF_X64"\n",
		entry->ie_id.id_epoch);
	d_iov_set(&iov, &id.id_epoch, sizeof(id.id_epoch));
	rc = dbtree_delete(*toh, BTR_PROBE_EQ, &iov, lctx);
	if (rc != 0) {
		D_ERROR("Could not remove entry from tree: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}
	D_DEBUG(DB_TRACE, "Removed ilog entry at "DF_X64"\n",
		entry->ie_id.id_epoch);

	(*removed)++;

	return 0;
}

struct agg_arg {
	const daos_epoch_range_t	*aa_epr;
	const struct ilog_entry		*aa_prev;
	const struct ilog_entry		*aa_prior_punch;
	daos_epoch_t			 aa_punched;
	bool				 aa_discard;
};

enum {
	AGG_RC_DONE,
	AGG_RC_NEXT,
	AGG_RC_REMOVE,
	AGG_RC_REMOVE_PREV,
};

static int
check_agg_entry(const struct ilog_entry *entry, struct agg_arg *agg_arg)
{
	int	rc;

	D_DEBUG(DB_TRACE, "Entry "DF_X64" punch=%s prev="DF_X64
		" prior_punch="DF_X64"\n", entry->ie_id.id_epoch,
		ilog_is_punch(entry) ? "yes" : "no",
		agg_arg->aa_prev ? agg_arg->aa_prev->ie_id.id_epoch : 0,
		agg_arg->aa_prior_punch ?
		agg_arg->aa_prior_punch->ie_id.id_epoch : 0);

	if (entry->ie_id.id_epoch > agg_arg->aa_epr->epr_hi)
		D_GOTO(done, rc = AGG_RC_DONE);
	if (entry->ie_id.id_epoch < agg_arg->aa_epr->epr_lo) {
		if (entry->ie_id.id_epoch <= agg_arg->aa_punched) {
			/* Skip entries outside of the range and
			 * punched by the parent
			 */
			D_GOTO(done, rc = AGG_RC_NEXT);
		}
		if (ilog_is_punch(entry)) {
			/* Just save the prior punch entry */
			agg_arg->aa_prior_punch = entry;
		} else {
			/* A create covers the prior punch */
			agg_arg->aa_prior_punch = NULL;
		}
		D_GOTO(done, rc = AGG_RC_NEXT);
	}

	/* With purge set, there should not be uncommitted entries */
	D_ASSERT(entry->ie_status != ILOG_UNCOMMITTED);

	if (agg_arg->aa_discard || entry->ie_status == ILOG_REMOVED ||
	    agg_arg->aa_punched >= entry->ie_id.id_epoch) {
		/* Remove stale entry or punched entry */
		D_GOTO(done, rc = AGG_RC_REMOVE);
	}

	if (agg_arg->aa_prev != NULL) {
		const struct ilog_entry	*prev = agg_arg->aa_prev;
		bool			 punch = ilog_is_punch(prev);

		if (!punch) {
			/* punched by outer level */
			punch = prev->ie_id.id_epoch <= agg_arg->aa_punched;
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

	if (agg_arg->aa_prev == NULL) {
		/* No punched entry to remove */
		D_GOTO(done, rc = AGG_RC_REMOVE);
	}

	if (agg_arg->aa_prev->ie_id.id_epoch < agg_arg->aa_epr->epr_lo) {
		/** Data punched is not in range */
		agg_arg->aa_prior_punch = entry;
		D_GOTO(done, rc = AGG_RC_NEXT);
	}

	D_ASSERT(!ilog_is_punch(agg_arg->aa_prev));

	/* Punch is redundant or covers nothing.  Remove it. */
	rc = AGG_RC_REMOVE_PREV;
done:
	return rc;
}

int
ilog_aggregate(struct umem_instance *umm, struct ilog_df *ilog,
	       const struct ilog_desc_cbs *cbs, const daos_epoch_range_t *epr,
	       bool discard, daos_epoch_t punched, struct ilog_entries *entries)
{
	struct ilog_priv	*priv = ilog_ent2priv(entries);
	struct ilog_context	*lctx;
	struct ilog_entry	*entry;
	struct agg_arg		 agg_arg;
	struct ilog_root	*root;
	struct ilog_root	 tmp = {0};
	struct ilog_id		 old_id;
	struct umem_attr	 uma;
	bool			 empty = false;
	int			 rc = 0;
	int			 removed = 0;
	daos_handle_t		 toh = DAOS_HDL_INVAL;

	D_ASSERT(epr != NULL);
	D_ASSERT(punched <= epr->epr_hi);

	D_DEBUG(DB_TRACE, "%s incarnation log: epr: "DF_X64"-"DF_X64" punched="
		DF_X64"\n", discard ? "Discard" : "Aggregate", epr->epr_lo,
		epr->epr_hi, punched);

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

	agg_arg.aa_epr = epr;
	agg_arg.aa_prev = NULL;
	agg_arg.aa_prior_punch = NULL;
	agg_arg.aa_punched = punched;
	agg_arg.aa_discard = discard;

	if (root->lr_tree.it_embedded) {
		entry = &entries->ie_entries[0];
		rc = check_agg_entry(&entries->ie_entries[0], &agg_arg);

		switch (rc) {
		case AGG_RC_DONE:
		case AGG_RC_NEXT:
			rc = 0;
			break;
		case AGG_RC_REMOVE:
			old_id = root->lr_id;
			tmp.lr_ts_idx = root->lr_ts_idx;
			tmp.lr_magic = ilog_ver_inc(lctx);
			rc = ilog_ptr_set(lctx, root, &tmp);
			if (rc != 0)
				break;

			empty = true;
			rc = ilog_log_del(lctx, &old_id, true);
			D_DEBUG(DB_TRACE, "Removed ilog entry at "DF_X64" "DF_RC
				"\n", entry->ie_id.id_epoch, DP_RC(rc));
			if (rc == 0)
				removed++;
			break;
		case AGG_RC_REMOVE_PREV:
			/* Fall through: Should not get this here */
		default:
			D_ASSERT(0);
		}
		goto done;
	}

	umem_attr_get(&lctx->ic_umm, &uma);
	rc = dbtree_open(root->lr_tree.it_root, &uma, &toh);
	if (rc != 0) {
		D_ERROR("Failed to open incarnation log tree: "DF_RC
			"\n", DP_RC(rc));
		return rc;
	}
	ilog_foreach_entry(entries, entry) {
		rc = check_agg_entry(entry, &agg_arg);

		switch (rc) {
		case AGG_RC_DONE:
			goto collapse;
		case AGG_RC_NEXT:
			agg_arg.aa_prev = entry;
			break;
		case AGG_RC_REMOVE_PREV:
			rc = remove_ilog_entry(lctx, &toh, agg_arg.aa_prev,
					       &removed);
			if (rc != 0)
				goto done;

			agg_arg.aa_prev = agg_arg.aa_prior_punch;
			/* Fall through */
		case AGG_RC_REMOVE:
			rc = remove_ilog_entry(lctx, &toh, entry, &removed);
			if (rc != 0)
				goto done;
			break;
		default:
			/* Unknown return code */
			D_ASSERT(0);
		}
	}
collapse:
	rc = collapse_tree(lctx, &toh);

	empty = ilog_empty(root);
done:
	if (!daos_handle_is_inval(toh))
		dbtree_close(toh);

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
