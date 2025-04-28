/*
 * (C) Copyright 2016-2025 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * ds_pool: Target Operations
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related target states.
 *
 * Data structures used here:
 *
 *                 Pool           Container
 *
 *         Global  ds_pool
 *                 ds_pool_hdl
 *
 *   Thread-local  ds_pool_child  ds_cont
 *                                ds_cont_hdl
 */

#define D_LOGFAC	DD_FAC(pool)

#include <daos_srv/pool.h>

#include <sys/stat.h>
#include <gurt/telemetry_producer.h>
#include <daos/pool_map.h>
#include <daos/rpc.h>
#include <daos/pool.h>
#include <daos_srv/container.h>
#include <daos_srv/daos_mgmt_srv.h>
#include <daos_srv/object.h>
#include <daos_srv/vos.h>
#include <daos_srv/rebuild.h>
#include <daos_srv/srv_csum.h>
#include "rpc.h"
#include "srv_internal.h"

/* ds_pool_child **************************************************************/

static void
stop_gc_ult(struct ds_pool_child *child)
{
	D_ASSERT(child != NULL);
	/* GC ULT is not started */
	if (child->spc_gc_req == NULL)
		return;

	D_DEBUG(DB_MGMT, DF_UUID"[%d]: Stopping GC ULT\n",
		DP_UUID(child->spc_uuid), dss_get_module_info()->dmi_tgt_id);

	sched_req_wait(child->spc_gc_req, true);
	sched_req_put(child->spc_gc_req);
	child->spc_gc_req = NULL;
}

static void
stop_flush_ult(struct ds_pool_child *child)
{
	D_ASSERT(child != NULL);
	/* Flush ULT is not started */
	if (child->spc_flush_req == NULL)
		return;

	D_DEBUG(DB_MGMT, DF_UUID"[%d]: Stopping Flush ULT\n",
		DP_UUID(child->spc_uuid), dss_get_module_info()->dmi_tgt_id);

	sched_req_wait(child->spc_flush_req, true);
	sched_req_put(child->spc_flush_req);
	child->spc_flush_req = NULL;
}

static struct ds_pool_child *
pool_child_lookup_noref(const uuid_t uuid)
{
	struct ds_pool_child   *child;
	struct pool_tls	       *tls = pool_tls_get();

	d_list_for_each_entry(child, &tls->dt_pool_list, spc_list) {
		if (uuid_compare(uuid, child->spc_uuid) == 0) {
			return child;
		}
	}
	return NULL;
}

struct ds_pool_child *
ds_pool_child_find(const uuid_t uuid)
{
	struct ds_pool_child	*child;

	child = pool_child_lookup_noref(uuid);
	if (child == NULL) {
		D_ERROR(DF_UUID": Pool child isn't found.\n", DP_UUID(uuid));
		return child;
	}

	child->spc_ref++;
	return child;
}

struct ds_pool_child *
ds_pool_child_lookup(const uuid_t uuid)
{
	struct ds_pool_child	*child;

	child = pool_child_lookup_noref(uuid);
	if (child == NULL) {
		D_ERROR(DF_UUID": Pool child isn't found.\n", DP_UUID(uuid));
		return child;
	}

	if (*child->spc_state == POOL_CHILD_NEW || *child->spc_state == POOL_CHILD_STOPPING) {
		D_ERROR(DF_UUID": Pool child isn't ready. (%u)\n",
			DP_UUID(uuid), *child->spc_state);
		return NULL;
	}

	child->spc_ref++;
	return child;
}

void
ds_pool_child_put(struct ds_pool_child *child)
{
	D_ASSERTF(child->spc_ref > 0, "%d\n", child->spc_ref);
	child->spc_ref--;
	if (child->spc_ref == 0 && *child->spc_state == POOL_CHILD_STOPPING)
		ABT_eventual_set(child->spc_ref_eventual, (void *)&child->spc_ref,
				 sizeof(child->spc_ref));
}

static int
gc_rate_ctl(void *arg)
{
	struct ds_pool_child	*child = (struct ds_pool_child *)arg;
	struct ds_pool		*pool = child->spc_pool;
	struct sched_request	*req = child->spc_gc_req;
	uint32_t		 msecs;

	if (dss_ult_exiting(req))
		return -1;

	/* Let GC ULT run in tight mode when system is idle or under space pressure */
	if (!dss_xstream_is_busy() || sched_req_space_check(req) != SCHED_SPACE_PRESS_NONE) {
		sched_req_yield(req);
		return 0;
	}

	msecs = (pool->sp_reclaim == DAOS_RECLAIM_LAZY ||
			pool->sp_reclaim == DAOS_RECLAIM_DISABLED) ? 1000 : 50;
	sched_req_sleep(req, msecs);
	/* Let GC ULT run in slack mode when system is busy and no space pressure */
	return 1;
}

static void
gc_ult(void *arg)
{
	struct ds_pool_child	*child = (struct ds_pool_child *)arg;
	struct dss_module_info	*dmi = dss_get_module_info();
	int			 rc;

	D_DEBUG(DB_MGMT, DF_UUID"[%d]: GC ULT started\n",
		DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);

	if (child->spc_gc_req == NULL)
		goto out;

	while (!dss_ult_exiting(child->spc_gc_req)) {
		rc = vos_gc_pool(child->spc_hdl, -1, gc_rate_ctl, (void *)child);
		if (rc < 0)
			D_ERROR(DF_UUID"[%d]: GC pool run failed. "DF_RC"\n",
				DP_UUID(child->spc_uuid), dmi->dmi_tgt_id,
				DP_RC(rc));

		if (dss_ult_exiting(child->spc_gc_req))
			break;

		/* It'll be woke up by container destroy or aggregation */
		if (rc > 0)
			sched_req_yield(child->spc_gc_req);
		else
			sched_req_sleep(child->spc_gc_req, 10UL * 1000);
	}
out:
	D_DEBUG(DB_MGMT, DF_UUID"[%d]: GC ULT stopped\n",
		DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);
}

static int
start_gc_ult(struct ds_pool_child *child)
{
	struct dss_module_info	*dmi = dss_get_module_info();
	struct sched_req_attr	 attr;

	D_ASSERT(child != NULL);
	D_ASSERT(child->spc_gc_req == NULL);

	D_DEBUG(DB_MGMT, DF_UUID"[%d]: starting GC ULT\n",
		DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);

	sched_req_attr_init(&attr, SCHED_REQ_GC, &child->spc_uuid);
	attr.sra_flags = SCHED_REQ_FL_NO_DELAY;

	child->spc_gc_req = sched_create_ult(&attr, gc_ult, child, DSS_DEEP_STACK_SZ);
	if (child->spc_gc_req == NULL) {
		D_ERROR(DF_UUID"[%d]: Failed to create GC ULT.\n",
			DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);
		return -DER_NOMEM;
	}

	return 0;
}

static void
flush_ult(void *arg)
{
	struct ds_pool_child	*child = (struct ds_pool_child *)arg;
	struct dss_module_info	*dmi = dss_get_module_info();
	uint32_t		 sleep_ms, nr_flushed, nr_flush = 6000;
	int			 rc;

	D_DEBUG(DB_MGMT, DF_UUID"[%d]: Flush ULT started\n",
		DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);

	D_ASSERT(child->spc_flush_req != NULL);

	while (!dss_ult_exiting(child->spc_flush_req)) {
		rc = vos_flush_pool(child->spc_hdl, nr_flush, &nr_flushed);
		if (rc < 0) {
			D_ERROR(DF_UUID"[%d]: Flush pool failed. "DF_RC"\n",
				DP_UUID(child->spc_uuid), dmi->dmi_tgt_id, DP_RC(rc));
			sleep_ms = 2000;
		} else if (rc) {	/* This pool doesn't have NVMe partition */
			sleep_ms = 60000;
		} else if (sched_req_space_check(child->spc_flush_req) == SCHED_SPACE_PRESS_NONE) {
			sleep_ms = 500;
		} else {
			sleep_ms = (nr_flushed < nr_flush) ? 50 : 0;
		}

		if (dss_ult_exiting(child->spc_flush_req))
			break;

		if (sleep_ms)
			sched_req_sleep(child->spc_flush_req, sleep_ms);
		else
			sched_req_yield(child->spc_flush_req);
	}

	D_DEBUG(DB_MGMT, DF_UUID"[%d]: Flush ULT stopped\n",
		DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);
}

static int
start_flush_ult(struct ds_pool_child *child)
{
	struct dss_module_info	*dmi = dss_get_module_info();
	struct sched_req_attr	 attr;

	D_ASSERT(child != NULL);
	D_ASSERT(child->spc_flush_req == NULL);

	sched_req_attr_init(&attr, SCHED_REQ_GC, &child->spc_uuid);
	attr.sra_flags = SCHED_REQ_FL_NO_DELAY;

	child->spc_flush_req = sched_create_ult(&attr, flush_ult, child, DSS_DEEP_STACK_SZ);
	if (child->spc_flush_req == NULL) {
		D_ERROR(DF_UUID"[%d]: Failed to create flush ULT.\n",
			DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);
		return -DER_NOMEM;
	}

	return 0;
}

/* This query API could be called from any xstream */
uint32_t
ds_pool_child_state(uuid_t pool_uuid, uint32_t tgt_id)
{
	struct dss_module_info	*info = dss_get_module_info();
	struct ds_pool_child	*child;

	D_ASSERT(info->dmi_tgt_id < dss_tgt_nr);
	child = pool_child_lookup_noref(pool_uuid);
	if (child == NULL) {
		D_ERROR(DF_UUID": Pool child isn't found.\n", DP_UUID(pool_uuid));
		return POOL_CHILD_NEW;
	}

	D_ASSERT(child->spc_pool != NULL);
	return child->spc_pool->sp_states[info->dmi_tgt_id];
}

static void
pool_child_free(struct ds_pool_child *child)
{
	D_ASSERTF(*child->spc_state == POOL_CHILD_NEW, "state:%u", *child->spc_state);
	D_ASSERT(child->spc_ref == 0);

	/* Remove from cache */
	d_list_del_init(&child->spc_list);
	dss_module_fini_metrics(DAOS_TGT_TAG, child->spc_metrics);
	ABT_eventual_free(&child->spc_ref_eventual);
	D_FREE(child);
}

static struct ds_pool_child *
pool_child_create(uuid_t pool_uuid, struct ds_pool *pool, uint32_t pool_map_ver)
{
	struct dss_module_info	*info = dss_get_module_info();
	struct pool_tls		*tls = pool_tls_get();
	struct ds_pool_child	*child;
	int			 rc;

	D_DEBUG(DB_MGMT, DF_UUID": Create pool child\n", DP_UUID(pool_uuid));

	D_ALLOC_PTR(child);
	if (child == NULL)
		return NULL;

	/* initialize metrics on the target xstream for each module */
	rc = dss_module_init_metrics(DAOS_TGT_TAG, child->spc_metrics, pool->sp_path,
				     info->dmi_tgt_id);
	if (rc != 0) {
		D_ERROR(DF_UUID ": failed to initialize module metrics for pool"
			"." DF_RC "\n", DP_UUID(pool_uuid), DP_RC(rc));
		goto out_free;
	}

	rc = ABT_eventual_create(sizeof(child->spc_ref), &child->spc_ref_eventual);
	if (rc != ABT_SUCCESS)
		goto out_metrics;

	uuid_copy(child->spc_uuid, pool_uuid);
	child->spc_map_version = pool_map_ver;
	child->spc_pool = pool;
	D_INIT_LIST_HEAD(&child->spc_list);
	D_INIT_LIST_HEAD(&child->spc_cont_list);

	D_ASSERT(info->dmi_tgt_id < dss_tgt_nr);
	child->spc_state = &pool->sp_states[info->dmi_tgt_id];
	*child->spc_state = POOL_CHILD_NEW;

	/* Add to cache */
	d_list_add(&child->spc_list, &tls->dt_pool_list);

	return child;

out_metrics:
	dss_module_fini_metrics(DAOS_TGT_TAG, child->spc_metrics);
out_free:
	D_FREE(child);
	return NULL;
}

/*
 * Since PMEM_PAGESIZE and PMEM_OBJ_POOL_HEAD_SIZE are not exported,
 * for safety, we will use 64k for now.
 */
#define	POOL_OBJ_HEADER_SIZE	65536

static int
pool_reset_header(const char *path)
{
	int	 rc;
	char	*buf = NULL;
	FILE	*file;
	int	 header_size = 65536;

	/* Skip MD-ON-SSD case */
	if (bio_nvme_configured(SMD_DEV_TYPE_META))
		return 0;

	file = fopen(path, "r+");
	if (file == NULL) {
		D_ERROR("failed to open %s\n", path);
		return daos_errno2der(errno);
	}

	D_ALLOC(buf, header_size);
	if (buf == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	rc = fwrite(buf, header_size, 1, file);
	if (rc < 0)
		goto out;
	else if (rc != 1)
		rc = -DER_IO;
	else
		rc = 0;
out:
	D_FREE(buf);
	fclose(file);
	return rc;
}

static int
pool_child_recreate(struct ds_pool_child *child)
{
	struct dss_module_info	*info = dss_get_module_info();
	struct smd_pool_info	*pool_info;
	struct stat		 lstat;
	uint32_t		 vos_df_version;
	char			*path;
	int			 rc;

	vos_df_version = ds_pool_get_vos_df_version(child->spc_pool->sp_global_version);
	if (vos_df_version == 0) {
		rc = -DER_NO_PERM;
		DL_ERROR(rc, DF_UUID ": pool global version %u not supported",
			 DP_UUID(child->spc_uuid), child->spc_pool->sp_global_version);
		return rc;
	}

	rc = ds_mgmt_tgt_file(child->spc_uuid, VOS_FILE, &info->dmi_tgt_id, &path);
	if (rc != 0)
		return rc;

	rc = stat(path, &lstat);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID": Stat VOS pool file failed.", DP_UUID(child->spc_uuid));
		rc = daos_errno2der(errno);
		goto out;
	}
	if (lstat.st_size == 0) {
		D_ERROR(DF_UUID": VOS pool file isn't fallocated.", DP_UUID(child->spc_uuid));
		goto out;
	}

	rc = smd_pool_get_info(child->spc_uuid, &pool_info);
	if (rc) {
		DL_ERROR(rc, DF_UUID": Get pool info failed.", DP_UUID(child->spc_uuid));
		goto out;
	}

	rc = vos_pool_kill(child->spc_uuid, 0);
	if (rc) {
		DL_ERROR(rc, DF_UUID": Destroy VOS pool failed.", DP_UUID(child->spc_uuid));
		goto pool_info;
	}

	/* If pool header is nonzero, pmemobj_create() will fail.
	 * Therefore, reset the header in this case.
	 */
	rc = pool_reset_header(path);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID": Reset VOS pool header failed.", DP_UUID(child->spc_uuid));
		goto pool_info;
	}

	rc = vos_pool_create(path, child->spc_uuid, 0 /* scm_sz */,
			     pool_info->spi_blob_sz[SMD_DEV_TYPE_DATA],
			     pool_info->spi_blob_sz[SMD_DEV_TYPE_META],
			     VOS_POF_FOR_RECREATE /* flags */, vos_df_version, NULL);
	if (rc)
		DL_ERROR(rc, DF_UUID": Create VOS pool failed.", DP_UUID(child->spc_uuid));

pool_info:
	smd_pool_free_info(pool_info);
out:
	D_FREE(path);
	return rc;

}


static int
pool_child_start(struct ds_pool_child *child, bool recreate)
{
	struct dss_module_info	*info = dss_get_module_info();
	char			*path;
	int			 rc;

	D_ASSERTF(*child->spc_state == POOL_CHILD_NEW, "state:%u", *child->spc_state);
	D_ASSERT(!d_list_empty(&child->spc_list));

	*child->spc_state = POOL_CHILD_STARTING;

	if (recreate) {
		rc = pool_child_recreate(child);
		if (rc != 0)
			goto out;
	}

	rc = ds_mgmt_tgt_file(child->spc_uuid, VOS_FILE, &info->dmi_tgt_id, &path);
	if (rc != 0)
		goto out;

	D_ASSERT(child->spc_metrics[DAOS_VOS_MODULE] != NULL);
	rc = vos_pool_open_metrics(path, child->spc_uuid, VOS_POF_EXCL | VOS_POF_EXTERNAL_FLUSH,
				   child->spc_metrics[DAOS_VOS_MODULE], &child->spc_hdl);

	D_FREE(path);

	if (rc != 0) {
		if (rc != -DER_NONEXIST) {
			DL_CDEBUG(rc == -DER_NVME_IO, DB_MGMT, DLOG_ERR, rc,
				  DF_UUID": Open VOS pool failed.", DP_UUID(child->spc_uuid));
			goto out;
		}

		D_WARN("Lost pool "DF_UUIDF" shard %u on rank %u.\n",
		       DP_UUID(child->spc_uuid), info->dmi_tgt_id, dss_self_rank());
		/*
		 * Ignore the failure to allow subsequent logic (such as DAOS check)
		 * to handle the trouble.
		 */
		child->spc_no_storage = 1;
		goto done;
	}

	if (!ds_pool_skip_for_check(child->spc_pool) &&
	    vos_pool_feature_skip_start(child->spc_hdl)) {
		D_INFO(DF_UUID ": skipped to start\n", DP_UUID(child->spc_uuid));
		rc = -DER_SHUTDOWN;
		goto out_close;
	}

	if (vos_pool_feature_immutable(child->spc_hdl))
		child->spc_pool->sp_immutable = 1;

	/*
	 * Rebuild depends on DTX resync, if DTX resync is skipped,
	 * then rebuild also needs to be skipped.
	 */
	if (vos_pool_feature_skip_rebuild(child->spc_hdl) ||
	    vos_pool_feature_skip_dtx_resync(child->spc_hdl))
		child->spc_pool->sp_disable_rebuild = 1;

	if (vos_pool_feature_skip_dtx_resync(child->spc_hdl))
		child->spc_pool->sp_disable_dtx_resync = 1;

	if (!ds_pool_restricted(child->spc_pool, false)) {
		rc = start_gc_ult(child);
		if (rc != 0)
			goto out_close;

		rc = start_flush_ult(child);
		if (rc != 0)
			goto out_gc;

		rc = ds_start_scrubbing_ult(child);
		if (rc != 0)
			goto out_flush;
	}

	rc = ds_start_chkpt_ult(child);
	if (rc != 0)
		goto out_scrub;

	/* Start all containers */
	rc = ds_cont_child_start_all(child);
	if (rc)
		goto out_cont;

done:
	*child->spc_state = POOL_CHILD_STARTED;
	return 0;

out_cont:
	ds_cont_child_stop_all(child);
	ds_stop_chkpt_ult(child);
out_scrub:
	ds_stop_scrubbing_ult(child);
out_flush:
	stop_flush_ult(child);
out_gc:
	stop_gc_ult(child);
out_close:
	if (likely(!child->spc_no_storage))
		vos_pool_close(child->spc_hdl);
out:
	*child->spc_state = POOL_CHILD_NEW;
	return rc;
}

int
ds_pool_child_start(uuid_t pool_uuid, bool recreate)
{
	struct ds_pool_child	*child;
	int			 rc;

	child = pool_child_lookup_noref(pool_uuid);
	if (child == NULL) {
		D_ERROR(DF_UUID": Pool child not found.\n", DP_UUID(pool_uuid));
		return -DER_NONEXIST;
	}

	if (*child->spc_state == POOL_CHILD_STOPPING) {
		D_ERROR(DF_UUID": Pool is in stopping.\n", DP_UUID(pool_uuid));
		return -DER_BUSY;
	} else if (*child->spc_state == POOL_CHILD_STARTING) {
		D_DEBUG(DB_MGMT, DF_UUID": Pool is already in starting.\n", DP_UUID(pool_uuid));
		return 1;
	} else if (*child->spc_state == POOL_CHILD_STARTED) {
		D_DEBUG(DB_MGMT, DF_UUID": Pool is already started.\n", DP_UUID(pool_uuid));
		return 0;
	}

	rc = pool_child_start(child, recreate);
	if (rc)
		DL_ERROR(rc, DF_UUID": Pool start failed.", DP_UUID(pool_uuid));

	return rc;
}

static int
pool_child_stop(struct ds_pool_child *child)
{
	int	*ref, rc;

	D_ASSERT(!d_list_empty(&child->spc_list));

	if (*child->spc_state == POOL_CHILD_STARTING) {
		D_ERROR(DF_UUID": Pool is in starting.\n", DP_UUID(child->spc_uuid));
		return -DER_BUSY;
	} else if (*child->spc_state == POOL_CHILD_STOPPING) {
		D_DEBUG(DB_MGMT, DF_UUID": Pool is already in stopping.\n",
			DP_UUID(child->spc_uuid));
		return 1;
	} else if (*child->spc_state == POOL_CHILD_NEW) {
		D_DEBUG(DB_MGMT, DF_UUID": Pool isn't started.\n", DP_UUID(child->spc_uuid));
		return 0;
	}

	D_DEBUG(DB_MGMT, DF_UUID": Stopping pool child.\n", DP_UUID(child->spc_uuid));

	*child->spc_state = POOL_CHILD_STOPPING;

	if (unlikely(child->spc_no_storage))
		goto wait;

	/* First stop all the ULTs who might need to hold ds_pool_child (or ds_cont_child) */
	ds_cont_child_stop_all(child);
	D_ASSERT(d_list_empty(&child->spc_cont_list));
	ds_stop_scrubbing_ult(child);

wait:
	/* Wait for all references dropped */
	if (child->spc_ref > 0) {
		D_DEBUG(DB_MGMT, DF_UUID": Wait on pool child refs (%d) dropping.\n",
			DP_UUID(child->spc_uuid), child->spc_ref);
		rc = ABT_eventual_wait(child->spc_ref_eventual, (void **)&ref);
		D_ASSERT(rc == ABT_SUCCESS);
		ABT_eventual_reset(child->spc_ref_eventual);
	}
	D_DEBUG(DB_MGMT, DF_UUID": Pool child refs dropped.\n", DP_UUID(child->spc_uuid));

	if (unlikely(child->spc_no_storage))
		goto done;

	/* Stop all pool child owned ULTs which doesn't hold ds_pool_child reference */
	ds_stop_chkpt_ult(child);
	D_DEBUG(DB_MGMT, DF_UUID": Checkpoint ULT stopped.\n", DP_UUID(child->spc_uuid));

	stop_gc_ult(child);
	stop_flush_ult(child);

	/* Close VOS pool at the end */
	vos_pool_close(child->spc_hdl);
	child->spc_hdl = DAOS_HDL_INVAL;

done:
	D_DEBUG(DB_MGMT, DF_UUID": Pool child stopped.\n", DP_UUID(child->spc_uuid));
	*child->spc_state = POOL_CHILD_NEW;
	return 0;
}

int
ds_pool_child_stop(uuid_t pool_uuid, bool free)
{
	struct ds_pool_child	*child;
	int			 rc;

	child = pool_child_lookup_noref(pool_uuid);
	if (child == NULL) {
		D_ERROR(DF_UUID": Pool child not found.\n", DP_UUID(pool_uuid));
		return -DER_NONEXIST;
	}

	rc = pool_child_stop(child);
	if (rc == 0 && free)
		pool_child_free(child);

	return rc;
}

struct pool_child_lookup_arg {
	struct ds_pool *pla_pool;
	void	       *pla_uuid;
	uint32_t	pla_map_version;
};

/*
 * Called via dss_thread_collective() to create and add the ds_pool_child object
 * for one thread. This opens the matching VOS pool.
 */
static int
pool_child_add_one(void *varg)
{
	struct pool_child_lookup_arg	*arg = varg;
	struct ds_pool_child		*child;
	int				 rc;

	child = pool_child_lookup_noref(arg->pla_uuid);
	if (child != NULL)
		return 0;

	child = pool_child_create(arg->pla_uuid, arg->pla_pool, arg->pla_map_version);
	if (child == NULL) {
		D_ERROR(DF_UUID ": Create pool child failed.\n", DP_UUID(child->spc_uuid));
		return -DER_NOMEM;
	}

	rc = pool_child_start(child, false);
	if (rc) {
		DL_CDEBUG(rc == -DER_NVME_IO, DLOG_WARN, DLOG_ERR, rc,
			  DF_UUID": Pool start failed.", DP_UUID(child->spc_uuid));
		if (rc == -DER_NVME_IO)
			rc = 0;
		pool_child_free(child);
	}

	return rc;
}

/*
 * Called via dss_thread_collective() to delete the ds_pool_child object for one
 * thread. If nobody else is referencing this object, then its VOS pool handle
 * is closed and the object itself is freed.
 */
static int
pool_child_delete_one(void *uuid)
{
	struct ds_pool_child	*child;
	int			 rc, retry_cnt = 0;

	child = pool_child_lookup_noref(uuid);
	if (child == NULL)
		return 0;
retry:
	rc = pool_child_stop(child);
	if (rc) {
		D_ASSERT(rc == 1 || rc == -DER_BUSY);
		/* Rare race case, simply retry */
		retry_cnt++;
		if (retry_cnt % 10 == 0)
			D_WARN(DF_UUID": Pool stop race (%d). retry:%d\n",
			       DP_UUID(uuid), rc, retry_cnt);
		dss_sleep(500);
		goto retry;
	}
	pool_child_free(child);

	return 0;
}

static void
pool_child_delete_all(struct ds_pool *pool)
{
	int rc;

	rc = ds_pool_thread_collective(pool->sp_uuid, 0, pool_child_delete_one, pool->sp_uuid, 0);
	if (rc == 0)
		D_INFO(DF_UUID ": deleted\n", DP_UUID(pool->sp_uuid));
	else if (rc == -DER_CANCELED)
		D_INFO(DF_UUID ": no ESs\n", DP_UUID(pool->sp_uuid));
	else
		DL_ERROR(rc, DF_UUID ": failed to delete ES pool caches", DP_UUID(pool->sp_uuid));
}

static int
pool_child_add_all(struct ds_pool *pool)
{
	struct pool_child_lookup_arg collective_arg = {
		.pla_pool		= pool,
		.pla_uuid		= pool->sp_uuid,
		.pla_map_version	= pool->sp_map_version
	};
	int rc;

	rc = ds_pool_thread_collective(pool->sp_uuid, 0, pool_child_add_one, &collective_arg,
				       DSS_ULT_DEEP_STACK);
	if (rc == 0) {
		D_INFO(DF_UUID ": added\n", DP_UUID(pool->sp_uuid));
	} else {
		DL_ERROR(rc, DF_UUID ": failed to add ES pool caches", DP_UUID(pool->sp_uuid));
		pool_child_delete_all(pool);
		return rc;
	}
	return 0;
}

/* ds_pool ********************************************************************/

static struct daos_lru_cache   *pool_cache;

static inline struct ds_pool *
pool_obj(struct daos_llink *llink)
{
	return container_of(llink, struct ds_pool, sp_entry);
}

struct ds_pool_create_arg {
	uint32_t	pca_map_version;
};

static int
pool_alloc_ref(void *key, unsigned int ksize, void *varg,
	       struct daos_llink **link)
{
	struct ds_pool_create_arg      *arg = varg;
	struct ds_pool		       *pool;
	char				group_id[DAOS_UUID_STR_SIZE];
	struct dss_module_info	       *info = dss_get_module_info();
	unsigned int			iv_ns_id;
	int				rc;
	int				rc_tmp;

	if (arg == NULL) {
		/* The caller doesn't want to create a ds_pool object. */
		rc = -DER_NONEXIST;
		goto err;
	}

	D_DEBUG(DB_MGMT, DF_UUID": creating\n", DP_UUID(key));

	D_ALLOC_PTR(pool);
	if (pool == NULL)
		D_GOTO(err, rc = -DER_NOMEM);

	D_ASSERT(dss_tgt_nr > 0);
	D_ALLOC_ARRAY(pool->sp_states, dss_tgt_nr);
	if (pool->sp_states == NULL)
		D_GOTO(err_pool, rc = -DER_NOMEM);

	rc = ABT_rwlock_create(&pool->sp_lock);
	if (rc != ABT_SUCCESS)
		D_GOTO(err_states, rc = dss_abterr2der(rc));

	rc = ABT_mutex_create(&pool->sp_mutex);
	if (rc != ABT_SUCCESS)
		D_GOTO(err_lock, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&pool->sp_fetch_hdls_cond);
	if (rc != ABT_SUCCESS)
		D_GOTO(err_mutex, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&pool->sp_fetch_hdls_done_cond);
	if (rc != ABT_SUCCESS)
		D_GOTO(err_cond, rc = dss_abterr2der(rc));

	D_INIT_LIST_HEAD(&pool->sp_ec_ephs_list);
	uuid_copy(pool->sp_uuid, key);
	D_INIT_LIST_HEAD(&pool->sp_hdls);
	pool->sp_map_version = arg->pca_map_version;
	pool->sp_reclaim = DAOS_RECLAIM_LAZY; /* default reclaim strategy */
	pool->sp_data_thresh = DAOS_PROP_PO_DATA_THRESH_DEFAULT;

	/** set up ds_pool metrics */
	rc = ds_pool_metrics_start(pool);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to set up ds_pool metrics: %d\n",
			DP_UUID(key), rc);
		goto err_done_cond;
	}

	uuid_unparse_lower(key, group_id);
	rc = crt_group_secondary_create(group_id, NULL /* primary_grp */,
					NULL /* ranks */, &pool->sp_group);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool group: %d\n",
			DP_UUID(key), rc);
		goto err_metrics;
	}

	rc = ds_iv_ns_create(info->dmi_ctx, pool->sp_uuid, pool->sp_group,
			     &iv_ns_id, &pool->sp_iv_ns);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool IV NS: %d\n",
			DP_UUID(key), rc);
		goto err_group;
	}

	*link = &pool->sp_entry;
	return 0;

err_group:
	rc_tmp = crt_group_secondary_destroy(pool->sp_group);
	if (rc_tmp != 0)
		D_ERROR(DF_UUID": failed to destroy pool group: "DF_RC"\n",
			DP_UUID(pool->sp_uuid), DP_RC(rc_tmp));
err_metrics:
	ds_pool_metrics_stop(pool);
err_done_cond:
	ABT_cond_free(&pool->sp_fetch_hdls_done_cond);
err_cond:
	ABT_cond_free(&pool->sp_fetch_hdls_cond);
err_mutex:
	ABT_mutex_free(&pool->sp_mutex);
err_lock:
	ABT_rwlock_free(&pool->sp_lock);
err_states:
	D_FREE(pool->sp_states);
err_pool:
	D_FREE(pool);
err:
	return rc;
}

static void
pool_free_ref(struct daos_llink *llink)
{
	struct ds_pool *pool = pool_obj(llink);
	int		rc;

	D_DEBUG(DB_MGMT, DF_UUID": freeing\n", DP_UUID(pool->sp_uuid));

	D_ASSERT(d_list_empty(&pool->sp_hdls));

	ds_cont_track_eph_free(pool);

	pl_map_disconnect(pool->sp_uuid);
	if (pool->sp_map != NULL)
		pool_map_decref(pool->sp_map);

	ds_iv_ns_put(pool->sp_iv_ns);

	rc = crt_group_secondary_destroy(pool->sp_group);
	if (rc != 0)
		D_ERROR(DF_UUID": failed to destroy pool group: %d\n",
			DP_UUID(pool->sp_uuid), rc);

	/** release metrics */
	ds_pool_metrics_stop(pool);

	if (pool->sp_map_bc != NULL)
		ds_pool_put_map_bc(pool->sp_map_bc);
	ABT_cond_free(&pool->sp_fetch_hdls_cond);
	ABT_cond_free(&pool->sp_fetch_hdls_done_cond);
	ABT_mutex_free(&pool->sp_mutex);
	ABT_rwlock_free(&pool->sp_lock);
	D_FREE(pool->sp_states);
	D_FREE(pool);
}

static bool
pool_cmp_keys(const void *key, unsigned int ksize, struct daos_llink *llink)
{
	struct ds_pool *pool = pool_obj(llink);

	return uuid_compare(key, pool->sp_uuid) == 0;
}

static uint32_t
pool_rec_hash(struct daos_llink *llink)
{
	struct ds_pool *pool = pool_obj(llink);

	return d_hash_string_u32((const char *)pool->sp_uuid, sizeof(uuid_t));
}

static struct daos_llink_ops pool_cache_ops = {
	.lop_alloc_ref	= pool_alloc_ref,
	.lop_free_ref	= pool_free_ref,
	.lop_cmp_keys	= pool_cmp_keys,
	.lop_rec_hash	= pool_rec_hash,
};

int
ds_pool_cache_init(void)
{
	int rc;

	rc = daos_lru_cache_create(-1 /* bits */, D_HASH_FT_NOLOCK /* feats */,
				   &pool_cache_ops, &pool_cache);
	return rc;
}

void
ds_pool_cache_fini(void)
{
	daos_lru_cache_destroy(pool_cache);
}

int
ds_pool_lookup_internal(const uuid_t uuid, struct ds_pool **pool)
{
	struct daos_llink	*llink;
	int			 rc;

	D_ASSERT(pool != NULL);
	*pool = NULL;
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	rc = daos_lru_ref_hold(pool_cache, (void *)uuid, sizeof(uuid_t),
			       NULL /* create_args */, &llink);
	if (rc != 0)
		return rc;

	*pool = pool_obj(llink);
	return 0;
}

/**
 * If the pool can not be found due to non-existence or it is being stopped, then
 * @pool will be set to NULL and return proper failure code, otherwise return 0 and
 * set @pool.
 */
int
ds_pool_lookup(const uuid_t uuid, struct ds_pool **pool)
{
	int rc;

	rc = ds_pool_lookup_internal(uuid, pool);
	if (rc != 0)
		return rc;

	if ((*pool)->sp_stopping) {
		D_DEBUG(DB_MD, DF_UUID": is in stopping\n", DP_UUID(uuid));
		ds_pool_put(*pool);
		*pool = NULL;
		return -DER_SHUTDOWN;
	}

	return 0;
}

void
ds_pool_get(struct ds_pool *pool)
{
	D_ASSERT(pool != NULL);
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	daos_lru_ref_add(&pool->sp_entry);
}

void
ds_pool_put(struct ds_pool *pool)
{
	D_ASSERT(pool != NULL);
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	daos_lru_ref_release(pool_cache, &pool->sp_entry);
}

void
pool_fetch_hdls_ult(void *data)
{
	struct ds_pool	*pool = data;
	int		rc = 0;

	D_INFO(DF_UUID": begin: fetch_hdls=%u stopping=%u\n", DP_UUID(pool->sp_uuid),
	       pool->sp_fetch_hdls, pool->sp_stopping);

	/* sp_map == NULL means the IV ns is not setup yet, i.e.
	 * the pool leader does not broadcast the pool map to the
	 * current node yet, see pool_iv_pre_sync().
	 */
	ABT_mutex_lock(pool->sp_mutex);
	if (pool->sp_map == NULL) {
		D_INFO(DF_UUID": waiting for map\n", DP_UUID(pool->sp_uuid));
		ABT_cond_wait(pool->sp_fetch_hdls_cond, pool->sp_mutex);
	}
	ABT_mutex_unlock(pool->sp_mutex);

	if (pool->sp_stopping) {
		D_DEBUG(DB_MD, DF_UUID": skip fetching hdl due to stop\n",
			DP_UUID(pool->sp_uuid));
		D_GOTO(out, rc);
	}
	D_INFO(DF_UUID": fetching handles\n", DP_UUID(pool->sp_uuid));
	rc = ds_pool_iv_conn_hdl_fetch(pool);
	if (rc) {
		D_INFO(DF_UUID" iv conn fetch %d\n", DP_UUID(pool->sp_uuid), rc);
		D_GOTO(out, rc);
	}

out:
	D_INFO(DF_UUID": signaling done\n", DP_UUID(pool->sp_uuid));
	ABT_mutex_lock(pool->sp_mutex);
	ABT_cond_signal(pool->sp_fetch_hdls_done_cond);
	ABT_mutex_unlock(pool->sp_mutex);

	pool->sp_fetch_hdls = 0;
	D_INFO(DF_UUID": end\n", DP_UUID(pool->sp_uuid));
}

static void
tgt_track_eph_query_ult(void *data)
{
	ds_cont_track_eph_query_ult(data);
}

static int
ds_pool_start_track_eph_query_ult(struct ds_pool *pool)
{
	struct sched_req_attr	attr;
	uuid_t			anonym_uuid;

	if (unlikely(ec_agg_disabled))
		return 0;

	D_ASSERT(pool->sp_ec_ephs_req == NULL);
	uuid_clear(anonym_uuid);
	sched_req_attr_init(&attr, SCHED_REQ_ANONYM, &anonym_uuid);
	pool->sp_ec_ephs_req = sched_create_ult(&attr, tgt_track_eph_query_ult, pool,
						DSS_DEEP_STACK_SZ);
	if (pool->sp_ec_ephs_req == NULL) {
		D_ERROR(DF_UUID": failed create ec eph equery ult.\n",
			DP_UUID(pool->sp_uuid));
		return -DER_NOMEM;
	}

	return 0;
}

static void
ds_pool_tgt_ec_eph_query_abort(struct ds_pool *pool)
{
	if (pool->sp_ec_ephs_req == NULL)
		return;

	D_DEBUG(DB_MD, DF_UUID": Stopping EC query ULT\n",
		DP_UUID(pool->sp_uuid));

	sched_req_wait(pool->sp_ec_ephs_req, true);
	sched_req_put(pool->sp_ec_ephs_req);
	pool->sp_ec_ephs_req = NULL;
	D_INFO(DF_UUID": EC query ULT stopped\n", DP_UUID(pool->sp_uuid));
}

static void
pool_fetch_hdls_ult_abort(struct ds_pool *pool)
{
	D_INFO(DF_UUID": begin: fetch_hdls=%u stopping=%u\n", DP_UUID(pool->sp_uuid),
	       pool->sp_fetch_hdls, pool->sp_stopping);

	if (!pool->sp_fetch_hdls) {
		D_INFO(DF_UUID": fetch hdls ULT aborted\n", DP_UUID(pool->sp_uuid));
		return;
	}

	ABT_mutex_lock(pool->sp_mutex);
	ABT_cond_signal(pool->sp_fetch_hdls_cond);
	ABT_mutex_unlock(pool->sp_mutex);
	D_INFO(DF_UUID": signaled\n", DP_UUID(pool->sp_uuid));

	ABT_mutex_lock(pool->sp_mutex);
	D_INFO(DF_UUID": waiting for ULT\n", DP_UUID(pool->sp_uuid));
	ABT_cond_wait(pool->sp_fetch_hdls_done_cond, pool->sp_mutex);
	ABT_mutex_unlock(pool->sp_mutex);
	D_INFO(DF_UUID": fetch hdls ULT aborted\n", DP_UUID(pool->sp_uuid));
}

/*
 * Start a pool. Must be called on the system xstream. Hold the ds_pool object
 * till ds_pool_stop. Only for mgmt and pool modules.
 */
int
ds_pool_start(uuid_t uuid, bool aft_chk, bool immutable)
{
	struct ds_pool			*pool;
	struct daos_llink		*llink;
	struct ds_pool_create_arg	arg = {};
	int				rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	/*
	 * Look up the pool without create_args (see pool_alloc_ref) to see if
	 * the pool is started already.
	 */
	rc = daos_lru_ref_hold(pool_cache, (void *)uuid, sizeof(uuid_t),
			       NULL /* create_args */, &llink);
	if (rc == 0) {
		pool = pool_obj(llink);
		if (pool->sp_stopping) {
			D_ERROR(DF_UUID": stopping isn't done yet\n",
				DP_UUID(uuid));
			rc = -DER_BUSY;
		} else if (unlikely(aft_chk)) {
			/*
			 * Someone still references the pool after CR check
			 * that blocks pool (re)start for full pool service.
			 */
			D_ERROR(DF_UUID": someone still references the pool after CR check\n",
				DP_UUID(uuid));
			rc = -DER_BUSY;
		}
		/* Already started; drop our reference. */
		daos_lru_ref_release(pool_cache, &pool->sp_entry);
		return rc;
	} else if (rc != -DER_NONEXIST) {
		D_ERROR(DF_UUID": failed to look up pool: %d\n", DP_UUID(uuid),
			rc);
		if (rc == -DER_EXIST)
			rc = -DER_BUSY;
		return rc;
	}

	/* Start it by creating the ds_pool object and hold the reference. */
	rc = daos_lru_ref_hold(pool_cache, (void *)uuid, sizeof(uuid_t), &arg,
			       &llink);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to start pool: %d\n", DP_UUID(uuid),
			rc);
		return rc;
	}

	pool = pool_obj(llink);

	if (aft_chk)
		pool->sp_cr_checked = 1;
	else
		pool->sp_cr_checked = 0;

	if (immutable)
		pool->sp_immutable = 1;
	else
		pool->sp_immutable = 0;

	rc = pool_child_add_all(pool);
	if (rc != 0)
		goto failure_pool;

	if (!ds_pool_skip_for_check(pool)) {
		rc = dss_ult_create(pool_fetch_hdls_ult, pool, DSS_XS_SYS, 0,
				    DSS_DEEP_STACK_SZ, NULL);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to create fetch ult: "DF_RC"\n",
				DP_UUID(uuid), DP_RC(rc));
			goto failure_children;
		}

		pool->sp_fetch_hdls = 1;
	}

	if (!ds_pool_restricted(pool, false)) {
		rc = ds_pool_start_track_eph_query_ult(pool);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to start ec eph query ult: "DF_RC"\n",
				DP_UUID(uuid), DP_RC(rc));
			D_GOTO(failure_ult, rc);
		}
	}

	ds_iv_ns_start(pool->sp_iv_ns);

	/* Ignore errors, for other PS replicas may work. */
	rc = ds_pool_svc_start(uuid);
	if (rc != 0)
		DL_ERROR(rc, DF_UUID": failed to start pool service", DP_UUID(uuid));

	return 0;

failure_ult:
	pool_fetch_hdls_ult_abort(pool);
failure_children:
	pool_child_delete_all(pool);
failure_pool:
	ds_pool_put(pool);
	return rc;
}

static void ds_pool_hdl_get(struct ds_pool_hdl *hdl);
static void pool_tgt_disconnect(struct ds_pool_hdl *hdl);

static void
pool_tgt_disconnect_all(struct ds_pool *pool)
{
	while (!d_list_empty(&pool->sp_hdls)) {
		struct ds_pool_hdl *hdl;

		/*
		 * The handle will not be freed before we get our reference,
		 * because ds_pool_tgt_connect, ds_pool_tgt_disconnect, and us
		 * all run on the same xstream.
		 */
		hdl = d_list_entry(pool->sp_hdls.next, struct ds_pool_hdl, sph_pool_entry);
		ds_pool_hdl_get(hdl);
		pool_tgt_disconnect(hdl);
		ds_pool_hdl_put(hdl);
	}
}

/*
 * Stop a pool. Must be called on the system xstream. Release the ds_pool
 * object reference held by ds_pool_start. Only for mgmt and pool modules.
 */
int
ds_pool_stop(uuid_t uuid)
{
	struct ds_pool *pool;
	int             rc;

	ds_pool_failed_remove(uuid);

	ds_pool_lookup_internal(uuid, &pool);
	if (pool == NULL) {
		D_INFO(DF_UUID ": not found\n", DP_UUID(uuid));
		return 0;
	}
	if (pool->sp_stopping) {
		rc = -DER_AGAIN;
		DL_INFO(rc, DF_UUID ": already stopping", DP_UUID(uuid));
		ds_pool_put(pool);
		return rc;
	}
	pool->sp_stopping = 1;
	D_INFO(DF_UUID ": stopping\n", DP_UUID(uuid));

	/* An error means the PS is stopping. Ignore it. */
	rc = ds_pool_svc_stop(uuid);
	if (rc != 0)
		DL_INFO(rc, DF_UUID": stop pool service", DP_UUID(uuid));

	pool_tgt_disconnect_all(pool);

	ds_iv_ns_stop(pool->sp_iv_ns);
	ds_pool_tgt_ec_eph_query_abort(pool);
	pool_fetch_hdls_ult_abort(pool);

	ds_rebuild_abort(pool->sp_uuid, -1, -1, -1);
	ds_migrate_stop(pool, -1, -1);

	ds_pool_put(pool); /* held by ds_pool_start */

	while (!daos_lru_is_last_user(&pool->sp_entry))
		dss_sleep(1000 /* ms */);
	D_INFO(DF_UUID ": completed reference wait\n", DP_UUID(uuid));

	pool_child_delete_all(pool);

	ds_pool_put(pool);
	D_INFO(DF_UUID ": stopped\n", DP_UUID(uuid));
	return 0;
}

/* ds_pool_hdl ****************************************************************/

static struct d_hash_table *pool_hdl_hash;

static inline struct ds_pool_hdl *
pool_hdl_obj(d_list_t *rlink)
{
	return container_of(rlink, struct ds_pool_hdl, sph_entry);
}

static bool
pool_hdl_key_cmp(struct d_hash_table *htable, d_list_t *rlink,
		 const void *key, unsigned int ksize)
{
	struct ds_pool_hdl *hdl = pool_hdl_obj(rlink);

	D_ASSERTF(ksize == sizeof(uuid_t), "%u\n", ksize);
	return uuid_compare(hdl->sph_uuid, key) == 0;
}

static uint32_t
pool_hdl_key_hash(struct d_hash_table *htable, const void *key,
		  unsigned int ksize)
{
	D_ASSERTF(ksize == sizeof(uuid_t), "%u\n", ksize);
	return *((const uint32_t *)key);
}

static uint32_t
pool_hdl_rec_hash(struct d_hash_table *htable, d_list_t *link)
{
	struct ds_pool_hdl *hdl = pool_hdl_obj(link);
	uint32_t *retp = (uint32_t *)hdl->sph_uuid;

	return *retp;
}

static void
pool_hdl_rec_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	pool_hdl_obj(rlink)->sph_ref++;
}

static bool
pool_hdl_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ds_pool_hdl *hdl = pool_hdl_obj(rlink);

	D_ASSERTF(hdl->sph_ref > 0, "%d\n", hdl->sph_ref);
	hdl->sph_ref--;
	return hdl->sph_ref == 0;
}

static void
pool_hdl_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ds_pool_hdl *hdl = pool_hdl_obj(rlink);

	D_DEBUG(DB_MD, DF_UUID": freeing "DF_UUID"\n",
		DP_UUID(hdl->sph_pool->sp_uuid), DP_UUID(hdl->sph_uuid));
	D_ASSERT(d_hash_rec_unlinked(&hdl->sph_entry));
	D_ASSERT(d_list_empty(&hdl->sph_pool_entry));
	D_ASSERTF(hdl->sph_ref == 0, "%d\n", hdl->sph_ref);
	daos_iov_free(&hdl->sph_cred);
	ds_pool_put(hdl->sph_pool);
	D_FREE(hdl);
}

static d_hash_table_ops_t pool_hdl_hash_ops = {
	.hop_key_cmp	= pool_hdl_key_cmp,
	.hop_key_hash	= pool_hdl_key_hash,
	.hop_rec_hash	= pool_hdl_rec_hash,
	.hop_rec_addref	= pool_hdl_rec_addref,
	.hop_rec_decref	= pool_hdl_rec_decref,
	.hop_rec_free	= pool_hdl_rec_free
};

int
ds_pool_hdl_hash_init(void)
{
	return d_hash_table_create(0 /* feats */, 4 /* bits */, NULL /* priv */,
				   &pool_hdl_hash_ops, &pool_hdl_hash);
}

void
ds_pool_hdl_hash_fini(void)
{
	d_hash_table_destroy(pool_hdl_hash, true /* force */);
}

static int
pool_hdl_add(struct ds_pool_hdl *hdl)
{
	if (dss_srv_shutting_down())
		return -DER_CANCELED;

	return d_hash_rec_insert(pool_hdl_hash, hdl->sph_uuid,
				 sizeof(uuid_t), &hdl->sph_entry,
				 true /* exclusive */);
}

static void
pool_hdl_delete(struct ds_pool_hdl *hdl)
{
	bool deleted;

	deleted = d_hash_rec_delete(pool_hdl_hash, hdl->sph_uuid,
				    sizeof(uuid_t));
	D_ASSERT(deleted == true);
}

struct ds_pool_hdl *
ds_pool_hdl_lookup(const uuid_t uuid)
{
	d_list_t *rlink;

	rlink = d_hash_rec_find(pool_hdl_hash, uuid, sizeof(uuid_t));
	if (rlink == NULL)
		return NULL;

	return pool_hdl_obj(rlink);
}

static void
ds_pool_hdl_get(struct ds_pool_hdl *hdl)
{
	d_hash_rec_addref(pool_hdl_hash, &hdl->sph_entry);
}

void
ds_pool_hdl_put(struct ds_pool_hdl *hdl)
{
	d_hash_rec_decref(pool_hdl_hash, &hdl->sph_entry);
}

static void
aggregate_pool_space(struct daos_pool_space *agg_ps, uint64_t *agg_mem_bytes,
		     struct daos_pool_space *ps, uint64_t *mem_bytes)
{
	int	i;
	bool	first;

	D_ASSERT(agg_ps && ps);

	if (ps->ps_ntargets == 0) {
		D_DEBUG(DB_TRACE, "Skip empty space info\n");
		return;
	}

	first = (agg_ps->ps_ntargets == 0);
	agg_ps->ps_ntargets += ps->ps_ntargets;

	for (i = DAOS_MEDIA_SCM; i < DAOS_MEDIA_MAX; i++) {
		agg_ps->ps_space.s_total[i] += ps->ps_space.s_total[i];
		agg_ps->ps_space.s_free[i] += ps->ps_space.s_free[i];

		if (agg_ps->ps_free_max[i] < ps->ps_free_max[i])
			agg_ps->ps_free_max[i] = ps->ps_free_max[i];
		if (agg_ps->ps_free_min[i] > ps->ps_free_min[i] || first)
			agg_ps->ps_free_min[i] = ps->ps_free_min[i];

		agg_ps->ps_free_mean[i] = agg_ps->ps_space.s_free[i] /
					  agg_ps->ps_ntargets;
	}
	if (agg_mem_bytes != NULL) {
		D_ASSERT(mem_bytes != NULL);
		*agg_mem_bytes += *mem_bytes;
	}
}

struct pool_query_xs_arg {
	struct ds_pool		*qxa_pool;
	struct daos_pool_space	 qxa_space;
	uint64_t		 qxa_mem_bytes;
};

static void
pool_query_xs_reduce(void *agg_arg, void *xs_arg)
{
	struct pool_query_xs_arg	*a_arg = agg_arg;
	struct pool_query_xs_arg	*x_arg = xs_arg;

	if (x_arg->qxa_space.ps_ntargets == 0)
		return;

	D_ASSERT(x_arg->qxa_space.ps_ntargets == 1);
	aggregate_pool_space(&a_arg->qxa_space, &a_arg->qxa_mem_bytes, &x_arg->qxa_space,
			     &x_arg->qxa_mem_bytes);
}

static int
pool_query_xs_arg_alloc(struct dss_stream_arg_type *xs, void *agg_arg)
{
	struct pool_query_xs_arg	*x_arg, *a_arg = agg_arg;

	D_ALLOC_PTR(x_arg);
	if (x_arg == NULL)
		return -DER_NOMEM;

	xs->st_arg = x_arg;
	x_arg->qxa_pool = a_arg->qxa_pool;
	return 0;
}

static void
pool_query_xs_arg_free(struct dss_stream_arg_type *xs)
{
	D_ASSERT(xs->st_arg != NULL);
	D_FREE(xs->st_arg);
}

static int
pool_query_space(uuid_t pool_uuid, struct daos_pool_space *x_ps, uint64_t *mem_file_bytes)
{
	struct dss_module_info	*info = dss_get_module_info();
	int			 tid = info->dmi_tgt_id;
	struct ds_pool_child	*pool_child;
	vos_pool_info_t		 vos_pool_info = { 0 };
	struct vos_pool_space	*vps = &vos_pool_info.pif_space;
	int			 i, rc;

	pool_child = ds_pool_child_lookup(pool_uuid);
	if (pool_child == NULL)
		return -DER_NO_HDL;

	rc = vos_pool_query(pool_child->spc_hdl, &vos_pool_info);
	if (rc != 0) {
		D_ERROR("Failed to query pool "DF_UUID", tgt_id: %d, "
			"rc: "DF_RC"\n", DP_UUID(pool_uuid), tid,
			DP_RC(rc));
		goto out;
	}

	x_ps->ps_ntargets = 1;
	x_ps->ps_space.s_total[DAOS_MEDIA_SCM] = SCM_TOTAL(vps);
	x_ps->ps_space.s_total[DAOS_MEDIA_NVME] = NVME_TOTAL(vps);
	if (mem_file_bytes != NULL)
		*mem_file_bytes = vps->vps_mem_bytes;

	/* Exclude the sys reserved space before reporting to user */
	if (SCM_FREE(vps) > SCM_SYS(vps))
		x_ps->ps_space.s_free[DAOS_MEDIA_SCM] =
				SCM_FREE(vps) - SCM_SYS(vps);
	else
		x_ps->ps_space.s_free[DAOS_MEDIA_SCM] = 0;

	if (NVME_FREE(vps) > NVME_SYS(vps))
		x_ps->ps_space.s_free[DAOS_MEDIA_NVME] =
				NVME_FREE(vps) - NVME_SYS(vps);
	else
		x_ps->ps_space.s_free[DAOS_MEDIA_NVME] = 0;

	for (i = DAOS_MEDIA_SCM; i < DAOS_MEDIA_MAX; i++) {
		x_ps->ps_free_max[i] = x_ps->ps_space.s_free[i];
		x_ps->ps_free_min[i] = x_ps->ps_space.s_free[i];
	}
out:
	ds_pool_child_put(pool_child);
	return rc;
}

static int
pool_query_one(void *vin)
{
	struct dss_coll_stream_args	*reduce = vin;
	struct dss_stream_arg_type	*streams = reduce->csa_streams;
	struct dss_module_info		*info = dss_get_module_info();
	int				 tid = info->dmi_tgt_id;
	struct pool_query_xs_arg	*x_arg = streams[tid].st_arg;
	struct ds_pool			*pool = x_arg->qxa_pool;

	return pool_query_space(pool->sp_uuid, &x_arg->qxa_space, &x_arg->qxa_mem_bytes);
}

static int
pool_tgt_query(struct ds_pool *pool, struct daos_pool_space *ps, uint64_t *mem_file_bytes)
{
	struct dss_coll_ops		 coll_ops;
	struct dss_coll_args		 coll_args = { 0 };
	struct pool_query_xs_arg	 agg_arg = { 0 };
	int				 rc = 0;

	D_ASSERT(ps != NULL);
	memset(ps, 0, sizeof(*ps));

	/* collective operations */
	coll_ops.co_func		= pool_query_one;
	coll_ops.co_reduce		= pool_query_xs_reduce;
	coll_ops.co_reduce_arg_alloc	= pool_query_xs_arg_alloc;
	coll_ops.co_reduce_arg_free	= pool_query_xs_arg_free;

	/* packing arguments for aggregator args */
	agg_arg.qxa_pool		= pool;

	/* setting aggregator args */
	coll_args.ca_aggregator		= &agg_arg;
	coll_args.ca_func_args		= &coll_args.ca_stream_args;

	rc = ds_pool_thread_collective_reduce(pool->sp_uuid,
					PO_COMP_ST_DOWN | PO_COMP_ST_DOWNOUT | PO_COMP_ST_NEW,
					&coll_ops, &coll_args, 0);
	if (rc != 0) {
		D_ERROR("Pool query on pool "DF_UUID" failed, "DF_RC"\n",
			DP_UUID(pool->sp_uuid), DP_RC(rc));
		goto out;
	}

	*ps = agg_arg.qxa_space;
	if (mem_file_bytes != NULL)
		*mem_file_bytes = agg_arg.qxa_mem_bytes;

out:
	return rc;
}

int
ds_pool_tgt_connect(struct ds_pool *pool, struct pool_iv_conn *pic)
{
	struct ds_pool_hdl	*hdl = NULL;
	d_iov_t			cred_iov;
	int			rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	hdl = ds_pool_hdl_lookup(pic->pic_hdl);
	if (hdl != NULL) {
		if (hdl->sph_sec_capas == pic->pic_capas) {
			D_DEBUG(DB_MD, DF_UUID": found compatible pool "
				"handle: hdl="DF_UUID" capas="DF_U64"\n",
				DP_UUID(pool->sp_uuid), DP_UUID(pic->pic_hdl),
				hdl->sph_sec_capas);
			rc = 0;
		} else {
			D_ERROR(DF_UUID": found conflicting pool handle: hdl="
				DF_UUID" capas="DF_U64"\n",
				DP_UUID(pool->sp_uuid), DP_UUID(pic->pic_hdl),
				hdl->sph_sec_capas);
			rc = -DER_EXIST;
		}
		ds_pool_hdl_put(hdl);
		return 0;
	}

	D_ALLOC_PTR(hdl);
	if (hdl == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	uuid_copy(hdl->sph_uuid, pic->pic_hdl);
	hdl->sph_flags = pic->pic_flags;
	hdl->sph_sec_capas = pic->pic_capas;
	hdl->sph_global_ver = pic->pic_global_ver;
	hdl->sph_obj_ver = pic->pic_obj_ver;
	ds_pool_get(pool);
	hdl->sph_pool = pool;

	cred_iov.iov_len = pic->pic_cred_size;
	cred_iov.iov_buf_len = pic->pic_cred_size;
	cred_iov.iov_buf = &pic->pic_creds[0];
	rc = daos_iov_copy(&hdl->sph_cred, &cred_iov);
	if (rc != 0) {
		ds_pool_put(pool);
		D_GOTO(out, rc);
	}

	if (pool->sp_stopping) {
		daos_iov_free(&hdl->sph_cred);
		ds_pool_put(pool);
		rc = -DER_SHUTDOWN;
		goto out;
	}

	d_list_add(&hdl->sph_pool_entry, &hdl->sph_pool->sp_hdls);

	rc = pool_hdl_add(hdl);
	if (rc != 0) {
		d_list_del_init(&hdl->sph_pool_entry);
		daos_iov_free(&hdl->sph_cred);
		ds_pool_put(pool);
		D_GOTO(out, rc);
	}

out:
	if (rc != 0)
		D_FREE(hdl);

	D_DEBUG(DB_MD, DF_UUID": connect "DF_RC"\n",
		DP_UUID(pool->sp_uuid), DP_RC(rc));
	return rc;
}

static void
pool_tgt_disconnect(struct ds_pool_hdl *hdl)
{
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	D_DEBUG(DB_MD, DF_UUID ": hdl=" DF_UUID "\n", DP_UUID(hdl->sph_pool->sp_uuid),
		DP_UUID(hdl->sph_uuid));
	pool_hdl_delete(hdl);
	d_list_del_init(&hdl->sph_pool_entry);
	ds_pool_iv_conn_hdl_invalidate(hdl->sph_pool, hdl->sph_uuid);
}

void
ds_pool_tgt_disconnect(uuid_t uuid)
{
	struct ds_pool_hdl *hdl;

	hdl = ds_pool_hdl_lookup(uuid);
	if (hdl == NULL) {
		D_DEBUG(DB_MD, "handle "DF_UUID" does not exist\n",
			DP_UUID(uuid));
		return;
	}

	pool_tgt_disconnect(hdl);

	ds_pool_hdl_put(hdl);
}

void
ds_pool_tgt_disconnect_handler(crt_rpc_t *rpc)
{
	struct pool_tgt_disconnect_in  *in = crt_req_get(rpc);
	struct pool_tgt_disconnect_out *out = crt_reply_get(rpc);
	uuid_t			       *hdl_uuids = in->tdi_hdls.ca_arrays;
	int				i;
	int				rc = 0;

	if (in->tdi_hdls.ca_count == 0)
		D_GOTO(out, rc = 0);

	if (in->tdi_hdls.ca_arrays == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	D_DEBUG(DB_MD, DF_UUID ": handling rpc: %p hdls[0]=" DF_UUID " nhdls=" DF_U64 "\n",
		DP_UUID(in->tdi_uuid), rpc, DP_UUID(hdl_uuids), in->tdi_hdls.ca_count);

	for (i = 0; i < in->tdi_hdls.ca_count; i++)
		ds_pool_tgt_disconnect(hdl_uuids[i]);
out:
	out->tdo_rc = (rc == 0 ? 0 : 1);
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p %d " DF_RC "\n", DP_UUID(in->tdi_uuid), rpc,
		out->tdo_rc, DP_RC(rc));
	crt_reply_send(rpc);
}

int
ds_pool_tgt_disconnect_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				  void *priv)
{
	struct pool_tgt_disconnect_out *out_source = crt_reply_get(source);
	struct pool_tgt_disconnect_out *out_result = crt_reply_get(result);

	out_result->tdo_rc += out_source->tdo_rc;
	return 0;
}

static int
update_pool_group(struct ds_pool *pool, struct pool_map *map)
{
	uint32_t	version;
	d_rank_list_t	ranks;
	int		rc;

	rc = crt_group_version(pool->sp_group, &version);
	D_ASSERTF(rc == 0, "%d\n", rc);
	D_DEBUG(DB_MD, DF_UUID": %u -> %u\n", DP_UUID(pool->sp_uuid), version,
		pool_map_get_version(map));

	rc = map_ranks_init(map, DC_POOL_GROUP_MAP_STATES, &ranks);
	if (rc != 0)
		return rc;

	/* Let secondary rank == primary rank. */
	rc = crt_group_secondary_modify(pool->sp_group, &ranks, &ranks,
					CRT_GROUP_MOD_OP_REPLACE,
					pool_map_get_version(map));
	if (rc != 0) {
		if (rc == -DER_OOG)
			D_DEBUG(DB_MD, DF_UUID": SG and PG out of sync: %d\n",
				DP_UUID(pool->sp_uuid), rc);
		else
			D_ERROR(DF_UUID": failed to update group: %d\n",
				DP_UUID(pool->sp_uuid), rc);
	}

	map_ranks_fini(&ranks);
	return rc;
}

/*
 * Called via dss_collective() to update the pool map version in the
 * ds_pool_child object.
 */
static int
update_child_map(void *data)
{
	struct ds_pool		*pool = (struct ds_pool *)data;
	struct ds_pool_child	*child;

	/* The pool child could be stopped */
	child = ds_pool_child_lookup(pool->sp_uuid);
	if (child == NULL) {
		D_DEBUG(DB_MD, DF_UUID": Pool child isn't found.", DP_UUID(pool->sp_uuid));
		return 0;
	}

	ds_cont_child_reset_ec_agg_eph_all(child);
	child->spc_map_version = pool->sp_map_version;
	ds_pool_child_put(child);
	return 0;
}

static int
map_bc_create(crt_context_t ctx, struct pool_map *map, struct ds_pool_map_bc **map_bc_out)
{
	struct ds_pool_map_bc *map_bc;
	d_iov_t                map_iov;
	d_sg_list_t            map_sgl;
	int                    rc;

	D_ALLOC_PTR(map_bc);
	if (map_bc == NULL) {
		rc = -DER_NOMEM;
		goto err;
	}

	map_bc->pmc_ref = 1;

	rc = pool_buf_extract(map, &map_bc->pmc_buf);
	if (rc != 0) {
		DL_ERROR(rc, "failed to extract pool map buffer");
		goto err_map_bc;
	}

	d_iov_set(&map_iov, map_bc->pmc_buf, pool_buf_size(map_bc->pmc_buf->pb_nr));
	map_sgl.sg_nr     = 1;
	map_sgl.sg_nr_out = 0;
	map_sgl.sg_iovs   = &map_iov;

	rc = crt_bulk_create(ctx, &map_sgl, CRT_BULK_RO, &map_bc->pmc_bulk);
	if (rc != 0)
		goto err_buf;

	*map_bc_out = map_bc;
	return 0;

err_buf:
	D_FREE(map_bc->pmc_buf);
err_map_bc:
	D_FREE(map_bc);
err:
	return rc;
}

static void
map_bc_get(struct ds_pool_map_bc *map_bc)
{
	map_bc->pmc_ref++;
}

static void
map_bc_put(struct ds_pool_map_bc *map_bc)
{
	map_bc->pmc_ref--;
	if (map_bc->pmc_ref == 0) {
		crt_bulk_free(map_bc->pmc_bulk);
		D_FREE(map_bc->pmc_buf);
		D_FREE(map_bc);
	}
}

int
ds_pool_lookup_map_bc(struct ds_pool *pool, crt_context_t ctx, struct ds_pool_map_bc **map_bc_out,
		      uint32_t *map_version_out)
{
	struct ds_pool_map_bc *map_bc;
	uint32_t               map_version;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	/* For accessing pool->sp_map, but not really necessary. */
	ABT_rwlock_rdlock(pool->sp_lock);

	if (pool->sp_map == NULL) {
		ABT_rwlock_unlock(pool->sp_lock);
		return -DER_NONEXIST;
	}

	if (pool->sp_map_bc == NULL) {
		int rc;

		rc = map_bc_create(ctx, pool->sp_map, &pool->sp_map_bc);
		if (rc != 0) {
			ABT_rwlock_unlock(pool->sp_lock);
			return rc;
		}
	}

	map_bc_get(pool->sp_map_bc);
	map_bc      = pool->sp_map_bc;
	map_version = pool_map_get_version(pool->sp_map);

	ABT_rwlock_unlock(pool->sp_lock);

	*map_bc_out      = map_bc;
	*map_version_out = map_version;
	return 0;
}

void
ds_pool_put_map_bc(struct ds_pool_map_bc *map_bc)
{
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	map_bc_put(map_bc);
}

int
ds_pool_tgt_map_update(struct ds_pool *pool, struct pool_buf *buf,
		       unsigned int map_version)
{
	struct pool_map *map = NULL;
	bool		map_updated = false;
	int		rc = 0;

	if (buf != NULL) {
		rc = pool_map_create(buf, map_version, &map);
		if (rc != 0) {
			D_ERROR(DF_UUID" failed to create pool map: "DF_RC"\n",
				DP_UUID(pool->sp_uuid), DP_RC(rc));
			return rc;
		}
	}

	ABT_rwlock_wrlock(pool->sp_lock);
	/* Check if the pool map needs to to update */
	if (map != NULL &&
	    (pool->sp_map == NULL ||
	     pool_map_get_version(pool->sp_map) < map_version)) {
		struct pool_map *tmp = pool->sp_map;

		D_DEBUG(DB_MD, DF_UUID ": updating pool map: version=%u->%u pointer=%p->%p\n",
			DP_UUID(pool->sp_uuid),
			pool->sp_map == NULL ? 0 : pool_map_get_version(pool->sp_map),
			pool_map_get_version(map), pool->sp_map, map);

		rc = update_pool_group(pool, map);
		if (rc != 0) {
			D_ERROR(DF_UUID": Can not update pool group: "DF_RC"\n",
				DP_UUID(pool->sp_uuid), DP_RC(rc));
			D_GOTO(out, rc);
		}

		rc = pl_map_update(pool->sp_uuid, map,
				   pool->sp_map != NULL ? false : true,
				   DEFAULT_PL_TYPE);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed update pl_map: "
				""DF_RC"\n", DP_UUID(pool->sp_uuid), DP_RC(rc));
			D_GOTO(out, rc);
		}

		rc = pool_map_update_failed_cnt(map);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed fail-cnt update pl_map"
				": %d\n", DP_UUID(pool->sp_uuid), rc);
			D_GOTO(out, rc);
		}

		/* Swap pool->sp_map and map. */
		pool->sp_map = map;
		map = tmp;

		/* Invalidate pool->sp_map_bc. */
		if (pool->sp_map_bc != NULL) {
			map_bc_put(pool->sp_map_bc);
			pool->sp_map_bc = NULL;
		}

		map_updated = true;
		D_INFO(DF_UUID ": updated pool map: version=%u->%u pointer=%p->%p\n",
		       DP_UUID(pool->sp_uuid), map == NULL ? 0 : pool_map_get_version(map),
		       pool_map_get_version(pool->sp_map), map, pool->sp_map);
	}

	/* Check if the pool map on each xstream needs to update */
	if (pool->sp_map_version < map_version) {
		unsigned int map_version_before = pool->sp_map_version;

		D_DEBUG(DB_MD, DF_UUID ": updating cached pool map version: %u->%u\n",
			DP_UUID(pool->sp_uuid), map_version_before, map_version);

		pool->sp_map_version = map_version;
		rc = ds_pool_task_collective(pool->sp_uuid, PO_COMP_ST_DOWN |
					     PO_COMP_ST_DOWNOUT | PO_COMP_ST_NEW,
					     update_child_map, pool, 0);
		D_ASSERT(rc == 0);

		map_updated = true;
		D_INFO(DF_UUID ": updated cached pool map version: %u->%u\n",
		       DP_UUID(pool->sp_uuid), map_version_before, map_version);
	}

	if (map_updated && !ds_pool_restricted(pool, false)) {
		struct dtx_scan_args	*arg;
		int ret;

		D_ALLOC_PTR(arg);
		if (arg == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		uuid_copy(arg->pool_uuid, pool->sp_uuid);
		arg->version = pool->sp_map_version;
		ret = dss_ult_create(dtx_resync_ult, arg, DSS_XS_SYS,
				     0, 0, NULL);
		if (ret) {
			/* Ignore DTX resync failure that is not fatal. */
			D_WARN("dtx_resync_ult failure %d\n", ret);
			D_FREE(arg);
		}
	} else {
		/* This should be a D_DEBUG eventually. */
		D_INFO(DF_UUID ": ignored pool map update: version=%u->%u cached_version=%u\n",
		       DP_UUID(pool->sp_uuid), pool_map_get_version(pool->sp_map), map_version,
		       pool->sp_map_version);
	}
out:
	ABT_rwlock_unlock(pool->sp_lock);
	if (map != NULL)
		pool_map_decref(map);
	return rc;
}

static void
pool_tgt_query_handler(crt_rpc_t *rpc, int handler_version)
{
	struct pool_tgt_query_in	*in = crt_req_get(rpc);
	struct pool_tgt_query_out	*out = crt_reply_get(rpc);
	struct ds_pool			*pool;
	uint64_t			*mem_file_bytes;
	int				 rc;

	if (handler_version >= 7)
		mem_file_bytes = &out->tqo_mem_file_bytes;
	else
		mem_file_bytes = NULL;

	/* Single target query */
	if (dss_get_module_info()->dmi_xs_id != 0) {
		rc = pool_query_space(in->tqi_op.pi_uuid, &out->tqo_space, mem_file_bytes);
		goto out;
	}

	/* Aggregate query over all targets on the node */
	rc = ds_pool_lookup(in->tqi_op.pi_uuid, &pool);
	if (rc) {
		D_ERROR("Failed to find pool "DF_UUID": %d\n",
			DP_UUID(in->tqi_op.pi_uuid), rc);
		D_GOTO(out, rc = -DER_NONEXIST);
	}

	rc = pool_tgt_query(pool, &out->tqo_space, mem_file_bytes);
	if (rc != 0)
		rc = 1;	/* For query aggregator */
	ds_pool_put(pool);
out:
	out->tqo_rc = rc;
	crt_reply_send(rpc);
}

void
ds_pool_tgt_query_handler_v6(crt_rpc_t *rpc)
{
	pool_tgt_query_handler(rpc, 6);
}

void
ds_pool_tgt_query_handler(crt_rpc_t *rpc)
{
	pool_tgt_query_handler(rpc, DAOS_POOL_VERSION);
}

int
ds_pool_tgt_query_aggregator_v6(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct pool_tgt_query_v6_out *out_source = crt_reply_get(source);
	struct pool_tgt_query_v6_out *out_result = crt_reply_get(result);

	out_result->tqo_rc += out_source->tqo_rc;
	if (out_source->tqo_rc != 0)
		return 0;

	aggregate_pool_space(&out_result->tqo_space, NULL, &out_source->tqo_space, NULL);
	return 0;
}

int
ds_pool_tgt_query_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct pool_tgt_query_out	*out_source = crt_reply_get(source);
	struct pool_tgt_query_out	*out_result = crt_reply_get(result);

	out_result->tqo_rc += out_source->tqo_rc;
	if (out_source->tqo_rc != 0)
		return 0;

	aggregate_pool_space(&out_result->tqo_space, &out_result->tqo_mem_file_bytes,
			     &out_source->tqo_space, &out_source->tqo_mem_file_bytes);
	return 0;
}

struct update_vos_prop_arg {
	struct ds_pool *uvp_pool;
	bool            uvp_checkpoint_props_changed;
};

static int
update_vos_prop_on_targets(void *in)
{
	struct update_vos_prop_arg *arg   = in;
	struct ds_pool             *pool  = arg->uvp_pool;
	struct ds_pool_child       *child = NULL;
	uint32_t                    df_version;
	int                         ret = 0;

	child = ds_pool_child_lookup(pool->sp_uuid);
	if (child == NULL)
		return -DER_NONEXIST;	/* no child created yet? */

	if (unlikely(child->spc_no_storage))
		D_GOTO(out, ret = 0);

	ret = vos_pool_ctl(child->spc_hdl, VOS_PO_CTL_SET_DATA_THRESH, &pool->sp_data_thresh);
	if (ret)
		goto out;

	ret = vos_pool_ctl(child->spc_hdl, VOS_PO_CTL_SET_SPACE_RB, &pool->sp_space_rb);
	if (ret)
		goto out;

	/** If necessary, upgrade the vos pool format */
	df_version = ds_pool_get_vos_df_version(pool->sp_global_version);
	if (df_version == 0) {
		ret = -DER_NO_PERM;
		DL_ERROR(ret, DF_UUID ": pool global version %u no longer supported",
			 DP_UUID(pool->sp_uuid), pool->sp_global_version);
		D_GOTO(out, ret);
	}
	D_DEBUG(DB_MGMT, DF_UUID ": upgrading VOS pool durable format to %u\n",
		DP_UUID(pool->sp_uuid), df_version);
	ret = vos_pool_upgrade(child->spc_hdl, df_version);

	if (arg->uvp_checkpoint_props_changed) {
		if (child->spc_chkpt_req != NULL)
			sched_req_wakeup(child->spc_chkpt_req);
	}
	child->spc_reint_mode = pool->sp_reint_mode;
out:
	ds_pool_child_put(child);

	return ret;
}

int
ds_pool_tgt_prop_update(struct ds_pool *pool, struct pool_iv_prop *iv_prop)
{
	struct update_vos_prop_arg arg;
	int                        ret;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	pool->sp_ec_cell_sz = iv_prop->pip_ec_cell_sz;
	pool->sp_global_version = iv_prop->pip_global_version;
	pool->sp_reclaim = iv_prop->pip_reclaim;
	pool->sp_redun_fac = iv_prop->pip_redun_fac;
	pool->sp_ec_pda = iv_prop->pip_ec_pda;
	pool->sp_rp_pda = iv_prop->pip_rp_pda;
	pool->sp_perf_domain = iv_prop->pip_perf_domain;
	pool->sp_space_rb = iv_prop->pip_space_rb;
	pool->sp_data_thresh = iv_prop->pip_data_thresh;

	if (iv_prop->pip_reint_mode == DAOS_REINT_MODE_DATA_SYNC &&
	    iv_prop->pip_self_heal & DAOS_SELF_HEAL_AUTO_REBUILD)
		pool->sp_disable_rebuild = 0;
	else
		pool->sp_disable_rebuild = 1;

	if (iv_prop->pip_reint_mode == DAOS_REINT_MODE_INCREMENTAL)
		pool->sp_incr_reint = 1;

	D_DEBUG(DB_CSUM, "Updating pool to sched: %lu\n",
		iv_prop->pip_scrub_mode);
	pool->sp_scrub_mode = iv_prop->pip_scrub_mode;
	pool->sp_scrub_freq_sec = iv_prop->pip_scrub_freq;
	pool->sp_scrub_thresh = iv_prop->pip_scrub_thresh;
	pool->sp_reint_mode = iv_prop->pip_reint_mode;

	arg.uvp_pool                     = pool;
	arg.uvp_checkpoint_props_changed = false;

	if (pool->sp_checkpoint_mode != iv_prop->pip_checkpoint_mode) {
		pool->sp_checkpoint_mode         = iv_prop->pip_checkpoint_mode;
		arg.uvp_checkpoint_props_changed = 1;
	}

	if (pool->sp_checkpoint_freq != iv_prop->pip_checkpoint_freq) {
		pool->sp_checkpoint_freq         = iv_prop->pip_checkpoint_freq;
		arg.uvp_checkpoint_props_changed = 1;
	}

	if (pool->sp_checkpoint_thresh != iv_prop->pip_checkpoint_thresh) {
		pool->sp_checkpoint_thresh       = iv_prop->pip_checkpoint_thresh;
		arg.uvp_checkpoint_props_changed = 1;
	}

	ret = ds_pool_thread_collective(pool->sp_uuid,
					PO_COMP_ST_DOWN | PO_COMP_ST_DOWNOUT | PO_COMP_ST_NEW,
					update_vos_prop_on_targets, &arg, DSS_ULT_DEEP_STACK);
	if (ret != 0)
		return ret;

	ret = ds_pool_svc_upgrade_vos_pool(pool);

	return ret;
}

/**
 * Query the cached pool map. If the cached version is <= in->tmi_map_version,
 * the pool map will not be transferred to the client.
 */
void
ds_pool_tgt_query_map_handler(crt_rpc_t *rpc)
{
	struct pool_tgt_query_map_in   *in = crt_req_get(rpc);
	struct pool_tgt_query_map_out  *out = crt_reply_get(rpc);
	struct ds_pool		       *pool;
	struct ds_pool_map_bc          *bc;
	unsigned int			version;
	int				rc;

	D_DEBUG(DB_TRACE, DF_UUID ": handling rpc: %p hdl=" DF_UUID "\n",
		DP_UUID(in->tmi_op.pi_uuid), rpc, DP_UUID(in->tmi_op.pi_hdl));

	/* Validate the pool handle and get the ds_pool object. */
	if (daos_rpc_from_client(rpc)) {
		struct ds_pool_hdl *hdl;

		hdl = ds_pool_hdl_lookup(in->tmi_op.pi_hdl);
		if (hdl == NULL) {
			D_ERROR(DF_UUID": cannot find pool handle "DF_UUID"\n",
				DP_UUID(in->tmi_op.pi_uuid), DP_UUID(in->tmi_op.pi_hdl));
			rc = -DER_NO_HDL;
			goto out;
		}
		ds_pool_get(hdl->sph_pool);
		pool = hdl->sph_pool;
		ds_pool_hdl_put(hdl);
	} else {
		/*
		 * See the comment on validating the pool handle in
		 * ds_pool_query_handler.
		 */
		rc = ds_pool_lookup(in->tmi_op.pi_uuid, &pool);
		if (rc) {
			D_ERROR(DF_UUID": failed to look up pool: %d\n",
				DP_UUID(in->tmi_op.pi_uuid), rc);
			rc = -DER_NONEXIST;
			goto out;
		}
		rc = ds_pool_hdl_is_from_srv(pool, in->tmi_op.pi_hdl);
		if (rc < 0) {
			DL_CDEBUG(rc == -DER_NOTLEADER, DLOG_DBG, DLOG_ERR, rc,
				  DF_UUID ": failed to check server pool handle " DF_UUID,
				  DP_UUID(in->tmi_op.pi_uuid), DP_UUID(in->tmi_op.pi_hdl));
			if (rc == -DER_NOTLEADER)
				rc = -DER_AGAIN;
			goto out_pool;
		} else if (!rc) {
			D_ERROR(DF_UUID": cannot find server pool handle "DF_UUID"\n",
				DP_UUID(in->tmi_op.pi_uuid), DP_UUID(in->tmi_op.pi_hdl));
			rc = -DER_NO_HDL;
			goto out_pool;
		}
	}

	/* Inefficient; better invent some zero-copy IV APIs. */
	rc = ds_pool_lookup_map_bc(pool, rpc->cr_ctx, &bc, &version);
	if (rc == -DER_NONEXIST)
		version = 0;
	else if (rc != 0)
		goto out_pool;
	if (version <= in->tmi_map_version) {
		rc = 0;
		goto out_version;
	}

	rc = ds_pool_transfer_map_buf(bc, rpc, in->tmi_map_bulk, &out->tmo_map_buf_size);

	ds_pool_put_map_bc(bc);
out_version:
	out->tmo_op.po_map_version = version;
out_pool:
	ds_pool_put(pool);
out:
	out->tmo_op.po_rc = rc;
	D_DEBUG(DB_TRACE, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->tmi_op.pi_uuid),
		rpc, DP_RC(out->tmo_op.po_rc));
	crt_reply_send(rpc);
}

struct tgt_discard_arg {
	uuid_t			     pool_uuid;
	uint64_t		     epoch;
	struct pool_target_addr_list tgt_list;
};

struct child_discard_arg {
	struct tgt_discard_arg	*tgt_discard;
	uuid_t			cont_uuid;
};

static struct tgt_discard_arg*
tgt_discard_arg_alloc(struct pool_target_addr_list *tgt_list)
{
	struct tgt_discard_arg	*arg;
	int			i;
	int			rc;

	D_ALLOC_PTR(arg);
	if (arg == NULL)
		return NULL;

	rc = pool_target_addr_list_alloc(tgt_list->pta_number, &arg->tgt_list);
	if (rc != 0) {
		D_FREE(arg);
		return NULL;
	}

	for (i = 0; i < tgt_list->pta_number; i++) {
		arg->tgt_list.pta_addrs[i].pta_rank = tgt_list->pta_addrs[i].pta_rank;
		arg->tgt_list.pta_addrs[i].pta_target = tgt_list->pta_addrs[i].pta_target;
	}

	return arg;
}

static void
tgt_discard_arg_free(struct tgt_discard_arg *arg)
{
	pool_target_addr_list_free(&arg->tgt_list);
	D_FREE(arg);
}

static int
obj_discard_cb(daos_handle_t ch, vos_iter_entry_t *ent,
	       vos_iter_type_t type, vos_iter_param_t *param,
	       void *data, unsigned *acts)
{
	struct child_discard_arg	*arg = data;
	struct d_backoff_seq		backoff_seq;
	daos_epoch_range_t		epr;
	int				rc;

	rc = d_backoff_seq_init(&backoff_seq, 0 /* nzeros */, 16 /* factor */, 8 /* next (ms) */,
				1 << 10 /* max (ms) */);
	D_ASSERTF(rc == 0, "d_backoff_seq_init: "DF_RC"\n", DP_RC(rc));

	epr.epr_hi = arg->tgt_discard->epoch;
	epr.epr_lo = 0;
	do {
		/* Inform the iterator and delete the object */
		*acts |= VOS_ITER_CB_DELETE;
		rc = vos_discard(param->ip_hdl, &ent->ie_oid, &epr, NULL, NULL);
		if (rc != -DER_BUSY && rc != -DER_INPROGRESS)
			break;

		D_DEBUG(DB_REBUILD, "retry by "DF_RC"/"DF_UOID"\n",
			DP_RC(rc), DP_UOID(ent->ie_oid));
		dss_sleep(d_backoff_seq_next(&backoff_seq));
	} while (1);

	d_backoff_seq_fini(&backoff_seq);

	if (rc != 0)
		D_ERROR("discard object pool/object "DF_UUID"/"DF_UOID" rc: "DF_RC"\n",
			DP_UUID(arg->tgt_discard->pool_uuid), DP_UOID(ent->ie_oid),
			DP_RC(rc));
	return rc;
}

/** vos_iter_cb_t */
static int
cont_discard_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		vos_iter_type_t type, vos_iter_param_t *iter_param,
		void *cb_arg, unsigned int *acts)
{
	struct child_discard_arg *arg = cb_arg;
	struct ds_cont_child	*cont = NULL;
	vos_iter_param_t	param = { 0 };
	struct vos_iter_anchors	anchor = { 0 };
	daos_handle_t		coh;
	struct d_backoff_seq	backoff_seq;
	int			rc;

	D_ASSERT(type == VOS_ITER_COUUID);
	if (uuid_compare(arg->cont_uuid, entry->ie_couuid) == 0) {
		D_DEBUG(DB_REBUILD, DF_UUID" already discard\n",
			DP_UUID(arg->cont_uuid));
		return 0;
	}

	rc = ds_cont_child_lookup(arg->tgt_discard->pool_uuid, entry->ie_couuid,
				  &cont);
	if (rc != DER_SUCCESS) {
		D_ERROR("Lookup container '"DF_UUIDF"' failed: "DF_RC"\n",
			DP_UUID(entry->ie_couuid), DP_RC(rc));
		return rc;
	}

	rc = vos_cont_open(iter_param->ip_hdl, entry->ie_couuid, &coh);
	if (rc != 0) {
		D_ERROR("Open container "DF_UUID" failed: "DF_RC"\n",
			DP_UUID(entry->ie_couuid), DP_RC(rc));
		D_GOTO(put, rc);
	}

	rc = d_backoff_seq_init(&backoff_seq, 0 /* nzeros */, 16 /* factor */, 8 /* next (ms) */,
				1 << 10 /* max (ms) */);
	D_ASSERTF(rc == 0, "d_backoff_seq_init: "DF_RC"\n", DP_RC(rc));

	param.ip_hdl = coh;
	param.ip_epr.epr_lo = 0;
	param.ip_epr.epr_hi = arg->tgt_discard->epoch;
	uuid_copy(arg->cont_uuid, entry->ie_couuid);
	do {
		/* Inform the iterator and delete the object */
		*acts |= VOS_ITER_CB_DELETE;
		rc = vos_iterate(&param, VOS_ITER_OBJ, false, &anchor, obj_discard_cb, NULL,
				 arg, NULL);
		if (rc != -DER_BUSY && rc != -DER_INPROGRESS)
			break;

		D_DEBUG(DB_REBUILD, "retry by "DF_RC"/"DF_UUID"\n",
			DP_RC(rc), DP_UUID(entry->ie_couuid));
		dss_sleep(d_backoff_seq_next(&backoff_seq));
	} while (1);

	d_backoff_seq_fini(&backoff_seq);
	vos_cont_close(coh);
	D_DEBUG(DB_TRACE, DF_UUID"/"DF_UUID" discard cont done: "DF_RC"\n",
		DP_UUID(arg->tgt_discard->pool_uuid), DP_UUID(entry->ie_couuid),
		DP_RC(rc));

put:
	ds_cont_child_put(cont);
	if (rc == 0)
		rc = ds_cont_child_destroy(arg->tgt_discard->pool_uuid, entry->ie_couuid);
	return rc;
}

static int
pool_child_discard(void *data)
{
	struct tgt_discard_arg	*arg = data;
	struct child_discard_arg cont_arg;
	struct ds_pool_child	*child;
	vos_iter_param_t	param = { 0 };
	struct vos_iter_anchors	anchor = { 0 };
	struct pool_target_addr addr;
	uint32_t		myrank;
	struct d_backoff_seq	backoff_seq;
	int			rc;

	myrank = dss_self_rank();
	addr.pta_rank = myrank;
	addr.pta_target = dss_get_module_info()->dmi_tgt_id;
	if (!pool_target_addr_found(&arg->tgt_list, &addr)) {
		D_DEBUG(DB_TRACE, "skip discard %u/%u.\n", addr.pta_rank,
			addr.pta_target);
		return 0;
	}

	D_DEBUG(DB_MD, DF_UUID" discard %u/%u\n", DP_UUID(arg->pool_uuid),
		myrank, addr.pta_target);

	/**
	 * When a faulty device is replaced with a new one using the
	 * “dmg storage replace nvme” command, the reintegration of
	 * affected pool targets is automatically triggered.
	 * The following steps outline the device replacement process on the engine side:
	 *
	 * 1) Replace the old device with the new device in the SMD.
	 * 2) Setup all SPDK related stuff for the new device.
	 * 3) Start ds_pool_child
	 *
	 * It is important to note that manual reintegration may be initiated
	 * before step 3, in which case, the function should return “DER_AGAIN."
	 */
	child = ds_pool_child_lookup(arg->pool_uuid);
	if (child == NULL)
		return -DER_AGAIN;

	param.ip_hdl = child->spc_hdl;

	rc = d_backoff_seq_init(&backoff_seq, 0 /* nzeros */, 16 /* factor */, 8 /* next (ms) */,
				1 << 10 /* max (ms) */);
	D_ASSERTF(rc == 0, "d_backoff_seq_init: "DF_RC"\n", DP_RC(rc));

	cont_arg.tgt_discard = arg;
	child->spc_discard_done = 0;
	do {
		rc = vos_iterate(&param, VOS_ITER_COUUID, false, &anchor,
				 cont_discard_cb, NULL, &cont_arg, NULL);
		if (rc != -DER_BUSY && rc != -DER_INPROGRESS)
			break;

		D_DEBUG(DB_REBUILD, "retry by "DF_RC"/"DF_UUID"\n",
			DP_RC(rc), DP_UUID(arg->pool_uuid));
		dss_sleep(d_backoff_seq_next(&backoff_seq));
	} while (1);

	child->spc_discard_done = 1;

	d_backoff_seq_fini(&backoff_seq);

	ds_pool_child_put(child);

	return rc;
}

static int
ds_pool_collective_reduce(uuid_t pool_uuid, uint32_t exclude_status, struct dss_coll_ops *coll_ops,
			  struct dss_coll_args *coll_args, uint32_t flags, bool thread)
{
	int			 *exclude_tgts = NULL;
	uint32_t		 exclude_tgt_nr = 0;
	int			 rc;

	if (exclude_status != 0) {
		rc = ds_pool_get_tgt_idx_by_state(pool_uuid, exclude_status, &exclude_tgts,
						  &exclude_tgt_nr);
		if (rc != 0) {
			D_ERROR(DF_UUID "failed to get index : rc "DF_RC"\n",
				DP_UUID(pool_uuid), DP_RC(rc));
			return rc;
		}

		if (exclude_tgts != NULL) {
			rc = dss_build_coll_bitmap(exclude_tgts, exclude_tgt_nr,
						   &coll_args->ca_tgt_bitmap,
						   &coll_args->ca_tgt_bitmap_sz);
			if (rc != 0)
				goto out;
		}
	}

	if (thread)
		rc = dss_thread_collective_reduce(coll_ops, coll_args, flags);
	else
		rc = dss_task_collective_reduce(coll_ops, coll_args, flags);

	D_DEBUG(DB_MD, DF_UUID " collective: "DF_RC"", DP_UUID(pool_uuid), DP_RC(rc));

out:
	if (coll_args->ca_tgt_bitmap)
		D_FREE(coll_args->ca_tgt_bitmap);
	if (exclude_tgts)
		D_FREE(exclude_tgts);

	return rc;
}

/* collective function over all target xstreams, exclude_status indicate which
 * targets should be excluded during collective.
 */
static int
ds_pool_collective(uuid_t pool_uuid, uint32_t exclude_status, int (*coll_func)(void *),
		   void *arg, uint32_t flags, bool thread)
{
	struct dss_coll_ops	 coll_ops = { 0 };
	struct dss_coll_args	 coll_args = { 0 };

	coll_ops.co_func = coll_func;
	coll_args.ca_func_args	= arg;
	return ds_pool_collective_reduce(pool_uuid, exclude_status, &coll_ops, &coll_args, flags,
					 thread);
}

int
ds_pool_thread_collective_reduce(uuid_t pool_uuid, uint32_t ex_status, struct dss_coll_ops *coll_ops,
				 struct dss_coll_args *coll_args, uint32_t flags)
{
	return ds_pool_collective_reduce(pool_uuid, ex_status, coll_ops, coll_args, flags, true);
}

int
ds_pool_task_collective_reduce(uuid_t pool_uuid, uint32_t ex_status, struct dss_coll_ops *coll_ops,
			       struct dss_coll_args *coll_args, uint32_t flags)
{
	return ds_pool_collective_reduce(pool_uuid, ex_status, coll_ops, coll_args, flags, false);
}

int
ds_pool_thread_collective(uuid_t pool_uuid, uint32_t ex_status, int (*coll_func)(void *),
			  void *arg, uint32_t flags)
{
	return ds_pool_collective(pool_uuid, ex_status, coll_func, arg, flags, true);
}

int
ds_pool_task_collective(uuid_t pool_uuid, uint32_t ex_status, int (*coll_func)(void *),
			void *arg, uint32_t flags)
{
	return ds_pool_collective(pool_uuid, ex_status, coll_func, arg, flags, false);
}

/* Discard the objects by epoch in this pool */
static void
ds_pool_tgt_discard_ult(void *data)
{
	struct ds_pool		*pool;
	struct tgt_discard_arg	*arg = data;
	uint32_t		ex_status;
	int			rc;

	/* If discard failed, let's still go ahead, since reintegration might
	 * still succeed, though it might leave some garbage on the reintegration
	 * target, the future scrub tool might fix it. XXX
	 */
	rc = ds_pool_lookup(arg->pool_uuid, &pool);
	if (pool == NULL) {
		D_INFO(DF_UUID" can not be found: %d\n", DP_UUID(arg->pool_uuid), rc);
		D_GOTO(free, rc = 0);
	}

	ex_status = PO_COMP_ST_UP | PO_COMP_ST_UPIN | PO_COMP_ST_DRAIN |
		    PO_COMP_ST_DOWN | PO_COMP_ST_NEW;
	ds_pool_thread_collective(arg->pool_uuid, ex_status, pool_child_discard, arg,
				  DSS_ULT_DEEP_STACK);

	pool->sp_need_discard = 0;
	pool->sp_discard_status = rc;
	ds_pool_put(pool);
free:
	tgt_discard_arg_free(arg);
}

void
ds_pool_tgt_discard_handler(crt_rpc_t *rpc)
{
	struct pool_tgt_discard_in	*in = crt_req_get(rpc);
	struct pool_tgt_discard_out	*out = crt_reply_get(rpc);
	struct pool_target_addr_list	pta_list;
	struct tgt_discard_arg		*arg = NULL;
	struct ds_pool			*pool;
	int				rc;

	pta_list.pta_number = in->ptdi_addrs.ca_count;
	pta_list.pta_addrs = in->ptdi_addrs.ca_arrays;
	arg = tgt_discard_arg_alloc(&pta_list);
	if (arg == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* POOL is already started in ds_mgmt_hdlr_tgt_create() during reintegration,
	 * though pool might being stopped for some reason.
	 * Let's do pool lookup to make sure pool child is already created.
	 */
	uuid_copy(arg->pool_uuid, in->ptdi_uuid);
	arg->epoch = DAOS_EPOCH_MAX;
	rc = ds_pool_lookup(arg->pool_uuid, &pool);
	if (rc) {
		D_INFO(DF_UUID" can not be found: %d\n", DP_UUID(arg->pool_uuid), rc);
		D_GOTO(out, rc = 0);
	}

	pool->sp_need_discard = 1;
	pool->sp_discard_status = 0;
	rc = dss_ult_create(ds_pool_tgt_discard_ult, arg, DSS_XS_SYS, 0, 0, NULL);

	ds_pool_put(pool);
out:
	out->ptdo_rc = rc;
	D_DEBUG(DB_MD, DF_UUID": replying rpc "DF_RC"\n", DP_UUID(in->ptdi_uuid),
		DP_RC(rc));
	crt_reply_send(rpc);
	if (rc != 0 && arg != NULL)
		tgt_discard_arg_free(arg);
}

static int
bulk_cb(const struct crt_bulk_cb_info *cb_info)
{
	ABT_eventual *eventual = cb_info->bci_arg;

	ABT_eventual_set(*eventual, (void *)&cb_info->bci_rc, sizeof(cb_info->bci_rc));
	return 0;
}

void
ds_pool_tgt_warmup_handler(crt_rpc_t *rpc)
{
	struct pool_tgt_warmup_in *in;
	crt_bulk_t                 bulk_cli;
	crt_bulk_t                 bulk_local = NULL;
	crt_bulk_opid_t            bulk_opid;
	uint64_t                   len;
	struct crt_bulk_desc       bulk_desc;
	ABT_eventual               eventual;
	void                      *buf = NULL;
	int                       *status;
	d_sg_list_t                sgl;
	d_iov_t                    iov;
	int                        rc;

	in       = crt_req_get(rpc);
	bulk_cli = in->tw_bulk;
	rc       = crt_bulk_get_len(bulk_cli, &len);
	if (rc != 0)
		D_GOTO(out, rc);
	D_ALLOC(buf, len);
	if (buf == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &iov;
	d_iov_set(&iov, buf, len);
	rc = crt_bulk_create(rpc->cr_ctx, &sgl, CRT_BULK_RW, &bulk_local);
	if (rc)
		goto out;

	bulk_desc.bd_rpc        = rpc;
	bulk_desc.bd_bulk_op    = CRT_BULK_GET;
	bulk_desc.bd_remote_hdl = bulk_cli;
	bulk_desc.bd_remote_off = 0;
	bulk_desc.bd_local_hdl  = bulk_local;
	bulk_desc.bd_local_off  = 0;
	bulk_desc.bd_len        = len;

	rc = ABT_eventual_create(sizeof(*status), &eventual);
	if (rc != ABT_SUCCESS)
		D_GOTO(out, rc = dss_abterr2der(rc));

	rc = crt_bulk_transfer(&bulk_desc, bulk_cb, &eventual, &bulk_opid);
	if (rc != 0)
		D_GOTO(out_eventual, rc);

	rc = ABT_eventual_wait(eventual, (void **)&status);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_eventual, rc = dss_abterr2der(rc));

	if (*status != 0)
		rc = *status;

out_eventual:
	ABT_eventual_free(&eventual);
out:
	if (bulk_local != NULL)
		crt_bulk_free(bulk_local);
	D_FREE(buf);
	if (rc)
		D_ERROR("rpc failed, " DF_RC "\n", DP_RC(rc));
	crt_reply_send(rpc);
}
