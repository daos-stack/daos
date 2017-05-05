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

#include <daos/list.h>

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

	/* user data associated with the scheduler (completion cb data, etc.) */
	void		*ds_udata;

	/* Linked to the executed list */
	daos_list_t	ds_list;

	/* daos schedule internal */
	struct {
		uint64_t	ds_space[48];
	}			ds_private;
};

typedef int (*daos_sched_comp_cb_t)(void *args, int rc);
typedef int (*daos_task_func_t)(struct daos_task *);

/** MSC - deprecate both function defs */
typedef int (*daos_task_comp_cb_t)(struct daos_task *, void *arg);
typedef int (*daos_task_result_cb_t)(struct daos_task *, void *arg);
/** CB type for prepare, completion, and result processing */
typedef int (*daos_task_cb_t)(struct daos_task *, void *arg);

void *
daos_task2arg(struct daos_task *task);

void *
daos_task2sp(struct daos_task *task);

struct daos_sched *
daos_task2sched(struct daos_task *task);

/**
 *  Initialize the scheduler with an optional completion callback and pointer to
 *  user data. Caller is responsible to complete or cancel the scheduler.
 *
 * \param sched [input]		scheduler to be initialized.
 * \param comp_cb [input]	Optional callback to be called when scheduler
 *				is done.
 * \param udata [input]		Optional pointer to user data to associate with
 *				the scheduler. This is stored in ds_udata in the
 *				scheduler struct and passed in to comp_cb as the
 *				argument when the callback is invoked.
 *
 * \return			0 if initialization succeeds.
 * \return			negative errno if initialization fails.
 */
int
daos_sched_init(struct daos_sched *sched, daos_sched_comp_cb_t comp_cb,
		void *udata);

/**
 * Finish the scheduler.
 *
 * \param sched [input]		the scheduler to be finished.
 */
void
daos_sched_fini(struct daos_sched *sched);

/**
 * Wait for all tasks in the scheduler to complete and finalize it.
 * If another thread is completing the scheduler, this returns immediately.
 *
 * \param sched	[input]	scheduler to be completed.
 * \param ret	[input]	result for scheduler completion.
 * \param cancel [input]
 *			cancel all tasks in scheduler if true.
 */
void
daos_sched_complete(struct daos_sched *sched, int ret, bool cancel);

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
 * Make progress on scheduler. Runs tasks that are ready to be executed after
 * the tasks they depend on were completed. Note that task completion using
 * daos_task_complete() must be done by the engine user to push progress on
 * the engine. In DAOS daos_task_complete is called by the completion CB of the
 * RPC request that is sent to the server.
 *
 * \param sched	[IN]	Scheduler to make progress on.
 *
 */
void
daos_sched_progress(struct daos_sched *sched);

/**
 * Check completion on all tasks in the scheduler.
 *
 * \param sched	[IN]	Schedule to check.
 *
 * \return		true if scheduler is empty, false otherwise.
 */
bool
daos_sched_check_complete(struct daos_sched *sched);

/**
 * Initialize the daos_task.
 *
 * The task will be added to the scheduler task list, and
 * being scheduled later, if dependent task is provided, then
 * the task will be added to the dep list of the dependent
 * task, once the dependent task is done, then the task will
 * be added to the scheduler list.
 *
 * \param taskp [output]	pointer to daos_task to be allocated and
 *				initialized. The task is freed internally when
 *				complete is called.
 * \param task_func [input]	the function to be executed when
 *                              the task is executed.
 * \param arg [input]		the task_func argument.
 * \param arg_size [input]	the task_func argument size.
 * \paran ready [input]		Indicate whether the func can be immediately
 *				called. If set to true, the func will be called
 *				and the task will be inserted in the running
 *				list of the scheduler.
 * \param sched [input]		daos scheduler where the daos
 *                              task will be attached to.
 *
 * \return			0  if initialization succeeds.
 * \return			negative errno if it fails.
 */
int
daos_task_init(struct daos_task **taskp, daos_task_func_t task_func, void *arg,
	       int arg_size, struct daos_sched *sched);
int
daos_task_schedule(struct daos_task *task, bool ready);

/**
 * register complete callback for the task.
 *
 * \param task [input]		task to be registered complete callback.
 * \param comp_cb [input]	complete callback.
 * \param arg [input]		callback argument.
 * \param top [input]		indicate whether cb to be inserted at top of
 *				cb stack (true) or buttom (false).
 *
 * \return		0 if register succeeds.
 * \return		negative errno if it fails.
 */
int
daos_task_register_comp_cb(struct daos_task *task, daos_task_cb_t comp_cb,
			   void *arg, daos_size_t arg_size);

/**
 * Mark task as completed.
 *
 * \param task [input]	task to be completed.
 * \param ret [input]	ret result of the task.
 **/
void
daos_task_complete(struct daos_task *task, int ret);

/**
 * MSC - deprecate in favor of daos_task_register_deps
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
 * MSC - I think we can move this as an internal function for just DAOS.
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
 * MSC - I think this can be deprecated. But not sure yet.
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

/**
 * Register dependency tasks that will be required to be completed before the
 * the task can be scheduled. The dependency tasks cannot be in progress.
 *
 * \param task	[IN]	Task to add dependencies for.
 * \param num_deps [IN]	Number of tasks in the task array.
 * \param dep_tasks [IN]
 *			Task array for all the tasks that are required to
 *			complete before the task can scheduled.
 *
 * \return		0 if success.
 *			negative errno if it fails.
 */
int
daos_task_register_deps(struct daos_task *task, int num_deps,
			struct daos_task *dep_tasks[]);

/**
 * Register prepare and completion callbacks that will be executed right before
 * the task is scheduled and after it completes respectively.
 *
 * \param task	[IN]	Task to add CBs on.
 * \param prep_cb [IN]	Prepare callback.
 * \param prep_data_size [IN]
 *			Size of the user provided prep data to be copied
 *			internally.
 * \param prep_data [IN] User data passed to the prepare callback.
 * \param comp_cb [IN]	Completion callback
 * \param comp_data_size [IN]
 *			Size of the user provided comp data to be copied
 *			internally.
 * \param comp_data [IN]
 *			User data passed to the completion callback.
 *
 * \return		0 if success.
 *			negative errno if it fails.
 */
int
daos_task_register_cbs(struct daos_task *task, daos_task_cb_t prep_cb,
		       void *prep_data, daos_size_t prep_data_size,
		       daos_task_cb_t comp_cb, void *comp_data,
		       daos_size_t comp_data_size);

/**
 * Reinitialize a task and move it into the scheduler's initialize list. The
 * task must have a body function to be reinserted into the scheduler. If the
 * task is reintialzed in one of its completion CBs, that callback and the ones
 * that have already executed will have been removed from the cb list and will
 * need to be re-registered by the user after re-insertion.
 *
 * \param task	[IN]	Task to reinitialize
 *
 * \return		0 if success.
 *			negative errno if it fails.
 */
int
daos_task_reinit(struct daos_task *task);

#endif
