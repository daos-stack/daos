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
 * Internal event API.
 */

#ifndef __DAOS_EVENTX_H__
#define __DAOS_EVENTX_H__

#include <daos_types.h>
#include <daos_errno.h>
#include <gurt/list.h>
#include <gurt/hash.h>
#include <daos_task.h>
#include <daos/task.h>

enum daos_ev_flags {
	/**
	 * An event will be queued in EQ on completion and wait for polling
	 * by default. With this flag, the completed event will not be queued
	 * in the EQ anymore, upper level stack is supposed to be notified by
	 * callback.
	 */
	DAOS_EVF_NO_POLL	= (1 << 0),
	/**
	 * Only useful for parent event:
	 * without this flag, a parent event will be automatically launched
	 * if any child event is launched. With this flag, a parent event
	 * always needs to be explicitly launched.
	 */
	DAOS_EVF_NEED_LAUNCH	= (1 << 1),
};

struct tse_task_t;

/**
 * Finish event queue library.
 */
int
daos_eq_lib_fini(void);

/**
 * Initialize event queue library.
 */
int
daos_eq_lib_init(void);

crt_context_t *
daos_task2ctx(tse_task_t *task);

/**
 * Initialize a new event for \a eqh
 *
 * \param ev [IN]	event to initialize
 * \param flags [IN]	see daos_ev_flags
 * \param eqh [IN]	where the event to be queued on, it's ignored if
 *			\a parent is specified
 * \param parent [IN]	"parent" event, it can be NULL if no parent event.
 *			If it's not NULL, caller will never see completion
 *			of this event, instead he will only see completion
 *			of \a parent when all children of \a parent are
 *			completed.
 *
 * \return		zero on success, negative value if error
 */
int daos_event_init_adv(struct daos_event *ev, enum daos_ev_flags flags,
			daos_handle_t eqh, struct daos_event *parent);

/**
 * Mark the event completed, i.e. move this event
 * to completion list.
 *
 * \param ev [IN]	event to complete.
 * \param rc [IN]	operation return code
 */
void
daos_event_complete(daos_event_t *ev, int rc);

/**
 * Mark the event launched, i.e. move this event to running list.
 *
 * \param ev		[IN]	event to launch.
 */
int
daos_event_launch(struct daos_event *ev);

/**
 * Return transport context associated with a particular event
 *
 * \param ev [IN]	event to retrieve context.
 */
crt_context_t
daos_ev2ctx(struct daos_event *ev);

tse_sched_t *
daos_ev2sched(struct daos_event *ev);

/**
 * Return the EQ handle of the specified event.
 *
 * \param ev [IN]	event to retrieve handle.
 */
daos_handle_t
daos_ev2eqh(struct daos_event *ev);

int
daos_event_destroy(struct daos_event *ev, bool force);

int
daos_event_destroy_children(struct daos_event *ev, bool force);


/**
 * Wait for completion of the private event
 * This function is deprecated, use dc_task_new() and dc_task_schedule()
 * instead of it.
 */
int
daos_event_priv_wait();

/**
 * Check whether \a ev is the private per-thread event
 *
 * \param ev [IN]	input event to compare with the private event.
 */
bool
daos_event_is_priv(daos_event_t *ev);

/**
 * Create a new task with the input scheduler \a sched, and associate
 * the task with the input event \a ev.
 *
 * If the input scheduler is NULL, the task will attach on the internal
 * scheduler of event/EQ.
 */
int
dc_task_create(tse_task_func_t func, tse_sched_t *sched, daos_event_t *ev,
	       tse_task_t **taskp);

/**
 * Schedule the task created by dc_task_create, this function will execute
 * the body function instantly if \a instant is true.
 */
int
dc_task_schedule(tse_task_t *task, bool instant);

/** return embedded parameters of task created by dc_task_create */
void *
dc_task_get_args(tse_task_t *task);

/** set opc of the task */
void
dc_task_set_opc(tse_task_t *task, uint32_t opc);

/** get opc of the task */
uint32_t
dc_task_get_opc(tse_task_t *task);

/* It's a little confusing to use both tse_task_* and dc_task_* at the same
 * time, we probably want to use macros to wrap all tse_task_* functions?
 *
 * NB: There are still a bunch of functions w/o wrappers.
 */
#define dc_task_addref(task)					\
	tse_task_addref(task)

#define dc_task_decref(task)					\
	tse_task_decref(task)

#define dc_task_set_priv(task, priv)				\
	tse_task_set_priv_internal(task, priv)

#define dc_task_get_priv(task)					\
	tse_task_get_priv_internal(task)

#define dc_task_list_add(task, head)				\
	tse_task_list_add(task, head)

#define dc_task_list_del(task)					\
	tse_task_list_del(task)

#define dc_task_list_first(head)				\
	tse_task_list_first(head)

#define dc_task_list_depend(head, task)				\
	tse_task_list_depend(head, task)

#define dc_task_depend_list(task, head)				\
	tse_task_depend_list(task, head)

#define dc_task_reg_comp_cb(task, comp_cb, arg, arg_size)	\
	tse_task_register_comp_cb(task, comp_cb, arg, arg_size)

#define dc_task_depend(task, dep_nr, dep_tasks)			\
	tse_task_register_deps(task, dep_nr, dep_tasks)

#define dc_task_resched(task)					\
	tse_task_reinit(task)

void
dc_task_list_sched(d_list_t *head, bool instant);

/* Get CART context on client side */
crt_context_t
daos_get_crt_ctx(void);

#endif /*  __DAOS_EV_INTERNAL_H__ */
