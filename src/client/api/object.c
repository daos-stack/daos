/**
 * (C) Copyright 2015-2020 Intel Corporation.
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
#include <daos/container.h>
#include "client_internal.h"
#include "task_internal.h"

int
daos_obj_register_class(daos_handle_t coh, daos_oclass_id_t cid,
			struct daos_oclass_attr *cattr, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_query_class(daos_handle_t coh, daos_oclass_id_t cid,
		     struct daos_oclass_attr *cattr, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_list_class(daos_handle_t coh, struct daos_oclass_list *clist,
		    daos_anchor_t *anchor, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_open(daos_handle_t coh, daos_obj_id_t oid, unsigned int mode,
	      daos_handle_t *oh, daos_event_t *ev)
{
	tse_task_t *task;
	int rc;

	rc = dc_obj_open_task_create(coh, oid, mode, oh, ev, NULL, &task);
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
daos_obj_punch(daos_handle_t oh, daos_handle_t th, uint64_t flags,
	       daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_punch_task_create(oh, th, flags, ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_punch_dkeys(daos_handle_t oh, daos_handle_t th, uint64_t flags,
		     unsigned int nr, daos_key_t *dkeys, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	if (nr != 1) {
		/* TODO: create multiple tasks for punch of multiple dkeys */
		D_ERROR("Can't punch multiple dkeys for now\n");
		return -DER_INVAL;
	}

	rc = dc_obj_punch_dkeys_task_create(oh, th, flags, nr, dkeys, ev, NULL,
					    &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_punch_akeys(daos_handle_t oh, daos_handle_t th, uint64_t flags,
		     daos_key_t *dkey, unsigned int nr, daos_key_t *akeys,
		     daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_punch_akeys_task_create(oh, th, flags, dkey, nr, akeys, ev,
					    NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_query(daos_handle_t oh, struct daos_obj_attr *oa, d_rank_list_t *ranks,
	       daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_query_key(daos_handle_t oh, daos_handle_t th, uint64_t flags,
		   daos_key_t *dkey, daos_key_t *akey, daos_recx_t *recx,
		   daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_query_key_task_create(oh, th, flags, dkey, akey, recx,
					  ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_fetch(daos_handle_t oh, daos_handle_t th, uint64_t flags,
	       daos_key_t *dkey, unsigned int nr, daos_iod_t *iods,
	       d_sg_list_t *sgls, daos_iom_t *maps, daos_event_t *ev)
{
	tse_task_t	*task;
	int		 rc;

	rc = dc_obj_fetch_task_create(oh, th, flags, dkey, nr, 0, iods,
				      sgls, maps, NULL, ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_update(daos_handle_t oh, daos_handle_t th, uint64_t flags,
		daos_key_t *dkey, unsigned int nr, daos_iod_t *iods,
		d_sg_list_t *sgls, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_update_task_create(oh, th, flags, dkey, nr, iods, sgls,
				       ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_list_dkey(daos_handle_t oh, daos_handle_t th, uint32_t *nr,
		   daos_key_desc_t *kds, d_sg_list_t *sgl,
		   daos_anchor_t *anchor, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_list_dkey_task_create(oh, th, nr, kds, sgl, anchor, ev,
					  NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_list_akey(daos_handle_t oh, daos_handle_t th, daos_key_t *dkey,
		   uint32_t *nr, daos_key_desc_t *kds, d_sg_list_t *sgl,
		   daos_anchor_t *anchor, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_list_akey_task_create(oh, th, dkey, nr, kds, sgl, anchor,
					  ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_list_recx(daos_handle_t oh, daos_handle_t th, daos_key_t *dkey,
		   daos_key_t *akey, daos_size_t *size, uint32_t *nr,
		   daos_recx_t *recxs, daos_epoch_range_t *eprs,
		   daos_anchor_t *anchor, bool incr_order, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_list_recx_task_create(oh, th, dkey, akey, DAOS_IOD_ARRAY,
					  size, nr, recxs, eprs, anchor,
					  incr_order, ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

/* Use to query the object layout */
int
daos_obj_layout_get(daos_handle_t coh, daos_obj_id_t oid,
		    struct daos_obj_layout **layout)
{
	daos_handle_t	oh;
	int		rc;

	rc = daos_obj_open(coh, oid, 0, &oh, NULL);
	if (rc)
		return rc;

	rc = dc_obj_layout_get(oh, layout);

	daos_obj_close(oh, NULL);
	if (rc != 0 && *layout != NULL)
		daos_obj_layout_free(*layout);

	return rc;
}

int
daos_obj_verify(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch)
{
	tse_task_t	*task;
	daos_handle_t	 oh;
	daos_epoch_t	*epochs_p = NULL;
	int		 epoch_nr = 0;
	int		 rc;

	rc = daos_obj_open(coh, oid, 0, &oh, NULL);
	if (rc != 0)
		return rc;

	/* Sync object against the given @epoch. */
	rc = dc_obj_sync_task_create(oh, epoch, &epochs_p, &epoch_nr,
				     NULL, NULL, &task);
	if (rc == 0) {
		rc = dc_task_schedule(task, true);
		if (rc == 0)
			rc = dc_obj_verify(oh, epochs_p, epoch_nr);
	}

	D_FREE(epochs_p);
	daos_obj_close(oh, NULL);
	return rc;
}

int
daos_obj_anchor_split(daos_handle_t oh, uint32_t *nr, daos_anchor_t *anchors)
{
	struct daos_obj_layout	*layout;
	int			rc;

	if (nr == NULL)
		return -DER_INVAL;

	rc = dc_obj_layout_get(oh, &layout);
	if (rc)
		return rc;

	/** TBD - support more than per shard iteration */
	if (*nr != 0 && *nr != layout->ol_nr) {
		D_ERROR("For now, num anchors should be the same as what is"
			" reported as optimal\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	*nr = layout->ol_nr;

	if (anchors) {
		uint32_t i;

		for (i = 0; i < layout->ol_nr; i++) {
			daos_anchor_set_zero(&anchors[i]);
			dc_obj_shard2anchor(&anchors[i], i);
			daos_anchor_set_flags(&anchors[i], DIOF_TO_SPEC_SHARD);
		}
	}
out:
	daos_obj_layout_free(layout);
	return rc;
}

int
daos_obj_anchor_set(daos_handle_t oh, uint32_t index, daos_anchor_t *anchor)
{
	/** TBD - support more than per shard iteration */
	daos_anchor_set_zero(anchor);
	dc_obj_shard2anchor(anchor, index);
	daos_anchor_set_flags(anchor, DIOF_TO_SPEC_SHARD);

	return 0;
}
