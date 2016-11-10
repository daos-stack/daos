/**
 * (C) Copyright 2015, 2016 Intel Corporation.
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
 * DAOs task/scheduler structure API, which are used to track the asynchronous
 * operations on client side, RPC etc. See src/obj/cli_obj.c as an example.
 */

#ifndef __DAOS_SCHEDULE_H__
#define __DAOS_SCHEDULE_H__

#include <daos_types.h>
#include <daos_errno.h>
#include <daos/list.h>
#include <daos/hash.h>
#include <daos/event.h>
#include <daos/rpc.h>

typedef int (*daos_task_func_t)(struct daos_task *);
typedef int (*daos_task_comp_cb_t)(struct daos_task *, void *arg);
typedef int (*daos_task_result_cb_t)(struct daos_task *, void *arg);

/**
 * daos_task is used to track single asynchronous operation.
 **/
struct daos_task {
	int			dt_result;
	/* daos schedule internal */
	struct {
		uint64_t	dt_space[60];
	}			dt_private;
};

/**
 * Track all of the tasks under a scheduler.
 **/
struct daos_sched {
	int		ds_result;

	/* the event associated with the scheduler */
	daos_event_t	*ds_event;

	/* Linked to the executed list */
	daos_list_t	ds_list;

	/* daos schedule internal */
	struct {
		uint64_t	ds_space[48];
	}			ds_private;
};
typedef int (*daos_sched_comp_cb_t)(void *args, int rc);

struct daos_sched *
daos_ev2sched(struct daos_event *ev);

void *
daos_task2arg(struct daos_task *task);

void *
daos_task2sp(struct daos_task *task);

crt_context_t *
daos_task2ctx(struct daos_task *task);

struct daos_sched *
daos_task2sched(struct daos_task *task);

/**
 * Initialize the daos_task.
 *
 * The task will be added to the scheduler task list, and
 * being scheduled later, if dependent task is provided, then
 * the task will be added to the dep list of the dependent
 * task, once the dependent task is done, then the task will
 * be added to the scheduler list.
 *
 * \param task [input]		daos_task to be initialized.
 * \param task_func [input]	the function to be executed when
 *                              the task is executed.
 * \param arg [input]		the task_func argument.
 * \param arg_size [input]	the task_func argument size.
 * \param sched [input]		daos scheduler where the daos
 *                              task will be attached to.
 * \param dependent [input]	task which this task dependents on
 *
 * \return			0  if initialization succeeds.
 * \return			negative errno if it fails.
 */
int
daos_task_init(struct daos_task *task, daos_task_func_t task_func,
	       void *arg, int arg_size, struct daos_sched *sched,
	       struct daos_task *dependent);

/**
 *  Initialized the scheduler, if the scheduler is launched after
 *  daos_sched_run(sched), then  it will be finalized after all
 *  tasks has been finished (see daos_task_complete_cb()),
 *  otherwise it needs to call daos_sched_cancel() to finalize
 *  itself.
 *
 * \param shced [input]	scheduler to be initialized.
 *
 * \param ev [input]	event associated with the scheduler, which will
 *                      be launched when the scheduler is ready(added
 *                      to the scheduler list), and completed when all
 *                      of tasks of the scheduler are completed.
 *
 * \return	0 if initialization succeeds.
 * \return	negative errno if initialization fails.
 */
int
daos_sched_init(struct daos_sched *sched, daos_event_t *ev);

/**
 * register complete callback for scheduler.
 *
 * \param sched [input]		scheduler where to register the completion
 *                              callback.
 * \param comp_cb [input]	completion callback to be registered.
 * \param arg [input]		argument of the completion callback.
 *
 * \return			0 if registeration succeeds.
 * \return			errno if registeration fails.
 */
int
daos_sched_register_comp_cb(struct daos_sched *sched,
			    daos_sched_comp_cb_t comp_cb, void *arg);

/**
 * register complete callback for the task.
 *
 * \param task [input]		task to be registered complete callback.
 * \param comp_cb [input]	complete callback.
 * \param arg [input]		callback argument.
 *
 * \return		0 if register succeeds.
 * \return		negative errno if it fails.
 */
int
daos_task_register_comp_cb(struct daos_task *task,
			   daos_task_comp_cb_t comp_cb,
			   void *arg);
/**
 * run all of tasks of the scheduler.
 *
 * \param sched [input] scheduler whose tasks will be executed.
 */
void
daos_sched_run(struct daos_sched *sched);

/**
 * cancel all of tasks in the scheduler.
 *
 * Cancel all of tasks of the scheduler.
 *
 * \param sched [input] scheduler to be canceled.
 * \param ret   [input] result of the scheduler.
 */
void
daos_sched_cancel(struct daos_sched *sched, int ret);

/**
 * complete daos_task.
 *
 * Mark the task to be completed.
 *
 * \param task [input]	task to be completed.
 * \param ret [input]	ret result of the task.
 **/
void
daos_task_complete(struct daos_task *task, int ret);

/**
 * Add dependent task
 *
 * If one task depends on other tasks, only if all of its dependent
 * tasks finish, then the task can be scheduled.
 *
 * param task [in]	task which depends on dep task(@dep).
 * param dep [in]	dependent task which the task depends on.
 *
 * return		0 if adding dependent succeeds.
 * return		errno if adding dependent fails.
 **/
int
daos_task_add_dependent(struct daos_task *task, struct daos_task *dep);

/**
 * Process the result tasks.
 *
 * After one task finish, if it has dependent task, then this task will
 * be added to the result task list of its dependent task, in case the
 * dependent task might check this task result later. This function will
 * walk through the result task list and call the callback for each task.
 *
 * \param task	[in]	task of its result tasks to be called callback.
 * \param callback [in]	callback to be called for each task.
 * \param arg [in]	argument of the callback.
 **/
void
daos_task_result_process(struct daos_task *task, daos_task_result_cb_t callback,
			 void *arg);

/**
 * Get a buffer from task.
 *
 * Get a buffer from task internal buffer pool.
 *
 * \param task [in] task to get the buffer.
 * \param task [in] task buffer size.
 *
 * \return	pointer to the buffer.
 **/
void *
daos_task_buf_get(struct daos_task *task, int buf_size);
#endif
