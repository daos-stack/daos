/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
/*
 * This file is part of daos_m
 *
 * src/addons/dac_array.c
 */
#define D_LOGFAC	DD_FAC(addons)

#include <daos/common.h>
#include <daos/container.h>
#include <daos/tse.h>
#include <daos/addons.h>
#include <daos_api.h>
#include <daos_addons.h>
#include <daos_task.h>

#define AKEY_MAGIC_V	0xdaca55a9daca55a9
#define ARRAY_MD_KEY	"daos_array_metadata"
#define CELL_SIZE	"daos_array_cell_size"
#define CHUNK_SIZE	"daos_array_chunk_size"

struct dac_array {
	/** DAOS KV object handle */
	daos_handle_t		daos_oh;
	/** Array cell size of each element */
	daos_size_t		cell_size;
	/** elems to store in 1 dkey before moving to the next one in the grp */
	daos_size_t		chunk_size;
	/** DAOS container handle of array */
	daos_handle_t		coh;
	/** DAOS object ID of array */
	daos_obj_id_t		oid;
	/** object handle access mode */
	unsigned int		mode;
	/** ref count on array */
	unsigned int		cob_ref;
	/** protect ref count */
	pthread_spinlock_t	cob_lock;
};

struct md_params {
	daos_key_t		dkey;
	char			*dkey_str;
	char			*akey_str;
	daos_iod_t		iod;
	daos_recx_t		recx;
	daos_sg_list_t		sgl;
	daos_iov_t		sg_iovs[3];
	uint64_t		magic_val;
};

struct io_params {
	daos_key_t		dkey;
	char			*dkey_str;
	char			akey_str;
	daos_iod_t		iod;
	daos_sg_list_t		sgl;
	bool			user_sgl_used;
	daos_size_t		cell_size;
	struct io_params	*next;
};

static struct dac_array *
array_alloc(void)
{
	struct dac_array *obj;
	int rc = 0;

	D_ALLOC_PTR(obj);
	if (obj == NULL)
		return NULL;

	obj->cob_ref = 1;
	rc = D_SPIN_INIT(&obj->cob_lock, PTHREAD_PROCESS_PRIVATE);
	if (rc != 0) {
		D_FREE_PTR(obj);
		return NULL;
	}
	return obj;
}

static void
array_decref(struct dac_array *obj)
{
	D_SPIN_LOCK(&obj->cob_lock);
	obj->cob_ref--;
	if (obj->cob_ref == 0) {
		D_SPIN_UNLOCK(&obj->cob_lock);
		D_SPIN_DESTROY(&obj->cob_lock);
		D_FREE_PTR(obj);
	} else {
		D_SPIN_UNLOCK(&obj->cob_lock);
	}
}

static void
array_addref(struct dac_array *obj)
{
	D_SPIN_LOCK(&obj->cob_lock);
	obj->cob_ref++;
	D_SPIN_UNLOCK(&obj->cob_lock);
}

static daos_handle_t
array_ptr2hdl(struct dac_array *obj)
{
	daos_handle_t oh;

	oh.cookie = (uint64_t)obj;
	return oh;
}

static struct dac_array *
array_hdl2ptr(daos_handle_t oh)
{
	struct dac_array *obj;

	obj = (struct dac_array *)oh.cookie;
	array_addref(obj);
	return obj;
}


static int
free_md_params_cb(tse_task_t *task, void *data)
{
	struct md_params *params = *((struct md_params **)data);

	D_FREE_PTR(params);
	return task->dt_result;
}

static int
free_val_cb(tse_task_t *task, void *data)
{
	char	*val = *((char **)data);
	int	rc = task->dt_result;

	free(val);
	return rc;
}

static int
free_io_params_cb(tse_task_t *task, void *data)
{
	struct	io_params *io_list = *((struct io_params **)data);
	int	rc = task->dt_result;

	while (io_list) {
		struct io_params *current = io_list;

		if (current->iod.iod_recxs) {
			free(current->iod.iod_recxs);
			current->iod.iod_recxs = NULL;
		}
		if (current->sgl.sg_iovs) {
			free(current->sgl.sg_iovs);
			current->sgl.sg_iovs = NULL;
		}
		if (current->dkey_str) {
			free(current->dkey_str);
			current->dkey_str = NULL;
		}

		io_list = current->next;
		D_FREE_PTR(current);
	}

	return rc;
}

static int
create_handle_cb(tse_task_t *task, void *data)
{
	daos_array_create_t *args = *((daos_array_create_t **)data);
	struct dac_array	*array;
	int			rc = task->dt_result;

	if (rc != 0) {
		D_ERROR("Failed to create array obj (%d)\n", rc);
		D_GOTO(err_obj, rc);
	}

	/** Create an array OH from the DAOS one */
	array = array_alloc();
	if (array == NULL)
		D_GOTO(err_obj, rc = -DER_NOMEM);

	array->coh = args->coh;
	array->oid.hi = args->oid.hi;
	array->oid.lo = args->oid.lo;
	array->mode = DAOS_OO_RW;
	array->cell_size = args->cell_size;
	array->chunk_size = args->chunk_size;
	array->daos_oh = *args->oh;

	*args->oh = array_ptr2hdl(array);

	return 0;

err_obj:
	{
		daos_obj_close_t *close_args;
		tse_task_t *close_task;

		daos_task_create(DAOS_OPC_OBJ_CLOSE, tse_task2sched(task),
				 0, NULL, &close_task);
		close_args = daos_task_get_args(close_task);
		close_args->oh = *args->oh;
		return rc;
	}
}

static int
free_handle_cb(tse_task_t *task, void *data)
{
	daos_handle_t		*oh = (daos_handle_t *)data;
	struct dac_array	*array;
	int			rc = task->dt_result;

	if (rc != 0)
		return rc;

	array = array_hdl2ptr(*oh);
	if (array == NULL)
		return -DER_NO_HDL;

	/** -1 for hdl2ptr */
	array_decref(array);
	/** -1 for array_create/open */
	array_decref(array);

	return 0;
}

#define DAC_ARRAY_GLOB_MAGIC	(0xdaca0387)

/* Structure of global buffer for dac_array */
struct dac_array_glob {
	uint32_t	magic;
	uint32_t	mode;
	daos_obj_id_t	oid;
	daos_size_t	cell_size;
	daos_size_t	chunk_size;
	uuid_t		cont_uuid;
	uuid_t		coh_uuid;
};

static inline daos_size_t
dac_array_glob_buf_size()
{
	return sizeof(struct dac_array_glob);
}

static inline void
swap_array_glob(struct dac_array_glob *array_glob)
{
	D_ASSERT(array_glob != NULL);

	D_SWAP32S(&array_glob->magic);
	D_SWAP32S(&array_glob->mode);
	D_SWAP64S(&array_glob->cell_size);
	D_SWAP64S(&array_glob->chunk_size);
	D_SWAP64S(&array_glob->oid.hi);
	D_SWAP64S(&array_glob->oid.lo);
	/* skip cont_uuid */
	/* skip coh_uuid */
}

static int
dac_array_l2g(daos_handle_t oh, daos_iov_t *glob)
{
	struct dac_array	*array;
	struct dac_array_glob	*array_glob;
	uuid_t			 coh_uuid;
	uuid_t			 cont_uuid;
	daos_size_t		 glob_buf_size;
	int			 rc = 0;

	D_ASSERT(glob != NULL);

	array = array_hdl2ptr(oh);
	if (array == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	rc = dc_cont_hdl2uuid(array->coh, &coh_uuid, &cont_uuid);
	if (rc != 0)
		D_GOTO(out_array, rc);

	glob_buf_size = dac_array_glob_buf_size();

	if (glob->iov_buf == NULL) {
		glob->iov_buf_len = glob_buf_size;
		D_GOTO(out_array, rc = 0);
	}

	if (glob->iov_buf_len < glob_buf_size) {
		D_DEBUG(DF_DSMC, "Larger glob buffer needed ("DF_U64" bytes "
			"provided, "DF_U64" required).\n", glob->iov_buf_len,
			glob_buf_size);
		glob->iov_buf_len = glob_buf_size;
		D_GOTO(out_array, rc = -DER_TRUNC);
	}
	glob->iov_len = glob_buf_size;

	/* init global handle */
	array_glob = (struct dac_array_glob *)glob->iov_buf;
	array_glob->magic	= DAC_ARRAY_GLOB_MAGIC;
	array_glob->cell_size	= array->cell_size;
	array_glob->chunk_size	= array->chunk_size;
	array_glob->mode	= array->mode;
	array_glob->oid.hi	= array->oid.hi;
	array_glob->oid.lo	= array->oid.lo;
	uuid_copy(array_glob->coh_uuid, coh_uuid);
	uuid_copy(array_glob->cont_uuid, cont_uuid);

out_array:
	array_decref(array);
out:
	if (rc)
		D_ERROR("daos_array_l2g failed, rc: %d\n", rc);
	return rc;
}

int
dac_array_local2global(daos_handle_t oh, daos_iov_t *glob)
{
	int rc = 0;

	if (glob == NULL) {
		D_ERROR("Invalid parameter, NULL glob pointer.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (glob->iov_buf != NULL && (glob->iov_buf_len == 0 ||
	    glob->iov_buf_len < glob->iov_len)) {
		D_ERROR("Invalid parameter of glob, iov_buf %p, iov_buf_len "
			""DF_U64", iov_len "DF_U64".\n", glob->iov_buf,
			glob->iov_buf_len, glob->iov_len);
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dac_array_l2g(oh, glob);

out:
	return rc;
}

static int
dac_array_g2l(daos_handle_t coh, struct dac_array_glob *array_glob,
	      daos_handle_t *oh)
{
	struct dac_array	*array;
	uuid_t			coh_uuid;
	uuid_t			cont_uuid;
	int			rc = 0;

	D_ASSERT(array_glob != NULL);
	D_ASSERT(oh != NULL);

	/** Check container uuid mismatch */
	rc = dc_cont_hdl2uuid(coh, &coh_uuid, &cont_uuid);
	if (rc != 0)
		D_GOTO(out, rc);
	if (uuid_compare(cont_uuid, array_glob->cont_uuid) != 0) {
		D_ERROR("Container uuid mismatch, in coh: "DF_UUID", "
			"in array_glob:" DF_UUID"\n", DP_UUID(cont_uuid),
			DP_UUID(array_glob->cont_uuid));
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (uuid_compare(coh_uuid, array_glob->coh_uuid) != 0) {
		D_ERROR("Container handle mismatch, in coh: "DF_UUID", "
			"in array_glob:" DF_UUID"\n", DP_UUID(coh_uuid),
			DP_UUID(array_glob->coh_uuid));
		D_GOTO(out, rc = -DER_INVAL);
	}

	/** create an array open handle */
	array = array_alloc();
	if (array == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = daos_obj_open(coh, array_glob->oid, 0, array_glob->mode,
			   &array->daos_oh, NULL);
	if (rc) {
		D_ERROR("Failed local object open (%d).\n", rc);
		D_GOTO(out_array, rc);
	}
	array->coh = coh;
	array->cell_size = array_glob->cell_size;
	array->chunk_size = array_glob->chunk_size;
	array->oid.hi = array_glob->oid.hi;
	array->oid.lo = array_glob->oid.lo;
	array->mode = array_glob->mode;
	*oh = array_ptr2hdl(array);

out_array:
	if (rc)
		array_decref(array);
out:
	return rc;
}

int
dac_array_global2local(daos_handle_t coh, daos_iov_t glob, daos_handle_t *oh)
{
	struct dac_array_glob	*array_glob;
	int			 rc = 0;

	if (oh == NULL) {
		D_DEBUG(DF_DSMC, "Invalid parameter, NULL coh.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (glob.iov_buf == NULL || glob.iov_buf_len < glob.iov_len ||
	    glob.iov_len != dac_array_glob_buf_size()) {
		D_DEBUG(DF_DSMC, "Invalid parameter of glob, iov_buf %p, "
			"iov_buf_len "DF_U64", iov_len "DF_U64".\n",
			glob.iov_buf, glob.iov_buf_len, glob.iov_len);
		D_GOTO(out, rc = -DER_INVAL);
	}

	array_glob = (struct dac_array_glob *)glob.iov_buf;
	if (array_glob->magic == D_SWAP32(DAC_ARRAY_GLOB_MAGIC)) {
		swap_array_glob(array_glob);
		D_ASSERT(array_glob->magic == DAC_ARRAY_GLOB_MAGIC);

	} else if (array_glob->magic != DAC_ARRAY_GLOB_MAGIC) {
		D_ERROR("Bad magic value: 0x%x.\n", array_glob->magic);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (array_glob->cell_size == 0 ||
	    array_glob->chunk_size == 0) {
		D_ERROR("Invalid parameter, cell/chunk size is 0.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dac_array_g2l(coh, array_glob, oh);
	if (rc != 0)
		D_ERROR("dac_array_g2l failed (%d).\n", rc);

out:
	return rc;
}

static int
write_md_cb(tse_task_t *task, void *data)
{
	daos_array_create_t *args = *((daos_array_create_t **)data);
	daos_obj_update_t *update_args;
	struct md_params *params;
	int rc = task->dt_result;

	if (rc != 0) {
		D_ERROR("Failed to open object (%d)\n", rc);
		return rc;
	}

	D_ALLOC_PTR(params);
	if (params == NULL) {
		D_ERROR("Failed memory allocation\n");
		return -DER_NOMEM;
	}

	/** write metadata to DKEY 0 */
	params->dkey_str = "0";
	daos_iov_set(&params->dkey, params->dkey_str, strlen(params->dkey_str));

	/** set SGL */
	params->magic_val = AKEY_MAGIC_V;
	daos_iov_set(&params->sg_iovs[0], &params->magic_val, sizeof(uint64_t));
	daos_iov_set(&params->sg_iovs[1], &args->cell_size,
		     sizeof(daos_size_t));
	daos_iov_set(&params->sg_iovs[2], &args->chunk_size,
		     sizeof(daos_size_t));
	params->sgl.sg_nr = 3;
	params->sgl.sg_nr_out = 0;
	params->sgl.sg_iovs = params->sg_iovs;

	/** set IOD */
	params->akey_str = ARRAY_MD_KEY;
	daos_iov_set(&params->iod.iod_name, (void *)params->akey_str,
		     strlen(params->akey_str));
	daos_csum_set(&params->iod.iod_kcsum, NULL, 0);
	params->iod.iod_nr	= 1;
	params->iod.iod_size	= sizeof(daos_size_t);
	params->recx.rx_idx	= 0;
	params->recx.rx_nr	= 3;
	params->iod.iod_recxs	= &params->recx;
	params->iod.iod_eprs	= NULL;
	params->iod.iod_csums	= NULL;
	params->iod.iod_type	= DAOS_IOD_ARRAY;

	/** Set the args for the update task */
	update_args = daos_task_get_args(task);
	update_args->oh = *args->oh;
	update_args->epoch = args->epoch;
	update_args->dkey = &params->dkey;
	update_args->nr = 1;
	update_args->iods = &params->iod;
	update_args->sgls = &params->sgl;

	rc = tse_task_register_comp_cb(task, free_md_params_cb, &params,
				       sizeof(params));
	if (rc != 0)
		return rc;

	return 0;
}

int
dac_array_create(tse_task_t *task)
{
	daos_array_create_t	*args = daos_task_get_args(task);
	tse_task_t		*open_task, *update_task;
	daos_obj_open_t		*open_args;
	int			rc;

	/** Create task to open object */
	rc = daos_task_create(DAOS_OPC_OBJ_OPEN, tse_task2sched(task),
			      0, NULL, &open_task);
	if (rc != 0) {
		D_ERROR("Failed to create object_open task\n");
		return rc;
	}

	open_args = daos_task_get_args(open_task);
	open_args->coh	 = args->coh;
	open_args->oid	 = args->oid;
	open_args->epoch = args->epoch;
	open_args->mode	 = DAOS_OO_RW;
	open_args->oh	 = args->oh;

	tse_task_schedule(open_task, false);

	/** Create task to write object metadata */
	rc = daos_task_create(DAOS_OPC_OBJ_UPDATE, tse_task2sched(task),
			      1, &open_task, &update_task);
	if (rc != 0) {
		D_ERROR("Failed to create object_update task\n");
		D_GOTO(err_put1, rc);
	}

	/** add a prepare CB to set the args for the metadata write */
	rc = tse_task_register_cbs(update_task, write_md_cb, &args,
				   sizeof(args), NULL, NULL, 0);
	if (rc != 0) {
		D_ERROR("Failed to register prep CB\n");
		D_GOTO(err_put2, rc);
	}

	/** The upper task completes when the update task completes */
	rc = tse_task_register_deps(task, 1, &update_task);
	if (rc != 0) {
		D_ERROR("Failed to register dependency\n");
		D_GOTO(err_put2, rc);
	}

	/** CB to generate the array OH */
	rc = tse_task_register_cbs(task, NULL, NULL, 0, create_handle_cb,
				    &args, sizeof(args));
	if (rc != 0) {
		D_ERROR("Failed to register completion cb\n");
		D_GOTO(err_put2, rc);
	}

	tse_task_schedule(update_task, false);
	tse_sched_progress(tse_task2sched(task));

	return rc;

err_put2:
	D_FREE_PTR(update_task);
err_put1:
	tse_task_complete(open_task, rc);
	return rc;
}

static int
open_handle_cb(tse_task_t *task, void *data)
{
	daos_array_open_t	*args = *((daos_array_open_t **)data);
	struct dac_array	*array;
	int64_t			*magic_val;
	int			rc = task->dt_result;

	if (rc != 0)
		D_GOTO(err_obj, rc);

	/** Check magic value */
	magic_val = daos_task_get_priv(task);
	D_ASSERT(magic_val != NULL);
	if (*magic_val != AKEY_MAGIC_V) {
		D_FREE(magic_val);
		D_ERROR("DAOS Object is not an array object\n");
		D_GOTO(err_obj, rc = -DER_NO_PERM);
	}

	/** If no cell and chunk size, this isn't an array obj. */
	if (*args->cell_size == 0 || *args->chunk_size == 0) {
		D_ERROR("Failed to retrieve array metadata\n");
		D_GOTO(err_obj, rc = -DER_NO_PERM);
	}

	/** Create an array OH from the DAOS one */
	array = array_alloc();
	if (array == NULL)
		D_GOTO(err_obj, rc = -DER_NOMEM);

	array->coh = args->coh;
	array->oid.hi = args->oid.hi;
	array->oid.lo = args->oid.lo;
	array->mode = args->mode;
	array->cell_size = *args->cell_size;
	array->chunk_size = *args->chunk_size;
	array->daos_oh = *args->oh;

	*args->oh = array_ptr2hdl(array);

	D_FREE(magic_val);
	return 0;

err_obj:
	{
		daos_obj_close_t *close_args;
		tse_task_t	 *close_task;

		daos_task_create(DAOS_OPC_OBJ_CLOSE, tse_task2sched(task),
				 0, NULL, &close_task);
		close_args = daos_task_get_args(close_task);
		close_args->oh = *args->oh;
		return rc;
	}
}

static int
fetch_md_cb(tse_task_t *task, void *data)
{
	daos_array_open_t *args = *((daos_array_open_t **)data);
	daos_obj_fetch_t *fetch_args;
	struct md_params *params;
	uint64_t *magic_val;
	int rc = task->dt_result;

	if (rc != 0) {
		D_ERROR("Failed to open object (%d)\n", rc);
		return rc;
	}

	D_ALLOC_PTR(params);
	if (params == NULL) {
		D_ERROR("Failed memory allocation\n");
		return -DER_NOMEM;
	}

	/** read metadata from DKEY 0 */
	params->dkey_str = "0";
	daos_iov_set(&params->dkey, params->dkey_str, strlen(params->dkey_str));

	/** set SGL */
	magic_val = daos_task_get_priv(task);
	D_ASSERT(magic_val != NULL);
	daos_iov_set(&params->sg_iovs[0], magic_val, sizeof(uint64_t));
	daos_iov_set(&params->sg_iovs[1], args->cell_size, sizeof(daos_size_t));
	daos_iov_set(&params->sg_iovs[2], args->chunk_size,
		     sizeof(daos_size_t));
	params->sgl.sg_nr = 3;
	params->sgl.sg_nr_out = 0;
	params->sgl.sg_iovs = params->sg_iovs;

	/** set IOD */
	params->akey_str = ARRAY_MD_KEY;
	daos_iov_set(&params->iod.iod_name, (void *)params->akey_str,
		     strlen(params->akey_str));
	daos_csum_set(&params->iod.iod_kcsum, NULL, 0);
	params->iod.iod_nr	= 1;
	params->iod.iod_size	= sizeof(daos_size_t);
	params->recx.rx_idx	= 0;
	params->recx.rx_nr	= 3;
	params->iod.iod_recxs	= &params->recx;
	params->iod.iod_eprs	= NULL;
	params->iod.iod_csums	= NULL;
	params->iod.iod_type	= DAOS_IOD_ARRAY;

	/** Set the args for the fetch task */
	fetch_args = daos_task_get_args(task);
	fetch_args->oh = *args->oh;
	fetch_args->epoch = args->epoch;
	fetch_args->dkey = &params->dkey;
	fetch_args->nr = 1;
	fetch_args->iods = &params->iod;
	fetch_args->sgls = &params->sgl;

	rc = tse_task_register_comp_cb(task, free_md_params_cb, &params,
				       sizeof(params));
	if (rc != 0)
		return rc;

	return 0;
}

int
dac_array_open(tse_task_t *task)
{
	daos_array_open_t	*args = daos_task_get_args(task);
	tse_task_t		*open_task, *fetch_task;
	daos_obj_open_t		*open_args;
	uint64_t		*magic_val;
	int			rc;

	/** Create task to open object */
	rc = daos_task_create(DAOS_OPC_OBJ_OPEN, tse_task2sched(task),
			      0, NULL, &open_task);
	if (rc != 0) {
		D_ERROR("Failed to open object_open task\n");
		return rc;
	}

	open_args = daos_task_get_args(open_task);
	open_args->coh	 = args->coh;
	open_args->oid	 = args->oid;
	open_args->epoch = args->epoch;
	open_args->mode	 = args->mode;
	open_args->oh	 = args->oh;

	tse_task_schedule(open_task, false);

	/** Create task to fetch object metadata */
	rc = daos_task_create(DAOS_OPC_OBJ_FETCH, tse_task2sched(task),
			      1, &open_task, &fetch_task);
	if (rc != 0) {
		D_ERROR("Failed to open object_fetch task\n");
		D_GOTO(err_put1, rc);
	}

	/** add a prepare CB to set the args for the metadata fetch */
	rc = tse_task_register_cbs(fetch_task, fetch_md_cb, &args,
				   sizeof(args), NULL, NULL, 0);
	if (rc != 0) {
		D_ERROR("Failed to register prep CB\n");
		D_GOTO(err_put2, rc);
	}

	/** The upper task completes when the fetch task completes */
	rc = tse_task_register_deps(task, 1, &fetch_task);
	if (rc != 0) {
		D_ERROR("Failed to register dependency\n");
		D_GOTO(err_put2, rc);
	}

	/** Add a completion CB on the upper task to generate the array OH */
	rc = tse_task_register_cbs(task, NULL, NULL, 0, open_handle_cb,
				   &args, sizeof(args));
	if (rc != 0) {
		D_ERROR("Failed to register completion cb\n");
		D_GOTO(err_put2, rc);
	}

	D_ALLOC(magic_val, sizeof(uint64_t));
	if (magic_val == NULL)
		D_GOTO(err_put2, rc);
	daos_task_set_priv(fetch_task, magic_val);
	daos_task_set_priv(task, magic_val);

	tse_task_schedule(fetch_task, false);

	tse_sched_progress(tse_task2sched(task));

	return rc;

err_put2:
	D_FREE_PTR(fetch_task);
err_put1:
	tse_task_complete(open_task, rc);
	return rc;
}

int
dac_array_close(tse_task_t *task)
{
	daos_array_close_t	*args = daos_task_get_args(task);
	struct dac_array	*array;
	tse_task_t		*close_task;
	daos_obj_close_t	*close_args;
	int			rc;

	array = array_hdl2ptr(args->oh);
	if (array == NULL)
		return -DER_NO_HDL;

	/** Create task to close object */
	rc = daos_task_create(DAOS_OPC_OBJ_CLOSE, tse_task2sched(task),
			      0, NULL, &close_task);
	if (rc != 0) {
		D_ERROR("Failed to create object_close task\n");
		D_GOTO(out, rc);
	}
	close_args = daos_task_get_args(close_task);
	close_args->oh = array->daos_oh;

	/** The upper task completes when the close task completes */
	rc = tse_task_register_deps(task, 1, &close_task);
	if (rc != 0) {
		D_ERROR("Failed to register dependency\n");
		D_GOTO(err, rc);
	}

	/** Add a completion CB on the upper task to free the array */
	rc = tse_task_register_cbs(task, NULL, NULL, 0, free_handle_cb,
				    &args->oh, sizeof(args->oh));
	if (rc != 0) {
		D_ERROR("Failed to register completion cb\n");
		D_GOTO(err, rc);
	}

	tse_task_schedule(close_task, false);
	tse_sched_progress(tse_task2sched(task));

out:
	array_decref(array);
	return rc;
err:
	D_FREE_PTR(close_task);
	goto out;
}

int
dac_array_destroy(tse_task_t *task)
{
	daos_array_destroy_t	*args = daos_task_get_args(task);
	struct dac_array	*array;
	tse_task_t		*punch_task;
	daos_obj_punch_t	*punch_args;
	int			rc;

	array = array_hdl2ptr(args->oh);
	if (array == NULL)
		return -DER_NO_HDL;

	/** Create task to punch object */
	rc = daos_task_create(DAOS_OPC_OBJ_PUNCH, tse_task2sched(task),
			      0, NULL, &punch_task);
	if (rc != 0) {
		D_ERROR("Failed to create object_punch task\n");
		D_GOTO(out, rc);
	}
	punch_args = daos_task_get_args(punch_task);
	punch_args->oh		= array->daos_oh;
	punch_args->epoch	= args->epoch;
	punch_args->dkey	= NULL;
	punch_args->akeys	= NULL;
	punch_args->akey_nr	= 0;

	/** The upper task completes when the punch task completes */
	rc = tse_task_register_deps(task, 1, &punch_task);
	if (rc != 0) {
		D_ERROR("Failed to register dependency\n");
		D_GOTO(err, rc);
	}

	tse_task_schedule(punch_task, false);
	tse_sched_progress(tse_task2sched(task));

out:
	array_decref(array);
	return rc;
err:
	D_FREE_PTR(punch_task);
	goto out;
}

static bool
io_extent_same(daos_array_iod_t *iod, daos_sg_list_t *sgl,
	       daos_size_t cell_size)
{
	daos_size_t rgs_len;
	daos_size_t sgl_len;
	daos_size_t u;

	rgs_len = 0;

	D_DEBUG(DB_IO, "USER ARRAY RANGE -----------------------\n");
	D_DEBUG(DB_IO, "Array IOD nr = %zu\n", iod->arr_nr);

	for (u = 0 ; u < iod->arr_nr ; u++) {
		rgs_len += iod->arr_rgs[u].rg_len;
		D_DEBUG(DB_IO, "%zu: length %zu, index %d\n",
			u, iod->arr_rgs[u].rg_len,
			(int)iod->arr_rgs[u].rg_idx);
	}

	D_DEBUG(DB_IO, "------------------------------------\n");
	D_DEBUG(DB_IO, "USER SGL -----------------------\n");
	D_DEBUG(DB_IO, "sg_nr = %u\n", sgl->sg_nr);

	sgl_len = 0;
	for (u = 0 ; u < sgl->sg_nr; u++) {
		sgl_len += sgl->sg_iovs[u].iov_len;
		D_DEBUG(DB_IO, "%zu: length %zu, Buf %p\n", u,
			sgl->sg_iovs[u].iov_len, sgl->sg_iovs[u].iov_buf);
	}

	return (rgs_len * cell_size == sgl_len);
}

/*
 * Compute the dkey given the array index for this range. Also compute: - the
 * number of records that the dkey can hold starting at the index where we start
 * writing. - the record index relative to the dkey.
 */
static int
compute_dkey(struct dac_array *array, daos_off_t array_idx,
	     daos_size_t *num_records, daos_off_t *record_i, char **dkey_str)
{
	daos_size_t	dkey_num;	/* dkey number */
	daos_off_t	dkey_i;		/* Logical Start IDX of dkey_num */

	/* Compute dkey number and starting index relative to the array */
	dkey_num = array_idx / array->chunk_size;
	dkey_i = dkey_num * array->chunk_size;

	if (record_i)
		*record_i = array_idx - dkey_i;
	if (num_records)
		*num_records = array->chunk_size - *record_i;

	if (dkey_str) {
		int ret;

		ret = asprintf(dkey_str, "%zu", dkey_num);
		if (ret < 0 || *dkey_str == NULL) {
			D_ERROR("Failed memory allocation\n");
			return -DER_NOMEM;
		}
	}

	return 0;
}

static int
create_sgl(daos_sg_list_t *user_sgl, daos_size_t cell_size,
	   daos_size_t num_records, daos_off_t *sgl_off, daos_size_t *sgl_i,
	   daos_sg_list_t *sgl)
{
	daos_size_t	k;
	daos_size_t	rem_records;
	daos_size_t	cur_i;
	daos_off_t	cur_off;

	cur_i = *sgl_i;
	cur_off = *sgl_off;
	sgl->sg_nr = k = 0;
	sgl->sg_iovs = NULL;
	rem_records = num_records;

	/*
	 * Keep iterating through the user sgl till we populate our sgl to
	 * satisfy the number of records to read/write from the KV object
	 */
	do {
		D_ASSERT(user_sgl->sg_nr > cur_i);

		sgl->sg_nr++;
		sgl->sg_iovs = (daos_iov_t *)realloc
			(sgl->sg_iovs, sizeof(daos_iov_t) * sgl->sg_nr);
		if (sgl->sg_iovs == NULL) {
			D_ERROR("Failed memory allocation\n");
			return -DER_NOMEM;
		}

		sgl->sg_iovs[k].iov_buf = user_sgl->sg_iovs[cur_i].iov_buf +
			cur_off;

		if (rem_records * cell_size >=
		    (user_sgl->sg_iovs[cur_i].iov_len - cur_off)) {
			sgl->sg_iovs[k].iov_len =
				user_sgl->sg_iovs[cur_i].iov_len - cur_off;
			cur_i++;
			cur_off = 0;
		} else {
			sgl->sg_iovs[k].iov_len = rem_records * cell_size;
			cur_off += rem_records * cell_size;
		}

		sgl->sg_iovs[k].iov_buf_len = sgl->sg_iovs[k].iov_len;
		rem_records -= sgl->sg_iovs[k].iov_len / cell_size;

		k++;
	} while (rem_records && user_sgl->sg_nr > cur_i);

	sgl->sg_nr_out = 0;

	*sgl_i = cur_i;
	*sgl_off = cur_off;

	return 0;
}

static int
dac_array_io(daos_handle_t array_oh, daos_epoch_t epoch,
	     daos_array_iod_t *rg_iod, daos_sg_list_t *user_sgl,
	     daos_opc_t op_type, tse_task_t *task)
{
	struct dac_array *array = NULL;
	daos_handle_t	oh;
	daos_off_t	cur_off; /* offset into user buf to track current pos */
	daos_size_t	cur_i; /* index into user sgl to track current pos */
	daos_size_t	records; /* Number of records to access in cur range */
	daos_off_t	array_idx; /* object array index of current range */
	daos_size_t	u; /* index in the array range rg_iod->arr_nr*/
	daos_size_t	num_records;
	daos_off_t	record_i;
	daos_csum_buf_t	null_csum;
	struct io_params *head, *current;
	daos_size_t	num_ios;
	int		rc;

	if (rg_iod == NULL) {
		D_ERROR("NULL iod passed\n");
		D_GOTO(err_task, rc = -DER_INVAL);
	}

	array = array_hdl2ptr(array_oh);
	if (array == NULL)
		return -DER_NO_HDL;

	if (op_type == DAOS_OPC_ARRAY_PUNCH) {
		D_ASSERT(user_sgl == NULL);
	} else if (user_sgl == NULL) {
		D_ERROR("NULL scatter-gather list passed\n");
		D_GOTO(err_task, rc = -DER_INVAL);
	} else if (!io_extent_same(rg_iod, user_sgl, array->cell_size)) {
		D_ERROR("Unequal extents of memory and array descriptors\n");
		D_GOTO(err_task, rc = -DER_INVAL);
	}

	oh = array->daos_oh;

	cur_off = 0;
	cur_i = 0;
	u = 0;
	num_ios = 0;
	records = rg_iod->arr_rgs[0].rg_len;
	array_idx = rg_iod->arr_rgs[0].rg_idx;
	daos_csum_set(&null_csum, NULL, 0);

	head = NULL;

	/*
	 * Loop over every range, but at the same time combine consecutive
	 * ranges that belong to the same dkey. If the user gives ranges that
	 * are not increasing in offset, they probably won't be combined unless
	 * the separating ranges also belong to the same dkey.
	 */
	while (u < rg_iod->arr_nr) {
		daos_iod_t	*iod;
		daos_sg_list_t	*sgl;
		char		*dkey_str;
		daos_key_t	*dkey;
		daos_size_t	dkey_records;
		tse_task_t	*io_task = NULL;
		struct io_params *params;
		daos_size_t	i; /* index for iod recx */

		/** In some cases, users can pass an empty range, so skip it. */
		if (rg_iod->arr_rgs[u].rg_len == 0) {
			u++;
			if (u < rg_iod->arr_nr) {
				records = rg_iod->arr_rgs[u].rg_len;
				array_idx = rg_iod->arr_rgs[u].rg_idx;
			}
			continue;
		}

		/** allocate params for this dkey io */
		D_ALLOC_PTR(params);
		if (params == NULL) {
			D_ERROR("Failed memory allocation\n");
			D_GOTO(err_task, rc = -DER_NOMEM);
		}

		/*
		 * since we probably have multiple dkey ios, put them in linked
		 * list to free later.
		 */
		if (num_ios == 0) {
			head = params;
			current = head;
			tse_task_register_comp_cb(task, free_io_params_cb,
						  &head, sizeof(head));
		} else {
			current->next = params;
			current = params;
		}

		iod = &params->iod;
		sgl = &params->sgl;
		dkey = &params->dkey;
		params->akey_str = '0';
		params->user_sgl_used = false;

		num_ios++;

		rc = compute_dkey(array, array_idx, &num_records, &record_i,
				  &params->dkey_str);
		if (rc != 0) {
			D_ERROR("Failed to compute dkey\n");
			D_GOTO(err_task, rc);
		}
		dkey_str = params->dkey_str;

		D_DEBUG(DB_IO, "DKEY IOD %s ---------------------------\n",
			dkey_str);
		D_DEBUG(DB_IO, "idx = %d\t num_records = %zu\t record_i = %d\n",
			(int)array_idx, num_records, (int)record_i);

		daos_iov_set(dkey, (void *)dkey_str, strlen(dkey_str));

		/* set descriptor for KV object */
		daos_iov_set(&iod->iod_name, &params->akey_str, 1);
		iod->iod_kcsum = null_csum;
		iod->iod_nr = 0;
		iod->iod_csums = NULL;
		iod->iod_eprs = NULL;
		iod->iod_recxs = NULL;
		iod->iod_type = DAOS_IOD_ARRAY;
		if (op_type == DAOS_OPC_ARRAY_PUNCH)
			iod->iod_size = 0;
		else
			iod->iod_size = array->cell_size;

		i = 0;
		dkey_records = 0;

		/*
		 * Create the IO descriptor for this dkey. If the entire range
		 * fits in the dkey, continue to the next range to see if we can
		 * combine it fully or partially in the current dkey IOD.
		 */
		do {
			daos_off_t	old_array_idx;

			iod->iod_nr++;

			/** add another element to recxs */
			iod->iod_recxs = (daos_recx_t *)realloc
				(iod->iod_recxs, sizeof(daos_recx_t) *
				 iod->iod_nr);
			if (iod->iod_recxs == NULL) {
				D_ERROR("Failed memory allocation\n");
				D_GOTO(err_task, rc = -DER_NOMEM);
			}

			/** set the record access for this range */
			iod->iod_recxs[i].rx_idx = record_i;
			iod->iod_recxs[i].rx_nr = (num_records > records) ?
				records : num_records;

			D_DEBUG(DB_IO, "%zu: index = %"PRIu64", size = %zu\n",
				u, iod->iod_recxs[i].rx_idx,
				iod->iod_recxs[i].rx_nr);

			/*
			 * if the current range is bigger than what the dkey can
			 * hold, update the array index and number of records in
			 * the current range and break to issue the I/O on the
			 * current dkey.
			 */
			if (records > num_records) {
				array_idx += num_records;
				records -= num_records;
				dkey_records += num_records;
				break;
			}

			/** bump the index for the iods */
			u++;
			i++;
			dkey_records += records;

			/** if there are no more ranges to write, then break */
			if (rg_iod->arr_nr <= u)
				break;

			old_array_idx = array_idx;
			records = rg_iod->arr_rgs[u].rg_len;
			array_idx = rg_iod->arr_rgs[u].rg_idx;

			/*
			 * Boundary case where number of records align with the
			 * end boundary of the dkey. break after we have
			 * advanced to the next range in the array iod.
			 */
			if (records == num_records)
				break;

			/** process the next range in the cur dkey */
			if (array_idx < old_array_idx + num_records &&
			    array_idx >= ((old_array_idx + num_records) -
					  array->chunk_size)) {
				char	*dkey_str_tmp = NULL;

				/*
				 * verify that the dkey is the same as the one
				 * we are working on given the array index, and
				 * also compute the number of records left in
				 * the dkey and the record indexin the dkey.
				 */
				rc = compute_dkey(array, array_idx,
						  &num_records, &record_i,
						  &dkey_str_tmp);
				if (rc != 0) {
					D_ERROR("Failed to compute dkey\n");
					D_GOTO(err_task, rc);
				}

				D_ASSERT(strcmp(dkey_str_tmp, dkey_str) == 0);

				free(dkey_str_tmp);
				dkey_str_tmp = NULL;
			} else {
				break;
			}
		} while (1);

		D_DEBUG(DB_IO, "END DKEY IOD %s ---------------------------\n",
			dkey_str);

		/*
		 * if the user sgl maps directly to the array range, no need to
		 * partition it.
		 */
		if ((op_type == DAOS_OPC_ARRAY_PUNCH) ||
		    (1 == rg_iod->arr_nr && 1 == user_sgl->sg_nr &&
		     dkey_records == rg_iod->arr_rgs[0].rg_len)) {
			sgl = user_sgl;
			params->user_sgl_used = true;
		}
		/** create an sgl from the user sgl for the current IOD */
		else {
			daos_size_t s;

			/* set sgl for current dkey */
			rc = create_sgl(user_sgl, array->cell_size,
					dkey_records, &cur_off, &cur_i, sgl);
			if (rc != 0) {
				D_ERROR("Failed to create sgl\n");
				D_GOTO(err_task, rc);
			}

			D_DEBUG(DB_IO, "DKEY SGL -----------------------\n");
			D_DEBUG(DB_IO, "sg_nr = %u\n", sgl->sg_nr);
			for (s = 0; s < sgl->sg_nr; s++) {
				D_DEBUG(DB_IO, "%zu: length %zu, Buf %p\n",
					s, sgl->sg_iovs[s].iov_len,
					sgl->sg_iovs[s].iov_buf);
			}
			D_DEBUG(DB_IO, "--------------------------------\n");
		}

		/* issue IO to DAOS */
		if (op_type == DAOS_OPC_ARRAY_READ) {
			daos_obj_fetch_t *io_arg;

			rc = daos_task_create(DAOS_OPC_OBJ_FETCH,
					      tse_task2sched(task),
					      0, NULL, &io_task);
			if (rc != 0) {
				D_ERROR("KV Fetch of dkey %s failed (%d)\n",
					dkey_str, rc);
				D_GOTO(err_task, rc);
			}
			io_arg = daos_task_get_args(io_task);
			io_arg->oh	= oh;
			io_arg->epoch	= epoch;
			io_arg->dkey	= dkey;
			io_arg->nr	= 1;
			io_arg->iods	= iod;
			io_arg->sgls	= sgl;
			io_arg->maps	= NULL;
		} else if (op_type == DAOS_OPC_ARRAY_WRITE ||
			   op_type == DAOS_OPC_ARRAY_PUNCH) {
			daos_obj_update_t *io_arg;

			rc = daos_task_create(DAOS_OPC_OBJ_UPDATE,
					      tse_task2sched(task),
					      0, NULL, &io_task);
			if (rc != 0) {
				D_ERROR("KV Update of dkey %s failed (%d)\n",
					dkey_str, rc);
				D_GOTO(err_task, rc);
			}
			io_arg = daos_task_get_args(io_task);
			io_arg->oh	= oh;
			io_arg->epoch	= epoch;
			io_arg->dkey	= dkey;
			io_arg->nr	= 1;
			io_arg->iods	= iod;
			io_arg->sgls	= sgl;
		} else {
			D_ASSERTF(0, "Invalid array operation.\n");
		}

		tse_task_register_deps(task, 1, &io_task);
		tse_task_schedule(io_task, false);
	} /* end while */

	array_decref(array);
	tse_sched_progress(tse_task2sched(task));
	return 0;

err_task:
	if (array)
		array_decref(array);
	tse_task_complete(task, rc);
	return rc;
}

int
dac_array_read(tse_task_t *task)
{
	daos_array_io_t *args = daos_task_get_args(task);

	return dac_array_io(args->oh, args->epoch, args->iod, args->sgl,
			    DAOS_OPC_ARRAY_READ, task);
}

int
dac_array_write(tse_task_t *task)
{
	daos_array_io_t *args = daos_task_get_args(task);

	return dac_array_io(args->oh, args->epoch, args->iod, args->sgl,
			    DAOS_OPC_ARRAY_WRITE, task);
}

int
dac_array_punch(tse_task_t *task)
{
	daos_array_io_t *args = daos_task_get_args(task);

	return dac_array_io(args->oh, args->epoch, args->iod, NULL,
			    DAOS_OPC_ARRAY_PUNCH, task);
}

#define ENUM_KEY_BUF	32
#define ENUM_DESC_BUF	512
#define ENUM_DESC_NR	5

struct get_size_props {
	struct dac_array	*array;
	char			key[ENUM_DESC_BUF];
	char			buf[ENUM_DESC_BUF];
	daos_key_desc_t		kds[ENUM_DESC_NR];
	daos_iov_t		iov;
	daos_sg_list_t		sgl;
	uint32_t		nr;
	bool			found_dkey;
	daos_anchor_t		anchor;
	daos_size_t		dkey_num;
	daos_size_t		*size;
	tse_task_t		*ptask;
};

struct list_recxs_params {
	daos_key_t		dkey;
	char			*dkey_str;
	daos_key_t		akey;
	char			akey_str;
	daos_recx_t		recx;
	uint32_t		nr;
	daos_size_t		cell_size;
	daos_size_t		chunk_size;
	daos_anchor_t		anchor;
	daos_size_t		*size;
	tse_task_t		*task;
};

static int
free_get_size_cb(tse_task_t *task, void *data)
{
	struct get_size_props *props = *((struct get_size_props **)data);

	if (props->array)
		array_decref(props->array);
	D_FREE_PTR(props);
	return 0;
}

static int
list_recxs_cb(tse_task_t *task, void *data)
{
	daos_obj_list_recx_t *args = daos_task_get_args(task);
	struct list_recxs_params *params = *((struct list_recxs_params **)data);
	daos_size_t dkey_num;
	int ret;
	int rc = task->dt_result;
	daos_size_t cur_size;

	ret = sscanf(params->dkey_str, "%zu", &dkey_num);
	D_ASSERT(ret == 1);
	cur_size = dkey_num * params->chunk_size + params->recx.rx_idx +
		params->recx.rx_nr;
	if (*params->size < cur_size)
		*params->size = cur_size;

	/** if enumeration is not done, re-init this task to continue */
	if (!daos_anchor_is_eof(args->anchor)) {
		params->nr = 1;
		rc = tse_task_reinit(task);
		if (rc != 0) {
			D_ERROR("FAILED to reinit task\n");
			D_GOTO(out, rc);
		}

		rc = tse_task_register_cbs(task, NULL, NULL, 0, list_recxs_cb,
					   &params, sizeof(params));
		if (rc) {
			tse_task_complete(task, rc);
			D_GOTO(out, rc);
		}

		return rc;
	}

out:
	if (params->dkey_str) {
		free(params->dkey_str);
		params->dkey_str = NULL;
	}
	D_FREE_PTR(params);

	return rc;
}

static int
get_array_size_cb(tse_task_t *task, void *data)
{
	daos_obj_list_dkey_t *args = daos_task_get_args(task);
	struct get_size_props *props = *((struct get_size_props **)data);
	struct dac_array *array = props->array;
	char		*ptr;
	uint32_t	i;
	int		rc = task->dt_result;

	if (rc != 0) {
		D_ERROR("Array DKEY enumermation Failed (%d)\n", rc);
		return rc;
	}

	/** track the highest dkey from the ones currently enumerated */
	for (ptr = props->buf, i = 0; i < props->nr; i++) {
		daos_size_t dkey_num;
		int ret;

		snprintf(props->key, args->kds[i].kd_key_len + 1, "%s", ptr);
		ptr += args->kds[i].kd_key_len;

		if (!strcmp(ARRAY_MD_KEY, props->key))
			continue;

		props->found_dkey = true;
		/** Keep a record of the highest dkey */
		ret = sscanf(props->key, "%zu", &dkey_num);
		D_ASSERT(ret == 1);

		if (dkey_num > props->dkey_num)
			props->dkey_num = dkey_num;
	}

	/** if enumeration is not done, re-init this task to continue */
	if (!daos_anchor_is_eof(args->anchor)) {
		props->nr = ENUM_DESC_NR;
		memset(props->buf, 0, ENUM_DESC_BUF);
		args->sgl->sg_nr = 1;
		daos_iov_set(&args->sgl->sg_iovs[0], props->buf, ENUM_DESC_BUF);

		rc = tse_task_reinit(task);
		if (rc != 0) {
			D_ERROR("FAILED to continue enumrating task\n");
			return rc;
		}

		rc = tse_task_register_cbs(task, NULL, NULL, 0,
					   get_array_size_cb, &props,
					   sizeof(props));
		if (rc) {
			tse_task_complete(task, rc);
			return rc;
		}

		return rc;
	}

	if (!props->found_dkey)
		return 0;

	char key[ENUM_KEY_BUF];

	sprintf(key, "%zu", props->dkey_num);

	/** retrieve the highest index from the highest key */
	props->nr = ENUM_DESC_NR;

	tse_task_t *io_task = NULL;
	struct list_recxs_params *params = NULL;
	daos_key_t *dkey, *akey;
	daos_obj_list_recx_t *list_args;

	D_ALLOC_PTR(params);
	if (params == NULL) {
		D_ERROR("Failed memory allocation\n");
		return -DER_NOMEM;
	}

	akey = &params->akey;
	dkey = &params->dkey;

	params->akey_str = '0';
	params->dkey_str = strdup(key);
	if (params->dkey_str == NULL)
		D_GOTO(err, rc = -DER_NOMEM);
	daos_iov_set(dkey, (void *)params->dkey_str, strlen(params->dkey_str));
	daos_iov_set(akey, &params->akey_str, 1);
	params->nr = 1;
	params->chunk_size = array->chunk_size;
	params->size = props->size;

	rc = daos_task_create(DAOS_OPC_OBJ_LIST_RECX, tse_task2sched(task),
			      0, NULL, &io_task);
	if (rc != 0) {
		D_ERROR("punch recs failed (%d)\n", rc);
		D_GOTO(err, rc);
	}

	list_args = daos_task_get_args(io_task);
	list_args->oh		= args->oh;
	list_args->epoch	= args->epoch;
	list_args->dkey		= dkey;
	list_args->akey		= akey;
	list_args->type		= DAOS_IOD_ARRAY;
	list_args->size		= &params->cell_size;
	list_args->nr		= &params->nr;
	list_args->recxs	= &params->recx;
	list_args->eprs		= NULL;
	list_args->incr_order	= false;
	list_args->versions	= NULL;
	list_args->anchor	= &params->anchor;
	memset(&params->anchor, 0, sizeof(params->anchor));

	rc = tse_task_register_comp_cb(io_task, list_recxs_cb, &params,
				       sizeof(params));
	if (rc != 0)
		D_GOTO(err, rc);

	rc = tse_task_register_deps(props->ptask, 1, &io_task);
	if (rc != 0)
		D_GOTO(err, rc);

	tse_task_schedule(io_task, false);
	return rc;

err:
	if (params->dkey_str)
		free(params->dkey_str);
	if (io_task)
		tse_task_complete(io_task, rc);
	D_FREE_PTR(params);
	return rc;
}

int
dac_array_get_size(tse_task_t *task)
{
	daos_array_get_size_t	*args = daos_task_get_args(task);
	struct dac_array	*array;
	daos_obj_list_dkey_t	*enum_args;
	struct get_size_props	*get_size_props = NULL;
	tse_task_t		*enum_task = NULL;
	daos_handle_t		oh;
	int			rc;

	array = array_hdl2ptr(args->oh);
	if (array == NULL)
		D_GOTO(err_task, rc = -DER_NO_HDL);

	oh = array->daos_oh;

	D_ALLOC_PTR(get_size_props);
	if (get_size_props == NULL)
		D_GOTO(err_task, rc = -DER_NOMEM);

	*args->size = 0;

	/** List all DKEYS to determine the highest dkey */
	get_size_props->dkey_num = 0;
	get_size_props->found_dkey = false;
	get_size_props->nr = ENUM_DESC_NR;
	get_size_props->ptask = task;
	get_size_props->size = args->size;
	get_size_props->array = array;
	memset(get_size_props->buf, 0, ENUM_DESC_BUF);
	memset(&get_size_props->anchor, 0, sizeof(get_size_props->anchor));
	get_size_props->sgl.sg_nr = 1;
	get_size_props->sgl.sg_iovs = &get_size_props->iov;
	daos_iov_set(&get_size_props->sgl.sg_iovs[0], get_size_props->buf,
		     ENUM_DESC_BUF);

	rc = daos_task_create(DAOS_OPC_OBJ_LIST_DKEY, tse_task2sched(task),
			      0, NULL, &enum_task);
	if (rc != 0)
		D_GOTO(err_task, rc);

	enum_args	  = daos_task_get_args(enum_task);
	enum_args->oh	  = oh;
	enum_args->epoch  = args->epoch;
	enum_args->nr	  = &get_size_props->nr;
	enum_args->kds	  = get_size_props->kds;
	enum_args->sgl	  = &get_size_props->sgl;
	enum_args->anchor = &get_size_props->anchor;

	rc = tse_task_register_cbs(enum_task, NULL, NULL, 0, get_array_size_cb,
				   &get_size_props, sizeof(get_size_props));
	if (rc != 0) {
		D_ERROR("Failed to register completion cb\n");
		D_GOTO(err_enum_task, rc);
	}

	rc = tse_task_register_deps(task, 1, &enum_task);
	if (rc != 0) {
		D_ERROR("Failed to register dependency\n");
		D_GOTO(err_enum_task, rc);
	}

	rc = tse_task_register_comp_cb(task, free_get_size_cb, &get_size_props,
				       sizeof(get_size_props));
	if (rc != 0)
		D_GOTO(err_enum_task, rc);

	rc = tse_task_schedule(enum_task, false);
	if (rc != 0)
		D_GOTO(err_enum_task, rc);

	tse_sched_progress(tse_task2sched(task));

	return 0;

err_enum_task:
	tse_task_complete(enum_task, rc);
err_task:
	if (get_size_props)
		D_FREE_PTR(get_size_props);
	if (enum_task)
		D_FREE_PTR(enum_task);
	if (array)
		array_decref(array);
	tse_task_complete(task, rc);
	return rc;
} /* end daos_array_get_size */

struct set_size_props {
	struct dac_array *array;
	char		key[ENUM_DESC_BUF];
	char		buf[ENUM_DESC_BUF];
	daos_key_desc_t kds[ENUM_DESC_NR];
	char		*val;
	daos_iov_t	iov;
	daos_sg_list_t  sgl;
	uint32_t	nr;
	daos_anchor_t	anchor;
	bool		update_dkey;
	daos_size_t	dkey_num;
	daos_size_t	size;
	daos_size_t	cell_size;
	daos_size_t	num_records;
	daos_size_t	chunk_size;
	daos_off_t	record_i;
	tse_task_t	*ptask;
};

static int
free_set_size_cb(tse_task_t *task, void *data)
{
	struct set_size_props *props = *((struct set_size_props **)data);

	if (props->val)
		free(props->val);
	if (props->array)
		array_decref(props->array);
	D_FREE_PTR(props);
	return 0;
}

static int
punch_key(daos_handle_t oh, daos_epoch_t epoch, const char *key, int dkey_num,
	  tse_task_t *task)
{
	daos_obj_punch_t	*p_args;
	daos_key_t		*dkey;
	struct io_params	*params = NULL;
	tse_task_t		*io_task = NULL;
	daos_opc_t		opc = DAOS_OPC_OBJ_PUNCH_DKEYS;
	int			rc;

	D_ALLOC_PTR(params);
	if (params == NULL) {
		D_ERROR("Failed memory allocation\n");
		return -DER_NOMEM;
	}

	params->dkey_str = strdup(key);
	dkey = &params->dkey;
	daos_iov_set(dkey, (void *)params->dkey_str,
		     strlen(params->dkey_str));

	/** Punch this entire dkey */
	D_DEBUG(DB_IO, "Punching Key %s\n", params->dkey_str);

	/*
	 * If this is dkey "0", punch only the akey "0" because
	 * it contains other metadata keys that we don't want to
	 * punch.
	 */
	if (dkey_num == 0)
		opc = DAOS_OPC_OBJ_PUNCH_AKEYS;

	rc = daos_task_create(opc, tse_task2sched(task), 0, NULL, &io_task);
	if (rc) {
		D_ERROR("daos_task_create() failed (%d)\n", rc);
		D_GOTO(err, rc);
	}

	p_args = daos_task_get_args(io_task);
	p_args->oh	= oh;
	p_args->epoch	= epoch;
	p_args->dkey	= dkey;

	/** set akey if we punch akey "0" only */
	if (dkey_num == 0) {
		daos_key_t *akey;

		akey = &params->iod.iod_name;
		params->akey_str = '0';
		daos_iov_set(akey, &params->akey_str, 1);
		p_args->akey_nr = 1;
		p_args->akeys = akey;
	}

	rc = tse_task_register_comp_cb(io_task, free_io_params_cb, &params,
				       sizeof(params));
	if (rc)
		D_GOTO(err, rc);

	rc = tse_task_register_deps(task, 1, &io_task);
	if (rc)
		D_GOTO(err, rc);

	rc = tse_task_schedule(io_task, false);
	if (rc)
		D_GOTO(err, rc);

	return rc;
err:
	if (params) {
		if (params->dkey_str)
			free(params->dkey_str);
		D_FREE_PTR(params);
	}
	if (io_task)
		tse_task_complete(io_task, rc);
	return rc;
}

static int
punch_extent(daos_handle_t oh, daos_epoch_t epoch, const char *key,
	     daos_off_t record_i, daos_size_t num_records, tse_task_t *task)
{
	daos_obj_update_t	*io_arg;
	daos_iod_t		*iod;
	daos_sg_list_t		*sgl;
	daos_key_t		*dkey;
	daos_csum_buf_t		null_csum;
	struct io_params	*params = NULL;
	tse_task_t		*io_task = NULL;
	int			rc;

	D_DEBUG(DB_IO, "Punching (%zu, %zu) in Key %s\n",
		record_i + 1, num_records, key);

	D_ALLOC_PTR(params);
	if (params == NULL) {
		D_ERROR("Failed memory allocation\n");
		return -DER_NOMEM;
	}

	daos_csum_set(&null_csum, NULL, 0);

	iod = &params->iod;
	sgl = NULL;
	dkey = &params->dkey;
	params->akey_str = '0';
	params->user_sgl_used = false;
	params->dkey_str = strdup(key);
	dkey = &params->dkey;
	daos_iov_set(dkey, (void *)params->dkey_str,
		     strlen(params->dkey_str));

	/* set descriptor for KV object */
	daos_iov_set(&iod->iod_name, &params->akey_str, 1);
	iod->iod_kcsum = null_csum;
	iod->iod_nr = 1;
	iod->iod_csums = NULL;
	iod->iod_eprs = NULL;
	iod->iod_size = 0; /* 0 to punch */
	iod->iod_type = DAOS_IOD_ARRAY;
	iod->iod_recxs = malloc(sizeof(daos_recx_t));
	iod->iod_recxs[0].rx_idx = record_i + 1;
	iod->iod_recxs[0].rx_nr = num_records;

	rc = daos_task_create(DAOS_OPC_OBJ_UPDATE, tse_task2sched(task), 0,
			      NULL, &io_task);
	if (rc)
		D_GOTO(err, rc);

	io_arg = daos_task_get_args(io_task);
	io_arg->oh	= oh;
	io_arg->epoch	= epoch;
	io_arg->dkey	= dkey;
	io_arg->nr	= 1;
	io_arg->iods	= iod;
	io_arg->sgls	= sgl;

	rc = tse_task_register_comp_cb(io_task, free_io_params_cb, &params,
				       sizeof(params));
	if (rc)
		D_GOTO(err, rc);

	rc = tse_task_register_deps(task, 1, &io_task);
	if (rc)
		D_GOTO(err, rc);

	rc = tse_task_schedule(io_task, false);
	if (rc)
		D_GOTO(err, rc);

	return rc;
err:
	if (params) {
		if (params->dkey_str)
			free(params->dkey_str);
		D_FREE_PTR(params);
	}
	if (io_task)
		tse_task_complete(io_task, rc);
	return rc;
}

static int
check_record_cb(tse_task_t *task, void *data)
{
	daos_obj_fetch_t	*args = daos_task_get_args(task);
	struct io_params	*params = *((struct io_params **)data);
	daos_obj_update_t	*io_arg;
	tse_task_t		*io_task = NULL;
	daos_iod_t		*iod;
	daos_sg_list_t		*sgl;
	daos_key_t		*dkey;
	char			*val;
	int			rc = task->dt_result;

	/** Last record is there, no need to add it */
	if (rc || params->iod.iod_size != 0)
		D_GOTO(out, rc);

	/** add record with value 0 */
	iod = &params->iod;
	sgl = &params->sgl;
	dkey = &params->dkey;

	/** update the iod size, rest should be already set */
	iod->iod_size = params->cell_size;

	/** set memory location */
	val = calloc(1, params->cell_size);
	sgl->sg_nr = 1;
	sgl->sg_iovs = malloc(sizeof(daos_iov_t));
	daos_iov_set(&sgl->sg_iovs[0], val, params->cell_size);

	rc = daos_task_create(DAOS_OPC_OBJ_UPDATE, tse_task2sched(task), 0,
			      NULL, &io_task);
	if (rc) {
		D_ERROR("Task create failed (%d)\n", rc);
		D_GOTO(out, rc);
	}

	io_arg = daos_task_get_args(io_task);
	io_arg->oh	= args->oh;
	io_arg->epoch	= args->epoch;
	io_arg->dkey	= dkey;
	io_arg->nr	= 1;
	io_arg->iods	= iod;
	io_arg->sgls	= sgl;

	rc = tse_task_register_comp_cb(io_task, free_io_params_cb, &params,
				       sizeof(params));
	if (rc)
		D_GOTO(err_task, rc);

	rc = tse_task_register_comp_cb(io_task, free_val_cb, &val, sizeof(val));
	if (rc)
		D_GOTO(err_task, rc);

	rc = tse_task_register_deps(task, 1, &io_task);
	if (rc)
		D_GOTO(err_task, rc);

	rc = tse_task_schedule(io_task, false);
	if (rc)
		D_GOTO(err_task, rc);

	return rc;

err_task:
	if (io_task)
		tse_task_complete(io_task, rc);
out:
	if (params) {
		if (params->dkey_str)
			free(params->dkey_str);
		D_FREE_PTR(params);
	}
	return rc;
}

static int
check_record(daos_handle_t oh, daos_epoch_t epoch, const char *key,
	     daos_off_t record_i, daos_size_t cell_size, tse_task_t *task)
{
	daos_obj_fetch_t	*io_arg;
	daos_iod_t		*iod;
	daos_sg_list_t		*sgl;
	daos_key_t		*dkey;
	daos_csum_buf_t		null_csum;
	struct io_params	*params = NULL;
	tse_task_t		*io_task = NULL;
	int			rc;

	D_ALLOC_PTR(params);
	if (params == NULL) {
		D_ERROR("Failed memory allocation\n");
		return -DER_NOMEM;
	}

	iod = &params->iod;
	sgl = NULL;
	dkey = &params->dkey;
	params->akey_str = '0';
	params->user_sgl_used = false;
	params->dkey_str = strdup(key);
	params->cell_size = cell_size;
	dkey = &params->dkey;
	daos_iov_set(dkey, (void *)params->dkey_str,
		     strlen(params->dkey_str));

	/* set descriptor for KV object */
	daos_iov_set(&iod->iod_name, &params->akey_str, 1);
	daos_csum_set(&null_csum, NULL, 0);
	iod->iod_kcsum = null_csum;
	iod->iod_nr = 1;
	iod->iod_csums = NULL;
	iod->iod_eprs = NULL;
	iod->iod_size = DAOS_REC_ANY;
	iod->iod_type = DAOS_IOD_ARRAY;
	iod->iod_recxs = malloc(sizeof(daos_recx_t));
	iod->iod_recxs[0].rx_idx = record_i;
	iod->iod_recxs[0].rx_nr = 1;

	rc = daos_task_create(DAOS_OPC_OBJ_FETCH, tse_task2sched(task), 0, NULL,
			      &io_task);
	if (rc) {
		D_ERROR("Task create failed (%d)\n", rc);
		D_GOTO(err, rc);
	}

	io_arg = daos_task_get_args(io_task);
	io_arg->oh	= oh;
	io_arg->epoch	= epoch;
	io_arg->dkey	= dkey;
	io_arg->nr	= 1;
	io_arg->iods	= iod;
	io_arg->sgls	= sgl;
	io_arg->maps	= NULL;

	rc = tse_task_register_comp_cb(io_task, check_record_cb, &params,
				       sizeof(params));
	if (rc)
		D_GOTO(err, rc);

	rc = tse_task_register_deps(task, 1, &io_task);
	if (rc)
		D_GOTO(err, rc);

	rc = tse_task_schedule(io_task, false);
	if (rc)
		D_GOTO(err, rc);

	return rc;
err:
	if (params) {
		if (params->dkey_str)
			free(params->dkey_str);
		D_FREE_PTR(params);
	}
	if (io_task)
		tse_task_complete(io_task, rc);
	return rc;
}


static int
add_record(daos_handle_t oh, daos_epoch_t epoch, struct set_size_props *props)
{
	daos_obj_update_t	*io_arg;
	daos_iod_t		*iod;
	daos_sg_list_t		*sgl;
	daos_key_t		*dkey;
	daos_csum_buf_t		null_csum;
	struct io_params	*params = NULL;
	tse_task_t		*io_task = NULL;
	int			rc;

	daos_csum_set(&null_csum, NULL, 0);

	D_ALLOC_PTR(params);
	if (params == NULL) {
		D_ERROR("Failed memory allocation\n");
		return -DER_NOMEM;
	}

	iod = &params->iod;
	sgl = &params->sgl;
	dkey = &params->dkey;
	params->akey_str = '0';
	params->next = NULL;
	params->user_sgl_used = false;

	rc = asprintf(&params->dkey_str, "%zu", props->dkey_num);
	if (rc < 0 || params->dkey_str == NULL) {
		D_ERROR("Failed memory allocation\n");
		D_GOTO(err, -DER_NOMEM);
	}

	daos_iov_set(dkey, (void *)params->dkey_str, strlen(params->dkey_str));

	/** set memory location */
	props->val = calloc(1, props->cell_size);
	sgl->sg_nr = 1;
	sgl->sg_iovs = malloc(sizeof(daos_iov_t));
	daos_iov_set(&sgl->sg_iovs[0], props->val, props->cell_size);

	/* set descriptor for KV object */
	daos_iov_set(&iod->iod_name, &params->akey_str, 1);
	iod->iod_kcsum = null_csum;
	iod->iod_nr = 1;
	iod->iod_csums = NULL;
	iod->iod_eprs = NULL;
	iod->iod_size = props->cell_size;
	iod->iod_type = DAOS_IOD_ARRAY;
	iod->iod_recxs = malloc(sizeof(daos_recx_t));
	iod->iod_recxs[0].rx_idx = props->record_i;
	iod->iod_recxs[0].rx_nr = 1;

	rc = daos_task_create(DAOS_OPC_OBJ_UPDATE, tse_task2sched(props->ptask),
			      0, NULL, &io_task);
	if (rc) {
		free(sgl->sg_iovs);
		D_GOTO(err, rc);
	}
	io_arg = daos_task_get_args(io_task);
	io_arg->oh	= oh;
	io_arg->epoch	= epoch;
	io_arg->dkey	= dkey;
	io_arg->nr	= 1;
	io_arg->iods	= iod;
	io_arg->sgls	= sgl;

	rc = tse_task_register_comp_cb(io_task, free_io_params_cb, &params,
				       sizeof(params));
	if (rc)
		D_GOTO(err, rc);

	rc = tse_task_register_deps(props->ptask, 1, &io_task);
	if (rc)
		D_GOTO(err, rc);

	rc = tse_task_schedule(io_task, false);
	if (rc)
		D_GOTO(err, rc);

	return rc;
err:
	if (params) {
		if (params->dkey_str)
			free(params->dkey_str);
		D_FREE_PTR(params);
	}
	if (io_task)
		tse_task_complete(io_task, rc);
	return rc;
}

static int
adjust_array_size_cb(tse_task_t *task, void *data)
{
	daos_obj_list_dkey_t	*args = daos_task_get_args(task);
	struct set_size_props	*props = *((struct set_size_props **)data);
	char			*ptr;
	uint32_t		j;
	int			rc = task->dt_result;

	for (ptr = props->buf, j = 0; j < props->nr; j++) {
		daos_size_t dkey_num;
		int ret;

		snprintf(props->key, args->kds[j].kd_key_len + 1, "%s", ptr);
		ptr += args->kds[j].kd_key_len;

		if (!strcmp(ARRAY_MD_KEY, props->key))
			continue;

		ret = sscanf(props->key, "%zu", &dkey_num);
		D_ASSERT(ret == 1);

		if (props->size == 0 || dkey_num > props->dkey_num) {
			/*
			 * Punch the entire dkey since it's in a higher dkey
			 * group than the intended size.
			 */
			rc = punch_key(args->oh, args->epoch, props->key,
				       dkey_num, props->ptask);
			if (rc)
				return rc;
		} else if (dkey_num == props->dkey_num && props->record_i) {
			props->update_dkey = false;

			/*
			 * Punch all records above record_i, then check if
			 * record_i exists and insert a record if it doesn't.
			 */
			rc = punch_extent(args->oh, args->epoch, props->key,
					  props->record_i, props->num_records,
					  props->ptask);
			if (rc)
				return rc;

			rc = check_record(args->oh, args->epoch, props->key,
					  props->record_i, props->cell_size,
					  props->ptask);
			if (rc)
				return rc;
		}
		continue;
	}

	if (!daos_anchor_is_eof(args->anchor)) {
		props->nr = ENUM_DESC_NR;
		memset(props->buf, 0, ENUM_DESC_BUF);
		args->sgl->sg_nr = 1;
		daos_iov_set(&args->sgl->sg_iovs[0], props->buf, ENUM_DESC_BUF);

		rc = tse_task_reinit(task);
		if (rc) {
			D_ERROR("FAILED to reinit task\n");
			return rc;
		}

		rc = tse_task_register_cbs(task, NULL, NULL, 0,
					   adjust_array_size_cb, &props,
					   sizeof(props));
		if (rc) {
			tse_task_complete(task, rc);
			return rc;
		}

		return rc;
	}

	/** if array is smaller, write a record at the new size */
	if (props->update_dkey) {
		D_DEBUG(DB_IO, "Extending array key %zu, rec = %d\n",
			props->dkey_num, (int)props->record_i);

		/** no need to check the record, we know it's not there */
		rc = add_record(args->oh, args->epoch, props);
		if (rc)
			return rc;
	}
	return rc;
}

int
dac_array_set_size(tse_task_t *task)
{
	daos_array_set_size_t	*args;
	daos_handle_t		oh;
	struct dac_array	*array;
	char			*dkey_str = NULL;
	daos_size_t		num_records;
	daos_off_t		record_i;
	daos_obj_list_dkey_t	*enum_args;
	struct set_size_props	*set_size_props = NULL;
	tse_task_t		*enum_task;
	int			rc, ret;

	args = daos_task_get_args(task);
	array = array_hdl2ptr(args->oh);
	if (array == NULL)
		D_GOTO(err_task, rc = -DER_NO_HDL);

	oh = array->daos_oh;

	/** get key information for the last record */
	if (args->size == 0) {
		dkey_str = strdup("0");
		num_records = array->chunk_size;
		record_i = 0;
	} else {
		rc = compute_dkey(array, args->size-1, &num_records, &record_i,
				  &dkey_str);
		if (rc) {
			D_ERROR("Failed to compute dkey\n");
			D_GOTO(err_task, rc);
		}
	}

	D_ASSERT(record_i + num_records == array->chunk_size);

	D_ALLOC_PTR(set_size_props);
	if (set_size_props == NULL) {
		free(dkey_str);
		D_GOTO(err_task, rc = -DER_NOMEM);
	}

	ret = sscanf(dkey_str, "%zu", &set_size_props->dkey_num);
	D_ASSERT(ret == 1);
	free(dkey_str);

	set_size_props->array = array;
	set_size_props->cell_size = array->cell_size;
	set_size_props->num_records = num_records;
	set_size_props->record_i = record_i;
	set_size_props->chunk_size = array->chunk_size;
	set_size_props->nr = ENUM_DESC_NR;
	set_size_props->size = args->size;
	set_size_props->ptask = task;
	set_size_props->val = NULL;
	if (args->size == 0)
		set_size_props->update_dkey = false;
	else
		set_size_props->update_dkey = true;
	memset(set_size_props->buf, 0, ENUM_DESC_BUF);
	memset(&set_size_props->anchor, 0, sizeof(set_size_props->anchor));
	set_size_props->sgl.sg_nr = 1;
	set_size_props->sgl.sg_iovs = &set_size_props->iov;
	daos_iov_set(&set_size_props->sgl.sg_iovs[0], set_size_props->buf,
		     ENUM_DESC_BUF);

	rc = daos_task_create(DAOS_OPC_OBJ_LIST_DKEY, tse_task2sched(task),
			      0, NULL, &enum_task);
	if (rc)
		D_GOTO(err_task, rc);

	enum_args = daos_task_get_args(enum_task);
	enum_args->oh	  = oh;
	enum_args->epoch  = args->epoch;
	enum_args->nr	  = &set_size_props->nr;
	enum_args->kds	  = set_size_props->kds;
	enum_args->sgl	  = &set_size_props->sgl;
	enum_args->anchor = &set_size_props->anchor;

	rc = tse_task_register_cbs(enum_task, NULL, NULL, 0,
				   adjust_array_size_cb, &set_size_props,
				   sizeof(set_size_props));
	if (rc)
		D_GOTO(err_enum_task, rc);

	rc = tse_task_register_deps(task, 1, &enum_task);
	if (rc)
		D_GOTO(err_enum_task, rc);

	rc = tse_task_register_comp_cb(task, free_set_size_cb, &set_size_props,
				       sizeof(set_size_props));
	if (rc)
		D_GOTO(err_enum_task, rc);

	rc = tse_task_schedule(enum_task, false);
	if (rc)
		D_GOTO(err_enum_task, rc);

	tse_sched_progress(tse_task2sched(task));

	return 0;

err_enum_task:
	tse_task_complete(enum_task, rc);
err_task:
	if (set_size_props)
		D_FREE_PTR(set_size_props);
	if (array)
		array_decref(array);
	tse_task_complete(task, rc);
	return rc;
} /* end daos_array_set_size */
