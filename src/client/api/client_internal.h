/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Client internal data structures and routines.
 *
 * Author: Liang Zhen  <liang.zhen@intel.com>
 */

#ifndef __DAOS_CLI_INTERNAL_H__
#define  __DAOS_CLI_INTERNAL_H__

#include <pthread.h>
#include <daos/common.h>
#include <daos_event.h>
#include <daos/event.h>
#include <gurt/list.h>

typedef struct daos_eq {
	/* After event is completed, it will be moved to the eq_comp list */
	d_list_t		eq_comp;
	int			eq_n_comp;

	/** Launched events will be added to the running list */
	d_list_t		eq_running;
	int			eq_n_running;

	struct {
		uint64_t	space[72];
	}			eq_private;

} daos_eq_t;

struct daos_event_comp_list {
	d_list_t	op_comp_list;
	daos_event_comp_cb_t op_comp_cb;
	void *op_comp_arg;
};

struct daos_event_callback {
	daos_event_comp_cb_t	evx_inline_cb;
	d_list_t		evx_comp_list;
};

struct daos_event_private {
	daos_handle_t		evx_eqh;
	d_list_t		evx_link;
	/** children list */
	d_list_t		evx_child;
	unsigned int		evx_nchild;
	unsigned int		evx_nchild_running;
	unsigned int		evx_nchild_comp;
	/** flag to indicate whether event is a barrier event */
	unsigned int		is_barrier:1;
	/** flag to indicate whether to convert DER to errno */
	unsigned int		is_errno:1;

	unsigned int		evx_flags;
	ATOMIC daos_ev_status_t	evx_status;

	struct daos_event_private *evx_parent;

	crt_context_t		evx_ctx;
	struct daos_event_callback evx_callback;

	tse_sched_t		*evx_sched;
	/** Lock for events that are not in an EQ, including the thread private event */
	pthread_mutex_t		evx_lock;
};

static inline struct daos_event_private *
daos_ev2evx(struct daos_event *ev)
{
	return (struct daos_event_private *)&ev->ev_private;
}

static inline struct daos_event *
daos_evx2ev(struct daos_event_private *evx)
{
	return container_of(evx, struct daos_event, ev_private);
}

struct daos_eq_private {
	/* link chain in the global hash list */
	struct d_hlink		eqx_hlink;
	pthread_mutex_t		eqx_lock;
	unsigned int		eqx_lock_init:1,
				eqx_finalizing:1;

	/* CRT context associated with this eq */
	crt_context_t		eqx_ctx;

	/* Scheduler associated with this EQ */
	tse_sched_t		eqx_sched;
};

static inline struct daos_eq_private *
daos_eq2eqx(struct daos_eq *eq)
{
	return (struct daos_eq_private *)&eq->eq_private;
}

static inline struct daos_eq *
daos_eqx2eq(struct daos_eq_private *eqx)
{
	return container_of(eqx, struct daos_eq, eq_private);
}

/**
 * Reset the private per-thread event.
 *
 * Can be used in failure case after daos_event_launch() called for the
 * ev_thpriv and before its completion. For example some synchronous test
 * cases in cmocka may fail in the middle of I/O operation, then in this
 * test case's teardown func can reset the ev_thpriv to avoid it affects
 * next test case due to dirty ev_thpriv status.
 */
int
daos_event_priv_reset(void);

/**
 * Retrieve the private per-thread event
 *
 * \param ev [OUT]	per-thread event.
 */
int
daos_event_priv_get(daos_event_t **ev);

#endif /* __DAOS_CLI_INTERNAL_H__ */
