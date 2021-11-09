/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

#include <daos_srv/daos_engine.h>
#include "srv_internal.h"

/*
 * TODO:
 * Client APIs may need to acquire some global pthread lock, that could block
 * the whole xstream unexpectedly, we need to revise the client APIs to make
 * sure the global pthread locks are not used when they are called on server.
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

	/* NB: EC recovery will be done inside this ULT, so let's use DEEP stack size */
	rc = dss_ult_create(dsc_progress, dx, DSS_XS_SELF, 0, DSS_DEEP_STACK_SZ, NULL);
	if (rc == 0)
		dx->dx_dsc_started = true;
	return rc;
}

static int
dsc_task_comp_cb(tse_task_t *task, void *arg)
{
	ABT_eventual *eventual = arg;

	DABT_EVENTUAL_SET(*eventual, &task->dt_result, sizeof(task->dt_result));
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
			DABT_EVENTUAL_FREE(&eventual);
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
				DABT_EVENTUAL_FREE(&eventual);
			return rc;
		}
	}

	/* Task completion will be called by scheduler eventually */
	rc = tse_task_schedule(task, true);

	if (sync) {
		DABT_EVENTUAL_WAIT(eventual, (void **)&status);
		if (rc == 0)
			rc = *status;

		DABT_EVENTUAL_FREE(&eventual);
	}

	return rc;
}

tse_sched_t *
dsc_scheduler(void)
{
	return &dss_get_module_info()->dmi_xstream->dx_sched_dsc;
}
