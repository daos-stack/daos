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
#include <daos/list.h>

typedef struct daos_eq {
	/* After event is completed, it will be moved to the eq_comp list */
	daos_list_t		eq_comp;
	int			eq_n_comp;

	/** Launched events will be added to the running list */
	daos_list_t		eq_running;
	int			eq_n_running;

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
	daos_list_t		evx_comp_list;
};

struct daos_event_private {
	daos_handle_t		evx_eqh;
	daos_list_t		evx_link;
	/** children list */
	daos_list_t		evx_child;
	unsigned int		evx_nchild;
	unsigned int		evx_nchild_running;
	unsigned int		evx_nchild_comp;

	unsigned int		evx_flags;
	daos_ev_status_t	evx_status;

	struct daos_event_private *evx_parent;

	crt_context_t		evx_ctx;
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

	/* CRT context associated with this eq */
	crt_context_t		 eqx_ctx;
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
 * Retrieve the private per-thread event
 *
 * \param ev [OUT]	per-thread event.
 */
int
daos_event_priv_get(daos_event_t **ev);

/**
 * Check whether \a ev is the private per-thread event
 *
 * \param ev [IN]	input event to compare with the private event.
 */
bool
daos_event_is_priv(daos_event_t *ev);

/**
 * Wait for completion of the private event
 */
int
daos_event_priv_wait();

int
daos_pool_query_async(daos_handle_t ph, daos_rank_list_t *tgts,
		      daos_pool_info_t *info, struct daos_task *task);

int
daos_pool_map_version_get(daos_handle_t ph, unsigned int *map_ver);

/**
 * Wait for completion if blocking mode. We always return 0 for asynchronous
 * mode because the application will get the result from event in this case,
 * besides certain failure might be reset anyway see daos_obj_comp_cb() .
 */
static inline int
daos_client_result_wait(daos_event_t *ev)
{
	D_ASSERT(ev != NULL);
	if (daos_event_is_priv(ev)) /* blocking mode */
		return daos_event_priv_wait(ev);

	return 0;
}

#endif /* __DAOS_CLI_INTERNAL_H__ */
