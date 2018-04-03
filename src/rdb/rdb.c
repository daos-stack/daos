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
 * rdb: Databases
 */
#define D_LOGFAC	DD_FAC(rdb)

#include <daos_srv/rdb.h>

#include <daos_srv/daos_mgmt_srv.h>
#include <daos_srv/daos_server.h>
#include "rdb_internal.h"
#include "rdb_layout.h"

/**
 * Create an rdb replica at \a path with \a uuid, \a size, and \a ranks.
 *
 * \param[in]	path	replica path
 * \param[in]	uuid	database UUID
 * \param[in]	size	replica size in bytes
 * \param[in]	ranks	list of replica ranks
 */
int
rdb_create(const char *path, const uuid_t uuid, size_t size,
	   const d_rank_list_t *ranks)
{
	PMEMobjpool	       *pmem;
	PMEMoid			sb_oid;
	struct rdb_sb	       *sb;
	volatile daos_handle_t	attr = DAOS_HDL_INVAL;
	uint8_t			nreplicas = ranks->rl_nr;
	volatile int		rc;

	D_DEBUG(DB_ANY, "creating db %s with %u replicas\n", path, nreplicas);

	pmem = pmemobj_create(path, RDB_LAYOUT, size, 0666);
	if (pmem == NULL) {
		D_ERROR("failed to create db in %s: %d\n", path, errno);
		return daos_errno2der(errno);
	}

	sb_oid = pmemobj_root(pmem, sizeof(*sb));
	if (OID_IS_NULL(sb_oid)) {
		D_ERROR("failed to allocate db superblock in %s\n", path);
		D_GOTO(out_pmem, rc = -DER_NOSPACE);
	}
	sb = pmemobj_direct(sb_oid);

	TX_BEGIN(pmem) {
		struct umem_attr	uma;
		daos_handle_t		tmp;
		daos_iov_t		value;

		/* Initialize the superblock. */
		pmemobj_tx_add_range_direct(sb, sizeof(*sb));
		sb->dsb_magic = RDB_SB_MAGIC;
		uuid_copy(sb->dsb_uuid, uuid);
		uma.uma_id = UMEM_CLASS_PMEM;
		uma.uma_u.pmem_pool = pmem;
		rc = dbtree_create_inplace(DBTREE_CLASS_KV, 0 /* feats */,
					   4 /* order */, &uma, &sb->dsb_attr,
					   &tmp);
		if (rc != 0)
			pmemobj_tx_abort(rc);
		attr = tmp;

		/* Initialize the attribute tree. */
		daos_iov_set(&value, &nreplicas, sizeof(nreplicas));
		rc = dbtree_update(attr, &rdb_attr_nreplicas, &value);
		if (rc != 0)
			pmemobj_tx_abort(rc);
		daos_iov_set(&value, ranks->rl_ranks,
			     sizeof(*ranks->rl_ranks) * nreplicas);
		rc = dbtree_update(attr, &rdb_attr_replicas, &value);
		if (rc != 0)
			pmemobj_tx_abort(rc);
		rc = rdb_raft_init(attr);
		if (rc != 0)
			pmemobj_tx_abort(rc);
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_FINALLY {
		if (!daos_handle_is_inval(attr))
			dbtree_close(attr);
	} TX_END

out_pmem:
	if (rc != 0) {
		if (remove(path) != 0)
			D_ERROR("failed to remove %s: %d\n", path, errno);
	}
	pmemobj_close(pmem);
	return rc;
}

/**
 * Destroy the rdb replica at \a path.
 *
 * \param[in]	path	replica path
 */
int
rdb_destroy(const char *path)
{
	if (remove(path) != 0)
		return daos_errno2der(errno);
	return 0;
}

void
rdb_get(struct rdb *db)
{
	ABT_mutex_lock(db->d_mutex);
	db->d_ref++;
	ABT_mutex_unlock(db->d_mutex);
}

void
rdb_put(struct rdb *db)
{
	ABT_mutex_lock(db->d_mutex);
	D_ASSERTF(db->d_ref > 0, "%d\n", db->d_ref);
	db->d_ref--;
	if (db->d_ref == RDB_BASE_REFS)
		ABT_cond_broadcast(db->d_ref_cv);
	ABT_mutex_unlock(db->d_mutex);
}

static inline struct rdb *
rdb_obj(d_list_t *rlink)
{
	return container_of(rlink, struct rdb, d_entry);
}

static bool
rdb_key_cmp(struct d_hash_table *htable, d_list_t *rlink, const void *key,
	    unsigned int ksize)
{
	struct rdb *db = rdb_obj(rlink);

	D_ASSERTF(ksize == sizeof(uuid_t), "%u\n", ksize);
	return uuid_compare(db->d_uuid, key) == 0;
}

static void
rdb_rec_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	rdb_get(rdb_obj(rlink));
}

static bool
rdb_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	rdb_put(rdb_obj(rlink));
	return false;
}

static d_hash_table_ops_t rdb_hash_ops = {
	.hop_key_cmp	= rdb_key_cmp,
	.hop_rec_addref	= rdb_rec_addref,
	.hop_rec_decref	= rdb_rec_decref,
};

static struct d_hash_table	rdb_hash;
static ABT_mutex		rdb_hash_lock;

int
rdb_hash_init(void)
{
	int rc;

	rc = ABT_mutex_create(&rdb_hash_lock);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);
	rc = d_hash_table_create_inplace(D_HASH_FT_NOLOCK, 4 /* bits */,
					NULL /* priv */, &rdb_hash_ops,
					&rdb_hash);
	if (rc != 0)
		ABT_mutex_free(&rdb_hash_lock);
	return rc;
}

void
rdb_hash_fini(void)
{
	d_hash_table_destroy_inplace(&rdb_hash, true /* force */);
	ABT_mutex_free(&rdb_hash_lock);
}

struct rdb *
rdb_lookup(const uuid_t uuid)
{
	d_list_t *entry;

	ABT_mutex_lock(rdb_hash_lock);
	entry = d_hash_rec_find(&rdb_hash, uuid, sizeof(uuid_t));
	ABT_mutex_unlock(rdb_hash_lock);
	if (entry == NULL)
		return NULL;
	return rdb_obj(entry);
}

/**
 * Start an RDB replica at \a path.
 *
 * \param[in]	path	replica path
 * \param[in]	cbs	callbacks (not copied)
 * \param[in]	arg	argument for cbs
 * \param[out]	dbp	database
 */
int
rdb_start(const char *path, struct rdb_cbs *cbs, void *arg, struct rdb **dbp)
{
	struct rdb	       *db;
	PMEMoid			sb_oid;
	struct rdb_sb	       *sb;
	struct umem_attr	uma;
	daos_iov_t		value;
	uint8_t			nreplicas;
	int			rc;

	D_ASSERT(cbs->dc_stop != NULL);

	D_ALLOC_PTR(db);
	if (db == NULL) {
		D_ERROR("failed to allocate db object\n");
		D_GOTO(err, rc = -DER_NOMEM);
	}

	db->d_ref = 1;
	db->d_cbs = cbs;
	db->d_arg = arg;

	rc = ABT_mutex_create(&db->d_mutex);
	if (rc != ABT_SUCCESS)
		D_GOTO(err_db, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&db->d_ref_cv);
	if (rc != ABT_SUCCESS) {
		D_ERROR("failed to create ref CV: %d\n", rc);
		D_GOTO(err_mutex, rc = dss_abterr2der(rc));
	}

	rc = rdb_tree_cache_create(&db->d_trees);
	if (rc != 0)
		D_GOTO(err_ref_cv, rc);

	db->d_pmem = pmemobj_open(path, RDB_LAYOUT);
	if (db->d_pmem == NULL) {
		D_ERROR("failed to open db in %s: %d\n", path, errno);
		D_GOTO(err_trees, rc = daos_errno2der(errno));
	}

	sb_oid = pmemobj_root(db->d_pmem, sizeof(*sb));
	if (OID_IS_NULL(sb_oid)) {
		D_ERROR("failed to retrieve db superblock in %s\n", path);
		D_GOTO(err_pmem, rc = -DER_IO);
	}
	sb = pmemobj_direct(sb_oid);

	uuid_copy(db->d_uuid, sb->dsb_uuid);

	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_u.pmem_pool = db->d_pmem;
	rc = dbtree_open_inplace(&sb->dsb_attr, &uma, &db->d_attr);
	if (rc != 0) {
		D_ERROR("failed to open db attribute tree: %d\n", rc);
		D_GOTO(err_pmem, rc);
	}

	/* Read the list of replicas. */
	daos_iov_set(&value, &nreplicas, sizeof(nreplicas));
	rc = dbtree_lookup(db->d_attr, &rdb_attr_nreplicas, &value);
	if (rc != 0)
		D_GOTO(err_attr, rc);
	/* Query the address and the length of the persistent list. */
	daos_iov_set(&value, NULL /* buf */, 0 /* size */);
	rc = dbtree_lookup(db->d_attr, &rdb_attr_replicas, &value);
	if (rc != 0)
		D_GOTO(err_attr, rc);
	if (value.iov_len != sizeof(*db->d_replicas->rl_ranks) * nreplicas) {
		D_ERROR(DF_DB": inconsistent replica list: size="DF_U64
			" n=%u\n", DP_DB(db), value.iov_len, nreplicas);
		D_GOTO(err_attr, rc);
	}
	db->d_replicas = daos_rank_list_alloc(nreplicas);
	if (db->d_replicas == NULL)
		D_GOTO(err_attr, rc);
	memcpy(db->d_replicas->rl_ranks, value.iov_buf, value.iov_len);

	rc = rdb_raft_start(db);
	if (rc != 0)
		D_GOTO(err_replicas, rc);

	ABT_mutex_lock(rdb_hash_lock);
	rc = d_hash_rec_insert(&rdb_hash, db->d_uuid, sizeof(uuid_t),
			       &db->d_entry, true /* exclusive */);
	ABT_mutex_unlock(rdb_hash_lock);
	if (rc != 0) {
		/* We have the PMDK pool open. */
		D_ASSERT(rc != -DER_EXIST);
		D_GOTO(err_raft, rc);
	}

	*dbp = db;
	D_DEBUG(DB_ANY, DF_DB": started db %s %p with %u replicas\n", DP_DB(db),
		path, db, nreplicas);
	return 0;

err_raft:
	rdb_raft_stop(db);
err_replicas:
	daos_rank_list_free(db->d_replicas);
err_attr:
	dbtree_close(db->d_attr);
err_pmem:
	pmemobj_close(db->d_pmem);
err_trees:
	rdb_tree_cache_destroy(db->d_trees);
err_ref_cv:
	ABT_cond_free(&db->d_ref_cv);
err_mutex:
	ABT_mutex_free(&db->d_mutex);
err_db:
	D_FREE_PTR(db);
err:
	return rc;
}

/**
 * Stop an RDB replica \a db. All TXs in \a db must be either ended already or
 * blocking only in rdb.
 *
 * \param[in]	db	database
 */
void
rdb_stop(struct rdb *db)
{
	bool deleted;

	D_DEBUG(DB_ANY, DF_DB": stopping db %p\n", DP_DB(db), db);
	ABT_mutex_lock(rdb_hash_lock);
	deleted = d_hash_rec_delete(&rdb_hash, db->d_uuid, sizeof(uuid_t));
	ABT_mutex_unlock(rdb_hash_lock);
	D_ASSERT(deleted);
	rdb_raft_stop(db);
	daos_rank_list_free(db->d_replicas);
	dbtree_close(db->d_attr);
	pmemobj_close(db->d_pmem);
	rdb_tree_cache_destroy(db->d_trees);
	ABT_cond_free(&db->d_ref_cv);
	ABT_mutex_free(&db->d_mutex);
	D_FREE_PTR(db);
}

/**
 * Resign the leadership in \a term. If \a term is not current or this replica
 * is not in leader state, this function does nothing. Otherwise, all TXs in \a
 * term will eventually abort, and the dc_step_down callback will eventually be
 * called with \a term.
 *
 * \param[in]	db	database
 * \param[in]	term	term of leadership to resign
 */
void
rdb_resign(struct rdb *db, uint64_t term)
{
	rdb_raft_resign(db, term);
}

/**
 * Is this replica in the leader state? True does not guarantee a _current_
 * leadership.
 *
 * \param[in]	db	database
 * \param[out]	term	latest term heard of
 */
bool
rdb_is_leader(struct rdb *db, uint64_t *term)
{
	*term = raft_get_current_term(db->d_raft);
	return raft_is_leader(db->d_raft);
}

/**
 * Get a hint of the current leader, if available.
 *
 * \param[in]	db	database
 * \param[out]	term	term of current leader
 * \param[out]	rank	rank of current leader
 *
 * \retval -DER_NONEXIST	no leader hint available
 */
int
rdb_get_leader(struct rdb *db, uint64_t *term, d_rank_t *rank)
{
	raft_node_t	       *node;
	struct rdb_raft_node   *dnode;

	node = raft_get_current_leader_node(db->d_raft);
	if (node == NULL)
		return -DER_NONEXIST;
	dnode = raft_node_get_udata(node);
	D_ASSERT(dnode != NULL);
	*term = raft_get_current_term(db->d_raft);
	*rank = dnode->dn_rank;
	return 0;
}

/**
 * Get the list of replica ranks. Callers are responsible for
 * daos_rank_list_free(*ranksp).
 *
 * \param[in]	db	database
 * \param[out]	ranksp	list of replica ranks
 */
int
rdb_get_ranks(struct rdb *db, d_rank_list_t **ranksp)
{
	return daos_rank_list_dup(ranksp, db->d_replicas);
}

/* I regretted... May move these back to the service level. */
#include <daos_srv/pool.h>

/**
 * Perform a distributed create, if \a create is true, and start operation on
 * all replicas of a database with \a uuid spanning \a ranks. This method can
 * be called on any rank. If \a create is false, \a ranks may be NULL.
 *
 * \param[in]	uuid		database UUID
 * \param[in]	pool_uuid	pool UUID (for ds_mgmt_tgt_file())
 * \param[in]	ranks		list of replica ranks
 * \param[in]	create		create replicas first
 * \param[in]	size		size of each replica in bytes if \a create
 */
int
rdb_dist_start(const uuid_t uuid, const uuid_t pool_uuid,
	       const d_rank_list_t *ranks, bool create, size_t size)
{
	crt_rpc_t	       *rpc;
	struct rdb_start_in    *in;
	struct rdb_start_out   *out;
	int			rc;

	D_ASSERT(!create || ranks != NULL);

	/*
	 * If ranks doesn't include myself, creating a group with ranks will
	 * fail; bcast to the primary group instead.
	 */
	rc = rdb_create_bcast(RDB_START, NULL /* group */, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);
	in = crt_req_get(rpc);
	uuid_copy(in->dai_uuid, uuid);
	uuid_copy(in->dai_pool, pool_uuid);
	if (create)
		in->dai_flags |= RDB_AF_CREATE;
	in->dai_size = size;
	in->dai_ranks = (d_rank_list_t *)ranks;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->dao_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to start%s %d replicas\n",
			DP_UUID(uuid), create ? "/create" : "", rc);
		rdb_dist_stop(uuid, pool_uuid, ranks, create /* destroy */);
		rc = -DER_IO;
	}

out_rpc:
	crt_req_decref(rpc);
out:
	return rc;
}

void
rdb_start_handler(crt_rpc_t *rpc)
{
	struct rdb_start_in    *in = crt_req_get(rpc);
	struct rdb_start_out   *out = crt_reply_get(rpc);
	char		       *path;
	int			rc;

	if (in->dai_flags & RDB_AF_CREATE && in->dai_ranks == NULL)
		D_GOTO(out, rc = -DER_PROTO);

	if (in->dai_ranks != NULL) {
		d_rank_t	rank;
		int		i;

		/* Do nothing if I'm not one of the replicas. */
		rc = crt_group_rank(NULL /* grp */, &rank);
		D_ASSERTF(rc == 0, "%d\n", rc);
		if (!daos_rank_list_find(in->dai_ranks, rank, &i))
			D_GOTO(out, rc = 0);
	}

	path = ds_pool_rdb_path(in->dai_uuid, in->dai_pool);
	if (path == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	if (in->dai_flags & RDB_AF_CREATE) {
		rc = rdb_create(path, in->dai_uuid, in->dai_size,
				in->dai_ranks);
		if (rc != 0 && rc != -DER_EXIST) {
			D_ERROR(DF_UUID": failed to create replica: %d\n",
				DP_UUID(in->dai_uuid), rc);
			D_GOTO(out_path, rc);
		}
	}

	rc = ds_pool_svc_start(in->dai_uuid);
	if (rc != 0) {
		if ((in->dai_flags & RDB_AF_CREATE) || rc != -DER_NONEXIST)
			D_ERROR(DF_UUID": failed to start replica: %d\n",
				DP_UUID(in->dai_uuid), rc);
		if (in->dai_flags & RDB_AF_CREATE)
			rdb_destroy(path);
	}

out_path:
	free(path);
out:
	out->dao_rc = (rc == 0 ? 0 : 1);
	crt_reply_send(rpc);
}

int
rdb_start_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct rdb_start_out   *out_source;
	struct rdb_start_out   *out_result;

	out_source = crt_reply_get(source);
	out_result = crt_reply_get(result);
	out_result->dao_rc += out_source->dao_rc;
	return 0;
}

/**
 * Perform a distributed stop, and if \a destroy is true, destroy operation on
 * all replicas of a database with \a uuid spanning \a ranks. This method can
 * be called on any rank. \a ranks may be NULL.
 *
 * \param[in]	uuid		database UUID
 * \param[in]	pool_uuid	pool UUID (for ds_mgmt_tgt_file())
 * \param[in]	ranks		list of \a ranks->rl_nr replica ranks
 * \param[in]	destroy		destroy after close
 */
int
rdb_dist_stop(const uuid_t uuid, const uuid_t pool_uuid,
	      const d_rank_list_t *ranks, bool destroy)
{
	crt_rpc_t	       *rpc;
	struct rdb_stop_in     *in;
	struct rdb_stop_out    *out;
	int			rc;

	/*
	 * If ranks doesn't include myself, creating a group with ranks will
	 * fail; bcast to the primary group instead.
	 */
	rc = rdb_create_bcast(RDB_STOP, NULL /* group */, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);
	in = crt_req_get(rpc);
	uuid_copy(in->doi_uuid, uuid);
	uuid_copy(in->doi_pool, pool_uuid);
	if (destroy)
		in->doi_flags |= RDB_OF_DESTROY;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->doo_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to stop%s %d replicas\n",
			DP_UUID(uuid), destroy ? "/destroy" : "", rc);
		rc = -DER_IO;
	}

out_rpc:
	crt_req_decref(rpc);
out:
	return rc;
}

void
rdb_stop_handler(crt_rpc_t *rpc)
{
	struct rdb_stop_in     *in = crt_req_get(rpc);
	struct rdb_stop_out    *out = crt_reply_get(rpc);
	int			rc = 0;

	ds_pool_svc_stop(in->doi_uuid);

	if (in->doi_flags & RDB_OF_DESTROY) {
		char *path;

		path = ds_pool_rdb_path(in->doi_uuid, in->doi_pool);
		if (path == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		rc = rdb_destroy(path);
		free(path);
		if (rc == -DER_NONEXIST)
			rc = 0;
		else if (rc != 0)
			D_ERROR(DF_UUID": failed to destroy replica: %d\n",
				DP_UUID(in->doi_uuid), rc);
	}

out:
	out->doo_rc = (rc == 0 ? 0 : 1);
	crt_reply_send(rpc);
}

int
rdb_stop_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct rdb_stop_out   *out_source;
	struct rdb_stop_out   *out_result;

	out_source = crt_reply_get(source);
	out_result = crt_reply_get(result);
	out_result->doo_rc += out_source->doo_rc;
	return 0;
}
