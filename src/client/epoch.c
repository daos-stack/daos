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

#define DD_SUBSYS	DD_FAC(client)

#include <daos/container.h>
#include "client_internal.h"
#include "task_internal.h"

int
daos_epoch_query(daos_handle_t coh, daos_epoch_state_t *state,
		 daos_event_t *ev)
{
	daos_epoch_query_t	args;
	struct daos_task	*task;

	DAOS_API_ARG_ASSERT(args, EPOCH_QUERY);

	args.coh = coh;
	args.state = state;

	dc_task_create(DAOS_OPC_EPOCH_QUERY, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_epoch_flush(daos_handle_t coh, daos_epoch_t epoch,
		 daos_epoch_state_t *state, daos_event_t *ev)
{
	/** all updates are synchronous for now, no need to do anything */
	return daos_epoch_query(coh, state, ev);
}

int
daos_epoch_discard(daos_handle_t coh, daos_epoch_t epoch,
		   daos_epoch_state_t *state, daos_event_t *ev)
{
	daos_epoch_discard_t	args;
	struct daos_task	*task;

	DAOS_API_ARG_ASSERT(args, EPOCH_DISCARD);

	args.coh = coh;
	args.epoch = epoch;
	args.state = state;

	dc_task_create(DAOS_OPC_EPOCH_DISCARD, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_epoch_hold(daos_handle_t coh, daos_epoch_t *epoch,
		daos_epoch_state_t *state, daos_event_t *ev)
{
	daos_epoch_hold_t	args;
	struct daos_task	*task;

	DAOS_API_ARG_ASSERT(args, EPOCH_HOLD);

	args.coh = coh;
	args.epoch = epoch;
	args.state = state;

	dc_task_create(DAOS_OPC_EPOCH_HOLD, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_epoch_slip(daos_handle_t coh, daos_epoch_t epoch,
		daos_epoch_state_t *state, daos_event_t *ev)
{
	daos_epoch_slip_t	args;
	struct daos_task	*task;

	DAOS_API_ARG_ASSERT(args, EPOCH_SLIP);

	args.coh = coh;
	args.epoch = epoch;
	args.state = state;

	dc_task_create(DAOS_OPC_EPOCH_SLIP, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_epoch_commit(daos_handle_t coh, daos_epoch_t epoch,
		  daos_epoch_state_t *state, daos_event_t *ev)
{
	daos_epoch_commit_t	args;
	struct daos_task	*task;

	DAOS_API_ARG_ASSERT(args, EPOCH_COMMIT);

	args.coh = coh;
	args.epoch = epoch;
	args.state = state;

	dc_task_create(DAOS_OPC_EPOCH_COMMIT, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_epoch_wait(daos_handle_t coh, daos_epoch_t epoch,
		daos_epoch_state_t *state, daos_event_t *ev)
{
	return -DER_NOSYS;
}
