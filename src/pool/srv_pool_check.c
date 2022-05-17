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

int
ds_pool_clues_find_rank(struct ds_pool_clues *clues, d_rank_t rank)
{
	int i;

	for (i = 0; i < clues->pcs_len; i++)
		if (clues->pcs_array[i].pc_rank == rank)
			return i;
	return -DER_NONEXIST;
}

/*
 * Return
 *
 *   > 0	<x_last_term, x_last_index> is newer than <y_last_term, y_last_index>
 *   < 0	<x_last_term, x_last_index> is older than <y_last_term, y_last_index>
 *   = 0	<x_last_term, x_last_index> is equal to   <y_last_term, y_last_index>
 */
static int
compare_logs(uint64_t x_last_term, uint64_t x_last_index,
	     uint64_t y_last_term, uint64_t y_last_index)
{
	if (x_last_term > y_last_term)
		return 1;
	if (x_last_term < y_last_term)
		return -1;

	if (x_last_index > y_last_index)
		return 1;
	if (x_last_index < y_last_index)
		return -1;

	return 0;
}

/**
 * Analyze \a clues, which must be nonempty and comprise clues about replicas
 * of one PS, and report if this PS requires catastrophic recovery or not.
 *
 * \param[in]	clues		pool clues for one PS
 * \param[out]	advice_out	when the return value is >0, the index of the
 *				advised replica in \a clues to rebootstrap the
 *				PS from
 *
 * \return	0	this PS does not require catastrophic recovery
 *		>0	the caller is advised to rebootstrap this PS from the
 *			replica at index \a *advice_out in \a clues
 */
int
ds_pool_check_svc_clues(struct ds_pool_clues *clues, int *advice_out)
{
	uuid_t		uuid;
	uint32_t	map_version = 0;
	uint64_t	log_term = 0;
	uint64_t	log_index = 0;
	int		advice = -1;
	int		i;

	/* Assert that all clues are about replicas of the same PS. */
	D_ASSERT(clues->pcs_len > 0);
	uuid_copy(uuid, clues->pcs_array[0].pc_uuid);
	for (i = 0; i < clues->pcs_len; i++) {
		struct ds_pool_clue *clue = &clues->pcs_array[i];

		D_ASSERT(uuid_compare(uuid, clue->pc_uuid) == 0);
		D_ASSERTF(clue->pc_rc == 0, DF_RC"\n", DP_RC(clue->pc_rc));
		D_ASSERT(clue->pc_svc_clue != NULL);
	}

	/* For each replica, see if it can get votes from a majority. */
	for (i = 0; i < clues->pcs_len; i++) {
		struct ds_pool_clue	       *clue = &clues->pcs_array[i];
		struct ds_pool_svc_clue	       *svc_clue = clue->pc_svc_clue;
		struct rdb_clue		       *db_clue = &svc_clue->psc_db_clue;
		int				n_votes = 0;
		int				j;

		/* This replica must be a voting node itself. */
		if (!d_rank_list_find(db_clue->bcl_replicas, db_clue->bcl_self, NULL /* idx */))
			continue;

		/*
		 * Check each replica in the local membership and count the
		 * number of votes this replica can get.
		 */
		for (j = 0; j < db_clue->bcl_replicas->rl_nr; j++) {
			struct rdb_clue	       *c;
			int			k;

			/*
			 * Find the member, which may be missing. If it is
			 * ourself, the log comparison will pass.
			 */
			k = ds_pool_clues_find_rank(clues, db_clue->bcl_replicas->rl_ranks[j]);
			if (k < 0)
				continue;
			c = &clues->pcs_array[k].pc_svc_clue->psc_db_clue;

			/*
			 * Since terms will grow as replicas communicate with
			 * each other, we only compare the logs.
			 */
			if (compare_logs(db_clue->bcl_last_term, db_clue->bcl_last_index,
					 c->bcl_last_term, c->bcl_last_index) < 0)
				continue;

			n_votes++;
		}

		D_DEBUG(DB_MD, DF_UUID": rank %u: %d/%u votes\n", DP_UUID(uuid),
			db_clue->bcl_self, n_votes, db_clue->bcl_replicas->rl_nr);

		if (n_votes > db_clue->bcl_replicas->rl_nr / 2)
			return 0;
	}

	/*
	 * No replica can become a leader. Find out which replica among those
	 * who have the newest pool map version has the newest log.
	 */
	for (i = 0; i < clues->pcs_len; i++) {
		struct ds_pool_clue	       *clue = &clues->pcs_array[i];
		struct ds_pool_svc_clue	       *svc_clue = clue->pc_svc_clue;
		struct rdb_clue		       *db_clue = &svc_clue->psc_db_clue;

		/* Track who has the newest pool map version. */
		if (svc_clue->psc_map_version > map_version) {
			map_version = svc_clue->psc_map_version;
			log_term = db_clue->bcl_last_term;
			log_index = db_clue->bcl_last_index;
			advice = i;
		} else if (svc_clue->psc_map_version == map_version) {
			/*
			 * Track who among those with this map version has the
			 * newest log.
			 */
			if (compare_logs(db_clue->bcl_last_term, db_clue->bcl_last_index,
					 log_term, log_index) > 0) {
				log_term = db_clue->bcl_last_term;
				log_index = db_clue->bcl_last_index;
				advice = i;
			}
		}
	}
	D_ASSERTF(advice >= 0 && advice < clues->pcs_len, "%d\n", advice);
	*advice_out = advice;
	return 1;
}

#if 0 /* TODO: Adapt these tests to some new test framework. */
/* Test compare_logs. */
void
ds_pool_test_compare_logs(void)
{
	D_ASSERT(compare_logs(2, 2, 1, 2) > 0);		/* term > */
	D_ASSERT(compare_logs(1, 2, 2, 1) < 0);		/* term < */
	D_ASSERT(compare_logs(1, 4, 1, 3) > 0);		/* term ==, index > */
	D_ASSERT(compare_logs(1, 2, 1, 3) < 0);		/* term ==, index < */
	D_ASSERT(compare_logs(1, 2, 1, 2) == 0);	/* term ==, index == */
}

static d_rank_t			test_ranks[] = {0, 1, 2, 3, 4};
static struct ds_pool_svc_clue	test_svc_clues[ARRAY_SIZE(test_ranks)];
static struct ds_pool_clue	test_clues[ARRAY_SIZE(test_ranks)];

/* Test ds_pool_check_svc_clues. */
void
ds_pool_test_check_svc_clues(void)
{
	uuid_t	uuid;
	int	i;
	int	rc;

	/*
	 * Initialize test data that can't be initialized at compile time.
	 * Every test_svc_clues[i] corresponds to test_clues[i]. Each subtest
	 * must set the following fields.
	 *
	 *   test_svc_clue[i].psc_db_clue.bcl_replicas
	 *   test_svc_clue[i].psc_db_clue.bcl_last_index
	 *   test_svc_clue[i].psc_db_clue.bcl_last_term
	 *   test_svc_clue[i].psc_map_version
	 */
	uuid_generate(uuid);
	for (i = 0; i < ARRAY_SIZE(test_clues); i++) {
		uuid_copy(test_clues[i].pc_uuid, uuid);
		test_clues[i].pc_rank = i;
		test_clues[i].pc_dir = DS_POOL_DIR_NORMAL;
		test_clues[i].pc_svc_clue = &test_svc_clues[i];
	}
	for (i = 0; i < ARRAY_SIZE(test_svc_clues); i++)
		test_svc_clues[i].psc_db_clue.bcl_self = i;

	/* Test a single replica that does not require CR. */
	{
		struct ds_pool_clues	clues = {
			.pcs_array	= test_clues,
			.pcs_len	= 1,
			.pcs_cap	= 1
		};
		d_rank_list_t		replicas = {
			.rl_ranks	= test_ranks,
			.rl_nr		= 1
		};
		int			advice = -1;

		test_svc_clues[0].psc_db_clue.bcl_replicas = &replicas;
		test_svc_clues[0].psc_db_clue.bcl_last_index = 9;
		test_svc_clues[0].psc_db_clue.bcl_last_term = 1;
		test_svc_clues[0].psc_map_version = 1;

		rc = ds_pool_check_svc_clues(&clues, &advice);
		D_ASSERTF(rc == 0, DF_RC"\n", DP_RC(rc));
		D_ASSERTF(advice == -1, "%d\n", advice);
	}

	/*
	 * Test a single replica that has not itself but a missing replica in
	 * the membership and requires CR.
	 */
	{
		struct ds_pool_clues	clues = {
			.pcs_array	= test_clues,
			.pcs_len	= 1,
			.pcs_cap	= 1
		};
		d_rank_list_t		replicas = {
			.rl_ranks	= &test_ranks[1],
			.rl_nr		= 1
		};
		int			advice = -1;

		test_svc_clues[0].psc_db_clue.bcl_replicas = &replicas;
		test_svc_clues[0].psc_db_clue.bcl_last_index = 9;
		test_svc_clues[0].psc_db_clue.bcl_last_term = 1;
		test_svc_clues[0].psc_map_version = 1;

		rc = ds_pool_check_svc_clues(&clues, &advice);
		D_ASSERTF(rc > 0, DF_RC"\n", DP_RC(rc));
		D_ASSERTF(advice == 0, "%d\n", advice);
	}

	/* Test a complete set of replicas that do not require CR. */
	{
		const int		n = 3;
		struct ds_pool_clues	clues = {
			.pcs_array	= test_clues,
			.pcs_len	= n,
			.pcs_cap	= n
		};
		d_rank_list_t		replicas = {
			.rl_ranks	= test_ranks,
			.rl_nr		= n
		};
		int			advice = -1;

		D_ASSERT(n <= ARRAY_SIZE(test_ranks));

		test_svc_clues[0].psc_db_clue.bcl_replicas = &replicas;
		test_svc_clues[0].psc_db_clue.bcl_last_index = 9;
		test_svc_clues[0].psc_db_clue.bcl_last_term = 1;
		test_svc_clues[0].psc_map_version = 1;

		test_svc_clues[1].psc_db_clue.bcl_replicas = &replicas;
		test_svc_clues[1].psc_db_clue.bcl_last_index = 9;
		test_svc_clues[1].psc_db_clue.bcl_last_term = 1;
		test_svc_clues[1].psc_map_version = 1;

		test_svc_clues[2].psc_db_clue.bcl_replicas = &replicas;
		test_svc_clues[2].psc_db_clue.bcl_last_index = 9;
		test_svc_clues[2].psc_db_clue.bcl_last_term = 1;
		test_svc_clues[2].psc_map_version = 1;

		rc = ds_pool_check_svc_clues(&clues, &advice);
		D_ASSERTF(rc == 0, DF_RC"\n", DP_RC(rc));
		D_ASSERTF(advice == -1, "%d\n", advice);
	}

	/* Test an incomplete but sufficient set of replicas that do not require CR. */
	{
		const int		m = 2;
		const int		n = 3;
		struct ds_pool_clues	clues = {
			.pcs_array	= test_clues,
			.pcs_len	= m,
			.pcs_cap	= m
		};
		d_rank_list_t		replicas = {
			.rl_ranks	= test_ranks,
			.rl_nr		= n
		};
		int			advice = -1;

		D_ASSERT(m <= ARRAY_SIZE(test_ranks));
		D_ASSERT(n <= ARRAY_SIZE(test_ranks));
		D_ASSERT(m < n);

		test_svc_clues[0].psc_db_clue.bcl_replicas = &replicas;
		test_svc_clues[0].psc_db_clue.bcl_last_index = 9;
		test_svc_clues[0].psc_db_clue.bcl_last_term = 1;
		test_svc_clues[0].psc_map_version = 1;

		test_svc_clues[1].psc_db_clue.bcl_replicas = &replicas;
		test_svc_clues[1].psc_db_clue.bcl_last_index = 9;
		test_svc_clues[1].psc_db_clue.bcl_last_term = 1;
		test_svc_clues[1].psc_map_version = 1;

		rc = ds_pool_check_svc_clues(&clues, &advice);
		D_ASSERTF(rc == 0, DF_RC"\n", DP_RC(rc));
		D_ASSERTF(advice == -1, "%d\n", advice);
	}

	/*
	 * Test a complete (for at least one replica) but insufficient set of
	 * replicas that require CR.
	 */
	{
		const int		n = 3;
		struct ds_pool_clues	clues = {
			.pcs_array	= test_clues,
			.pcs_len	= n,
			.pcs_cap	= n
		};
		d_rank_list_t		replicas_0;
		d_rank_list_t		replicas_1;
		d_rank_list_t		replicas_2;
		int			advice = -1;

		D_ASSERT(n <= ARRAY_SIZE(test_ranks));

		/* Unable to get votes from {1, 2} in {0, 1, 2}. */
		replicas_0.rl_ranks = test_ranks;
		replicas_0.rl_nr = n;
		test_svc_clues[0].psc_db_clue.bcl_replicas = &replicas_0;
		test_svc_clues[0].psc_db_clue.bcl_last_index = 9;
		test_svc_clues[0].psc_db_clue.bcl_last_term = 1;
		test_svc_clues[0].psc_map_version = 1;

		/* Unable to get votes from {2} in {1, 2}. */
		replicas_1.rl_ranks = &test_ranks[1];
		replicas_1.rl_nr = n - 1;
		test_svc_clues[1].psc_db_clue.bcl_replicas = &replicas_1;
		test_svc_clues[1].psc_db_clue.bcl_last_index = 10;
		test_svc_clues[1].psc_db_clue.bcl_last_term = 1;
		test_svc_clues[1].psc_map_version = 1;

		/* Unable to get votes from absent {3} in {2, 3}. */
		D_ASSERT(ARRAY_SIZE(test_ranks) >= 4);
		replicas_2.rl_ranks = &test_ranks[2];
		replicas_2.rl_nr = 2;
		test_svc_clues[2].psc_db_clue.bcl_replicas = &replicas_2;
		test_svc_clues[2].psc_db_clue.bcl_last_index = 11;
		test_svc_clues[2].psc_db_clue.bcl_last_term = 1;
		test_svc_clues[2].psc_map_version = 1;

		rc = ds_pool_check_svc_clues(&clues, &advice);
		D_ASSERTF(rc > 0, DF_RC"\n", DP_RC(rc));
		D_ASSERTF(advice == 2, "%d\n", advice);
	}

	/* Test an insufficient set of replicas that require CR: case 1. */
	{
		const int		m = 2;
		const int		n = 5;
		struct ds_pool_clues	clues = {
			.pcs_array	= test_clues,
			.pcs_len	= m,
			.pcs_cap	= m
		};
		d_rank_list_t		replicas = {
			.rl_ranks	= test_ranks,
			.rl_nr		= n
		};
		int			advice = -1;

		D_ASSERT(m <= ARRAY_SIZE(test_ranks));
		D_ASSERT(n <= ARRAY_SIZE(test_ranks));
		D_ASSERT(m < n);

		test_svc_clues[0].psc_db_clue.bcl_replicas = &replicas;
		test_svc_clues[0].psc_db_clue.bcl_last_index = 9;
		test_svc_clues[0].psc_db_clue.bcl_last_term = 1;
		test_svc_clues[0].psc_map_version = 1;

		/* A newer map version and a newer log. */
		test_svc_clues[1].psc_db_clue.bcl_replicas = &replicas;
		test_svc_clues[1].psc_db_clue.bcl_last_index = 11;
		test_svc_clues[1].psc_db_clue.bcl_last_term = 1;
		test_svc_clues[1].psc_map_version = 2;

		rc = ds_pool_check_svc_clues(&clues, &advice);
		D_ASSERTF(rc > 0, DF_RC"\n", DP_RC(rc));
		D_ASSERTF(advice == 1, "%d\n", advice);
	}

	/* Test an insufficient set of replicas that require CR: case 2. */
	{
		const int		m = 2;
		const int		n = 5;
		struct ds_pool_clues	clues = {
			.pcs_array	= test_clues,
			.pcs_len	= m,
			.pcs_cap	= m
		};
		d_rank_list_t		replicas = {
			.rl_ranks	= test_ranks,
			.rl_nr		= n
		};
		int			advice = -1;

		D_ASSERT(m <= ARRAY_SIZE(test_ranks));
		D_ASSERT(n <= ARRAY_SIZE(test_ranks));
		D_ASSERT(m < n);

		/* A newer map version. */
		test_svc_clues[0].psc_db_clue.bcl_replicas = &replicas;
		test_svc_clues[0].psc_db_clue.bcl_last_index = 11;
		test_svc_clues[0].psc_db_clue.bcl_last_term = 1;
		test_svc_clues[0].psc_map_version = 2;

		/* A newer log. */
		test_svc_clues[1].psc_db_clue.bcl_replicas = &replicas;
		test_svc_clues[1].psc_db_clue.bcl_last_index = 10;
		test_svc_clues[1].psc_db_clue.bcl_last_term = 2;
		test_svc_clues[1].psc_map_version = 1;

		rc = ds_pool_check_svc_clues(&clues, &advice);
		D_ASSERTF(rc > 0, DF_RC"\n", DP_RC(rc));
		D_ASSERTF(advice == 0, "%d\n", advice);
	}

	/* Test an insufficient set of replicas that require CR: case 3. */
	{
		const int		m = 2;
		const int		n = 5;
		struct ds_pool_clues	clues = {
			.pcs_array	= test_clues,
			.pcs_len	= m,
			.pcs_cap	= m
		};
		d_rank_list_t		replicas = {
			.rl_ranks	= test_ranks,
			.rl_nr		= n
		};
		int			advice = -1;

		D_ASSERT(m <= ARRAY_SIZE(test_ranks));
		D_ASSERT(n <= ARRAY_SIZE(test_ranks));
		D_ASSERT(m < n);

		test_svc_clues[0].psc_db_clue.bcl_replicas = &replicas;
		test_svc_clues[0].psc_db_clue.bcl_last_index = 11;
		test_svc_clues[0].psc_db_clue.bcl_last_term = 1;
		test_svc_clues[0].psc_map_version = 2;

		/* The same map version but a newer log. */
		test_svc_clues[1].psc_db_clue.bcl_replicas = &replicas;
		test_svc_clues[1].psc_db_clue.bcl_last_index = 10;
		test_svc_clues[1].psc_db_clue.bcl_last_term = 2;
		test_svc_clues[1].psc_map_version = 2;

		rc = ds_pool_check_svc_clues(&clues, &advice);
		D_ASSERTF(rc > 0, DF_RC"\n", DP_RC(rc));
		D_ASSERTF(advice == 1, "%d\n", advice);
	}
}
#endif
