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
 * DAOS Event Queue (EQ) and Event
 *
 * DAOS APIs can run either in non-blocking mode or in blocking mode:
 *
 * - Non-blocking mode
 *   If input event(daos_event_t) of API is not NULL, it will run in
 *   non-blocking mode and return immediately after submitting API request
 *   to underlying stack.
 *   Returned value of API is zero on success, or negative error code only if
 *   there is an invalid parameter or other failure which can be detected
 *   without calling into server stack.
 *   Error codes for all other failures will be returned by event::ev_error.
 *
 * - Blocking mode
 *   If input event of API is NULL, it will run in blocking mode and return
 *   after completing of operation. Error codes for all failure cases should
 *   be returned by return value of API.
 */

#ifndef __DAOS_EVENT_H__
#define __DAOS_EVENT_H__

#include <daos_types.h>
#include <daos_errno.h>

#if !defined(container_of)
/**
 * Given a pointer @ptr to the field @member embedded into type (usually
 * struct) @type, return pointer to the embedding instance of @type.
 */
# define container_of(ptr, type, member)                \
		((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))
#endif

/**
 * create an Event Queue
 *
 * \param eq [OUT]	returned EQ handle
 *
 * \return		zero on success, negative value if error
 */
int
daos_eq_create(daos_handle_t *eqh);

#define DAOS_EQ_DESTROY_FORCE	1
/**
 * Destroy an Event Queue, it wait -EBUSY if EQ is not empty.
 *
 * \param eqh [IN]	EQ to finalize
 * \param ev [IN]	pointer to completion event
 * \param flags [IN]	flags to indicate the behavior of the destroy.
 *
 * \return		zero on success, EBUSY if there's any inflight event
 */
int
daos_eq_destroy(daos_handle_t eqh, int flags);

/**
 * Retrieve completion events from an EQ
 *
 * \param eqh [IN]	EQ handle
 * \param wait_inflight [IN]
 *			wait only if there's inflight event
 * \param timeout [IN]	how long is caller going to wait (micro-second)
 *			if \a timeout > 0,
 *			it can also be DAOS_EQ_NOWAIT, DAOS_EQ_WAIT
 * \param nevents [IN]	size of \a events array, returned number of events
 *			should always be less than or equal to \a nevents
 * \param events [OUT]	pointer to returned events array
 *
 * \return		>= 0	returned number of events
 *			< 0	negative value if error
 */
int
daos_eq_poll(daos_handle_t eqh, int wait_inflight,
	     int64_t timeout, unsigned int nevents, daos_event_t **events);

/**
 * Query how many outstanding events in EQ, if \a events is not NULL,
 * these events will be stored into it.
 *
 * Events returned by query are still owned by DAOS, it's not allowed to
 * finalize or free events returned by this function, but it's allowed
 * to call daos_event_abort() to abort inflight operation.
 *
 * Also, status of returned event could be still in changing, for example,
 * returned "inflight" event can be turned to "completed" before acessing.
 * It's user's responsibility to guarantee that returned events would be
 * freed by polling process.
 *
 * \param eqh [IN]	EQ handle
 * \param mode [IN]	query mode
 * \param nevents [IN]	size of \a events array
 * \param events [OUT]	pointer to returned events array
 * \return		>= 0	returned number of events
 *			 < 0	negative value if error
 */
int
daos_eq_query(daos_handle_t eqh, daos_eq_query_t query,
	      unsigned int nevents, daos_event_t **events);

/**
 * Initialize a new event for \a eq
 *
 * \param ev [IN]	event to initialize
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
int
daos_event_init(daos_event_t *ev, daos_handle_t eqh, daos_event_t *parent);

/**
 * Finalize an event. If event has been passed into any DAOS API, it can only
 * be finalized when it's been polled out from EQ, even it's aborted by
 * calling daos_event_abort().
 * Event will be removed from child-list of parent event if it's initialized
 * with parent. If \a ev itself is a parent event, then this function will
 * finalize all child events and \a ev.
 *
 * \param ev [IN]	event to finialize
 *
 * \return		zero on success, negative value if error
 */
int
daos_event_fini(daos_event_t *ev);

/**
 * Get the next child event of \a ev, it will return the first child event
 * if \a child is NULL.
 *
 * \param parent [IN]	parent event
 * \param child [IN]	current child event.
 *
 * \return		the next child event after \a child, or NULL if it's
 *			the last one.
 */
daos_event_t *
daos_event_next(daos_event_t *parent, daos_event_t *child);

/**
 * Try to abort operations associated with this event.
 * If \a ev is a parent event, this call will abort all child operations.
 *
 * \param ev [IN]	event (operation) to abort
 *
 * \return		zero on success, negative value if error
 */
int
daos_event_abort(daos_event_t *ev);
#endif /*  __DAOS_EVENT_H__ */
