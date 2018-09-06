/**
 * (C) Copyright 2015-2018 Intel Corporation.
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

#include <daos/object.h>
#include <daos/task.h>
#include "client_internal.h"
#include "task_internal.h"

int
daos_obj_class_register(daos_handle_t coh, daos_oclass_id_t cid,
			daos_oclass_attr_t *cattr, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_class_query(daos_handle_t coh, daos_oclass_id_t cid,
		     daos_oclass_attr_t *cattr, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_class_list(daos_handle_t coh, daos_oclass_list_t *clist,
		    daos_anchor_t *anchor, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_declare(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
		 daos_obj_attr_t *oa, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_open(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
	      unsigned int mode, daos_handle_t *oh, daos_event_t *ev)
{
	tse_task_t *task;
	int rc;

	rc = dc_obj_open_task_create(coh, oid, epoch, mode, oh, ev, NULL,
				     &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_close(daos_handle_t oh, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_close_task_create(oh, ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_punch(daos_handle_t oh, daos_epoch_t epoch, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_punch_task_create(oh, epoch, ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_punch_dkeys(daos_handle_t oh, daos_epoch_t epoch, unsigned int nr,
		     daos_key_t *dkeys, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	if (dkeys == NULL) {
		D_ERROR("NULL dkeys\n");
		return -DER_INVAL;
	} else if (nr != 1) {
		/* TODO: create multiple tasks for punch of multiple dkeys */
		D_ERROR("Can't punch multiple dkeys for now\n");
		return -DER_INVAL;
	} else if (dkeys[0].iov_buf == NULL || dkeys[0].iov_len == 0) {
		D_ERROR("invalid dkey (NULL iov_buf or zero iov_len.\n");
		return -DER_INVAL;
	}

	rc = dc_obj_punch_dkeys_task_create(oh, epoch, nr, dkeys, ev, NULL,
					    &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_punch_akeys(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		     unsigned int nr, daos_key_t *akeys, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	if (dkey == NULL || dkey->iov_buf == NULL || dkey->iov_len == 0) {
		D_ERROR("NULL or invalid dkey\n");
		return -DER_INVAL;
	}

	rc = dc_obj_punch_akeys_task_create(oh, epoch, dkey, nr, akeys, ev,
					    NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_query(daos_handle_t oh, daos_epoch_t epoch, daos_obj_attr_t *oa,
	       d_rank_list_t *ranks, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
	       unsigned int nr, daos_iod_t *iods, daos_sg_list_t *sgls,
	       daos_iom_t *maps, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_fetch_task_create(oh, epoch, dkey, nr, iods, sgls,
				      maps, ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_update(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		unsigned int nr, daos_iod_t *iods, daos_sg_list_t *sgls,
		daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_update_task_create(oh, epoch, dkey, nr, iods, sgls,
				       ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_list_dkey(daos_handle_t oh, daos_epoch_t epoch, uint32_t *nr,
		   daos_key_desc_t *kds, daos_sg_list_t *sgl,
		   daos_anchor_t *anchor, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_list_dkey_task_create(oh, epoch, nr, kds, sgl, anchor, ev,
					  NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		   uint32_t *nr, daos_key_desc_t *kds, daos_sg_list_t *sgl,
		   daos_anchor_t *anchor, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_list_akey_task_create(oh, epoch, dkey, nr, kds, sgl, anchor,
					  ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_list_recx(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
	daos_key_t *akey, daos_size_t *size, uint32_t *nr,
	daos_recx_t *recxs, daos_epoch_range_t *eprs,
	daos_anchor_t *anchor, bool incr_order, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_list_recx_task_create(oh, epoch, dkey, akey, DAOS_IOD_ARRAY,
					  size, nr, recxs, eprs, anchor,
					  incr_order, ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}
