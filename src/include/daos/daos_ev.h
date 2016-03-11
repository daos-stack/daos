/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2015 Intel Corporation.
 */
/**
 * DAOS Event Queue
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 *
 * Version 0.1
 */
#ifndef __DAOS_EV_H__
#define __DAOS_EV_H__

#if defined (__cplusplus)
extern "C" {
#endif

#include <daos/daos_types.h>
#include <daos/daos_errno.h>
#include <daos/daos_list.h>
#include <daos/daos_hash.h>

typedef enum {
	DAOS_EV_NONE,
	/**
	 * a parent event, it has child events which can be accessed by
	 * calling daos_event_next()
	 */
	DAOS_EV_COMPOUND,
	DAOS_EV_CO_CREATE,		/**< container created */
	DAOS_EV_CO_OPEN,		/**< container opened */
	DAOS_EV_CO_CLOSE,		/**< container closed */
	DAOS_EV_CO_DESTROY,		/**< container destroyed */
	/** TODO: add event types */
} daos_ev_type_t;

typedef struct daos_event {
	daos_ev_type_t		ev_type;
	daos_errno_t		ev_error;
	struct {
		uint64_t	space[20];
	}			ev_private;
} daos_event_t;

/** wait for completion event forever */
#define DAOS_EQ_WAIT            -1
/** always return immediately */
#define DAOS_EQ_NOWAIT          0

typedef enum {
	/** query outstanding completed event */
	DAOS_EQR_COMPLETED	= (1),
	/** query # inflight event */
	DAOS_EQR_DISPATCH	= (1 << 1),
	/** query # inflight + completed events in EQ */
	DAOS_EQR_ALL		= (DAOS_EQR_COMPLETED | DAOS_EQR_DISPATCH),
} daos_eq_query_t;

typedef enum {
	DAOS_EVS_INIT,
	DAOS_EVS_DISPATCH,
	DAOS_EVS_COMPLETED,
	DAOS_EVS_ABORT,
} daos_ev_status_t;

struct daos_event_ops {
	int (*op_abort)(void *param, int unlinked);
	int (*op_complete)(void *param, int error, int unlinked);
};

struct daos_eq {
	/* After event is completed, it will be moved to the eq_comp list */
	daos_list_t		eq_comp;
	int			eq_n_comp;

	/** In flight events will be put to the disp list */
	daos_list_t		eq_disp;
	int			eq_n_disp;

	struct {
		uint64_t	space[20];
	}			eq_private;

};

/**
 * Initialize event queue library
 */
void
daos_eq_lib_fini(void);

/**
 * Finish event queue library
 */
int
daos_eq_lib_init(void);

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
	     int64_t timeout, int nevents, daos_event_t **events);

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

#if defined (__cplusplus)
}
#endif
#endif /*  __DAOS_EV_H__ */
