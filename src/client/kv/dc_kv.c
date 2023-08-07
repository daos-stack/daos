/**
 * (C) Copyright 2017-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * src/kv/dc_kv.c
 */
#define D_LOGFAC	DD_FAC(kv)

#include <daos/common.h>
#include <daos/tse.h>
#include <daos/object.h>
#include <daos/kv.h>
#include <daos_api.h>
#include <daos_kv.h>
#include <daos_task.h>

struct dc_kv {
	/** link chain in the global handle hash table */
	struct d_hlink		hlink;
	/** DAOS object handle */
	daos_handle_t		daos_oh;
	/** DAOS container handle of kv */
	daos_handle_t		coh;
	/** DAOS object ID of kv */
	daos_obj_id_t		oid;
	/** object handle access mode */
	unsigned int		mode;
};

struct io_params {
	daos_key_t		dkey;
	daos_iod_t		iod;
	d_iov_t			iov;
	d_sg_list_t		sgl;
	char			akey_val;
};

static void
kv_free(struct d_hlink *hlink)
{
	struct dc_kv *kv;

	kv = container_of(hlink, struct dc_kv, hlink);
	D_ASSERT(daos_hhash_link_empty(&kv->hlink));
	D_FREE(kv);
}

static struct d_hlink_ops kv_h_ops = {
	.hop_free	= kv_free,
};

static struct dc_kv *
kv_alloc(void)
{
	struct dc_kv *kv;

	D_ALLOC_PTR(kv);
	if (kv == NULL)
		return NULL;

	daos_hhash_hlink_init(&kv->hlink, &kv_h_ops);
	return kv;
}

static void
kv_decref(struct dc_kv *kv)
{
	daos_hhash_link_putref(&kv->hlink);
}

static daos_handle_t
kv_ptr2hdl(struct dc_kv *kv)
{
	daos_handle_t oh;

	daos_hhash_link_key(&kv->hlink, &oh.cookie);
	return oh;
}

static struct dc_kv *
kv_hdl2ptr(daos_handle_t oh)
{
	struct d_hlink *hlink;

	hlink = daos_hhash_link_lookup(oh.cookie);
	if (hlink == NULL)
		return NULL;

	return container_of(hlink, struct dc_kv, hlink);
}

daos_handle_t daos_kv2objhandle(daos_handle_t kv_oh)
{
	struct dc_kv *dk = kv_hdl2ptr(kv_oh);

	if (dk)
		return dk->daos_oh;

	return DAOS_HDL_INVAL;
}

static void
kv_hdl_link(struct dc_kv *kv)
{
	daos_hhash_link_insert(&kv->hlink, DAOS_HTYPE_KV);
}

static void
kv_hdl_unlink(struct dc_kv *kv)
{
	daos_hhash_link_delete(&kv->hlink);
}

static int
open_handle_cb(tse_task_t *task, void *data)
{
	daos_kv_open_t	*args = *((daos_kv_open_t **)data);
	struct dc_kv	*kv;
	int		rc = task->dt_result;

	if (rc != 0) {
		D_ERROR("Failed to open kv obj "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_obj, rc);
	}

	/** Create an KV OH from the DAOS one */
	kv = kv_alloc();
	if (kv == NULL)
		D_GOTO(err_obj, rc = -DER_NOMEM);

	kv->coh		= args->coh;
	kv->oid.hi	= args->oid.hi;
	kv->oid.lo	= args->oid.lo;
	kv->mode	= DAOS_OO_RW;
	kv->daos_oh	= *args->oh;

	kv_hdl_link(kv);
	*args->oh = kv_ptr2hdl(kv);

	return 0;

err_obj:
	if (daos_handle_is_valid(*args->oh)) {
		daos_obj_close_t	*close_args;
		tse_task_t		*close_task;
		int			rc2;

		rc2 = daos_task_create(DAOS_OPC_OBJ_CLOSE, tse_task2sched(task),
				       0, NULL, &close_task);
		if (rc2) {
			D_ERROR("Failed to create task to cleanup obj hdl\n");
			return rc;
		}

		close_args = daos_task_get_args(close_task);
		close_args->oh = *args->oh;
		tse_task_schedule(close_task, true);
	}
	return rc;
}

static int
free_handle_cb(tse_task_t *task, void *data)
{
	struct dc_kv	*kv = *((struct dc_kv **)data);
	int		rc = task->dt_result;

	if (rc != 0)
		return rc;

	kv_hdl_unlink(kv);

	/* -1 for ref taken in dc_kv_close */
	kv_decref(kv);
	/* -1 for kv handle */
	kv_decref(kv);

	return 0;
}

int
dc_kv_open(tse_task_t *task)
{
	daos_kv_open_t		*args = daos_task_get_args(task);
	tse_task_t		*open_task = NULL;
	daos_obj_open_t		*open_args;
	int			rc;

	if (!daos_is_kv(args->oid)) {
		D_ERROR("KV object must be of type Flat KV (OID feats).\n");
		D_GOTO(err_ptask, rc = -DER_INVAL);
	}

	/** Create task to open object */
	rc = daos_task_create(DAOS_OPC_OBJ_OPEN, tse_task2sched(task), 0, NULL, &open_task);
	if (rc != 0) {
		D_ERROR("Failed to create object_open task "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_ptask, rc);
	}

	open_args = daos_task_get_args(open_task);
	open_args->coh	= args->coh;
	open_args->oid	= args->oid;
	open_args->mode	= args->mode;
	open_args->oh	= args->oh;

	/** The upper task completes when the open task completes */
	rc = tse_task_register_deps(task, 1, &open_task);
	if (rc != 0) {
		D_ERROR("Failed to register dependency\n");
		D_GOTO(err_put1, rc);
	}
	rc = tse_task_register_comp_cb(task, open_handle_cb, &args, sizeof(args));
	if (rc != 0) {
		D_ERROR("Failed to register completion cb\n");
		D_GOTO(err_put1, rc);
	}
	rc = tse_task_schedule(open_task, true);
	if (rc)
		D_GOTO(err_ptask, rc);
	return rc;

err_put1:
	tse_task_complete(open_task, rc);
err_ptask:
	tse_task_complete(task, rc);
	return rc;
}

int
dc_kv_close_direct(daos_handle_t oh)
{
	struct dc_kv	*kv;
	int		rc;

	kv = kv_hdl2ptr(oh);
	if (kv == NULL)
		return -DER_NO_HDL;

	rc = daos_obj_close(kv->daos_oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_close() failed: "DF_RC"\n", DP_RC(rc));
		kv_decref(kv);
		return rc;
	}

	kv_hdl_unlink(kv);
	/* -1 for ref taken here */
	kv_decref(kv);
	/* -1 for kv handle */
	kv_decref(kv);
	return 0;
}

int
dc_kv_close(tse_task_t *task)
{
	daos_kv_close_t		*args = daos_task_get_args(task);
	struct dc_kv		*kv;
	tse_task_t		*close_task;
	daos_obj_close_t	*close_args;
	int			rc;

	/** decref for that in free_handle_cb */
	kv = kv_hdl2ptr(args->oh);
	if (kv == NULL)
		D_GOTO(err_ptask, rc = -DER_NO_HDL);

	/** Create task to close object */
	rc = daos_task_create(DAOS_OPC_OBJ_CLOSE, tse_task2sched(task), 0, NULL, &close_task);
	if (rc != 0) {
		D_ERROR("Failed to create object_close task\n");
		D_GOTO(err_put1, rc);
	}
	close_args = daos_task_get_args(close_task);
	close_args->oh = kv->daos_oh;

	/** The upper task completes when the close task completes */
	rc = tse_task_register_deps(task, 1, &close_task);
	if (rc != 0) {
		D_ERROR("Failed to register dependency\n");
		D_GOTO(err_put2, rc);
	}

	/** Add a completion CB on the upper task to free the kv */
	rc = tse_task_register_cbs(task, NULL, NULL, 0, free_handle_cb,
				   &kv, sizeof(kv));
	if (rc != 0) {
		D_ERROR("Failed to register completion cb\n");
		D_GOTO(err_put2, rc);
	}

	rc = tse_task_schedule(close_task, true);
	if (rc)
		D_GOTO(err_put1, rc);
	return rc;
err_put2:
	tse_task_complete(close_task, rc);
err_put1:
	kv_decref(kv);
err_ptask:
	tse_task_complete(task, rc);
	return rc;
}

int
dc_kv_destroy(tse_task_t *task)
{
	daos_kv_destroy_t	*args = daos_task_get_args(task);
	struct dc_kv		*kv;
	tse_task_t		*punch_task;
	daos_obj_punch_t	*punch_args;
	int			rc;

	kv = kv_hdl2ptr(args->oh);
	if (kv == NULL)
		D_GOTO(err_ptask, rc = -DER_NO_HDL);

	/** Create task to punch object */
	rc = daos_task_create(DAOS_OPC_OBJ_PUNCH, tse_task2sched(task), 0, NULL, &punch_task);
	if (rc != 0) {
		D_ERROR("Failed to create object_punch task\n");
		D_GOTO(err_put1, rc);
	}
	punch_args = daos_task_get_args(punch_task);
	punch_args->oh		= kv->daos_oh;
	punch_args->th		= args->th;
	punch_args->dkey	= NULL;
	punch_args->akeys	= NULL;
	punch_args->akey_nr	= 0;

	/** The upper task completes when the punch task completes */
	rc = tse_task_register_deps(task, 1, &punch_task);
	if (rc != 0) {
		D_ERROR("Failed to register dependency\n");
		D_GOTO(err_put2, rc);
	}

	rc = tse_task_schedule(punch_task, true);
	if (rc)
		D_GOTO(err_put1, rc);
	kv_decref(kv);
	return rc;

err_put2:
	tse_task_complete(punch_task, rc);
err_put1:
	kv_decref(kv);
err_ptask:
	tse_task_complete(task, rc);
	return rc;
}

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
dc_kv_put(tse_task_t *task)
{
	daos_kv_put_t		*args = daos_task_get_args(task);
	struct dc_kv		*kv = NULL;
	daos_obj_update_t	*update_args;
	tse_task_t		*update_task;
	struct io_params	*params = NULL;
	bool			free_params = true;
	int			rc;

	if (args->key == NULL)
		D_GOTO(err_task, rc = -DER_INVAL);

	kv = kv_hdl2ptr(args->oh);
	if (kv == NULL)
		D_GOTO(err_task, rc = -DER_NO_HDL);

	D_ALLOC_PTR(params);
	if (params == NULL)
		D_GOTO(err_task, rc = -DER_NOMEM);

	/** init dkey */
	d_iov_set(&params->dkey, (void *)args->key, strlen(args->key));

	/** init iod. */
	params->akey_val = '0';
	d_iov_set(&params->iod.iod_name, &params->akey_val, 1);
	params->iod.iod_nr	= 1;
	params->iod.iod_recxs	= NULL;
	params->iod.iod_size	= args->buf_size;
	params->iod.iod_type	= DAOS_IOD_SINGLE;

	/** init sgl */
	params->sgl.sg_nr = 1;
	params->sgl.sg_iovs = &params->iov;
	d_iov_set(&params->sgl.sg_iovs[0], (void *)args->buf, args->buf_size);

	rc = daos_task_create(DAOS_OPC_OBJ_UPDATE, tse_task2sched(task), 0, NULL, &update_task);
	if (rc != 0)
		D_GOTO(err_task, rc);

	update_args = daos_task_get_args(update_task);
	update_args->oh		= kv->daos_oh;
	update_args->th		= args->th;
	update_args->flags	= args->flags;
	update_args->dkey	= &params->dkey;
	update_args->nr		= 1;
	update_args->iods	= &params->iod;
	update_args->sgls	= &params->sgl;

	rc = tse_task_register_comp_cb(task, free_io_params_cb, &params, sizeof(params));
	if (rc != 0)
		D_GOTO(err_utask, rc);
	free_params = false;

	rc = tse_task_register_deps(task, 1, &update_task);
	if (rc != 0)
		D_GOTO(err_utask, rc);

	rc = tse_task_schedule(update_task, true);
	if (rc != 0)
		D_GOTO(err_task, rc);
	kv_decref(kv);
	return 0;

err_utask:
	tse_task_complete(update_task, rc);
err_task:
	tse_task_complete(task, rc);
	if (free_params)
		D_FREE(params);
	if (kv)
		kv_decref(kv);
	return rc;
}

int
dc_kv_get(tse_task_t *task)
{
	daos_kv_get_t		*args = daos_task_get_args(task);
	struct dc_kv		*kv = NULL;
	daos_obj_fetch_t	*fetch_args;
	tse_task_t		*fetch_task;
	struct io_params	*params = NULL;
	void			*buf;
	daos_size_t		*buf_size;
	bool			free_params = true;
	int			rc;

	if (args->key == NULL)
		D_GOTO(err_task, rc = -DER_INVAL);

	kv = kv_hdl2ptr(args->oh);
	if (kv == NULL)
		D_GOTO(err_task, rc = -DER_NO_HDL);

	buf = args->buf;
	buf_size = args->buf_size;
	if (buf_size == NULL) {
		D_ERROR("Buffer size pointer is NULL\n");
		D_GOTO(err_task, rc = -DER_INVAL);
	}

	D_ALLOC_PTR(params);
	if (params == NULL)
		D_GOTO(err_task, rc = -DER_NOMEM);

	/** init dkey */
	d_iov_set(&params->dkey, (void *)args->key, strlen(args->key));

	/** init iod. */
	params->akey_val = '0';
	d_iov_set(&params->iod.iod_name, &params->akey_val, 1);
	params->iod.iod_nr	= 1;
	params->iod.iod_recxs	= NULL;
	params->iod.iod_size	= *buf_size;
	params->iod.iod_type	= DAOS_IOD_SINGLE;

	/** init sgl */
	if (buf && *buf_size) {
		d_iov_set(&params->iov, buf, *buf_size);
		params->sgl.sg_iovs = &params->iov;
		params->sgl.sg_nr = 1;
	}

	rc = daos_task_create(DAOS_OPC_OBJ_FETCH, tse_task2sched(task), 0, NULL, &fetch_task);
	if (rc != 0)
		D_GOTO(err_task, rc);

	fetch_args = daos_task_get_args(fetch_task);
	fetch_args->oh		= kv->daos_oh;
	fetch_args->th		= args->th;
	fetch_args->flags	= args->flags;
	fetch_args->dkey	= &params->dkey;
	fetch_args->nr		= 1;
	fetch_args->iods	= &params->iod;
	if (buf && *buf_size)
		fetch_args->sgls = &params->sgl;

	rc = tse_task_register_comp_cb(fetch_task, set_size_cb, &buf_size, sizeof(buf_size));
	if (rc != 0)
		D_GOTO(err_ftask, rc);

	rc = tse_task_register_comp_cb(task, free_io_params_cb, &params, sizeof(params));
	if (rc != 0)
		D_GOTO(err_ftask, rc);
	free_params = false;

	rc = tse_task_register_deps(task, 1, &fetch_task);
	if (rc != 0)
		D_GOTO(err_ftask, rc);

	rc = tse_task_schedule(fetch_task, true);
	if (rc != 0)
		D_GOTO(err_task, rc);
	kv_decref(kv);
	return 0;

err_ftask:
	tse_task_complete(fetch_task, rc);
err_task:
	tse_task_complete(task, rc);
	if (free_params)
		D_FREE(params);
	if (kv)
		kv_decref(kv);
	return rc;
}

int
dc_kv_remove(tse_task_t *task)
{
	daos_kv_remove_t	*args = daos_task_get_args(task);
	struct dc_kv		*kv = NULL;
	daos_obj_punch_t	*punch_args;
	tse_task_t		*punch_task;
	struct io_params	*params = NULL;
	bool			free_params = true;
	int			rc;

	if (args->key == NULL)
		D_GOTO(err_task, rc = -DER_INVAL);

	kv = kv_hdl2ptr(args->oh);
	if (kv == NULL)
		D_GOTO(err_task, rc = -DER_NO_HDL);

	D_ALLOC_PTR(params);
	if (params == NULL)
		D_GOTO(err_task, rc = -DER_NOMEM);

	/** init dkey */
	d_iov_set(&params->dkey, (void *)args->key, strlen(args->key));

	rc = daos_task_create(DAOS_OPC_OBJ_PUNCH_DKEYS, tse_task2sched(task), 0, NULL, &punch_task);
	if (rc != 0)
		D_GOTO(err_task, rc);

	punch_args = daos_task_get_args(punch_task);
	punch_args->oh		= kv->daos_oh;
	punch_args->th		= args->th;
	punch_args->flags	= args->flags;
	punch_args->dkey	= &params->dkey;
	punch_args->akeys	= NULL;
	punch_args->akey_nr	= 0;

	rc = tse_task_register_comp_cb(task, free_io_params_cb, &params, sizeof(params));
	if (rc != 0)
		D_GOTO(err_ptask, rc);
	free_params = false;

	rc = tse_task_register_deps(task, 1, &punch_task);
	if (rc != 0)
		D_GOTO(err_ptask, rc);

	rc = tse_task_schedule(punch_task, true);
	if (rc != 0)
		D_GOTO(err_task, rc);
	kv_decref(kv);
	return 0;

err_ptask:
	tse_task_complete(punch_task, rc);
err_task:
	tse_task_complete(task, rc);
	if (free_params)
		D_FREE(params);
	if (kv)
		kv_decref(kv);
	return rc;
}

int
dc_kv_list(tse_task_t *task)
{
	daos_kv_list_t		*args = daos_task_get_args(task);
	struct dc_kv		*kv = NULL;
	daos_obj_list_dkey_t	*list_args;
	tse_task_t		*list_task;
	int			rc;

	kv = kv_hdl2ptr(args->oh);
	if (kv == NULL)
		D_GOTO(err_task, rc = -DER_NO_HDL);

	rc = daos_task_create(DAOS_OPC_OBJ_LIST_DKEY, tse_task2sched(task), 0, NULL, &list_task);
	if (rc != 0)
		D_GOTO(err_task, rc);

	list_args = daos_task_get_args(list_task);
	list_args->oh		= kv->daos_oh;
	list_args->th		= args->th;
	list_args->nr		= args->nr;
	list_args->sgl		= args->sgl;
	list_args->kds		= args->kds;
	list_args->dkey_anchor	= args->anchor;

	rc = tse_task_register_deps(task, 1, &list_task);
	if (rc != 0)
		D_GOTO(err_ltask, rc);

	rc = tse_task_schedule(list_task, true);
	if (rc != 0)
		D_GOTO(err_task, rc);
	kv_decref(kv);
	return 0;

err_ltask:
	tse_task_complete(list_task, rc);
err_task:
	tse_task_complete(task, rc);
	if (kv)
		kv_decref(kv);
	return rc;
}
