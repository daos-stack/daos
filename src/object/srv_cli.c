/**
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include <daos_srv/daos_engine.h>

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

	return dsc_task_run(task, NULL, NULL, 0, true);
}

int
dsc_obj_close(daos_handle_t oh)
{
	tse_task_t	 *task;
	int		  rc;

	rc = dc_obj_close_task_create(oh, NULL, dsc_scheduler(), &task);
	if (rc)
		return rc;

	return dsc_task_run(task, NULL, &oh, sizeof(oh), true);
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
	rc = dc_tx_local_open(coh, epoch, 0, &th);
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

	return dsc_task_run(task, NULL, &oh, sizeof(oh), true);
}

int
dsc_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
	      unsigned int nr, daos_iod_t *iods, d_sg_list_t *sgls,
	      daos_iom_t *maps, uint32_t extra_flag, uint32_t *extra_arg,
	      d_iov_t *csum_iov)
{
	tse_task_t	*task;
	daos_handle_t	coh, th;
	int		rc;

	coh = dc_obj_hdl2cont_hdl(oh);
	rc = dc_tx_local_open(coh, epoch, 0, &th);
	if (rc)
		return rc;

	rc = dc_obj_fetch_task_create(oh, th, 0, dkey, nr, extra_flag,
				      iods, sgls, maps, extra_arg, csum_iov,
				      NULL, dsc_scheduler(), &task);
	if (rc)
		return rc;

	rc = tse_task_register_comp_cb(task, tx_close_cb, &th, sizeof(th));
	if (rc) {
		dc_tx_local_close(th);
		tse_task_complete(task, rc);
		return rc;
	}

	return dsc_task_run(task, NULL, &oh, sizeof(oh), true);
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

	return dsc_task_run(task, NULL, &oh, sizeof(oh), true);
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

	return dsc_task_run(task, NULL, &oh, sizeof(oh), true);
}

int
dsc_obj_id2oc_attr(daos_obj_id_t oid, struct cont_props *prop,
		   struct daos_oclass_attr *oca)
{
	struct daos_oclass_attr *tmp;
	uint32_t                 nr_grps;

	tmp = daos_oclass_attr_find(oid, &nr_grps);
	if (!tmp)
		return -DER_NOSCHEMA;

	*oca = *tmp;
	oca->ca_grp_nr = nr_grps;
	if (daos_oclass_is_ec(oca)) {
		D_ASSERT(prop->dcp_ec_cell_sz > 0);
		oca->u.ec.e_len = prop->dcp_ec_cell_sz;
	}

	return 0;
}
