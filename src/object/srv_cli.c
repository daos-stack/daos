/**
 * (C) Copyright 2017-2020 Intel Corporation.
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
 * This file includes functions to call client daos API on the server side.
 */
#define D_LOGFAC	DD_FAC(object)

#include <daos/pool.h>
#include <daos/container.h>
#include <daos/object.h>
#include <daos/event.h>
#include <daos/task.h>
#include <daos_types.h>
#include <daos_errno.h>
#include <daos_event.h>
#include <daos_task.h>
#include <daos_srv/daos_server.h>

static int
dsc_obj_retry_cb(tse_task_t *task, void *arg)
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
	rc = dc_obj_layout_refresh(*oh);
	if (rc) {
		D_ERROR("task %p, dc_obj_layout_refresh failed rc %d\n",
			task, rc);
		task->dt_result = rc;
		return rc;
	}

	D_DEBUG(DB_REBUILD, "retry task %p\n", task);
	rc = dc_task_resched(task);
	if (rc != 0) {
		D_ERROR("Failed to re-init task (%p)\n", task);
		return rc;
	}

	/*
	 * Register the retry callback again, because it has been removed
	 * from the completion callback list. If this registration failed,
	 * the task will just stop retry on next run.
	 */
	rc = dc_task_reg_comp_cb(task, dsc_obj_retry_cb, oh, sizeof(*oh));
	return rc;
}

int
dsc_obj_open(daos_handle_t coh, daos_obj_id_t oid, unsigned int mode,
	     daos_handle_t *oh)
{
	tse_task_t	*task;
	int		 rc;

	rc = dc_obj_open_task_create(coh, oid, mode, oh, NULL,
				     dsc_scheduler(), &task);
	if (rc)
		return rc;

	return dsc_task_run(task, dsc_obj_retry_cb, NULL, 0, true);
}

int
dsc_obj_close(daos_handle_t oh)
{
	tse_task_t	 *task;
	int		  rc;

	rc = dc_obj_close_task_create(oh, NULL, dsc_scheduler(), &task);
	if (rc)
		return rc;

	return dsc_task_run(task, dsc_obj_retry_cb, &oh, sizeof(oh), true);
}

static int
tx_close_cb(tse_task_t *task, void *data)
{
	daos_handle_t *th = (daos_handle_t *)data;

	dc_tx_local_close(*th);
	return task->dt_result;
}

int
dsc_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		 uint32_t *nr, daos_key_desc_t *kds, d_sg_list_t *sgl,
		 daos_anchor_t *anchor)
{
	tse_task_t	*task;
	daos_handle_t	coh, th;
	int		rc;

	coh = dc_obj_hdl2cont_hdl(oh);
	rc = dc_tx_local_open(coh, epoch, DAOS_TF_RDONLY, &th);
	if (rc)
		return rc;

	rc = dc_obj_list_akey_task_create(oh, th, dkey, nr, kds, sgl, anchor,
					  NULL, dsc_scheduler(), &task);
	if (rc)
		return rc;

	rc = tse_task_register_comp_cb(task, tx_close_cb, &th, sizeof(th));
	if (rc) {
		dc_tx_local_close(th);
		tse_task_complete(task, rc);
		return rc;
	}

	return dsc_task_run(task, dsc_obj_retry_cb, &oh, sizeof(oh), true);
}

int
dsc_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
	      unsigned int nr, daos_iod_t *iods, d_sg_list_t *sgls,
	      daos_iom_t *maps, uint32_t extra_flag, uint32_t *extra_arg)
{
	tse_task_t	*task;
	daos_handle_t	coh, th;
	int		rc;

	coh = dc_obj_hdl2cont_hdl(oh);
	rc = dc_tx_local_open(coh, epoch, DAOS_TF_RDONLY, &th);
	if (rc)
		return rc;

	rc = dc_obj_fetch_task_create(oh, th, 0, dkey, nr, extra_flag,
				      iods, sgls, maps, extra_arg, NULL,
				      dsc_scheduler(), &task);
	if (rc)
		return rc;

	rc = tse_task_register_comp_cb(task, tx_close_cb, &th, sizeof(th));
	if (rc) {
		dc_tx_local_close(th);
		tse_task_complete(task, rc);
		return rc;
	}

	return dsc_task_run(task, dsc_obj_retry_cb, &oh, sizeof(oh), true);
}

int
dsc_obj_update(daos_handle_t oh, uint64_t flags, daos_key_t *dkey,
	       unsigned int nr, daos_iod_t *iods, d_sg_list_t *sgls)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_update_task_create(oh, DAOS_TX_NONE, flags, dkey, nr, iods,
				       sgls, NULL, dsc_scheduler(), &task);
	if (rc)
		return rc;

	return dsc_task_run(task, dsc_obj_retry_cb, &oh, sizeof(oh), true);
}

int
dsc_obj_list_obj(daos_handle_t oh, daos_epoch_range_t *epr, daos_key_t *dkey,
		 daos_key_t *akey, daos_size_t *size, uint32_t *nr,
		 daos_key_desc_t *kds, d_sg_list_t *sgl, daos_anchor_t *anchor,
		 daos_anchor_t *dkey_anchor, daos_anchor_t *akey_anchor,
		 d_iov_t *csum)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_list_obj_task_create(oh, DAOS_TX_NONE, epr, dkey, akey,
					 size, nr, kds, sgl, anchor,
					 dkey_anchor, akey_anchor, true, NULL,
					 dsc_scheduler(), csum, &task);
	if (rc)
		return rc;

	return dsc_task_run(task, dsc_obj_retry_cb, &oh, sizeof(oh), true);
}
