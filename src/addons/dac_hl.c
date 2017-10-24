/**
 * (C) Copyright 2017 Intel Corporation.
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
 * src/addons/dac_hl.c
 */

#define DDSUBSYS	DDFAC(client)

#include <daos/common.h>
#include <daos/tse.h>
#include <daos/addons.h>
#include <daos_api.h>
#include <daos_addons.h>
#include <daos_task.h>

struct io_params {
	daos_key_t		dkey;
	daos_iod_t		iod;
	daos_iov_t		iov;
	daos_sg_list_t		sgl;
};

static int
free_io_params_cb(tse_task_t *task, void *data)
{
	struct io_params *params = *((struct io_params **)data);

	D__FREE_PTR(params);
	return 0;
}

static int
set_size_cb(tse_task_t *task, void *data)
{
	daos_size_t *buf_size = *((daos_size_t **)data);
	daos_obj_fetch_t *args;

	D__ASSERT(buf_size != NULL);

	args = daos_task_get_args(DAOS_OPC_OBJ_FETCH, task);
	D__ASSERTF(args != NULL, "Task Argument OPC does not match fetch OPC\n");

	*buf_size = args->iods[0].iod_size;

	return 0;
}

int
dac_kv_put(tse_task_t *task)
{
	daos_kv_put_t		*args;
	daos_obj_update_t	update_args;
	tse_task_t		*update_task;
	struct io_params	*params;
	int			rc;

	args = daos_task_get_args(DAOS_OPC_KV_PUT, task);
	D__ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	D__ALLOC_PTR(params);
	if (params == NULL) {
		D__ERROR("Failed memory allocation\n");
		return -DER_NOMEM;
	}

	/** init dkey */
	daos_iov_set(&params->dkey, (void *)args->key, strlen(args->key));

	/** init iod. For now set akey = dkey */
	daos_iov_set(&params->iod.iod_name, (void *)args->key,
		     strlen(args->key));
	params->iod.iod_nr	= 1;
	params->iod.iod_recxs	= NULL;
	params->iod.iod_eprs	= NULL;
	params->iod.iod_csums	= NULL;
	params->iod.iod_size	= args->buf_size;
	params->iod.iod_type	= DAOS_IOD_SINGLE;

	/** init sgl */
	params->sgl.sg_nr.num = 1;
	params->sgl.sg_iovs = &params->iov;
	daos_iov_set(&params->sgl.sg_iovs[0], (void *)args->buf,
		     args->buf_size);

	update_args.oh		= args->oh;
	update_args.epoch	= args->epoch;
	update_args.dkey	= &params->dkey;
	update_args.nr		= 1;
	update_args.iods	= &params->iod;
	update_args.sgls	= &params->sgl;

	rc = daos_task_create(DAOS_OPC_OBJ_UPDATE, tse_task2sched(task),
			      &update_args, 0, NULL, &update_task);
	if (rc != 0)
		D__GOTO(err_task, rc);

	rc = tse_task_register_comp_cb(task, free_io_params_cb, &params,
				       sizeof(params));
	if (rc != 0)
		D__GOTO(err_task, rc);

	rc = tse_task_register_deps(task, 1, &update_task);
	if (rc != 0)
		D__GOTO(err_task, rc);

	rc = tse_task_schedule(update_task, false);
	if (rc != 0)
		D__GOTO(err_task, rc);

	tse_sched_progress(tse_task2sched(task));

	return 0;

err_task:
	if (params)
		D__FREE_PTR(params);
	if (update_task)
		D__FREE_PTR(update_task);
	tse_task_complete(task, rc);
	return rc;
}

int
dac_kv_get(tse_task_t *task)
{
	daos_kv_get_t		*args;
	daos_obj_fetch_t	fetch_args;
	tse_task_t		*fetch_task;
	struct io_params	*params;
	void			*buf;
	daos_size_t		*buf_size;
	int			rc;

	args = daos_task_get_args(DAOS_OPC_KV_GET, task);
	D__ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	buf = args->buf;
	buf_size = args->buf_size;

	if (buf_size == NULL) {
		D__ERROR("Buffer size pointer is NULL\n");
		return -DER_INVAL;
	}

	D__ALLOC_PTR(params);
	if (params == NULL) {
		D__ERROR("Failed memory allocation\n");
		return -DER_NOMEM;
	}

	/** init dkey */
	daos_iov_set(&params->dkey, (void *)args->key, strlen(args->key));

	/** init iod. For now set akey = dkey */
	daos_iov_set(&params->iod.iod_name, (void *)args->key,
		     strlen(args->key));
	params->iod.iod_nr	= 1;
	params->iod.iod_recxs	= NULL;
	params->iod.iod_eprs	= NULL;
	params->iod.iod_csums	= NULL;
	params->iod.iod_size	= *buf_size;
	params->iod.iod_type	= DAOS_IOD_SINGLE;

	/** init sgl */
	if (buf && *buf_size) {
		daos_iov_set(&params->iov, buf, *buf_size);
		params->sgl.sg_iovs = &params->iov;
		params->sgl.sg_nr.num = 1;
		fetch_args.sgls = &params->sgl;
	} else {
		fetch_args.sgls = NULL;
	}

	fetch_args.oh		= args->oh;
	fetch_args.epoch	= args->epoch;
	fetch_args.dkey		= &params->dkey;
	fetch_args.nr		= 1;
	fetch_args.iods		= &params->iod;

	rc = daos_task_create(DAOS_OPC_OBJ_FETCH, tse_task2sched(task),
			      &fetch_args, 0, NULL, &fetch_task);
	if (rc != 0)
		D__GOTO(err_task, rc);

	if (*buf_size == DAOS_REC_ANY) {
		rc = tse_task_register_comp_cb(fetch_task, set_size_cb,
					       &buf_size, sizeof(buf_size));
		if (rc != 0)
			D__GOTO(err_task, rc);
	}

	rc = tse_task_register_comp_cb(task, free_io_params_cb, &params,
				       sizeof(params));
	if (rc != 0)
		D__GOTO(err_task, rc);

	rc = tse_task_register_deps(task, 1, &fetch_task);
	if (rc != 0)
		D__GOTO(err_task, rc);

	rc = tse_task_schedule(fetch_task, false);
	if (rc != 0)
		D__GOTO(err_task, rc);

	tse_sched_progress(tse_task2sched(task));

	return 0;

err_task:
	if (params)
		D__FREE_PTR(params);
	if (fetch_task)
		D__FREE_PTR(fetch_task);
	tse_task_complete(task, rc);
	return rc;
}

int
dac_kv_remove(tse_task_t *task)
{
	return -DER_NOSYS;
}

static int
dac_multi_io(daos_handle_t oh, daos_epoch_t epoch, unsigned int num_dkeys,
	     daos_dkey_io_t *io_array, daos_opc_t opc, tse_task_t *task)
{
	daos_opc_t	d_opc;
	int		i;
	int		rc = 0;
	tse_task_t	**io_tasks;

	d_opc = (opc == DAOS_OPC_OBJ_FETCH_MULTI ? DAOS_OPC_OBJ_FETCH :
		 DAOS_OPC_OBJ_UPDATE);

	D__ALLOC(io_tasks, sizeof(*io_tasks) * num_dkeys);
	if (io_tasks == NULL)
		D__GOTO(err_task, rc = -DER_NOMEM);

	for (i = 0; i < num_dkeys; i++) {
		daos_obj_fetch_t args;

		args.oh		= oh;
		args.epoch	= epoch;
		args.dkey	= io_array[i].ioa_dkey;
		args.nr		= io_array[i].ioa_nr;
		args.iods	= io_array[i].ioa_iods;
		args.sgls	= io_array[i].ioa_sgls;
		args.maps	= io_array[i].ioa_maps;

		rc = daos_task_create(d_opc, tse_task2sched(task),
				      &args, 0, NULL, &io_tasks[i]);
		if (rc != 0)
			D__GOTO(err_task, rc);
	}

	rc = tse_task_register_deps(task, num_dkeys, io_tasks);
	if (rc != 0)
		D__GOTO(err_task, rc);

	for (i = 0; i < num_dkeys; i++)
		tse_task_schedule(io_tasks[i], false);

	tse_sched_progress(tse_task2sched(task));

out_task:
	if (io_tasks != NULL)
		D__FREE(io_tasks, sizeof(*io_tasks) * num_dkeys);
	return rc;

err_task:
	for (i = 0; i < num_dkeys; i++) {
		if (io_tasks[i])
			D__FREE_PTR(io_tasks[i]);
	}
	tse_task_complete(task, rc);

	goto out_task;
}

int
dac_obj_fetch_multi(tse_task_t *task)
{
	daos_obj_multi_io_t *args;

	args = daos_task_get_args(DAOS_OPC_OBJ_FETCH_MULTI, task);
	D__ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return dac_multi_io(args->oh, args->epoch, args->num_dkeys,
			    args->io_array, DAOS_OPC_OBJ_FETCH_MULTI, task);
}

int
dac_obj_update_multi(tse_task_t *task)
{
	daos_obj_multi_io_t *args;

	args = daos_task_get_args(DAOS_OPC_OBJ_UPDATE_MULTI, task);
	D__ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return dac_multi_io(args->oh, args->epoch, args->num_dkeys,
			    args->io_array, DAOS_OPC_OBJ_UPDATE_MULTI, task);
}
