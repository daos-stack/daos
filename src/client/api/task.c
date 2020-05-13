/**
 * (C) Copyright 2017 Intel Corporation.
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
/*
 * This file is part of client DAOS library.
 *
 * client/task.c
 */
#define D_LOGFAC	DD_FAC(client)

#include <daos/common.h>
#include <daos/event.h>
#include <daos/task.h>
#include "client_internal.h"
#include "task_internal.h"

static struct daos_task_args *
task_ptr2args(tse_task_t *task)
{
	return tse_task_buf_embedded(task, sizeof(struct daos_task_args));
}

static bool
task_is_valid(tse_task_t *task)
{
	return task_ptr2args(task)->ta_magic == DAOS_TASK_MAGIC;
}

/*
 * Task completion CB to complete the high level event. This is used by the
 * event APIs.
 */
static int
task_comp_event(tse_task_t *task, void *data)
{
	D_ASSERT(task_is_valid(task));
	daos_event_complete(task_ptr2args(task)->ta_ev, task->dt_result);
	return 0;
}

/**
 * Create a new task and associate it with the input event. If the event
 * is NULL, the private event will be taken.
 *
 * NB: task created by this function can only be scheduled by calling
 * dc_task_sched_ev(), otherwise the event will never be completed.
 */
int
dc_task_create(tse_task_func_t func, tse_sched_t *sched, daos_event_t *ev,
	       tse_task_t **taskp)
{
	struct daos_task_args *args;
	tse_task_t	      *task;
	int		       rc;

	if (sched == NULL) {
		if (ev == NULL) {
			rc = daos_event_priv_get(&ev);
			if (rc != 0)
				return rc;
		}
		sched = daos_ev2sched(ev);
	}

	rc = tse_task_create(func, sched, NULL, &task);
	if (rc)
		return rc;

	args = task_ptr2args(task);
	args->ta_magic = DAOS_TASK_MAGIC;
	if (ev) {
		/** register a comp cb on the task to complete the event */
		rc = tse_task_register_comp_cb(task, task_comp_event, NULL, 0);
		if (rc != 0)
			D_GOTO(failed, rc);
		args->ta_ev = ev;
	}

	*taskp = task;
	return 0;
 failed:
	tse_task_decref(task);
	return rc;
}

/**
 * Schedule \a task created by \a dc_task_create_ev(), if the associated event
 * of \a task is the private event, this function will wait until completion
 * of the task, otherwise it returns immediately and its completion will be
 * found by testing event or polling on EQ.
 *
 * The task will be executed immediately if \a instant is true.
 */
int
dc_task_schedule(tse_task_t *task, bool instant)
{
	daos_event_t *ev;
	int	      rc;

	D_ASSERT(task_is_valid(task));

	ev = task_ptr2args(task)->ta_ev;
	if (ev) {
		rc = daos_event_launch(ev);
		if (rc) {
			tse_task_complete(task, rc);
			/* error has been reported to event */
			D_GOTO(out, rc = 0);
		}
	}

	rc = tse_task_schedule(task, instant);
	if (rc) {
		/** user is responsible for completing event with error */
		D_GOTO(out, rc = 0); /* error has been reported to event */
	}

out:
	if (daos_event_is_priv(ev)) {
		daos_event_priv_wait();
		rc = ev->ev_error;
	}
	return rc;
}

void
dc_task_list_sched(d_list_t *head, bool instant)
{
	tse_task_t *task;

	while (!d_list_empty(head)) {
		task = tse_task_list_first(head);
		tse_task_list_del(task);
		dc_task_schedule(task, instant);
	}
}

void *
dc_task_get_args(tse_task_t *task)
{
	D_ASSERT(task_is_valid(task));
	return &task_ptr2args(task)->ta_u;
}

void
dc_task_set_opc(tse_task_t *task, uint32_t opc)
{
	task_ptr2args(task)->ta_opc = opc;
}

uint32_t
dc_task_get_opc(tse_task_t *task)
{
	return task_ptr2args(task)->ta_opc;
}

/***************************************************************************
 * Task based interface for all DAOS API
 *
 * NB: event is not required anymore while using task based DAOS API.
 ***************************************************************************/

/**
 * Create a new task for DAOS API
 */
int
daos_task_create(daos_opc_t opc, tse_sched_t *sched, unsigned int num_deps,
		 tse_task_t *dep_tasks[], tse_task_t **taskp)
{
	tse_task_t	*task;
	int		 rc;

	if (dep_tasks && num_deps == 0)
		return -DER_INVAL;

	if (!sched || !taskp)
		return -DER_INVAL;

	if (DAOS_OPC_INVALID >= opc || DAOS_OPC_MAX <= opc)
		return -DER_NOSYS;

	D_ASSERT(dc_funcs[opc].task_func);
	rc = dc_task_create(dc_funcs[opc].task_func, sched, NULL, &task);
	if (rc)
		return rc;

	if (dep_tasks) {
		rc = dc_task_depend(task, num_deps, dep_tasks);
		if (rc)
			D_GOTO(failed, rc);
	}
	*taskp = task;
	return 0;
failed:
	dc_task_decref(task);
	return rc;
}

void *
daos_task_get_args(tse_task_t *task)
{
	return dc_task_get_args(task);
}

void *
daos_task_get_priv(tse_task_t *task)
{
	return dc_task_get_priv(task);
}

void *
daos_task_set_priv(tse_task_t *task, void *priv)
{
	return dc_task_set_priv(task, priv);
}

struct daos_progress_args_t {
	tse_sched_t	*sched;
	bool		*is_empty;
};

static int
sched_progress_cb(void *data)
{
	struct daos_progress_args_t *args = (struct daos_progress_args_t *)data;

	if (tse_sched_check_complete(args->sched)) {
		*args->is_empty = true;
		return 1;
	}

	tse_sched_progress(args->sched);
	return 0;
}

/** Progress all tasks attached on the scheduler */
int
daos_progress(tse_sched_t *sched, int64_t timeout, bool *is_empty)
{
	struct daos_progress_args_t args;
	int rc;

	*is_empty = false;
	tse_sched_progress(sched);

	args.sched = sched;
	args.is_empty = is_empty;

	rc = crt_progress_cond((crt_context_t *)sched->ds_udata, timeout,
			       sched_progress_cb, &args);
	if (rc != 0 && rc != -DER_TIMEDOUT)
		D_ERROR("crt progress failed with "DF_RC"\n", DP_RC(rc));

	return rc;
}

/** Convert a task to CaRT context */
crt_context_t *
daos_task2ctx(tse_task_t *task)
{
	tse_sched_t *sched = tse_task2sched(task);

	D_ASSERT(sched->ds_udata != NULL);
	return (crt_context_t *)sched->ds_udata;
}
