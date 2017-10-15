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

#define DDSUBSYS	DDFAC(client)

#include <daos/common.h>
#include <daos/event.h>
#include "client_internal.h"
#include "task_internal.h"

static struct daos_task_args *
task_ptr2args(tse_task_t *task)
{
	return (struct daos_task_args *)tse_task2arg(task);
}

int
daos_task_create(daos_opc_t opc, tse_sched_t *sched, void *op_args,
		 unsigned int num_deps, tse_task_t *dep_tasks[],
		 tse_task_t **taskp)
{
	struct daos_task_args	*args;
	tse_task_t		*task;
	int			 rc;

	if (DAOS_OPC_INVALID >= opc || DAOS_OPC_MAX <= opc)
		return -DER_NOSYS;

	rc = tse_task_init(dc_funcs[opc].task_func, NULL, 0, sched, &task);
	if (rc != 0)
		return rc;

	args = task_ptr2args(task);
	args->opc = opc;
	if (op_args) {
		D__ASSERT(sizeof(args->op_args) >= dc_funcs[opc].arg_size);
		memcpy(&args->op_args, op_args, dc_funcs[opc].arg_size);
	}

	if (num_deps && dep_tasks)
		rc = tse_task_register_deps(task, num_deps, dep_tasks);

	if (rc == 0)
		*taskp = task;

	return rc;
}

void *
daos_task_get_args(daos_opc_t opc, tse_task_t *task)
{
	struct daos_task_args *task_arg;

	task_arg = tse_task_buf_get(task, sizeof(*task_arg));

	if (task_arg->opc != opc) {
		D__DEBUG(DB_ANY, "OPC does not match task's OPC\n");
		return NULL;
	}

	return &task_arg->op_args;
}

void *
daos_task_get_priv(tse_task_t *task)
{
	struct daos_task_args *task_arg;

	task_arg = tse_task_buf_get(task, sizeof(*task_arg));
	return task_arg->priv;
}

void
daos_task_set_priv(tse_task_t *task, void *priv)
{
	struct daos_task_args *task_arg;

	task_arg = tse_task_buf_get(task, sizeof(*task_arg));
	task_arg->priv = priv;
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

int
daos_progress(tse_sched_t *sched, int64_t timeout, bool *is_empty)
{
	struct daos_progress_args_t args;
	int rc;

	*is_empty = false;
	tse_sched_progress(sched);

	args.sched = sched;
	args.is_empty = is_empty;

	rc = crt_progress((crt_context_t *)sched->ds_udata, timeout,
			  sched_progress_cb, &args);
	if (rc != 0 && rc != -DER_TIMEDOUT)
		D__ERROR("crt progress failed with %d\n", rc);

	return rc;
}

/*****************************************************************************
 * Below are DAOS Client (DC) private API, they are built on top of public
 * DAOS task API
 ****************************************************************************/

/*
 * Task completion CB to complete the high level event. This is used by the
 * event APIs.
 */
static int
task_comp_cb(tse_task_t *task, void *data)
{
	int	rc = task->dt_result;

	daos_event_complete(task_ptr2args(task)->ta_ev, rc);
	return rc;
}

/**
 * Deprecated function, create and schedule a task for DAOS API.
 * Please use dc_task_new() and dc_task_schedule() instead of this.
 */
int
dc_task_create(daos_opc_t opc, void *arg, int arg_size,
	       tse_task_t **taskp, daos_event_t **evp)
{
	daos_event_t *ev = *evp;
	tse_task_t *task = NULL;
	int rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
		*evp = ev;
	}

	rc = daos_task_create(opc, daos_ev2sched(ev), arg, 0, NULL, &task);
	if (rc != 0)
		return rc;

	/** register a comp cb on the task to complete the event */
	rc = tse_task_register_comp_cb(task, task_comp_cb, NULL, 0);
	if (rc != 0)
		D__GOTO(err_task, rc);

	task_ptr2args(task)->ta_ev = ev;
	rc = daos_event_launch(ev);
	if (rc != 0)
		D__GOTO(err_task, rc);

	rc = tse_task_schedule(task, true);
	if (rc != 0)
		return rc;

	*taskp = task;

	return rc;

err_task:
	D__FREE_PTR(task);
	return rc;
}

/**
 * Create a new task and associate it with the input event. If the event
 * is NULL, the private event will be taken.
 */
int
dc_task_new(daos_opc_t opc, daos_event_t *ev, tse_task_t **taskp)
{
	tse_task_t *task;
	int	    rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
	}

	rc = daos_task_create(opc, daos_ev2sched(ev), NULL, 0, NULL, &task);
	if (rc)
		return rc;

	/** register a comp cb on the task to complete the event */
	rc = tse_task_register_comp_cb(task, task_comp_cb, NULL, 0);
	if (rc != 0)
		D__GOTO(failed, rc);

	task_ptr2args(task)->ta_ev = ev;
	*taskp = task;
	return 0;
 failed:
	tse_task_decref(task);
	return rc;
}

/**
 * Schedule \a task, if the associated event of \a task is the private event,
 * this function will wait until the completion of the task, otherwise it
 * returns immediately.
 */
int
dc_task_schedule(tse_task_t *task)
{
	daos_event_t *ev = task_ptr2args(task)->ta_ev;
	int	      rc;

	rc = daos_event_launch(ev);
	if (rc)
		D__GOTO(out, rc);

	rc = tse_task_schedule(task, true);
	if (rc)
		D__GOTO(out, rc);

	D_EXIT;
 out:
	if (rc) {
		tse_task_complete(task, rc);
		rc = 0; /* error has been reported via ev_error */
	}

	if (daos_event_is_priv(ev)) {
		daos_event_priv_wait();
		rc = ev->ev_error;
	}
	return rc;
}

/* XXX Functions below should be reviewed and cleaned up */

/** Convert a task to CaRT context */
crt_context_t *
daos_task2ctx(tse_task_t *task)
{
	tse_sched_t *sched = tse_task2sched(task);

	D__ASSERT(sched->ds_udata != NULL);
	return (crt_context_t *)sched->ds_udata;
}

/**
 * The daos client internal will use tse_task, this function will
 * initialize the daos task/scheduler from event, and launch
 * event. This is a convenience function used in the event APIs.
 */
int
daos_client_task_prep(void *arg, int arg_size, tse_task_t **taskp,
		      daos_event_t **evp)
{
	daos_event_t *ev = *evp;
	tse_task_t *task = NULL;
	int rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
		*evp = ev;
	}

	rc = tse_task_init(NULL, arg, arg_size, daos_ev2sched(ev), &task);
	if (rc != 0)
		return rc;

	rc = tse_task_register_comp_cb(task, task_comp_cb, &ev,
				       sizeof(ev));
	if (rc != 0)
		D__GOTO(err_task, rc);

	rc = daos_event_launch(ev);
	if (rc != 0)
		D__GOTO(err_task, rc);

	rc = tse_task_schedule(task, false);
	if (rc != 0)
		return rc;

	*taskp = task;

	return rc;

err_task:
	D__FREE_PTR(task);
	return rc;
}
