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
#define D_LOGFAC	DD_FAC(client)

#include <daos/container.h>
#include "client_internal.h"
#include "task_internal.h"

int
daos_epoch_query(daos_handle_t coh, daos_epoch_state_t *state,
		 daos_event_t *ev)
{
	daos_epoch_query_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, EPOCH_QUERY);
	rc = dc_task_create(dc_epoch_query, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->state	= state;

	return dc_task_schedule(task, true);
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
	daos_epoch_discard_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, EPOCH_DISCARD);
	rc = dc_task_create(dc_epoch_discard, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->epoch	= epoch;
	args->state	= state;

	return dc_task_schedule(task, true);
}

int
daos_epoch_hold(daos_handle_t coh, daos_epoch_t *epoch,
		daos_epoch_state_t *state, daos_event_t *ev)
{
	daos_epoch_hold_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, EPOCH_HOLD);
	rc = dc_task_create(dc_epoch_hold, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->epoch	= epoch;
	args->state	= state;

	return dc_task_schedule(task, true);
}

int
daos_epoch_slip(daos_handle_t coh, daos_epoch_t epoch,
		daos_epoch_state_t *state, daos_event_t *ev)
{
	daos_epoch_slip_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, EPOCH_SLIP);
	rc = dc_task_create(dc_epoch_slip, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->epoch	= epoch;
	args->state	= state;

	return dc_task_schedule(task, true);
}

int
daos_epoch_commit(daos_handle_t coh, daos_epoch_t epoch,
		  daos_epoch_state_t *state, daos_event_t *ev)
{
	daos_epoch_commit_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, EPOCH_COMMIT);
	rc = dc_task_create(dc_epoch_commit, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->epoch	= epoch;
	args->state	= state;

	return dc_task_schedule(task, true);
}

int
daos_epoch_wait(daos_handle_t coh, daos_epoch_t epoch,
		daos_epoch_state_t *state, daos_event_t *ev)
{
	return -DER_NOSYS;
}
