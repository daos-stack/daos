/**
 * (C) Copyright 2017 Intel Corporation.
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
 * rdb: Transactions (TXs)
 *
 *   - TX methods: Check/verify leadership, append entries, and wait for
 *     entries to be applied.
 *   - TX update methods: Pack updates of each TX into an entry.
 *   - TX update applying: Unpack and apply the updates in an entry.
 *   - TX query methods: Call directly into dbtree.
 */

#define DDSUBSYS DDFAC(rdb)

#include <daos_srv/rdb.h>

#include "rdb_internal.h"
#include "rdb_layout.h"

/* Check leadership locally. */
static inline int
rdb_tx_leader_check(struct rdb_tx *tx)
{
	if (!raft_is_leader(tx->dt_db->d_raft))
		return -DER_NOTLEADER;
	if (tx->dt_term != raft_get_current_term(tx->dt_db->d_raft))
		return -DER_NOTLEADER;
	return 0;
}

/**
 * Initialize and begin \a tx. May Argobots-block.
 *
 * If \a term differs from the current term, -DER_NOTLEADER is returned. (An
 * RDB_NIL_TERM \a term is substituted with the current term.) A caller shall
 * tag any DB caches with the term that the caches are valid in, and begin all
 * TXs in that term, so that each TX gets consistent results from cache and DB
 * queries.
 *
 * \param[in]	db	database
 * \param[in]	term	if not RDB_NIL_TERM, term to begin in
 * \param[out]	tx	transaction
 *
 * \retval -DER_NOTLEADER	this replica not current leader
 */
int
rdb_tx_begin(struct rdb *db, uint64_t term, struct rdb_tx *tx)
{
	struct rdb_tx	t = {};
	int		rc;

	if (term == RDB_NIL_TERM)
		term = raft_get_current_term(db->d_raft);
	/*
	 * Wait until the first entry of this term to be applied, so that
	 * queries are possible. Not actually required for update-only
	 * transactions.
	 */
	rc = rdb_raft_wait_applied(db, db->d_debut, term);
	if (rc != 0)
		return rc;
	/*
	 * If this verification succeeds, then queries in this TX will return
	 * valid results.
	 */
	rc = rdb_raft_verify_leadership(db);
	if (rc != 0)
		return rc;
	rdb_get(db);
	t.dt_db = db;
	t.dt_term = term;
	*tx = t;
	return 0;
}

/**
 * Commit \a tx. If successful, then all updates in \a tx are revealed to
 * queries. If an error occurs, then \a tx is aborted.
 *
 * \param[in]	tx	transaction
 *
 * \retval -DER_NOTLEADER	this replica not current leader
 */
int
rdb_tx_commit(struct rdb_tx *tx)
{
	struct rdb     *db = tx->dt_db;
	int		result;
	int		rc;

	/* Don't fail query-only TXs for leader checks. */
	if (tx->dt_entry == NULL)
		return 0;
	rc = rdb_tx_leader_check(tx);
	if (rc != 0)
		return rc;
	rc = rdb_raft_append_apply(db, tx->dt_entry, tx->dt_entry_len, &result);
	if (rc != 0)
		return rc;
	return result;
}

/**
 * End and finalize \a tx. If \a tx is not committed, then all updates in \a tx
 * are discarded.
 *
 * \param[in]	tx	transaction
 */
void
rdb_tx_end(struct rdb_tx *tx)
{
	rdb_put(tx->dt_db);
	if (tx->dt_entry != NULL)
		D__FREE(tx->dt_entry, tx->dt_entry_cap);
}

/* Update operation codes */
enum rdb_tx_opc {
	RDB_TX_INVALID		= 0,
	RDB_TX_CREATE_ROOT	= 1,
	RDB_TX_DESTROY_ROOT	= 2,
	RDB_TX_CREATE		= 3,
	RDB_TX_DESTROY		= 4,
	RDB_TX_UPDATE		= 5,
	RDB_TX_DELETE		= 6,
	RDB_TX_LAST_OPC		= UINT8_MAX
};

static inline char *
rdb_tx_opc_str(enum rdb_tx_opc opc)
{
	switch (opc) {
	case RDB_TX_INVALID:
		return "invalid";
	case RDB_TX_CREATE_ROOT:
		return "create_root";
	case RDB_TX_DESTROY_ROOT:
		return "destroy_root";
	case RDB_TX_CREATE:
		return "create";
	case RDB_TX_DESTROY:
		return "destroy";
	case RDB_TX_UPDATE:
		return "update";
	case RDB_TX_DELETE:
		return "delete";
	default:
		return "unknown";
	}
}

/* Update operation */
struct rdb_tx_op {
	enum rdb_tx_opc		dto_opc;
	rdb_path_t		dto_kvs;
	daos_iov_t		dto_key;
	daos_iov_t		dto_value;
	struct rdb_kvs_attr    *dto_attr;
};

#define DF_TX_OP	"%s("DF_IOV","DF_IOV","DF_IOV",%p)"
#define DP_TX_OP(op)	rdb_tx_opc_str((op)->dto_opc),			\
			DP_IOV(&(op)->dto_kvs), DP_IOV(&(op)->dto_key),	\
			DP_IOV(&(op)->dto_value), op->dto_attr

/* If buf is NULL, then just calculate and return the length required. */
static size_t
rdb_tx_op_encode(struct rdb_tx_op *op, void *buf)
{
	void *p = buf;

	/* opc */
	if (buf != NULL)
		*(uint8_t *)p = op->dto_opc;
	p += sizeof(uint8_t);
	/* kvs */
	p += rdb_encode_iov(&op->dto_kvs, buf == NULL ? NULL : p);
	/* key */
	p += rdb_encode_iov(&op->dto_key, buf == NULL ? NULL : p);
	if (op->dto_opc == RDB_TX_UPDATE) {
		/* value */
		p += rdb_encode_iov(&op->dto_value, buf == NULL ? NULL : p);
	} else if (op->dto_opc == RDB_TX_CREATE_ROOT ||
		   op->dto_opc == RDB_TX_CREATE) {
		/* attr */
		if (buf != NULL)
			*(struct rdb_kvs_attr *)p = *(op->dto_attr);
		p += sizeof(struct rdb_kvs_attr);
	} else {
		D__ASSERT(op->dto_value.iov_buf == NULL);
		D__ASSERT(op->dto_value.iov_buf_len == 0);
		D__ASSERT(op->dto_value.iov_len == 0);
		D__ASSERT(op->dto_attr == NULL);
	}
	return p - buf;
}

/* Returns the number of bytes processed or -DER_IO if the content is bad. */
static ssize_t
rdb_tx_op_decode(const void *buf, size_t len, struct rdb_tx_op *op)
{
	struct rdb_tx_op	o = {};
	const void	       *p = buf;
	ssize_t			n;

	/* opc */
	if (p + sizeof(uint8_t) > buf + len) {
		D__ERROR("truncated opc: %zu < %zu\n", len, sizeof(uint8_t));
		return -DER_IO;
	}
	o.dto_opc = *(const uint8_t *)p;
	p += sizeof(uint8_t);
	/* kvs */
	n = rdb_decode_iov(p, buf + len - p, &o.dto_kvs);
	if (n < 0) {
		D__ERROR("failed to decode kvs\n");
		return n;
	}
	p += n;
	/* key */
	n = rdb_decode_iov(p, buf + len - p, &o.dto_key);
	if (n < 0) {
		D__ERROR("failed to decode key\n");
		return n;
	}
	p += n;
	if (o.dto_opc == RDB_TX_UPDATE) {
		/* value */
		n = rdb_decode_iov(p, buf + len - p, &o.dto_value);
		if (n < 0) {
			D__ERROR("failed to decode value\n");
			return n;
		}
		p += n;
	} else if (o.dto_opc == RDB_TX_CREATE_ROOT ||
		   o.dto_opc == RDB_TX_CREATE) {
		/* attr */
		if (p + sizeof(struct rdb_kvs_attr) > buf + len) {
			D__ERROR("truncated attr: %zu < %zu\n", buf + len - p,
				sizeof(struct rdb_kvs_attr));
			return -DER_IO;
		}
		o.dto_attr = (struct rdb_kvs_attr *)p;
		p += sizeof(struct rdb_kvs_attr);
	}
	*op = o;
	return p - buf;
}

/* Append an update operation to tx->dt_entry. */
static int
rdb_tx_append(struct rdb_tx *tx, struct rdb_tx_op *op)
{
	size_t	len;
	int	rc;

	D__ASSERTF((tx->dt_entry == NULL && tx->dt_entry_cap == 0 &&
		   tx->dt_entry_len == 0) ||
		  (tx->dt_entry != NULL && tx->dt_entry_cap > 0 &&
		   tx->dt_entry_len <= tx->dt_entry_cap),
		  "entry=%p cap=%zu len=%zu\n", tx->dt_entry, tx->dt_entry_cap,
		  tx->dt_entry_len);
	if (op->dto_opc == RDB_TX_CREATE || op->dto_opc == RDB_TX_DESTROY ||
	    op->dto_opc == RDB_TX_UPDATE || op->dto_opc == RDB_TX_DELETE) {
		if (op->dto_key.iov_len == 0)
			return -DER_INVAL;
	}

	rc = rdb_tx_leader_check(tx);
	if (rc != 0)
		return rc;

	/* Calculate and check the additional bytes required. */
	len = rdb_tx_op_encode(op, NULL);
	if (len > tx->dt_entry_cap - tx->dt_entry_len) {
		size_t	new_size = tx->dt_entry_cap;
		void   *new_buf;

		/* Not enough room; reallocate a larger buffer. */
		do {
			if (new_size == 0)
				new_size = 4096;
			else
				new_size *= 2;
		} while (len > new_size - tx->dt_entry_len);
		D__ALLOC(new_buf, new_size);
		if (new_buf == NULL)
			return -DER_NOMEM;
		if (tx->dt_entry_len > 0)
			memcpy(new_buf, tx->dt_entry, tx->dt_entry_len);
		if (tx->dt_entry != NULL)
			D__FREE(tx->dt_entry, tx->dt_entry_cap);
		tx->dt_entry = new_buf;
		tx->dt_entry_cap = new_size;
	}

	/* Now do the actual encoding. */
	rdb_tx_op_encode(op, tx->dt_entry + tx->dt_entry_len);
	tx->dt_entry_len += len;
	return 0;
}

/**
 * Create the root KVS.
 *
 * \param[in]	tx	transaction
 * \param[in]	attr	attributes of root KVS
 *
 * \retval -DER_NOTLEADER	not current leader
 */
int
rdb_tx_create_root(struct rdb_tx *tx, const struct rdb_kvs_attr *attr)
{
	struct rdb_tx_op op = {
		.dto_opc	= RDB_TX_CREATE_ROOT,
		.dto_kvs	= {},
		.dto_key	= {},
		.dto_value	= {},
		.dto_attr	= (struct rdb_kvs_attr *)attr
	};

	return rdb_tx_append(tx, &op);
}

/**
 * Destroy the root KVS. Any child KVSs must have already been destroyed.
 *
 * \param[in]	tx	transaction
 *
 * \retval -DER_NOTLEADER	not current leader
 */
int
rdb_tx_destroy_root(struct rdb_tx *tx)
{
	struct rdb_tx_op op = {
		.dto_opc	= RDB_TX_DESTROY_ROOT,
		.dto_kvs	= {},
		.dto_key	= {},
		.dto_value	= {},
		.dto_attr	= NULL
	};

	return rdb_tx_append(tx, &op);
}

/**
 * Create a new KVS for \a key in KVS \a parent.
 *
 * \param[in]	tx	transaction
 * \param[in]	parent	path to parent KVS
 * \param[in]	key	key in parent KVS
 * \param[in]	attr	attr of new KVS
 *
 * \retval -DER_NOTLEADER	not current leader
 */
int
rdb_tx_create_kvs(struct rdb_tx *tx, const rdb_path_t *parent,
		  const daos_iov_t *key, const struct rdb_kvs_attr *attr)
{
	struct rdb_tx_op op = {
		.dto_opc	= RDB_TX_CREATE,
		.dto_kvs	= *parent,
		.dto_key	= *key,
		.dto_value	= {},
		.dto_attr	= (struct rdb_kvs_attr *)attr
	};

	return rdb_tx_append(tx, &op);
}

/**
 * Destroy the KVS for \a key in KVS \a parent. Any child KVSs must have
 * already been destroyed.
 *
 * \param[in]	tx	transaction
 * \param[in]	parent	path to parent KVS
 * \param[in]	key	key in parent KVS
 *
 * \retval -DER_NOTLEADER	not current leader
 */
int
rdb_tx_destroy_kvs(struct rdb_tx *tx, const rdb_path_t *parent,
		   const daos_iov_t *key)
{
	struct rdb_tx_op op = {
		.dto_opc	= RDB_TX_DESTROY,
		.dto_kvs	= *parent,
		.dto_key	= *key,
		.dto_value	= {},
		.dto_attr	= NULL
	};

	return rdb_tx_append(tx, &op);
}

/**
 * Update the value of \a key in \a kvs to \a value.
 *
 * \param[in]	tx	transaction
 * \param[in]	kvs	path to KVS
 * \param[in]	key	key in KVS
 * \param[in]	value	new value
 *
 * \retval -DER_NOTLEADER	not current leader
 */
int
rdb_tx_update(struct rdb_tx *tx, const rdb_path_t *kvs, const daos_iov_t *key,
	      const daos_iov_t *value)
{
	struct rdb_tx_op op = {
		.dto_opc	= RDB_TX_UPDATE,
		.dto_kvs	= *kvs,
		.dto_key	= *key,
		.dto_value	= *value,
		.dto_attr	= NULL
	};

	return rdb_tx_append(tx, &op);
}

/**
 * Delete \a key in \a kvs.
 *
 * \param[in]	tx	transaction
 * \param[in]	kvs	path to KVS
 * \param[in]	key	key in KVS
 *
 * \retval -DER_NOTLEADER	not current leader
 */
int
rdb_tx_delete(struct rdb_tx *tx, const rdb_path_t *kvs, const daos_iov_t *key)
{
	struct rdb_tx_op op = {
		.dto_opc	= RDB_TX_DELETE,
		.dto_kvs	= *kvs,
		.dto_key	= *key,
		.dto_value	= {},
		.dto_attr	= NULL
	};

	return rdb_tx_append(tx, &op);
}

static int
rdb_tx_apply_op(struct rdb *db, struct rdb_tx_op *op, d_list_t *destroyed)
{
	struct rdb_tree	       *tree = NULL;
	rdb_path_t		victim_path;
	volatile int		rc;

	D__DEBUG(DB_ANY, DF_DB": "DF_TX_OP"\n", DP_DB(db), DP_TX_OP(op));

	if (op->dto_opc != RDB_TX_CREATE_ROOT &&
	    op->dto_opc != RDB_TX_DESTROY_ROOT) {
		/* Look up the tree. */
		rc = rdb_tree_lookup(db, &op->dto_kvs, &tree);
		if (rc != 0)
			return rc;
	}

	/* If destroying a tree, prepare a path to it. */
	if (op->dto_opc == RDB_TX_DESTROY_ROOT) {
		rc = rdb_path_init(&victim_path);
		if (rc != 0)
			return rc;
		rc = rdb_path_push(&victim_path, &rdb_path_root_key);
		if (rc != 0) {
			rdb_path_fini(&victim_path);
			return rc;
		}
	} else if (op->dto_opc == RDB_TX_DESTROY) {
		rc = rdb_path_clone(&op->dto_kvs, &victim_path);
		if (rc != 0) {
			rdb_tree_put(db, tree);
			return rc;
		}
		rc = rdb_path_push(&victim_path, &op->dto_key);
		if (rc != 0) {
			rdb_path_fini(&victim_path);
			rdb_tree_put(db, tree);
			return rc;
		}
	}

	TX_BEGIN(db->d_pmem) {
		switch (op->dto_opc) {
		case RDB_TX_CREATE_ROOT:
			rc = rdb_create_tree(db->d_attr, &rdb_attr_root,
					     op->dto_attr->dsa_class,
					     0 /* feats */,
					     op->dto_attr->dsa_order,
					     NULL /* child */);
			break;
		case RDB_TX_DESTROY_ROOT:
			rc = rdb_destroy_tree(db->d_attr, &rdb_attr_root);
			break;
		case RDB_TX_CREATE:
			rc = rdb_create_tree(tree->de_hdl, &op->dto_key,
					     op->dto_attr->dsa_class,
					     0 /* feats */,
					     op->dto_attr->dsa_order,
					     NULL /* child */);
			break;
		case RDB_TX_DESTROY:
			rc = rdb_destroy_tree(tree->de_hdl, &op->dto_key);
			break;
		case RDB_TX_UPDATE:
			rc = dbtree_update(tree->de_hdl, &op->dto_key,
					   &op->dto_value);
			break;
		case RDB_TX_DELETE:
			rc = dbtree_delete(tree->de_hdl, &op->dto_key, NULL);
			break;
		default:
			D__ERROR(DF_DB": unknown update operation %u\n",
				DP_DB(db), op->dto_opc);
			rc = -DER_IO;
		}
		if (rc != 0)
			pmemobj_tx_abort(rc);
	} TX_ONCOMMIT {
		if (op->dto_opc == RDB_TX_DESTROY_ROOT ||
		    op->dto_opc == RDB_TX_DESTROY) {
			struct rdb_tree	       *victim;
			int			rc_tmp;

			/*
			 * Look up and save victim in destroyed, so that
			 * we can evict it only if the upper-level PMDK TX
			 * commits successfully.
			 */
			rc_tmp = rdb_tree_lookup(db, &victim_path, &victim);
			if (rc_tmp == 0) {
				D__DEBUG(DB_ANY, DF_DB": add to destroyed %p\n",
					DP_DB(db), victim);
				daos_list_add_tail(&victim->de_list, destroyed);
			}
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_FINALLY {
		if (op->dto_opc == RDB_TX_DESTROY_ROOT ||
		    op->dto_opc == RDB_TX_DESTROY)
			rdb_path_fini(&victim_path);
		if (tree != NULL)
			rdb_tree_put(db, tree);
	} TX_END

	return rc;
}

/* Is "error" deterministic? */
static inline bool
rdb_tx_deterministic_error(int error)
{
	return error == -DER_NONEXIST || error == -DER_EXIST ||
	       error == -DER_INVAL || error == -DER_NO_PERM;
}

/*
 * Apply an entry and return the error only if a non-deterministic error
 * happens.  Ask callers to provide memory for destroyed to avoid fiddling with
 * volatiles.
 */
int
rdb_tx_apply(struct rdb *db, uint64_t index, const void *buf, size_t len,
	     void *result, d_list_t *destroyed)
{
	daos_iov_t	value;
	volatile int	rc;

	D__DEBUG(DB_ANY, DF_DB": applying entry "DF_U64": buf=%p len="DF_U64
		"\n", DP_DB(db), index, buf, len);

	daos_iov_set(&value, &index, sizeof(index));

	TX_BEGIN(db->d_pmem) {
		const void *p = buf;

		while (p < buf + len) {
			struct rdb_tx_op	op;
			ssize_t			n;

			n = rdb_tx_op_decode(p, buf + len - p, &op);
			if (n < 0) {
				/* Perhaps due to storage corruptions. */
				D__ERROR(DF_DB": invalid entry format: buf=%p "
					"len="DF_U64" p=%p\n", DP_DB(db), buf,
					len, p);
				pmemobj_tx_abort(n);
			}
			rc = rdb_tx_apply_op(db, &op, destroyed);
			if (rc != 0) {
				if (!rdb_tx_deterministic_error(rc))
					D__ERROR(DF_DB": failed to apply entry "
						DF_U64" op %u <%td, %zd>: %d\n",
						DP_DB(db), index, op.dto_opc,
						p - buf, n, rc);
				pmemobj_tx_abort(rc);
			}
			p += n;
		}
		rc = dbtree_update(db->d_attr, &rdb_attr_applied, &value);
		if (rc != 0)
			pmemobj_tx_abort(rc);
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_FINALLY {
		struct rdb_tree	       *tree;
		struct rdb_tree	       *tmp;

		/*
		 * If rc == 0, then evict the tree objects that have been
		 * destroyed. Otherwise, just release them.
		 */
		daos_list_for_each_entry_safe(tree, tmp, destroyed, de_list) {
			daos_list_del_init(&tree->de_list);
			if (rc == 0) {
				D__DEBUG(DB_ANY, DF_DB": evicting %p\n",
					DP_DB(db), tree);
				rdb_tree_evict(db, tree);
			}
			rdb_tree_put(db, tree);
		}
	} TX_END

	if (rc != 0 && !rdb_tx_deterministic_error(rc))
		return rc;

	if (rc != 0) {
		volatile int rc_tmp;

		/* For deterministic errors, update rdb_attr_applied. */
		TX_BEGIN(db->d_pmem) {
			rc_tmp = dbtree_update(db->d_attr, &rdb_attr_applied,
					       &value);
			if (rc_tmp != 0)
				pmemobj_tx_abort(rc_tmp);
		} TX_ONABORT {
			rc_tmp = umem_tx_errno(rc_tmp);
		} TX_END
		if (rc_tmp != 0)
			return rc_tmp;
	}
	/*
	 * Report the deterministic error to the result buffer, if there is
	 * one, and consider this entry applied.
	 */
	if (result != NULL)
		*(int *)result = rc;
	return 0;
}

/* Called at the beginning of every query. */
static int
rdb_tx_query_pre(struct rdb_tx *tx, const rdb_path_t *path,
		 struct rdb_tree **tree)
{
	int rc;

	rc = rdb_tx_leader_check(tx);
	if (rc != 0)
		return rc;
	return rdb_tree_lookup(tx->dt_db, path, tree);
}

/* Called at the end of every query. */
static void
rdb_tx_query_post(struct rdb_tx *tx, struct rdb_tree *tree)
{
	rdb_tree_put(tx->dt_db, tree);
}

/**
 * Look up the value of \a key in \a kvs.
 *
 * \param[in]		tx	transaction
 * \param[in]		kvs	path to KVS
 * \param[in]		key	key
 * \param[in,out]	value	value
 *
 * \retval -DER_NOTLEADER	not current leader
 */
int
rdb_tx_lookup(struct rdb_tx *tx, const rdb_path_t *kvs, const daos_iov_t *key,
	      daos_iov_t *value)
{
	struct rdb_tree	       *tree;
	int			rc;

	rc = rdb_tx_query_pre(tx, kvs, &tree);
	if (rc != 0)
		return rc;
	rc = dbtree_lookup(tree->de_hdl, (daos_iov_t *)key, value);
	rdb_tx_query_post(tx, tree);
	return rc;
}

/**
 * Perform a probe-and-fetch operation on \a kvs.
 *
 * \param[in]		tx	transaction
 * \param[in]		kvs	path to KVS
 * \param[in]		opc	probe operation
 * \param[in]		key_in	input key
 * \param[out]		key_out	output key
 * \param[in,out]	value	value
 *
 * \retval -DER_NOTLEADER	not current leader
 */
int
rdb_tx_fetch(struct rdb_tx *tx, const rdb_path_t *kvs, enum rdb_probe_opc opc,
	     const daos_iov_t *key_in, daos_iov_t *key_out, daos_iov_t *value)
{
	struct rdb_tree	       *tree;
	int			rc;

	rc = rdb_tx_query_pre(tx, kvs, &tree);
	if (rc != 0)
		return rc;
	rc = dbtree_fetch(tree->de_hdl, (dbtree_probe_opc_t)opc, (daos_iov_t *)key_in, key_out,
			  value);
	rdb_tx_query_post(tx, tree);
	return rc;
}

/**
 * Perform an iteration on \a kvs.
 *
 * \param[in]	tx		transaction
 * \param[in]	kvs		path to KVS
 * \param[in]	backward	key
 * \param[in]	cb		callback
 * \param[in]	arg		argument for callback
 *
 * \retval -DER_NOTLEADER	not current leader
 */
int rdb_tx_iterate(struct rdb_tx *tx, const rdb_path_t *kvs, bool backward,
		   rdb_iterate_cb_t cb, void *arg)
{
	struct rdb_tree	       *tree;
	int			rc;

	rc = rdb_tx_query_pre(tx, kvs, &tree);
	if (rc != 0)
		return rc;
	rc = dbtree_iterate(tree->de_hdl, backward, cb, arg);
	rdb_tx_query_post(tx, tree);
	return rc;
}
