/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC       DD_FAC(client)

#include <daos/task.h>
#include "client_internal.h"
#include "task_internal.h"

/** Disable backward compat code */
#undef daos_cont_create

/**
 * Kept for backward ABI compatibility when a UUID is provided instead of a
 * label
 */
int
daos_cont_create(daos_handle_t poh, const char *cont, daos_prop_t *cont_prop,
		 daos_event_t *ev)
{
	daos_cont_create_t	*args;
	tse_task_t		*task;
	const unsigned char	*uuid = (const unsigned char *) cont;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_CREATE);
	if (!daos_uuid_valid(uuid))
		return -DER_INVAL;

	if (cont_prop != NULL && !daos_prop_valid(cont_prop, false, true)) {
		D_ERROR("Invalid container properties.\n");
		return -DER_INVAL;
	}

	rc = dc_task_create(dc_cont_create, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->poh	= poh;
	uuid_copy((unsigned char *)args->uuid, uuid);
	args->prop	= cont_prop;
	args->label	= NULL;
	args->cuuid	= NULL;

	return dc_task_schedule(task, true);
}
