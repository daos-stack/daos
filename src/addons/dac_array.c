/**
 * (C) Copyright 2016 Intel Corporation.
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
 * src/addons/dac_array.c
 */

#define DD_SUBSYS	DD_FAC(client)

#include <daos/common.h>
#include <daos/tse.h>
#include <daos/addons.h>
#include <daos_api.h>
#include <daos_addons.h>
#include <daos_task.h>

/* #define ARRAY_DEBUG */

#define ARRAY_MD_KEY "daos_array_metadata"
#define CELL_SIZE "daos_array_cell_size"
#define BLOCK_SIZE "daos_array_block_size"

struct dac_array {
	/** DAOS KV object handle */
	daos_handle_t	daos_oh;
	/** Array cell size of each element */
	daos_size_t	cell_size;
	/** elems to store in 1 dkey before moving to the next one in the grp */
	daos_size_t	block_size;
	/** ref count on array */
	unsigned int	cob_ref;
};

struct io_params {
	daos_key_t		dkey;
	char			*dkey_str;
	char			*akey_str;
	daos_iod_t		iod;
	daos_sg_list_t		sgl;
	bool			user_sgl_used;
	tse_task_t		*task;
	struct io_params	*next;
};

static struct dac_array *
array_alloc(void)
{
	struct dac_array *obj;

	D_ALLOC_PTR(obj);
	if (obj == NULL)
		return NULL;

	obj->cob_ref = 1;
	return obj;
}

static void
array_decref(struct dac_array *obj)
{
	obj->cob_ref--;
	if (obj->cob_ref == 0)
		D_FREE_PTR(obj);
}

static void
array_addref(struct dac_array *obj)
{
	obj->cob_ref++;
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
free_io_params_cb(tse_task_t *task, void *data)
{
	struct io_params *io_list = *((struct io_params **)data);
	int rc = task->dt_result;

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
		if (current->akey_str) {
			free(current->akey_str);
			current->akey_str = NULL;
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

	array->daos_oh = *args->oh;
	array->cell_size = args->cell_size;
	array->block_size = args->block_size;

	*args->oh = array_ptr2hdl(array);

	return 0;

err_obj:
	{
		daos_obj_close_t close_args;
		tse_task_t *close_task;

		close_args.oh = *args->oh;
		daos_task_create(DAOS_OPC_OBJ_CLOSE, tse_task2sched(task),
				 &close_args, 0, NULL, &close_task);
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

static int
write_md_cb(tse_task_t *task, void *data)
{
	daos_array_create_t *args = *((daos_array_create_t **)data);
	daos_obj_update_t *update_args;
	struct io_params *params;
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
	params->next = NULL;
	params->user_sgl_used = false;

	/** init dkey */
	daos_iov_set(&params->dkey, ARRAY_MD_KEY, strlen(ARRAY_MD_KEY));

	/** init scatter/gather */
	params->sgl.sg_iovs = malloc(sizeof(daos_iov_t) * 2);
	daos_iov_set(&params->sgl.sg_iovs[0], &args->cell_size,
		     sizeof(daos_size_t));
	daos_iov_set(&params->sgl.sg_iovs[1], &args->block_size,
		     sizeof(daos_size_t));
	params->sgl.sg_nr.num		= 2;
	params->sgl.sg_nr.num_out	= 0;

	/** init I/O descriptor */
	daos_iov_set(&params->iod.iod_name, ARRAY_MD_KEY, strlen(ARRAY_MD_KEY));
	daos_csum_set(&params->iod.iod_kcsum, NULL, 0);
	params->iod.iod_recxs = malloc(sizeof(daos_recx_t));
	params->iod.iod_nr = 1;
	params->iod.iod_recxs[0].rx_idx = 0;
	params->iod.iod_recxs[0].rx_nr = 2;
	params->iod.iod_eprs	= NULL;
	params->iod.iod_csums	= NULL;
	params->iod.iod_size = sizeof(daos_size_t);
	params->iod.iod_type = DAOS_IOD_ARRAY;

	/** Set the args for the update task */
	update_args = daos_task_get_args(DAOS_OPC_OBJ_UPDATE, task);
	update_args->oh = *args->oh;
	update_args->epoch = args->epoch;
	update_args->dkey = &params->dkey;
	update_args->nr = 1;
	update_args->iods = &params->iod;
	update_args->sgls = &params->sgl;

	rc = tse_task_register_comp_cb(task, free_io_params_cb, &params,
				       sizeof(params));
	if (rc != 0)
		return rc;

	return 0;
}

int
dac_array_create(tse_task_t *task)
{
	daos_array_create_t	*args;
	tse_task_t		*open_task, *update_task;
	daos_obj_open_t		open_args;
	int			rc;

	args = daos_task_get_args(DAOS_OPC_ARRAY_CREATE, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	/** Create task to open object */
	open_args.coh = args->coh;
	open_args.oid = args->oid;
	open_args.epoch = args->epoch;
	open_args.mode = DAOS_OO_RW;
	open_args.oh = args->oh;
	rc = daos_task_create(DAOS_OPC_OBJ_OPEN, tse_task2sched(task),
			      &open_args, 0, NULL, &open_task);
	if (rc != 0) {
		D_ERROR("Failed to create object_open task\n");
		return rc;
	}

	tse_task_schedule(open_task, false);

	/** Create task to write object metadata */
	rc = daos_task_create(DAOS_OPC_OBJ_UPDATE, tse_task2sched(task),
			      NULL, 1, &open_task, &update_task);
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
	daos_array_open_t *args = *((daos_array_open_t **)data);
	struct dac_array	*array;
	int			rc = task->dt_result;

	if (rc != 0)
		D_GOTO(err_obj, rc);

	/** If no cell and block size, this isn't an array obj. */
	if (*args->cell_size == 0 || *args->block_size == 0) {
		D_ERROR("Failed to retrieve array metadata\n");
		D_GOTO(err_obj, rc = -DER_NO_PERM);
	}

	/** Create an array OH from the DAOS one */
	array = array_alloc();
	if (array == NULL)
		D_GOTO(err_obj, rc = -DER_NOMEM);

	array->daos_oh = *args->oh;
	array->cell_size = *args->cell_size;
	array->block_size = *args->block_size;

	*args->oh = array_ptr2hdl(array);

	return 0;

err_obj:
	{
		daos_obj_close_t close_args;
		tse_task_t *close_task;

		close_args.oh = *args->oh;
		daos_task_create(DAOS_OPC_OBJ_CLOSE, tse_task2sched(task),
				 &close_args, 0, NULL, &close_task);
		return rc;
	}
}

static int
fetch_md_cb(tse_task_t *task, void *data)
{
	daos_array_open_t *args = *((daos_array_open_t **)data);
	daos_obj_fetch_t *fetch_args;
	struct io_params *params;
	int rc = task->dt_result;

	if (rc != 0)
		return rc;

	D_ALLOC_PTR(params);
	if (params == NULL) {
		D_ERROR("Failed memory allocation\n");
		return -DER_NOMEM;
	}
	params->next = NULL;
	params->user_sgl_used = false;

	/** init dkey */
	daos_iov_set(&params->dkey, ARRAY_MD_KEY, strlen(ARRAY_MD_KEY));

	/** init scatter/gather */
	params->sgl.sg_iovs = malloc(sizeof(daos_iov_t) * 2);
	daos_iov_set(&params->sgl.sg_iovs[0], args->cell_size,
		     sizeof(daos_size_t));
	daos_iov_set(&params->sgl.sg_iovs[1], args->block_size,
		     sizeof(daos_size_t));
	params->sgl.sg_nr.num		= 2;
	params->sgl.sg_nr.num_out	= 0;

	/** init I/O descriptor */
	daos_iov_set(&params->iod.iod_name, ARRAY_MD_KEY, strlen(ARRAY_MD_KEY));
	daos_csum_set(&params->iod.iod_kcsum, NULL, 0);
	params->iod.iod_recxs = malloc(sizeof(daos_recx_t));
	params->iod.iod_nr = 1;
	params->iod.iod_recxs[0].rx_idx = 0;
	params->iod.iod_recxs[0].rx_nr = 2;
	params->iod.iod_eprs	= NULL;
	params->iod.iod_csums	= NULL;
	params->iod.iod_size = sizeof(daos_size_t);
	params->iod.iod_type = DAOS_IOD_ARRAY;

	/** Set the args for the fetch task */
	fetch_args = daos_task_get_args(DAOS_OPC_OBJ_FETCH, task);
	fetch_args->oh = *args->oh;
	fetch_args->epoch = args->epoch;
	fetch_args->dkey = &params->dkey;
	fetch_args->nr = 1;
	fetch_args->iods = &params->iod;
	fetch_args->sgls = &params->sgl;

	rc = tse_task_register_comp_cb(task, free_io_params_cb, &params,
				       sizeof(params));
	if (rc != 0)
		return rc;

	return 0;
}

int
dac_array_open(tse_task_t *task)
{
	daos_array_open_t	*args;
	tse_task_t		*open_task, *fetch_task;
	daos_obj_open_t		open_args;
	int			rc;

	args = daos_task_get_args(DAOS_OPC_ARRAY_OPEN, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	/** Open task to open object */
	open_args.coh = args->coh;
	open_args.oid = args->oid;
	open_args.epoch = args->epoch;
	open_args.mode = args->mode;
	open_args.oh = args->oh;
	rc = daos_task_create(DAOS_OPC_OBJ_OPEN, tse_task2sched(task),
			      &open_args, 0, NULL, &open_task);
	if (rc != 0) {
		D_ERROR("Failed to open object_open task\n");
		return rc;
	}

	tse_task_schedule(open_task, false);

	/** Create task to fetch object metadata */
	rc = daos_task_create(DAOS_OPC_OBJ_FETCH, tse_task2sched(task),
			      NULL, 1, &open_task, &fetch_task);
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
	daos_array_close_t	*args;
	struct dac_array	*array;
	tse_task_t		*close_task;
	daos_obj_close_t	close_args;
	int			rc;

	args = daos_task_get_args(DAOS_OPC_ARRAY_CLOSE, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	array = array_hdl2ptr(args->oh);
	if (array == NULL)
		return -DER_NO_HDL;

	/** Create task to close object */
	close_args.oh = array->daos_oh;
	rc = daos_task_create(DAOS_OPC_OBJ_CLOSE, tse_task2sched(task),
			      &close_args, 0, NULL, &close_task);
	if (rc != 0) {
		D_ERROR("Failed to create object_close task\n");
		return rc;
	}

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
	array_decref(array);

	return rc;
err:
	D_FREE_PTR(close_task);
	return rc;
}

static bool
io_extent_same(daos_array_ranges_t *ranges, daos_sg_list_t *sgl,
	       daos_size_t cell_size)
{
	daos_size_t ranges_len;
	daos_size_t sgl_len;
	daos_size_t u;

	ranges_len = 0;
#ifdef ARRAY_DEBUG
	printf("USER ARRAY RANGE -----------------------\n");
	printf("ranges_nr = %zu\n", ranges->arr_nr);
#endif
	for (u = 0 ; u < ranges->arr_nr ; u++) {
		ranges_len += ranges->arr_rgs[u].rg_len;
#ifdef ARRAY_DEBUG
		printf("%zu: length %zu, index %d\n",
			u, ranges->arr_rgs[u].rg_len,
		       (int)ranges->arr_rgs[u].rg_idx);
#endif
	}
#ifdef ARRAY_DEBUG
	printf("------------------------------------\n");
	printf("USER SGL -----------------------\n");
	printf("sg_nr = %u\n", sgl->sg_nr.num);
#endif
	sgl_len = 0;
	for (u = 0 ; u < sgl->sg_nr.num; u++) {
		sgl_len += sgl->sg_iovs[u].iov_len;
#ifdef ARRAY_DEBUG
		printf("%zu: length %zu, Buf %p\n",
			u, sgl->sg_iovs[u].iov_len, sgl->sg_iovs[u].iov_buf);
#endif
	}

	return (ranges_len * cell_size == sgl_len);
}

/**
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
	dkey_num = array_idx / array->block_size;
	dkey_i = dkey_num * array->block_size;

	if (record_i)
		*record_i = array_idx - dkey_i;
	if (num_records)
		*num_records = array->block_size - *record_i;

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
	sgl->sg_nr.num = k = 0;
	sgl->sg_iovs = NULL;
	rem_records = num_records;

	/**
	 * Keep iterating through the user sgl till we populate our sgl to
	 * satisfy the number of records to read/write from the KV object
	 */
	do {
		D_ASSERT(user_sgl->sg_nr.num > cur_i);

		sgl->sg_nr.num++;
		sgl->sg_iovs = (daos_iov_t *)realloc
			(sgl->sg_iovs, sizeof(daos_iov_t) * sgl->sg_nr.num);
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
	} while (rem_records && user_sgl->sg_nr.num > cur_i);

	sgl->sg_nr.num_out = 0;

	*sgl_i = cur_i;
	*sgl_off = cur_off;

	return 0;
}

static int
dac_array_io(daos_handle_t array_oh, daos_epoch_t epoch,
	     daos_array_ranges_t *ranges, daos_sg_list_t *user_sgl,
	     daos_opc_t op_type, tse_task_t *task)
{
	struct dac_array *array = NULL;
	daos_handle_t	oh;
	daos_off_t	cur_off;/* offset into user buf to track current pos */
	daos_size_t	cur_i;	/* index into user sgl to track current pos */
	daos_size_t	records; /* Number of records to access in cur range */
	daos_off_t	array_idx; /* object array index of current range */
	daos_size_t	u;
	daos_size_t	num_records;
	daos_off_t	record_i;
	daos_csum_buf_t	null_csum;
	struct io_params *head, *current;
	daos_size_t	num_ios;
	int		rc;

	if (ranges == NULL) {
		D_ERROR("NULL ranges passed\n");
		D_GOTO(err_task, rc = -DER_INVAL);
	}
	if (user_sgl == NULL) {
		D_ERROR("NULL scatter-gather list passed\n");
		D_GOTO(err_task, rc = -DER_INVAL);
	}

	array = array_hdl2ptr(array_oh);
	if (array == NULL)
		return -DER_NO_HDL;

	if (!io_extent_same(ranges, user_sgl, array->cell_size)) {
		D_ERROR("Unequal extents of memory and array descriptors\n");
		D_GOTO(err_task, rc = -DER_INVAL);
	}

	oh = array->daos_oh;

	cur_off = 0;
	cur_i = 0;
	u = 0;
	num_ios = 0;
	records = ranges->arr_rgs[0].rg_len;
	array_idx = ranges->arr_rgs[0].rg_idx;
	daos_csum_set(&null_csum, NULL, 0);

	head = NULL;

	/**
	 * Loop over every range, but at the same time combine consecutive
	 * ranges that belong to the same dkey. If the user gives ranges that
	 * are not increasing in offset, they probably won't be combined unless
	 * the separating ranges also belong to the same dkey.
	 */
	while (u < ranges->arr_nr) {
		daos_iod_t	*iod;
		daos_sg_list_t	*sgl;
		char		*dkey_str;
		daos_key_t	*dkey;
		daos_size_t	dkey_records;
		tse_task_t	*io_task;
		struct io_params *params = NULL;
		daos_size_t	i;

		if (ranges->arr_rgs[u].rg_len == 0) {
			u++;
			if (u < ranges->arr_nr) {
				records = ranges->arr_rgs[u].rg_len;
				array_idx = ranges->arr_rgs[u].rg_idx;
			}
			continue;
		}

		D_ALLOC_PTR(params);
		if (params == NULL) {
			D_ERROR("Failed memory allocation\n");
			return -1;
		}

		if (num_ios == 0) {
			head = params;
			current = head;
		} else {
			current->next = params;
			current = params;
		}

		iod = &params->iod;
		sgl = &params->sgl;
		io_task = params->task;
		dkey = &params->dkey;
		params->akey_str = strdup("akey_not_used");
		params->next = NULL;
		params->user_sgl_used = false;

		num_ios++;

		rc = compute_dkey(array, array_idx, &num_records, &record_i,
				  &params->dkey_str);
		if (rc != 0) {
			D_ERROR("Failed to compute dkey\n");
			return rc;
		}
		dkey_str = params->dkey_str;
#ifdef ARRAY_DEBUG
		printf("DKEY IOD %s ---------------------------\n", dkey_str);
		printf("array_idx = %d\t num_records = %zu\t record_i = %d\n",
		       (int)array_idx, num_records, (int)record_i);
#endif
		daos_iov_set(dkey, (void *)dkey_str, strlen(dkey_str));

		/* set descriptor for KV object */
		daos_iov_set(&iod->iod_name, (void *)params->akey_str,
			     strlen(params->akey_str));
		iod->iod_kcsum = null_csum;
		iod->iod_nr = 0;
		iod->iod_csums = NULL;
		iod->iod_eprs = NULL;
		iod->iod_recxs = NULL;
		iod->iod_size = array->cell_size;
		iod->iod_type = DAOS_IOD_ARRAY;

		i = 0;
		dkey_records = 0;

		/**
		 * Create the IO descriptor for this dkey. If the entire range
		 * fits in the dkey, continue to the next range to see if we can
		 * combine it fully or partially in the current dkey IOD/
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
				return -DER_NOMEM;
			}

			/** set the record access for this range */
			iod->iod_recxs[i].rx_idx = record_i;
			iod->iod_recxs[i].rx_nr = (num_records > records) ?
				records : num_records;
#ifdef ARRAY_DEBUG
			printf("Add %zu to ARRAY IOD (size = %zu index = %d)\n",
			       u, iod->iod_recxs[i].rx_nr,
			       (int)iod->iod_recxs[i].rx_idx);
#endif
			/**
			 * if the current range is bigger than what the dkey can
			 * hold, update the array index and number of records in
			 * the current range and break to issue the I/O on the
			 * current KV.
			 */
			if (records > num_records) {
				array_idx += num_records;
				records -= num_records;
				dkey_records += num_records;
				break;
			}

			u++;
			i++;
			dkey_records += records;

			/** if there are no more ranges to write, then break */
			if (ranges->arr_nr <= u)
				break;

			old_array_idx = array_idx;
			records = ranges->arr_rgs[u].rg_len;
			array_idx = ranges->arr_rgs[u].rg_idx;

			/**
			 * Boundary case where number of records align with the
			 * end boundary of the KV. break after advancing to the
			 * next range
			 */
			if (records == num_records)
				break;

			/** cont processing the next range in the cur dkey */
			if (array_idx < old_array_idx + num_records &&
			   array_idx >= ((old_array_idx + num_records) -
				       array->block_size)) {
				char	*dkey_str_tmp = NULL;

				/**
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
					return rc;
				}

				D_ASSERT(strcmp(dkey_str_tmp, dkey_str) == 0);

				free(dkey_str_tmp);
				dkey_str_tmp = NULL;
			} else {
				break;
			}
		} while (1);
#ifdef ARRAY_DEBUG
		printf("END DKEY IOD %s ---------------------------\n",
		       dkey_str);
#endif
		/**
		 * if the user sgl maps directly to the array range, no need to
		 * partition it.
		 */
		if (1 == ranges->arr_nr && 1 == user_sgl->sg_nr.num &&
		    dkey_records == ranges->arr_rgs[0].rg_len) {
			sgl = user_sgl;
			params->user_sgl_used = true;
		}
		/** create an sgl from the user sgl for the current IOD */
		else {
			/* set sgl for current dkey */
			rc = create_sgl(user_sgl, array->cell_size,
					dkey_records, &cur_off, &cur_i, sgl);
			if (rc != 0) {
				D_ERROR("Failed to create sgl\n");
				return rc;
			}
#ifdef ARRAY_DEBUG
			daos_size_t s;

			printf("DKEY SGL -----------------------\n");
			printf("sg_nr = %u\n", sgl->sg_nr.num);
			for (s = 0; s < sgl->sg_nr.num; s++) {
				printf("%zu: length %zu, Buf %p\n",
				       s, sgl->sg_iovs[s].iov_len,
				       sgl->sg_iovs[s].iov_buf);
			}
			printf("------------------------------------\n");
#endif
		}

		/* issue KV IO to DAOS */
		if (op_type == DAOS_OPC_ARRAY_READ) {
			daos_obj_fetch_t io_arg;

			io_arg.oh = oh;
			io_arg.epoch = epoch;
			io_arg.dkey = dkey;
			io_arg.nr = 1;
			io_arg.iods = iod;
			io_arg.sgls = sgl;
			io_arg.maps = NULL;

			rc = daos_task_create(DAOS_OPC_OBJ_FETCH,
					      tse_task2sched(task), &io_arg, 0,
					      NULL, &io_task);
			if (rc != 0) {
				D_ERROR("KV Fetch of dkey %s failed (%d)\n",
					dkey_str, rc);
				return rc;
			}
		} else if (op_type == DAOS_OPC_ARRAY_WRITE) {
			daos_obj_update_t io_arg;

			io_arg.oh = oh;
			io_arg.epoch = epoch;
			io_arg.dkey = dkey;
			io_arg.nr = 1;
			io_arg.iods = iod;
			io_arg.sgls = sgl;

			rc = daos_task_create(DAOS_OPC_OBJ_UPDATE,
					      tse_task2sched(task), &io_arg, 0,
					      NULL, &io_task);
			if (rc != 0) {
				D_ERROR("KV Update of dkey %s failed (%d)\n",
					dkey_str, rc);
				return rc;
			}
		} else {
			D_ASSERTF(0, "Invalid array operation.\n");
		}

		tse_task_register_deps(task, 1, &io_task);

		rc = tse_task_schedule(io_task, false);
		if (rc != 0)
			return rc;
	} /* end while */

	if (head)
		tse_task_register_comp_cb(task, free_io_params_cb, &head,
					  sizeof(head));

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
	daos_array_io_t *args;

	args = daos_task_get_args(DAOS_OPC_ARRAY_READ, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return dac_array_io(args->oh, args->epoch, args->ranges, args->sgl,
			    DAOS_OPC_ARRAY_READ, task);
}

int
dac_array_write(tse_task_t *task)
{
	daos_array_io_t *args;

	args = daos_task_get_args(DAOS_OPC_ARRAY_WRITE, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return dac_array_io(args->oh, args->epoch, args->ranges, args->sgl,
			    DAOS_OPC_ARRAY_WRITE, task);
}

#define ENUM_KEY_BUF	32
#define ENUM_DESC_BUF	512
#define ENUM_DESC_NR	5

struct get_size_props {
	struct dac_array *array;
	char		key[ENUM_DESC_BUF];
	char		buf[ENUM_DESC_BUF];
	daos_key_desc_t kds[ENUM_DESC_NR];
	daos_iov_t	iov;
	daos_sg_list_t  sgl;
	uint32_t	nr;
	daos_hash_out_t anchor;
	daos_size_t	dkey_num;
	daos_size_t	*size;
	tse_task_t	*ptask;
};

struct list_recxs_params {
	daos_key_t		dkey;
	char			*dkey_str;
	daos_key_t		akey;
	char			*akey_str;
	daos_recx_t		recx;
	uint32_t		nr;
	daos_size_t		cell_size;
	daos_size_t		block_size;
	daos_hash_out_t		anchor;
	daos_size_t		*size;
	tse_task_t		*task;
};

static int
list_recxs_cb(tse_task_t *task, void *data)
{
	struct list_recxs_params *params = *((struct list_recxs_params **)data);
	daos_size_t dkey_num;
	int ret;
	int rc = task->dt_result;

#ifdef ARRAY_DEBUG
	printf("cell size %d recx idx = %d NR = %d\n",
	       (int)params->cell_size, (int)params->recx.rx_idx,
	       (int)params->recx.rx_nr);
#endif

	ret = sscanf(params->dkey_str, "%zu", &dkey_num);
	D_ASSERT(ret == 1);

	*params->size = dkey_num * params->block_size + params->recx.rx_idx +
		params->recx.rx_nr;

	if (params->dkey_str) {
		free(params->dkey_str);
		params->dkey_str = NULL;
	}
	if (params->akey_str) {
		free(params->akey_str);
		params->akey_str = NULL;
	}
	D_FREE_PTR(params);

	return rc;
}

static int
get_array_size_cb(tse_task_t *task, void *data)
{
	struct get_size_props *props = *((struct get_size_props **)data);
	struct dac_array *array = props->array;
	daos_obj_list_dkey_t *args;
	char		*ptr;
	uint32_t	i;
	int		rc = task->dt_result;

	args = daos_task_get_args(DAOS_OPC_OBJ_LIST_DKEY, task);

	for (ptr = props->buf, i = 0; i < props->nr; i++) {
		daos_size_t dkey_num;
		int ret;

		snprintf(props->key, args->kds[i].kd_key_len + 1, "%s", ptr);
#ifdef ARRAY_DEBUG
		printf("%d: key %s len %d\n", i, props->key,
		       (int)args->kds[i].kd_key_len);
#endif
		ptr += args->kds[i].kd_key_len;

		if (!strcmp(ARRAY_MD_KEY, props->key))
			continue;

		/** Keep a record of the highest dkey */
		ret = sscanf(props->key, "%zu", &dkey_num);
		D_ASSERT(ret == 1);

		if (dkey_num > props->dkey_num)
			props->dkey_num = dkey_num;
	}

	if (!daos_hash_is_eof(args->anchor)) {
		props->nr = ENUM_DESC_NR;
		memset(props->buf, 0, ENUM_DESC_BUF);
		args->sgl->sg_nr.num = 1;
		daos_iov_set(&args->sgl->sg_iovs[0], props->buf, ENUM_DESC_BUF);

		rc = tse_task_reinit(task);
		if (rc != 0) {
			D_ERROR("FAILED to continue enumrating task\n");
			D_GOTO(out, rc);
		}

		tse_task_register_cbs(task, NULL, NULL, 0, get_array_size_cb,
				       &props, sizeof(props));

		return rc;
	}

#ifdef ARRAY_DEBUG
	printf("DKEY NUM %zu\n", props->dkey_num);
#endif
	char key[ENUM_KEY_BUF];

	sprintf(key, "%zu", props->dkey_num);

	/** retrieve the highest index from the highest key */
	props->nr = ENUM_DESC_NR;

	tse_task_t *io_task = NULL;
	struct list_recxs_params *params = NULL;
	daos_key_t *dkey, *akey;
	daos_obj_list_recx_t list_args;

	D_ALLOC_PTR(params);
	if (params == NULL) {
		D_ERROR("Failed memory allocation\n");
		return -DER_NOMEM;
	}

	akey = &params->akey;
	dkey = &params->dkey;

	io_task = params->task;
	params->akey_str = strdup("akey_not_used");
	params->dkey_str = strdup(key);
	daos_iov_set(dkey, (void *)params->dkey_str, strlen(params->dkey_str));
	daos_iov_set(akey, (void *)params->akey_str, strlen(params->akey_str));
	params->nr = 1;
	params->block_size = array->block_size;
	params->size = props->size;

	list_args.oh = args->oh;
	list_args.epoch = args->epoch;
	list_args.dkey = dkey;
	list_args.akey = akey;
	list_args.type = DAOS_IOD_ARRAY;
	list_args.size = &params->cell_size;
	list_args.nr = &params->nr;
	list_args.recxs = &params->recx;
	list_args.eprs = NULL;
	list_args.cookies = NULL;
	list_args.incr_order = false;
	memset(&params->anchor, 0, sizeof(params->anchor));
	list_args.anchor = &params->anchor;
	list_args.versions = NULL;

	rc = daos_task_create(DAOS_OPC_OBJ_LIST_RECX, tse_task2sched(task),
			      &list_args, 0, NULL, &io_task);
	if (rc != 0) {
		D_ERROR("punch recs failed (%d)\n", rc);
		D_GOTO(out, rc);
	}

	rc = tse_task_register_comp_cb(io_task, list_recxs_cb, &params,
				       sizeof(params));
	if (rc != 0)
		D_GOTO(out, rc);

	rc = tse_task_register_deps(props->ptask, 1, &io_task);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = tse_task_schedule(io_task, false);
	if (rc != 0)
		D_GOTO(out, rc);

out:
	array_decref(array);
	D_FREE_PTR(props);
	return rc;
}

int
dac_array_get_size(tse_task_t *task)
{
	daos_array_get_size_t	*args;
	daos_handle_t		oh;
	struct dac_array	*array;
	daos_obj_list_dkey_t	enum_args;
	struct get_size_props	*get_size_props = NULL;
	tse_task_t		*enum_task;
	int			rc;

	args = daos_task_get_args(DAOS_OPC_ARRAY_GET_SIZE, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	array = array_hdl2ptr(args->oh);
	if (array == NULL)
		D_GOTO(err_task, rc = -DER_NO_HDL);

	oh = array->daos_oh;

	D_ALLOC_PTR(get_size_props);
	if (get_size_props == NULL)
		D_GOTO(err_task, rc = -DER_NOMEM);

	get_size_props->dkey_num = 0;
	get_size_props->nr = ENUM_DESC_NR;
	get_size_props->ptask = task;
	get_size_props->size = args->size;
	get_size_props->array = array;
	memset(get_size_props->buf, 0, ENUM_DESC_BUF);
	memset(&get_size_props->anchor, 0, sizeof(get_size_props->anchor));
	get_size_props->sgl.sg_nr.num = 1;
	get_size_props->sgl.sg_iovs = &get_size_props->iov;
	daos_iov_set(&get_size_props->sgl.sg_iovs[0], get_size_props->buf,
		     ENUM_DESC_BUF);

	enum_args.oh = oh;
	enum_args.epoch = args->epoch;
	enum_args.nr = &get_size_props->nr;
	enum_args.kds = get_size_props->kds;
	enum_args.sgl = &get_size_props->sgl;
	enum_args.anchor = &get_size_props->anchor;

	rc = daos_task_create(DAOS_OPC_OBJ_LIST_DKEY, tse_task2sched(task),
			      &enum_args, 0, NULL, &enum_task);
	if (rc != 0)
		D_GOTO(err_task, rc);

	rc = tse_task_register_cbs(enum_task, NULL, NULL, 0, get_array_size_cb,
				    &get_size_props, sizeof(get_size_props));
	if (rc != 0) {
		D_ERROR("Failed to register completion cb\n");
		D_GOTO(err_task, rc);
	}

	rc = tse_task_register_deps(task, 1, &enum_task);
	if (rc != 0) {
		D_ERROR("Failed to register dependency\n");
		D_GOTO(err_task, rc);
	}

	rc = tse_task_schedule(enum_task, false);
	if (rc != 0)
		D_GOTO(err_task, rc);

	tse_sched_progress(tse_task2sched(task));

	return 0;

err_task:
	if (get_size_props)
		D_FREE_PTR(get_size_props);
	if (enum_task)
		D_FREE_PTR(enum_task);
	array_decref(array);
	tse_task_complete(task, rc);
	return rc;
} /* end daos_array_get_size */

struct set_size_props {
	char		key[ENUM_DESC_BUF];
	char		buf[ENUM_DESC_BUF];
	daos_key_desc_t kds[ENUM_DESC_NR];
	char		*val;
	daos_iov_t	iov;
	daos_sg_list_t  sgl;
	uint32_t	nr;
	daos_hash_out_t anchor;
	bool		shrinking;
	daos_size_t	dkey_num;
	daos_size_t	size;
	daos_size_t	cell_size;
	daos_size_t	num_records;
	daos_size_t	block_size;
	daos_off_t	record_i;
	tse_task_t	*ptask;
};

static int
free_props_cb(tse_task_t *task, void *data)
{
	struct set_size_props *props = *((struct set_size_props **)data);

	if (props->val)
		free(props->val);
	D_FREE_PTR(props);
	return 0;
}

static int
adjust_array_size_cb(tse_task_t *task, void *data)
{
	struct set_size_props *props = *((struct set_size_props **)data);
	daos_obj_list_dkey_t *args;
	char		*ptr;
	tse_task_t	*io_task = NULL;
	struct io_params *params = NULL;
	uint32_t	j;
	int		rc = task->dt_result;

	args = daos_task_get_args(DAOS_OPC_OBJ_LIST_DKEY, task);

	if (props->size == 0)
		props->shrinking = true;

	for (ptr = props->buf, j = 0; j < props->nr; j++) {
		daos_size_t dkey_num;
		int ret;

		snprintf(props->key, args->kds[j].kd_key_len + 1, "%s", ptr);
#ifdef ARRAY_DEBUG
		printf("%d: key %s len %d\n", j, props->key,
		       (int)args->kds[j].kd_key_len);
#endif
		ptr += args->kds[j].kd_key_len;

		if (!strcmp(ARRAY_MD_KEY, props->key))
			continue;

		/** Keep a record of the highest dkey */
		ret = sscanf(props->key, "%zu", &dkey_num);
		D_ASSERT(ret == 1);

		if (props->size == 0 || dkey_num > props->dkey_num) {
			daos_obj_punch_dkeys_t p_args;
			daos_key_t *dkey;

			/*
			 * Punch the entire dkey since it's in a higher dkey
			 * group than the intended size.
			 */
			props->shrinking = true;

			D_ALLOC_PTR(params);
			if (params == NULL) {
				D_ERROR("Failed memory allocation\n");
				return -DER_NOMEM;
			}

			io_task = params->task;
			params->dkey_str = strdup(props->key);
			dkey = &params->dkey;
			daos_iov_set(dkey, (void *)params->dkey_str,
				     strlen(params->dkey_str));

			/** Punch this entire dkey */
			p_args.oh = args->oh;
			p_args.epoch = args->epoch;
			p_args.nr = 1;
			p_args.dkeys = dkey;

			rc = daos_task_create(DAOS_OPC_OBJ_PUNCH_DKEYS,
					      tse_task2sched(task), &p_args, 0,
					      NULL, &io_task);
			if (rc != 0) {
				D_ERROR("Punch dkey %s failed (%d)\n",
					params->dkey_str, rc);
				D_GOTO(err_out, rc);
			}

			rc = tse_task_register_comp_cb(io_task,
						       free_io_params_cb,
						       &params,
						       sizeof(params));
			if (rc != 0)
				D_GOTO(err_out, rc);

			rc = tse_task_register_deps(props->ptask, 1,
						    &io_task);
			if (rc != 0)
				D_GOTO(err_out, rc);

			rc = tse_task_schedule(io_task, false);
			if (rc != 0)
				D_GOTO(err_out, rc);
		} else if (dkey_num == props->dkey_num && props->record_i) {
			/* punch all records above record_i */
			daos_obj_update_t io_arg;
			daos_iod_t	*iod;
			daos_sg_list_t	*sgl;
			daos_key_t	*dkey;
			daos_csum_buf_t	null_csum;

			props->shrinking = true;

			daos_csum_set(&null_csum, NULL, 0);

			D_ALLOC_PTR(params);
			if (params == NULL) {
				D_ERROR("Failed memory allocation\n");
				return -DER_NOMEM;
			}

			iod = &params->iod;
			sgl = NULL;
			dkey = &params->dkey;

			io_task = params->task;
			params->akey_str = strdup("akey_not_used");
			params->next = NULL;
			params->user_sgl_used = false;

			params->dkey_str = strdup(props->key);
			dkey = &params->dkey;
			daos_iov_set(dkey, (void *)params->dkey_str,
				     strlen(params->dkey_str));

			/* set descriptor for KV object */
			daos_iov_set(&iod->iod_name, (void *)params->akey_str,
				     strlen(params->akey_str));
			iod->iod_kcsum = null_csum;
			iod->iod_nr = 1;
			iod->iod_csums = NULL;
			iod->iod_eprs = NULL;
			iod->iod_size = 0; /* 0 to punch */
			iod->iod_type = DAOS_IOD_ARRAY;
			iod->iod_recxs = malloc(sizeof(daos_recx_t));
			iod->iod_recxs[0].rx_idx = props->record_i;
			iod->iod_recxs[0].rx_nr = props->num_records;

			io_arg.oh = args->oh;
			io_arg.epoch = args->epoch;
			io_arg.dkey = dkey;
			io_arg.nr = 1;
			io_arg.iods = iod;
			io_arg.sgls = sgl;

			rc = daos_task_create(DAOS_OPC_OBJ_UPDATE,
					      tse_task2sched(task),
					      &io_arg, 0, NULL,
					      &io_task);
			if (rc != 0) {
				D_ERROR("punch recs failed (%d)\n", rc);
				D_GOTO(err_out, rc);
			}

			rc = tse_task_register_comp_cb(io_task,
						       free_io_params_cb,
						       &params,
						       sizeof(params));
			if (rc != 0)
				D_GOTO(err_out, rc);

			rc = tse_task_register_deps(props->ptask, 1,
						    &io_task);
			if (rc != 0)
				D_GOTO(err_out, rc);

			rc = tse_task_schedule(io_task, false);
			if (rc != 0)
				D_GOTO(err_out, rc);
		}
		continue;
	}

	if (!daos_hash_is_eof(args->anchor)) {
		props->nr = ENUM_DESC_NR;
		memset(props->buf, 0, ENUM_DESC_BUF);
		args->sgl->sg_nr.num = 1;
		daos_iov_set(&args->sgl->sg_iovs[0], props->buf, ENUM_DESC_BUF);

		rc = tse_task_reinit(task);
		if (rc != 0) {
			D_ERROR("FAILED to continue enumrating task\n");
			return rc;
		}

		tse_task_register_cbs(task, NULL, NULL, 0,
				       adjust_array_size_cb, &props,
				       sizeof(props));

		return rc;
	}

	/** if array is smaller, write a record at the new size */
	if (!props->shrinking) {
		daos_obj_update_t io_arg;
		daos_iod_t	*iod;
		daos_sg_list_t	*sgl;
		daos_key_t	*dkey;
		daos_csum_buf_t	null_csum;

#ifdef ARRAY_DEBUG
		printf("Extending array key %zu, rec = %d\n",
		       props->dkey_num, (int)props->record_i);
#endif
		daos_csum_set(&null_csum, NULL, 0);

		D_ALLOC_PTR(params);
		if (params == NULL) {
			D_ERROR("Failed memory allocation\n");
			return -DER_NOMEM;
		}

		iod = &params->iod;
		sgl = &params->sgl;
		dkey = &params->dkey;

		io_task = params->task;
		params->akey_str = strdup("akey_not_used");
		params->next = NULL;
		params->user_sgl_used = false;

		rc = asprintf(&params->dkey_str, "%zu", props->dkey_num);
		if (rc < 0 || params->dkey_str == NULL) {
			D_ERROR("Failed memory allocation\n");
			D_GOTO(err_out, -DER_NOMEM);
		}
		daos_iov_set(dkey, (void *)params->dkey_str,
			     strlen(params->dkey_str));

		/** set memory location */
		props->val = calloc(1, props->cell_size);
		sgl->sg_nr.num = 1;
		sgl->sg_iovs = malloc(sizeof(daos_iov_t));
		daos_iov_set(&sgl->sg_iovs[0], props->val, props->cell_size);

		/* set descriptor for KV object */
		daos_iov_set(&iod->iod_name, (void *)params->akey_str,
			     strlen(params->akey_str));
		iod->iod_kcsum = null_csum;
		iod->iod_nr = 1;
		iod->iod_csums = NULL;
		iod->iod_eprs = NULL;
		iod->iod_size = props->cell_size;
		iod->iod_type = DAOS_IOD_ARRAY;
		iod->iod_recxs = malloc(sizeof(daos_recx_t));
		iod->iod_recxs[0].rx_idx = props->record_i;
		iod->iod_recxs[0].rx_nr = 1;

		io_arg.oh = args->oh;
		io_arg.epoch = args->epoch;
		io_arg.dkey = dkey;
		io_arg.nr = 1;
		io_arg.iods = iod;
		io_arg.sgls = sgl;

		rc = daos_task_create(DAOS_OPC_OBJ_UPDATE,
				      tse_task2sched(task), &io_arg, 0, NULL,
				      &io_task);
		if (rc != 0) {
			D_ERROR("KV Update of dkey %s failed (%d)\n",
				params->dkey_str, rc);
			D_GOTO(err_out, rc);
		}

		rc = tse_task_register_comp_cb(io_task, free_io_params_cb,
					       &params, sizeof(params));
		if (rc != 0)
			D_GOTO(err_out, rc);

		rc = tse_task_register_deps(props->ptask, 1, &io_task);
		if (rc != 0)
			D_GOTO(err_out, rc);

		rc = tse_task_schedule(io_task, false);
		if (rc != 0)
			D_GOTO(err_out, rc);
	}

	return rc;

err_out:
	if (params->dkey_str)
		D_FREE_PTR(params->dkey_str);
	if (params)
		D_FREE_PTR(params);
	if (io_task)
		D_FREE_PTR(io_task);

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
	daos_obj_list_dkey_t	enum_args;
	struct set_size_props	*set_size_props = NULL;
	tse_task_t		*enum_task;
	int			rc, ret;

	args = daos_task_get_args(DAOS_OPC_ARRAY_SET_SIZE, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	array = array_hdl2ptr(args->oh);
	if (array == NULL)
		D_GOTO(err_task, rc = -DER_NO_HDL);

	oh = array->daos_oh;

	/** get key information for the last record */
	if (args->size == 0) {
		dkey_str = strdup("0_0");
		num_records = array->block_size;
		record_i = 0;
	} else {
		rc = compute_dkey(array, args->size-1, &num_records, &record_i,
				  &dkey_str);
		if (rc != 0) {
			D_ERROR("Failed to compute dkey\n");
			D_GOTO(err_task, rc);
		}
	}

	D_ASSERT(record_i + num_records == array->block_size);

	D_ALLOC_PTR(set_size_props);
	if (set_size_props == NULL) {
		free(dkey_str);
		D_GOTO(err_task, rc = -DER_NOMEM);
	}

	ret = sscanf(dkey_str, "%zu", &set_size_props->dkey_num);
	D_ASSERT(ret == 1);
	free(dkey_str);

	set_size_props->cell_size = array->cell_size;
	set_size_props->num_records = num_records;
	set_size_props->record_i = record_i;
	set_size_props->block_size = array->block_size;
	set_size_props->shrinking = false;
	set_size_props->nr = ENUM_DESC_NR;
	set_size_props->size = args->size;
	set_size_props->ptask = task;
	set_size_props->val = NULL;
	memset(set_size_props->buf, 0, ENUM_DESC_BUF);
	memset(&set_size_props->anchor, 0, sizeof(set_size_props->anchor));
	set_size_props->sgl.sg_nr.num = 1;
	set_size_props->sgl.sg_iovs = &set_size_props->iov;
	daos_iov_set(&set_size_props->sgl.sg_iovs[0], set_size_props->buf,
		     ENUM_DESC_BUF);

	enum_args.oh = oh;
	enum_args.epoch = args->epoch;
	enum_args.nr = &set_size_props->nr;
	enum_args.kds = set_size_props->kds;
	enum_args.sgl = &set_size_props->sgl;
	enum_args.anchor = &set_size_props->anchor;

	rc = daos_task_create(DAOS_OPC_OBJ_LIST_DKEY, tse_task2sched(task),
			      &enum_args, 0, NULL, &enum_task);
	if (rc != 0)
		return rc;

	rc = tse_task_register_cbs(enum_task, NULL, NULL, 0,
				   adjust_array_size_cb, &set_size_props,
				   sizeof(set_size_props));
	if (rc != 0)
		D_GOTO(err_task, rc);

	rc = tse_task_register_deps(task, 1, &enum_task);
	if (rc != 0)
		D_GOTO(err_task, rc);

	rc = tse_task_register_comp_cb(task, free_props_cb, &set_size_props,
				       sizeof(set_size_props));
	if (rc != 0)
		D_GOTO(err_task, rc);

	rc = tse_task_schedule(enum_task, false);
	if (rc != 0)
		D_GOTO(err_task, rc);

	tse_sched_progress(tse_task2sched(task));

	array_decref(array);
	return 0;

err_task:
	if (set_size_props)
		D_FREE_PTR(set_size_props);
	if (enum_task)
		D_FREE_PTR(enum_task);
	array_decref(array);
	tse_task_complete(task, rc);
	return rc;
} /* end daos_array_set_size */
