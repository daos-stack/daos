/**
 * (C) Copyright 2017-2023 Intel Corporation.
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

static int rdb_open_internal(daos_handle_t pool, daos_handle_t mc, const uuid_t uuid,
			     uint64_t caller_term, struct rdb_cbs *cbs, void *arg,
			     struct rdb **dbp);
static int
rdb_chkptd_start(struct rdb *db);
static void
rdb_chkptd_stop(struct rdb *db);

/**
 * Create an RDB replica at \a path with \a uuid, \a size, and \a replicas, and
 * open it with \a cbs and \a arg.
 *
 * \param[in]	path		replica path
 * \param[in]	uuid		database UUID
 * \param[in]	caller_term	caller term if not RDB_NIL_TERM (see rdb_open)
 * \param[in]	size		replica size in bytes
 * \param[in]	replicas	list of replica ranks
 * \param[in]	cbs		callbacks (not copied)
 * \param[in]	arg		argument for cbs
 * \param[out]	storagep	database storage
 */
int
rdb_create(const char *path, const uuid_t uuid, uint64_t caller_term, size_t size,
	   const d_rank_list_t *replicas, struct rdb_cbs *cbs, void *arg,
	   struct rdb_storage **storagep)
{
	daos_handle_t	pool;
	daos_handle_t	mc;
	d_iov_t		value;
	uint32_t	version = RDB_LAYOUT_VERSION;
	struct rdb     *db;
	int		rc;

	D_DEBUG(DB_MD, DF_UUID": creating db %s with %u replicas: caller_term="DF_X64"\n",
		DP_UUID(uuid), path, replicas == NULL ? 0 : replicas->rl_nr, caller_term);

	/*
	 * Create and open a VOS pool. RDB pools specify VOS_POF_SMALL for
	 * basic system memory reservation and VOS_POF_EXCL for concurrent
	 * access protection.
	 */
	rc = vos_pool_create(path, (unsigned char *)uuid, size, 0 /* nvme_sz */,
			     VOS_POF_SMALL | VOS_POF_EXCL | VOS_POF_RDB, &pool);
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

	rc = rdb_open_internal(pool, mc, uuid, caller_term, cbs, arg, &db);
	if (rc != 0)
		goto out_mc_hdl;

	db->d_new = true;

	*storagep = rdb_to_storage(db);
out_mc_hdl:
	if (rc != 0)
		vos_cont_close(mc);
out_pool_hdl:
	if (rc != 0) {
		int rc_tmp;

		vos_pool_close(pool);
		rc_tmp = vos_pool_destroy_ex(path, (unsigned char *)uuid, VOS_POF_RDB);
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

	rc = vos_pool_destroy_ex(path, (unsigned char *)uuid, VOS_POF_RDB);
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
rdb_open_internal(daos_handle_t pool, daos_handle_t mc, const uuid_t uuid, uint64_t caller_term,
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

	rc = rdb_chkptd_start(db);
	if (rc != 0)
		goto err_ref_cv;

	rc = rdb_kvs_cache_create(&db->d_kvss);
	if (rc != 0)
		goto err_chkptd;

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

	db->d_nospc_ts = daos_getutime();

	rc = rdb_raft_open(db, caller_term);
	if (rc != 0)
		goto err_kvss;

	*dbp = db;
	return 0;

err_kvss:
	rdb_kvs_cache_destroy(db->d_kvss);
err_chkptd:
	rdb_chkptd_stop(db);
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
 * Open an RDB replica at \a path.
 *
 * If \a caller_term is not RDB_NIL_TERM, it shall be the term of the leader
 * (of the same RDB) who is calling this function (usually via an RPC). This is
 * used to perform the Raft term check/update so that an older leader doesn't
 * interrupt with a newer leader.
 *
 * \param[in]	path		replica path
 * \param[in]	uuid		database UUID
 * \param[in]	caller_term	caller term if not RDB_NIL_TERM
 * \param[in]	cbs		callbacks (not copied)
 * \param[in]	arg		argument for cbs
 * \param[out]	storagep	database storage
 *
 * \retval	-DER_STALE	\a caller_term < the current term
 */
int
rdb_open(const char *path, const uuid_t uuid, uint64_t caller_term, struct rdb_cbs *cbs, void *arg,
	 struct rdb_storage **storagep)
{
	daos_handle_t	pool;
	daos_handle_t	mc;
	d_iov_t		value;
	uuid_t		uuid_persist;
	uint32_t	version;
	struct rdb     *db;
	int		rc;

	D_DEBUG(DB_MD, DF_UUID": opening db %s: caller_term="DF_X64"\n", DP_UUID(uuid), path,
		caller_term);

	/*
	 * RDB pools specify VOS_POF_SMALL for basic system memory reservation
	 * and VOS_POF_EXCL for concurrent access protection.
	 */
	rc = vos_pool_open(path, (unsigned char *)uuid,
			   VOS_POF_SMALL | VOS_POF_EXCL | VOS_POF_RDB, &pool);
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

	rc = rdb_open_internal(pool, mc, uuid, caller_term, cbs, arg, &db);
	if (rc != 0)
		goto err_mc;

	D_DEBUG(DB_MD, DF_DB": opened db %s %p\n", DP_DB(db), path, db);
	*storagep = rdb_to_storage(db);
	return 0;

err_mc:
	vos_cont_close(mc);
err_pool:
	vos_pool_close(pool);
err:
	return rc;
}

/**
 * Close \a storage.
 *
 * \param[in]	storage	database storage
 */
void
rdb_close(struct rdb_storage *storage)
{
	struct rdb *db = rdb_from_storage(storage);

	D_ASSERTF(db->d_ref == 1, "d_ref %d == 1\n", db->d_ref);
	rdb_raft_close(db);
	rdb_chkptd_stop(db);
	vos_cont_close(db->d_mc);
	vos_pool_close(db->d_pool);
	rdb_kvs_cache_destroy(db->d_kvss);
	ABT_cond_free(&db->d_ref_cv);
	ABT_mutex_free(&db->d_raft_mutex);
	ABT_mutex_free(&db->d_mutex);
	D_DEBUG(DB_MD, DF_DB": closed db %p\n", DP_DB(db), db);
	D_FREE(db);
}

static bool
rdb_get_use_leases(void)
{
	char   *name = "RDB_USE_LEASES";
	bool	value = true;

	d_getenv_bool(name, &value);
	return value;
}

/**
 * Start \a storage, converting \a storage into \a dbp. If this is successful,
 * the caller must stop using \a storage; otherwise, the caller remains
 * responsible for closing \a storage.
 *
 * \param[in]	storage	database storage
 * \param[out]	dbp	database
 */
int
rdb_start(struct rdb_storage *storage, struct rdb **dbp)
{
	struct rdb     *db = rdb_from_storage(storage);
	int		rc;

	rc = rdb_raft_start(db);
	if (rc != 0)
		return rc;

	ABT_mutex_lock(rdb_hash_lock);
	rc = d_hash_rec_insert(&rdb_hash, db->d_uuid, sizeof(uuid_t), &db->d_entry,
			       true /* exclusive */);
	ABT_mutex_unlock(rdb_hash_lock);
	if (rc != 0) {
		/* We have the PMDK pool open. */
		D_ASSERT(rc != -DER_EXIST);
		rdb_raft_stop(db);
		return rc;
	}

	db->d_use_leases = rdb_get_use_leases();

	D_DEBUG(DB_MD, DF_DB": started db %p: use_leases=%d\n", DP_DB(db), db, db->d_use_leases);
	*dbp = db;
	return 0;
}

/**
 * Stop \a db, converting \a db into \a storagep. All TXs in \a db must be
 * either ended already or blocking only in rdb.
 *
 * \param[in]	db		database
 * \param[out]	storagep	database storage
 */
void
rdb_stop(struct rdb *db, struct rdb_storage **storagep)
{
	bool deleted;

	D_DEBUG(DB_MD, DF_DB": stopping db %p\n", DP_DB(db), db);

	ABT_mutex_lock(rdb_hash_lock);
	deleted = d_hash_rec_delete(&rdb_hash, db->d_uuid, sizeof(uuid_t));
	ABT_mutex_unlock(rdb_hash_lock);
	D_ASSERT(deleted);

	rdb_raft_stop(db);

	D_DEBUG(DB_MD, DF_DB": stopped db %p\n", DP_DB(db), db);
	*storagep = rdb_to_storage(db);
}

/**
 * Stop and close \a db. All TXs in \a db must be either ended already or
 * blocking only in rdb.
 *
 * \param[in]	db	database
 */
void
rdb_stop_and_close(struct rdb *db)
{
	struct rdb_storage *storage;

	rdb_stop(db, &storage);
	rdb_close(storage);
}

/**
 * Add \a replicas.
 *
 * \param[in]	db		database
 * \param[in,out]
 *		replicas	[in] list of replica ranks;
 *				[out] list of replica ranks that could not be added
 */
int
rdb_add_replicas(struct rdb *db, d_rank_list_t *replicas)
{
	int	i;
	int	rc;

	D_DEBUG(DB_MD, DF_DB": Adding %d replicas\n",
		DP_DB(db), replicas->rl_nr);

	ABT_mutex_lock(db->d_raft_mutex);

	rc = rdb_raft_wait_applied(db, db->d_debut, raft_get_current_term(db->d_raft));
	if (rc != 0) {
		ABT_mutex_unlock(db->d_raft_mutex);
		return rc;
	}

	rc = -DER_INVAL;
	for (i = 0; i < replicas->rl_nr; ++i) {
		rc = rdb_raft_add_replica(db, replicas->rl_ranks[i]);
		if (rc != 0) {
			D_ERROR(DF_DB": failed to add rank %u: "DF_RC"\n", DP_DB(db),
				replicas->rl_ranks[i], DP_RC(rc));
			break;
		}
	}

	ABT_mutex_unlock(db->d_raft_mutex);

	/* Update list to only contain ranks which could not be added. */
	replicas->rl_nr -= i;
	if (replicas->rl_nr > 0 && i > 0)
		memmove(&replicas->rl_ranks[0], &replicas->rl_ranks[i],
			replicas->rl_nr * sizeof(d_rank_t));
	return rc;
}

/**
 * Remove \a replicas.
 *
 * \param[in]	db		database
 * \param[in,out]
 *		replicas	[in] list of replica ranks;
 *				[out] list of replica ranks that could not be removed
 */
int
rdb_remove_replicas(struct rdb *db, d_rank_list_t *replicas)
{
	int	i;
	int	rc;

	D_DEBUG(DB_MD, DF_DB": Removing %d replicas\n",
		DP_DB(db), replicas->rl_nr);

	ABT_mutex_lock(db->d_raft_mutex);

	rc = rdb_raft_wait_applied(db, db->d_debut, raft_get_current_term(db->d_raft));
	if (rc != 0) {
		ABT_mutex_unlock(db->d_raft_mutex);
		return rc;
	}

	rc = -DER_INVAL;
	for (i = 0; i < replicas->rl_nr; ++i) {
		rc = rdb_raft_remove_replica(db, replicas->rl_ranks[i]);
		if (rc != 0) {
			D_ERROR(DF_DB": failed to remove rank %u: "DF_RC"\n", DP_DB(db),
				replicas->rl_ranks[i], DP_RC(rc));
			break;
		}
	}

	ABT_mutex_unlock(db->d_raft_mutex);

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
 * Call a new election (campaign to become leader). Must be a voting replica.
 *
 * \param[in]	db	database
 *
 * \retval -DER_INVAL	not a voting replica
 */
int
rdb_campaign(struct rdb *db)
{
	return rdb_raft_campaign(db);
}

/**
 * Simulate a ping (i.e., an empty AE) from the leader of \a caller_term to \a
 * db. This essentially checks if \a caller_term is stale, and if not, update
 * the current term. See also rdb_open.
 *
 * \param[in]	db		database
 * \param[in]	caller_term	caller term
 *
 * \retval -DER_STALE	\a caller_term < the current term
 */
int
rdb_ping(struct rdb *db, uint64_t caller_term)
{
	return rdb_raft_ping(db, caller_term);
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
	return rdb_raft_get_ranks(db, ranksp);
}

/** Implementation of the RDB pool checkpoint ULT. The ULT
 *  is only active if DAOS is using MD on SSD.
 */
static void
rdb_chkpt_wait(void *arg, uint64_t wait_id, uint64_t *commit_id)
{
	struct rdb              *db    = arg;
	struct rdb_chkpt_record *dcr   = &db->d_chkpt_record;
	struct umem_store       *store = dcr->dcr_store;

	D_DEBUG(DB_MD, DF_DB ": commit >= " DF_X64 "\n", DP_DB(db), wait_id);

	if (wait_id == 0) {
		/** Special case, checkpoint needs to yield to allow progress */
		ABT_thread_yield();
		return;
	}

	if (store->stor_ops->so_wal_id_cmp(store, dcr->dcr_commit_id, wait_id) >= 0)
		goto out;

	D_DEBUG(DB_MD, DF_DB ": wait for commit >= " DF_X64 "\n", DP_DB(db), wait_id);
	ABT_mutex_lock(db->d_chkpt_mutex);
	dcr->dcr_waiting = 1;
	dcr->dcr_wait_id = wait_id;
	ABT_cond_wait(db->d_commit_cv, db->d_chkpt_mutex);
	ABT_mutex_unlock(db->d_chkpt_mutex);
out:
	D_DEBUG(DB_MD, DF_DB ": commit " DF_X64 " is >= " DF_X64 "\n", DP_DB(db),
		dcr->dcr_commit_id, wait_id);
	*commit_id = dcr->dcr_commit_id;
}

static void
rdb_chkpt_update(void *arg, uint64_t commit_id, uint32_t used_blocks, uint32_t total_blocks)
{
	struct rdb              *db    = arg;
	struct rdb_chkpt_record *dcr   = &db->d_chkpt_record;
	struct umem_store       *store = dcr->dcr_store;

	if (!dcr->dcr_init) {
		/** Set threshold to 50% of blocks */
		dcr->dcr_thresh = total_blocks >> 1;
	}

	if (commit_id == dcr->dcr_commit_id)
		return; /** reserve path can call this with duplicate commit_id */

	dcr->dcr_commit_id = commit_id;

	if (dcr->dcr_idle) {
		if (used_blocks >= dcr->dcr_thresh) {
			dcr->dcr_needed = 1;
			D_DEBUG(DB_MD,
				DF_DB ": used %u/%u exceeds threshold %u, triggering checkpoint\n",
				DP_DB(db), used_blocks, total_blocks, dcr->dcr_thresh);
			ABT_cond_broadcast(db->d_chkpt_cv);
		}
		D_DEBUG(DB_MD, DF_DB ": update commit = " DF_X64 ", chkpt is idle\n", DP_DB(db),
			commit_id);
		return;
	}

	if (!dcr->dcr_waiting) {
		D_DEBUG(DB_MD, DF_DB ": update commit = " DF_X64 ", chkpt is not waiting\n",
			DP_DB(db), commit_id);
		return;
	}

	/** Checkpoint ULT is waiting for a commit, check if we can wake it up */
	if (store->stor_ops->so_wal_id_cmp(store, commit_id, dcr->dcr_wait_id) >= 0) {
		dcr->dcr_waiting = 0;
		D_DEBUG(DB_MD,
			DF_DB ": update commit = " DF_X64 ", waking checkpoint waiting for " DF_X64
			      "\n",
			DP_DB(db), commit_id, dcr->dcr_wait_id);
		ABT_cond_broadcast(db->d_commit_cv);
	} else {
		D_DEBUG(DB_MD, DF_DB ": update commit = " DF_X64 "\n", DP_DB(db), commit_id);
	}
}

static bool
rdb_chkpt_enabled(struct rdb *db)
{
	struct rdb_chkpt_record *dcr = &db->d_chkpt_record;

	if (dcr->dcr_init)
		return dcr->dcr_enabled == 1;

	if (!vos_pool_needs_checkpoint(db->d_pool)) {
		D_DEBUG(DB_MD, DF_DB ": checkpointing is disabled for rdb replica\n", DP_DB(db));
		dcr->dcr_init    = 1;
		dcr->dcr_enabled = 0;
		return false;
	}

	D_DEBUG(DB_MD, DF_DB ": checkpointing is enabled for rdb replica\n", DP_DB(db));
	vos_pool_checkpoint_init(db->d_pool, rdb_chkpt_update, rdb_chkpt_wait, db, &dcr->dcr_store);

	dcr->dcr_enabled = 1;
	dcr->dcr_init    = 1;

	return true;
}

static void
rdb_chkpt_fini(struct rdb *db)
{
	vos_pool_checkpoint_fini(db->d_pool);
}

/* Daemon ULT for checkpointing to metadata blob (MD on SSD only) */
static void
rdb_chkptd(void *arg)
{
	struct timespec          last;
	struct timespec          deadline;
	struct rdb *db = arg;
	struct rdb_chkpt_record *dcr = &db->d_chkpt_record;

	D_DEBUG(DB_MD, DF_DB ": checkpointd starting\n", DP_DB(db));
	/** ABT_cond_timedwait uses CLOCK_REALTIME internally so we have to use it but using
	 *  COARSE version should be fine.  CLOCK_MONOTONIC might be completely different
	 *  because it's not affected by system changes.
	 */
	clock_gettime(CLOCK_REALTIME_COARSE, &last);
	for (;;) {
		int  rc;

		ABT_mutex_lock(db->d_chkpt_mutex);
		for (;;) {
			if (db->d_chkpt_record.dcr_needed)
				break;
			clock_gettime(CLOCK_REALTIME_COARSE, &deadline);
			if (deadline.tv_sec >= last.tv_sec + 10)
				break;
			if (dcr->dcr_stop)
				break;
			deadline.tv_sec += 10;
			dcr->dcr_idle = 1;
			ABT_cond_timedwait(db->d_chkpt_cv, db->d_chkpt_mutex, &deadline);
		}
		ABT_mutex_unlock(db->d_chkpt_mutex);
		if (dcr->dcr_stop)
			break;
		dcr->dcr_idle = 0;
		rc            = vos_pool_checkpoint(db->d_pool);
		if (rc != 0) {
			D_ERROR(DF_DB ": failed to checkpoint: rc=" DF_RC "\n", DP_DB(db),
				DP_RC(rc));
			break;
		}
		db->d_chkpt_record.dcr_needed = 0;
		clock_gettime(CLOCK_REALTIME_COARSE, &last);
	}
	D_DEBUG(DB_MD, DF_DB ": checkpointd stopping\n", DP_DB(db));
	rdb_chkpt_fini(db);
}

static void
rdb_chkptd_stop(struct rdb *db)
{
	struct rdb_chkpt_record *dcr = &db->d_chkpt_record;
	int                      rc;

	switch (dcr->dcr_state) {
	default:
		D_ASSERTF(0, "Invalid state %d\n", dcr->dcr_state);
	case CHKPT_NONE:
		return;
	case CHKPT_ULT:
		D_DEBUG(DB_MD, DF_DB ": Stopping chkptd ULT\n", DP_DB(db));
		dcr->dcr_stop = 1;
		ABT_cond_broadcast(db->d_chkpt_cv);
		rc = ABT_thread_free(&db->d_chkptd);
		D_ASSERTF(rc == 0, "free rdb_chkptd: rc=%d\n", rc);
		D_DEBUG(DB_MD, DF_DB ": Stopped chkptd ULT\n", DP_DB(db));
		/** Fall through */
	case CHKPT_COMMIT_CV:
		ABT_cond_free(&db->d_commit_cv);
		/** Fall through */
	case CHKPT_MAIN_CV:
		ABT_cond_free(&db->d_chkpt_cv);
		/** Fall through */
	case CHKPT_MUTEX:
		ABT_mutex_free(&db->d_chkpt_mutex);
		/** Fall through */
	}

	dcr->dcr_state = CHKPT_NONE;
}

static int
rdb_chkptd_start(struct rdb *db)
{
	struct rdb_chkpt_record *dcr = &db->d_chkpt_record;
	int                      rc;

	if (!rdb_chkpt_enabled(db))
		return 0;

	rc = ABT_mutex_create(&db->d_chkpt_mutex);
	if (rc != ABT_SUCCESS) {
		D_ERROR(DF_DB ": failed to create checkpoint mutex: %d\n", DP_DB(db), rc);
		D_GOTO(error, rc = dss_abterr2der(rc));
	}
	dcr->dcr_state = CHKPT_MUTEX;

	rc = ABT_cond_create(&db->d_chkpt_cv);
	if (rc != ABT_SUCCESS) {
		D_ERROR(DF_DB ": failed to create checkpoint main CV: %d\n", DP_DB(db), rc);
		D_GOTO(error, rc = dss_abterr2der(rc));
	}
	dcr->dcr_state = CHKPT_MAIN_CV;

	rc = ABT_cond_create(&db->d_commit_cv);
	if (rc != ABT_SUCCESS) {
		D_ERROR(DF_DB ": failed to create checkpoint commit CV: %d\n", DP_DB(db), rc);
		D_GOTO(error, rc = dss_abterr2der(rc));
	}
	dcr->dcr_state = CHKPT_COMMIT_CV;

	rc = dss_ult_create(rdb_chkptd, db, DSS_XS_SELF, 0, DSS_DEEP_STACK_SZ, &db->d_chkptd);
	if (rc != 0) {
		D_ERROR(DF_DB ": failed to start chkptd ULT: " DF_RC "\n", DP_DB(db), DP_RC(rc));
		goto error;
	}
	dcr->dcr_state = CHKPT_ULT;

	return 0;
error:
	rdb_chkptd_stop(db);
	return rc;
}
