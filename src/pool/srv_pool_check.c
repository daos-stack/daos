/*
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * ds_pool: Pool Service Check
 */

#define D_LOGFAC DD_FAC(pool)

#include <daos_srv/pool.h>

#include <sys/stat.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/daos_mgmt_srv.h>
#include <daos_srv/rdb.h>
#include "srv_internal.h"
#include "srv_layout.h"

static int
pool_svc_glance(uuid_t uuid, char *path, struct ds_pool_svc_clue *clue_out)
{
	struct rdb_storage     *storage;
	struct ds_pool_svc_clue	clue;
	struct rdb_tx		tx;
	rdb_path_t		root;
	struct pool_buf	       *map_buf;
	int			rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	rc = rdb_open(path, uuid, NULL /* cbs */, NULL /* arg */, &storage);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to open %s: "DF_RC"\n", DP_UUID(uuid), path, DP_RC(rc));
		goto out;
	}

	rc = rdb_glance(storage, &clue.psc_db_clue);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to glance at %s: "DF_RC"\n", DP_UUID(uuid), path,
			DP_RC(rc));
		goto out_storage;
	}

	rc = rdb_tx_begin_local(storage, &tx);
	if (rc != 0)
		goto out_db_clue;

	rc = rdb_path_init(&root);
	if (rc != 0)
		goto out_tx;
	rc = rdb_path_push(&root, &rdb_path_root_key);
	if (rc != 0)
		goto out_root;

	rc = ds_pool_svc_load(&tx, uuid, &root, &map_buf, &clue.psc_map_version);
	if (rc == DER_UNINIT) {
		clue.psc_map_version = 0;
		rc = 0;
	} else if (rc != 0) {
		goto out_root;
	}

	*clue_out = clue;
	D_FREE(map_buf);
out_root:
	rdb_path_fini(&root);
out_tx:
	rdb_tx_end(&tx);
out_db_clue:
	if (rc != 0)
		d_rank_list_free(clue.psc_db_clue.bcl_replicas);
out_storage:
	rdb_close(storage);
out:
	return rc;
}

/**
 * Glance at the pool with \a uuid in \a dir, and report \a clue about its
 * persistent state. Note that if an error has occurred, it is reported in \a
 * clue->pc_rc, with \a clue->pc_uuid and \a clue->pc_dir fields also being
 * valid.
 *
 * \param[in]	uuid	pool UUID
 * \param[in]	dir	storage directory
 * \param[out]	clue	pool clue
 */
void
ds_pool_clue_init(uuid_t uuid, enum ds_pool_dir dir, struct ds_pool_clue *clue)
{
	char	       *path;
	struct stat	st;
	int		rc;

	memset(clue, 0, sizeof(*clue));
	uuid_copy(clue->pc_uuid, uuid);
	clue->pc_rank = dss_self_rank();
	clue->pc_dir = dir;

	/*
	 * Only glance at pool services in the normal directory for simplicity.
	 */
	if (dir != DS_POOL_DIR_NORMAL) {
		rc = 0;
		goto out;
	}

	path = ds_pool_svc_rdb_path(uuid);
	if (path == NULL) {
		D_ERROR(DF_UUID": failed to allocate RDB path\n", DP_UUID(uuid));
		rc = -DER_NOMEM;
		goto out;
	}

	rc = stat(path, &st);
	if (rc != 0) {
		rc = errno;
		if (rc == ENOENT) {
			/* Not a pool service replica. */
			rc = 0;
		} else {
			D_ERROR(DF_UUID": failed to stat %s: %d\n", DP_UUID(uuid), path, rc);
			rc = daos_errno2der(rc);
		}
		goto out_path;
	}

	D_ALLOC(clue->pc_svc_clue, sizeof(*clue->pc_svc_clue));
	if (clue->pc_svc_clue == NULL) {
		D_ERROR(DF_UUID": failed to allocate service clue\n", DP_UUID(uuid));
		rc = -DER_NOMEM;
		goto out_path;
	}

	rc = pool_svc_glance(uuid, path, clue->pc_svc_clue);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to glance pool service: "DF_RC"\n", DP_UUID(uuid),
			DP_RC(rc));
		D_FREE(clue->pc_svc_clue);
	}

out_path:
	D_FREE(path);
out:
	clue->pc_rc = rc;
}

/**
 * Finalize \a clue that was initialized by ds_pool_clue_init.
 *
 * \param[in,out]	clue	pool clue
 */
void
ds_pool_clue_fini(struct ds_pool_clue *clue)
{
	if (clue->pc_rc == 0 && clue->pc_svc_clue != NULL) {
		d_rank_list_free(clue->pc_svc_clue->psc_db_clue.bcl_replicas);
		D_FREE(clue->pc_svc_clue);
	}
}

/* Argument for glance_at_one */
struct glance_arg {
	ds_pool_clues_init_filter_t	ga_filter;
	void			       *ga_filter_arg;
	enum ds_pool_dir		ga_dir;
	struct ds_pool_clues		ga_clues;
};

static int
glance_at_one(uuid_t uuid, void *varg)
{
	struct glance_arg      *arg = varg;
	struct ds_pool_clues   *clues = &arg->ga_clues;

	if (arg->ga_filter != NULL && arg->ga_filter(uuid, arg->ga_filter_arg) != 0)
		goto out;

	if (clues->pcs_cap < clues->pcs_len + 1) {
		int			new_cap = clues->pcs_cap;
		struct ds_pool_clue    *new_array;

		/* Double the capacity. */
		if (new_cap == 0)
			new_cap = 1;
		else
			new_cap *= 2;
		D_REALLOC_ARRAY(new_array, clues->pcs_array, clues->pcs_cap, new_cap);
		if (new_array == NULL) {
			D_ERROR(DF_UUID": failed to reallocate clues array\n", DP_UUID(uuid));
			goto out;
		}
		clues->pcs_array = new_array;
		clues->pcs_cap = new_cap;
	}

	ds_pool_clue_init(uuid, arg->ga_dir, &clues->pcs_array[clues->pcs_len]);
	clues->pcs_len++;

out:
	/* Always return 0 to continue scanning other pools. */
	return 0;
}

/**
 * Finalize \a clues that was initialized by ds_pool_clues_init.
 *
 * \param[in,out]	clues	pool clues
 */
void
ds_pool_clues_fini(struct ds_pool_clues *clues)
{
	int i;

	for (i = 0; i < clues->pcs_len; i++)
		ds_pool_clue_fini(&clues->pcs_array[i]);
	if (clues->pcs_array != NULL)
		D_FREE(clues->pcs_array);
}

/**
 * Scan local pools and glance at (i.e., call ds_pool_clue_init on) those for
 * which \a filter returns 0. If \a filter is NULL, all local pools will be
 * glanced at. Must be called on the system xstream when all local pools are
 * stopped. If successfully initialized, \a clues must be finalized with
 * ds_pool_clues_fini eventually.
 *
 * \param[in]	filter		optional filter callback
 * \param[in]	filter_arg	optional argument for \a filter
 * \param[out]	clues_out	pool clues
 */
int
ds_pool_clues_init(ds_pool_clues_init_filter_t filter, void *filter_arg,
		   struct ds_pool_clues *clues_out)
{
	struct glance_arg	arg = {0};
	int			rc;

	arg.ga_filter = filter;
	arg.ga_filter_arg = filter_arg;

	arg.ga_dir = DS_POOL_DIR_NORMAL;
	rc = ds_mgmt_tgt_pool_iterate(glance_at_one, &arg);
	if (rc != 0) {
		D_ERROR("failed to glance at local pools: "DF_RC"\n", DP_RC(rc));
		goto err_clues;
	}

	arg.ga_dir = DS_POOL_DIR_NEWBORN;
	rc = ds_mgmt_newborn_pool_iterate(glance_at_one, &arg);
	if (rc != 0) {
		D_ERROR("failed to glance at local new born pools: "DF_RC"\n", DP_RC(rc));
		goto err_clues;
	}

	arg.ga_dir = DS_POOL_DIR_ZOMBIE;
	rc = ds_mgmt_zombie_pool_iterate(glance_at_one, &arg);
	if (rc != 0) {
		D_ERROR("failed to glance at local new born pools: "DF_RC"\n", DP_RC(rc));
		goto err_clues;
	}

	*clues_out = arg.ga_clues;
	return 0;

err_clues:
	ds_pool_clues_fini(&arg.ga_clues);
	return rc;
}

/* For testing purposes... */
void
ds_pool_clues_print(struct ds_pool_clues *clues)
{
	struct ds_pool_svc_clue	svc_clue = {0};
	int			i;

	for (i = 0; i < clues->pcs_len; i++) {
		struct ds_pool_clue	       *c = &clues->pcs_array[i];
		struct ds_pool_svc_clue	       *sc = c->pc_svc_clue == NULL ? &svc_clue :
									      c->pc_svc_clue;
		struct rdb_clue		       *dc = &sc->psc_db_clue;

		D_PRINT("pool clue %d:\n"
			"	uuid		"DF_UUID"\n"
			"	rank		%u\n"
			"	dir		%d\n"
			"	rc		%d\n"
			"	map_version	%u\n"
			"	term		"DF_U64"\n"
			"	vote		%d\n"
			"	self		%u\n"
			"	last_index	"DF_U64"\n"
			"	last_term	"DF_U64"\n"
			"	base_index	"DF_U64"\n"
			"	base_term	"DF_U64"\n"
			"	n_replicas	%u\n"
			"	oid_next	"DF_U64"\n",
			i, DP_UUID(c->pc_uuid), c->pc_rank, c->pc_dir, c->pc_rc,
			sc->psc_map_version, dc->bcl_term, dc->bcl_vote, dc->bcl_self,
			dc->bcl_last_index, dc->bcl_last_term, dc->bcl_base_index,
			dc->bcl_base_term, dc->bcl_replicas == NULL ? 0 : dc->bcl_replicas->rl_nr,
			dc->bcl_oid_next);
	}
}
