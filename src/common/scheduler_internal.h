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
	/* refcount of the task */
	int			dtp_refcnt;

	/* function for the task */
	daos_task_func_t	dtp_func;
	void			*dtp_func_arg;

	/* links to scheduler */
	daos_list_t		dtp_list;

	/* links to tasks which dependent on it */
	daos_list_t		dtp_dep_list;

	/* daos complete task callback list */
	daos_list_t		dtp_comp_cb_list;

	/* daos complete task callback list */
	daos_list_t		dtp_ret_list;

	/* daos_task internal buffer */
	struct {
		uint32_t	dtp_buf_space[25];
		uint32_t	dtp_buf_size;
	}			dtp_buf;

	struct daos_op_sp	dtp_sp;

	uint32_t		dtp_complete:1,
				dtp_running:1;
	int			dtp_dep_cnt;
	struct daos_sched_private	*dtp_sched;
};

struct daos_task_comp_cb {
	daos_list_t		dtc_list;
	daos_task_comp_cb_t	dtc_comp_cb;
	void			*dtc_arg;
};

struct daos_sched_private {
	/* lock to protect schedule status and sub task list */
	pthread_mutex_t dsp_lock;

	/* The task will be added to init list when it is initially
	 * added to scheduler.
	 **/
	daos_list_t	dsp_init_list;

	/* The task will be moved to complete list after the
	 * complete callback is being executed
	 **/
	daos_list_t	dsp_complete_list;

	/**
	 * The task running list.
	 **/
	daos_list_t	dsp_running_list;

	/* the list for complete callback */
	daos_list_t	dsp_comp_cb_list;

	int		dsp_refcount;

	/* number of tasks being executed */
	int		dsp_inflight;

	uint32_t	dsp_cancelling:1,
			dsp_completing:1;
};

struct daos_sched_comp {
	daos_list_t		dsc_list;
	daos_sched_comp_cb_t	dsc_comp_cb;
	void			*dsc_arg;
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
