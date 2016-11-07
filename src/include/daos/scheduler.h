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
#include <daos/event.h>
#include <daos/transport.h>

#include <crt_util/list.h>
#include <crt_util/hash.h>

typedef int (*daos_task_comp_cb_t)(struct daos_task *);
typedef int (*daos_task_func_t)(struct daos_task *);

/**
 * daos_task is used to track single asynchronous operation.
 **/
struct daos_task {
	int			dt_result;

	daos_task_comp_cb_t	dt_comp_cb;
	/* daos schedule internal */
	struct {
		uint64_t	dt_space[60];
	}			dt_private;
};

/**
 * Track a sub group of tasks (from one scheduler).
 **/
typedef int (*daos_task_group_cb_t)(void *args);
struct daos_task_group {
	/* link to daos_sched */
	crt_list_t		dtg_list;

	/* protect the task list */
	pthread_mutex_t		dtg_task_list_lock;

	/* link to all tasks of the group */
	crt_list_t		dtg_task_list;

	int			dtg_result;
	/* completion callback, which is called when all tasks
	 * in dsc_task_list are finished.
	 **/
	daos_task_group_cb_t	dtg_comp_cb;

	/* completion callback argument */
	void			*dtg_cb_arg;
};

typedef int (*daos_sched_comp_cb_t)(void *args, int rc);
struct daos_sched_comp {
	crt_list_t		dsc_list;
	daos_sched_comp_cb_t	dsc_comp_cb;
	void			*dsc_arg;
};

/**
 * Track all of the tasks under a scheduler.
 **/
struct daos_sched {
	/* the event associated with the scheduler */
	daos_event_t	*ds_event;

	/* The result of daos schedule */
	int		ds_result;

	/* daos schedule internal */
	struct {
		uint64_t	ds_space[28];
	}			ds_private;
};

struct daos_sched *
daos_ev2sched(struct daos_event *ev);

void *
daos_sched_get_inline_dtg(struct daos_sched *sched);

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
 * \param task [input]		daos_task to be initialized.
 * \param task_func [input]	the function to be executed when
 *                              the task is executed.
 * \param sched [input]		daos scheduler where the daos
 *                              task will be attached to.
 *
 * \return			0  if initialization succeeds.
 * \return			negative errno if it fails.
 */
int
daos_task_init(struct daos_task *task, daos_task_func_t task_func,
	       struct daos_sched *sched);

/**
 * Finalize the daos_task.
 *
 * \param task [input]		daos_task to be finalized.
 */
void
daos_task_fini(struct daos_task *task);


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
 * Initialize daos task group.
 *
 * daos task group is used to track a group of tasks under one scheduler,
 * The group completion callback will be called when all of its tasks are
 * finished.
 *
 * \param dtg [input]	daos task group to be initialize.
 * \param sched [input] daos scheduler which the task group belongs to.
 * \param callback [input] callback of the task group, which is called
 *                         when all of its tasks are finished.
 * \param arg [input]	argument for callback.
 *
 * \return		0 if initialization succeeds.
 * \return		negative errno if it fails.
 */
int
daos_task_group_init(struct daos_task_group *dtg,
		     struct daos_sched *sched,
		     daos_task_group_cb_t callback,
		     void *arg);

/**
 * Add task to daos task group.
 *
 * \param dtg [input]	daos task group which task is added to.
 * \param task [input]  daos task to be added.
 *
 * \return		0 if adding succeeds.
 * \return		negative errno if adding fails.
 */
int
daos_task_group_add(struct daos_task_group *dtg,
		    struct daos_task *task);


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
 * Cancel all of tasks and task group of the scheduler.
 *
 * \param sched [input] scheduler to be canceled.
 * \param ret   [input] result of the scheduler.
 */
void
daos_sched_cancel(struct daos_sched *sched, int ret);
#endif
