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
 * rebuild: server daos api
 *
 * This file includes functions to call client daos API on the server side.
 */
#define DDSUBSYS	DDFAC(rebuild)

#include <daos_types.h>
#include <daos_errno.h>
#include <daos_event.h>
#include <daos_task.h>

#include <daos/pool.h>
#include <daos/container.h>
#include <daos/object.h>
#include <daos/event.h>

#include <daos_srv/daos_server.h>
#include "rebuild_internal.h"

static int
rebuild_need_retry_cb(tse_task_t *task, void *arg)
{
	daos_handle_t *oh = arg;
	int rc;

	if (task->dt_result != -DER_NO_HDL || oh == NULL)
		return 0;

	/* If the remote rebuild pool/container is not ready,
	 * or the remote target has been evicted from pool.
	 * Note: the pool map will redistributed by IV
	 * automatically, so let's just keep refreshing the
	 * layout.
	 */
	D_DEBUG(DB_TRACE, "retry task %p\n", task);

	/* let's check if the pool_map has been changed */
	dc_obj_layout_refresh(*oh);

	task->dt_result = 0;
	rc = dc_task_resched(task);
	if (rc != 0) {
		D_ERROR("Failed to re-init task (%p)\n", task);
		return rc;
	}

	/* Register the callback, since it will be removed
	 * after callback.
	 */
	rc = dc_task_reg_comp_cb(task, rebuild_need_retry_cb, arg, sizeof(arg));
	return rc;
}

int
ds_obj_open(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
	    unsigned int mode, daos_handle_t *oh)
{
	tse_task_t	*task;
	daos_obj_open_t	*arg;
	int		 rc;

	rc = dc_task_create(dc_obj_open, dss_tse_scheduler(), NULL, &task);
	if (rc)
		return rc;

	arg = dc_task_get_args(task);
	arg->coh	= coh;
	arg->oid	= oid;
	arg->epoch	= epoch;
	arg->mode	= mode;
	arg->oh		= oh;

	return dss_task_run(task, DSS_POOL_REBUILD, rebuild_need_retry_cb,
			    NULL);
}

int
ds_obj_close(daos_handle_t oh)
{
	tse_task_t	 *task;
	daos_obj_close_t *arg;
	int		  rc;

	rc = dc_task_create(dc_obj_close, dss_tse_scheduler(), NULL, &task);
	if (rc)
		return rc;

	arg = dc_task_get_args(task);
	arg->oh = oh;

	return dss_task_run(task, DSS_POOL_REBUILD, rebuild_need_retry_cb, &oh);
}

int
ds_obj_single_shard_list_dkey(daos_handle_t oh, daos_epoch_t epoch,
			      uint32_t *nr, daos_key_desc_t *kds,
			      daos_sg_list_t *sgl, daos_hash_out_t *anchor)
{
	tse_task_t	     *task;
	daos_obj_list_dkey_t *arg;
	int		      rc;

	rc = dc_task_create(dc_obj_single_shard_list_dkey,
			    dss_tse_scheduler(), NULL, &task);
	if (rc)
		return rc;

	arg = dc_task_get_args(task);
	arg->oh		= oh;
	arg->epoch	= epoch;
	arg->nr		= nr;
	arg->kds	= kds;
	arg->sgl	= sgl;
	arg->anchor	= anchor;

	return dss_task_run(task, DSS_POOL_REBUILD, rebuild_need_retry_cb, &oh);
}

int
ds_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		 uint32_t *nr, daos_key_desc_t *kds, daos_sg_list_t *sgl,
		 daos_hash_out_t *anchor)
{
	tse_task_t	     *task;
	daos_obj_list_akey_t *arg;
	int		      rc;

	rc = dc_task_create(dc_obj_list_akey, dss_tse_scheduler(),
			     NULL, &task);
	if (rc)
		return rc;

	arg = dc_task_get_args(task);
	arg->oh		= oh;
	arg->epoch	= epoch;
	arg->dkey	= dkey;
	arg->nr		= nr;
	arg->kds	= kds;
	arg->sgl	= sgl;
	arg->anchor	= anchor;

	return dss_task_run(task, DSS_POOL_REBUILD, rebuild_need_retry_cb, &oh);
}

int
ds_obj_fetch(daos_handle_t oh, daos_epoch_t epoch,
	     daos_key_t *dkey, unsigned int nr,
	     daos_iod_t *iods, daos_sg_list_t *sgls,
	     daos_iom_t *maps)
{
	tse_task_t	 *task;
	daos_obj_fetch_t *arg;
	int		  rc;

	rc = dc_task_create(dc_obj_fetch, dss_tse_scheduler(), NULL, &task);
	if (rc)
		return rc;

	arg = dc_task_get_args(task);
	arg->oh		= oh;
	arg->epoch	= epoch;
	arg->dkey	= dkey;
	arg->nr		= nr;
	arg->iods	= iods;
	arg->sgls	= sgls;
	arg->maps	= maps;

	return dss_task_run(task, DSS_POOL_REBUILD, rebuild_need_retry_cb, &oh);
}

int
ds_obj_list_rec(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		daos_key_t *akey, daos_iod_type_t type, daos_size_t *size,
		uint32_t *nr, daos_recx_t *recxs, daos_epoch_range_t *eprs,
		uuid_t *cookies, uint32_t *versions, daos_hash_out_t *anchor,
		bool incr)
{
	tse_task_t		*task;
	daos_obj_list_recx_t	*arg;
	int			 rc;

	rc = dc_task_create(dc_obj_list_rec, dss_tse_scheduler(), NULL, &task);
	if (rc)
		return rc;

	arg = dc_task_get_args(task);
	arg->oh		= oh;
	arg->epoch	= epoch;
	arg->dkey	= dkey;
	arg->akey	= akey;
	arg->type	= type;
	arg->size	= size;
	arg->nr		= nr;
	arg->recxs	= recxs;
	arg->eprs	= eprs;
	arg->cookies	= cookies;
	arg->versions	= versions;
	arg->anchor	= anchor;
	arg->incr_order	= incr;

	return dss_task_run(task, DSS_POOL_REBUILD, rebuild_need_retry_cb, &oh);
}
