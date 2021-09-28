/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * rdb: Databases
 */

#define D_LOGFAC	DD_FAC(rdb)

#include <daos_srv/rdb.h>

#include <daos_srv/daos_mgmt_srv.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/vos.h>
#include "rdb_internal.h"
#include "rdb_layout.h"

static int rdb_start_internal(daos_handle_t pool, daos_handle_t mc,
			      const uuid_t uuid, struct rdb_cbs *cbs, void *arg,
			      struct rdb **dbp);

/**
 * Create an RDB replica at \a path with \a uuid, \a size, and \a replicas, and
 * start it with \a cbs and \a arg.
 *
 * \param[in]	path		replica path
 * \param[in]	uuid		database UUID
 * \param[in]	size		replica size in bytes
 * \param[in]	replicas	list of replica ranks
 * \param[in]	cbs		callbacks (not copied)
 * \param[in]	arg		argument for cbs
 * \param[out]	dbp		database
 */
int
rdb_create(const char *path, const uuid_t uuid, size_t size,
	   const d_rank_list_t *replicas, struct rdb_cbs *cbs, void *arg,
	   struct rdb **dbp)
{
	daos_handle_t	pool;
	daos_handle_t	mc;
	d_iov_t		value;
	uint32_t	version = RDB_LAYOUT_VERSION;
	int		rc;

	D_DEBUG(DB_MD, DF_UUID": creating db %s with %u replicas\n",
		DP_UUID(uuid), path, replicas == NULL ? 0 : replicas->rl_nr);

	/*
	 * Create and open a VOS pool. RDB pools specify VOS_POF_SMALL for
	 * basic system memory reservation and VOS_POF_EXCL for concurrent
	 * access protection.
	 */
	rc = vos_pool_create(path, (unsigned char *)uuid, size, 0 /* nvme_sz */,
			     VOS_POF_SMALL | VOS_POF_EXCL, &pool);
	if (rc != 0)
		goto out;
	ABT_thread_yield();

	/* Create and open the metadata container. */
	rc = vos_cont_create(pool, (unsigned char *)uuid);
	if (rc != 0)
		goto out_pool_hdl;
	rc = vos_cont_open(pool, (unsigned char *)uuid, &mc);
	if (rc != 0)
		goto out_pool_hdl;

	/* Initialize the layout version. */
	d_iov_set(&value, &version, sizeof(version));
	rc = rdb_mc_update(mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_version,
			   &value);
	if (rc != 0)
		goto out_mc_hdl;

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
	if (rc != 0)
		goto out_mc_hdl;

	rc = rdb_start_internal(pool, mc, uuid, cbs, arg, dbp);

out_mc_hdl:
	if (rc != 0)
		vos_cont_close(mc);
out_pool_hdl:
	if (rc != 0) {
		int rc_tmp;

		vos_pool_close(pool);
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

/*
 * If created successfully, the new DB handle will consume pool and mc, which
 * the caller shall not close in this case.
 */
static int
rdb_start_internal(daos_handle_t pool, daos_handle_t mc, const uuid_t uuid,
		   struct rdb_cbs *cbs, void *arg, struct rdb **dbp)
{
	struct rdb	       *db;
	int			rc;
	struct vos_pool_space	vps;
	uint64_t		rdb_extra_sys[DAOS_MEDIA_MAX];

	D_ASSERT(cbs->dc_stop != NULL);

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
	db->d_pool = pool;
	db->d_mc = mc;

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
		goto err_kvss;
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
			goto err_kvss;
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

	rc = rdb_raft_start(db);
	if (rc != 0)
		goto err_kvss;

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
	return 0;

err_raft:
	rdb_raft_stop(db);
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
	daos_handle_t		pool;
	daos_handle_t		mc;
	d_iov_t			value;
	uuid_t			uuid_persist;
	uint32_t		version;
	int			rc;

	D_INFO(DF_UUID": starting RDB %s\n", DP_UUID(uuid), path);

	/*
	 * RDB pools specify VOS_POF_SMALL for basic system memory reservation
	 * and VOS_POF_EXCL for concurrent access protection.
	 */
	rc = vos_pool_open(path, (unsigned char *)uuid,
			   VOS_POF_SMALL | VOS_POF_EXCL, &pool);
	if (rc == -DER_ID_MISMATCH) {
		ds_notify_ras_eventf(RAS_RDB_DF_INCOMPAT, RAS_TYPE_INFO, RAS_SEV_ERROR,
				     NULL /* hwid */, NULL /* rank */, NULL /* inc */,
				     NULL /* jobid */, NULL /* pool */, NULL /* cont */,
				     NULL /* objid */, NULL /* ctlop */, NULL /* data */,
				     "%s: incompatible DB UUID: "DF_UUIDF"\n", path, DP_UUID(uuid));
		goto err;
	} else if (rc != 0) {
		D_ERROR(DF_UUID": failed to open %s: "DF_RC"\n", DP_UUID(uuid),
			path, DP_RC(rc));
		goto err;
	}
	ABT_thread_yield();

	rc = vos_cont_open(pool, (unsigned char *)uuid, &mc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to open metadata container: "DF_RC"\n",
			DP_UUID(uuid), DP_RC(rc));
		goto err_pool;
	}

	/* Check if this replica is fully initialized. See rdb_create(). */
	d_iov_set(&value, uuid_persist, sizeof(uuid_t));
	rc = rdb_mc_lookup(mc, RDB_MC_ATTRS, &rdb_mc_uuid, &value);
	if (rc == -DER_NONEXIST) {
		D_ERROR(DF_UUID": not fully initialized\n", DP_UUID(uuid));
		rc = -DER_DF_INVAL;
		goto err_mc;
	} else if (rc != 0) {
		D_ERROR(DF_UUID": failed to look up UUID: "DF_RC"\n",
			DP_UUID(uuid), DP_RC(rc));
		goto err_mc;
	}

	/* Check if the layout version is compatible. */
	d_iov_set(&value, &version, sizeof(version));
	rc = rdb_mc_lookup(mc, RDB_MC_ATTRS, &rdb_mc_version, &value);
	if (rc == -DER_NONEXIST) {
		ds_notify_ras_eventf(RAS_RDB_DF_INCOMPAT, RAS_TYPE_INFO, RAS_SEV_ERROR,
				     NULL /* hwid */, NULL /* rank */, NULL /* inc */,
				     NULL /* jobid */, NULL /* pool */, NULL /* cont */,
				     NULL /* objid */, NULL /* ctlop */, NULL /* data */,
				     DF_UUID": %s: incompatible layout version",
				     DP_UUID(uuid), path);
		rc = -DER_DF_INCOMPT;
		goto err_mc;
	} else if (rc != 0) {
		D_ERROR(DF_UUID": failed to look up layout version: "DF_RC"\n",
			DP_UUID(uuid), DP_RC(rc));
		goto err_mc;
	}
	if (version < RDB_LAYOUT_VERSION_LOW || version > RDB_LAYOUT_VERSION) {
		ds_notify_ras_eventf(RAS_RDB_DF_INCOMPAT, RAS_TYPE_INFO, RAS_SEV_ERROR,
				     NULL /* hwid */, NULL /* rank */, NULL /* inc */,
				     NULL /* jobid */, NULL /* pool */, NULL /* cont */,
				     NULL /* objid */, NULL /* ctlop */, NULL /* data */,
				     DF_UUID": %s: incompatible layout version: %u not in [%u, %u]",
				     DP_UUID(uuid), path, version, RDB_LAYOUT_VERSION_LOW,
				     RDB_LAYOUT_VERSION);
		rc = -DER_DF_INCOMPT;
		goto err_mc;
	}

	rc = rdb_start_internal(pool, mc, uuid, cbs, arg, dbp);
	if (rc != 0)
		goto err_mc;

	D_DEBUG(DB_MD, DF_DB": started db %s %p with %u replicas\n",
		DP_DB(*dbp), path, *dbp,
		(*dbp)->d_replicas == NULL ? 0 : (*dbp)->d_replicas->rl_nr);
	return 0;

err_mc:
	vos_cont_close(mc);
err_pool:
	vos_pool_close(pool);
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

	if (db == NULL) {
		D_ERROR("db is NULL\n");
		return;
	}

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
	D_DEBUG(DB_MD, DF_DB": stopped db %p\n", DP_DB(db), db);
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
