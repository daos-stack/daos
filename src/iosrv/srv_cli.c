/**
 * (C) Copyright 2017-2020 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(server)

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

	rc = ABT_thread_create(dx->dx_pools[DSS_POOL_GENERIC], dsc_progress,
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

int
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

tse_sched_t *
dsc_scheduler(void)
{
	return &dss_get_module_info()->dmi_xstream->dx_sched_dsc;
}
