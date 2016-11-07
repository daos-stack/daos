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
 * common/scheduler_internal.h
 *
 * DAOS client will use scheduler/task to manage the asynchronous tasks.
 * Tasks will be attached to one scheduler, when scheduler is executed,
 * it will walk through the task list of the scheduler and pick up those
 * ready tasks to executed.
 *
 * Author: Di Wang  <di.wang@intel.com>
 */

struct daos_task_private {
	/* function for the task */
	daos_task_func_t	crt_func;

	/* links to scheduler */
	crt_list_t		crt_list;

	/* links to task group */
	crt_list_t		crt_dtg_list;

	/* daos_task argument */
	struct {
		uint64_t	crt_arg_space[12];
	}			crt_arg;

	struct daos_op_sp	crt_sp;

	struct daos_sched_private	*crt_sched;
	struct daos_task_group		*crt_dtg;
	int			crt_result;
};

struct daos_sched_private {
	/* lock to protect schedule status and sub task list */
	pthread_mutex_t dsp_lock;

	/* The task will initially being added to init list */
	crt_list_t	dsp_init_list;

	/* The task will be moved to running list when it is
	 * being executed
	 **/
	crt_list_t	dsp_running_list;

	/* The task will be moved to complete list after the
	 * complete callback is being executed
	 **/
	crt_list_t	dsp_complete_list;

	/* the list for complete callback */
	crt_list_t	dsp_comp_cb_list;

	/* daos task group list */
	crt_list_t	dsp_dtg_list;

	int		dsp_refcount;

	struct daos_task_group	dsp_inline_dtg;

};

static inline struct daos_task_private *
daos_task2priv(struct daos_task *task)
{
	return (struct daos_task_private *)&task->dt_private;
}

static inline struct daos_task *
daos_priv2task(struct daos_task_private *priv)
{
	return container_of(priv, struct daos_task, dt_private);
}

static inline struct daos_sched_private *
daos_sched2priv(struct daos_sched *sched)
{
	return (struct daos_sched_private *)&sched->ds_private;
}

static inline struct daos_sched *
daos_priv2sched(struct daos_sched_private *priv)
{
	return container_of(priv, struct daos_sched, ds_private);
}
