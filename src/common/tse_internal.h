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
 * common/tse_internal.h
 *
 * DAOS client will use scheduler/task to manage the asynchronous tasks.
 * Tasks will be attached to one scheduler, when scheduler is executed,
 * it will walk through the task list of the scheduler and pick up those
 * ready tasks to executed.
 *
 * Author: Di Wang  <di.wang@intel.com>
 */

struct tse_task_private {
	/* refcount of the task */
	int			dtp_refcnt;

	/* function for the task */
	tse_task_func_t		dtp_func;
	void			*dtp_func_arg;

	/* links to scheduler */
	daos_list_t		dtp_list;

	/* links to tasks which dependent on it */
	daos_list_t		dtp_dep_list;

	/* daos prepare task callback list */
	daos_list_t		dtp_prep_cb_list;

	/* daos complete task callback list */
	daos_list_t		dtp_comp_cb_list;

	/* daos complete task callback list */
	daos_list_t		dtp_ret_list;

	/* tse_task internal buffer */
	struct {
		/*
		 * MSC - We should change that by making the arguments an
		 * extension of the task struct allocated with the task.
		 */
		uint32_t	dtp_buf_space[50];
	}			dtp_buf;

	uint32_t		dtp_complete:1,
				dtp_running:1;
	int			dtp_dep_cnt;
	struct tse_sched_private	*dtp_sched;
};

struct tse_task_cb {
	daos_list_t		dtc_list;
	tse_task_cb_t		dtc_cb;
	daos_size_t		dtc_arg_size;
	char			dtc_arg[0];
};

struct tse_sched_private {
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

struct tse_sched_comp {
	daos_list_t		dsc_list;
	tse_sched_comp_cb_t	dsc_comp_cb;
	void			*dsc_arg;
};


static inline struct tse_task_private *
tse_task2priv(tse_task_t *task)
{
	return (struct tse_task_private *)&task->dt_private;
}

static inline tse_task_t *
tse_priv2task(struct tse_task_private *priv)
{
	return container_of(priv, tse_task_t, dt_private);
}

static inline struct tse_sched_private *
tse_sched2priv(tse_sched_t *sched)
{
	return (struct tse_sched_private *)&sched->ds_private;
}

static inline tse_sched_t *
tse_priv2sched(struct tse_sched_private *priv)
{
	return container_of(priv, tse_sched_t, ds_private);
}
