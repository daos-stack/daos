/*
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * rdb: Replicated Database
 *
 * An RDB database comprises a hierarchy of key-value stores (KVSs), much like
 * how a file system comprises a hierarchy of directories. A key-value pair
 * (KV) in a (parent) KVS may be another (child) KVS. A KVS is therefore
 * identified by a path, which is the list of keys leading from the root KVS to
 * the key whose value is the KVS in question. A newly-created database is
 * empty; to store data, callers must first create the root KVS.
 *
 * Each KVS belongs to one of the predefined KVS classes (see rdb_kvs_class).
 * Each value is a nonempty byte stream or a child KVS (see above).
 *
 * The key space of an example database may look like:
 *
 *   rdb_path_root_key {
 *       "containers" {
 *           5742bdea-90e2-4765-ad74-b7f19cb6d78f {
 *               "ghce"
 *               "ghpce"
 *               "lhes" {
 *                   5
 *                   12349875
 *               }
 *               "lres" {
 *                   0
 *                   10
 *               }
 *               "snapshots" {
 *               }
 *               "user.attr_a"
 *               "user.attr_b"
 *           }
 *       }
 *       "container_handles" {
 *           b0733249-0c9a-471b-86e8-027bcfccc6b1
 *           92ccc99c-c755-45f4-b4ee-78fd081e54ca
 *       }
 *   }
 *
 * The RDB API is organized mostly around three types of objects:
 *
 *   - databases
 *   - paths
 *   - transactions
 *
 * And a few distributed helper methods, rdb_dist_*, makes certain distributed
 * tasks easier.
 *
 * All access to the KVSs in a database employ transactions (TX). Ending a TX
 * without committing it discards all its updates. Ending a query-only TX
 * without committing is fine at the moment.
 *
 * A query sees all (conflicting) updates committed (successfully) before its
 * rdb_tx_begin(). It may or may not see updates committed after its
 * rdb_tx_begin(). And, it currently does not see uncommitted updates, even
 * those in the same TX.
 *
 * Updates in a TX are queued, not revealed to queries, until rdb_tx_commit().
 * They are applied sequentially. If one update fails to apply, then the TX is
 * aborted (i.e., all applied updates in the TX are rolled back), and
 * rdb_tx_commit() returns the error.
 *
 * If a TX destroys a KVS, then it must first destroy any child KVSs.
 *
 * If a TX does not include any updates, then rdb_tx_commit() will be a no-op
 * and is not required.
 *
 * Currently, a database can be accessed by only one ES. This is to take
 * advantage of Argobots's non-preemptive scheduling in order to simplify the
 * locking inside rdb.
 *
 * Caller locking rules:
 *
 *   rdb_tx_begin()
 *   rdlock(rl)
 *   rdb_tx_<query>()
 *   rdb_tx_<update>()
 *   wrlock(wl)		// must before commit(); may not cover update()s
 *   rdb_tx_commit()
 *   unlock(wl)		// must after commit()
 *   unlock(rl)		// must after all {rd,wr}lock()s; may before commit()
 *   rdb_tx_end()
 *
 * These cases must be serialized:
 *
 *   - rdb_tx_destroy_{root,kvs}(kvs0) versus any query or update to kvs0 or
 *     any of its child KVSs
 *
 *   - rdb_tx_create_{root,kvs}(kvs0) versus any query or update to kvs0 or any
 *     of its child KVSs
 */

#ifndef DAOS_SRV_RDB_H
#define DAOS_SRV_RDB_H

#include <daos/common.h>
#include <daos_types.h>

/** Database (opaque) */
struct rdb;

/** Database callbacks */
struct rdb_cbs {
	/**
	 * If not NULL, called after this replica becomes the leader of \a
	 * term. A replicated service over rdb may want to take the chance to
	 * start itself on this replica. If an error is returned, rdb steps
	 * down, but without calling dc_step_down. If the error is
	 * -DER_SHUTDOWN, rdb will also call the dc_stop callback to trigger a
	 * replica stop.
	 */
	int (*dc_step_up)(struct rdb *db, uint64_t term, void *arg);

	/**
	 * If not NULL, called after this replica steps down as the leader of
	 * \a term. A replicated service over rdb may want to take the chance
	 * to stop itself on this replica.
	 */
	void (*dc_step_down)(struct rdb *db, uint64_t term, void *arg);

	/**
	 * Called to suggest that this replica shall be stopped due to an
	 * error. A replicated service over rdb shall schedule a rdb_stop()
	 * call made from a non-rdb context (i.e., not in this or any other rdb
	 * callbacks and not inside any rdb TXs) to avoid deadlocks.
	 */
	void (*dc_stop)(struct rdb *db, int err, void *arg);
};

/** Database methods */
int rdb_create(const char *path, const uuid_t uuid, size_t size,
	       const d_rank_list_t *replicas, struct rdb_cbs *cbs, void *arg,
	       struct rdb **dbp);
int rdb_start(const char *path, const uuid_t uuid, struct rdb_cbs *cbs,
	      void *arg, struct rdb **dbp);
void rdb_stop(struct rdb *db);
int rdb_destroy(const char *path, const uuid_t uuid);
void rdb_resign(struct rdb *db, uint64_t term);
int rdb_campaign(struct rdb *db);
bool rdb_is_leader(struct rdb *db, uint64_t *term);
int rdb_get_leader(struct rdb *db, uint64_t *term, d_rank_t *rank);
int rdb_get_ranks(struct rdb *db, d_rank_list_t **ranksp);
void rdb_get_uuid(struct rdb *db, uuid_t uuid);
int rdb_add_replicas(struct rdb *db, d_rank_list_t *replicas);
int rdb_remove_replicas(struct rdb *db, d_rank_list_t *replicas);

/**
 * Path (opaque)
 *
 * A path is a list of keys. An absolute path begins with a special key
 * (rdb_path_root_key) representing the root KVS.
 */
typedef d_iov_t rdb_path_t;

/**
 * Root key (opaque)
 *
 * A special key representing the root KVS in a path.
 */
extern d_iov_t rdb_path_root_key;

/** Path methods */
int rdb_path_init(rdb_path_t *path);
void rdb_path_fini(rdb_path_t *path);
int rdb_path_clone(const rdb_path_t *path, rdb_path_t *new_path);
int rdb_path_push(rdb_path_t *path, const d_iov_t *key);

/**
 * Define a d_iov_t object, named \a prefix + \a name, that represents a
 * constant string key. See rdb_layout.[ch] for an example of the usage of this
 * helper macro.
 */
#define RDB_STRING_KEY(prefix, name)					\
static char	prefix ## name ## _buf[] = #name;			\
d_iov_t		prefix ## name = {					\
	.iov_buf	= prefix ## name ## _buf,			\
	.iov_buf_len	= sizeof(prefix ## name ## _buf),		\
	.iov_len	= sizeof(prefix ## name ## _buf)		\
}

/** KVS classes */
enum rdb_kvs_class {
	RDB_KVS_GENERIC,	/**< hash-ordered byte-stream keys */
	RDB_KVS_INTEGER		/**< numerically-ordered uint64_t keys */
};

/** KVS attributes */
struct rdb_kvs_attr {
	enum rdb_kvs_class	dsa_class;
	unsigned int		dsa_order;	/**< dbtree order (unused) */
};

/**
 * Transaction (TX) (opaque)
 *
 * All fields are private. These are revealed to callers so that they may
 * allocate rdb_tx objects, possibly on their stacks.
 */
struct rdb_tx {
	struct rdb     *dt_db;
	uint64_t	dt_term;	/* raft term this tx begins in */
	void	       *dt_entry;	/* raft entry buffer */
	size_t		dt_entry_cap;	/* buffer capacity */
	size_t		dt_entry_len;	/* data length */
	size_t		dt_num_ops;	/* number of individual operations */
};

/** Nil term */
#define RDB_NIL_TERM UINT64_MAX

/** TX methods */
int rdb_tx_begin(struct rdb *db, uint64_t term, struct rdb_tx *tx);
int rdb_tx_commit(struct rdb_tx *tx);
void rdb_tx_end(struct rdb_tx *tx);

/** TX update methods */
int rdb_tx_create_root(struct rdb_tx *tx, const struct rdb_kvs_attr *attr);
int rdb_tx_destroy_root(struct rdb_tx *tx);
int rdb_tx_create_kvs(struct rdb_tx *tx, const rdb_path_t *parent,
		      const d_iov_t *key, const struct rdb_kvs_attr *attr);
int rdb_tx_destroy_kvs(struct rdb_tx *tx, const rdb_path_t *parent,
		       const d_iov_t *key);
int rdb_tx_update(struct rdb_tx *tx, const rdb_path_t *kvs,
		  const d_iov_t *key, const d_iov_t *value);
int rdb_tx_delete(struct rdb_tx *tx, const rdb_path_t *kvs,
		  const d_iov_t *key);

/** Probe operation codes */
enum rdb_probe_opc {
	RDB_PROBE_FIRST,	/**< first key */
	RDB_PROBE_LAST,		/**< unsupported */
	RDB_PROBE_EQ,		/**< unsupported */
	RDB_PROBE_GE,		/**< unsupported */
	RDB_PROBE_LE		/**< unsupported */
};

/**
 * Iteration callback
 *
 * When a callback returns an rc,
 *   - if rc == 0, rdb_tx_iterate() continues;
 *   - if rc == 1, rdb_tx_iterate() stops and returns 0;
 *   - otherwise, rdb_tx_iterate() stops and returns rc.
 *
 * If a callback yields (e.g., via ABT_thread_yield), it must call
 * rdb_tx_revalidate after the yield and return the return value of
 * rdb_tx_revalidate.
 */
typedef int (*rdb_iterate_cb_t)(daos_handle_t ih, d_iov_t *key,
				d_iov_t *val, void *arg);

/** TX query methods */
int rdb_tx_lookup(struct rdb_tx *tx, const rdb_path_t *kvs,
		  const d_iov_t *key, d_iov_t *value);
int rdb_tx_fetch(struct rdb_tx *tx, const rdb_path_t *kvs,
		 enum rdb_probe_opc opc, const d_iov_t *key_in,
		 d_iov_t *key_out, d_iov_t *value);
int rdb_tx_iterate(struct rdb_tx *tx, const rdb_path_t *kvs, bool backward,
		   rdb_iterate_cb_t cb, void *arg);
int rdb_tx_revalidate(struct rdb_tx *tx);

#endif /* DAOS_SRV_RDB_H */
