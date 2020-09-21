/**
 * (C) Copyright 2016-2020 Intel Corporation.
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

#include <daos/tse.h>

struct tse_task_private {
	struct tse_sched_private	*dtp_sched;

	/* function for the task */
	tse_task_func_t			 dtp_func;

	/* links to user task list like tse_task_list_add/_del etc APIs */
	d_list_t			 dtp_task_list;

	/* links to scheduler */
	d_list_t			 dtp_list;

	/* links to tasks which dependent on it */
	d_list_t			 dtp_dep_list;

	/* daos prepare task callback list */
	d_list_t			 dtp_prep_cb_list;

	/* daos complete task callback list */
	d_list_t			 dtp_comp_cb_list;

	uint32_t			/* task has been completed, no chance to
					 * be re-initialized.
					 */
					 dtp_completed:1,
					/* task is being completed, before those
					 * complete callbacks are being called,
					 * if the task is re-initialized, then
					 * this dtp_completing will be reset
					 * to 0.
					 */
					 dtp_completing:1,
					/* task is in running state */
					 dtp_running:1,
					 dtp_dep_cnt:29;
	/* refcount of the task */
	uint32_t			 dtp_refcnt;
	/**
	 * task parameter pointer, it can be assigned while creating task,
	 * or explicitly call API tse_task_priv_set. User can just use
	 * \a dtp_buf instead of this if parameter structure is enough to
	 * fit in.
	 */
	void				*dtp_priv;
	/**
	 * DAOS internal task parameter pointer.
	 */
	void				*dtp_priv_internal;
	/**
	 * reserved buffer for user to assign embedded parameters, it also can
	 * be used as task stack space that can push/pop parameters to
	 * facilitate I/O handling. The embedded parameter uses buffer from the
	 * bottom, and the stack space grows down from top.
	 *
	 * The sum of dtp_stack_top and dtp_embed_top should not exceed
	 * TSE_TASK_ARG_LEN.
	 */
	uint32_t			 dtp_stack_top;
	uint32_t			 dtp_embed_top;
	char				 dtp_buf[TSE_TASK_ARG_LEN];
};

struct tse_task_cb {
	d_list_t		dtc_list;
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
	d_list_t	dsp_init_list;

	/* The task will be moved to complete list after the
	 * complete callback is being executed
	 **/
	d_list_t	dsp_complete_list;

	/**
	 * The task running list.
	 **/
	d_list_t	dsp_running_list;

	/* the list for complete callback */
	d_list_t	dsp_comp_cb_list;

	int		dsp_refcount;

	/* number of tasks being executed */
	int		dsp_inflight;

	uint32_t	dsp_cancelling:1,
			dsp_completing:1;
};

struct tse_sched_comp {
	d_list_t		dsc_list;
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
