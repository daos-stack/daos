/**
 * (C) Copyright 2016 Intel Corporation.
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
 * This file is part of common DAOS library.
 *
 * common/schedule.c
 *
 * DAOS client will use scheduler/task to manage the asynchronous tasks.
 * Tasks will be attached to one scheduler, when scheduler is executed,
 * it will walk through the task list of the scheduler and pick up those
 * ready tasks to executed.
 */
#include <stdint.h>
#include <pthread.h>
#include <daos/common.h>
#include <daos_event.h>
#include <daos/event.h>
#include <daos/scheduler.h>
#include "client_internal.h"
#include "scheduler_internal.h"

int
daos_sched_init(struct daos_sched *sched, struct daos_event *event)
{
	struct daos_sched_private *dsp = daos_sched2priv(sched);

	D_CASSERT(sizeof(sched->ds_private) >= sizeof(*dsp));

	memset(sched, 0, sizeof(*sched));
	DAOS_INIT_LIST_HEAD(&dsp->dsp_init_list);
	DAOS_INIT_LIST_HEAD(&dsp->dsp_running_list);
	DAOS_INIT_LIST_HEAD(&dsp->dsp_complete_list);
	DAOS_INIT_LIST_HEAD(&dsp->dsp_comp_cb_list);
	DAOS_INIT_LIST_HEAD(&dsp->dsp_dtg_list);
	dsp->dsp_refcount = 1;
	pthread_mutex_init(&dsp->dsp_lock, NULL);

	sched->ds_event = event;
	sched->ds_result = 0;

	return 0;
}

struct daos_sched *
daos_ev2sched(struct daos_event *ev)
{
	return &daos_ev2evx(ev)->evx_sched;
}

void *
daos_task2arg(struct daos_task *task)
{
	return &daos_task2priv(task)->dtp_arg;
}

void *
daos_task2sp(struct daos_task *task)
{
	return &daos_task2priv(task)->dtp_sp;
}

dtp_context_t*
daos_task2ctx(struct daos_task *task)
{
	struct daos_sched_private	*sched_priv;
	struct daos_sched		*sched;

	sched_priv = daos_task2priv(task)->dtp_sched;

	sched = daos_priv2sched(sched_priv);

	return daos_ev2ctx(sched->ds_event);
}

struct daos_sched *
daos_task2sched(struct daos_task *task)
{
	struct daos_sched_private	*sched_priv;
	struct daos_sched		*sched;

	sched_priv = daos_task2priv(task)->dtp_sched;
	sched = daos_priv2sched(sched_priv);

	return sched;
}

/* Assume scheduler is being locked before finish */
static void
daos_sched_fini(struct daos_sched *sched)
{
	struct daos_sched_private	*dsp = daos_sched2priv(sched);
	struct daos_task_private	*dtp;
	struct daos_task_private	*tmp;

	D_ASSERT(daos_list_empty(&dsp->dsp_dtg_list));
	D_ASSERT(daos_list_empty(&dsp->dsp_init_list));
	D_ASSERT(daos_list_empty(&dsp->dsp_running_list));

	daos_list_for_each_entry_safe(dtp, tmp, &dsp->dsp_complete_list,
				      dtp_list) {
		struct daos_task *task = daos_priv2task(dtp);

		daos_list_del_init(&dtp->dtp_list);
		daos_task_fini(task);
		D_FREE_PTR(task);
	}
	pthread_mutex_unlock(&dsp->dsp_lock);
	pthread_mutex_destroy(&dsp->dsp_lock);
}


static inline void
daos_sched_addref_locked(struct daos_sched_private *dsp)
{
	dsp->dsp_refcount++;
}

static inline void
daos_sched_addref(struct daos_sched_private *dsp)
{
	pthread_mutex_lock(&dsp->dsp_lock);
	D_ASSERT(dsp->dsp_refcount >= 0);
	daos_sched_addref_locked(dsp);
	pthread_mutex_unlock(&dsp->dsp_lock);
}

static inline void
daos_sched_decref_locked(struct daos_sched_private *dsp)
{
	D_ASSERT(dsp->dsp_refcount > 1);
	dsp->dsp_refcount--;
}

static inline void
daos_sched_decref(struct daos_sched_private *dsp)
{
	pthread_mutex_lock(&dsp->dsp_lock);
	D_ASSERT(dsp->dsp_refcount > 0);
	if (--dsp->dsp_refcount == 0) {
		daos_sched_fini(daos_priv2sched(dsp));
		return;
	}
	pthread_mutex_unlock(&dsp->dsp_lock);
}

void *
daos_sched_get_inline_dtg(struct daos_sched *sched)
{
	return &daos_sched2priv(sched)->dsp_inline_dtg;
}

int
daos_task_group_init(struct daos_task_group *dtg,
		     struct daos_sched *sched,
		     daos_task_group_cb_t callback,
		     void *arg)
{
	struct daos_sched_private *dsp;

	dsp = daos_sched2priv(sched);

	memset(dtg, 0, sizeof(*dtg));
	DAOS_INIT_LIST_HEAD(&dtg->dtg_task_list);
	DAOS_INIT_LIST_HEAD(&dtg->dtg_list);
	dtg->dtg_comp_cb = callback;
	dtg->dtg_cb_arg = arg;
	pthread_mutex_init(&dtg->dtg_task_list_lock, NULL);

	pthread_mutex_lock(&dsp->dsp_lock);
	/* Make sure the scheduler is not finalized */
	D_ASSERT(sched->ds_event != NULL);
	daos_list_add(&dtg->dtg_list, &dsp->dsp_dtg_list);
	daos_sched_addref_locked(dsp);
	pthread_mutex_unlock(&dsp->dsp_lock);
	return 0;
}

int
daos_task_group_add(struct daos_task_group *dtg,
		    struct daos_task *task)
{
	struct daos_task_private *dtp = daos_task2priv(task);

	D_ASSERT(dtp->dtp_dtg == NULL);
	D_ASSERT(daos_list_empty(&dtp->dtp_dtg_list));
	pthread_mutex_lock(&dtg->dtg_task_list_lock);
	daos_list_add(&dtp->dtp_dtg_list, &dtg->dtg_task_list);
	pthread_mutex_unlock(&dtg->dtg_task_list_lock);
	dtp->dtp_dtg = dtg;

	return 0;
}

int
daos_sched_register_comp_cb(struct daos_sched *sched,
			    daos_sched_comp_cb_t comp_cb, void *arg)
{
	struct daos_sched_private	*dsp = daos_sched2priv(sched);
	struct daos_sched_comp		*dsc;

	D_ALLOC_PTR(dsc);
	if (dsc == NULL)
		return -DER_NOMEM;

	dsc->dsc_comp_cb = comp_cb;
	dsc->dsc_arg = arg;

	pthread_mutex_lock(&dsp->dsp_lock);
	daos_list_add(&dsc->dsc_list,
		      &dsp->dsp_comp_cb_list);
	pthread_mutex_unlock(&dsp->dsp_lock);
	return 0;
}

static int
daos_sched_complete_cb(struct daos_sched *sched)
{
	struct daos_sched_comp		*dsc;
	struct daos_sched_comp		*tmp;
	struct daos_sched_private	*dsp = daos_sched2priv(sched);
	int				rc;

	daos_list_for_each_entry_safe(dsc, tmp,
			&dsp->dsp_comp_cb_list, dsc_list) {
		daos_list_del(&dsc->dsc_list);
		rc = dsc->dsc_comp_cb(dsc->dsc_arg, sched->ds_result);
		if (sched->ds_result == 0)
			sched->ds_result = rc;
		D_FREE_PTR(dsc);
	}
	return 0;
}

/* Schedule the tasks for this schedule */
void
daos_sched_run(struct daos_sched *sched)
{
	struct daos_sched_private	*dsp = daos_sched2priv(sched);
	struct daos_task_private	*dtp;
	struct daos_task_private	*tmp;
	int				rc = 0;

	pthread_mutex_lock(&dsp->dsp_lock);
	daos_list_for_each_entry_safe(dtp, tmp, &dsp->dsp_init_list,
				      dtp_list) {
		/* Move the task to the running list, execute it, then the
		 * task will be moved to the complete list in the complete
		 * callback.
		 **/
		daos_list_move_tail(&dtp->dtp_list,
				    &dsp->dsp_running_list);
		D_ASSERT(dtp->dtp_func != NULL);
		rc = dtp->dtp_func(daos_priv2task(dtp));
		if (sched->ds_result == 0)
			sched->ds_result = rc;
	}
	pthread_mutex_unlock(&dsp->dsp_lock);
}

/* Task completion callback */
int
daos_task_complete_cb(struct daos_task *task)
{
	struct daos_task_private *dtp = daos_task2priv(task);
	struct daos_sched_private *dsp = dtp->dtp_sched;
	struct daos_op_sp *sp = daos_task2sp(task);

	if (sp->sp_callback != NULL) {
		int err;

		err = sp->sp_callback(task, task->dt_result);
		if (task->dt_result == 0)
			task->dt_result = err;
	}

	pthread_mutex_lock(&dsp->dsp_lock);
	/* Check task group first */
	if (dtp->dtp_dtg != NULL) {
		struct daos_task_group	*dtg;
		struct daos_task_group	*tmp;
		daos_list_t		dtg_comp_list;

		DAOS_INIT_LIST_HEAD(&dtg_comp_list);
		daos_list_del_init(&dtp->dtp_dtg_list);
		if (dtp->dtp_dtg->dtg_result == 0)
			dtp->dtp_dtg->dtg_result = task->dt_result;

		daos_list_for_each_entry_safe(dtg, tmp,
					 &dsp->dsp_dtg_list, dtg_list) {
			if (daos_list_empty(&dtg->dtg_task_list))
				daos_list_move_tail(&dtg->dtg_list,
						    &dtg_comp_list);
		}

		if (!daos_list_empty(&dtg_comp_list)) {
			pthread_mutex_unlock(&dsp->dsp_lock);
			daos_list_for_each_entry_safe(dtg, tmp, &dtg_comp_list,
						      dtg_list) {
				D_ASSERT(dtg->dtg_comp_cb != NULL);
				/* It might add new tasks by this callback */
				dtg->dtg_comp_cb(dtg->dtg_cb_arg);
				daos_list_del_init(&dtg->dtg_list);
				daos_sched_decref_locked(dsp);
			}
			pthread_mutex_lock(&dsp->dsp_lock);
		}
	}

	/* Then check sched */
	daos_list_move_tail(&dtp->dtp_list, &dsp->dsp_complete_list);
	if (daos_priv2sched(dsp)->ds_result == 0)
		daos_priv2sched(dsp)->ds_result = task->dt_result;

	/* check if all sub tasks are done, then complete the event */
	if (daos_list_empty(&dsp->dsp_running_list) &&
	    daos_list_empty(&dsp->dsp_init_list)) {
		struct daos_sched *sched = daos_priv2sched(dsp);

		/* drop reference of daos_sched_init() */
		daos_sched_decref_locked(dsp);
		daos_sched_complete_cb(sched);
		if (sched->ds_event != NULL) {
			daos_event_complete(sched->ds_event, sched->ds_result);
			sched->ds_event = NULL;
		}
	}
	pthread_mutex_unlock(&dsp->dsp_lock);
	daos_sched_decref(dsp);

	return task->dt_result;
}

/* Cancel all tasks on the scheduler */
void
daos_sched_cancel(struct daos_sched *sched, int ret)
{
	struct daos_sched_private	*dsp = daos_sched2priv(sched);
	struct daos_task_private	*dtp;
	struct daos_task_private	*tmp;
	daos_list_t			list;

	DAOS_INIT_LIST_HEAD(&list);

	pthread_mutex_lock(&dsp->dsp_lock);
	daos_list_for_each_entry_safe(dtp, tmp, &dsp->dsp_init_list,
				      dtp_list)
		daos_list_move_tail(&dtp->dtp_list, &list);
	pthread_mutex_unlock(&dsp->dsp_lock);

	daos_list_for_each_entry_safe(dtp, tmp, &list, dtp_list) {
		struct daos_task *task = daos_priv2task(dtp);

		if (task->dt_result == 0)
			task->dt_result = ret;

		/* task will be moved to complete list */
		daos_task_complete_cb(task);
	}
	daos_sched_decref(dsp);
}

int
daos_task_init(struct daos_task *task,
	       daos_task_func_t task_func,
	       struct daos_sched *sched)
{
	struct daos_task_private *dtp = daos_task2priv(task);
	struct daos_sched_private *dsp = daos_sched2priv(sched);

	D_CASSERT(sizeof(task->dt_private) >= sizeof(*dtp));
	memset(task, 0, sizeof(*task));

	task->dt_comp_cb = daos_task_complete_cb;

	DAOS_INIT_LIST_HEAD(&dtp->dtp_dtg_list);
	dtp->dtp_func = task_func;

	task->dt_result = 0;

	pthread_mutex_lock(&dsp->dsp_lock);
	D_ASSERT(sched->ds_event != NULL);
	daos_list_add_tail(&dtp->dtp_list, &dsp->dsp_init_list);
	daos_sched_addref_locked(dsp);
	dtp->dtp_sched = dsp;
	pthread_mutex_unlock(&dsp->dsp_lock);

	return 0;
}

void
daos_task_fini(struct daos_task *task)
{
	struct daos_task_private *dtp;

	dtp = daos_task2priv(task);

	D_ASSERT(daos_list_empty(&dtp->dtp_list));
}
