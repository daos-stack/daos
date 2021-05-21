/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(pool)

#include "srv_internal.h"
#include <gurt/telemetry_producer.h>

/*
 * Parent directory in metrics tree for all the per-pool metric directories
 */
#define POOL_METRICS_DIR_FMT	"pool/current/%s"

/*
 * Size in bytes of each per-pool metric directory.
 */
#define POOL_METRICS_DIR_BYTES	(8 * 1024)

/**
 * Global pool metrics
 */
struct pool_metrics ds_global_pool_metrics;

/**
 * Per-pool metrics
 */
static d_list_t		per_pool_metrics;
static pthread_mutex_t	per_pool_lock;

struct per_pool_entry {
	struct ds_pool_metrics	metrics;
	d_list_t		link;
};

/**
 * Initializes the pool metrics
 */
int
ds_pool_metrics_init(void)
{
	int rc;

	memset(&ds_global_pool_metrics, 0, sizeof(ds_global_pool_metrics));

	D_INIT_LIST_HEAD(&per_pool_metrics);
	D_MUTEX_INIT(&per_pool_lock, NULL);

	rc = d_tm_add_metric(&ds_global_pool_metrics.open_hdl_gauge, D_TM_GAUGE,
			     "Number of open pool handles", "",
			     "pool/ops/open/active");
	if (rc != 0)
		D_ERROR("Couldn't add open handle gauge: "DF_RC"\n", DP_RC(rc));

	D_DEBUG(DB_TRACE, "Initialized pool metrics\n");
	return rc;
}

static void
per_pool_metrics_lock(void)
{
	int rc = D_MUTEX_LOCK(&per_pool_lock);

	if (unlikely(rc != 0))
		D_ERROR("failed to lock per-pool metrics, "DF_RC"\n",
			DP_RC(rc));
}

static void
per_pool_metrics_unlock(void)
{
	int rc = D_MUTEX_UNLOCK(&per_pool_lock);

	if (unlikely(rc != 0))
		D_ERROR("failed to unlock per-pool metrics, "DF_RC"\n",
			DP_RC(rc));
}

static void
free_per_pool_metrics(struct per_pool_entry *entry)
{
	if (entry == NULL)
		return;

	d_list_del(&entry->link);
	D_MUTEX_DESTROY(&entry->metrics.pm_lock);
	D_FREE(entry);
}

static void
close_pool_metrics_dir(const uuid_t pool_uuid)
{
	char	path[D_TM_MAX_NAME_LEN] = {0};
	int	rc;

	rc = ds_pool_metrics_get_path(pool_uuid, path, sizeof(path));
	if (rc != 0) {
		D_ERROR(DF_UUID ": unable to get pool metrics path, "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		return;
	}

	rc = d_tm_del_ephemeral_dir(path);
	if (rc != 0) {
		D_ERROR(DF_UUID ": unable to remove metrics dir for pool, "
			DF_RC "\n", DP_UUID(pool_uuid), DP_RC(rc));
		return;
	}
}

/**
 * Finalizes the pool metrics
 */
int
ds_pool_metrics_fini(void)
{
	struct per_pool_entry	*cur = NULL;
	struct per_pool_entry	*next = NULL;
	uuid_t			 uuid;

	per_pool_metrics_lock();
	d_list_for_each_entry_safe(cur, next, &per_pool_metrics, link) {
		uuid_copy(uuid, cur->metrics.pm_pool_uuid);
		free_per_pool_metrics(cur);
		close_pool_metrics_dir(uuid);
	}
	per_pool_metrics_unlock();

	D_DEBUG(DB_TRACE, "Finalized pool metrics\n");

	return D_MUTEX_DESTROY(&per_pool_lock);
}

/**
 * Create the metrics path for a specific pool UUID.
 *
 * \param[in]	pool_uuid	UUID of the pool
 * \param[out]	path		Path to the pool metrics
 * \param[in]	path_len	Length of path array
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid inputs
 */
int
ds_pool_metrics_get_path(const uuid_t pool_uuid, char *path, size_t path_len)
{
	char uuid_str[DAOS_UUID_STR_SIZE];

	if (!daos_uuid_valid(pool_uuid)) {
		D_ERROR(DF_UUID ": invalid uuid\n", DP_UUID(pool_uuid));
		return -DER_INVAL;
	}
	uuid_unparse(pool_uuid, uuid_str);

	snprintf(path, path_len, POOL_METRICS_DIR_FMT, uuid_str);
	path[path_len - 1] = '\0';
	return 0;
}

static int
new_per_pool_metrics(const uuid_t pool_uuid, const char *path)
{
	struct per_pool_entry	*entry;
	int			 rc;

	D_ALLOC_PTR(entry);
	if (entry == NULL) {
		D_ERROR(DF_UUID ": failed to allocate metrics struct\n",
			DP_UUID(pool_uuid));
		return -DER_NOMEM;
	}

	D_MUTEX_INIT(&entry->metrics.pm_lock, NULL);
	uuid_copy(entry->metrics.pm_pool_uuid, pool_uuid);

	/* Init all of the per-pool metrics */
	rc = d_tm_add_metric(&entry->metrics.pm_started_timestamp,
			     D_TM_TIMESTAMP,
			     "Last time the pool started", NULL,
			     "%s/started_at", path);
	if (rc != 0) /* Probably a bad sign, but not fatal */
		D_ERROR(DF_UUID ": failed to add started_timestamp metric, "
			DF_RC "\n", DP_UUID(pool_uuid), DP_RC(rc));

	per_pool_metrics_lock();
	d_list_add(&entry->link, &per_pool_metrics);
	per_pool_metrics_unlock();

	return 0;
}

/**
 * Add metrics for a specific pool UUID.
 *
 * \param[in]	pool_uuid	Pool UUID
 */
void
ds_pool_metrics_start(const uuid_t pool_uuid)
{
	struct ds_pool_metrics	*metrics;
	char			 path[D_TM_MAX_NAME_LEN] = {0};
	int			 rc;

	metrics = ds_pool_metrics_get(pool_uuid);
	if (metrics != NULL) /* already exists - nothing to do */
		return;

	rc = ds_pool_metrics_get_path(pool_uuid, path, sizeof(path));
	if (rc != 0) {
		D_ERROR(DF_UUID ": unable to get pool metrics path, "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		return;
	}

	rc = d_tm_add_ephemeral_dir(NULL, POOL_METRICS_DIR_BYTES, path);
	if (rc != 0) {
		D_ERROR(DF_UUID ": unable to create metrics dir for pool, "
			DF_RC "\n", DP_UUID(pool_uuid), DP_RC(rc));
		return;
	}

	rc = new_per_pool_metrics(pool_uuid, path);
	if (rc != 0) {
		D_ERROR(DF_UUID ": unable to start metrics for pool, "
			DF_RC "\n", DP_UUID(pool_uuid), DP_RC(rc));
		return;
	}

	D_INFO(DF_UUID ": created metrics for pool\n", DP_UUID(pool_uuid));
}

static struct per_pool_entry *
get_per_pool_entry(const uuid_t pool_uuid)
{
	struct per_pool_entry *cur = NULL;

	d_list_for_each_entry(cur, &per_pool_metrics, link) {
		if (uuid_compare(pool_uuid, cur->metrics.pm_pool_uuid) == 0)
			return cur;
	}

	return NULL;
}

/**
 * Destroy metrics for a specific pool UUID.
 *
 * \param[in]	pool_uuid	Pool UUID
 */
void
ds_pool_metrics_stop(const uuid_t pool_uuid)
{
	struct per_pool_entry	*entry;

	if (!daos_uuid_valid(pool_uuid)) {
		D_ERROR(DF_UUID ": invalid uuid\n", DP_UUID(pool_uuid));
		return;
	}

	per_pool_metrics_lock();
	entry = get_per_pool_entry(pool_uuid);
	free_per_pool_metrics(entry);
	per_pool_metrics_unlock();

	close_pool_metrics_dir(pool_uuid);

	D_INFO(DF_UUID ": destroyed metrics for pool\n", DP_UUID(pool_uuid));
}

/**
 * Get metrics for a specific active pool.
 *
 * \param[in]	pool_uuid	Pool UUID
 *
 * \return	Pool's metrics structure, or NULL if not found
 */
struct ds_pool_metrics *
ds_pool_metrics_get(const uuid_t pool_uuid)
{
	struct per_pool_entry *result;

	per_pool_metrics_lock();
	result = get_per_pool_entry(pool_uuid);
	per_pool_metrics_unlock();

	if (result == NULL)
		return NULL;

	return &result->metrics;
}

