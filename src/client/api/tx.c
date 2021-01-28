/**
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(client)

#include <daos/object.h>
#include <daos/task.h>
#include "client_internal.h"
#include "task_internal.h"

int
daos_tx_open(daos_handle_t coh, daos_handle_t *th, uint64_t flags,
	     daos_event_t *ev)
{
	daos_tx_open_t	*args;
	tse_task_t	*task;
	int		 rc;

	DAOS_API_ARG_ASSERT(*args, TX_OPEN);
	rc = dc_task_create(dc_tx_open, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->th	= th;
	args->flags	= flags;

	return dc_task_schedule(task, true);
}

int
daos_tx_close(daos_handle_t th, daos_event_t *ev)
{
	daos_tx_close_t		*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, TX_CLOSE);
	rc = dc_task_create(dc_tx_close, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->th	= th;

	return dc_task_schedule(task, true);
}

int
daos_tx_commit(daos_handle_t th, daos_event_t *ev)
{
	daos_tx_commit_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, TX_COMMIT);
	rc = dc_task_create(dc_tx_commit, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->th	= th;
	args->flags	= 0;

	return dc_task_schedule(task, true);
}

int
daos_tx_abort(daos_handle_t th, daos_event_t *ev)
{
	daos_tx_abort_t		*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, TX_ABORT);
	rc = dc_task_create(dc_tx_abort, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->th	= th;

	return dc_task_schedule(task, true);
}

int
daos_tx_open_snap(daos_handle_t coh, daos_epoch_t epoch, daos_handle_t *th,
		  daos_event_t *ev)
{
	daos_tx_open_snap_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, TX_OPEN_SNAP);
	rc = dc_task_create(dc_tx_open_snap, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->epoch	= epoch;
	args->th	= th;

	return dc_task_schedule(task, true);
}

int
daos_tx_restart(daos_handle_t th, daos_event_t *ev)
{
	daos_tx_restart_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, TX_RESTART);
	rc = dc_task_create(dc_tx_restart, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->th	= th;

	return dc_task_schedule(task, true);
}

int
daos_tx_hdl2epoch(daos_handle_t th, daos_epoch_t *epoch)
{
	return dc_tx_hdl2epoch(th, epoch);
}

int
daos_tx_local2global(daos_handle_t th, d_iov_t *glob)
{
	return -DER_NOSYS;
}

int
daos_tx_global2local(daos_handle_t coh, d_iov_t glob, daos_handle_t *th)
{
	return -DER_NOSYS;
}
