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
#include <daos/list.h>
#include <daos/hash.h>
#include <daos/transport.h>

/** Common scratchpad for the operation in flight */
struct daos_op_sp {
	dtp_rpc_t	*sp_rpc;
	daos_handle_t	 sp_hdl;
	daos_handle_t	*sp_hdlp;
	void		*sp_arg;
};

typedef int (*daos_event_abort_cb_t)(struct daos_op_sp *, daos_event_t *);
typedef int (*daos_event_comp_cb_t)(struct daos_op_sp *, daos_event_t *, int);

/**
 * Finish event queue library
 */
int
daos_eq_lib_fini(void);

/**
 * Initialize event queue library
 */
int
daos_eq_lib_init();

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
 * Mark the event launched, i.e. move this event to launch list.
 *
 * \param ev		[IN]	event to launch.
 * \param abort_cb	[IN]	abort callback
 * \param comp_cb	[IN]	completion callback
 */
int
daos_event_launch(struct daos_event *ev, daos_event_abort_cb_t abort_cb,
		  daos_event_comp_cb_t comp_cb);

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

/**
 * Return transport context associated with a particular event
 *
 * \param ev [IN]	event to retrieve context.
 */
dtp_context_t
daos_ev2ctx(struct daos_event *ev);

/**
 * Return scratchpad associated with a particular event
 *
 * \param ev [IN]	event to retrieve scratchpad.
 */
struct daos_op_sp *
daos_ev2sp(struct daos_event *ev);
#endif /*  __DAOS_EV_INTERNAL_H__ */
