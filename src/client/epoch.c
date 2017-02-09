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

int
daos_epoch_query(daos_handle_t coh, daos_epoch_state_t *state,
		 daos_event_t *ev)
{
	struct daos_task	*task;
	int			rc;

	rc = daos_client_task_prep(NULL, NULL, 0, &task, &ev);
	if (rc != 0)
		return rc;

	dc_epoch_query(coh, state, task);

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
	struct daos_task	*task;
	int			rc;

	rc = daos_client_task_prep(NULL, NULL, 0, &task, &ev);
	if (rc != 0)
		return rc;

	dc_epoch_discard(coh, epoch, state, task);

	return daos_client_result_wait(ev);
}

int
daos_epoch_hold(daos_handle_t coh, daos_epoch_t *epoch,
		daos_epoch_state_t *state, daos_event_t *ev)
{
	struct daos_task	*task;
	int			rc;

	rc = daos_client_task_prep(NULL, NULL, 0, &task, &ev);
	if (rc != 0)
		return rc;

	dc_epoch_hold(coh, epoch, state, task);

	return daos_client_result_wait(ev);
}

int
daos_epoch_slip(daos_handle_t coh, daos_epoch_t epoch,
		daos_epoch_state_t *state, daos_event_t *ev)
{
	struct daos_task	*task;
	int			rc;

	rc = daos_client_task_prep(NULL, NULL, 0, &task, &ev);
	if (rc != 0)
		return rc;

	dc_epoch_slip(coh, epoch, state, task);

	return daos_client_result_wait(ev);
}

int
daos_epoch_commit(daos_handle_t coh, daos_epoch_t epoch,
		  daos_epoch_state_t *state, daos_event_t *ev)
{
	struct daos_task	*task;
	int			rc;

	rc = daos_client_task_prep(NULL, NULL, 0, &task, &ev);
	if (rc != 0)
		return rc;

	dc_epoch_commit(coh, epoch, state, task);

	return daos_client_result_wait(ev);
}

int
daos_epoch_wait(daos_handle_t coh, daos_epoch_t epoch,
		daos_epoch_state_t *state, daos_event_t *ev)
{
	return -DER_NOSYS;
}
