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
/**
 * common/event_internal.h
 *
 * Author: Liang Zhen  <liang.zhen@intel.com>
 */

#ifndef EVENT_INTERNAL_H
#define EVENT_INTERNAL_H

#include <pthread.h>
#include <daos/common.h>
#include <daos_event.h>
#include <daos/event.h>
#include <daos/list.h>
#include <daos/scheduler.h>

typedef struct daos_eq {
	/* After event is completed, it will be moved to the eq_comp list */
	daos_list_t		eq_comp;
	int			eq_n_comp;

	/** In flight events will be put to the disp list */
	daos_list_t		eq_disp;
	int			eq_n_disp;

	struct {
		uint64_t	space[20];
	}			eq_private;

} daos_eq_t;

struct daos_event_comp_list {
	daos_list_t	op_comp_list;
	daos_event_comp_cb_t op_comp_cb;
	void *op_comp_arg;
};

struct daos_event_callback {
	daos_event_comp_cb_t	evx_inline_cb;
	struct daos_op_sp	evx_inline_cb_sp;
	daos_list_t		evx_comp_list;
};

struct daos_event_private {
	daos_handle_t		evx_eqh;
	daos_list_t		evx_link;
	/** children list */
	daos_list_t		evx_child;
	unsigned int		evx_nchild;
	unsigned int		evx_nchild_if;
	unsigned int		evx_nchild_comp;

	unsigned int		evx_flags;
	daos_ev_status_t	evx_status;

	struct daos_event_private *evx_parent;

	dtp_context_t		evx_ctx;
	struct daos_event_callback evx_callback;
	struct daos_sched	evx_sched;
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
	struct daos_hlink	eqx_hlink;
	pthread_mutex_t		eqx_lock;
	unsigned int		eqx_lock_init:1,
				eqx_finalizing:1;

	/* All of its events are linked here */
	struct daos_hhash	*eqx_events_hash;

	/* DTP context associated with this eq */
	dtp_context_t		 eqx_ctx;
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
#endif /* __EVENT_INTERNAL_H__ */
