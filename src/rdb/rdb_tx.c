/**
 * (C) Copyright 2017-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * rdb: Transactions (TXs)
 *
 *   - TX methods: Check/verify leadership, append entries, and wait for
 *     entries to be applied.
 *   - TX update methods: Pack updates of each TX into an entry.
 *   - TX update applying: Unpack and apply the updates in an entry. Note that
 *     the applied updates become visible only when the entry becomes committed.
 *   - TX query methods: Call directly into vos.
 */

#define D_LOGFAC	DD_FAC(rdb)

#include <daos_srv/rdb.h>

#include <daos_srv/vos.h>
#include "rdb_internal.h"
#include "rdb_layout.h"

/* Flags for rdb_tx.dt_flags */
#define RDB_TX_LOCAL	(1U << 0)	/* local and query-only */

/* Check leadership locally. Caller must hold d_raft_mutex lock. */
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

	ABT_mutex_lock(db->d_raft_mutex);
	if (term == RDB_NIL_TERM)
		term = raft_get_current_term(db->d_raft);
	/*
	 * Wait until the first entry of this term to be applied, so that
	 * queries are possible. Not actually required for update-only
	 * transactions.
	 */
	rc = rdb_raft_wait_applied(db, db->d_debut, term);
	if (rc != 0) {
		ABT_mutex_unlock(db->d_raft_mutex);
		return rc;
	}
	/*
	 * If this verification succeeds, then queries in this TX will return
	 * valid results.
	 */
	rc = rdb_raft_verify_leadership(db);
	ABT_mutex_unlock(db->d_raft_mutex);
	if (rc != 0)
		return rc;
	rdb_get(db);
	t.dt_db = db;
	t.dt_term = term;
	*tx = t;
	return 0;
}

/**
 * Initialize and begin a local, query-only \a tx. The resulting \a tx sees the
 * latest DB contents that may contain uncommitted updates. This is mainly
 * intended for special scenarios such as catastrophic recovery and testing.
 *
 * \param[in]	storage	database storage
 * \param[out]	tx	transaction
 */
int
rdb_tx_begin_local(struct rdb_storage *storage, struct rdb_tx *tx)
{
	struct rdb     *db = rdb_from_storage(storage);
	struct rdb_tx	t = {};

	rdb_get(db);
	t.dt_db = db;
	t.dt_flags = RDB_TX_LOCAL;
	*tx = t;
	return 0;
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
		D_FREE(tx->dt_entry);
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
	d_iov_t			dto_key;
	d_iov_t			dto_value;
	struct rdb_kvs_attr    *dto_attr;
};

/* TX header to indicate if the transaction is critical and must bypass SCM space checks.
 * If more protocol needed (e.g., to convey log compaction), could make this a bit flag.
 */
struct rdb_tx_hdr {
	uint32_t	critical;	/* use VOS_OF_CRIT for all ops in TX? */
};

#define DF_TX_OP	"%s("DF_IOV","DF_IOV","DF_IOV",%p)"
#define DP_TX_OP(op)	rdb_tx_opc_str((op)->dto_opc),			\
			DP_IOV(&(op)->dto_kvs), DP_IOV(&(op)->dto_key),	\
			DP_IOV(&(op)->dto_value), op->dto_attr

/* If buf is NULL, then just calculate and return the length required. */
static size_t
rdb_tx_hdr_encode(struct rdb_tx_hdr *hdr, void *buf)
{
	void	*p = buf;

	if (buf != NULL)
		*(uint32_t *)p = hdr->critical;

	p += sizeof(uint32_t);

	return p - buf;
}

static ssize_t
rdb_tx_hdr_decode(const void *buf, size_t len, struct rdb_tx_hdr *hdr)
{
	struct rdb_tx_hdr	out = {};
	const void	       *p = buf;

	/* critical */
	if (p + sizeof(uint32_t) > buf + len) {
		D_ERROR("truncated hdr: %zu < %zu\n", len, sizeof(uint32_t));
		return -DER_IO;
	}
	out.critical = *(const uint32_t *)p;
	p += sizeof(uint32_t);

	*hdr = out;
	return p - buf;
}

static bool
rdb_tx_is_critical(struct rdb_tx *tx)
{
	struct rdb_tx_hdr	hdr;
	bool			crit = true;

	D_ASSERT(tx != NULL);
	if (tx->dt_entry) {
		ssize_t		nb;

		nb = rdb_tx_hdr_decode(tx->dt_entry, tx->dt_entry_len, &hdr);
		D_ASSERT(nb == sizeof(struct rdb_tx_hdr));
		crit = hdr.critical;
	}
	return crit;
}

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
		D_ASSERT(op->dto_value.iov_buf == NULL);
		D_ASSERT(op->dto_value.iov_buf_len == 0);
		D_ASSERT(op->dto_value.iov_len == 0);
		D_ASSERT(op->dto_attr == NULL);
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
		D_ERROR("truncated opc: %zu < %zu\n", len, sizeof(uint8_t));
		return -DER_IO;
	}
	o.dto_opc = *(const uint8_t *)p;
	p += sizeof(uint8_t);
	/* kvs */
	n = rdb_decode_iov(p, buf + len - p, &o.dto_kvs);
	if (n < 0) {
		D_ERROR("failed to decode kvs\n");
		return n;
	}
	p += n;
	/* key */
	n = rdb_decode_iov(p, buf + len - p, &o.dto_key);
	if (n < 0) {
		D_ERROR("failed to decode key\n");
		return n;
	}
	p += n;
	if (o.dto_opc == RDB_TX_UPDATE) {
		/* value */
		n = rdb_decode_iov(p, buf + len - p, &o.dto_value);
		if (n < 0) {
			D_ERROR("failed to decode value\n");
			return n;
		}
		p += n;
	} else if (o.dto_opc == RDB_TX_CREATE_ROOT ||
		   o.dto_opc == RDB_TX_CREATE) {
		/* attr */
		if (p + sizeof(struct rdb_kvs_attr) > buf + len) {
			D_ERROR("truncated attr: %zu < %zu\n", buf + len - p,
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
rdb_tx_append(struct rdb_tx *tx, struct rdb_tx_op *op, bool is_critical)
{
	struct rdb_tx_hdr	hdr;
	size_t			op_len;
	size_t			len;
	const size_t		RDB_TX_CRITICAL_OPS_LIMIT = 8;
	int			rc;

	D_ASSERT(!(tx->dt_flags & RDB_TX_LOCAL));
	D_ASSERTF((tx->dt_entry == NULL && tx->dt_entry_cap == 0 &&
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

	ABT_mutex_lock(tx->dt_db->d_raft_mutex);
	rc = rdb_tx_leader_check(tx);
	ABT_mutex_unlock(tx->dt_db->d_raft_mutex);
	if (rc != 0)
		return rc;

	/* Calculate and check the additional bytes required (no encoding).
	 * Before first op: insert one uint32_t (boolean) "critical"
	 * interpreted in the raft log_offer execution flow.
	 */
	op_len = rdb_tx_op_encode(op, NULL);
	len = op_len;
	if (tx->dt_entry_len == 0)
		len += rdb_tx_hdr_encode(&hdr, NULL);

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
		D_ALLOC(new_buf, new_size);
		if (new_buf == NULL)
			return -DER_NOMEM;
		if (tx->dt_entry_len > 0)
			memcpy(new_buf, tx->dt_entry, tx->dt_entry_len);
		if (tx->dt_entry != NULL)
			D_FREE(tx->dt_entry);
		tx->dt_entry = new_buf;
		tx->dt_entry_cap = new_size;
	}

	/* TX is critical if it is reasonably-sized, and any op is critical */
	tx->dt_num_ops++;
	if (tx->dt_entry_len == 0) {
		hdr.critical = is_critical ? 1 : 0;
		tx->dt_entry_len += rdb_tx_hdr_encode(&hdr, tx->dt_entry);
	} else if (tx->dt_num_ops > RDB_TX_CRITICAL_OPS_LIMIT) {
		hdr.critical = 0;
		rdb_tx_hdr_encode(&hdr, tx->dt_entry);
	} else if (is_critical) {
		hdr.critical = 1;
		rdb_tx_hdr_encode(&hdr, tx->dt_entry);
	}

	/* Now do the actual encoding. */
	rdb_tx_op_encode(op, tx->dt_entry + tx->dt_entry_len);
	tx->dt_entry_len += op_len;
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
	int		result = 0;
	int		rc;

	/* Don't fail query-only TXs for leader checks. */
	if ((tx->dt_flags & RDB_TX_LOCAL) || tx->dt_entry == NULL)
		return 0;

	ABT_mutex_lock(tx->dt_db->d_raft_mutex);
	rc = rdb_tx_leader_check(tx);
	if (rc != 0) {
		D_ERROR(DF_DB": leader check: "DF_RC"\n", DP_DB(tx->dt_db),
			DP_RC(rc));
		goto out_lock;
	}

	/* If tx is not critical, and out of space (even after log compaction), do not append. */
	if (!rdb_tx_is_critical(tx)) {
		daos_size_t	scm_remaining = 0;
		uint32_t	nchecks = 0;

check_space:
		rc = rdb_scm_left(tx->dt_db, &scm_remaining);
		if (rc != 0) {
			D_ERROR(DF_DB": failed to query free space\n", DP_DB(tx->dt_db));
			goto out_lock;
		}
		nchecks++;

		if (scm_remaining < RDB_NOAPPEND_FREE_SPACE) {
			uint64_t		idx = 0;

			if (nchecks > 1) {
				D_DEBUG(DB_TRACE, DF_DB": nearly out of space, do not append! "
				       "scm_left="DF_U64"\n", DP_DB(tx->dt_db), scm_remaining);
				D_GOTO(out_lock, rc = -DER_NOSPACE);
			}

			/* Compact applied entries (not too often). May recover enough space. */
			if ((daos_getutime() - tx->dt_db->d_nospc_ts) < RDB_NOSPC_ERR_INTVL_USEC) {
				D_DEBUG(DB_TRACE, DF_DB": nearly out of space, but too "
					"early to trigger compaction\n", DP_DB(tx->dt_db));
				goto check_space;	/* will return via nchecks test above */
			}
			D_DEBUG(DB_TRACE, DF_DB": nearly out of space, compact log before retry! "
				"scm_left="DF_U64"\n", DP_DB(tx->dt_db), scm_remaining);
			rc = rdb_raft_trigger_compaction(tx->dt_db, true /* compact_all */, &idx);
			if (rc != 0) {
				D_WARN(DF_DB": failed to trigger compaction!\n", DP_DB(tx->dt_db));
				D_GOTO(out_lock, rc = -DER_NOSPACE);
			}
			while ((idx != 0) && (tx->dt_db->d_lc_record.dlr_aggregated < idx)) {
				sched_cond_wait(tx->dt_db->d_compacted_cv, tx->dt_db->d_raft_mutex);
				D_DEBUG(DB_TRACE, DF_DB": compacted to "DF_U64", need "DF_U64"\n",
					DP_DB(tx->dt_db), tx->dt_db->d_lc_record.dlr_aggregated,
					idx);
			}
			tx->dt_db->d_nospc_ts = daos_getutime();

			/* Do not append if we lost leadership while waiting for log compaction. */
			rc = rdb_tx_leader_check(tx);
			if (rc != 0)
				goto out_lock;

			goto check_space;
		}

		D_DEBUG(DB_TRACE, DF_DB": %s append tx entry to raft log, scm_left="DF_U64"\n",
			DP_DB(tx->dt_db), (nchecks > 1) ? "(after log compaction)" : "",
			scm_remaining);
	}

	rc = rdb_raft_append_apply(tx->dt_db, tx->dt_entry, tx->dt_entry_len,
				   &result);
out_lock:
	ABT_mutex_unlock(tx->dt_db->d_raft_mutex);
	if (rc != 0)
		return rc;
	return result;
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

	return rdb_tx_append(tx, &op, false /* is_critical */);
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

	return rdb_tx_append(tx, &op, true /* is_critical */);
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
		  const d_iov_t *key, const struct rdb_kvs_attr *attr)
{
	struct rdb_tx_op op = {
		.dto_opc	= RDB_TX_CREATE,
		.dto_kvs	= *parent,
		.dto_key	= *key,
		.dto_value	= {},
		.dto_attr	= (struct rdb_kvs_attr *)attr
	};

	return rdb_tx_append(tx, &op, false /* is_critical */);
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
		   const d_iov_t *key)
{
	struct rdb_tx_op op = {
		.dto_opc	= RDB_TX_DESTROY,
		.dto_kvs	= *parent,
		.dto_key	= *key,
		.dto_value	= {},
		.dto_attr	= NULL
	};

	return rdb_tx_append(tx, &op, true /* is_critical */);
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
rdb_tx_update(struct rdb_tx *tx, const rdb_path_t *kvs, const d_iov_t *key,
	      const d_iov_t *value)
{
	struct rdb_tx_op op = {
		.dto_opc	= RDB_TX_UPDATE,
		.dto_kvs	= *kvs,
		.dto_key	= *key,
		.dto_value	= *value,
		.dto_attr	= NULL
	};

	return rdb_tx_append(tx, &op, false /* is_critical */);
}

/**
 * Update the value of \a key in \a kvs to \a value.
 * Mark the TX as critical (to not fail the TX due to SCM free space checks).
 *
 * \param[in]	tx	transaction
 * \param[in]	kvs	path to KVS
 * \param[in]	key	key in KVS
 * \param[in]	value	new value
 *
 * \retval -DER_NOTLEADER	not current leader
 */
int
rdb_tx_update_critical(struct rdb_tx *tx, const rdb_path_t *kvs, const d_iov_t *key,
		       const d_iov_t *value)
{
	struct rdb_tx_op op = {
		.dto_opc	= RDB_TX_UPDATE,
		.dto_kvs	= *kvs,
		.dto_key	= *key,
		.dto_value	= *value,
		.dto_attr	= NULL
	};

	return rdb_tx_append(tx, &op, true /* is_critical */);
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
rdb_tx_delete(struct rdb_tx *tx, const rdb_path_t *kvs, const d_iov_t *key)
{
	struct rdb_tx_op op = {
		.dto_opc	= RDB_TX_DELETE,
		.dto_kvs	= *kvs,
		.dto_key	= *key,
		.dto_value	= {},
		.dto_attr	= NULL
	};

	return rdb_tx_append(tx, &op, true /* is_critical */);
}

static inline int
rdb_oid_class(enum rdb_kvs_class class, rdb_oid_t *oid_class)
{
	switch (class) {
	case RDB_KVS_GENERIC:
		*oid_class = RDB_OID_CLASS_GENERIC;
		return 0;
	case RDB_KVS_INTEGER:
		*oid_class = RDB_OID_CLASS_INTEGER;
		return 0;
	default:
		return -DER_IO;
	}
}

static int
rdb_tx_apply_create(struct rdb *db, uint64_t index, rdb_oid_t parent,
		    d_iov_t *key, enum rdb_kvs_class class, bool crit)
{
	d_iov_t	value;
	rdb_oid_t	oid_class;
	rdb_oid_t	oid_number;
	rdb_oid_t	oid;
	int		rc;

	/* Convert the KVS class into the object ID class. */
	rc = rdb_oid_class(class, &oid_class);
	if (rc != 0) {
		D_ERROR(DF_DB": unknown KVS class %x: %d\n", DP_DB(db), class,
			rc);
		return rc;
	}

	/* Does the KVS already exist? */
	d_iov_set(&value, NULL, sizeof(rdb_oid_t));
	rc = rdb_lc_lookup(db->d_lc, index, parent, key, &value);
	if (rc == 0) {
		return -DER_EXIST;
	} else if (rc != -DER_NONEXIST) {
		D_ERROR(DF_DB": failed to check KVS existence: %d\n", DP_DB(db),
			rc);
		return rc;
	}

	/* Allocate an object for the new KVS. */
	d_iov_set(&value, &oid_number, sizeof(oid_number));
	rc = rdb_lc_lookup(db->d_lc, index, RDB_LC_ATTRS, &rdb_lc_oid_next,
			   &value);
	if (rc == -DER_NONEXIST) {
		oid_number = RDB_LC_OID_NEXT_INIT;
		D_DEBUG(DB_MD, DF_DB": initialized rdb_lc_oid_next to "DF_U64
			"\n", DP_DB(db), oid_number);
	} else if (rc != 0) {
		D_ERROR(DF_DB": failed to look up next object number: %d\n",
			DP_DB(db), rc);
		return rc;
	}
	if ((oid_number & RDB_OID_CLASS_MASK) != 0) {
		D_ERROR(DF_DB": invalid next object number: "DF_X64"\n",
			DP_DB(db), oid_number);
		return -DER_IO;
	}
	oid = oid_class | oid_number;
	oid_number += 1;

	/* Update the next object number. */
	rc = rdb_lc_update(db->d_lc, index, RDB_LC_ATTRS, crit, 1 /* n */,
			   &rdb_lc_oid_next, &value);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to update next object number"DF_X64
			": %d\n", DP_DB(db), oid_number, rc);
		return rc;
	}

	/* Update the key in the parent object. */
	d_iov_set(&value, &oid, sizeof(oid));
	rc = rdb_lc_update(db->d_lc, index, parent, crit, 1 /* n */,
			   key, &value);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to update parent KVS: %d\n", DP_DB(db),
			rc);
		return rc;
	}

	return 0;
}

static int
rdb_tx_apply_destroy(struct rdb *db, uint64_t index, rdb_oid_t parent,
		     d_iov_t *key)
{
	d_iov_t	value;
	rdb_oid_t	oid;
	int		rc;

	/* Does the KVS exist? */
	d_iov_set(&value, &oid, sizeof(oid));
	rc = rdb_lc_lookup(db->d_lc, index, parent, key, &value);
	if (rc != 0) {
		if (rc != -DER_NONEXIST)
			D_ERROR(DF_DB": failed to check KVS existence: %d\n",
				DP_DB(db), rc);
		return rc;
	}

	/* Punch the key in the parent object. */
	rc = rdb_lc_punch(db->d_lc, index, parent, 1 /* n */, key);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to update parent KVS "DF_X64": %d\n",
			DP_DB(db), parent, rc);
		return rc;
	}

	/* Punch the KVS object. */
	rc = rdb_lc_punch(db->d_lc, index, oid, 0 /* n */, NULL /* akeys */);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to punch KVS "DF_X64": %d\n", DP_DB(db),
			oid, rc);
		return rc;
	}

	return 0;
}

static int
rdb_tx_apply_update(struct rdb *db, uint64_t index, rdb_oid_t kvs,
		    d_iov_t *key, d_iov_t *value, bool crit)
{
	int rc;

	rc = rdb_lc_update(db->d_lc, index, kvs, crit, 1 /* n */, key, value);
	if (rc != 0)
		D_ERROR(DF_DB ": failed to update KVS " DF_X64 ": " DF_RC "\n", DP_DB(db), kvs,
			DP_RC(rc));
	return rc;
}

static int
rdb_tx_apply_delete(struct rdb *db, uint64_t index, rdb_oid_t kvs,
		    d_iov_t *key)
{
	int rc;

	rc = rdb_lc_punch(db->d_lc, index, kvs, 1 /* n */, key);
	if (rc != 0)
		D_ERROR(DF_DB": failed to update KVS "DF_X64": %d\n", DP_DB(db),
			kvs, rc);
	return rc;
}

static int
rdb_tx_apply_op(struct rdb *db, uint64_t index, struct rdb_tx_op *op, bool crit)
{
	struct rdb_kvs *kvs = NULL;
	rdb_path_t	victim_path;
	int		rc;

	D_DEBUG(DB_TRACE, DF_DB": "DF_TX_OP"\n", DP_DB(db), DP_TX_OP(op));

	if (op->dto_opc != RDB_TX_CREATE_ROOT &&
	    op->dto_opc != RDB_TX_DESTROY_ROOT) {
		/* Look up the KVS. */
		rc = rdb_kvs_lookup(db, &op->dto_kvs, index, true /* alloc */,
				    &kvs);
		if (rc != 0)
			goto out;
	}

	/* If destroying a KVS, prepare a path to it. */
	if (op->dto_opc == RDB_TX_DESTROY_ROOT) {
		rc = rdb_path_init(&victim_path);
		if (rc != 0)
			goto out_kvs;
		rc = rdb_path_push(&victim_path, &rdb_path_root_key);
		if (rc != 0)
			goto out_victim_path;
	} else if (op->dto_opc == RDB_TX_DESTROY) {
		rc = rdb_path_clone(&op->dto_kvs, &victim_path);
		if (rc != 0)
			goto out_kvs;
		rc = rdb_path_push(&victim_path, &op->dto_key);
		if (rc != 0)
			goto out_victim_path;
	}

	switch (op->dto_opc) {
	case RDB_TX_CREATE_ROOT:
		rc = rdb_tx_apply_create(db, index, RDB_LC_ATTRS, &rdb_lc_root,
					 op->dto_attr->dsa_class, crit);
		break;
	case RDB_TX_CREATE:
		rc = rdb_tx_apply_create(db, index, kvs->de_object,
					 &op->dto_key, op->dto_attr->dsa_class,
					 crit);
		break;
	case RDB_TX_DESTROY_ROOT:
		rc = rdb_tx_apply_destroy(db, index, RDB_LC_ATTRS,
					  &rdb_lc_root);
		break;
	case RDB_TX_DESTROY:
		rc = rdb_tx_apply_destroy(db, index, kvs->de_object,
					  &op->dto_key);
		break;
	case RDB_TX_UPDATE:
		rc = rdb_tx_apply_update(db, index, kvs->de_object,
					 &op->dto_key, &op->dto_value, crit);
		break;
	case RDB_TX_DELETE:
		rc = rdb_tx_apply_delete(db, index, kvs->de_object,
					 &op->dto_key);
		break;
	default:
		D_ERROR(DF_DB": unknown update operation %u\n",
			DP_DB(db), op->dto_opc);
		rc = -DER_IO;
	}
	if (rc != 0)
		goto out_victim_path;

	if (op->dto_opc == RDB_TX_DESTROY_ROOT ||
	    op->dto_opc == RDB_TX_DESTROY) {
		struct rdb_kvs *victim;
		int		rc_tmp;

		/*
		 * Evict the matching rdb_kvs object (if any). Since this TX
		 * destroys victim_path, no other TXs can query or update
		 * victim_path or its child KVSs (if any). Hence, until this TX
		 * releases the lock for victim_path after the rdb_tx_commit()
		 * call returns, no other TXs will look up victim_path in the
		 * rdb_kvs cache.
		 */
		rc_tmp = rdb_kvs_lookup(db, &victim_path, index,
					false /* alloc */, &victim);
		if (rc_tmp == 0) {
			D_DEBUG(DB_TRACE, DF_DB": evicting kvs %p\n",
				DP_DB(db), victim);
			rdb_kvs_evict(db, victim);
			rdb_kvs_put(db, victim);
		}
	}

out_victim_path:
	if (op->dto_opc == RDB_TX_DESTROY_ROOT ||
	    op->dto_opc == RDB_TX_DESTROY)
		rdb_path_fini(&victim_path);
out_kvs:
	if (kvs != NULL)
		rdb_kvs_put(db, kvs);
out:
	return rc;
}

/* Is "error" deterministic? */
static inline bool
rdb_tx_deterministic_error(int error)
{
	return error == -DER_NONEXIST || error == -DER_MISMATCH ||
	       error == -DER_INVAL || error == -DER_NO_PERM;
}

/*
 * Apply an entry and return the error only if a nondeterministic error
 * happens. This function tries to discard index if an error occurs.
 * Interpret header to know if ops in the TX are deemed "critical".
 */
int
rdb_tx_apply(struct rdb *db, uint64_t index, const void *buf, size_t len,
	     void *result, bool *critp)
{
	const void	       *p = buf;
	ssize_t			n;
	bool			crit = true;
	daos_size_t		scm_remaining = 0;
	int			rc = 0;

	rc = rdb_scm_left(db, &scm_remaining);
	if (rc != 0) {
		D_ERROR(DF_DB": could not query free space: "DF_RC"\n",
			DP_DB(db), DP_RC(rc));
		return rc;
	}

	if (buf) {
		struct rdb_tx_hdr	hdr;

		n = rdb_tx_hdr_decode(p, sizeof(struct rdb_tx_hdr), &hdr);
		if (n < 0) {
			D_ERROR(DF_DB": invalid header: buf=%p, len="DF_U64"\n",
				DP_DB(db), buf, sizeof(struct rdb_tx_hdr));
			rc = n;
			return rc;
		}
		p += n;
		crit = hdr.critical;

		/* scm_remaining < RDB_NOAPPEND_FREE_SPACE can happen on
		 * on follower after leader compacts log first.
		 * Warn only when critically low on space.
		 */
		if (!crit && (scm_remaining < RDB_CRITICAL_FREE_SPACE)) {
			D_WARN(DF_DB": space is tight! index "DF_U64" buf=%p "
			       "len="DF_U64" crit=%d scm_left="DF_U64"\n",
			       DP_DB(db), index, buf, len, crit, scm_remaining);
		}
	}

	D_DEBUG(DB_TRACE, DF_DB": applying index "DF_U64": buf=%p len="DF_U64
		" crit=%d, scm_left="DF_U64"\n", DP_DB(db), index, buf, len,
		crit, scm_remaining);

	while (p < buf + len) {
		struct rdb_tx_op	op;

		n = rdb_tx_op_decode(p, buf + len - p, &op);
		if (n < 0) {
			D_ERROR(DF_DB": invalid entry format: buf=%p len="DF_U64
				" p=%p\n", DP_DB(db), buf, len, p);
			rc = n;
			break;
		}
		rc = rdb_tx_apply_op(db, index, &op, crit);
		if (rc != 0) {
			if (!rdb_tx_deterministic_error(rc))
				D_ERROR(DF_DB ": failed to apply entry " DF_U64
					      " op %u <%td, %zd>: " DF_RC "\n",
					DP_DB(db), index, op.dto_opc, p - buf, n, DP_RC(rc));
			break;
		}
		p += n;
	}

	/*
	 * If an error occurs after we have potentially made some
	 * modifications, empty the rdb_kvs cache (to evict any rdb_kvs
	 * objects corresponding to KVSs created by this TX) and discard all
	 * updates in index. Don't bother with undoing the exact set of changes
	 * made by this TX, as nondeterministic errors must be rare and
	 * deterministic errors can be easily avoided by rdb callers.
	 */
	if (rc != 0) {
		int rc_tmp;

		rdb_kvs_cache_evict(db->d_kvss);
		rc_tmp = rdb_lc_discard(db->d_lc, index, index);
		if (rc_tmp != 0) {
			D_ERROR(DF_DB ": failed to discard entry " DF_U64 ": " DF_RC "\n",
				DP_DB(db), index, DP_RC(rc_tmp));
			if (rdb_tx_deterministic_error(rc))
				return rc_tmp;
			else
				return rc;
		}
	}

	if (rc != 0 && !rdb_tx_deterministic_error(rc))
		return rc;

	/*
	 * Report the deterministic error to the result buffer, if there is
	 * one, and consider this entry applied.
	 */
	if (result != NULL)
		*(int *)result = rc;

	*critp = crit;
	return 0;
}

/* Called at the beginning of every query. */
static int
rdb_tx_query_pre(struct rdb_tx *tx, const rdb_path_t *path,
		 struct rdb_kvs **kvs, uint64_t *index)
{
	uint64_t	i;
	int		rc;

	ABT_mutex_lock(tx->dt_db->d_raft_mutex);
	if (tx->dt_flags & RDB_TX_LOCAL) {
		i = tx->dt_db->d_lc_record.dlr_tail - 1;
	} else {
		i = tx->dt_db->d_applied;
		rc = rdb_tx_leader_check(tx);
		if (rc != 0) {
			ABT_mutex_unlock(tx->dt_db->d_raft_mutex);
			return rc;
		}
	}
	ABT_mutex_unlock(tx->dt_db->d_raft_mutex);

	if (path == NULL)
		return 0;

	rc = rdb_kvs_lookup(tx->dt_db, path, i, true /* alloc */, kvs);
	if (rc != 0)
		return rc;

	*index = i;
	return 0;
}

/* Called at the end of every query. */
static void
rdb_tx_query_post(struct rdb_tx *tx, struct rdb_kvs *kvs)
{
	rdb_kvs_put(tx->dt_db, kvs);
}

/**
 * Look up the value of \a key in \a kvs.
 *
 * If \a value->iov_len is nonzero and different from the actual value length,
 * then -DER_MISMATCH is returned and \a value outputs the actual value.
 *
 * \param[in]		tx	transaction
 * \param[in]		kvs	path to KVS
 * \param[in]		key	key
 * \param[in,out]	value	value
 *
 * \retval -DER_NOTLEADER	not current leader
 * \retval -DER_MISMATCH	unexpected value length
 */
int
rdb_tx_lookup(struct rdb_tx *tx, const rdb_path_t *kvs, const d_iov_t *key,
	      d_iov_t *value)
{
	struct rdb     *db = tx->dt_db;
	struct rdb_kvs *s;
	uint64_t	i;
	int		rc;

	rc = rdb_tx_query_pre(tx, kvs, &s, &i);
	if (rc != 0)
		return rc;
	rc = rdb_lc_lookup(db->d_lc, i, s->de_object, (d_iov_t *)key, value);
	rdb_tx_query_post(tx, s);
	return rc;
}

/**
 * Perform a probe-and-fetch operation on \a kvs.
 *
 * If \a value->iov_len is nonzero and different from the actual value length,
 * then -DER_MISMATCH is returned and \a value outputs the actual value.
 *
 * \param[in]		tx	transaction
 * \param[in]		kvs	path to KVS
 * \param[in]		opc	probe operation
 * \param[in]		key_in	input key
 * \param[out]		key_out	output key
 * \param[in,out]	value	value
 *
 * \retval -DER_NOTLEADER	not current leader
 * \retval -DER_MISMATCH	unexpected value length
 */
int
rdb_tx_fetch(struct rdb_tx *tx, const rdb_path_t *kvs, enum rdb_probe_opc opc,
	     const d_iov_t *key_in, d_iov_t *key_out, d_iov_t *value)
{
	struct rdb     *db = tx->dt_db;
	struct rdb_kvs *s;
	uint64_t	i;
	int		rc;

	rc = rdb_tx_query_pre(tx, kvs, &s, &i);
	if (rc != 0)
		return rc;
	rc = rdb_lc_iter_fetch(db->d_lc, i, s->de_object, opc, (d_iov_t *)key_in, key_out, value);
	rdb_tx_query_post(tx, s);
	return rc;
}

/* Find the largest integer key in \a kvs
 * \param[in]		tx	transaction
 * \param[in]		kvs	path to a KVS with an integer key
 * \param[out]		key_out	output maximum key
 *
 * \retval -DER_NOTLEADER	not current leader
 * \retval -DER_NONEXIST	no keys (KVS is empty)
 */
int
rdb_tx_query_key_max(struct rdb_tx *tx, const rdb_path_t *kvs, d_iov_t *key_out)
{
	struct rdb     *db = tx->dt_db;
	struct rdb_kvs *s;
	uint64_t	i;
	int		rc;

	rc = rdb_tx_query_pre(tx, kvs, &s, &i);
	if (rc != 0)
		return rc;
	rc = rdb_lc_query_key_max(db->d_lc, i, s->de_object, key_out);
	if (rc != 0) {
		D_ERROR(DF_DB": rdb_lc_query_key_max index="DF_U64", d_applied="DF_U64", rdb_oid="
			DF_U64"\n", DP_DB(db), i, db->d_applied, s->de_object);
	}
	rdb_tx_query_post(tx, s);
	return rc;
}

/**
 * Perform an iteration on \a kvs.
 *
 * If \a cb yields, it must call rdb_tx_revalidate. See rdb_iterate_cb_t and
 * rdb_tx_revalidate.
 *
 * \param[in]	tx		transaction
 * \param[in]	kvs		path to KVS
 * \param[in]	backward	direction (false unsupported)
 * \param[in]	cb		callback
 * \param[in]	arg		argument for callback
 *
 * \retval -DER_NOTLEADER	not current leader
 */
int
rdb_tx_iterate(struct rdb_tx *tx, const rdb_path_t *kvs, bool backward, rdb_iterate_cb_t cb,
	       void *arg)
{
	struct rdb     *db = tx->dt_db;
	struct rdb_kvs *s;
	uint64_t	i;
	int		rc;

	rc = rdb_tx_query_pre(tx, kvs, &s, &i);
	if (rc != 0)
		return rc;
	rc = rdb_lc_iterate(db->d_lc, i, s->de_object, backward, cb, arg);
	rdb_tx_query_post(tx, s);
	return rc;
}

/**
 * Revalidate the TX after yielding in the middle of a TX query. Currently,
 * this only applies to rdb_iterate_cb_t implementations that need to yield. If
 * this function returns an error, the TX query shall abort with the error.
 *
 * Rationale: If the leadership is lost during the yield, a new leader may have
 * modified the data this TX query is accessing, regardless of rdb callers'
 * locking.
 *
 * \param[in]	tx	transaction
 *
 * \retval -DER_NOTLEADER	not current leader
 */
int
rdb_tx_revalidate(struct rdb_tx *tx)
{
	return rdb_tx_query_pre(tx, NULL /* path */, NULL /* kvs */, NULL /* index */);
}
