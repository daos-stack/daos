/**
 * (C) Copyright 2017-2023 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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

static int
rdb_open_internal(daos_handle_t pool, daos_handle_t mc, const uuid_t uuid, uint32_t layout_version,
		  uint64_t caller_term, struct rdb_cbs *cbs, void *arg, struct rdb **dbp);

/**
 * Create an RDB replica at \a path with \a uuid, \a caller_term, and \a params,
 * and open it with \a cbs and \a arg.
 *
 * \param[in]	path		replica path
 * \param[in]	uuid		database UUID
 * \param[in]	caller_term	caller term if not RDB_NIL_TERM (see rdb_open)
 * \param[in]	params		parameters for creating the replica
 * \param[in]	cbs		callbacks (not copied)
 * \param[in]	arg		argument for cbs
 * \param[out]	storagep	database storage
 */
int
rdb_create(const char *path, const uuid_t uuid, uint64_t caller_term,
	   struct rdb_create_params *params, struct rdb_cbs *cbs, void *arg,
	   struct rdb_storage **storagep)
{
	daos_handle_t	pool;
	daos_handle_t	mc;
	d_iov_t		value;
	uint32_t        version;
	struct rdb     *db;
	int		rc;

	D_DEBUG(DB_MD,
		DF_UUID ": creating db %s with %d replicas: caller_term=" DF_X64 " size=" DF_U64
			" vos_df_version=%u layout_version=%u self=" RDB_F_RID "\n",
		DP_UUID(uuid), path, params->rcp_replicas_len, caller_term, params->rcp_size,
		params->rcp_vos_df_version, params->rcp_layout_version, RDB_P_RID(params->rcp_id));

	/*
	 * Create and open a VOS pool. RDB pools specify VOS_POF_SMALL for
	 * basic system memory reservation and VOS_POF_EXCL for concurrent
	 * access protection.
	 */
	rc = dss_vos_pool_create(
	    path, (unsigned char *)uuid, params->rcp_size, 0 /* data_sz */, 0 /* meta_sz */,
	    VOS_POF_SMALL | VOS_POF_EXCL | VOS_POF_RDB | VOS_POF_EXTERNAL_CHKPT,
	    params->rcp_vos_df_version, &pool);
	if (rc != 0)
		goto out;

	/* Create and open the metadata container. */
	rc = vos_cont_create(pool, (unsigned char *)uuid);
	if (rc != 0)
		goto out_pool_hdl;
	rc = vos_cont_open(pool, (unsigned char *)uuid, &mc);
	if (rc != 0)
		goto out_pool_hdl;

	/* Initialize the layout version. */
	version = params->rcp_layout_version;
	if (version == 0)
		version = RDB_LAYOUT_VERSION;
	d_iov_set(&value, &version, sizeof(version));
	rc = rdb_mc_update(mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_version, &value, NULL /* vtx */);
	if (rc != 0)
		goto out_mc_hdl;

	/* Initialize the replica ID. */
	if (version >= RDB_LAYOUT_VERSION_REPLICA_ID) {
		d_iov_set(&value, &params->rcp_id, sizeof(params->rcp_id));
		rc = rdb_mc_update(mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_replica_id, &value,
				   NULL /* vtx */);
		if (rc != 0) {
			DL_ERROR(rc, DF_UUID ": failed to initialize replica ID", DP_UUID(uuid));
			goto out_mc_hdl;
		}
	}

	/* Initialize Raft. */
	rc = rdb_raft_init((unsigned char *)uuid, pool, mc, params->rcp_replicas,
			   params->rcp_replicas_len, version);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to initialize Raft", DP_UUID(uuid));
		goto out_mc_hdl;
	}

	/*
	 * Mark this replica as fully initialized by storing its UUID.
	 * rdb_start() checks this attribute when starting a DB.
	 */
	d_iov_set(&value, (void *)uuid, sizeof(uuid_t));
	rc = rdb_mc_update(mc, RDB_MC_ATTRS, 1 /* n */, &rdb_mc_uuid, &value, NULL /* vtx */);
	if (rc != 0)
		goto out_mc_hdl;

	rc = rdb_open_internal(pool, mc, uuid, version, caller_term, cbs, arg, &db);
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

	D_INFO(DF_UUID ": destroying db %s\n", DP_UUID(uuid), path);
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

static int rdb_chkptd_start(struct rdb *db);
static void rdb_chkptd_stop(struct rdb *db);

/*
 * If created successfully, the new DB handle will consume pool and mc, which
 * the caller shall not close in this case.
 */
static int
rdb_open_internal(daos_handle_t pool, daos_handle_t mc, const uuid_t uuid, uint32_t layout_version,
		  uint64_t caller_term, struct rdb_cbs *cbs, void *arg, struct rdb **dbp)
{
	struct rdb	       *db;
	int			rc;
	d_iov_t                 value;
	struct vos_pool_space	vps;
	uint64_t		rdb_extra_sys[DAOS_MEDIA_MAX];

	D_ASSERT(cbs == NULL || cbs->dc_stop != NULL);

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
	db->d_version = layout_version;
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

	rc = ABT_rwlock_create(&db->d_gen_lock);
	if (rc != ABT_SUCCESS) {
		D_ERROR(DF_DB ": failed to create gen rwlock: %d\n", DP_DB(db), rc);
		rc = dss_abterr2der(rc);
		goto err_ref_cv;
	}

	if (db->d_version >= RDB_LAYOUT_VERSION_REPLICA_ID) {
		d_iov_set(&value, &db->d_replica_id, sizeof(db->d_replica_id));
		rc = rdb_mc_lookup(mc, RDB_MC_ATTRS, &rdb_mc_replica_id, &value);
		if (rc != 0) {
			DL_ERROR(rc, DF_DB ": failed to look up replica ID", DP_DB(db));
			goto err_gen_lock;
		}
	} else {
		db->d_replica_id.rri_rank = dss_self_rank();
		db->d_replica_id.rri_gen  = 0;
	}

	rc = rdb_chkptd_start(db);
	if (rc != 0)
		goto err_gen_lock;

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
err_gen_lock:
	ABT_rwlock_free(&db->d_gen_lock);
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
	rc = dss_vos_pool_open(path, (unsigned char *)uuid,
			       VOS_POF_SMALL | VOS_POF_EXCL | VOS_POF_RDB | VOS_POF_EXTERNAL_CHKPT,
			       &pool);
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

	rc = rdb_open_internal(pool, mc, uuid, version, caller_term, cbs, arg, &db);
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
	ABT_rwlock_free(&db->d_gen_lock);
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
 * Glance at \a storage and return \a clue. Callers are responsible for freeing
 * \a clue->bcl_replicas with d_rank_list_free.
 *
 * \param[in]	storage	database storage
 * \param[out]	clue	database clue
 */
int
rdb_glance(struct rdb_storage *storage, struct rdb_clue *clue)
{
	struct rdb                *db = rdb_from_storage(storage);
	d_iov_t                    value;
	uint64_t                   term;
	rdb_replica_id_t           vote;
	uint64_t                   last_index = db->d_lc_record.dlr_tail - 1;
	uint64_t                   last_term;
	struct rdb_replica_record *replicas;
	int                        replicas_len;
	d_rank_list_t             *ranks;
	int                        i;
	uint64_t                   oid_next;
	int                        rc;

	d_iov_set(&value, &term, sizeof(term));
	rc = rdb_mc_lookup(db->d_mc, RDB_MC_ATTRS, &rdb_mc_term, &value);
	if (rc == -DER_NONEXIST) {
		term = 0;
	} else if (rc != 0) {
		D_ERROR(DF_DB": failed to look up term: "DF_RC"\n", DP_DB(db), DP_RC(rc));
		goto err;
	}

	rdb_set_mc_vote_lookup_buf(db, &vote, &value);
	rc = rdb_mc_lookup(db->d_mc, RDB_MC_ATTRS, &rdb_mc_vote, &value);
	if (rc == -DER_NONEXIST) {
		vote.rri_rank = -1;
		vote.rri_gen  = -1;
	} else if (rc != 0) {
		D_ERROR(DF_DB": failed to look up vote: "DF_RC"\n", DP_DB(db), DP_RC(rc));
		goto err;
	}

	if (last_index == db->d_lc_record.dlr_base) {
		last_term = db->d_lc_record.dlr_base_term;
	} else {
		struct rdb_entry header;

		d_iov_set(&value, &header, sizeof(header));
		rc = rdb_lc_lookup(db->d_lc, last_index, RDB_LC_ATTRS, &rdb_lc_entry_header,
				   &value);
		if (rc != 0) {
			D_ERROR(DF_DB": failed to look up entry "DF_U64" header: %d\n", DP_DB(db),
				last_index, rc);
			goto err;
		}
		last_term = header.dre_term;
	}

	rc = rdb_raft_load_replicas(db->d_uuid, db->d_lc, last_index, db->d_version, &replicas,
				    &replicas_len);
	if (rc != 0) {
		D_ERROR(DF_DB": failed to load replicas at "DF_U64": "DF_RC"\n", DP_DB(db),
			last_index, DP_RC(rc));
		goto err;
	}
	ranks = d_rank_list_alloc(replicas_len);
	if (ranks == NULL) {
		D_ERROR(DF_DB ": failed to convert replicas to ranks\n", DP_DB(db));
		rc = -DER_NOMEM;
		D_FREE(replicas);
		goto err;
	}
	for (i = 0; i < replicas_len; i++)
		ranks->rl_ranks[i] = replicas[i].drr_id.rri_rank;
	D_FREE(replicas);

	d_iov_set(&value, &oid_next, sizeof(oid_next));
	rc = rdb_lc_lookup(db->d_lc, last_index, RDB_LC_ATTRS, &rdb_lc_oid_next, &value);
	if (rc == -DER_NONEXIST) {
		oid_next = RDB_LC_OID_NEXT_INIT;
	} else if (rc != 0) {
		D_ERROR(DF_DB": failed to look up next object number: %d\n", DP_DB(db), rc);
		goto err_ranks;
	}

	clue->bcl_term       = term;
	clue->bcl_vote       = vote.rri_rank;
	clue->bcl_self       = db->d_replica_id.rri_rank;
	clue->bcl_last_index = last_index;
	clue->bcl_last_term  = last_term;
	clue->bcl_base_index = db->d_lc_record.dlr_base;
	clue->bcl_base_term  = db->d_lc_record.dlr_base_term;
	clue->bcl_replicas   = ranks;
	clue->bcl_oid_next   = oid_next;
	return 0;

err_ranks:
	d_rank_list_free(ranks);
err:
	return rc;
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

	D_INFO(DF_DB ": started: db=%p version=%u use_leases=%d election_timeout=%d "
		     "request_timeout=%d lease_maintenance_grace=%d compact_thres=" DF_U64
		     " ae_max_entries=%u ae_max_size=" DF_U64 "\n",
	       DP_DB(db), db, db->d_version, db->d_use_leases,
	       raft_get_election_timeout(db->d_raft), raft_get_request_timeout(db->d_raft),
	       raft_get_lease_maintenance_grace(db->d_raft), db->d_compact_thres,
	       db->d_ae_max_entries, db->d_ae_max_size);
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

	D_INFO(DF_DB ": stopping: db=%p\n", DP_DB(db), db);

	ABT_mutex_lock(rdb_hash_lock);
	deleted = d_hash_rec_delete(&rdb_hash, db->d_uuid, sizeof(uuid_t));
	ABT_mutex_unlock(rdb_hash_lock);
	D_ASSERT(deleted);

	rdb_raft_stop(db);

	D_INFO(DF_DB ": stopped: db=%p\n", DP_DB(db), db);
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
 * Forcefully removing all other replicas from the membership. Callers must
 * destroy all other replicas (or prevent them from starting) beforehand.
 *
 * This API is for catastrophic recovery scenarios, for instance, when more
 * than a minority of replicas are lost.
 *
 *   1 Choose the best replica to recover from (see ds_pool_check_svc_clues).
 *   2 Destroy all other replicas (or prevent them from starting).
 *   3 Call rdb_open and rdb_dictate on the chosen replica.
 *
 * \param[in]	storage		database storage
 */
int
rdb_dictate(struct rdb_storage *storage)
{
	struct rdb *db = rdb_from_storage(storage);

	return rdb_raft_dictate(db);
}

/**
 * Allocate a replica generation.
 *
 * \param[in]	db		database
 * \param[in]	term		if not RDB_NIL_TERM, term to allocate in
 * \param[out]	gen_out		replica generation
 */
int
rdb_alloc_replica_gen(struct rdb *db, uint64_t term, uint32_t *gen_out)
{
	struct rdb_tx tx;
	d_iov_t       value;
	uint32_t      next;
	int           rc;

	if (db->d_version < RDB_LAYOUT_VERSION_REPLICA_ID) {
		D_DEBUG(DB_MD, DF_DB ": zero for old layout\n", DP_DB(db));
		*gen_out = 0;
		rc       = 0;
		goto out;
	}

	rc = rdb_tx_begin(db, term, &tx);
	if (rc != 0)
		goto out;
	ABT_rwlock_wrlock(db->d_gen_lock);

	d_iov_set(&value, &next, sizeof(next));
	rc = rdb_tx_lookup(&tx, &rdb_path_attrs, &rdb_lc_replica_gen_next, &value);
	if (rc != 0)
		goto out_lock;

	next++;

	rc = rdb_tx_update_critical(&tx, &rdb_path_attrs, &rdb_lc_replica_gen_next, &value);
	if (rc != 0)
		goto out_lock;

	rc = rdb_tx_commit(&tx);

out_lock:
	ABT_rwlock_unlock(db->d_gen_lock);
	rdb_tx_end(&tx);
	if (rc != 0)
		goto out;

	D_INFO(DF_DB ": updated next replica generation to %u\n", DP_DB(db), next);
	*gen_out = next - 1;
out:
	return rc;
}

/**
 * Modify \a replicas.
 *
 * \param[in]		db		database
 * \param[in]		op		operation to perform
 * \param[in,out]	replicas	[in] list of replica ranks;
 *					[out] list of replica ranks that could not be modified
 * \param[in,out]	replicas_len	length of \a replicas;
 */
int
rdb_modify_replicas(struct rdb *db, enum rdb_replica_op op, rdb_replica_id_t *replicas,
		    int *replicas_len)
{
	raft_logtype_e type;
	int            i;
	int            rc;

	D_DEBUG(DB_MD, DF_DB ": op=%d replicas=%d\n", DP_DB(db), op, *replicas_len);

	ABT_mutex_lock(db->d_raft_mutex);

	rc = rdb_raft_wait_applied(db, db->d_debut, raft_get_current_term(db->d_raft));
	if (rc != 0) {
		ABT_mutex_unlock(db->d_raft_mutex);
		return rc;
	}

	rc = -DER_INVAL;
	switch (op) {
	case RDB_REPLICA_ADD:
		type = RAFT_LOGTYPE_ADD_NODE;
		break;
	case RDB_REPLICA_REMOVE:
		type = RAFT_LOGTYPE_REMOVE_NODE;
		break;
	default:
		D_ASSERTF(0, "invalid op %d\n", op);
	}
	for (i = 0; i < *replicas_len; ++i) {
		rc = rdb_raft_append_apply_cfg(db, type, replicas[i]);
		if (rc != 0) {
			DL_ERROR(rc, DF_DB ": failed to do op %d on replica " RDB_F_RID, DP_DB(db),
				 op, RDB_P_RID(replicas[i]));
			break;
		}
	}

	ABT_mutex_unlock(db->d_raft_mutex);

	/* Update list to only contain replicas which could not be modified. */
	if (i > 0) {
		*replicas_len -= i;
		if (*replicas_len > 0)
			memmove(&replicas[0], &replicas[i], *replicas_len * sizeof(replicas[0]));
	}
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
 * \retval -DER_NO_PERM	not a voting replica or might violate a lease
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
	raft_node_t *node;

	ABT_mutex_lock(db->d_raft_mutex);
	node = raft_get_current_leader_node(db->d_raft);
	if (node == NULL) {
		ABT_mutex_unlock(db->d_raft_mutex);
		return -DER_NONEXIST;
	}
	*term = raft_get_current_term(db->d_raft);
	*rank = rdb_replica_id_decode(raft_node_get_id(node)).rri_rank;
	ABT_mutex_unlock(db->d_raft_mutex);

	return 0;
}

rdb_replica_id_t
rdb_get_replica_id(struct rdb *db)
{
	return db->d_replica_id;
}

int
rdb_get_replicas(struct rdb *db, rdb_replica_id_t **replicas, int *replicas_len)
{
	return rdb_raft_get_replicas(db, replicas, replicas_len);
}

static d_rank_list_t *
rdb_replica_id_to_rank_list(rdb_replica_id_t *replicas, int replicas_len)
{
	d_rank_list_t *ranks;
	int            i;

	ranks = d_rank_list_alloc(replicas_len);
	if (ranks == NULL)
		return NULL;

	for (i = 0; i < replicas_len; i++)
		ranks->rl_ranks[i] = replicas[i].rri_rank;

	return ranks;
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
	rdb_replica_id_t *replicas;
	int               replicas_len;
	d_rank_list_t    *ranks;
	int               rc;

	rc = rdb_get_replicas(db, &replicas, &replicas_len);
	if (rc != 0)
		return rc;

	ranks = rdb_replica_id_to_rank_list(replicas, replicas_len);
	D_FREE(replicas);
	if (ranks == NULL)
		return -DER_NOMEM;

	*ranksp = ranks;
	return 0;
}

int
rdb_get_size(struct rdb *db, uint64_t *sizep)
{
	int                   rc;
	struct vos_pool_space vps;

	rc = vos_pool_query_space(db->d_uuid, &vps);
	if (rc != 0) {
		D_ERROR(DF_DB ": failed to query vos pool space: " DF_RC "\n", DP_DB(db),
			DP_RC(rc));
		return rc;
	}

	*sizep = SCM_TOTAL(&vps);

	return rc;
}

uint32_t
rdb_get_version(struct rdb *db)
{
	return db->d_version;
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

	if (store->store_faulty)
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
	if (store->store_faulty ||
	    store->stor_ops->so_wal_id_cmp(store, commit_id, dcr->dcr_wait_id) >= 0) {
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

/**
 * Upgrade the durable format of the VOS pool underlying \a db to
 * \a df_version.
 *
 * Exposing "VOS pool" makes this API function hacky, and probably indicates
 * that the upgrade model is not quite right.
 *
 * \param[in]	db		database
 * \param[in]	df_version	VOS durable format version (e.g.,
 *				VOS_POOL_DF_2_6)
 */
int
rdb_upgrade_vos_pool(struct rdb *db, uint32_t df_version)
{
	return vos_pool_upgrade(db->d_pool, df_version);
}
