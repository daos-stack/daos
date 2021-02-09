/**
 * (C) Copyright 2015-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

int
daos_oit_open(daos_handle_t coh, daos_epoch_t epoch,
	      daos_handle_t *oh, daos_event_t *ev)
{
	tse_task_t	*task;
	daos_obj_id_t	 oid;
	int		 rc;

	oid = daos_oit_gen_id(epoch);
	rc = dc_obj_open_task_create(coh, oid, DAOS_OO_RO, oh, ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_oit_close(daos_handle_t oh, daos_event_t *ev)
{
	tse_task_t	*task;
	int		 rc;

	rc = dc_obj_close_task_create(oh, ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

/* OI table enumeration args */
struct oit_args {
	daos_key_desc_t		*oa_kds;
	d_sg_list_t		 oa_sgl;
	daos_key_t		 oa_dkey;
	/* A bucket is just an integer dkey for OI object, the current
	 * implementation only has one bucket (dkey).
	 */
	uint32_t		 oa_bucket;
	int			 oa_nr;
};

static int
oit_list_cb(tse_task_t *task, void *args)
{
	struct oit_args	*oa = *(struct oit_args **)args;

	d_sgl_fini(&oa->oa_sgl, false);
	D_FREE(oa->oa_kds);
	D_FREE(oa);
	return 0;
}

int
daos_oit_list(daos_handle_t oh, daos_obj_id_t *oids, uint32_t *oids_nr,
	      daos_anchor_t *anchor, daos_event_t *ev)
{
	struct oit_args	*oa;
	tse_task_t	*task;
	int		 i;
	int		 rc;

	if (daos_handle_is_inval(oh) ||
	    oids == NULL || oids_nr == NULL || *oids_nr <= 0 || !anchor)
		return -DER_INVAL;

	D_ALLOC_PTR(oa);
	if (!oa)
		return -DER_NOMEM;

	oa->oa_nr = *oids_nr;
	D_ALLOC_ARRAY(oa->oa_kds, oa->oa_nr);
	if (!oa->oa_kds)
		D_GOTO(failed, rc = -DER_NOMEM);

	rc = d_sgl_init(&oa->oa_sgl, oa->oa_nr);
	if (rc)
		D_GOTO(failed, rc = -DER_NOMEM);

	for (i = 0; i < oa->oa_nr; i++)
		d_iov_set(&oa->oa_sgl.sg_iovs[i], &oids[i], sizeof(oids[i]));

	/* all OIDs are stored under one dkey for now */
	d_iov_set(&oa->oa_dkey, &oa->oa_bucket, sizeof(oa->oa_bucket));
	rc = dc_obj_list_akey_task_create(oh, DAOS_TX_NONE, &oa->oa_dkey,
					  oids_nr, oa->oa_kds, &oa->oa_sgl,
					  anchor, ev, NULL, &task);
	if (rc)
		D_GOTO(failed, rc);

	rc = tse_task_register_comp_cb(task, oit_list_cb, &oa, sizeof(oa));
	if (rc) {
		tse_task_complete(task, rc);
		D_GOTO(failed, rc);
	}

	return dc_task_schedule(task, true);

failed:
	/* NB: OK to call with empty sgl */
	d_sgl_fini(&oa->oa_sgl, false);
	if (oa->oa_kds)
		D_FREE(oa->oa_kds);
	D_FREE(oa);
	return rc;
}
