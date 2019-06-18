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
#define D_LOGFAC	DD_FAC(addons)

#include <daos/common.h>
#include <daos/tse.h>
#include <daos/addons.h>
#include <daos_api.h>
#include <daos_addons.h>
#include <daos_task.h>

struct io_params {
	daos_key_t		dkey;
	daos_iod_t		iod;
	d_iov_t		iov;
	d_sg_list_t		sgl;
};

static int
free_io_params_cb(tse_task_t *task, void *data)
{
	struct io_params *params = *((struct io_params **)data);

	D_FREE(params);
	return 0;
}

static int
set_size_cb(tse_task_t *task, void *data)
{
	daos_size_t *buf_size = *((daos_size_t **)data);
	daos_obj_fetch_t *args = daos_task_get_args(task);

	D_ASSERT(buf_size != NULL);
	*buf_size = args->iods[0].iod_size;
	return 0;
}

int
dac_kv_put(tse_task_t *task)
{
	daos_kv_put_t		*args = daos_task_get_args(task);
	daos_obj_update_t	*update_args;
	tse_task_t		*update_task;
	struct io_params	*params;
	int			rc;

	D_ALLOC_PTR(params);
	if (params == NULL) {
		D_ERROR("Failed memory allocation\n");
		return -DER_NOMEM;
	}

	/** init dkey */
	d_iov_set(&params->dkey, (void *)args->key, strlen(args->key));

	/** init iod. For now set akey = dkey */
	d_iov_set(&params->iod.iod_name, (void *)args->key,
		     strlen(args->key));
	params->iod.iod_nr	= 1;
	params->iod.iod_recxs	= NULL;
	params->iod.iod_eprs	= NULL;
	params->iod.iod_csums	= NULL;
	params->iod.iod_size	= args->buf_size;
	params->iod.iod_type	= DAOS_IOD_SINGLE;

	/** init sgl */
	params->sgl.sg_nr = 1;
	params->sgl.sg_iovs = &params->iov;
	d_iov_set(&params->sgl.sg_iovs[0], (void *)args->buf,
		     args->buf_size);

	rc = daos_task_create(DAOS_OPC_OBJ_UPDATE, tse_task2sched(task),
			      0, NULL, &update_task);
	if (rc != 0)
		D_GOTO(err_task, rc);

	update_args = daos_task_get_args(update_task);
	update_args->oh		= args->oh;
	update_args->th		= args->th;
	update_args->dkey	= &params->dkey;
	update_args->nr		= 1;
	update_args->iods	= &params->iod;
	update_args->sgls	= &params->sgl;

	rc = tse_task_register_comp_cb(task, free_io_params_cb, &params,
				       sizeof(params));
	if (rc != 0)
		D_GOTO(err_task, rc);

	rc = tse_task_register_deps(task, 1, &update_task);
	if (rc != 0)
		D_GOTO(err_task, rc);

	rc = tse_task_schedule(update_task, false);
	if (rc != 0)
		D_GOTO(err_task, rc);

	tse_sched_progress(tse_task2sched(task));

	return 0;

err_task:
	if (params)
		D_FREE(params);
	if (update_task)
		tse_task_complete(update_task, rc);
	tse_task_complete(task, rc);
	return rc;
}

int
dac_kv_get(tse_task_t *task)
{
	daos_kv_get_t		*args = daos_task_get_args(task);
	daos_obj_fetch_t	*fetch_args;
	tse_task_t		*fetch_task;
	struct io_params	*params;
	void			*buf;
	daos_size_t		*buf_size;
	int			rc;

	buf = args->buf;
	buf_size = args->buf_size;
	if (buf_size == NULL) {
		D_ERROR("Buffer size pointer is NULL\n");
		return -DER_INVAL;
	}

	D_ALLOC_PTR(params);
	if (params == NULL) {
		D_ERROR("Failed memory allocation\n");
		return -DER_NOMEM;
	}

	/** init dkey */
	d_iov_set(&params->dkey, (void *)args->key, strlen(args->key));

	/** init iod. For now set akey = dkey */
	d_iov_set(&params->iod.iod_name, (void *)args->key,
		     strlen(args->key));
	params->iod.iod_nr	= 1;
	params->iod.iod_recxs	= NULL;
	params->iod.iod_eprs	= NULL;
	params->iod.iod_csums	= NULL;
	params->iod.iod_size	= *buf_size;
	params->iod.iod_type	= DAOS_IOD_SINGLE;

	/** init sgl */
	if (buf && *buf_size) {
		d_iov_set(&params->iov, buf, *buf_size);
		params->sgl.sg_iovs = &params->iov;
		params->sgl.sg_nr = 1;
	}

	rc = daos_task_create(DAOS_OPC_OBJ_FETCH, tse_task2sched(task),
			      0, NULL, &fetch_task);
	if (rc != 0)
		D_GOTO(err_task, rc);

	fetch_args = daos_task_get_args(fetch_task);
	fetch_args->oh		= args->oh;
	fetch_args->th		= args->th;
	fetch_args->dkey	= &params->dkey;
	fetch_args->nr		= 1;
	fetch_args->iods	= &params->iod;
	if (buf && *buf_size)
		fetch_args->sgls = &params->sgl;

	if (*buf_size == DAOS_REC_ANY) {
		rc = tse_task_register_comp_cb(fetch_task, set_size_cb,
					       &buf_size, sizeof(buf_size));
		if (rc != 0)
			D_GOTO(err_task, rc);
	}

	rc = tse_task_register_comp_cb(task, free_io_params_cb, &params,
				       sizeof(params));
	if (rc != 0)
		D_GOTO(err_task, rc);

	rc = tse_task_register_deps(task, 1, &fetch_task);
	if (rc != 0)
		D_GOTO(err_task, rc);

	rc = tse_task_schedule(fetch_task, false);
	if (rc != 0)
		D_GOTO(err_task, rc);

	tse_sched_progress(tse_task2sched(task));

	return 0;

err_task:
	if (params)
		D_FREE(params);
	if (fetch_task)
		tse_task_complete(fetch_task, rc);
	tse_task_complete(task, rc);
	return rc;
}

int
dac_kv_remove(tse_task_t *task)
{
	daos_kv_remove_t	*args = daos_task_get_args(task);
	daos_obj_punch_t	*punch_args;
	tse_task_t		*punch_task = NULL;
	struct io_params	*params;
	int			rc;

	D_ALLOC_PTR(params);
	if (params == NULL) {
		D_ERROR("Failed memory allocation\n");
		return -DER_NOMEM;
	}

	/** init dkey */
	d_iov_set(&params->dkey, (void *)args->key, strlen(args->key));

	rc = daos_task_create(DAOS_OPC_OBJ_PUNCH_DKEYS, tse_task2sched(task),
			      0, NULL, &punch_task);
	if (rc != 0)
		D_GOTO(err_task, rc);

	punch_args = daos_task_get_args(punch_task);
	punch_args->oh		= args->oh;
	punch_args->th		= args->th;
	punch_args->dkey	= &params->dkey;
	punch_args->akeys	= NULL;
	punch_args->akey_nr	= 0;

	rc = tse_task_register_comp_cb(task, free_io_params_cb, &params,
				       sizeof(params));
	if (rc != 0)
		D_GOTO(err_task, rc);

	rc = tse_task_register_deps(task, 1, &punch_task);
	if (rc != 0)
		D_GOTO(err_task, rc);

	rc = tse_task_schedule(punch_task, false);
	if (rc != 0)
		D_GOTO(err_task, rc);

	tse_sched_progress(tse_task2sched(task));

	return 0;

err_task:
	if (params)
		D_FREE(params);
	if (punch_task)
		tse_task_complete(punch_task, rc);
	tse_task_complete(task, rc);
	return rc;
}

int
dac_kv_list(tse_task_t *task)
{
	daos_kv_list_t		*args = daos_task_get_args(task);
	daos_obj_list_dkey_t	*list_args;
	tse_task_t		*list_task = NULL;
	int			rc;

	rc = daos_task_create(DAOS_OPC_OBJ_LIST_DKEY, tse_task2sched(task),
			      0, NULL, &list_task);
	if (rc != 0)
		D_GOTO(err_task, rc);

	list_args = daos_task_get_args(list_task);
	list_args->oh		= args->oh;
	list_args->th		= args->th;
	list_args->nr		= args->nr;
	list_args->sgl		= args->sgl;
	list_args->kds		= args->kds;
	list_args->dkey_anchor	= args->anchor;

	rc = tse_task_register_deps(task, 1, &list_task);
	if (rc != 0)
		D_GOTO(err_task, rc);

	rc = tse_task_schedule(list_task, false);
	if (rc != 0)
		D_GOTO(err_task, rc);

	tse_sched_progress(tse_task2sched(task));

	return 0;

err_task:
	if (list_task)
		tse_task_complete(list_task, rc);
	tse_task_complete(task, rc);
	return rc;
}

static int
dac_multi_io(daos_handle_t oh, daos_handle_t th, unsigned int num_dkeys,
	     daos_dkey_io_t *io_array, daos_opc_t opc, tse_task_t *task)
{
	daos_opc_t	d_opc;
	d_list_t	head;
	int		i;
	int		rc = 0;

	d_opc = (opc == DAOS_OPC_OBJ_FETCH_MULTI ? DAOS_OPC_OBJ_FETCH :
		DAOS_OPC_OBJ_UPDATE);
	D_INIT_LIST_HEAD(&head);

	for (i = 0; i < num_dkeys; i++) {
		tse_task_t	 *io_task;
		daos_obj_fetch_t *args;

		rc = daos_task_create(d_opc, tse_task2sched(task), 0, NULL,
				      &io_task);
		if (rc != 0)
			D_GOTO(err_task, rc);

		args = daos_task_get_args(io_task);
		args->oh	= oh;
		args->th	= th;
		args->dkey	= io_array[i].ioa_dkey;
		args->nr	= io_array[i].ioa_nr;
		args->iods	= io_array[i].ioa_iods;
		args->sgls	= io_array[i].ioa_sgls;
		args->maps	= io_array[i].ioa_maps;

		tse_task_list_add(io_task, &head);
	}

	rc = tse_task_depend_list(task, &head);
	if (rc != 0)
		D_GOTO(err_task, rc);

	tse_task_list_sched(&head, false);
	tse_sched_progress(tse_task2sched(task));
	return 0;

err_task:
	while (!d_list_empty(&head)) {
		tse_task_t *tmp = tse_task_list_first(&head);

		tse_task_list_del(tmp);
		tse_task_decref(tmp);
	}
	tse_task_complete(task, rc);
	return rc;
}

int
dac_obj_fetch_multi(tse_task_t *task)
{
	daos_obj_multi_io_t *args = daos_task_get_args(task);

	return dac_multi_io(args->oh, args->th, args->num_dkeys,
			    args->io_array, DAOS_OPC_OBJ_FETCH_MULTI, task);
}

int
dac_obj_update_multi(tse_task_t *task)
{
	daos_obj_multi_io_t *args = daos_task_get_args(task);

	return dac_multi_io(args->oh, args->th, args->num_dkeys,
			    args->io_array, DAOS_OPC_OBJ_UPDATE_MULTI, task);
}
