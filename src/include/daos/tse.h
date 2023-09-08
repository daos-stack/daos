/*
 * (C) Copyright 2015-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * Task Execution Engine: Generic scheduler for creating tasks and dependencies
 * between them.
 */

#ifndef __TSE_SCHEDULE_H__
#define __TSE_SCHEDULE_H__

#include <gurt/list.h>

/* tse_task arguments max length (pthread_mutex_t is of different size between x86 and aarch64). */
#define TSE_TASK_ARG_LEN	(840 + sizeof(pthread_mutex_t))
/* internal tse private data size (struct tse_task_private) */
#define TSE_PRIV_SIZE		(TSE_TASK_ARG_LEN + 136)
/* tse_task is used to track single asynchronous operation (8 bytes used for public members). */
#define TSE_TASK_SIZE		(TSE_PRIV_SIZE + 8)

typedef struct tse_task {
	int			dt_result;
	/** padding bytes */
	int			dt_pad32;
	/* daos schedule internal */
	struct {
		char		dt_space[TSE_PRIV_SIZE];
	}			dt_private;
} tse_task_t;

/**
 * Track all of the tasks under a scheduler.
 */
typedef struct {
	int		ds_result;

	/* user data associated with the scheduler (completion cb data, etc.) */
	void		*ds_udata;

	/* daos schedule internal */
	struct {
		uint64_t	ds_space[48];
	}			ds_private;
} tse_sched_t;

typedef int (*tse_sched_comp_cb_t)(void *args, int rc);
typedef int (*tse_task_func_t)(tse_task_t *);

/** CB type for prepare, completion, and result processing */
typedef int (*tse_task_cb_t)(tse_task_t *, void *arg);

void *
tse_task2arg(tse_task_t *task);

tse_sched_t *
tse_task2sched(tse_task_t *task);

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
tse_sched_init(tse_sched_t *sched, tse_sched_comp_cb_t comp_cb,
		void *udata);

/**
 * Finish the scheduler.
 *
 * \param sched [input]		the scheduler to be finished.
 */
void
tse_sched_fini(tse_sched_t *sched);

/**
 * Take reference of the scheduler.
 *
 * \param sched [input]		the scheduler pointer.
 */
void
tse_sched_addref(tse_sched_t *sched);

/**
 * Release reference of the scheduler.
 *
 * \param sched [input]		the scheduler pointer.
 */
void
tse_sched_decref(tse_sched_t *sched);

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
tse_sched_complete(tse_sched_t *sched, int ret, bool cancel);

/**
 * register complete callback for scheduler.
 *
 * \param sched [input]		scheduler where to register the completion
 *                              callback.
 * \param comp_cb [input]	completion callback to be registered.
 * \param arg [input]		argument of the completion callback.
 *
 * \return			0 if registration succeeds.
 * \return			errno if registration fails.
 */
int
tse_sched_register_comp_cb(tse_sched_t *sched,
			   tse_sched_comp_cb_t comp_cb, void *arg);

/**
 * Make progress on scheduler. Runs tasks that are ready to be executed after
 * the tasks they depend on were completed. Note that task completion using
 * tse_task_complete() must be done by the engine user to push progress on
 * the engine. In DAOS tse_task_complete is called by the completion CB of the
 * RPC request that is sent to the server.
 *
 * \param sched	[IN]	Scheduler to make progress on.
 *
 */
void
tse_sched_progress(tse_sched_t *sched);

/**
 * Check completion on all tasks in the scheduler.
 *
 * \param sched	[IN]	Schedule to check.
 *
 * \return		true if scheduler is empty, false otherwise.
 */
bool
tse_sched_check_complete(tse_sched_t *sched);

/**
 * Initialize the tse_task.
 *
 * The task will be added to the scheduler task list, and
 * being scheduled later, if dependent task is provided, then
 * the task will be added to the dep list of the dependent
 * task, once the dependent task is done, then the task will
 * be added to the scheduler list.
 *
 * \param task_func [input]	the function to be executed when
 *                              the task is executed.
 * \param sched [input]		daos scheduler where the daos
 *                              task will be attached to.
 * \param priv [input]		private data passed into the task, user can
 *				get it by calling \a tse_task_get_priv, or
 *				rewrite by calling \a tse_task_set_priv.
 * \param taskp [output]	pointer to tse_task to be allocated and
 *				initialized. The task is freed internally when
 *				complete is called.
 *
 * \return			0  if initialization succeeds.
 * \return			negative errno if it fails.
 */
int
tse_task_create(tse_task_func_t task_func, tse_sched_t *sched, void *priv,
		tse_task_t **taskp);

/**
 * Add task to scheduler it was initialized with. If task body function should
 * be called immediately as part of this function, \a instant should be set to
 * true; otherwise if false task would be in the scheduler init list and
 * progressed when the scheduler is progressed.
 *
 * \param task [input]		task to be scheduled.
 * \param instant [input]	flag to indicate whether task should be
 *				executed immediately.
 *
 * \return			0 if success negative errno if fail.
 */
int
tse_task_schedule(tse_task_t *task, bool instant);

/**
 * Same as tse_task_schedule, expect that \a task will not be executed within
 * \a delay microseconds if \a instant is false.
 *
 * \param task [input]		task to be scheduled.
 * \param instant [input]	flag to indicate whether task should be
 *				executed immediately.
 * \param delay [input]		scheduling delay in microseconds.
 *
 * \return			0 if success negative errno if fail.
 */
int
tse_task_schedule_with_delay(tse_task_t *task, bool instant, uint64_t delay);

/**
 * register complete callback for the task.
 *
 * \param task [input]		task to be registered complete callback.
 * \param comp_cb [input]	complete callback.
 * \param arg [input]		callback argument.
 * \param top [input]		indicate whether cb to be inserted at top of
 *				cb stack (true) or bottom (false).
 *
 * \return		0 if register succeeds.
 * \return		negative errno if it fails.
 */
int
tse_task_register_comp_cb(tse_task_t *task, tse_task_cb_t comp_cb,
			  void *arg, size_t arg_size);

/**
 * Mark task as completed.
 *
 * \param task [input]	task to be completed.
 * \param ret [input]	ret result of the task.
 **/
void
tse_task_complete(tse_task_t *task, int ret);

/**
 * Get embedded buffer of a task, user can use it to carry function parameters.
 * Embedded buffer of task has size limit, this function will return NULL if
 * \a buf_size is larger than the limit.
 *
 * User should use private data by tse_task_set_priv() to pass large parameter.
 *
 * \param task [in] task to get the buffer.
 * \param size [in] task buffer size.
 *
 * \return	pointer to the buffer.
 **/
void *
tse_task_buf_embedded(tse_task_t *task, int size);

/**
 * Return the private data of the task.
 * Private data can be set while creating task, or by calling tse_task_set_priv.
 */
void *
tse_task_get_priv(tse_task_t *task);

/**
 * Set or change the private data of the task. The original private data will
 * be returned.
 */
void *
tse_task_set_priv(tse_task_t *task, void *priv);

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
tse_task_register_deps(tse_task_t *task, int num_deps,
		       tse_task_t *dep_tasks[]);

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
tse_task_register_cbs(tse_task_t *task, tse_task_cb_t prep_cb,
		      void *prep_data, size_t prep_data_size,
		      tse_task_cb_t comp_cb, void *comp_data,
		      size_t comp_data_size);

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
tse_task_reinit(tse_task_t *task);

/**
 * Same as tse_task_reinit, except that \a task will not be re-executed within
 * \a delay microseconds.
 *
 * \param task	[IN]	Task to reinitialize
 * \param delay	[IN]	Scheduling delay in microseconds
 *
 * \return		0 if success.
 *			negative errno if it fails.
 */
int
tse_task_reinit_with_delay(tse_task_t *task, uint64_t delay);

/**
 * Reset a task with a new body function. The task must have already completed
 * or not started yet, and must have a > 0 valid ref count (not freed).
 * This allows a user to reuse a task with a different body function and not
 * have to recreate a task for a different operation.
 *
 * \param task	[IN]	Task to reset
 *
 * \return		0 if success.
 *			negative errno if it fails.
 */
int
tse_task_reset(tse_task_t *task, tse_task_func_t task_func, void *priv);

void
tse_task_addref(tse_task_t *task);

void
tse_task_decref(tse_task_t *task);

/**
 * Add a newly created task to a list. It returns error if the task is already
 * running or completed.
 */
int
tse_task_list_add(tse_task_t *task, d_list_t *head);

/**
 * Remove the task from list head.
 */
void
tse_task_list_del(tse_task_t *task);

/**
 * The first task linked on list \a head, it returns NULL
 * if the list is empty.
 */
tse_task_t *
tse_task_list_first(d_list_t *head);

/**
 * Schedule all tasks attached on list \a head.
 */
void
tse_task_list_sched(d_list_t *head, bool instant);

/**
 * Abort all tasks attached on list \a head.
 */
void
tse_task_list_abort(d_list_t *head, int rc);

/**
 * All tasks attached on \a head depend on list \a task.
 */
int
tse_task_list_depend(d_list_t *head, tse_task_t *task);

/**
 * \a task depends on all tasks attached on list \a head
 */
int
tse_task_depend_list(tse_task_t *task, d_list_t *head);

/**
 * Traverse all tasks on list \a head, invoke the \a cb with parameter of \a arg
 * on each task. User is free to remove the task from the list \a head within
 * the \a cb's executing.
 */
int
tse_task_list_traverse(d_list_t *head, tse_task_cb_t cb, void *arg);

/**
 * Advanced tse_task_list_traverse, the task's dtp_task_list or head list possibly
 * be changed/zeroed after \a cb executed.
 */
int
tse_task_list_traverse_adv(d_list_t *head, tse_task_cb_t cb, void *arg);

/**
 * Set the task don't propagate err-code from dependent tasks.
 */
void
tse_disable_propagate(tse_task_t *task);

#endif /* __TSE_SCHEDULE_H__ */
