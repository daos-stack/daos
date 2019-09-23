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
 * This file includes functions to call client daos API on the server side.
 */
#define D_LOGFAC	DD_FAC(rebuild)
#include <daos/pool.h>
#include <daos/container.h>
#include <daos/object.h>
#include <daos/event.h>
#include <daos/task.h>

#include <daos_types.h>
#include <daos_errno.h>
#include <daos_event.h>
#include <daos_task.h>

#include <daos_srv/daos_server.h>
#include "srv_internal.h"

/*
 * TODO:
 * Client APIs may need to acquire some global pthread lock, that could block
 * the whole xstream unexpectedly, we need to revise the client APIs to make
 * sure the global phtread locks are not used when they are called on server.
 */
static void
dsc_progress(void *arg)
{
	struct dss_xstream	*dx = arg;

	while (!dss_xstream_exiting(dx)) {
		tse_sched_progress(&dx->dx_sched_dsc);
		ABT_thread_yield();
	}
}

static int
dsc_progress_start(void)
{
	struct dss_xstream	*dx = dss_get_module_info()->dmi_xstream;
	int			 rc;

	if (dx->dx_dsc_started)
		return 0;

	rc = ABT_thread_create(dx->dx_pools[DSS_POOL_REBUILD], dsc_progress,
			       dx, ABT_THREAD_ATTR_NULL, NULL);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);

	dx->dx_dsc_started = true;
	return 0;
}

static int
dsc_task_comp_cb(tse_task_t *task, void *arg)
{
	ABT_eventual *eventual = arg;

	ABT_eventual_set(*eventual, &task->dt_result, sizeof(task->dt_result));
	return 0;
}

static int
dsc_task_run(tse_task_t *task, tse_task_cb_t retry_cb, void *arg, int arg_size,
	     bool sync)
{
	ABT_eventual	eventual;
	int		rc, *status;

	rc = dsc_progress_start();
	if (rc) {
		tse_task_complete(task, rc);
		return rc;
	}

	if (sync) {
		rc = ABT_eventual_create(sizeof(*status), &eventual);
		if (rc != ABT_SUCCESS) {
			rc = dss_abterr2der(rc);
			tse_task_complete(task, rc);
			return rc;
		}

		rc = dc_task_reg_comp_cb(task, dsc_task_comp_cb, &eventual,
					 sizeof(eventual));
		if (rc) {
			tse_task_complete(task, rc);
			ABT_eventual_free(&eventual);
			return rc;
		}
	}

	/*
	 * This retry completion callback must be last registered, so that
	 * it'll be called first on completion.
	 */
	if (retry_cb != NULL) {
		rc = dc_task_reg_comp_cb(task, retry_cb, arg, arg_size);
		if (rc) {
			tse_task_complete(task, rc);
			if (sync)
				ABT_eventual_free(&eventual);
			return rc;
		}
	}

	/* Task completion will be called by scheduler eventually */
	rc = tse_task_schedule(task, true);

	if (sync) {
		int	ret;

		ret = ABT_eventual_wait(eventual, (void **)&status);
		if (rc == 0)
			rc = ret != ABT_SUCCESS ?
			     dss_abterr2der(ret) : *status;

		ABT_eventual_free(&eventual);
	}

	return rc;
}

static inline tse_sched_t *
dsc_scheduler(void)
{
	return &dss_get_module_info()->dmi_xstream->dx_sched_dsc;
}

static int
dsc_obj_retry_cb(tse_task_t *task, void *arg)
{
	daos_handle_t *oh = arg;
	int rc;

	if (task->dt_result != -DER_NO_HDL || oh == NULL)
		return 0;

	/*
	 * If the remote rebuild pool/container is not ready,
	 * or the remote target has been evicted from pool.
	 * Note: the pool map will redistributed by IV
	 * automatically, so let's just keep refreshing the
	 * layout.
	 */
	rc = dc_obj_layout_refresh(*oh);
	if (rc) {
		D_ERROR("task %p, dc_obj_layout_refresh failed rc %d\n",
			task, rc);
		task->dt_result = rc;
		return rc;
	}

	D_DEBUG(DB_TRACE, "retry task %p\n", task);
	rc = dc_task_resched(task);
	if (rc != 0) {
		D_ERROR("Failed to re-init task (%p)\n", task);
		return rc;
	}

	/*
	 * Register the retry callback again, because it has been removed
	 * from the completion callback list. If this registration failed,
	 * the task will just stop retry on next run.
	 */
	rc = dc_task_reg_comp_cb(task, dsc_obj_retry_cb, oh, sizeof(*oh));
	return rc;
}

int
dsc_obj_open(daos_handle_t coh, daos_obj_id_t oid, unsigned int mode,
	     daos_handle_t *oh)
{
	tse_task_t	*task;
	int		 rc;

	rc = dc_obj_open_task_create(coh, oid, mode, oh, NULL,
				     dsc_scheduler(), &task);
	if (rc)
		return rc;

	return dsc_task_run(task, dsc_obj_retry_cb, NULL, 0, true);
}

int
dsc_obj_close(daos_handle_t oh)
{
	tse_task_t	 *task;
	int		  rc;

	rc = dc_obj_close_task_create(oh, NULL, dsc_scheduler(), &task);
	if (rc)
		return rc;

	return dsc_task_run(task, dsc_obj_retry_cb, &oh, sizeof(oh), true);
}

static int
tx_close_cb(tse_task_t *task, void *data)
{
	daos_handle_t *th = (daos_handle_t *)data;

	dc_tx_local_close(*th);
	return task->dt_result;
}

int
dsc_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		 uint32_t *nr, daos_key_desc_t *kds, d_sg_list_t *sgl,
		 daos_anchor_t *anchor)
{
	tse_task_t	*task;
	daos_handle_t	coh, th;
	int		rc;

	coh = dc_obj_hdl2cont_hdl(oh);
	rc = dc_tx_local_open(coh, epoch, &th);
	if (rc)
		return rc;

	rc = dc_obj_list_akey_task_create(oh, th, dkey, nr, kds, sgl, anchor,
					  NULL, dsc_scheduler(), &task);
	if (rc)
		return rc;

	rc = tse_task_register_comp_cb(task, tx_close_cb, &th, sizeof(th));
	if (rc) {
		tse_task_complete(task, rc);
		return rc;
	}

	return dsc_task_run(task, dsc_obj_retry_cb, &oh, sizeof(oh), true);
}

int
dsc_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
	      unsigned int nr, daos_iod_t *iods, d_sg_list_t *sgls,
	      daos_iom_t *maps)
{
	tse_task_t	*task;
	daos_handle_t	coh, th;
	int		rc;

	coh = dc_obj_hdl2cont_hdl(oh);
	rc = dc_tx_local_open(coh, epoch, &th);
	if (rc)
		return rc;

	rc = dc_obj_fetch_shard_task_create(oh, th, DIOF_TO_LEADER, 0, dkey,
					    nr, iods, sgls, maps, NULL,
					    dsc_scheduler(), &task);
	if (rc)
		return rc;

	rc = tse_task_register_comp_cb(task, tx_close_cb, &th, sizeof(th));
	if (rc) {
		tse_task_complete(task, rc);
		return rc;
	}

	return dsc_task_run(task, dsc_obj_retry_cb, &oh, sizeof(oh), true);
}

int
dsc_obj_list_obj(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		 daos_key_t *akey, daos_size_t *size, uint32_t *nr,
		 daos_key_desc_t *kds, daos_epoch_range_t *eprs,
		 d_sg_list_t *sgl, daos_anchor_t *anchor,
		 daos_anchor_t *dkey_anchor, daos_anchor_t *akey_anchor)
{
	tse_task_t	*task;
	daos_handle_t	coh, th;
	int		rc;

	coh = dc_obj_hdl2cont_hdl(oh);
	rc = dc_tx_local_open(coh, epoch, &th);
	if (rc)
		return rc;

	rc = dc_obj_list_obj_task_create(oh, th, dkey, akey, size, nr, kds,
					 eprs, sgl, anchor, dkey_anchor,
					 akey_anchor, true, NULL,
					 dsc_scheduler(), &task);
	if (rc)
		return rc;

	rc = tse_task_register_comp_cb(task, tx_close_cb, &th, sizeof(th));
	if (rc) {
		tse_task_complete(task, rc);
		return rc;
	}

	return dsc_task_run(task, dsc_obj_retry_cb, &oh, sizeof(oh), true);
}

int
dsc_pool_tgt_exclude(const uuid_t uuid, const char *grp,
		     const d_rank_list_t *svc, struct d_tgt_list *tgts)
{
	daos_pool_update_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, POOL_EXCLUDE);

	rc = dc_task_create(dc_pool_exclude, dsc_scheduler(), NULL, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->grp	= grp;
	args->svc	= (d_rank_list_t *)svc;
	args->tgts	= tgts;
	uuid_copy((unsigned char *)args->uuid, uuid);

	return dsc_task_run(task, NULL, NULL, 0, false);
}
