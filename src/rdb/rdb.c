/**
 * (C) Copyright 2017-2019 Intel Corporation.
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
#include <daos_srv/vos.h>
#include "rdb_internal.h"
#include "rdb_layout.h"

/**
 * Create an RDB replica at \a path with \a uuid, \a size, and \a replicas.
 *
 * \param[in]	path		replica path
 * \param[in]	uuid		database UUID
 * \param[in]	size		replica size in bytes
 * \param[in]	replicas	list of replica ranks
 */
int
rdb_create(const char *path, const uuid_t uuid, size_t size,
	   const d_rank_list_t *replicas)
{
	daos_handle_t	pool;
	daos_handle_t	mc;
	d_iov_t	value;
	int		rc;

	D_DEBUG(DB_MD, DF_UUID": creating db %s with %u replicas\n",
		DP_UUID(uuid), path, replicas == NULL ? 0 : replicas->rl_nr);

	/* Create and open a VOS pool. */
	rc = vos_pool_create(path, (unsigned char *)uuid, size, 0);
	if (rc != 0)
		goto out;
	/* RDB pools specify small=true for basic system memory reservation */
	rc = vos_pool_open(path, (unsigned char *)uuid, true, &pool);
	if (rc != 0)
		goto out_pool;

	/* Create and open the metadata container. */
	rc = vos_cont_create(pool, (unsigned char *)uuid);
	if (rc != 0)
		goto out_pool_hdl;
	rc = vos_cont_open(pool, (unsigned char *)uuid, &mc);
	if (rc != 0)
		goto out_pool_hdl;

	/* Initialize Raft. */
	rc = rdb_raft_init(pool, mc, replicas);
	if (rc != 0)
		goto out_mc_hdl;

	/*
	 * Mark this replica as fully initialized by storing its UUID.
	 * rdb_start() checks this attribute when starting a DB.
	 */
	d_iov_set(&value, (void *)uuid, sizeof(uuid_t));
	rc = rdb_mc_update(mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_uuid, &value);

out_mc_hdl:
	vos_cont_close(mc);
out_pool_hdl:
	vos_pool_close(pool);
out_pool:
	if (rc != 0) {
		int rc_tmp;

		rc_tmp = vos_pool_destroy(path, (unsigned char *)uuid);
		if (rc_tmp != 0)
			D_ERROR(DF_UUID": failed to destroy %s: %d\n",
				DP_UUID(uuid), path, rc_tmp);
	}
out:
	return rc;
}

/**
 * Destroy the rdb replica at \a path.
 *
 * \param[in]	path	replica path
 * \param[in]	uuid	database UUID
 */
int
rdb_destroy(const char *path, const uuid_t uuid)
{
	int rc;

	rc = vos_pool_destroy(path, (unsigned char *)uuid);
	if (rc != 0)
		D_ERROR(DF_UUID": failed to destroy %s: "DF_RC"\n",
			DP_UUID(uuid), path, DP_RC(rc));
	return rc;
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
 * \param[in]	uuid	database UUID
 * \param[in]	cbs	callbacks (not copied)
 * \param[in]	arg	argument for cbs
 * \param[out]	dbp	database
 */
int
rdb_start(const char *path, const uuid_t uuid, struct rdb_cbs *cbs, void *arg,
	  struct rdb **dbp)
{
	struct rdb	       *db;
	d_iov_t			value;
	uuid_t			uuid_persist;
	int			rc;
	struct vos_pool_space	vps;
	uint64_t		rdb_extra_sys[DAOS_MEDIA_MAX];

	D_ASSERT(cbs->dc_stop != NULL);
	D_DEBUG(DB_MD, DF_UUID": starting db %s\n", DP_UUID(uuid), path);

	D_ALLOC_PTR(db);
	if (db == NULL) {
		D_ERROR(DF_UUID": failed to allocate db object\n",
			DP_UUID(uuid));
		rc = -DER_NOMEM;
		goto err;
	}

	uuid_copy(db->d_uuid, uuid);
	db->d_ref = 1;
	db->d_cbs = cbs;
	db->d_arg = arg;

	rc = ABT_mutex_create(&db->d_mutex);
	if (rc != ABT_SUCCESS) {
		D_ERROR(DF_DB": failed to create mutex: %d\n", DP_DB(db), rc);
		rc = dss_abterr2der(rc);
		goto err_db;
	}

	rc = ABT_mutex_create(&db->d_raft_mutex);
	if (rc != ABT_SUCCESS) {
		D_ERROR(DF_DB": failed to create raft mutex: %d\n",
			DP_DB(db), rc);
		rc = dss_abterr2der(rc);
		goto err_mutex;
	}

	rc = ABT_cond_create(&db->d_ref_cv);
	if (rc != ABT_SUCCESS) {
		D_ERROR(DF_DB": failed to create ref CV: %d\n", DP_DB(db), rc);
		rc = dss_abterr2der(rc);
		goto err_raft_mutex;
	}

	rc = rdb_kvs_cache_create(&db->d_kvss);
	if (rc != 0)
		goto err_ref_cv;

	/* RDB pools specify small=true for basic system memory reservation */
	rc = vos_pool_open(path, (unsigned char *)uuid, true, &db->d_pool);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to open %s: "DF_RC"\n", DP_DB(db), path,
			DP_RC(rc));
		goto err_kvss;
	}

	/* metadata vos pool management: reserved memory:
	 * vos sets aside a portion of a pool for system activity:
	 *   fragmentation overhead (e.g., 5%), aggregation, GC
	 * rdb here set aside additional memory, > 50% of remaining usable
	 * for INSTALLSNAPSHOT / staging log container.
	 */
	rc = vos_pool_query_space(db->d_uuid, &vps);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to query vos pool space: "DF_RC"\n",
			DP_DB(db), DP_RC(rc));
		goto err_pool;
	}
	rdb_extra_sys[DAOS_MEDIA_SCM] = 0;
	rdb_extra_sys[DAOS_MEDIA_NVME] = 0;
	if (SCM_FREE(&vps) > SCM_SYS(&vps)) {
		rdb_extra_sys[DAOS_MEDIA_SCM] =
			     ((SCM_FREE(&vps) - SCM_SYS(&vps)) * 52) / 100;
		rc = vos_pool_space_sys_set(db->d_pool, &rdb_extra_sys[0]);
		if (rc != 0) {
			D_ERROR(DF_DB": failed to reserve more vos pool SCM "
				DF_U64" : "DF_RC"\n", DP_DB(db),
				rdb_extra_sys[DAOS_MEDIA_SCM], DP_RC(rc));
			goto err_pool;
		}
	} else {
		D_WARN(DF_DB": vos pool SCM not reserved for SLC: "
		       "free="DF_U64 "sys="DF_U64"\n", DP_DB(db),
		       SCM_FREE(&vps), SCM_SYS(&vps));
	}
	D_DEBUG(DB_MD, DF_DB": vos pool SCM: tot: "DF_U64" free: "DF_U64
		" vos-rsvd: "DF_U64" rdb-rsvd-slc: "DF_U64"\n", DP_DB(db),
		SCM_TOTAL(&vps), SCM_FREE(&vps), SCM_SYS(&vps),
		rdb_extra_sys[DAOS_MEDIA_SCM]);

	rc = vos_cont_open(db->d_pool, (unsigned char *)uuid, &db->d_mc);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to open metadata container: "DF_RC"\n",
			DP_DB(db), DP_RC(rc));
		goto err_pool;
	}

	/* Check if this replica is fully initialized. See rdb_create(). */
	d_iov_set(&value, uuid_persist, sizeof(uuid_t));
	rc = rdb_mc_lookup(db->d_mc, RDB_MC_ATTRS, &rdb_mc_uuid, &value);
	if (rc == -DER_NONEXIST) {
		D_ERROR(DF_DB": not fully initialized\n", DP_DB(db));
		goto err_mc;
	} else if (rc != 0) {
		D_ERROR(DF_DB": failed to look up UUID: "DF_RC"\n", DP_DB(db),
			DP_RC(rc));
		goto err_mc;
	}

	rc = rdb_raft_start(db);
	if (rc != 0)
		goto err_mc;

	ABT_mutex_lock(rdb_hash_lock);
	rc = d_hash_rec_insert(&rdb_hash, db->d_uuid, sizeof(uuid_t),
			       &db->d_entry, true /* exclusive */);
	ABT_mutex_unlock(rdb_hash_lock);
	if (rc != 0) {
		/* We have the PMDK pool open. */
		D_ASSERT(rc != -DER_EXIST);
		goto err_raft;
	}

	*dbp = db;
	D_DEBUG(DB_MD, DF_DB": started db %s %p with %u replicas\n", DP_DB(db),
		path, db, db->d_replicas == NULL ? 0 : db->d_replicas->rl_nr);
	return 0;

err_raft:
	rdb_raft_stop(db);
err_mc:
	vos_cont_close(db->d_mc);
err_pool:
	vos_pool_close(db->d_pool);
err_kvss:
	rdb_kvs_cache_destroy(db->d_kvss);
err_ref_cv:
	ABT_cond_free(&db->d_ref_cv);
err_raft_mutex:
	ABT_mutex_free(&db->d_raft_mutex);
err_mutex:
	ABT_mutex_free(&db->d_mutex);
err_db:
	D_FREE(db);
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

	D_DEBUG(DB_MD, DF_DB": stopping db %p\n", DP_DB(db), db);
	ABT_mutex_lock(rdb_hash_lock);
	deleted = d_hash_rec_delete(&rdb_hash, db->d_uuid, sizeof(uuid_t));
	ABT_mutex_unlock(rdb_hash_lock);
	D_ASSERT(deleted);
	rdb_raft_stop(db);
	vos_cont_close(db->d_mc);
	vos_pool_close(db->d_pool);
	rdb_kvs_cache_destroy(db->d_kvss);
	ABT_cond_free(&db->d_ref_cv);
	ABT_mutex_free(&db->d_raft_mutex);
	ABT_mutex_free(&db->d_mutex);
	D_FREE(db);
}

int
rdb_add_replicas(struct rdb *db, d_rank_list_t *replicas)
{
	int i;
	int rc = -DER_INVAL;

	D_DEBUG(DB_MD, DF_DB": Adding %d replicas\n",
		DP_DB(db), replicas->rl_nr);
	for (i = 0; i < replicas->rl_nr; ++i) {
		rc = rdb_raft_add_replica(db, replicas->rl_ranks[i]);
		if (rc != 0)
			break;
	}

	/* Update list to only contain ranks which could not be added. */
	replicas->rl_nr -= i;
	if (replicas->rl_nr > 0 && i > 0)
		memmove(&replicas->rl_ranks[0], &replicas->rl_ranks[i],
			replicas->rl_nr * sizeof(d_rank_t));
	return rc;
}

int
rdb_remove_replicas(struct rdb *db, d_rank_list_t *replicas)
{
	int i;
	int rc = -DER_INVAL;

	D_DEBUG(DB_MD, DF_DB": Removing %d replicas\n",
		DP_DB(db), replicas->rl_nr);
	for (i = 0; i < replicas->rl_nr; ++i) {
		rc = rdb_raft_remove_replica(db, replicas->rl_ranks[i]);
		if (rc != 0)
			break;
	}

	/* Update list to only contain ranks which could not be removed. */
	replicas->rl_nr -= i;
	if (replicas->rl_nr > 0 && i > 0)
		memmove(&replicas->rl_ranks[0], &replicas->rl_ranks[i],
			replicas->rl_nr * sizeof(d_rank_t));
	return rc;
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
 * Call for a new election (campaign to become leader).
 */
int
rdb_campaign(struct rdb *db)
{
	return rdb_raft_campaign(db);
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
	int is_leader;

	ABT_mutex_lock(db->d_raft_mutex);
	*term = raft_get_current_term(db->d_raft);
	is_leader = raft_is_leader(db->d_raft);
	ABT_mutex_unlock(db->d_raft_mutex);

	return is_leader;
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

	ABT_mutex_lock(db->d_raft_mutex);
	node = raft_get_current_leader_node(db->d_raft);
	if (node == NULL) {
		ABT_mutex_unlock(db->d_raft_mutex);
		return -DER_NONEXIST;
	}
	dnode = raft_node_get_udata(node);
	D_ASSERT(dnode != NULL);
	*term = raft_get_current_term(db->d_raft);
	*rank = dnode->dn_rank;
	ABT_mutex_unlock(db->d_raft_mutex);

	return 0;
}

/**
 * Get the list of replica ranks. Callers are responsible for
 * d_rank_list_free(*ranksp).
 *
 * \param[in]	db	database
 * \param[out]	ranksp	list of replica ranks
 */
int
rdb_get_ranks(struct rdb *db, d_rank_list_t **ranksp)
{
	return daos_rank_list_dup(ranksp, db->d_replicas);
}

/**
 * Get the UUID of the database.
 *
 * \param[in]	db	database
 * \param[out]	uuid	UUID
 */

void rdb_get_uuid(struct rdb *db, uuid_t uuid)
{
	uuid_copy(uuid, db->d_uuid);
}
