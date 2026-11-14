/**
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * This file is part of common DAOS library.
 *
 * common/tse.c
 *
 * DAOS client will use scheduler/task to manage the asynchronous tasks.
 * Tasks will be attached to one scheduler, when scheduler is executed,
 * it will walk through the task list of the scheduler and pick up those
 * ready tasks to executed.
 */
#define D_LOGFAC	DD_FAC(client)

#include <stdint.h>
#include <pthread.h>

#include <daos/common.h>
#include <cart/api.h>

#include "tse_internal.h"

D_CASSERT(sizeof(struct tse_task) == TSE_TASK_SIZE);
D_CASSERT(sizeof(struct tse_task_private) <= TSE_PRIV_SIZE);

struct tse_task_link {
	d_list_t		 tl_link;
	tse_task_t		*tl_task;
};

static void tse_sched_priv_decref(struct tse_sched_private *dsp);

int
tse_sched_init(tse_sched_t *sched, tse_sched_comp_cb_t comp_cb,
	       void *udata)
{
	struct tse_sched_private	*dsp = tse_sched2priv(sched);
	int				 rc;

	D_CASSERT(sizeof(sched->ds_private) >= sizeof(*dsp));

	memset(sched, 0, sizeof(*sched));

	D_INIT_LIST_HEAD(&dsp->dsp_init_list);
	D_INIT_LIST_HEAD(&dsp->dsp_running_list);
	D_INIT_LIST_HEAD(&dsp->dsp_complete_list);
	D_INIT_LIST_HEAD(&dsp->dsp_sleeping_list);
	D_INIT_LIST_HEAD(&dsp->dsp_comp_cb_list);

	dsp->dsp_refcount = 1;
	dsp->dsp_inflight = 0;

	rc = D_MUTEX_INIT(&dsp->dsp_lock, NULL);
	if (rc != 0)
		return rc;

	if (comp_cb != NULL) {
		rc = tse_sched_register_comp_cb(sched, comp_cb, udata);
		if (rc != 0)
			return rc;
	}

	sched->ds_udata = udata;
	sched->ds_result = 0;

	return 0;
}

static inline uint32_t
tse_task_buf_size(int size)
{
	return (size + 7) & ~0x7;
}

/*
 * MSC - I changed this to be just a single buffer and not as before where it
 * keeps giving an addition pointer to the big pre-allcoated buffer. previous
 * way doesn't work well for public use.
 * We should make this simpler now and more generic as the comment below.
 */
void *
tse_task_buf_embedded(tse_task_t *task, int size)
{
	struct tse_task_private	*dtp = tse_task2priv(task);
	uint32_t		 avail_size;

	/** Let's assume dtp_buf is always enough at the moment */
	/** MSC - should malloc if size requested is bigger */
	size = tse_task_buf_size(size);
	D_ASSERT(size < UINT16_MAX);
	avail_size = sizeof(dtp->dtp_buf) - dtp->dtp_stack_top;
	D_ASSERTF(size <= avail_size,
		  "req size %u avail size %u (all_size %lu stack_top %u)\n",
		  size, avail_size, sizeof(dtp->dtp_buf),
		  dtp->dtp_stack_top);
	dtp->dtp_embed_top = size;
	D_ASSERT((dtp->dtp_stack_top + dtp->dtp_embed_top) <=
		  sizeof(dtp->dtp_buf));
	return (void *)dtp->dtp_buf;
}

void *
tse_task_stack_push(tse_task_t *task, uint32_t size)
{
	struct tse_task_private	*dtp = tse_task2priv(task);
	void			*pushed_ptr;
	uint32_t		 avail_size;

	avail_size = sizeof(dtp->dtp_buf) -
		     (dtp->dtp_stack_top + dtp->dtp_embed_top);
	size = tse_task_buf_size(size);
	D_ASSERTF(size <= avail_size, "push size %u exceed avail size %u "
		   "(all_size %lu, stack_top %u, embed_top %u).\n",
		   size, avail_size, sizeof(dtp->dtp_buf),
		   dtp->dtp_stack_top, dtp->dtp_embed_top);

	dtp->dtp_stack_top += size;
	pushed_ptr = dtp->dtp_buf + sizeof(dtp->dtp_buf) - dtp->dtp_stack_top;
	D_ASSERT((dtp->dtp_stack_top + dtp->dtp_embed_top) <=
		  sizeof(dtp->dtp_buf));

	return pushed_ptr;
}

void *
tse_task_stack_pop(tse_task_t *task, uint32_t size)
{
	struct tse_task_private	*dtp = tse_task2priv(task);
	void			*poped_ptr;

	size = tse_task_buf_size(size);
	D_ASSERTF(size <= dtp->dtp_stack_top,
		   "pop size %u exceed stack_top %u.\n",
		   size, dtp->dtp_stack_top);

	poped_ptr = dtp->dtp_buf + sizeof(dtp->dtp_buf) - dtp->dtp_stack_top;
	dtp->dtp_stack_top -= size;
	D_ASSERT((dtp->dtp_stack_top + dtp->dtp_embed_top) <=
		  sizeof(dtp->dtp_buf));

	return poped_ptr;
}

void
tse_task_stack_push_data(tse_task_t *task, void *data, uint32_t data_len)
{
	void	*stack_data;

	stack_data = tse_task_stack_push(task, data_len);
	memcpy(stack_data, data, data_len);
}

void
tse_task_stack_pop_data(tse_task_t *task, void *data, uint32_t data_len)
{
	void	*stack_data;

	stack_data = tse_task_stack_pop(task, data_len);
	memcpy(data, stack_data, data_len);
}

void *
tse_task_get_priv(tse_task_t *task)
{
	struct tse_task_private *dtp = tse_task2priv(task);

	return dtp->dtp_priv;
}

void *
tse_task_set_priv(tse_task_t *task, void *priv)
{
	struct tse_task_private *dtp = tse_task2priv(task);
	void			*old = dtp->dtp_priv;

	dtp->dtp_priv = priv;
	return old;
}

void *
tse_task_get_priv_internal(tse_task_t *task)
{
	struct tse_task_private *dtp = tse_task2priv(task);

	return dtp->dtp_priv_internal;
}

void *
tse_task_set_priv_internal(tse_task_t *task, void *priv)
{
	struct tse_task_private *dtp = tse_task2priv(task);
	void			*old = dtp->dtp_priv_internal;

	dtp->dtp_priv_internal = priv;
	return old;
}

tse_sched_t *
tse_task2sched(tse_task_t *task)
{
	struct tse_sched_private	*sched_priv;

	sched_priv = tse_task2priv(task)->dtp_sched;
	return tse_priv2sched(sched_priv);
}

static void
tse_task_addref_locked(struct tse_task_private *dtp)
{
	D_ASSERT(dtp->dtp_refcnt < UINT16_MAX);
	dtp->dtp_refcnt++;
}

static bool
tse_task_decref_locked(struct tse_task_private *dtp)
{
	D_ASSERT(dtp->dtp_refcnt > 0);
	dtp->dtp_refcnt--;
	return dtp->dtp_refcnt == 0;
}

void
tse_task_addref(tse_task_t *task)
{
	struct tse_task_private  *dtp = tse_task2priv(task);
	struct tse_sched_private *dsp = dtp->dtp_sched;

	D_ASSERT(dsp != NULL);

	D_MUTEX_LOCK(&dsp->dsp_lock);
	tse_task_addref_locked(dtp);
	D_MUTEX_UNLOCK(&dsp->dsp_lock);
}

void
tse_task_decref(tse_task_t *task)
{
	struct tse_task_private  *dtp = tse_task2priv(task);
	struct tse_sched_private *dsp = dtp->dtp_sched;
	bool			   zombie;

	D_ASSERT(dsp != NULL);
	D_MUTEX_LOCK(&dsp->dsp_lock);
	zombie = tse_task_decref_locked(dtp);
	D_MUTEX_UNLOCK(&dsp->dsp_lock);
	if (!zombie)
		return;

	D_ASSERT(d_list_empty(&dtp->dtp_dep_list));
	D_ASSERT(d_list_empty(&dtp->dtp_comp_cb_list));
	D_FREE(task);
}

static void
tse_task_decref_free_locked(tse_task_t *task)
{
	struct tse_task_private *dtp = tse_task2priv(task);
	bool			zombie;

	zombie = tse_task_decref_locked(dtp);
	if (!zombie)
		return;

	D_ASSERT(d_list_empty(&dtp->dtp_dep_list));
	D_ASSERT(d_list_empty(&dtp->dtp_comp_cb_list));
	D_FREE(task);
}

void
tse_sched_fini(tse_sched_t *sched)
{
	struct tse_sched_private *dsp = tse_sched2priv(sched);

	D_ASSERT(dsp->dsp_inflight == 0);
	D_ASSERT(d_list_empty(&dsp->dsp_init_list));
	D_ASSERT(d_list_empty(&dsp->dsp_running_list));
	D_ASSERT(d_list_empty(&dsp->dsp_complete_list));
	D_ASSERT(d_list_empty(&dsp->dsp_sleeping_list));
	D_MUTEX_DESTROY(&dsp->dsp_lock);
}

static inline void
tse_sched_priv_addref_locked(struct tse_sched_private *dsp)
{
	dsp->dsp_refcount++;
}

void tse_sched_fini(tse_sched_t *sched);

static void
tse_sched_priv_decref(struct tse_sched_private *dsp)
{
	bool	finalize;

	D_MUTEX_LOCK(&dsp->dsp_lock);

	D_ASSERT(dsp->dsp_refcount > 0);
	dsp->dsp_refcount--;
	finalize = dsp->dsp_refcount == 0;

	D_MUTEX_UNLOCK(&dsp->dsp_lock);

	if (finalize)
		tse_sched_fini(tse_priv2sched(dsp));
}

/* Drop a scheduler reference while dsp_lock is held. Returns true when
 * this was the last reference; the caller must release dsp_lock before
 * calling tse_sched_fini(). Callers on the drain path hold their own
 * loop reference, so a finalize here means accounting is broken. */
static bool
tse_sched_priv_decref_locked(struct tse_sched_private *dsp)
{
	D_ASSERT(dsp->dsp_refcount > 0);
	dsp->dsp_refcount--;
	return dsp->dsp_refcount == 0;
}

void
tse_sched_addref(tse_sched_t *sched)
{
	struct tse_sched_private *dsp = tse_sched2priv(sched);

	D_MUTEX_LOCK(&dsp->dsp_lock);
	tse_sched_priv_addref_locked(dsp);
	D_MUTEX_UNLOCK(&dsp->dsp_lock);
}

void
tse_sched_decref(tse_sched_t *sched)
{
	tse_sched_priv_decref(tse_sched2priv(sched));
}

int
tse_sched_register_comp_cb(tse_sched_t *sched,
			   tse_sched_comp_cb_t comp_cb, void *arg)
{
	struct tse_sched_private	*dsp = tse_sched2priv(sched);
	struct tse_sched_comp		*dsc;

	D_ALLOC_PTR(dsc);
	if (dsc == NULL)
		return -DER_NOMEM;

	dsc->dsc_comp_cb = comp_cb;
	dsc->dsc_arg = arg;

	D_MUTEX_LOCK(&dsp->dsp_lock);
	d_list_add(&dsc->dsc_list,
		      &dsp->dsp_comp_cb_list);
	D_MUTEX_UNLOCK(&dsp->dsp_lock);
	return 0;
}

/** MSC - we probably need just 1 completion cb instead of a list */
static int
tse_sched_complete_cb(tse_sched_t *sched)
{
	struct tse_sched_comp		*dsc;
	struct tse_sched_comp		*tmp;
	struct tse_sched_private	*dsp = tse_sched2priv(sched);
	int				rc;

	d_list_for_each_entry_safe(dsc, tmp, &dsp->dsp_comp_cb_list, dsc_list) {
		d_list_del(&dsc->dsc_list);
		rc = dsc->dsc_comp_cb(dsc->dsc_arg, sched->ds_result);
		if (sched->ds_result == 0)
			sched->ds_result = rc;
		D_FREE(dsc);
	}
	return 0;
}

/* Mark the tasks to complete */
static void
tse_task_complete_locked(struct tse_task_private *dtp,
			 struct tse_sched_private *dsp)
{
	if (dtp->dtp_completed == 1)
		return;

	/*
	 * if completing a task that never started, we need to bump in-flight tasks in scheduler
	 * before adding it to tail of completed list.
	 */
	if (!dtp->dtp_running) {
		tse_sched_priv_addref_locked(dsp);
		dsp->dsp_inflight++;
	}

	dtp->dtp_running = 0;
	dtp->dtp_completed = 1;
	d_list_move_tail(&dtp->dtp_list, &dsp->dsp_complete_list);
}

static int
register_cb(tse_task_t *task, bool is_comp, tse_task_cb_t cb,
	    void *arg, daos_size_t arg_size)
{
	struct tse_task_private *dtp = tse_task2priv(task);
	struct tse_task_cb *dtc;

	if (atomic_load(&dtp->dtp_completed) == 1) {
		D_ERROR("Can't add a callback for a completed task\n");
		return -DER_NO_PERM;
	}

	D_ALLOC(dtc, sizeof(*dtc) + arg_size);
	if (dtc == NULL)
		return -DER_NOMEM;

	dtc->dtc_arg_size = arg_size;
	dtc->dtc_cb = cb;
	if (arg)
		memcpy(dtc->dtc_arg, arg, arg_size);

	D_ASSERT(dtp->dtp_sched != NULL);

	D_MUTEX_LOCK(&dtp->dtp_sched->dsp_lock);
	if (is_comp)
		d_list_add(&dtc->dtc_list, &dtp->dtp_comp_cb_list);
	else /** MSC - don't see a need for more than 1 prep cb */
		d_list_add_tail(&dtc->dtc_list, &dtp->dtp_prep_cb_list);

	D_MUTEX_UNLOCK(&dtp->dtp_sched->dsp_lock);

	return 0;
}

int
tse_task_register_comp_cb(tse_task_t *task, tse_task_cb_t comp_cb,
			  void *arg, daos_size_t arg_size)
{
	D_ASSERT(comp_cb != NULL);
	return register_cb(task, true, comp_cb, arg, arg_size);
}

int
tse_task_register_cbs(tse_task_t *task, tse_task_cb_t prep_cb,
		      void *prep_data, daos_size_t prep_data_size,
		      tse_task_cb_t comp_cb, void *comp_data,
		      daos_size_t comp_data_size)
{
	int	rc = 0;

	D_ASSERT(prep_cb != NULL || comp_cb != NULL);
	if (prep_cb)
		rc = register_cb(task, false, prep_cb, prep_data,
				 prep_data_size);
	if (comp_cb && !rc)
		rc = register_cb(task, true, comp_cb, comp_data,
				 comp_data_size);
	return rc;
}

static uint32_t
dtp_generation_get(struct tse_task_private *dtp)
{
	return atomic_fetch_add(&dtp->dtp_generation, 0);
}

static void
dtp_generation_inc(struct tse_task_private *dtp)
{
	atomic_fetch_add(&dtp->dtp_generation, 1);
}

/*
 * Execute the prep callback(s) of the task.
 */
static bool
tse_task_prep_callback(tse_task_t *task)
{
	struct tse_task_private	*dtp = tse_task2priv(task);
	struct tse_task_cb	*dtc;
	struct tse_task_cb	*tmp;
	uint32_t		 gen, new_gen;
	bool			 ret = true;
	int			 rc;

	d_list_for_each_entry_safe(dtc, tmp, &dtp->dtp_prep_cb_list, dtc_list) {
		d_list_del(&dtc->dtc_list);
		/** no need to call if task was completed in one of the cbs */
		gen = dtp_generation_get(dtp);
		if (!atomic_load(&dtp->dtp_completed)) {
			rc = dtc->dtc_cb(task, dtc->dtc_arg);
			if (task->dt_result == 0)
				task->dt_result = rc;
		}
		D_FREE(dtc);
		new_gen = dtp_generation_get(dtp);
		/** Task was re-initialized; */
		if (!atomic_load(&dtp->dtp_running) && new_gen != gen)
			ret = false;
	}

	return ret;
}

/*
 * Execute the callback of the task and returns true if all CBs were executed
 * and non re-init the task. If the task is re-initialized by the user, it means
 * it's in-flight again, so we break at the current CB that re-initialized it,
 * and return false, meaning the task is not completed. All the remaining CBs
 * that haven't been executed remain attached, but the ones that have executed
 * already have been removed from the list at this point.
 */
static bool
tse_task_complete_callback(tse_task_t *task)
{
	struct tse_task_private		*dtp = tse_task2priv(task);
	uint32_t		 	gen, new_gen;
	struct tse_task_cb		*dtc;
	struct tse_task_cb		*tmp;

	/* Take one extra ref-count here and decref before exit, as in dtc_cb() it possibly
	 * re-init the task that may be completed immediately.
	 */
	tse_task_addref(task);

	d_list_for_each_entry_safe(dtc, tmp, &dtp->dtp_comp_cb_list, dtc_list) {
		int ret;

		d_list_del(&dtc->dtc_list);
		gen = dtp_generation_get(dtp);
		ret = dtc->dtc_cb(task, dtc->dtc_arg);
		if (task->dt_result == 0)
			task->dt_result = ret;
		D_FREE(dtc);
		/** Task was re-initialized, or new dep-task added */
		new_gen = dtp_generation_get(dtp);
		if (new_gen != gen) {
			D_DEBUG(DB_TRACE, "task %p re-inited or new dep-task added\n", task);
			tse_task_decref(task);
			return false;
		}
	}

	tse_task_decref(task);
	return true;
}

static int
tse_task_post_process(tse_task_t *task);

/*
 * Process the init and sleeping lists of the scheduler. This first moves all
 * tasks who shall wake up now from the sleeping list to the tail of the init
 * list, and then executes all the body functions of all tasks with no
 * dependencies in the scheduler's init list.
 */
static int
tse_sched_process_init(struct tse_sched_private *dsp)
{
	struct tse_task_private		*dtp;
	struct tse_task_private		*tmp;
	d_list_t			list;
	uint64_t			now = daos_getutime();
	int				processed = 0;

	D_INIT_LIST_HEAD(&list);
	D_MUTEX_LOCK(&dsp->dsp_lock);
	d_list_for_each_entry_safe(dtp, tmp, &dsp->dsp_sleeping_list,
				   dtp_list) {
		/* Take delayed-retry tasks too when cancelling, or they
		 * stay asleep forever and the drain never finishes.
		 * See DAOS-19955. */
		if (!dsp->dsp_cancelling && dtp->dtp_wakeup_time > now)
			break;
		dtp->dtp_wakeup_time = 0;
		d_list_move_tail(&dtp->dtp_list, &dsp->dsp_init_list);
	}
	d_list_for_each_entry_safe(dtp, tmp, &dsp->dsp_init_list, dtp_list) {
		if (dtp->dtp_dep_cnt == 0 || dsp->dsp_cancelling) {
			d_list_move_tail(&dtp->dtp_list, &list);
			dsp->dsp_inflight++;
		}
	}
	D_MUTEX_UNLOCK(&dsp->dsp_lock);

	while (!d_list_empty(&list)) {
		tse_task_t *task;
		bool cancelling;

		dtp = d_list_entry(list.next, struct tse_task_private,
				   dtp_list);

		task = tse_priv2task(dtp);

		D_MUTEX_LOCK(&dsp->dsp_lock);
		/* Ownership is decided once, under the lock: the branch
		 * taken here is the only authority for what happens to
		 * this task below. Re-reading dsp_cancelling after the
		 * unlock would let a drain claim a task this thread had
		 * just published with completed==0, and both threads
		 * would run its callbacks and drop its references. */
		cancelling = dsp->dsp_cancelling;
		if (cancelling) {
			/* process_init already accounted this task's
			 * inflight slot above; mark it running so the
			 * completion below does not bump dsp_inflight a
			 * second time and leak the count. Park the task
			 * on the running list in the callback-running
			 * state: the drain claims only completed==0
			 * tasks, so it cannot take this one, and the
			 * completion callbacks of a cancelled task must
			 * still run before it is freed - a delayed-retry
			 * task put to sleep by tse_task_reinit() may
			 * carry unexecuted callbacks, and freeing it
			 * with a non-empty comp cb list trips the
			 * tse_task_decref() invariant. See DAOS-19955. */
			dtp->dtp_running = 1;
			d_list_move_tail(&dtp->dtp_list,
					 &dsp->dsp_running_list);
			atomic_store(&dtp->dtp_completed, DTP_CB_RUNNING);
			/** +1 while the callbacks run unlocked */
			tse_task_addref_locked(dtp);
		} else {
			dtp->dtp_running = 1;
			d_list_move_tail(&dtp->dtp_list,
					 &dsp->dsp_running_list);
			/** +1 in case prep cb calls task_complete() */
			tse_task_addref_locked(dtp);
		}
		D_MUTEX_UNLOCK(&dsp->dsp_lock);

		if (!cancelling) {
			/** if task is reinitialized in prep cb, skip over it */
			if (!tse_task_prep_callback(task)) {
				tse_task_decref(task);
				continue;
			}
			D_ASSERT(dtp->dtp_func != NULL);
			if (!atomic_load(&dtp->dtp_completed))
				dtp->dtp_func(task);
			tse_task_decref(task);
		} else {
			/* Run the cancelled task's completion callbacks
			 * here (unlocked, DTP_CB_RUNNING protects against
			 * a concurrent tse_task_complete()), then finish
			 * it exactly like the drain does: unlink, mark
			 * completed, post-process, and drop the
			 * scheduler reference of
			 * tse_task_schedule_with_delay(), the local
			 * callback pin, and the completion reference the
			 * drain's claim path would have dropped. */
			tse_task_complete_callback(task);

			D_MUTEX_LOCK(&dsp->dsp_lock);
			d_list_del_init(&dtp->dtp_list);
			atomic_store(&dtp->dtp_completed, 1);
			D_MUTEX_UNLOCK(&dsp->dsp_lock);

			tse_task_post_process(task);
			/* drop reference of tse_task_schedule() */
			tse_sched_priv_decref(dsp);
			tse_task_decref(task);
			tse_task_decref(task);
		}

		processed++;
	}
	return processed;
}

/**
 * Check the task in the complete list, dependent task status check, schedule status update etc. The
 * task will be moved to fini list after this.
 **/
static int
tse_task_post_process(tse_task_t *task)
{
	struct tse_task_private  *dtp = tse_task2priv(task);
	struct tse_sched_private *dsp = dtp->dtp_sched;
	int rc = 0;

	D_ASSERT(atomic_load(&dtp->dtp_completed) == 1);
	D_MUTEX_LOCK(&dsp->dsp_lock);

	/* set scheduler result */
	if (tse_priv2sched(dsp)->ds_result == 0)
		tse_priv2sched(dsp)->ds_result = task->dt_result;

	/* Check dependent list */
	while (!d_list_empty(&dtp->dtp_dep_list)) {
		struct tse_task_link		*tlink;
		tse_task_t			*task_tmp;
		struct tse_task_private		*dtp_tmp;
		struct tse_sched_private	*dsp_tmp;
		bool				 diff_sched;

		tlink = d_list_entry(dtp->dtp_dep_list.next,
				     struct tse_task_link, tl_link);
		d_list_del(&tlink->tl_link);
		task_tmp = tlink->tl_task;
		dtp_tmp = tse_task2priv(task_tmp);
		D_FREE(tlink);

		/* propagate dep task's failure */
		if (task_tmp->dt_result == 0 && !dtp_tmp->dtp_no_propagate)
			task_tmp->dt_result = task->dt_result;

		dsp_tmp = dtp_tmp->dtp_sched;
		diff_sched = dsp != dsp_tmp;

		if (diff_sched) {
			D_MUTEX_UNLOCK(&dsp->dsp_lock);
			D_MUTEX_LOCK(&dsp_tmp->dsp_lock);
		}
		/* see if the dependent task is ready to be scheduled */
		D_ASSERT(dtp_tmp->dtp_dep_cnt > 0);
		dtp_tmp->dtp_dep_cnt--;
		D_DEBUG(DB_TRACE, "daos task %p dep_cnt %d\n", dtp_tmp,
			dtp_tmp->dtp_dep_cnt);
		if (!dsp_tmp->dsp_cancelling && dtp_tmp->dtp_dep_cnt == 0 &&
		    dtp_tmp->dtp_running) {
			bool done;

			/*
			 * If the task is already running, let's mark it
			 * complete. This happens when we create subtasks in the
			 * body function of the main task. So the task function
			 * is done, but it will stay in the running state until
			 * all the tasks that it depends on are completed, then
			 * it is completed when they completed in this code
			 * block.
			 */
			/* Mark the parent as executing its completion
			 * callbacks so the drain cannot claim it while
			 * the callbacks run here without the lock (it
			 * would traverse and free callback nodes this
			 * traversal still advances to). See DAOS-19955.
			 */
			D_ASSERT(dtp_tmp->dtp_completed == 0);
			dtp_tmp->dtp_completed = DTP_CB_RUNNING;
			/** release lock for CB */
			D_MUTEX_UNLOCK(&dsp_tmp->dsp_lock);
			done = tse_task_complete_callback(task_tmp);
			D_MUTEX_LOCK(&dsp_tmp->dsp_lock);

			/*
			 * task reinserted itself in scheduler by
			 * calling tse_task_reinit().
			 */
			if (!done) {
				/* back to the live state: the task is
				 * in flight again via reinit */
				dtp_tmp->dtp_completed = 0;
				/* -1 for tlink (addref by add_dependent) */
				tse_task_decref_free_locked(task_tmp);
				continue;
			}
			/* Callbacks finished and the list is empty now,
			 * so a drain claiming the task next cannot race
			 * this traversal. Restore the live state; the
			 * complete_locked below (not cancelling) or the
			 * drain (cancelling) marks completion and owns
			 * the unlink and the scheduler reference.
			 * See DAOS-19955. */
			dtp_tmp->dtp_completed = 0;

			tse_task_complete_locked(dtp_tmp, dsp_tmp);
		}

		/* -1 for tlink (addref by add_dependent) */
		tse_task_decref_free_locked(task_tmp);
		if (diff_sched) {
			D_MUTEX_UNLOCK(&dsp_tmp->dsp_lock);
			D_MUTEX_LOCK(&dsp->dsp_lock);
		}
	}

	D_ASSERT(dsp->dsp_inflight > 0);
	dsp->dsp_inflight--;
	D_MUTEX_UNLOCK(&dsp->dsp_lock);

	return rc;
}

int
tse_sched_process_complete(struct tse_sched_private *dsp)
{
	struct tse_task_private *dtp;
	struct tse_task_private *tmp;
	d_list_t comp_list;
	int processed = 0;

	/* pick tasks from complete_list */
	D_INIT_LIST_HEAD(&comp_list);
	D_MUTEX_LOCK(&dsp->dsp_lock);
	d_list_splice_init(&dsp->dsp_complete_list, &comp_list);
	D_MUTEX_UNLOCK(&dsp->dsp_lock);

	d_list_for_each_entry_safe(dtp, tmp, &comp_list, dtp_list) {
		tse_task_t *task = tse_priv2task(dtp);

		d_list_del_init(&dtp->dtp_list);
		tse_task_post_process(task);
		/* addref when the task add to dsp (tse_task_schedule) */
		tse_sched_priv_decref(dsp);
		tse_task_decref(task);  /* drop final ref */
		processed++;
	}
	return processed;
}

bool
tse_sched_is_cancelling(tse_sched_t *sched)
{
	struct tse_sched_private *dsp = tse_sched2priv(sched);
	bool				   cancelling;

	D_MUTEX_LOCK(&dsp->dsp_lock);
	cancelling = dsp->dsp_cancelling != 0;
	D_MUTEX_UNLOCK(&dsp->dsp_lock);

	return cancelling;
}

bool
tse_sched_check_complete(tse_sched_t *sched)
{
	struct tse_sched_private *dsp = tse_sched2priv(sched);
	bool completed;

	/* check if all tasks are done */
	D_MUTEX_LOCK(&dsp->dsp_lock);
	completed = (d_list_empty(&dsp->dsp_init_list) &&
		     d_list_empty(&dsp->dsp_sleeping_list) &&
		     dsp->dsp_inflight == 0);
	D_MUTEX_UNLOCK(&dsp->dsp_lock);

	return completed;
}

/* Run tasks for this schedule */
static void
tse_sched_run(tse_sched_t *sched)
{
	struct tse_sched_private *dsp = tse_sched2priv(sched);

	while (1) {
		int	processed = 0;
		bool	completed;

		processed += tse_sched_process_init(dsp);
		processed += tse_sched_process_complete(dsp);
		completed = tse_sched_check_complete(sched);
		if (completed || processed == 0)
			break;
	};

	/* drop reference of tse_sched_init() */
	tse_sched_priv_decref(dsp);
}

/*
 * Poke the scheduler to run tasks in the init list if ready, finish tasks that
 * have completed.
 */
void
tse_sched_progress(tse_sched_t *sched)
{
	struct tse_sched_private *dsp = tse_sched2priv(sched);

	if (dsp->dsp_cancelling)
		return;

	D_MUTEX_LOCK(&dsp->dsp_lock);
	/** +1 for tse_sched_run() */
	tse_sched_priv_addref_locked(dsp);
	D_MUTEX_UNLOCK(&dsp->dsp_lock);

	if (!dsp->dsp_cancelling)
		tse_sched_run(sched);
	/** If another thread canceled, drop the ref count */
	else
		tse_sched_priv_decref(dsp);
}

static int
tse_sched_complete_inflight(struct tse_sched_private *dsp)
{
	struct tse_task_private *dtp;
	struct tse_task_private *tmp;
	d_list_t			  list;
	int				  processed = 0;

	D_INIT_LIST_HEAD(&list);
	D_MUTEX_LOCK(&dsp->dsp_lock);
	d_list_for_each_entry_safe(dtp, tmp, &dsp->dsp_running_list,
				      dtp_list)
		if (dtp->dtp_dep_cnt == 0 && dtp->dtp_completed == 0) {
			d_list_move_tail(&dtp->dtp_list, &list);
			/* Mark the task completed and hold a reference
			 * while it is on the local list: a concurrent
			 * tse_task_complete() checks dtp_completed
			 * before touching the task and returns early,
			 * so its cancelling path cannot consume this
			 * reference. See DAOS-19955. */
			dtp->dtp_completed = 1;
			tse_task_addref_locked(dtp);
			processed++;
		}
	D_MUTEX_UNLOCK(&dsp->dsp_lock);

	while (!d_list_empty(&list)) {
		tse_task_t	*task;
		bool		 done;

		dtp = d_list_entry(list.next, struct tse_task_private,
				   dtp_list);
		task = tse_priv2task(dtp);

		/**
		 * Execute the completion callbacks before force-completing
		 * the task, as tse_task_post_process() does for dependent
		 * tasks. Completing a running task with pending callbacks
		 * breaks the tse_task_decref() invariant that the comp cb
		 * list is empty when the last reference is dropped.
		 * See DAOS-19955.
		 */
		done = tse_task_complete_callback(task);

		D_MUTEX_LOCK(&dsp->dsp_lock);
		d_list_del_init(&dtp->dtp_list);
		if (done) {
			/* complete the task now; post_process() below
			 * clears the dependency list, so it must run
			 * before any final reference drop */
			D_MUTEX_UNLOCK(&dsp->dsp_lock);

			tse_task_post_process(task);
			/* drop reference of tse_task_schedule() */
			tse_sched_priv_decref(dsp);
			/* drop final task reference */
			tse_task_decref(task);
		} else {
			tse_task_decref_free_locked(task);
			D_MUTEX_UNLOCK(&dsp->dsp_lock);
		}
		/* drop the reference taken when the task was moved to
		 * the local list (may free the task) */
		tse_task_decref(task);
	}

	return processed;
}

void
tse_sched_complete(tse_sched_t *sched, int ret, bool cancel)
{
	struct tse_sched_private *dsp = tse_sched2priv(sched);

	if (sched->ds_result == 0)
		sched->ds_result = ret;

	D_MUTEX_LOCK(&dsp->dsp_lock);
	if (dsp->dsp_cancelling || dsp->dsp_completing) {
		D_MUTEX_UNLOCK(&dsp->dsp_lock);
		return;
	}

	if (cancel)
		dsp->dsp_cancelling = 1;
	else
		dsp->dsp_completing = 1;

	/**
	 * Wait for all in-flight tasks. Hold one reference for the
	 * whole loop: tse_sched_run() drops its own reference each
	 * iteration, and without an extra hold the refcount can reach
	 * zero mid-loop, finalizing the scheduler (destroying
	 * dsp_lock) while this loop still touches it.
	 */
	tse_sched_priv_addref_locked(dsp);
	D_MUTEX_UNLOCK(&dsp->dsp_lock);

	while (1) {
		D_MUTEX_LOCK(&dsp->dsp_lock);
		/** +1 for tse_sched_run */
		tse_sched_priv_addref_locked(dsp);
		D_MUTEX_UNLOCK(&dsp->dsp_lock);

		tse_sched_run(sched);
		if (dsp->dsp_inflight == 0)
			break;

		/* Tasks waiting on in-flight RPCs only complete when
		 * somebody pumps the context; after the workers exit,
		 * that somebody must be this drain loop itself, or
		 * dsp_inflight never reaches zero (observed hang).
		 * ds_udata holds the crt context for schedulers
		 * created by daos_eq_lib_init(). See DAOS-19955. */
		if (sched->ds_udata != NULL)
			crt_progress((crt_context_t)sched->ds_udata, 0);
		D_MUTEX_LOCK(&dsp->dsp_lock);
		if (dsp->dsp_inflight == 0) {
			D_MUTEX_UNLOCK(&dsp->dsp_lock);
			break;
		}
		D_MUTEX_UNLOCK(&dsp->dsp_lock);
		if (dsp->dsp_cancelling)
			tse_sched_complete_inflight(dsp);
	}

	tse_sched_complete_cb(sched);
	sched->ds_udata = NULL;
	/* drop the loop-wide reference taken above, then the initial
	 * reference tse_sched_init() took, restoring the original
	 * complete-consumes-init semantics for ordinary schedulers.
	 * A generation scheduler keeps its pre-drain hold, so the
	 * object survives for stale waiters. See DAOS-19962. */
	tse_sched_priv_decref(dsp);
	tse_sched_priv_decref(dsp);
}

void
tse_task_complete(tse_task_t *task, int ret)
{
	struct tse_task_private		*dtp	= tse_task2priv(task);
	struct tse_sched_private	*dsp	= dtp->dtp_sched;
	bool				done;

	if (atomic_load(&dtp->dtp_completed))
		return;

	if (task->dt_result == 0)
		task->dt_result = ret;

	/*
	 * Mark the task as executing its completion callbacks before
	 * running them: the callbacks run without dsp_lock, and the
	 * drain (tse_sched_complete_inflight) must not collect a task
	 * whose callback list is being traversed, or it would free
	 * callback nodes the traversal still advances to. The drain
	 * skips tasks in the DTP_CB_RUNNING state and lets this path
	 * finish the completion (including the cancelling branch
	 * below). See DAOS-19955.
	 */
	D_MUTEX_LOCK(&dsp->dsp_lock);
	if (dtp->dtp_completed) {
		D_MUTEX_UNLOCK(&dsp->dsp_lock);
		return;
	}
	dtp->dtp_completed = DTP_CB_RUNNING;
	D_MUTEX_UNLOCK(&dsp->dsp_lock);

	/** Execute task completion callbacks first. */
	done = tse_task_complete_callback(task);

	D_MUTEX_LOCK(&dsp->dsp_lock);
	if (dtp->dtp_completed == DTP_CB_RUNNING) {
		if (dsp->dsp_cancelling) {
			/* The scheduler is being cancelled, so the drain
			 * (tse_sched_complete_inflight) can claim this
			 * task the moment the lock is dropped. Do not
			 * restore the live state (completed = 0):
			 * between that restore and the cancelling
			 * branch below, the drain could claim the task
			 * and both sides would drop the scheduler
			 * reference the task owes, underflowing
			 * dsp_refcount. Finalize the completed state
			 * here and let the cancelling branch below
			 * finish the teardown; it re-sets completed,
			 * unlinks and drops the reference exactly once.
			 * See DAOS-19955. */
			dtp->dtp_completed = 1;
		} else {
			/* Not cancelling: the drain never runs while
			 * dsp_cancelling is clear, so restoring the live
			 * state is race-free. tse_task_complete_locked()
			 * needs it: it marks completion itself. */
			dtp->dtp_completed = 0;
		}
	} else {
		/* The drain (tse_sched_complete_inflight) claimed this task
	 * while the completion callbacks ran without the lock above;
		 * it owns the scheduler reference and the unlink now. */
		D_MUTEX_UNLOCK(&dsp->dsp_lock);
		return;
	}
	if (!dsp->dsp_cancelling) {
		/** if task reinserted itself in scheduler, don't complete */
		if (done)
			tse_task_complete_locked(dtp, dsp);
	} else {
		/* The scheduler is being cancelled (drain). Account
		 * this task's in-flight slot before unlinking it:
		 * post_process() will never run for it, and a leaked
		 * count keeps tse_sched_complete() spinning forever.
		 * Only free the task when this call holds the last
		 * reference; if the drain still pins it (v11/v12),
		 * leave that reference for the drain to drop, or the
		 * late mercury callback drops a freed counter again.
		 * See DAOS-19955. */
		if (dtp->dtp_running) {
			dtp->dtp_running = 0;
			D_ASSERT(dsp->dsp_inflight > 0);
			dsp->dsp_inflight--;
		}
		if (!d_list_empty(&dtp->dtp_list)) {
			d_list_del_init(&dtp->dtp_list);
			dtp->dtp_completed = 1;
			/* the task is leaving the scheduler lists
			 * without tse_sched_process_complete(); drop
			 * the scheduler reference taken by
			 * tse_task_schedule_with_delay(). A task
			 * completed before it was ever scheduled
			 * (event launch rejected under cancel) owns no
			 * scheduler reference: an empty dtp_list marks
			 * it, and dropping one here underflowed the
			 * count. See DAOS-19955. */
			tse_sched_priv_decref_locked(dsp);
		} else {
			dtp->dtp_completed = 1;
		}
		/* Release any dependency links the same way
		 * tse_task_post_process() does: a late completion of a
		 * task with unresolved dependencies must not leave
		 * tlinks behind, the final decref asserts the dep list
		 * is empty. The dependent's dep_cnt must also drop, or
		 * it can never be selected by the drain. All drops
		 * happen with dsp_lock released, since the dependents
		 * can share this (non-recursive) scheduler lock.
		 * See DAOS-19955. */
		while (!d_list_empty(&dtp->dtp_dep_list)) {
			struct tse_task_link	*tlink;
			tse_task_t		*dep_task;
			struct tse_task_private *dep_dtp;
			struct tse_sched_private *dep_dsp;

			tlink = d_list_entry(dtp->dtp_dep_list.next,
					     struct tse_task_link, tl_link);
			d_list_del(&tlink->tl_link);
			dep_task = tlink->tl_task;
			D_FREE(tlink);
			if (dep_task == NULL)
				continue;

			dep_dtp = tse_task2priv(dep_task);
			dep_dsp = dep_dtp->dtp_sched;

			/* A dependent on the same scheduler shares this
			 * (non-recursive) lock: keep it held and adjust the
			 * dependency count under it. Only a cross-scheduler
			 * dependent needs a lock switch, and those are
			 * different mutexes, so no unlock-lock race exists.
			 * Do NOT complete the dependent here: it stays on
			 * its scheduler lists with its own in-flight
			 * accounting, and the drain (or the worker completing
			 * it) owns its completion. Completing it here left it
			 * on the running list and double-counted its
			 * in-flight slot. See DAOS-19955. */
			if (dep_dsp != dsp) {
				D_MUTEX_UNLOCK(&dsp->dsp_lock);
				D_MUTEX_LOCK(&dep_dsp->dsp_lock);
				D_ASSERT(dep_dtp->dtp_dep_cnt > 0);
				dep_dtp->dtp_dep_cnt--;
				/* drop the reference the link took on the
				 * dependent, like tse_task_post_process().
				 * The dependent may already have left its
				 * scheduler lists (init-list cancellation
				 * finished it without this link's
				 * knowledge), so this can be its final
				 * reference: free it the same way post-
				 * processing would. See DAOS-19955. */
				tse_task_decref_free_locked(dep_task);
				D_MUTEX_UNLOCK(&dep_dsp->dsp_lock);
				D_MUTEX_LOCK(&dsp->dsp_lock);
			} else {
				D_ASSERT(dep_dtp->dtp_dep_cnt > 0);
				dep_dtp->dtp_dep_cnt--;
				/* same scheduler: dsp_lock already covers
				 * the dependent; drop the link reference,
				 * freeing on the final reference like
				 * tse_task_post_process(). See DAOS-19955. */
				tse_task_decref_free_locked(dep_task);
			}
		}
		if (dtp->dtp_refcnt == 1)
			tse_task_decref_free_locked(task);
		else
			tse_task_decref_locked(dtp);
	}
	D_MUTEX_UNLOCK(&dsp->dsp_lock);

	/** update task in scheduler lists. */
	if (!dsp->dsp_cancelling && done)
		tse_sched_process_complete(dsp);
}

/**
 * If one task dependents on other tasks, only if the dependent task
 * is done, then the task can be added to the scheduler list
 **/
static int
tse_task_add_dependent(tse_task_t *task, tse_task_t *dep)
{
	struct tse_task_private	*dtp = tse_task2priv(task);
	struct tse_task_private	*dep_dtp = tse_task2priv(dep);
	struct tse_task_link	*tlink;
	bool			 diff_sched;

	D_ASSERT(task != dep);

	if (atomic_load(&dtp->dtp_completed) == 1) {
		D_ERROR("Can't add a dependency for a completed task (%p)\n",
			task);
		return -DER_NO_PERM;
	}

	/** if task to depend on has completed already, do nothing */
	if (atomic_load(&dep_dtp->dtp_completed) == 1)
		return 0;

	/* Adding dependencies while the scheduler is being cancelled
	 * lets completion callbacks arm cross-references that outlive
	 * the drain. Reject like reinit does. See DAOS-19955. */
	if (dtp->dtp_sched->dsp_cancelling || dep_dtp->dtp_sched->dsp_cancelling)
		return -DER_NO_PERM;

	diff_sched = dtp->dtp_sched != dep_dtp->dtp_sched;

	D_ALLOC_PTR(tlink);
	if (tlink == NULL)
		return -DER_NOMEM;

	D_DEBUG(DB_TRACE, "Add dependent %p ---> %p\n", dep, task);

	D_MUTEX_LOCK(&dtp->dtp_sched->dsp_lock);
	D_ASSERT(dtp->dtp_dep_cnt < UINT16_MAX);
	tse_task_addref_locked(dtp);
	tlink->tl_task = task;
	dtp->dtp_dep_cnt++;
	dtp_generation_inc(dtp);
	if (!diff_sched)
		d_list_add_tail(&tlink->tl_link, &dep_dtp->dtp_dep_list);
	D_MUTEX_UNLOCK(&dtp->dtp_sched->dsp_lock);

	if (diff_sched) {
		D_MUTEX_LOCK(&dep_dtp->dtp_sched->dsp_lock);
		d_list_add_tail(&tlink->tl_link, &dep_dtp->dtp_dep_list);
		D_MUTEX_UNLOCK(&dep_dtp->dtp_sched->dsp_lock);
	}

	return 0;
}

int
tse_task_register_deps(tse_task_t *task, int num_deps,
		       tse_task_t *dep_tasks[])
{
	int i;
	int rc;

	for (i = 0; i < num_deps; i++) {
		rc = tse_task_add_dependent(task, dep_tasks[i]);
		if (rc)
			return rc;
	}
	return 0;
}

int
tse_task_create(tse_task_func_t task_func, tse_sched_t *sched, void *priv,
		tse_task_t **taskp)
{
	struct tse_sched_private *dsp = tse_sched2priv(sched);
	struct tse_task_private	 *dtp;
	tse_task_t		 *task;

	D_ALLOC_PTR(task);
	if (task == NULL)
		return -DER_NOMEM;

	dtp = tse_task2priv(task);
	D_CASSERT(sizeof(task->dt_private) >= sizeof(*dtp));

	D_INIT_LIST_HEAD(&dtp->dtp_list);
	D_INIT_LIST_HEAD(&dtp->dtp_task_list);
	D_INIT_LIST_HEAD(&dtp->dtp_dep_list);
	D_INIT_LIST_HEAD(&dtp->dtp_comp_cb_list);
	D_INIT_LIST_HEAD(&dtp->dtp_prep_cb_list);

	dtp->dtp_refcnt   = 1;
	dtp->dtp_func	  = task_func;
	dtp->dtp_priv	  = priv;
	dtp->dtp_sched	  = dsp;

	*taskp = task;
	return 0;
}

/* Insert dtp to the sleeping list of dsp. */
static void
tse_task_insert_sleeping(struct tse_task_private *dtp,
			 struct tse_sched_private *dsp)
{
	struct tse_task_private *t;

	if (d_list_empty(&dsp->dsp_sleeping_list)) {
		d_list_add_tail(&dtp->dtp_list, &dsp->dsp_sleeping_list);
		return;
	}

	/* If this task < the head, we don't need to search the list. */
	t = d_list_entry(dsp->dsp_sleeping_list.next, struct tse_task_private,
			 dtp_list);
	if (dtp->dtp_wakeup_time < t->dtp_wakeup_time) {
		/* Insert before the head. */
		d_list_add(&dtp->dtp_list, &dsp->dsp_sleeping_list);
		return;
	}

	/*
	 * Search from the tail. Because this task >= the head, the search must
	 * have a hit.
	 */
	d_list_for_each_entry_reverse(t, &dsp->dsp_sleeping_list, dtp_list) {
		if (t->dtp_wakeup_time <= dtp->dtp_wakeup_time) {
			/* Insert after t. */
			d_list_add(&dtp->dtp_list, &t->dtp_list);
			return;
		}
	}
	D_ASSERT(false);
}

int
tse_task_schedule_with_delay(tse_task_t *task, bool instant, uint64_t delay)
{
	struct tse_task_private		*dtp = tse_task2priv(task);
	struct tse_sched_private	*dsp = dtp->dtp_sched;
	bool				ready;
	int				rc = 0;

	D_ASSERT(!instant || (dtp->dtp_func && delay == 0));

	/* Add task to scheduler */
	D_MUTEX_LOCK(&dsp->dsp_lock);
	/* A scheduler being cancelled (drained for teardown) must not
	 * admit new work: the drain loop would spin while the task's
	 * error paths keep re-scheduling, and every admitted task drags
	 * a scheduler reference into exactly the window the drain is
	 * releasing its own. Force-complete the task instead: register
	 * it like an instant run and mark it cancelled under this lock,
	 * so the caller's completion callbacks still execute (mirrors
	 * tse_sched_complete_inflight) and the reference pairing stays
	 * intact. See DAOS-19955. */
	if (dsp->dsp_cancelling) {
		dsp->dsp_inflight++;
		dtp->dtp_running = 1;
		dtp->dtp_wakeup_time = 0;
		task->dt_result = -DER_CANCELED;
		d_list_add_tail(&dtp->dtp_list, &dsp->dsp_running_list);
		/* Hold a task reference across the unlocked completion
		 * below: the drain may claim the task between the unlock
		 * and tse_task_complete(), and its final decref would
		 * otherwise free the task under us. The cancelling
		 * branch drops the scheduler reference taken here; this
		 * extra task reference is released after completion.
		 * See DAOS-19955. */
		tse_task_addref_locked(dtp);
		tse_sched_priv_addref_locked(dsp);
		D_MUTEX_UNLOCK(&dsp->dsp_lock);

		if (instant) {
			dtp->dtp_func(task);
		} else {
			/* No body to run here; complete it now so it
			 * never waits for a drain that outruns it. */
			tse_task_complete(task, -DER_CANCELED);
		}
		tse_task_decref(task);
		return 0;
	}
	ready = (dtp->dtp_dep_cnt == 0 && d_list_empty(&dtp->dtp_prep_cb_list));
	if ((dtp->dtp_func == NULL || instant) && ready) {
		/** If task has no body function, mark it as running */
		dsp->dsp_inflight++;
		dtp->dtp_running = 1;
		dtp->dtp_wakeup_time = 0;
		d_list_add_tail(&dtp->dtp_list, &dsp->dsp_running_list);

		/** +1 in case task is completed in body function */
		if (instant)
			tse_task_addref_locked(dtp);
	} else if (delay == 0) {
		/** Otherwise, scheduler will process it from init list */
		dtp->dtp_wakeup_time = 0;
		d_list_add_tail(&dtp->dtp_list, &dsp->dsp_init_list);
	} else {
		/* A delay is requested; insert into the sleeping list. */
		dtp->dtp_wakeup_time = daos_getutime() + delay;
		tse_task_insert_sleeping(dtp, dsp);
	}
	/* decref when remove the task from dsp (tse_sched_process_complete) */
	tse_sched_priv_addref_locked(dsp);
	D_MUTEX_UNLOCK(&dsp->dsp_lock);

	/** if caller wants to run the task instantly, call the task body function now. */
	if (instant && ready) {
		/** result of task should be set in dt_result and checked by caller. */
		dtp->dtp_func(task);
		tse_task_decref(task);
	}
	return rc;
}

int
tse_task_schedule(tse_task_t *task, bool instant)
{
	return tse_task_schedule_with_delay(task, instant, 0);
}

int
tse_task_reinit_with_delay(tse_task_t *task, uint64_t delay)
{
	struct tse_task_private		*dtp = tse_task2priv(task);
	tse_sched_t			*sched = tse_task2sched(task);
	struct tse_sched_private	*dsp = tse_sched2priv(sched);
	int				rc;

	D_CASSERT(sizeof(task->dt_private) >= sizeof(*dtp));

	D_MUTEX_LOCK(&dsp->dsp_lock);

	if (dsp->dsp_cancelling) {
		D_ERROR("Scheduler is canceling, can't re-insert task\n");
		D_GOTO(err_unlock, rc = -DER_NO_PERM);
	}

	if (dtp->dtp_func == NULL) {
		D_ERROR("Task body function can't be NULL.\n");
		D_GOTO(err_unlock, rc = -DER_INVAL);
	}

	if (dtp->dtp_completed == 1) {
		D_ASSERT(d_list_empty(&dtp->dtp_list));
		/* +1 ref for valid until complete */
		tse_task_addref_locked(dtp);
		/* +1 dsp ref as will add back to dsp again below */
		tse_sched_priv_addref_locked(dsp);
	} else if (dtp->dtp_running) {
		/** Task not in-flight anymore */
		dsp->dsp_inflight--;
	} else {
		D_ERROR("Can't re-init a task that is not running or "
			"completed.\n");
		D_GOTO(err_unlock, rc = -DER_NO_PERM);
	}

	/** Mark the task back at init state */
	dtp->dtp_running = 0;
	dtp->dtp_completed = 0;

	dtp_generation_inc(dtp);

	/** reset stack pointer as zero */
	if (dtp->dtp_stack_top != 0) {
		D_ERROR("task %p, dtp_stack_top reset from %d to zero.\n",
			task, dtp->dtp_stack_top);
		dtp->dtp_stack_top = 0;
	}

	task->dt_result = 0;

	/** Move back to init list */
	if (delay == 0) {
		dtp->dtp_wakeup_time = 0;
		d_list_move_tail(&dtp->dtp_list, &dsp->dsp_init_list);
	} else {
		dtp->dtp_wakeup_time = daos_getutime() + delay;
		d_list_del_init(&dtp->dtp_list);
		tse_task_insert_sleeping(dtp, dsp);
	}

	D_MUTEX_UNLOCK(&dsp->dsp_lock);

	return 0;

err_unlock:
	D_MUTEX_UNLOCK(&dsp->dsp_lock);
	return rc;
}

int
tse_task_reinit(tse_task_t *task)
{
	return tse_task_reinit_with_delay(task, 0 /* delay */);
}

int
tse_task_reset(tse_task_t *task, tse_task_func_t task_func, void *priv)
{
	struct tse_task_private		*dtp = tse_task2priv(task);
	tse_sched_t			*sched = tse_task2sched(task);
	struct tse_sched_private	*dsp = tse_sched2priv(sched);
	int				rc;

	D_CASSERT(sizeof(task->dt_private) >= sizeof(*dtp));

	D_MUTEX_LOCK(&dsp->dsp_lock);

	if (dsp->dsp_cancelling) {
		D_ERROR("Scheduler is canceling, can't reset task\n");
		D_GOTO(err_unlock, rc = -DER_NO_PERM);
	}

	if (dtp->dtp_completed != 1) {
		D_ERROR("Can't reset a task in init or running state.\n");
		D_GOTO(err_unlock, rc = -DER_NO_PERM);
	}

	if (!d_list_empty(&dtp->dtp_list)) {
		D_ERROR("task scheduler processing list should be empty\n");
		D_GOTO(err_unlock, rc = -DER_NO_PERM);
	}

	if (!d_list_empty(&dtp->dtp_task_list)) {
		D_ERROR("task user list should be empty\n");
		D_GOTO(err_unlock, rc = -DER_NO_PERM);
	}

	if (!d_list_empty(&dtp->dtp_dep_list)) {
		D_ERROR("task dep list should be empty\n");
		D_GOTO(err_unlock, rc = -DER_NO_PERM);
	}

	if (!d_list_empty(&dtp->dtp_comp_cb_list)) {
		D_ERROR("task completion CB list should be empty\n");
		D_GOTO(err_unlock, rc = -DER_NO_PERM);
	}

	if (!d_list_empty(&dtp->dtp_prep_cb_list)) {
		D_ERROR("task prep CB list should be empty\n");
		D_GOTO(err_unlock, rc = -DER_NO_PERM);
	}

	/** Mark the task back at init state */
	dtp->dtp_running = 0;
	dtp->dtp_completed = 0;

	/** reset stack pointer as zero */
	if (dtp->dtp_stack_top != 0) {
		D_ERROR("task %p, dtp_stack_top reset from %d to zero.\n",
			task, dtp->dtp_stack_top);
		dtp->dtp_stack_top = 0;
	}

	dtp->dtp_wakeup_time = 0;

	D_INIT_LIST_HEAD(&dtp->dtp_list);
	D_INIT_LIST_HEAD(&dtp->dtp_task_list);
	D_INIT_LIST_HEAD(&dtp->dtp_dep_list);
	D_INIT_LIST_HEAD(&dtp->dtp_comp_cb_list);
	D_INIT_LIST_HEAD(&dtp->dtp_prep_cb_list);

	dtp->dtp_func	  = task_func;
	dtp->dtp_priv	  = priv;
	dtp->dtp_sched	  = dsp;

	D_MUTEX_UNLOCK(&dsp->dsp_lock);

	task->dt_result = 0;

	return 0;

err_unlock:
	D_MUTEX_UNLOCK(&dsp->dsp_lock);
	return rc;
}

int
tse_task_list_add(tse_task_t *task, d_list_t *head)
{
	struct tse_task_private *dtp = tse_task2priv(task);

	D_ASSERT(d_list_empty(&dtp->dtp_task_list));
	d_list_add_tail(&dtp->dtp_task_list, head);
	return 0;
}

tse_task_t *
tse_task_list_first(d_list_t *head)
{
	struct tse_task_private	*dtp;

	if (d_list_empty(head))
		return NULL;

	dtp = d_list_entry(head->next, struct tse_task_private, dtp_task_list);
	return tse_priv2task(dtp);
}

void
tse_task_list_del(tse_task_t *task)
{
	struct tse_task_private *dtp = tse_task2priv(task);

	d_list_del_init(&dtp->dtp_task_list);
}

void
tse_task_list_sched(d_list_t *head, bool instant)
{
	while (!d_list_empty(head)) {
		tse_task_t *task = tse_task_list_first(head);

		tse_task_list_del(task);
		tse_task_schedule(task, instant);
	}
}

void
tse_task_list_abort(d_list_t *head, int rc)
{
	while (!d_list_empty(head)) {
		tse_task_t *task = tse_task_list_first(head);

		tse_task_list_del(task);
		tse_task_complete(task, rc);
	}
}

int
tse_task_list_depend(d_list_t *head, tse_task_t *task)
{
	struct tse_task_private *dtp;
	int			 rc;

	d_list_for_each_entry(dtp, head, dtp_task_list) {
		rc = tse_task_add_dependent(tse_priv2task(dtp), task);
		if (rc)
			return rc;
	}
	return 0;
}

int
tse_task_depend_list(tse_task_t *task, d_list_t *head)
{
	struct tse_task_private *dtp;
	int			 rc;

	d_list_for_each_entry(dtp, head, dtp_task_list) {
		rc = tse_task_add_dependent(task, tse_priv2task(dtp));
		if (rc)
			return rc;
	}
	return 0;
}

int
tse_task_list_traverse(d_list_t *head, tse_task_cb_t cb, void *arg)
{
	struct tse_task_private	*dtp;
	struct tse_task_private	*tmp;
	int			 ret = 0;
	int			 rc;

	d_list_for_each_entry_safe(dtp, tmp, head, dtp_task_list) {
		rc = cb(tse_priv2task(dtp), arg);
		if (rc != 0)
			ret = rc;
	}

	return ret;
}

int
tse_task_list_traverse_adv(d_list_t *head, tse_task_cb_t cb, void *arg)
{
	struct tse_task_private	*dtp;
	struct tse_task_private	*dtp_exec;
	struct tse_task_private	*tmp;
	int			 ret = 0;
	int			 rc;
	bool			 done;

	for (dtp = d_list_entry(head->next, struct tse_task_private, dtp_task_list),
	     tmp = d_list_entry(dtp->dtp_task_list.next, struct tse_task_private, dtp_task_list),
	     done = (&dtp->dtp_task_list == head); !done;) {
		dtp_exec = dtp;
		dtp = tmp,
		tmp = d_list_entry(tmp->dtp_task_list.next, struct tse_task_private,
				   dtp_task_list);
		done = (&dtp->dtp_task_list == head);
		rc = cb(tse_priv2task(dtp_exec), arg);
		if (rc != 0)
			ret = rc;
	}

	return ret;
}

void
tse_disable_propagate(tse_task_t *task)
{
	struct tse_task_private  *dtp = tse_task2priv(task);

	dtp->dtp_no_propagate = 1;
}
