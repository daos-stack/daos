/**
 * (C) Copyright 2015-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * DAOS Event Queue (EQ) and Event
 *
 * DAOS APIs can run either in non-blocking mode or in blocking mode:
 *
 * - Non-blocking mode
 *   If input event(daos_event_t) of API is not NULL, it will run in
 *   non-blocking mode and return immediately after submitting API requests
 *   to the underlying stack.
 *   The returned value of the API is zero on success, or negative error code
 *   only if there is an invalid parameter or other failure which can be
 *   detected without calling into the server stack.
 *   Error codes for all other failures will be returned by event::ev_error.
 *
 * - Blocking mode
 *   If input event of the API is NULL, it will run in blocking mode and return
 *   after completing the operation. Error codes for all failure cases should
 *   be returned by the return value of the API.
 */

#ifndef __DAOS_EVENT_H__
#define __DAOS_EVENT_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <daos_types.h>
#include <daos_errno.h>

/**
 * Create an Event Queue. An event queue is used to hold and pool multiple events. Each event queue
 * created will create a network (cart) context to be associated with the event queue. The network
 * context creation is an expensive operation, and the number of network context can be limited on
 * some systems. Thus, it is advisable to not create a lot of event queues in a user application or
 * middleware.
 *
 * \param[out] eq	Returned EQ handle
 *
 * \return		Zero on success, negative value if error
 */
int
daos_eq_create(daos_handle_t *eqh);

#define DAOS_EQ_DESTROY_FORCE	1
/**
 * Destroy an Event Queue, it returns -DER_BUSY if EQ is not empty.
 *
 * \param[in] eqh	EQ to finalize
 * \param[in] ev	Pointer to completion event
 * \param[in] flags	Flags to indicate the behavior of the destroy.
 *
 * \return		Zero on success, -DER_BUSY if there is any launched event
 */
int
daos_eq_destroy(daos_handle_t eqh, int flags);

/**
 * Retrieve completion events from an EQ
 *
 * \param[in] eqh	EQ handle
 * \param[in] wait_running Wait only if there are running event. Some events
 *			maybe initialized but not running. This selects
 *			whether to wait only on events that are running or all.
 * \param[in] timeout	How long is caller going to wait (micro-second)
 *			if \a timeout > 0,
 *			it can also be DAOS_EQ_NOWAIT, DAOS_EQ_WAIT
 * \param[in] nevents	Size of \a events array, returned number of events
 *			should always be less than or equal to \a nevents
 * \param[out] events	Pointer to returned events array
 *
 * \return		>= 0	Returned number of events
 *			< 0	negative value if error
 */
int
daos_eq_poll(daos_handle_t eqh, int wait_running,
	     int64_t timeout, unsigned int nevents, daos_event_t **events);

/**
 * Query how many outstanding events in EQ, if \a events is not NULL,
 * these events will be stored into it.
 *
 * Events returned by query are still owned by DAOS, it's not allowed to
 * finalize or free events returned by this function, but it's allowed
 * to call daos_event_abort() to abort launched operation.
 *
 * Also, the status of returned event could still be changing, for example,
 * the returned "launched" event can be turned to "completed" before accessing.
 * It is the user's responsibility to guarantee that returned events would be
 * freed by the polling process.
 *
 * \param[in] eqh	EQ handle
 * \param[in] mode	Query mode, bitmask of daos_eq_query_t
 * \param[in] nevents	Size of \a events array
 * \param[out] events	Pointer to returned events array
 * \return		>= 0	Returned number of events
 *			 < 0	negative value if error
 */
int
daos_eq_query(daos_handle_t eqh, daos_eq_query_t query,
	      unsigned int nevents, daos_event_t **events);

/**
 * Initialize a new event for \a eq
 *
 * \param[in] ev	Event to initialize
 * \param[in] eqh	Where the event to be queued on, it's ignored if
 *			\a parent is specified
 * \param[in] parent	"parent" event, it can be NULL if no parent event.
 *			If it's not NULL, caller will never see completion
 *			of this event, instead, will only see completion
 *			of \a parent when all children of \a parent are
 *			completed. The operation associated with the parent
 *			event may however be launched or completed before its
 *			children. The parent event completion is meant to be
 *			just an easy way to combine multiple events completion
 *			status into 1.
 *
 * \return		0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			-DER_NONEXIST	Event Queue does not exist
 */
int
daos_event_init(daos_event_t *ev, daos_handle_t eqh, daos_event_t *parent);

/**
 * Finalize an event. If event has been passed into any DAOS API, it can only
 * be finalized when it's been polled out from EQ, even if it is aborted by
 * calling daos_event_abort().
 * The event will be removed from child-list of the parent event if it is
 * initialized with parent. If \a ev itself is a parent event, then this
 * function will finalize all child events and \a ev.  Entries in \a ev
 * should not be considered valid after this call returns.
 *
 * \param[in] ev	Event to finalize
 *
 * \return		0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NONEXIST	Event Queue does not exist
 */
int
daos_event_fini(daos_event_t *ev);

/**
 * Get the next child event of \a ev, it will return the first child event
 * if \a child is NULL.
 *
 * \param[in] parent	Parent event
 * \param[in] child	Current child event.
 *
 * \return		The next child event after \a child, or NULL if it's
 *			the last one.
 */
daos_event_t *
daos_event_next(daos_event_t *parent, daos_event_t *child);

/**
 * Test completion of an event. If \a ev is a child, the operation will fail.
 * If the event was initialized in an event queue, and the test completes the
 * event, the event will be pulled out of the event queue.
 *
 * \param[in] ev	Event (operation) to test.
 * \param[in] timeout	How long is caller going to wait (micro-second)
 *			if \a timeout > 0,
 *			it can also be DAOS_EQ_NOWAIT, DAOS_EQ_WAIT
 * \param[out] flag	returned state of the event. true if the event is
 *			finished (completed or aborted), false if in-flight.
 *
 * \return		0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			-DER_NONEXIST	Event Queue does not exist
 *			negative rc of associated operation of the event.
 */
int
daos_event_test(struct daos_event *ev, int64_t timeout, bool *flag);

typedef int (*daos_event_comp_cb_t)(void *, daos_event_t *, int);

/**
 * Register completion callback on event.
 *
 * \param[in] ev	Event (operation).
 * \param[in] cb	Completion callback to register.
 * \param[in] arg	User args passed to completion callback.
 *
 * \return		0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			negative rc of associated operation of the event.
 */
int
daos_event_register_comp_cb(struct daos_event *ev, daos_event_comp_cb_t cb,
			    void *arg);

/**
 * Mark the parent event as a launched barrier, meaning no more child events can
 * be added before all other child events have completed and the parent event
 * polled out of the EQ or tested for completion, if it is not in an EQ. The
 * parent won't be polled out of the EQ or returns done with daos_event_test
 * until all children have completed.
 *
 * Note that if the parent event was launched as part of another daos operation,
 * this function should not be called anymore and the function the event was
 * launched with becomes the barrier operation. In that case, the operation
 * itself can be completed before the children do, but the event won't be marked
 * as ready before all the children complete.
 *
 * \param[in] ev	Parent event
 *
 * \return		0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			-DER_NONEXIST	Event Queue does not exist
 */
int
daos_event_parent_barrier(struct daos_event *ev);

/**
 * Try to abort operations associated with this event. The user is still required to wait or poll on
 * the event after this call.
 * This currently does not abort any internal DAOS operation and is effectively a no-op.
 *
 * \param[in] ev	Event (operation) to abort
 *
 * \return		0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 */
int
daos_event_abort(daos_event_t *ev);

#if defined(__cplusplus)
}
#endif

#endif /*  __DAOS_EVENT_H__ */
