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
 * common/scheduler.c
 *
 * DAOS client will use scheduler/task to manage the asynchronous tasks.
 * Tasks will be attached to one scheduler, when scheduler is executed,
 * it will walk through the task list of the scheduler and pick up those
 * ready tasks to executed.
 */
#define DD_SUBSYS	DD_FAC(client)

#include <stdint.h>
#include <pthread.h>
#include <daos/common.h>
#include <daos/scheduler.h>
#include "scheduler_internal.h"

struct daos_task_link {
	daos_list_t		 tl_link;
	struct daos_task	*tl_task;
};

static void daos_sched_decref(struct daos_sched_private *dsp);

int
daos_sched_init(struct daos_sched *sched, daos_sched_comp_cb_t comp_cb,
		void *udata)
{
	struct daos_sched_private *dsp = daos_sched2priv(sched);
	int rc;

	D_CASSERT(sizeof(sched->ds_private) >= sizeof(*dsp));

	memset(sched, 0, sizeof(*sched));

	DAOS_INIT_LIST_HEAD(&sched->ds_list);
	DAOS_INIT_LIST_HEAD(&dsp->dsp_init_list);
	DAOS_INIT_LIST_HEAD(&dsp->dsp_running_list);
	DAOS_INIT_LIST_HEAD(&dsp->dsp_complete_list);
	DAOS_INIT_LIST_HEAD(&dsp->dsp_comp_cb_list);

	dsp->dsp_refcount = 1;
	dsp->dsp_inflight = 0;
	pthread_mutex_init(&dsp->dsp_lock, NULL);

	if (comp_cb != NULL) {
		rc = daos_sched_register_comp_cb(sched, comp_cb, udata);
		if (rc != 0)
			return rc;
	}

	sched->ds_udata = udata;
	sched->ds_result = 0;

	return 0;
}

void *
daos_task2arg(struct daos_task *task)
{
	return daos_task2priv(task)->dtp_func_arg;
}

static inline uint32_t
daos_task_buf_size(int size)
{
	return (size + 7) & ~0x7;
}

void *
daos_task_buf_get(struct daos_task *task, int size)
{
	struct daos_task_private *dtp = daos_task2priv(task);
	void *ptr;

	/* Let's assume dtp_buf is always enough at the moment */
	D_ASSERTF(dtp->dtp_buf.dtp_buf_size + daos_task_buf_size(size) <
					sizeof(dtp->dtp_buf),
		  "size %u req size %u all size %lu\n",
		  dtp->dtp_buf.dtp_buf_size, daos_task_buf_size(size),
		  sizeof(dtp->dtp_buf));
	ptr = (void *)&dtp->dtp_buf + dtp->dtp_buf.dtp_buf_size;
	dtp->dtp_buf.dtp_buf_size += daos_task_buf_size(size);
	return ptr;
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

static void
daos_task_addref_locked(struct daos_task_private *dtp)
{
	dtp->dtp_refcnt++;
}

static bool
daos_task_decref_locked(struct daos_task_private *dtp)
{
	D_ASSERT(dtp->dtp_refcnt > 0);
	dtp->dtp_refcnt--;
	return dtp->dtp_refcnt == 0;
}

static void
daos_task_decref(struct daos_task *task)
{
	struct daos_task_private  *dtp = daos_task2priv(task);
	struct daos_sched_private *dsp = dtp->dtp_sched;
	bool			   zombie;

	D_ASSERT(dsp != NULL);

	pthread_mutex_lock(&dsp->dsp_lock);
	zombie = daos_task_decref_locked(dtp);
	pthread_mutex_unlock(&dsp->dsp_lock);
	if (!zombie)
		return;

	while (!daos_list_empty(&dtp->dtp_ret_list)) {
		struct daos_task_link *result;

		result = daos_list_entry(dtp->dtp_ret_list.next,
					 struct daos_task_link, tl_link);
		daos_list_del(&result->tl_link);
		daos_task_decref(result->tl_task);
		D_FREE_PTR(result);
	}

	D_ASSERT(daos_list_empty(&dtp->dtp_dep_list));
	daos_sched_decref(dsp);

	D_FREE_PTR(task);
}

static void
daos_sched_fini(struct daos_sched *sched)
{
	struct daos_sched_private *dsp = daos_sched2priv(sched);

	D_ASSERT(dsp->dsp_inflight == 0);
	D_ASSERT(daos_list_empty(&dsp->dsp_init_list));
	D_ASSERT(daos_list_empty(&dsp->dsp_running_list));
	D_ASSERT(daos_list_empty(&dsp->dsp_complete_list));
	pthread_mutex_destroy(&dsp->dsp_lock);
}

static inline void
daos_sched_addref_locked(struct daos_sched_private *dsp)
{
	dsp->dsp_refcount++;
}

static void
daos_sched_decref(struct daos_sched_private *dsp)
{
	bool	finalize;

	pthread_mutex_lock(&dsp->dsp_lock);

	D_ASSERT(dsp->dsp_refcount > 0);
	dsp->dsp_refcount--;
	finalize = dsp->dsp_refcount == 0;

	pthread_mutex_unlock(&dsp->dsp_lock);

	if (finalize)
		daos_sched_fini(daos_priv2sched(dsp));
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

/* Mark the tasks to complete */
static void
daos_task_complete_locked(struct daos_task_private *dtp,
			  struct daos_sched_private *dsp)
{
	if (dtp->dtp_complete)
		return;

	D_ASSERT(dtp->dtp_running);
	dtp->dtp_running = 0;
	dtp->dtp_complete = 1;
	daos_list_move_tail(&dtp->dtp_list, &dsp->dsp_complete_list);
}

/* register the callback of the task */
int
daos_task_register_comp_cb(struct daos_task *task,
			   daos_task_comp_cb_t comp_cb, void *arg)
{
	struct daos_task_private *dtp = daos_task2priv(task);
	struct daos_task_comp_cb *dtc;

	D_ALLOC_PTR(dtc);
	if (dtc == NULL)
		return -DER_NOMEM;

	dtc->dtc_comp_cb = comp_cb;
	dtc->dtc_arg = arg;

	D_ASSERT(dtp->dtp_sched != NULL);

	pthread_mutex_lock(&dtp->dtp_sched->dsp_lock);
	daos_list_add(&dtc->dtc_list, &dtp->dtp_comp_cb_list);
	pthread_mutex_unlock(&dtp->dtp_sched->dsp_lock);
	return 0;
}

/* Execute the callback of the task */
static int
daos_task_complete_callback(struct daos_task *task)
{
	struct daos_task_private *dtp = daos_task2priv(task);
	struct daos_task_comp_cb *dtc;
	struct daos_task_comp_cb *tmp;
	int rc;

	daos_list_for_each_entry_safe(dtc, tmp,
			&dtp->dtp_comp_cb_list, dtc_list) {
		daos_list_del(&dtc->dtc_list);
		rc = dtc->dtc_comp_cb(task, dtc->dtc_arg);
		if (task->dt_result == 0)
			task->dt_result = rc;
		D_FREE_PTR(dtc);
	}

	return 0;
}

/**
 * Walk through the result task list and execute callback for each
 * task.
 **/
void
daos_task_result_process(struct daos_task *task,
			 daos_task_result_cb_t callback, void *arg)
{
	struct daos_task_private *dtp = daos_task2priv(task);
	struct daos_task_link   *result;

	daos_list_for_each_entry(result, &dtp->dtp_ret_list, tl_link)
		callback(result->tl_task, arg);
}

/* Process the task in the init list of the scheduler */
static int
daos_sched_process_init(struct daos_sched_private *dsp)
{
	struct daos_task_private *dtp;
	struct daos_task_private *tmp;
	daos_list_t		  list;
	int			  processed = 0;

	DAOS_INIT_LIST_HEAD(&list);
	pthread_mutex_lock(&dsp->dsp_lock);
	daos_list_for_each_entry_safe(dtp, tmp, &dsp->dsp_init_list,
				      dtp_list) {
		if (dtp->dtp_dep_cnt == 0 || dsp->dsp_cancelling) {
			daos_list_move_tail(&dtp->dtp_list, &list);
			dsp->dsp_inflight++;
		}
	}
	pthread_mutex_unlock(&dsp->dsp_lock);

	while (!daos_list_empty(&list)) {
		struct daos_task *task;
		int		  rc;

		dtp = daos_list_entry(list.next,
				      struct daos_task_private, dtp_list);

		pthread_mutex_lock(&dsp->dsp_lock);
		if (dsp->dsp_cancelling) {
			daos_task_complete_locked(dtp, dsp);
		} else {
			dtp->dtp_running = 1;
			daos_list_move_tail(&dtp->dtp_list,
					    &dsp->dsp_running_list);
		}
		pthread_mutex_unlock(&dsp->dsp_lock);

		if (!dsp->dsp_cancelling) {
			task = daos_priv2task(dtp);
			D_ASSERT(dtp->dtp_func != NULL);
			rc = dtp->dtp_func(task);
			if (task->dt_result == 0)
				task->dt_result = rc;
		}
		processed++;
	}
	return processed;
}

/**
 * Check the task in the complete list, dependent task
 * status check, schedule status update etc. The task
 * will be moved to fini list after this
 **/
static int
daos_task_post_process(struct daos_task *task)
{
	struct daos_task_private  *dtp = daos_task2priv(task);
	struct daos_sched_private *dsp = dtp->dtp_sched;
	int rc = 0;

	D_ASSERT(dtp->dtp_complete == 1);

	/* set scheduler result */
	if (daos_priv2sched(dsp)->ds_result == 0)
		daos_priv2sched(dsp)->ds_result = task->dt_result;

	/* Check dependent list */
	pthread_mutex_lock(&dsp->dsp_lock);
	while (!daos_list_empty(&dtp->dtp_dep_list)) {
		struct daos_task_link	  *tlink;
		struct daos_task_private  *dtp_tmp;

		tlink = daos_list_entry(dtp->dtp_dep_list.next,
					struct daos_task_link, tl_link);
		daos_list_del(&tlink->tl_link);
		dtp_tmp = daos_task2priv(tlink->tl_task);

		/* see if the dependent task is ready to be scheduled */
		D_ASSERT(dtp_tmp->dtp_dep_cnt > 0);
		dtp_tmp->dtp_dep_cnt--;
		D_DEBUG(DB_TRACE, "daos task %p dep_cnt %d\n", dtp_tmp,
			dtp_tmp->dtp_dep_cnt);
		if (dtp_tmp->dtp_dep_cnt == 0 && !dsp->dsp_cancelling) {
			/* If the task is already running, let's mark it
			 * complete.
			 **/
			if (dtp_tmp->dtp_running)
				daos_task_complete_locked(dtp_tmp, dsp);
		}

		if (!dsp->dsp_cancelling) {
			/* let's attach the current task to the dependent task,
			 * in case the dependent task needs to check the result
			 * of these tasks.
			 *
			 * NB: reuse tlink.
			 */
			daos_task_addref_locked(dtp);
			tlink->tl_task = task;
			daos_list_add_tail(&tlink->tl_link,
					   &dtp_tmp->dtp_ret_list);
		} else {
			D_FREE_PTR(tlink);
		}

		/* -1 for tlink */
		daos_task_decref_locked(dtp_tmp);
	}

	D_ASSERT(dsp->dsp_inflight > 0);
	dsp->dsp_inflight--;
	pthread_mutex_unlock(&dsp->dsp_lock);

	if (task->dt_result == 0)
		task->dt_result = rc;

	return rc;
}

int
daos_sched_process_complete(struct daos_sched_private *dsp)
{
	struct daos_task_private *dtp;
	struct daos_task_private *tmp;
	daos_list_t comp_list;
	int processed = 0;

	/* pick tasks from complete_list */
	DAOS_INIT_LIST_HEAD(&comp_list);
	pthread_mutex_lock(&dsp->dsp_lock);
	daos_list_splice_init(&dsp->dsp_complete_list, &comp_list);
	pthread_mutex_unlock(&dsp->dsp_lock);

	daos_list_for_each_entry_safe(dtp, tmp, &comp_list, dtp_list) {
		struct daos_task *task = daos_priv2task(dtp);

		daos_task_complete_callback(task);
		daos_task_post_process(task);
		daos_list_del_init(&dtp->dtp_list);
		daos_task_decref(task);  /* drop final ref */
		processed++;
	}
	return processed;
}

static bool
daos_sched_check_complete(struct daos_sched_private *dsp)
{
	struct daos_sched *sched;
	bool		   completed;

	/* check if all sub tasks are done, then complete the event */
	pthread_mutex_lock(&dsp->dsp_lock);
	completed = (daos_list_empty(&dsp->dsp_init_list) &&
		     dsp->dsp_inflight == 0);

	if (!completed) {
		pthread_mutex_unlock(&dsp->dsp_lock);
		return false;
	}

	if (dsp->dsp_completing) {
		pthread_mutex_unlock(&dsp->dsp_lock);
		return true;
	}

	dsp->dsp_completing = 1;
	pthread_mutex_unlock(&dsp->dsp_lock);

	sched = daos_priv2sched(dsp);
	daos_sched_complete_cb(sched);
	sched->ds_udata = NULL;

	/* drop reference of daos_sched_init() */
	daos_sched_decref(dsp);

	return true;
}

/* Run tasks for this schedule */
static void
daos_sched_run(struct daos_sched *sched)
{
	struct daos_sched_private *dsp = daos_sched2priv(sched);

	while (1) {
		int	processed = 0;
		bool	completed;

		processed += daos_sched_process_init(dsp);
		processed += daos_sched_process_complete(dsp);
		completed = daos_sched_check_complete(dsp);
		if (completed || processed == 0)
			break;
	};

}

/* Cancel tasks for this schedule */
void
daos_sched_cancel(struct daos_sched *sched, int ret)
{
	struct daos_sched_private *dsp = daos_sched2priv(sched);

	if (sched->ds_result == 0)
		sched->ds_result = ret;

	pthread_mutex_lock(&dsp->dsp_lock);
	if (dsp->dsp_cancelling) {
		pthread_mutex_unlock(&dsp->dsp_lock);
		return;
	}
	dsp->dsp_cancelling = 1;
	daos_sched_addref_locked(dsp); /* +1 for daos_sched_run */
	pthread_mutex_unlock(&dsp->dsp_lock);

	/* Wait until all inflight task has been cancelled */
	while (1) {
		daos_sched_run(sched);
		if (dsp->dsp_inflight == 0)
			break;
	};
	daos_sched_decref(dsp);
}

void
daos_task_complete(struct daos_task *task, int ret)
{
	struct daos_task_private	*dtp	= daos_task2priv(task);
	struct daos_sched_private	*dsp	= dtp->dtp_sched;
	struct daos_sched		*sched	= daos_task2sched(task);

	if (task->dt_result == 0)
		task->dt_result = ret;

	pthread_mutex_lock(&dsp->dsp_lock);
	/* +1 for daos_sched_run() */
	daos_sched_addref_locked(dsp);

	if (!dsp->dsp_cancelling)
		daos_task_complete_locked(dtp, dsp);
	else
		daos_task_decref_locked(dtp);
	pthread_mutex_unlock(&dsp->dsp_lock);

	/* Let's run sched to process the complete task */
	if (!dsp->dsp_cancelling)
		daos_sched_run(sched);

	daos_sched_decref(dsp);
}

/**
 * If one task dependents on other tasks, only if the dependent task
 * is done, then the task can be added to the scheduler list
 **/
int
daos_task_add_dependent(struct daos_task *task, struct daos_task *dep)
{
	struct daos_task_private  *dtp = daos_task2priv(task);
	struct daos_task_private  *dep_dtp = daos_task2priv(dep);
	struct daos_task_link	  *tlink;

	if (dtp->dtp_sched != dep_dtp->dtp_sched) {
		D_ERROR("Two tasks should belong to the same scheduler.\n");
		return -DER_NO_PERM;
	}

	D_ALLOC_PTR(tlink);
	if (tlink == NULL)
		return -DER_NOMEM;

	D_DEBUG(DB_TRACE, "Add dependent %p ---> %p\n", dep_dtp, dtp);

	pthread_mutex_lock(&dtp->dtp_sched->dsp_lock);

	daos_task_addref_locked(dtp);
	tlink->tl_task = task;

	daos_list_add_tail(&tlink->tl_link, &dep_dtp->dtp_dep_list);
	dtp->dtp_dep_cnt++;

	pthread_mutex_unlock(&dtp->dtp_sched->dsp_lock);

	return 0;
}

int
daos_task_init(struct daos_task *task, daos_task_func_t task_func,
	       void *arg, int arg_size, struct daos_sched *sched,
	       struct daos_task *dependent)
{
	struct daos_task_private  *dtp = daos_task2priv(task);
	struct daos_sched_private *dsp = daos_sched2priv(sched);

	D_CASSERT(sizeof(task->dt_private) >= sizeof(*dtp));
	memset(task, 0, sizeof(*task));

	DAOS_INIT_LIST_HEAD(&dtp->dtp_list);
	DAOS_INIT_LIST_HEAD(&dtp->dtp_dep_list);
	DAOS_INIT_LIST_HEAD(&dtp->dtp_comp_cb_list);
	DAOS_INIT_LIST_HEAD(&dtp->dtp_ret_list);
	dtp->dtp_refcnt = 1;

	dtp->dtp_func = task_func;
	if (arg != NULL) {
		dtp->dtp_func_arg = daos_task_buf_get(task, arg_size);
		D_ASSERT(dtp->dtp_func_arg != NULL);
		memcpy(dtp->dtp_func_arg, arg, arg_size);
	}

	/* Add task to scheduler */
	pthread_mutex_lock(&dsp->dsp_lock);
	dtp->dtp_sched = dsp;
	if (dtp->dtp_func == NULL) {
		/* If dtp_func == NULL, it means it will not rely on
		 * daos_sched_process_init() to execute it, i.e. it
		 * is inflight already, see those tasks created in
		 * client/object.c
		 **/
		dsp->dsp_inflight++;
		dtp->dtp_running = 1;
		daos_list_add_tail(&dtp->dtp_list, &dsp->dsp_running_list);
	} else {
		daos_list_add_tail(&dtp->dtp_list, &dsp->dsp_init_list);
	}
	daos_sched_addref_locked(dsp);
	pthread_mutex_unlock(&dsp->dsp_lock);

	if (dependent != NULL)
		/* If there is dependent, only add it to the dependent list */
		daos_task_add_dependent(task, dependent);

	return 0;
}
