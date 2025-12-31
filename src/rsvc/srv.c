/*
 * (C) Copyright 2019-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
#include <daos_srv/control.h>
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

char *
ds_rsvc_state_str(enum ds_rsvc_state state)
{
	switch (state) {
	case DS_RSVC_STEPPING_UP:
		return "STEPPING_UP";
	case DS_RSVC_UP_EMPTY:
		return "UP_EMPTY";
	case DS_RSVC_UP:
		return "UP";
	case DS_RSVC_STEPPING_DOWN:
		return "STEPPING_DOWN";
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
	svc->s_map_distd = ABT_THREAD_NULL;

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
		rc = ABT_mutex_create(&svc->s_map_dist_mutex);
		if (rc != ABT_SUCCESS) {
			D_ERROR("%s: failed to create map_dist_mutex: %d\n", svc->s_name, rc);
			rc = dss_abterr2der(rc);
			goto err_leader_ref_cv;
		}
		rc = ABT_cond_create(&svc->s_map_dist_cv);
		if (rc != ABT_SUCCESS) {
			D_ERROR("%s: failed to create map_dist_cv: %d\n",
				svc->s_name, rc);
			rc = dss_abterr2der(rc);
			goto err_map_dist_mutex;
		}
	}

	*svcp = svc;
	return 0;

err_map_dist_mutex:
	if (rsvc_class(class)->sc_map_dist != NULL)
		ABT_mutex_free(&svc->s_map_dist_mutex);
err_leader_ref_cv:
	ABT_cond_free(&svc->s_leader_ref_cv);
err_state_cv:
	ABT_cond_free(&svc->s_state_cv);
err_mutex:
	ABT_mutex_free(&svc->s_mutex);
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
	if (rsvc_class(svc->s_class)->sc_map_dist != NULL) {
		ABT_cond_free(&svc->s_map_dist_cv);
		ABT_mutex_free(&svc->s_map_dist_mutex);
	}
	ABT_cond_free(&svc->s_leader_ref_cv);
	ABT_cond_free(&svc->s_state_cv);
	ABT_mutex_free(&svc->s_mutex);
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
		if (svc->s_db != NULL) { /* "nodb" */
			rdb_stop_and_close(svc->s_db);
			if (svc->s_destroy)
				rdb_destroy(svc->s_db_path, svc->s_db_uuid); /* ignore any error */
		}
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

	if (svc->s_db != NULL) /* "nodb" */
		rdb_stop_and_close(svc->s_db);
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
		ABT_cond_broadcast(svc->s_leader_ref_cv);
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
	D_DEBUG(DB_MD, "%s: term "DF_U64" state %s to %s\n", svc->s_name, svc->s_term,
		ds_rsvc_state_str(svc->s_state), ds_rsvc_state_str(state));
	svc->s_state = state;
	ABT_cond_broadcast(svc->s_state_cv);
}

static void map_distd(void *arg);

static int
init_map_distd(struct ds_rsvc *svc)
{
	int rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	D_ASSERT(svc->s_map_distd == ABT_THREAD_NULL);
	svc->s_map_dist = false;
	svc->s_map_dist_inp = false;
	svc->s_map_dist_ver = 0;
	svc->s_map_distd_stop = false;

	ds_rsvc_get(svc);
	get_leader(svc);
	rc = dss_ult_create(map_distd, svc, DSS_XS_SELF, 0, 0, &svc->s_map_distd);
	if (rc != 0) {
		D_ERROR("%s: failed to start map_distd: "DF_RC"\n", svc->s_name, DP_RC(rc));
		svc->s_map_distd = ABT_THREAD_NULL;
		put_leader(svc);
		ds_rsvc_put(svc);
	}

	return rc;
}

static void
drain_map_distd(struct ds_rsvc *svc)
{
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	svc->s_map_distd_stop = true;
	ABT_cond_broadcast(svc->s_map_dist_cv);
}

static void
fini_map_distd(struct ds_rsvc *svc)
{
	int rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	rc = ABT_thread_free(&svc->s_map_distd);
	D_ASSERTF(rc == 0, DF_RC"\n", DP_RC(rc));
}

/**
 * Release svc->s_mutex, which the caller must hold when calling. Change the
 * state of \a svc to STEPPING_UP. The caller must call ds_rsvc_end_stepping_up
 * after this function returns.
 */
void
ds_rsvc_begin_stepping_up(struct ds_rsvc *svc)
{
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	change_state(svc, DS_RSVC_STEPPING_UP);
	ABT_mutex_unlock(svc->s_mutex);
}

/**
 * Acquire svc->s_mutex, which the caller must release when appropriate. Change
 * the state of \a svc to \a state upon errors. If \a rc_in is nonzero, it will
 * be returned to the caller.
 */
int
ds_rsvc_end_stepping_up(struct ds_rsvc *svc, int rc_in, enum ds_rsvc_state state)
{
	bool map_distd_initialized = false;
	int  rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	D_DEBUG(DB_MD, "%s: ending stepping up: rc=" DF_RC " state=%s\n", svc->s_name, DP_RC(rc_in),
		ds_rsvc_state_str(state));
	D_ASSERT(state == DS_RSVC_UP_EMPTY || state == DS_RSVC_DOWN);

	if (rc_in != 0) {
		rc = rc_in;
		goto out;
	}

	if (rsvc_class(svc->s_class)->sc_map_dist != NULL) {
		rc = init_map_distd(svc);
		if (rc != 0) {
			DL_ERROR(rc, "%s: failed to initialize map_distd", svc->s_name);
			goto out;
		}
		map_distd_initialized = true;
	}

	if (state == DS_RSVC_UP_EMPTY && DAOS_FAIL_CHECK(DAOS_POOL_CREATE_FAIL_STEP_UP))
		rc = -DER_GRPVER;
	else
		rc = rsvc_class(svc->s_class)->sc_step_up(svc);
	if (rc != 0) {
		if (rc == DER_UNINIT)
			D_DEBUG(DB_MD, "%s: new db\n", svc->s_name);
		else
			D_DEBUG(DB_MD, "%s: failed to step up to " DF_U64 ": " DF_RC "\n",
				svc->s_name, svc->s_term, DP_RC(rc));
		/*
		 * For certain harder-to-recover errors, trigger a replica stop
		 * to avoid reporting them too many times. (A better strategy
		 * would be to leave the replica running without ever
		 * campaigning again, so that it could continue serving as a
		 * follower to other replicas.)
		 */
		if (rc == -DER_DF_INCOMPT)
			rc = -DER_SHUTDOWN;
		if (map_distd_initialized) {
			drain_map_distd(svc);
			fini_map_distd(svc);
		}
	}

out:
	ABT_mutex_lock(svc->s_mutex);
	if (rc == 0) {
		change_state(svc, DS_RSVC_UP);
	} else if (rc == DER_UNINIT) {
		change_state(svc, DS_RSVC_UP_EMPTY);
		rc = 0;
	} else {
		change_state(svc, state);
		/*
		 * If the error is from this function, and we are returning to
		 * UP_EMPTY, then resign the leadership so that a new leader
		 * will retry stepping up.
		 */
		if (rc_in == 0 && state == DS_RSVC_UP_EMPTY)
			rdb_resign(svc->s_db, svc->s_term);
	}
	return rc;
}

static int
rsvc_step_up_cb(struct rdb *db, uint64_t term, void *arg)
{
	struct ds_rsvc *svc = arg;
	int             rc;

	ABT_mutex_lock(svc->s_mutex);
	if (svc->s_stop) {
		D_DEBUG(DB_MD, "%s: skip term " DF_U64 " due to stopping\n", svc->s_name, term);
		rc = 0;
		goto out_mutex;
	}
	D_ASSERTF(svc->s_state == DS_RSVC_DOWN, "%d\n", svc->s_state);
	svc->s_term = term;
	D_DEBUG(DB_MD, "%s: stepping up to " DF_U64 "\n", svc->s_name, svc->s_term);

	ds_rsvc_begin_stepping_up(svc);

	rc = ds_rsvc_end_stepping_up(svc, 0 /* rc_in */, DS_RSVC_DOWN);

out_mutex:
	ABT_mutex_unlock(svc->s_mutex);
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
	 * This single-replica DB shall change from DS_RSVC_DOWN via
	 * DS_RSVC_STEPPING_UP to DS_RSVC_UP_EMPTY state promptly.
	 */
	while (svc->s_state == DS_RSVC_DOWN || svc->s_state == DS_RSVC_STEPPING_UP)
		ABT_cond_wait(svc->s_state_cv, svc->s_mutex);
	D_ASSERTF(svc->s_state == DS_RSVC_UP_EMPTY, "%d\n", svc->s_state);

	ds_rsvc_begin_stepping_up(svc);

	D_DEBUG(DB_MD, "%s: calling sc_bootstrap\n", svc->s_name);
	rc = rsvc_class(svc->s_class)->sc_bootstrap(svc, arg);

	rc = ds_rsvc_end_stepping_up(svc, rc, DS_RSVC_UP_EMPTY);

	ABT_mutex_unlock(svc->s_mutex);
	D_DEBUG(DB_MD, "%s: bootstrapped: "DF_RC"\n", svc->s_name, DP_RC(rc));
	return rc;
}

static void
rsvc_step_down_cb(struct rdb *db, uint64_t term, void *arg)
{
	struct ds_rsvc *svc = arg;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	D_DEBUG(DB_MD, "%s: stepping down from "DF_U64"\n", svc->s_name, term);
	ABT_mutex_lock(svc->s_mutex);

	/*
	 * There must have been a successful rsvc_step_up_cb call, which changes
	 * the state to either UP_EMPTY or UP. From UP_EMPTY, however, there may
	 * have been a bootstrap_self or ds_pool_create_handler calls either of
	 * which changes the state first to STEPPING_UP and then to either
	 * UP_EMPTY or UP.
	 */
	while (svc->s_state != DS_RSVC_UP_EMPTY && svc->s_state != DS_RSVC_UP) {
		D_ASSERTF(svc->s_state == DS_RSVC_STEPPING_UP, "unexpected state %s\n",
			  ds_rsvc_state_str(svc->s_state));
		ABT_cond_wait(svc->s_state_cv, svc->s_mutex);
	}
	D_ASSERTF(svc->s_term == term, DF_U64 " == " DF_U64 "\n", svc->s_term, term);

	if (svc->s_state == DS_RSVC_UP) {
		/* Stop accepting new leader references (ds_rsvc_lookup_leader). */
		change_state(svc, DS_RSVC_STEPPING_DOWN);

		if (rsvc_class(svc->s_class)->sc_map_dist != NULL)
			drain_map_distd(svc);

		rsvc_class(svc->s_class)->sc_drain(svc);

		/* Wait for all leader references to be released. */
		for (;;) {
			if (svc->s_leader_ref == 0)
				break;
			D_DEBUG(DB_MD, "%s: waiting for %d leader refs\n", svc->s_name,
				svc->s_leader_ref);
			ABT_cond_wait(svc->s_leader_ref_cv, svc->s_mutex);
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

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	D_DEBUG(DB_MD, "%s: start\n", svc->s_name);
	ABT_mutex_lock(svc->s_map_dist_mutex);
	for (;;) {
		uint32_t version;
		int      rc;

		for (;;) {
			if (svc->s_map_distd_stop)
				goto break_out;
			if (svc->s_map_dist) {
				/* Dequeue the request and start serving it. */
				svc->s_map_dist = false;
				svc->s_map_dist_inp = true;
				break;
			}
			sched_cond_wait(svc->s_map_dist_cv, svc->s_map_dist_mutex);
		}
		ABT_mutex_unlock(svc->s_map_dist_mutex);

		rc = rsvc_class(svc->s_class)->sc_map_dist(svc, &version);
		if (rc != 0) {
			/*
			 * Try again, but back off a little bit to limit the
			 * retry rate.
			 */
			dss_sleep(3000 /* ms */);
		}

		ABT_mutex_lock(svc->s_map_dist_mutex);
		/* Stop serving the request. */
		svc->s_map_dist_inp = false;
		if (rc == 0) {
			if (version > svc->s_map_dist_ver) {
				D_INFO("%s: version=%u->%u\n", svc->s_name, svc->s_map_dist_ver,
				       version);
				svc->s_map_dist_ver = version;
			}
			ABT_cond_broadcast(svc->s_map_dist_cv);
		} else {
			/* Enqueue the request again. */
			DL_INFO(rc, "%s: retrying due to error", svc->s_name);
			svc->s_map_dist = true;
		}
	}
break_out:
	ABT_mutex_unlock(svc->s_map_dist_mutex);
	put_leader(svc);
	D_DEBUG(DB_MD, "%s: stop\n", svc->s_name);
	ds_rsvc_put(svc);
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
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	svc->s_map_dist = true;
	ABT_cond_broadcast(svc->s_map_dist_cv);
	D_DEBUG(DB_MD, "%s: requested map distribution\n", svc->s_name);
}

/**
 * Query the map distribution state.
 * 
 * \param[in]	svc	replicated service
 * \param[out]	version	if not NULL, highest map version distributed
 *			successfully
 * \param[out]	idle	if not NULL, whether map distribution is idle (i.e., no
 *			in-progress or pending request)
 */
void
ds_rsvc_query_map_dist(struct ds_rsvc *svc, uint32_t *version, bool *idle)
{
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	if (version != NULL)
		*version = svc->s_map_dist_ver;
	if (idle != NULL)
		*idle = !svc->s_map_dist_inp && !svc->s_map_dist;
}

/**
 * Wait until map distribution is idle or stopping.
 * 
 * \param[in]	svc	replicated service
 */
void
ds_rsvc_wait_map_dist(struct ds_rsvc *svc)
{
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	D_DEBUG(DB_MD, "%s: begin", svc->s_name);
	ABT_mutex_lock(svc->s_map_dist_mutex);
	for (;;) {
		if (svc->s_map_distd_stop)
			break;
		if (!svc->s_map_dist && !svc->s_map_dist_inp)
			break;
		sched_cond_wait(svc->s_map_dist_cv, svc->s_map_dist_mutex);
	}
	ABT_mutex_unlock(svc->s_map_dist_mutex);
	D_DEBUG(DB_MD, "%s: end", svc->s_name);
}

static char *
start_mode_str(enum ds_rsvc_start_mode mode)
{
	switch (mode) {
	case DS_RSVC_START:
		return "start";
	case DS_RSVC_CREATE:
		return "create";
	case DS_RSVC_DICTATE:
		return "dictate";
	default:
		return "unknown";
	}
}

static bool
self_only(struct rdb_create_params *p)
{
	return p->rcp_replicas != NULL && p->rcp_replicas_len == 1 &&
	       rdb_replica_id_compare(p->rcp_replicas[0], p->rcp_id) == 0;
}

static int
start(enum ds_rsvc_class_id class, d_iov_t *id, uuid_t db_uuid, uint64_t term,
      enum ds_rsvc_start_mode mode, struct rdb_create_params *create_params, void *arg,
      struct ds_rsvc **svcp)
{
	struct rdb_storage     *storage;
	struct ds_rsvc	       *svc = NULL;
	int			rc;

	rc = alloc_init(class, id, db_uuid, &svc);
	if (rc != 0)
		goto err;
	svc->s_ref++;

	if (mode == DS_RSVC_CREATE)
		rc = rdb_create(svc->s_db_path, svc->s_db_uuid, term, create_params, &rsvc_rdb_cbs,
				svc, &storage);
	else
		rc = rdb_open(svc->s_db_path, svc->s_db_uuid, term, &rsvc_rdb_cbs, svc, &storage);
	if (rc != 0)
		goto err_svc;

	if (mode == DS_RSVC_DICTATE) {
		rc = rdb_dictate(storage);
		if (rc != 0)
			goto err_storage;
	}

	rc = rdb_start(storage, &svc->s_db);
	if (rc != 0)
		goto err_storage;

	if (mode == DS_RSVC_CREATE && self_only(create_params) &&
	    rsvc_class(class)->sc_bootstrap != NULL) {
		rc = bootstrap_self(svc, arg);
		if (rc != 0)
			goto err_db;
	}

	*svcp = svc;
	return 0;

err_db:
	rdb_stop(svc->s_db, &storage);
err_storage:
	rdb_close(storage);
	if (mode == DS_RSVC_CREATE)
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

	if (rsvc_class(svc->s_class)->sc_map_dist != NULL) {
		drain_map_distd(svc);
		fini_map_distd(svc);
	}

	ds_rsvc_put(svc);
	return 0;
}

/**
 * Start a replicated service. If \a mode is DS_RSVC_CREATE, create the replica
 * first; otherwise, \a create_params is ignored.
 *
 * \param[in]	class		replicated service class
 * \param[in]	id		replicated service ID
 * \param[in]	db_uuid		DB UUID
 * \param[in]	caller_term	caller term if not RDB_NIL_TERM (see rdb_open)
 * \param[in]	mode		mode of starting the replicated service
 * \param[in]	create_params	parameters used when \a mode is DS_RSVC_CREATE
 * \param[in]	arg		argument for cbs.sc_bootstrap
 *
 * \retval -DER_ALREADY		replicated service already started
 * \retval -DER_CANCELED	replicated service stopping
 * \retval -DER_STALE		stale \a caller_term
 */
int
ds_rsvc_start(enum ds_rsvc_class_id class, d_iov_t *id, uuid_t db_uuid, uint64_t caller_term,
	      enum ds_rsvc_start_mode mode, struct rdb_create_params *create_params, void *arg)
{
	struct ds_rsvc		*svc = NULL;
	d_list_t		*entry;
	int			 rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	entry = d_hash_rec_find(&rsvc_hash, id->iov_buf, id->iov_len);
	if (entry != NULL) {
		rdb_replica_id_t rid;

		svc = rsvc_obj(entry);
		rid = rdb_get_replica_id(svc->s_db);
		D_DEBUG(DB_MD, "%s: found " RDB_F_RID ": stop=%d mode=%s replicas=%p\n",
			svc->s_name, RDB_P_RID(rid), svc->s_stop, start_mode_str(mode),
			mode == DS_RSVC_CREATE ? create_params->rcp_replicas : NULL);
		if (mode == DS_RSVC_CREATE && create_params->rcp_replicas != NULL) {
			D_ERROR("%s: creating and bootstrapping existing replica not allowed\n",
				svc->s_name);
			rc = -DER_EXIST;
			goto out_svc;
		} else if (mode == DS_RSVC_CREATE && rid.rri_gen < create_params->rcp_id.rri_gen) {
			int n = 10;

			/*
			 * Destroy the older replica and continue. Note that the destroy only
			 * happens when the last svc reference is released.
			 */
			D_INFO("%s: destroying older replica " RDB_F_RID " for " RDB_F_RID "\n",
			       svc->s_name, RDB_P_RID(rid), RDB_P_RID(create_params->rcp_id));
			rc = ds_rsvc_stop(class, id, caller_term, true /* destroy */);
			if (rc != 0) {
				DL_ERROR(rc, "%s: failed to destroy existing replica", svc->s_name);
				goto out_svc;
			}
			while (svc->s_ref > 1 && n > 0) {
				dss_sleep(1000);
				n--;
			}
			if (svc->s_ref > 1) {
				D_ERROR("%s: gave up waiting for other service references\n",
					svc->s_name);
				rc = -DER_CANCELED;
				goto out_svc;
			}
		} else if (mode == DS_RSVC_CREATE && rid.rri_gen > create_params->rcp_id.rri_gen) {
			D_ERROR("%s: found newer replica: " RDB_F_RID " > " RDB_F_RID "\n",
				svc->s_name, RDB_P_RID(rid), RDB_P_RID(create_params->rcp_id));
			rc = -DER_EXIST;
			goto out_svc;
		} else if (mode == DS_RSVC_DICTATE && !svc->s_stop) {
			/*
			 * If we need to dictate, and the service is not
			 * stopping, then stop it, which should not fail in
			 * this case, and continue.
			 */
			rc = ds_rsvc_stop(class, id, caller_term, false /* destroy */);
			D_ASSERTF(rc == 0, DF_RC "\n", DP_RC(rc));
		} else {
			if (caller_term != RDB_NIL_TERM) {
				rc = rdb_ping(svc->s_db, caller_term);
				if (rc != 0) {
					D_CDEBUG(rc == -DER_STALE, DB_MD, DLOG_ERR,
						 "%s: failed to ping local replica\n", svc->s_name);
					goto out_svc;
				}
			}
			if (svc->s_stop)
				rc = -DER_CANCELED;
			else
				rc = -DER_ALREADY;
			goto out_svc;
		}
		ds_rsvc_put(svc);
	}

	rc = start(class, id, db_uuid, caller_term, mode, create_params, arg, &svc);
	if (rc != 0)
		goto out;

	rc = d_hash_rec_insert(&rsvc_hash, svc->s_id.iov_buf, svc->s_id.iov_len,
			       &svc->s_entry, true /* exclusive */);
	if (rc != 0) {
		D_DEBUG(DB_MD, "%s: insert: "DF_RC"\n", svc->s_name, DP_RC(rc));
		stop(svc, mode == DS_RSVC_CREATE /* destroy */);
		goto out;
	}

	D_DEBUG(DB_MD, "%s: started replicated service\n", svc->s_name);
out_svc:
	ds_rsvc_put(svc);
out:
	if (rc != 0 && rc != -DER_ALREADY && !(mode == DS_RSVC_CREATE && rc == -DER_EXIST))
		D_ERROR("Failed to start replicated service: "DF_RC"\n", DP_RC(rc));
	return rc;
}

static int
remove_path(char *path)
{
	int rc;

	rc = remove(path);
	if (rc != 0) {
		rc = errno;
		D_CDEBUG(rc == ENOENT, DB_MD, DLOG_ERR, "failed to remove %s: %d\n", path, rc);
		return daos_errno2der(rc);
	}
	return 0;
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

	if (svc->s_state == DS_RSVC_STEPPING_UP || svc->s_state == DS_RSVC_UP_EMPTY ||
	    svc->s_state == DS_RSVC_UP)
		/*
		 * The service is stepping up or has stepped up. If it is still
		 * the leader of svc->s_term, the following rdb_resign() call
		 * will trigger the matching rsvc_step_down_cb() callback in
		 * svc->s_term; otherwise, the callback must already be pending.
		 * Either way, the service shall eventually enter the
		 * DS_RSVC_DOWN state.
		 */
		rdb_resign(svc->s_db, svc->s_term);
	while (svc->s_state != DS_RSVC_DOWN)
		ABT_cond_wait(svc->s_state_cv, svc->s_mutex);

	if (destroy) {
		D_ASSERT(d_list_empty(&svc->s_entry));
		svc->s_destroy = true;
	}

	ABT_mutex_unlock(svc->s_mutex);
	ds_rsvc_put(svc);
	return rc;
}

/**
 * Stop a replicated service. If destroy is true, destroy the service
 * afterward.
 *
 * \param[in]	class		replicated service class
 * \param[in]	id		replicated service ID
 * \param[in]	caller_term	caller term if not RDB_NIL_TERM (see rdb_open)
 * \param[in]	destroy		whether to destroy the replica after stopping
 *
 * \retval -DER_CANCELED	replicated service stopping
 * \retval -DER_STALE		stale \a caller_term
 */
int
ds_rsvc_stop(enum ds_rsvc_class_id class, d_iov_t *id, uint64_t caller_term, bool destroy)
{
	struct ds_rsvc		*svc;
	int			 rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	rc = ds_rsvc_lookup(class, id, &svc);
	if (rc != 0) {
		if (rc != -DER_NOTREPLICA && destroy) {
			char *path;

			rc = rsvc_class(class)->sc_locate(id, &path);
			if (rc != 0)
				return rc;
			rc = remove_path(path);
			D_FREE(path);
			if (rc != 0 && rc != -DER_NONEXIST)
				return rc;
		}
		return 0;
	}

	if (caller_term != RDB_NIL_TERM) {
		rc = rdb_ping(svc->s_db, caller_term);
		if (rc != 0) {
			D_CDEBUG(rc == -DER_STALE, DB_MD, DLOG_ERR,
				 "%s: failed to ping local replica\n", svc->s_name);
			ds_rsvc_put(svc);
			return rc;
		}
	}

	d_hash_rec_delete_at(&rsvc_hash, &svc->s_entry);

	return stop(svc, destroy);
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
ds_rsvc_add_replicas_s(struct ds_rsvc *svc, d_rank_list_t *ranks, size_t size,
		       uint32_t vos_df_version)
{
	int i;
	int rc = 0;

	/* Add one by one to reduce waste of replica generations. */
	for (i = 0; i < ranks->rl_nr; i++) {
		d_rank_t                     r = ranks->rl_ranks[i];
		d_rank_list_t                rl;
		rdb_replica_id_t             id;
		int                          ids_len = 1;
		struct ds_rsvc_create_params create_params;

		rl.rl_ranks = &r;
		rl.rl_nr    = 1;

		id.rri_rank = r;

		/* This allocation cannot be rolled back. */
		rc = rdb_alloc_replica_gen(svc->s_db, svc->s_term, &id.rri_gen);
		if (rc != 0)
			break;

		create_params.scp_bootstrap      = false;
		create_params.scp_size           = size;
		create_params.scp_vos_df_version = vos_df_version;
		create_params.scp_layout_version = rdb_get_version(svc->s_db);
		create_params.scp_replicas       = &id;
		create_params.scp_replicas_len   = 1;

		rc = ds_rsvc_dist_start(svc->s_class, &svc->s_id, svc->s_db_uuid, &rl, svc->s_term,
					DS_RSVC_CREATE, &create_params);
		if (rc != 0)
			break;

		rc = rdb_modify_replicas(svc->s_db, RDB_REPLICA_ADD, &id, &ids_len);
		if (rc != 0) {
			ds_rsvc_dist_stop(svc->s_class, &svc->s_id, &rl, NULL, svc->s_term,
					  true /* destroy */);
			break;
		}
	}

	/* Remove all i successfully-added ranks from ranks. */
	if (i > 0) {
		ranks->rl_nr -= i;
		if (ranks->rl_nr > 0)
			memmove(&ranks->rl_ranks[0], &ranks->rl_ranks[i],
				ranks->rl_nr * sizeof(ranks->rl_ranks[0]));
	}
	return rc;
}

enum ds_rsvc_state
ds_rsvc_get_state(struct ds_rsvc *svc)
{
	return svc->s_state;
}

void
ds_rsvc_set_state(struct ds_rsvc *svc, enum ds_rsvc_state state)
{
	change_state(svc, state);
}

int
ds_rsvc_add_replicas(enum ds_rsvc_class_id class, d_iov_t *id, d_rank_list_t *ranks, size_t size,
		     uint32_t vos_df_version, struct rsvc_hint *hint)
{
	struct ds_rsvc	*svc;
	int		 rc;

	rc = ds_rsvc_lookup_leader(class, id, &svc, hint);
	if (rc != 0)
		return rc;
	rc = ds_rsvc_add_replicas_s(svc, ranks, size, vos_df_version);
	ds_rsvc_set_hint(svc, hint);
	ds_rsvc_put_leader(svc);
	return rc;
}

int
ds_rsvc_remove_replicas_s(struct ds_rsvc *svc, d_rank_list_t *ranks, bool destroy)
{
	d_rank_list_t    *stop_ranks;
	rdb_replica_id_t *all;
	int               all_len;
	rdb_replica_id_t *to_remove;
	int               to_remove_len = 0;
	int               i;
	int               rc;

	rc = d_rank_list_dup(&stop_ranks, ranks);
	if (rc != 0)
		goto out;

	/* Fill to_remove with replica IDs of ranks. */
	rc = rdb_get_replicas(svc->s_db, &all, &all_len);
	if (rc != 0)
		goto out_stop_ranks;
	D_ALLOC_ARRAY(to_remove, ranks->rl_nr);
	if (to_remove == NULL) {
		rc = -DER_NOMEM;
		goto out_all;
	}
	for (i = 0; i < ranks->rl_nr; i++) {
		d_rank_t rank = ranks->rl_ranks[i];
		int      j;

		for (j = 0; j < all_len; j++) {
			if (all[j].rri_rank == rank) {
				to_remove[to_remove_len] = all[j];
				to_remove_len++;
				break;
			}
		}
		if (j == all_len) {
			D_ERROR("%s: rank %u not found in replica list\n", svc->s_name, rank);
			rc = -DER_NONEXIST;
			goto out_to_remove;
		}
	}

	rc = rdb_modify_replicas(svc->s_db, RDB_REPLICA_REMOVE, to_remove, &to_remove_len);

	/* Update ranks with to_remove (those that couldn't be removed). */
	D_ASSERTF(ranks->rl_nr >= to_remove_len, "%d >= %d\n", ranks->rl_nr, to_remove_len);
	ranks->rl_nr = to_remove_len;
	for (i = 0; i < to_remove_len; i++)
		ranks->rl_ranks[i] = to_remove[i].rri_rank;

	if (destroy) {
		/* filter out failed ranks */
		d_rank_list_filter(ranks, stop_ranks, true /* exclude */);
		if (stop_ranks->rl_nr > 0)
			ds_rsvc_dist_stop(svc->s_class, &svc->s_id, stop_ranks, NULL, svc->s_term,
					  true /* destroy */);
	}

out_to_remove:
	D_FREE(to_remove);
out_all:
	D_FREE(all);
out_stop_ranks:
	d_rank_list_free(stop_ranks);
out:
	return rc;
}

int
ds_rsvc_remove_replicas(enum ds_rsvc_class_id class, d_iov_t *id,
			d_rank_list_t *ranks, struct rsvc_hint *hint)
{
	struct ds_rsvc	*svc;
	int		 rc;

	rc = ds_rsvc_lookup_leader(class, id, &svc, hint);
	if (rc != 0)
		return rc;
	rc = ds_rsvc_remove_replicas_s(svc, ranks, true /* destroy */);
	ds_rsvc_set_hint(svc, hint);
	ds_rsvc_put_leader(svc);
	return rc;
}

/*************************** Distributed Operations ***************************/

enum rdb_start_flag {
	RDB_AF_BOOTSTRAP	= 0x1,
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
	uint8_t                 rsvc_ver;
	int                     rc;

	rc = ds_rsvc_rpc_protocol(&rsvc_ver);
	if (rc)
		return rc;

	D_ASSERT(!filter_invert || filter_ranks != NULL);
	opc_full = DAOS_RPC_OPCODE(opc, DAOS_RSVC_MODULE, rsvc_ver);
	return crt_corpc_req_create(info->dmi_ctx, NULL /* grp */,
				    filter_ranks, opc_full,
				    NULL /* co_bulk_hdl */, NULL /* priv */,
				    filter_invert ?
				    CRT_RPC_FLAG_FILTER_INVERT : 0,
				    crt_tree_topo(CRT_TREE_KNOMIAL, 2), rpc);
}

/**
 * Perform a distributed start operation in \a mode on all replicas of a
 * database with \a dbid spanning \a ranks. This method can be called on any
 * rank. If \a mode is DS_RSVC_START, \a ranks may be NULL. If \a mode is
 * DS_RSVC_DICTATE, \a ranks must comprise one and only one rank.
 *
 * \param[in]	class		replicated service class
 * \param[in]	id		replicated service ID
 * \param[in]	dbid		database UUID
 * \param[in]	ranks		list of replica ranks
 * \param[in]	caller_term	caller term if not RDB_NIL_TERM (see rdb_open)
 * \param[in]	mode		mode of starting the replicated service
 * \param[in]	create_params	parameters used when \a mode is DS_RSVC_CREATE
 */
int
ds_rsvc_dist_start(enum ds_rsvc_class_id class, d_iov_t *id, const uuid_t dbid,
		   const d_rank_list_t *ranks, uint64_t caller_term, enum ds_rsvc_start_mode mode,
		   struct ds_rsvc_create_params *create_params)
{
	crt_rpc_t		*rpc;
	struct rsvc_start_in	*in;
	struct rsvc_start_out	*out;
	int			 rc;

	D_ASSERT(mode != DS_RSVC_CREATE ||
		 (create_params != NULL && create_params->scp_replicas != NULL &&
		  create_params->scp_replicas_len > 0));
	D_ASSERT(mode != DS_RSVC_DICTATE || (ranks != NULL && ranks->rl_nr == 1));
	D_DEBUG(DB_MD, DF_UUID": %s DB\n", DP_UUID(dbid), start_mode_str(mode));

	rc = bcast_create(RSVC_START, ranks != NULL /* filter_invert */,
			  (d_rank_list_t *)ranks, &rpc);
	if (rc != 0)
		goto out;
	in = crt_req_get(rpc);
	in->sai_class = class;
	in->sai_svc_id = *id;
	uuid_copy(in->sai_db_uuid, dbid);
	in->sai_mode = mode;
	in->sai_term = caller_term;
	if (mode == DS_RSVC_CREATE) {
		if (create_params->scp_bootstrap)
			in->sai_flags |= RDB_AF_BOOTSTRAP;
		in->sai_size               = create_params->scp_size;
		in->sai_vos_df_version     = create_params->scp_vos_df_version;
		in->sai_layout_version     = create_params->scp_layout_version;
		in->sai_replicas.ca_arrays = create_params->scp_replicas;
		in->sai_replicas.ca_count  = create_params->scp_replicas_len;
	}

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		goto out_rpc;

	out = crt_reply_get(rpc);
	rc = out->sao_rc;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to %s %d replicas: "DF_RC"\n", DP_UUID(dbid),
			start_mode_str(mode), rc, DP_RC(out->sao_rc_errval));
		if (ranks == NULL || ranks->rl_nr > 1)
			ds_rsvc_dist_stop(class, id, ranks, NULL, caller_term,
					  mode == DS_RSVC_CREATE);
		rc = out->sao_rc_errval;
	}

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
	struct rdb_create_params create_params;
	bool                     create = in->sai_mode == DS_RSVC_CREATE;
	int			 rc;

	if (create) {
		d_rank_t         self_rank = dss_self_rank();
		rdb_replica_id_t self;
		bool             bootstrap = in->sai_flags & RDB_AF_BOOTSTRAP;
		int              i;

		if (in->sai_replicas.ca_arrays == NULL || in->sai_replicas.ca_count == 0) {
			D_ERROR(DF_UUID ": no replica IDs\n", DP_UUID(in->sai_db_uuid));
			rc = -DER_PROTO;
			goto out;
		}

		/* Find self replica ID in in->sai_replicas. */
		for (i = 0; i < in->sai_replicas.ca_count; i++)
			if (in->sai_replicas.ca_arrays[i].rri_rank == self_rank)
				break;
		if (i == in->sai_replicas.ca_count) {
			D_ERROR(DF_UUID ": self not in replica IDs: self=%u replicas=" DF_U64 "\n",
				DP_UUID(in->sai_db_uuid), self_rank, in->sai_replicas.ca_count);
			rc = -DER_PROTO;
			goto out;
		}
		self = in->sai_replicas.ca_arrays[i];

		create_params.rcp_size           = in->sai_size;
		create_params.rcp_vos_df_version = in->sai_vos_df_version;
		create_params.rcp_layout_version = in->sai_layout_version;
		create_params.rcp_id             = self;
		create_params.rcp_replicas       = bootstrap ? in->sai_replicas.ca_arrays : NULL;
		create_params.rcp_replicas_len   = bootstrap ? in->sai_replicas.ca_count : 0;
	}

	rc = ds_rsvc_start(in->sai_class, &in->sai_svc_id, in->sai_db_uuid, in->sai_term,
			   in->sai_mode, create ? &create_params : NULL, NULL /* arg */);
	if (rc == -DER_ALREADY)
		rc = 0;

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
 * for collective RPC, so we have to use both ranks and excluded for the moment,
 * and it should be simplified once cart can provide rank list collective RPC.
 *
 * \param[in]	class		replicated service class
 * \param[in]	id		replicated service ID
 * \param[in]	ranks		list of \a ranks->rl_nr replica ranks
 * \param[in]	excluded	excluded rank list.
 * \param[in]	caller_term	caller term if not RDB_NIL_TERM (see rdb_open)
 * \param[in]	destroy		destroy after close
 */
int
ds_rsvc_dist_stop(enum ds_rsvc_class_id class, d_iov_t *id, const d_rank_list_t *ranks,
		  d_rank_list_t *excluded, uint64_t caller_term, bool destroy)
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
	in->soi_term = caller_term;

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

	rc = ds_rsvc_stop(in->soi_class, &in->soi_svc_id, in->soi_term,
			  in->soi_flags & RDB_OF_DESTROY);
	if (rc == -DER_ALREADY)
		rc = 0;

	out->soo_rc = (rc == 0 ? 0 : 1);
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
	const size_t	size_default = DEFAULT_DAOS_MD_CAP_SIZE;
	char	       *v;
	int		n;

	d_agetenv_str(&v, DAOS_MD_CAP_ENV); /* in MB */
	if (v == NULL)
		return size_default;
	n = atoi(v);
	d_freeenv_str(&v);
	if ((n << 20) < MINIMUM_DAOS_MD_CAP_SIZE) {
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
    .sm_name        = "rsvc",
    .sm_mod_id      = DAOS_RSVC_MODULE,
    .sm_ver         = DAOS_RSVC_VERSION,
    .sm_proto_count = 1,
    .sm_init        = rsvc_module_init,
    .sm_fini        = rsvc_module_fini,
    .sm_proto_fmt   = {&rsvc_proto_fmt},
    .sm_cli_count   = {0},
    .sm_handlers    = {rsvc_handlers},
    .sm_key         = NULL,
};

DEFINE_DS_RPC_PROTOCOL(rsvc, DAOS_RSVC_MODULE);
