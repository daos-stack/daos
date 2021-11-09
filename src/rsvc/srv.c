/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * ds_rsvc: Replicated Service Server
 */

#define D_LOGFAC DD_FAC(rsvc)

#include <sys/stat.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/rsvc.h>
#include "rpc.h"

static struct ds_rsvc_class *rsvc_classes[DS_RSVC_CLASS_COUNT];

void
ds_rsvc_class_register(enum ds_rsvc_class_id id, struct ds_rsvc_class *class)
{
	D_ASSERT(class != NULL);
	D_ASSERT(rsvc_classes[id] == NULL);
	rsvc_classes[id] = class;
}

void
ds_rsvc_class_unregister(enum ds_rsvc_class_id id)
{
	D_ASSERT(rsvc_classes[id] != NULL);
	rsvc_classes[id] = NULL;
}

static struct ds_rsvc_class *
rsvc_class(enum ds_rsvc_class_id id)
{
	D_ASSERTF(id >= 0 && id < DS_RSVC_CLASS_COUNT, "%d\n", id);
	D_ASSERT(rsvc_classes[id] != NULL);
	return rsvc_classes[id];
}

static char *
state_str(enum ds_rsvc_state state)
{
	switch (state) {
	case DS_RSVC_UP_EMPTY:
		return "UP_EMPTY";
	case DS_RSVC_UP:
		return "UP";
	case DS_RSVC_DRAINING:
		return "DRAINING";
	case DS_RSVC_DOWN:
		return "DOWN";
	default:
		return "UNKNOWN";
	}
}

/* Allocate and initialize a ds_rsvc object. */
static int
alloc_init(enum ds_rsvc_class_id class, d_iov_t *id, uuid_t db_uuid,
	   struct ds_rsvc **svcp)
{
	struct ds_rsvc *svc;
	int		rc;

	rc = rsvc_class(class)->sc_alloc(id, &svc);
	if (rc != 0)
		goto err;

	D_INIT_LIST_HEAD(&svc->s_entry);
	svc->s_class = class;
	D_ASSERT(svc->s_id.iov_buf != NULL);
	D_ASSERT(svc->s_id.iov_len > 0);
	D_ASSERT(svc->s_id.iov_buf_len >= svc->s_id.iov_len);
	uuid_copy(svc->s_db_uuid, db_uuid);
	svc->s_state = DS_RSVC_DOWN;

	rc = rsvc_class(class)->sc_name(&svc->s_id, &svc->s_name);
	if (rc != 0)
		goto err_svc;

	rc = rsvc_class(class)->sc_locate(&svc->s_id, &svc->s_db_path);
	if (rc != 0)
		goto err_name;

	rc = ABT_mutex_create(&svc->s_mutex);
	if (rc != ABT_SUCCESS) {
		D_ERROR("%s: failed to create mutex: %d\n", svc->s_name, rc);
		rc = dss_abterr2der(rc);
		goto err_db_path;
	}

	rc = ABT_cond_create(&svc->s_state_cv);
	if (rc != ABT_SUCCESS) {
		D_ERROR("%s: failed to create state_cv: %d\n", svc->s_name, rc);
		rc = dss_abterr2der(rc);
		goto err_mutex;
	}

	rc = ABT_cond_create(&svc->s_leader_ref_cv);
	if (rc != ABT_SUCCESS) {
		D_ERROR("%s: failed to create leader_ref_cv: %d\n", svc->s_name,
			rc);
		rc = dss_abterr2der(rc);
		goto err_state_cv;
	}

	if (rsvc_class(class)->sc_map_dist != NULL) {
		rc = ABT_cond_create(&svc->s_map_dist_cv);
		if (rc != ABT_SUCCESS) {
			D_ERROR("%s: failed to create map_dist_cv: %d\n",
				svc->s_name, rc);
			rc = dss_abterr2der(rc);
			goto err_leader_ref_cv;
		}
	}

	*svcp = svc;
	return 0;

err_leader_ref_cv:
	ABT_cond_free(&svc->s_leader_ref_cv);
err_state_cv:
	ABT_cond_free(&svc->s_state_cv);
err_mutex:
	DABT_MUTEX_FREE(&svc->s_mutex);
err_db_path:
	D_FREE(svc->s_db_path);
err_name:
	D_FREE(svc->s_name);
err_svc:
	rsvc_class(class)->sc_free(svc);
err:
	return rc;
}

static void
fini_free(struct ds_rsvc *svc)
{
	D_ASSERT(d_list_empty(&svc->s_entry));
	D_ASSERTF(svc->s_ref == 0, "%d\n", svc->s_ref);
	D_ASSERTF(svc->s_leader_ref == 0, "%d\n", svc->s_leader_ref);
	if (rsvc_class(svc->s_class)->sc_map_dist != NULL)
		ABT_cond_free(&svc->s_map_dist_cv);
	ABT_cond_free(&svc->s_leader_ref_cv);
	ABT_cond_free(&svc->s_state_cv);
	DABT_MUTEX_FREE(&svc->s_mutex);
	D_FREE(svc->s_db_path);
	D_FREE(svc->s_name);
	rsvc_class(svc->s_class)->sc_free(svc);
}

void
ds_rsvc_get(struct ds_rsvc *svc)
{
	svc->s_ref++;
}

/** Put a replicated service reference. */
void
ds_rsvc_put(struct ds_rsvc *svc)
{
	D_ASSERTF(svc->s_ref > 0, "%d\n", svc->s_ref);
	svc->s_ref--;
	if (svc->s_ref == 0) {
		rdb_stop(svc->s_db);
		fini_free(svc);
	}
}

static struct d_hash_table rsvc_hash;

static struct ds_rsvc *
rsvc_obj(d_list_t *rlink)
{
	return container_of(rlink, struct ds_rsvc, s_entry);
}

static bool
rsvc_key_cmp(struct d_hash_table *htable, d_list_t *rlink, const void *key,
	     unsigned int ksize)
{
	struct ds_rsvc *svc = rsvc_obj(rlink);

	if (svc->s_id.iov_len != ksize)
		return false;
	return memcmp(svc->s_id.iov_buf, key, svc->s_id.iov_len) == 0;
}

static void
rsvc_rec_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ds_rsvc *svc = rsvc_obj(rlink);

	svc->s_ref++;
}

static bool
rsvc_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ds_rsvc *svc = rsvc_obj(rlink);

	D_ASSERTF(svc->s_ref > 0, "%d\n", svc->s_ref);
	svc->s_ref--;
	return svc->s_ref == 0;
}

static void
rsvc_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ds_rsvc *svc = rsvc_obj(rlink);

	rdb_stop(svc->s_db);
	fini_free(svc);
}

static d_hash_table_ops_t rsvc_hash_ops = {
	.hop_key_cmp	= rsvc_key_cmp,
	.hop_rec_addref	= rsvc_rec_addref,
	.hop_rec_decref	= rsvc_rec_decref,
	.hop_rec_free	= rsvc_rec_free
};

static int
rsvc_hash_init(void)
{
	return d_hash_table_create_inplace(D_HASH_FT_NOLOCK, 4 /* bits */,
					   NULL /* priv */, &rsvc_hash_ops,
					   &rsvc_hash);
}

static void
rsvc_hash_fini(void)
{
	d_hash_table_destroy_inplace(&rsvc_hash, true /* force */);
}

/**
 * Look up a replicated service.
 *
 * \param[in]	class	replicated service class
 * \param[in]	id	replicated service ID
 * \param[out]	svc	replicated service
 */
int
ds_rsvc_lookup(enum ds_rsvc_class_id class, d_iov_t *id,
	       struct ds_rsvc **svc)
{
	d_list_t       *entry;
	bool		nonexist = false;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	entry = d_hash_rec_find(&rsvc_hash, id->iov_buf, id->iov_len);
	if (entry == NULL) {
		char	       *path = NULL;
		struct stat	buf;
		int		rc;

		/*
		 * See if the DB exists. If an error prevents us from find that
		 * out, return -DER_NOTLEADER so that the client tries other
		 * replicas.
		 */
		rc = rsvc_class(class)->sc_locate(id, &path);
		if (rc != 0)
			goto out;
		rc = stat(path, &buf);
		if (rc != 0) {
			if (errno == ENOENT) {
				nonexist = true;
			} else {
				char *name = NULL;

				rsvc_class(class)->sc_name(id, &name);
				D_ERROR("%s: failed to stat %s: %d\n", name,
					path, errno);
				if (name != NULL)
					D_FREE(name);
			}
		}
		D_FREE(path);
	}
out:
	if (nonexist)
		return -DER_NOTREPLICA;
	if (entry == NULL)
		return -DER_NOTLEADER;
	*svc = rsvc_obj(entry);
	return 0;
}

/*
 * Is svc up (i.e., ready to accept RPCs)? If not, the caller may always report
 * -DER_NOTLEADER, even if svc->s_db is in leader state, in which case the
 * client will retry the RPC.
 */
static bool
up(struct ds_rsvc *svc)
{
	return !svc->s_stop && svc->s_state == DS_RSVC_UP;
}

/**
 * Retrieve the latest leader hint from \a db and fill it into \a hint.
 *
 * \param[in]	svc	replicated service
 * \param[out]	hint	rsvc hint
 */
void
ds_rsvc_set_hint(struct ds_rsvc *svc, struct rsvc_hint *hint)
{
	int rc;

	rc = rdb_get_leader(svc->s_db, &hint->sh_term, &hint->sh_rank);
	if (rc != 0)
		return;
	hint->sh_flags |= RSVC_HINT_VALID;
}

static void
get_leader(struct ds_rsvc *svc)
{
	svc->s_leader_ref++;
}

static void
put_leader(struct ds_rsvc *svc)
{
	D_ASSERTF(svc->s_leader_ref > 0, "%d\n", svc->s_leader_ref);
	svc->s_leader_ref--;
	if (svc->s_leader_ref == 0)
		DABT_COND_BROADCAST(svc->s_leader_ref_cv);
}

/**
 * As a convenience for general replicated service RPC handlers, this function
 * looks up the replicated service by id, checks that it is up, and takes a
 * reference to the leader fields. svcp is filled only if zero is returned. If
 * the replicated service is not up, hint is filled.
 */
int
ds_rsvc_lookup_leader(enum ds_rsvc_class_id class, d_iov_t *id,
		      struct ds_rsvc **svcp, struct rsvc_hint *hint)
{
	struct ds_rsvc *svc;
	int		rc;

	rc = ds_rsvc_lookup(class, id, &svc);
	if (rc != 0)
		return rc;
	if (!up(svc)) {
		if (hint != NULL)
			ds_rsvc_set_hint(svc, hint);
		ds_rsvc_put(svc);
		return -DER_NOTLEADER;
	}
	get_leader(svc);
	*svcp = svc;
	return 0;
}

/** Get a reference to the leader fields of \a svc. */
void
ds_rsvc_get_leader(struct ds_rsvc *svc)
{
	ds_rsvc_get(svc);
	get_leader(svc);
}

/**
 * Put the reference returned by ds_rsvc_lookup_leader or ds_rsvc_get_leader to
 * the leader fields of \a svc.
 */
void
ds_rsvc_put_leader(struct ds_rsvc *svc)
{
	put_leader(svc);
	ds_rsvc_put(svc);
}

static void
change_state(struct ds_rsvc *svc, enum ds_rsvc_state state)
{
	D_DEBUG(DB_MD, "%s: term "DF_U64" state %s to %s\n", svc->s_name,
		svc->s_term, state_str(svc->s_state), state_str(state));
	svc->s_state = state;
	DABT_COND_BROADCAST(svc->s_state_cv);
}

static void map_distd(void *arg);

static int
init_map_distd(struct ds_rsvc *svc)
{
	int rc;

	svc->s_map_dist = false;
	svc->s_map_distd_stop = false;

	ds_rsvc_get(svc);
	get_leader(svc);
	rc = dss_ult_create(map_distd, svc, DSS_XS_SELF, 0, 0,
			    &svc->s_map_distd);
	if (rc != 0) {
		D_ERROR("%s: failed to start map_distd: "DF_RC"\n", svc->s_name,
			DP_RC(rc));
		put_leader(svc);
		ds_rsvc_put(svc);
	}

	return rc;
}

static void
drain_map_distd(struct ds_rsvc *svc)
{
	svc->s_map_distd_stop = true;
	DABT_COND_BROADCAST(svc->s_map_dist_cv);
}

static void
fini_map_distd(struct ds_rsvc *svc)
{
	DABT_THREAD_FREE(&svc->s_map_distd);
}

static int
rsvc_step_up_cb(struct rdb *db, uint64_t term, void *arg)
{
	struct ds_rsvc *svc = arg;
	bool		map_distd_initialized = false;
	int		rc;

	ABT_mutex_lock(svc->s_mutex);
	if (svc->s_stop) {
		D_DEBUG(DB_MD, "%s: skip term "DF_U64" due to stopping\n",
			svc->s_name, term);
		rc = 0;
		goto out_mutex;
	}
	D_ASSERTF(svc->s_state == DS_RSVC_DOWN, "%d\n", svc->s_state);
	svc->s_term = term;
	D_DEBUG(DB_MD, "%s: stepping up to "DF_U64"\n", svc->s_name,
		svc->s_term);

	if (rsvc_class(svc->s_class)->sc_map_dist != NULL) {
		rc = init_map_distd(svc);
		if (rc != 0)
			goto out_mutex;
		map_distd_initialized = true;
	}

	rc = rsvc_class(svc->s_class)->sc_step_up(svc);
	if (rc == DER_UNINIT) {
		change_state(svc, DS_RSVC_UP_EMPTY);
		rc = 0;
		goto out_mutex;
	} else if (rc != 0) {
		D_DEBUG(DB_MD, "%s: failed to step up to "DF_U64": "DF_RC"\n",
			svc->s_name, term, DP_RC(rc));
		if (map_distd_initialized)
			drain_map_distd(svc);
		/*
		 * For certain harder-to-recover errors, trigger a replica stop
		 * to avoid reporting them too many times. (A better strategy
		 * would be to leave the replica running without ever
		 * campaigning again, so that it could continue serving as a
		 * follower to other replicas.)
		 */
		if (rc == -DER_DF_INCOMPT)
			rc = -DER_SHUTDOWN;
		goto out_mutex;
	}

	change_state(svc, DS_RSVC_UP);
out_mutex:
	ABT_mutex_unlock(svc->s_mutex);
	if (rc != 0 && map_distd_initialized)
		fini_map_distd(svc);
	return rc;
}

/* Bootstrap a self-only, single-replica DB created in start. */
static int
bootstrap_self(struct ds_rsvc *svc, void *arg)
{
	int rc;

	D_DEBUG(DB_MD, "%s: bootstrapping\n", svc->s_name);
	ABT_mutex_lock(svc->s_mutex);

	/*
	 * This single-replica DB shall change from DS_RSVC_DOWN to
	 * DS_RSVC_UP_EMPTY state promptly.
	 */
	while (svc->s_state == DS_RSVC_DOWN)
		DABT_COND_WAIT(svc->s_state_cv, svc->s_mutex);
	D_ASSERTF(svc->s_state == DS_RSVC_UP_EMPTY, "%d\n", svc->s_state);

	D_DEBUG(DB_MD, "%s: calling sc_bootstrap\n", svc->s_name);
	rc = rsvc_class(svc->s_class)->sc_bootstrap(svc, arg);
	if (rc != 0)
		goto out_mutex;

	/* Try stepping up again. */
	D_DEBUG(DB_MD, "%s: calling sc_step_up\n", svc->s_name);
	rc = rsvc_class(svc->s_class)->sc_step_up(svc);
	if (rc != 0) {
		D_ASSERT(rc != DER_UNINIT);
		goto out_mutex;
	}

	change_state(svc, DS_RSVC_UP);
out_mutex:
	ABT_mutex_unlock(svc->s_mutex);
	D_DEBUG(DB_MD, "%s: bootstrapped: "DF_RC"\n", svc->s_name, DP_RC(rc));
	return rc;
}

static void
rsvc_step_down_cb(struct rdb *db, uint64_t term, void *arg)
{
	struct ds_rsvc *svc = arg;

	D_DEBUG(DB_MD, "%s: stepping down from "DF_U64"\n", svc->s_name, term);
	ABT_mutex_lock(svc->s_mutex);
	D_ASSERTF(svc->s_term == term, DF_U64" == "DF_U64"\n", svc->s_term,
		  term);
	D_ASSERT(svc->s_state == DS_RSVC_UP_EMPTY ||
		 svc->s_state == DS_RSVC_UP);

	if (svc->s_state == DS_RSVC_UP) {
		/* Stop accepting new leader references. */
		change_state(svc, DS_RSVC_DRAINING);

		if (rsvc_class(svc->s_class)->sc_map_dist != NULL)
			drain_map_distd(svc);

		rsvc_class(svc->s_class)->sc_drain(svc);

		/* TODO: Abort all in-flight RPCs we sent. */

		/* Wait for all leader references to be released. */
		for (;;) {
			if (svc->s_leader_ref == 0)
				break;
			D_DEBUG(DB_MD, "%s: waiting for %d leader refs\n",
				svc->s_name, svc->s_leader_ref);
			DABT_COND_WAIT(svc->s_leader_ref_cv, svc->s_mutex);
		}

		rsvc_class(svc->s_class)->sc_step_down(svc);

		if (rsvc_class(svc->s_class)->sc_map_dist != NULL)
			fini_map_distd(svc);
	}

	change_state(svc, DS_RSVC_DOWN);
	ABT_mutex_unlock(svc->s_mutex);
	D_DEBUG(DB_MD, "%s: stepped down from "DF_U64"\n", svc->s_name, term);
}

static int stop(struct ds_rsvc *svc, bool destroy);

static void
rsvc_stopper(void *arg)
{
	struct ds_rsvc *svc = arg;

	d_hash_rec_delete_at(&rsvc_hash, &svc->s_entry);
	stop(svc, false /* destroy */);
}

static void
rsvc_stop_cb(struct rdb *db, int err, void *arg)
{
	struct ds_rsvc *svc = arg;
	int		rc;

	ds_rsvc_get(svc);
	rc = dss_ult_create(rsvc_stopper, svc, DSS_XS_SELF, 0, 0, NULL);
	if (rc != 0) {
		D_ERROR("%s: failed to create service stopper: "DF_RC"\n",
			svc->s_name, DP_RC(rc));
		ds_rsvc_put(svc);
	}
}

static struct rdb_cbs rsvc_rdb_cbs = {
	.dc_step_up	= rsvc_step_up_cb,
	.dc_step_down	= rsvc_step_down_cb,
	.dc_stop	= rsvc_stop_cb
};

static void
map_distd(void *arg)
{
	struct ds_rsvc *svc = arg;

	D_DEBUG(DB_MD, "%s: start\n", svc->s_name);
	for (;;) {
		bool	stop;
		int	rc;

		ABT_mutex_lock(svc->s_mutex);
		for (;;) {
			stop = svc->s_map_distd_stop;
			if (stop)
				break;
			if (svc->s_map_dist) {
				svc->s_map_dist = false;
				break;
			}
			sched_cond_wait(svc->s_map_dist_cv, svc->s_mutex);
		}
		ABT_mutex_unlock(svc->s_mutex);
		if (stop)
			break;
		rc = rsvc_class(svc->s_class)->sc_map_dist(svc);
		if (rc != 0) {
			/*
			 * Try again, but back off a little bit to limit the
			 * retry rate.
			 */
			svc->s_map_dist = true;
			dss_sleep(3000 /* ms */);
		}
	}
	put_leader(svc);
	ds_rsvc_put(svc);
	D_DEBUG(DB_MD, "%s: stop\n", svc->s_name);
}

/**
 * Request an asynchronous map distribution. This eventually triggers
 * ds_rsvc_class.sc_map_dist, which must be implemented by the rsvc class.
 *
 * \param[in]	svc	replicated service
 */
void
ds_rsvc_request_map_dist(struct ds_rsvc *svc)
{
	svc->s_map_dist = true;
	DABT_COND_BROADCAST(svc->s_map_dist_cv);
}

static bool
nominated(d_rank_list_t *replicas, uuid_t db_uuid)
{
	int i;

	/* No initial membership. */
	if (replicas == NULL || replicas->rl_nr < 1)
		return false;

	/* Only one replica. */
	if (replicas->rl_nr == 1)
		return true;

	/*
	 * Nominate by hashing the DB UUID. The only requirement is that every
	 * replica shall end up with the same nomination.
	 */
	i = d_hash_murmur64(db_uuid, sizeof(uuid_t), 0x2db) % replicas->rl_nr;

	return (replicas->rl_ranks[i] == dss_self_rank());
}

static bool
self_only(d_rank_list_t *replicas)
{
	return (replicas != NULL && replicas->rl_nr == 1 &&
		replicas->rl_ranks[0] == dss_self_rank());
}

static int
start(enum ds_rsvc_class_id class, d_iov_t *id, uuid_t db_uuid, bool create,
      size_t size, d_rank_list_t *replicas, void *arg, struct ds_rsvc **svcp)
{
	struct ds_rsvc *svc = NULL;
	int		rc;

	rc = alloc_init(class, id, db_uuid, &svc);
	if (rc != 0)
		goto err;
	svc->s_ref++;

	if (create)
		rc = rdb_create(svc->s_db_path, svc->s_db_uuid, size, replicas,
				&rsvc_rdb_cbs, svc, &svc->s_db);
	else
		rc = rdb_start(svc->s_db_path, svc->s_db_uuid, &rsvc_rdb_cbs,
			       svc, &svc->s_db);
	if (rc != 0)
		goto err_svc;

	/*
	 * If creating a replica with an initial membership, we are
	 * bootstrapping the DB (via sc_bootstrap or an external mechanism). If
	 * we are the "nominated" replica, start a campaign without waiting for
	 * the election timeout.
	 */
	if (create && nominated(replicas, svc->s_db_uuid)) {
		/* Give others a chance to get ready for voting. */
		dss_sleep(1 /* ms */);
		rc = rdb_campaign(svc->s_db);
		if (rc != 0)
			goto err_db;
	}

	if (create && self_only(replicas) &&
	    rsvc_class(class)->sc_bootstrap != NULL) {
		rc = bootstrap_self(svc, arg);
		if (rc != 0)
			goto err_db;
	}

	*svcp = svc;
	return 0;

err_db:
	rdb_stop(svc->s_db);
	if (create)
		rdb_destroy(svc->s_db_path, svc->s_db_uuid);
err_svc:
	svc->s_ref--;
	fini_free(svc);
err:
	return rc;
}

int
ds_rsvc_start_nodb(enum ds_rsvc_class_id class, d_iov_t *id, uuid_t db_uuid)
{
	struct ds_rsvc		*svc = NULL;
	d_list_t		*entry;
	int			 rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	entry = d_hash_rec_find(&rsvc_hash, id->iov_buf, id->iov_len);
	if (entry != NULL) {
		svc = rsvc_obj(entry);
		D_DEBUG(DB_MD, "%s: found: stop=%d\n", svc->s_name,
			svc->s_stop);
		if (svc->s_stop)
			rc = -DER_CANCELED;
		else
			rc = -DER_ALREADY;
		ds_rsvc_put(svc);
		goto out;
	}

	rc = alloc_init(class, id, db_uuid, &svc);
	if (rc != 0)
		goto out;
	svc->s_ref++;

	rc = d_hash_rec_insert(&rsvc_hash, svc->s_id.iov_buf, svc->s_id.iov_len,
			       &svc->s_entry, true /* exclusive */);
	if (rc != 0) {
		D_DEBUG(DB_MD, "%s: insert: "DF_RC"\n", svc->s_name, DP_RC(rc));
		stop(svc, false /* destroy */);
		goto err_svc;
	}

	if (rsvc_class(svc->s_class)->sc_map_dist != NULL) {
		rc = init_map_distd(svc);
		if (rc != 0)
			goto err_svc;
	}
	change_state(svc, DS_RSVC_UP);

	D_DEBUG(DB_MD, "%s: started service\n", svc->s_name);
	ds_rsvc_put(svc);

	goto out;

err_svc:
	svc->s_ref--;
	fini_free(svc);
out:
	if (rc != 0 && rc != -DER_ALREADY)
		D_ERROR("Failed to start service: "DF_RC"\n",
			DP_RC(rc));
	return rc;
}

int
ds_rsvc_stop_nodb(enum ds_rsvc_class_id class, d_iov_t *id)
{
	struct ds_rsvc		*svc;
	int			 rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	rc = ds_rsvc_lookup(class, id, &svc);
	if (rc != 0)
		return -DER_ALREADY;

	d_hash_rec_delete_at(&rsvc_hash, &svc->s_entry);

	ABT_mutex_lock(svc->s_mutex);
	if (rsvc_class(svc->s_class)->sc_map_dist != NULL)
		drain_map_distd(svc);
	ABT_mutex_unlock(svc->s_mutex);
	if (rsvc_class(svc->s_class)->sc_map_dist != NULL)
		fini_map_distd(svc);

	ds_rsvc_put(svc);
	return 0;
}

/**
 * Start a replicated service. If \a create is false, all remaining input
 * parameters are ignored; otherwise, create the replica first. If \a replicas
 * is NULL, all remaining input parameters are ignored; otherwise, bootstrap
 * the replicated service.
 *
 * \param[in]	class		replicated service class
 * \param[in]	id		replicated service ID
 * \param[in]	db_uuid		DB UUID
 * \param[in]	create		whether to create the replica before starting
 * \param[in]	size		replica size in bytes
 * \param[in]	replicas	optional initial membership
 * \param[in]	arg		argument for cbs.sc_bootstrap
 *
 * \retval -DER_ALREADY		replicated service already started
 * \retval -DER_CANCELED	replicated service stopping
 */
int
ds_rsvc_start(enum ds_rsvc_class_id class, d_iov_t *id, uuid_t db_uuid,
	      bool create, size_t size, d_rank_list_t *replicas, void *arg)
{
	struct ds_rsvc		*svc = NULL;
	d_list_t		*entry;
	int			 rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	entry = d_hash_rec_find(&rsvc_hash, id->iov_buf, id->iov_len);
	if (entry != NULL) {
		svc = rsvc_obj(entry);
		D_DEBUG(DB_MD, "%s: found: stop=%d\n", svc->s_name,
			svc->s_stop);
		if (svc->s_stop)
			rc = -DER_CANCELED;
		else
			rc = -DER_ALREADY;
		ds_rsvc_put(svc);
		goto out;
	}

	rc = start(class, id, db_uuid, create, size, replicas, arg, &svc);
	if (rc != 0)
		goto out;

	rc = d_hash_rec_insert(&rsvc_hash, svc->s_id.iov_buf, svc->s_id.iov_len,
			       &svc->s_entry, true /* exclusive */);
	if (rc != 0) {
		D_DEBUG(DB_MD, "%s: insert: "DF_RC"\n", svc->s_name, DP_RC(rc));
		stop(svc, create /* destroy */);
		goto out;
	}

	D_DEBUG(DB_MD, "%s: started replicated service\n", svc->s_name);
	ds_rsvc_put(svc);
out:
	if (rc != 0 && rc != -DER_ALREADY && !(create && rc == -DER_EXIST))
		D_ERROR("Failed to start replicated service: "DF_RC"\n",
			DP_RC(rc));
	return rc;
}

static int
stop(struct ds_rsvc *svc, bool destroy)
{
	int rc = 0;

	ABT_mutex_lock(svc->s_mutex);

	if (svc->s_stop) {
		ABT_mutex_unlock(svc->s_mutex);
		D_DEBUG(DB_MD, "%s: stopping already\n", svc->s_name);
		return -DER_CANCELED;
	}
	svc->s_stop = true;
	D_DEBUG(DB_MD, "%s: stopping\n", svc->s_name);

	if (svc->s_state == DS_RSVC_UP || svc->s_state == DS_RSVC_UP_EMPTY)
		/*
		 * The service has stepped up. If it is still the leader of
		 * svc->s_term, the following rdb_resign() call will trigger
		 * the matching rsvc_step_down_cb() callback in svc->s_term;
		 * otherwise, the callback must already be pending. Either way,
		 * the service shall eventually enter the DS_RSVC_DOWN state.
		 */
		rdb_resign(svc->s_db, svc->s_term);
	while (svc->s_state != DS_RSVC_DOWN)
		DABT_COND_WAIT(svc->s_state_cv, svc->s_mutex);

	if (destroy)
		rc = remove(svc->s_db_path);

	ABT_mutex_unlock(svc->s_mutex);
	ds_rsvc_put(svc);
	return rc;
}

/**
 * Stop a replicated service. If destroy is false, all remaining parameters are
 * ignored; otherwise, destroy the service afterward.
 *
 * \param[in]	class		replicated service class
 * \param[in]	id		replicated service ID
 * \param[in]	destroy		whether to destroy the replica after stopping
 *
 * \retval -DER_ALREADY		replicated service already stopped
 * \retval -DER_CANCELED	replicated service stopping
 */
int
ds_rsvc_stop(enum ds_rsvc_class_id class, d_iov_t *id, bool destroy)
{
	struct ds_rsvc		*svc;
	int			 rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	rc = ds_rsvc_lookup(class, id, &svc);
	if (rc != 0)
		return -DER_ALREADY;
	d_hash_rec_delete_at(&rsvc_hash, &svc->s_entry);
	return stop(svc, destroy);
}

struct stop_ult {
	d_list_t	su_entry;
	ABT_thread	su_thread;
};

struct stop_all_arg {
	d_list_t		saa_list;	/* of stop_ult objects */
	enum ds_rsvc_class_id	saa_class;
};

static int
stop_all_cb(d_list_t *entry, void *varg)
{
	struct ds_rsvc	       *svc = rsvc_obj(entry);
	struct stop_all_arg    *arg = varg;
	struct stop_ult	       *ult;
	int			rc;

	if (svc->s_class != arg->saa_class)
		return 0;

	D_ALLOC_PTR(ult);
	if (ult == NULL)
		return -DER_NOMEM;

	d_hash_rec_addref(&rsvc_hash, &svc->s_entry);
	rc = dss_ult_create(rsvc_stopper, svc, DSS_XS_SYS, 0, 0,
			    &ult->su_thread);
	if (rc != 0) {
		d_hash_rec_decref(&rsvc_hash, &svc->s_entry);
		D_FREE(ult);
		return rc;
	}

	d_list_add(&ult->su_entry, &arg->saa_list);
	return 0;
}

/**
 * Stop all replicated services of \a class.
 *
 * \param[in]	class	replicated service class
 */
int
ds_rsvc_stop_all(enum ds_rsvc_class_id class)
{
	struct stop_all_arg	arg;
	struct stop_ult	       *ult;
	struct stop_ult	       *ult_tmp;
	int			rc;

	D_INIT_LIST_HEAD(&arg.saa_list);
	arg.saa_class = class;
	rc = d_hash_table_traverse(&rsvc_hash, stop_all_cb, &arg);

	/* Wait for the stopper ULTs to return. */
	d_list_for_each_entry_safe(ult, ult_tmp, &arg.saa_list, su_entry) {
		d_list_del_init(&ult->su_entry);
		DABT_THREAD_FREE(&ult->su_thread);
		D_FREE(ult);
	}

	if (rc != 0)
		D_ERROR("failed to stop all replicated services: "DF_RC"\n",
			DP_RC(rc));
	return rc;
}

/**
 * Stop a replicated service if it is in leader state. Currently, this is used
 * only for testing.
 *
 * \param[in]	class	replicated service class
 * \param[in]	id	replicated service ID
 * \param[out]	hint	rsvc hint
 */
int
ds_rsvc_stop_leader(enum ds_rsvc_class_id class, d_iov_t *id,
		    struct rsvc_hint *hint)
{
	struct ds_rsvc *svc;
	int		rc;

	rc = ds_rsvc_lookup_leader(class, id, &svc, hint);
	if (rc != 0)
		return rc;
	/* Drop our leader reference to allow the service to step down. */
	put_leader(svc);

	d_hash_rec_delete_at(&rsvc_hash, &svc->s_entry);

	return stop(svc, false /* destroy */);
}

int
ds_rsvc_add_replicas_s(struct ds_rsvc *svc, d_rank_list_t *ranks, size_t size)
{
	int	rc;

	rc = ds_rsvc_dist_start(svc->s_class, &svc->s_id, svc->s_db_uuid, ranks,
				true /* create */, false /* bootstrap */, size);

	/* TODO: Attempt to only add replicas that were successfully started */
	if (rc != 0)
		goto out_stop;
	rc = rdb_add_replicas(svc->s_db, ranks);
out_stop:
	/* Clean up ranks that were not added */
	if (ranks->rl_nr > 0) {
		D_ASSERT(rc != 0);
		ds_rsvc_dist_stop(svc->s_class, &svc->s_id, ranks,
				  NULL, true /* destroy */);
	}
	return rc;
}

int
ds_rsvc_add_replicas(enum ds_rsvc_class_id class, d_iov_t *id,
		     d_rank_list_t *ranks, size_t size, struct rsvc_hint *hint)
{
	struct ds_rsvc	*svc;
	int		 rc;

	rc = ds_rsvc_lookup_leader(class, id, &svc, hint);
	if (rc != 0)
		return rc;
	rc = ds_rsvc_add_replicas_s(svc, ranks, size);
	ds_rsvc_set_hint(svc, hint);
	put_leader(svc);
	return rc;
}

int
ds_rsvc_remove_replicas_s(struct ds_rsvc *svc, d_rank_list_t *ranks, bool stop)
{
	d_rank_list_t	*stop_ranks;
	int		 rc;

	rc = daos_rank_list_dup(&stop_ranks, ranks);
	if (rc != 0)
		return rc;
	rc = rdb_remove_replicas(svc->s_db, ranks);

	/* filter out failed ranks */
	daos_rank_list_filter(ranks, stop_ranks, true /* exclude */);
	if (stop_ranks->rl_nr > 0 && stop)
		ds_rsvc_dist_stop(svc->s_class, &svc->s_id, stop_ranks,
				  NULL, true /* destroy */);
	d_rank_list_free(stop_ranks);
	return rc;
}

int
ds_rsvc_remove_replicas(enum ds_rsvc_class_id class, d_iov_t *id,
			d_rank_list_t *ranks, bool stop, struct rsvc_hint *hint)
{
	struct ds_rsvc	*svc;
	int		 rc;

	rc = ds_rsvc_lookup_leader(class, id, &svc, hint);
	if (rc != 0)
		return rc;
	rc = ds_rsvc_remove_replicas_s(svc, ranks, stop);
	ds_rsvc_set_hint(svc, hint);
	put_leader(svc);
	return rc;
}

/*************************** Distributed Operations ***************************/

enum rdb_start_flag {
	RDB_AF_CREATE		= 0x1,
	RDB_AF_BOOTSTRAP	= 0x2
};

enum rdb_stop_flag {
	RDB_OF_DESTROY		= 0x1
};

/*
 * Create a bcast in the primary group. If filter_invert is false, bcast to the
 * whole primary group filtering out filter_ranks; otherwise, bcast to
 * filter_ranks only.
 */
static int
bcast_create(crt_opcode_t opc, bool filter_invert, d_rank_list_t *filter_ranks,
	     crt_rpc_t **rpc)
{
	struct dss_module_info *info = dss_get_module_info();
	crt_opcode_t		opc_full;

	D_ASSERT(!filter_invert || filter_ranks != NULL);
	opc_full = DAOS_RPC_OPCODE(opc, DAOS_RSVC_MODULE, DAOS_RSVC_VERSION);
	return crt_corpc_req_create(info->dmi_ctx, NULL /* grp */,
				    filter_ranks, opc_full,
				    NULL /* co_bulk_hdl */, NULL /* priv */,
				    filter_invert ?
				    CRT_RPC_FLAG_FILTER_INVERT : 0,
				    crt_tree_topo(CRT_TREE_FLAT, 0), rpc);
}

/**
 * Perform a distributed create, if \a create is true, and start operation on
 * all replicas of a database with \a dbid spanning \a ranks. This method can
 * be called on any rank. If \a create is false, \a ranks may be NULL.
 *
 * \param[in]	class		replicated service class
 * \param[in]	id		replicated service ID
 * \param[in]	dbid		database UUID
 * \param[in]	ranks		list of replica ranks
 * \param[in]	create		create replicas first
 * \param[in]	bootstrap	start with an initial list of replicas
 * \param[in]	size		size of each replica in bytes if \a create
 */
int
ds_rsvc_dist_start(enum ds_rsvc_class_id class, d_iov_t *id, const uuid_t dbid,
		   const d_rank_list_t *ranks, bool create, bool bootstrap,
		   size_t size)
{
	crt_rpc_t		*rpc;
	struct rsvc_start_in	*in;
	struct rsvc_start_out	*out;
	int			 rc;

	D_ASSERT(!bootstrap || ranks != NULL);
	D_DEBUG(DB_MD, DF_UUID": %s DB\n",
		DP_UUID(dbid), create ? "creating" : "starting");

	rc = bcast_create(RSVC_START, ranks != NULL /* filter_invert */,
			  (d_rank_list_t *)ranks, &rpc);
	if (rc != 0)
		goto out;
	in = crt_req_get(rpc);
	in->sai_class = class;
	rc = daos_iov_copy(&in->sai_svc_id, id);
	if (rc != 0)
		goto out_rpc;
	uuid_copy(in->sai_db_uuid, dbid);
	if (create)
		in->sai_flags |= RDB_AF_CREATE;
	if (bootstrap)
		in->sai_flags |= RDB_AF_BOOTSTRAP;
	in->sai_size = size;
	in->sai_ranks = (d_rank_list_t *)ranks;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		goto out_mem;

	out = crt_reply_get(rpc);
	rc = out->sao_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to start%s %d replicas: "DF_RC"\n",
			DP_UUID(dbid), create ? "/create" : "", rc,
			DP_RC(out->sao_rc_errval));
		ds_rsvc_dist_stop(class, id, ranks, NULL, create);
		rc = out->sao_rc_errval;
	}

out_mem:
	daos_iov_free(&in->sai_svc_id);
out_rpc:
	crt_req_decref(rpc);
out:
	return rc;
}

static void
ds_rsvc_start_handler(crt_rpc_t *rpc)
{
	struct rsvc_start_in	*in = crt_req_get(rpc);
	struct rsvc_start_out	*out = crt_reply_get(rpc);
	bool			 create = in->sai_flags & RDB_AF_CREATE;
	bool			 bootstrap = in->sai_flags & RDB_AF_BOOTSTRAP;
	int			 rc;

	if (bootstrap && in->sai_ranks == NULL) {
		rc = -DER_PROTO;
		goto out;
	}

	rc = ds_rsvc_start(in->sai_class, &in->sai_svc_id, in->sai_db_uuid,
			   create, in->sai_size,
			   bootstrap ? in->sai_ranks : NULL, NULL /* arg */);

out:
	out->sao_rc_errval = rc;
	out->sao_rc = (rc == 0 ? 0 : 1);
	crt_reply_send(rpc);
}

static int
ds_rsvc_start_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct rsvc_start_out   *out_source;
	struct rsvc_start_out   *out_result;

	out_source = crt_reply_get(source);
	out_result = crt_reply_get(result);
	/* rc is error count, rc_errval first error value */
	out_result->sao_rc += out_source->sao_rc;

	if (out_result->sao_rc_errval == 0) {
		if (out_source->sao_rc_errval != 0)
			out_result->sao_rc_errval = out_source->sao_rc_errval;
	}

	return 0;
}

/**
 * Perform a distributed stop, and if \a destroy is true, destroy operation on
 * all replicas of a database spanning \a ranks. This method can be called on
 * any rank. \a ranks may be NULL.
 *
 * XXX excluded and ranks are a bit duplicate here, since this function only
 * suppose to send RPC to @ranks list, but cart does not have such interface
 * for collective RPC, so we have to use both ranks and exclued for the moment,
 * and it should be simplified once cart can provide rank list collective RPC.
 *
 * \param[in]	class		replicated service class
 * \param[in]	id		replicated service ID
 * \param[in]	ranks		list of \a ranks->rl_nr replica ranks
 * \param[in]	excluded	excluded rank list.
 * \param[in]	destroy		destroy after close
 */
int
ds_rsvc_dist_stop(enum ds_rsvc_class_id class, d_iov_t *id,
		  const d_rank_list_t *ranks, d_rank_list_t *excluded,
		  bool destroy)
{
	crt_rpc_t		*rpc;
	struct rsvc_stop_in	*in;
	struct rsvc_stop_out	*out;
	int			 rc;

	/* No "ranks != NULL && excluded != NULL" use case currently. */
	D_ASSERT(ranks == NULL || excluded == NULL);

	rc = bcast_create(RSVC_STOP, ranks != NULL /* filter_invert */,
			  ranks != NULL ? (d_rank_list_t *)ranks : excluded,
			  &rpc);
	if (rc != 0)
		goto out;
	in = crt_req_get(rpc);
	in->soi_class = class;
	rc = daos_iov_copy(&in->soi_svc_id, id);
	if (rc != 0)
		goto out_rpc;
	if (destroy)
		in->soi_flags |= RDB_OF_DESTROY;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		goto out_mem;

	out = crt_reply_get(rpc);
	rc = out->soo_rc;
	if (rc != 0) {
		D_ERROR("failed to stop%s replicas: "DF_RC"\n",
			destroy ? "/destroy" : "", DP_RC(rc));
		rc = -DER_IO;
	}

out_mem:
	daos_iov_free(&in->soi_svc_id);
out_rpc:
	crt_req_decref(rpc);
out:
	return rc;
}

static void
ds_rsvc_stop_handler(crt_rpc_t *rpc)
{
	struct rsvc_stop_in	*in = crt_req_get(rpc);
	struct rsvc_stop_out	*out = crt_reply_get(rpc);
	int			 rc = 0;

	rc = ds_rsvc_stop(in->soi_class, &in->soi_svc_id,
			  in->soi_flags & RDB_OF_DESTROY);
	out->soo_rc = (rc == 0 || rc == -DER_ALREADY ? 0 : 1);
	crt_reply_send(rpc);
}

static int
ds_rsvc_stop_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct rsvc_stop_out   *out_source;
	struct rsvc_stop_out   *out_result;

	out_source = crt_reply_get(source);
	out_result = crt_reply_get(result);
	out_result->soo_rc += out_source->soo_rc;
	return 0;
}

static struct crt_corpc_ops ds_rsvc_start_co_ops = {
	.co_aggregate	= ds_rsvc_start_aggregator,
	.co_pre_forward	= NULL,
};

static struct crt_corpc_ops ds_rsvc_stop_co_ops = {
	.co_aggregate	= ds_rsvc_stop_aggregator,
	.co_pre_forward	= NULL,
};

#define X(a, b, c, d, e)	\
{				\
	.dr_opc       = a,	\
	.dr_hdlr      = d,	\
	.dr_corpc_ops = e,	\
}

static struct daos_rpc_handler rsvc_handlers[] = {
	RSVC_PROTO_SRV_RPC_LIST,
};

#undef X

size_t
ds_rsvc_get_md_cap(void)
{
	const size_t	size_default = 1 << 27 /* 128 MB */;
	char	       *v;
	int		n;

	v = getenv("DAOS_MD_CAP"); /* in MB */
	if (v == NULL)
		return size_default;
	n = atoi(v);
	if (n < size_default >> 20) {
		D_ERROR("metadata capacity too low; using %zu MB\n",
			size_default >> 20);
		return size_default;
	}
	return (size_t)n << 20;
}

static int
rsvc_module_init(void)
{
	return rsvc_hash_init();
}

static int
rsvc_module_fini(void)
{
	rsvc_hash_fini();
	return 0;
}

struct dss_module rsvc_module = {
	.sm_name	= "rsvc",
	.sm_mod_id	= DAOS_RSVC_MODULE,
	.sm_ver		= DAOS_RSVC_VERSION,
	.sm_init	= rsvc_module_init,
	.sm_fini	= rsvc_module_fini,
	.sm_proto_fmt	= &rsvc_proto_fmt,
	.sm_cli_count	= 0,
	.sm_handlers	= rsvc_handlers,
	.sm_key		= NULL,
};
