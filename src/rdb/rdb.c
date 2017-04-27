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

#define DD_SUBSYS DD_FAC(rdb)

#include <daos_srv/rdb.h>

#include <daos_srv/daos_mgmt_srv.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/pool.h>
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
	   const daos_rank_list_t *ranks)
{
	PMEMobjpool	       *pmem;
	PMEMoid			sb_oid;
	struct rdb_sb	       *sb;
	volatile daos_handle_t	attr = DAOS_HDL_INVAL;
	uint8_t			nreplicas = ranks->rl_nr.num;
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

/* Currently, only one rdb is supported. */
struct rdb *the_one_rdb_hack;

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
	daos_rank_list_t	replicas;
	int			rc;

	D_ALLOC_PTR(db);
	if (db == NULL) {
		D_ERROR("failed to allocate db object\n");
		D_GOTO(err, rc = -DER_NOMEM);
	}

	db->d_ref = 1;
	db->d_cbs = cbs;
	db->d_arg = arg;
	db->d_log = DAOS_HDL_INVAL;
	DAOS_INIT_LIST_HEAD(&db->d_replies);

	rc = rdb_tree_cache_create(&db->d_trees);
	if (rc != 0)
		D_GOTO(err_rdb, rc);

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
	if (value.iov_len != sizeof(*replicas.rl_ranks) * nreplicas) {
		D_ERROR(DF_DB": inconsistent replica list: size="DF_U64
			" n=%u\n", DP_DB(db), value.iov_len, nreplicas);
		D_GOTO(err_attr, rc);
	}
	D_ALLOC(replicas.rl_ranks, sizeof(*replicas.rl_ranks) * nreplicas);
	if (replicas.rl_ranks == NULL)
		D_GOTO(err_attr, rc);
	memcpy(replicas.rl_ranks, value.iov_buf, value.iov_len);
	replicas.rl_nr.num = nreplicas;
	replicas.rl_nr.num_out = nreplicas;

	rc = rdb_raft_start(db, &replicas);
	D_FREE(replicas.rl_ranks,
	       sizeof(*replicas.rl_ranks) * replicas.rl_nr.num);
	if (rc != 0)
		D_GOTO(err_attr, rc);

	D_ASSERT(the_one_rdb_hack == NULL);
	the_one_rdb_hack = db;
	*dbp = db;
	D_DEBUG(DB_ANY, "started db %s %p with %u replicas\n", path, db,
		nreplicas);
	return 0;

err_attr:
	dbtree_close(db->d_attr);
err_pmem:
	pmemobj_close(db->d_pmem);
err_trees:
	rdb_tree_cache_destroy(db->d_trees);
err_rdb:
	D_FREE_PTR(db);
err:
	return rc;
}

/**
 * Stop an RDB replica \a db.
 *
 * \param[in]	db	database
 */
void
rdb_stop(struct rdb *db)
{
	/* TODO: Design a real shutdown procedure. */
	D_DEBUG(DB_ANY, "stopping db %p\n", db);
	rdb_raft_stop(db);
	dbtree_close(db->d_attr);
	pmemobj_close(db->d_pmem);
	rdb_tree_cache_destroy(db->d_trees);
	D_FREE_PTR(db);
	D_ASSERT(the_one_rdb_hack != NULL);
	the_one_rdb_hack = NULL;
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
 * Get a hint of the rank of current leader.
 *
 * \param[in]	db	database
 * \param[out]	rank	rank of current leader
 *
 * \retval -DER_NONEXIST	no leader hint available
 */
int
rdb_get_leader(struct rdb *db, crt_rank_t *rank)
{
	raft_node_t	       *node;
	struct rdb_raft_node   *dnode;

	node = raft_get_current_leader_node(db->d_raft);
	if (node == NULL)
		return -DER_NONEXIST;
	dnode = raft_node_get_udata(node);
	D_ASSERT(dnode != NULL);
	*rank = dnode->dn_rank;
	return 0;
}

/**
 * Perform a distributed create, if \a create is true, and start operation on
 * all replicas of a database with \a uuid spanning \a ranks. This method can
 * be called on any rank. If \a create is false, \a ranks may be NULL, in which
 * case the RDB_START RPC will be broadcasted in the primary group.
 *
 * \param[in]	uuid		database UUID
 * \param[in]	pool_uuid	pool UUID (for ds_mgmt_tgt_file())
 * \param[in]	ranks		list of replica ranks
 * \param[in]	create		create replicas first
 * \param[in]	size		size of each replica in bytes if \a create
 */
int
rdb_dist_start(const uuid_t uuid, const uuid_t pool_uuid,
	       const daos_rank_list_t *ranks, bool create, size_t size)
{
	crt_group_t	       *group = NULL;
	crt_rpc_t	       *rpc;
	struct rdb_start_in    *in;
	struct rdb_start_out   *out;
	int			rc;

	D_ASSERT(!create || ranks != NULL);

	if (ranks != NULL) {
		rc = dss_group_create("rdb_ephemeral_group",
				      (daos_rank_list_t *)ranks, &group);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	rc = rdb_create_bcast(RDB_START, group, &rpc);
	if (rc != 0)
		D_GOTO(out_group, rc);

	in = crt_req_get(rpc);
	uuid_copy(in->dai_uuid, uuid);
	uuid_copy(in->dai_pool, pool_uuid);
	if (create)
		in->dai_flags |= RDB_AF_CREATE;
	in->dai_size = size;
	in->dai_ranks = (daos_rank_list_t *)ranks;

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
out_group:
	if (group != NULL)
		dss_group_destroy(group);
out:
	return rc;
}

int
rdb_start_handler(crt_rpc_t *rpc)
{
#if 0
	struct rdb_start_in    *in = crt_req_get(rpc);
#endif
	struct rdb_start_out   *out = crt_reply_get(rpc);
#if 0
	char		       *path;
	int			rc;

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
		D_ERROR(DF_UUID": failed to start replica: %d\n",
			DP_UUID(in->dai_uuid), rc);
		if (in->dai_flags & RDB_AF_CREATE)
			rdb_destroy(path);
	}

out_path:
	free(path);
out:
	out->dao_rc = (rc == 0 ? 0 : 1);
#else
	out->dao_rc = 0;
#endif
	return crt_reply_send(rpc);
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
 * be called on any rank. \a ranks may be NULL, in which case the RDB_STOP RPC
 * will be broadcasted in the primary group.
 *
 * \param[in]	uuid		database UUID
 * \param[in]	pool_uuid	pool UUID (for ds_mgmt_tgt_file())
 * \param[in]	ranks		list of \a ranks->rl_nr.num_out replica ranks
 * \param[in]	destroy		destroy after close
 */
int
rdb_dist_stop(const uuid_t uuid, const uuid_t pool_uuid,
	      const daos_rank_list_t *ranks, bool destroy)
{
	crt_group_t	       *group = NULL;
	crt_rpc_t	       *rpc;
	struct rdb_stop_in     *in;
	struct rdb_stop_out    *out;
	int			rc;

	if (ranks != NULL) {
		rc = dss_group_create("rdb_ephemeral_group",
				      (daos_rank_list_t *)ranks, &group);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	rc = rdb_create_bcast(RDB_STOP, group, &rpc);
	if (rc != 0)
		D_GOTO(out_group, rc);

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
out_group:
	if (group != NULL)
		dss_group_destroy(group);
out:
	return rc;
}

int
rdb_stop_handler(crt_rpc_t *rpc)
{
#if 0
	struct rdb_stop_in     *in = crt_req_get(rpc);
#endif
	struct rdb_stop_out    *out = crt_reply_get(rpc);
	int			rc = 0;

#if 0
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
#endif
	out->doo_rc = (rc == 0 ? 0 : 1);
	return crt_reply_send(rpc);
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
