/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include <gurt/atomic.h>

struct tse_task_private {
	struct tse_sched_private	*dtp_sched;

	/* function for the task */
	tse_task_func_t			 dtp_func;

	/* links to user task list like tse_task_list_add/_del etc APIs */
	d_list_t			 dtp_task_list;

	/* links to scheduler */
	d_list_t			 dtp_list;

	/* time to start running this task */
	uint64_t			 dtp_wakeup_time;

	/* list of tasks that depend on this task */
	d_list_t			 dtp_dep_list;

	/* daos prepare task callback list */
	d_list_t			 dtp_prep_cb_list;

	/* daos complete task callback list */
	d_list_t			 dtp_comp_cb_list;

	/* task has been completed */
	ATOMIC uint8_t			dtp_completed;
	/* task is in running state */
	ATOMIC uint8_t			dtp_running;
	/* Don't propagate err-code from dependent tasks */
	uint8_t				dtp_no_propagate;
	uint8_t				dtp_pad;
	/* number of dependent tasks */
	uint16_t			 dtp_dep_cnt;
	/* refcount of the task */
	uint16_t			 dtp_refcnt;
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
	uint16_t			 dtp_stack_top;
	uint16_t			 dtp_embed_top;
	/* generation of the task, +1 every time when task re-init or add dependent task */
	ATOMIC uint32_t			 dtp_generation;
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
	 * added to scheduler without any delay. A task with a delay
	 * will be added to dsp_sleeping_list.
	 */
	d_list_t	dsp_init_list;

	/* The task will be moved to complete list after the
	 * complete callback is being executed
	 **/
	d_list_t	dsp_complete_list;

	/**
	 * The task running list.
	 **/
	d_list_t	dsp_running_list;

	/* list of sleeping tasks sorted by dtp_wakeup_time */
	d_list_t	dsp_sleeping_list;

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
