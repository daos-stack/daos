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
 * This file is part of daos_m
 *
 * src/addons/daos_obj.c
 */

#define DD_SUBSYS	DD_FAC(client)

#include <daos/common.h>
#include <daos/event.h>
#include <daos/addons.h>
#include <daos_addons.h>

int
daos_obj_put(daos_handle_t oh, daos_epoch_t epoch, const char *key,
	     daos_size_t buf_size, const void *buf, daos_event_t *ev)
{
	daos_obj_put_t		args;
	struct daos_task	*task;

	args.oh		= oh;
	args.epoch	= epoch;
	args.key	= key;
	args.buf_size	= buf_size;
	args.buf	= buf;

	dc_task_create(DAOS_OPC_OBJ_PUT, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_obj_get(daos_handle_t oh, daos_epoch_t epoch, const char *key,
	     daos_size_t *buf_size, void *buf, daos_event_t *ev)
{
	daos_obj_get_t		args;
	struct daos_task	*task;

	args.oh		= oh;
	args.epoch	= epoch;
	args.key	= key;
	args.buf_size	= buf_size;
	args.buf	= buf;

	dc_task_create(DAOS_OPC_OBJ_GET, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_obj_remove(daos_handle_t oh, daos_epoch_t epoch, const char *key,
		daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_fetch_multi(daos_handle_t oh, daos_epoch_t epoch,
		     unsigned int num_dkeys, daos_dkey_io_t *io_array,
		     daos_event_t *ev)
{
	daos_obj_multi_io_t	args;
	struct daos_task	*task;

	if (num_dkeys == 0)
		return 0;

	args.oh         = oh;
	args.epoch      = epoch;
	args.num_dkeys	= num_dkeys;
	args.io_array	= io_array;

	dc_task_create(DAOS_OPC_OBJ_FETCH_MULTI, &args, sizeof(args), &task,
		       &ev);
	return daos_client_result_wait(ev);
}

int
daos_obj_update_multi(daos_handle_t oh, daos_epoch_t epoch,
		      unsigned int num_dkeys, daos_dkey_io_t *io_array,
		      daos_event_t *ev)
{
	daos_obj_multi_io_t	args;
	struct daos_task	*task;

	if (num_dkeys == 0)
		return 0;

	args.oh         = oh;
	args.epoch      = epoch;
	args.num_dkeys	= num_dkeys;
	args.io_array	= io_array;

	dc_task_create(DAOS_OPC_OBJ_UPDATE_MULTI, &args, sizeof(args), &task,
		       &ev);
	return daos_client_result_wait(ev);
}
