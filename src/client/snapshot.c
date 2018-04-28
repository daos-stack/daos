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
#include <daos/task.h>
#include "client_internal.h"
#include "task_internal.h"

int
daos_snap_list(daos_handle_t coh, daos_epoch_t *buf, int *n, daos_event_t *ev)
{
	daos_snap_list_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, SNAP_LIST);

	rc = dc_task_create(dc_snap_list, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->n		= n;
	args->buf	= buf;

	return dc_task_schedule(task, true);
}

int
daos_snap_create(daos_handle_t coh, daos_epoch_t epoch, daos_event_t *ev)
{
	daos_snap_create_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, SNAP_CREATE);

	rc = dc_task_create(dc_snap_create, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->epoch	= epoch;

	return dc_task_schedule(task, true);
}

int
daos_snap_destroy(daos_handle_t coh, daos_epoch_t epoch, daos_event_t *ev)
{
	daos_snap_destroy_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, SNAP_DESTROY);

	rc = dc_task_create(dc_snap_destroy, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->epoch	= epoch;

	return dc_task_schedule(task, true);
}
