/**
 * (C) Copyright 2018-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/** DFS read & write ops */

#define D_LOGFAC DD_FAC(dfs)

#include <daos/array.h>
#include <daos/common.h>
#include <daos/event.h>
#include <daos/object.h>
#include <gurt/shm_utils.h>

#include "dfs_internal.h"

static void
dfs_update_file_metrics(dfs_t *dfs, daos_size_t read_bytes, daos_size_t write_bytes)
{
	if (dfs == NULL || dfs->metrics == NULL)
		return;

	if (read_bytes > 0)
		d_tm_inc_gauge(dfs->metrics->dm_read_bytes, read_bytes);
	if (write_bytes > 0)
		d_tm_inc_gauge(dfs->metrics->dm_write_bytes, write_bytes);
}

struct dfs_read_params {
	dfs_t           *dfs;
	daos_size_t     *read_size;
	daos_array_iod_t arr_iod;
	daos_range_t     rg;
};

static int
read_cb(tse_task_t *task, void *data)
{
	struct dfs_read_params *params;
	int                     rc = task->dt_result;

	params = daos_task_get_priv(task);
	D_ASSERT(params != NULL);

	if (rc != 0) {
		D_ERROR("Failed to read from array object: " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	DFS_OP_STAT_INCR(params->dfs, DOS_READ);
	dfs_update_file_metrics(params->dfs, params->arr_iod.arr_nr_read, 0);
	*params->read_size = params->arr_iod.arr_nr_read;
out:
	D_FREE(params);
	return rc;
}

static int
dfs_read_int(dfs_t *dfs, dfs_obj_t *obj, daos_off_t off, dfs_iod_t *iod, d_sg_list_t *sgl,
	     daos_size_t buf_size, daos_size_t *read_size, daos_event_t *ev)
{
	tse_task_t             *task = NULL;
	daos_array_io_t        *args;
	struct dfs_read_params *params;
	int                     rc;

	D_ASSERT(ev);
	daos_event_errno_rc(ev);

	rc = dc_task_create(dc_array_read, NULL, ev, &task);
	if (rc != 0)
		return daos_der2errno(rc);

	D_ALLOC_PTR(params);
	if (params == NULL)
		D_GOTO(err_task, rc = -DER_NOMEM);

	params->dfs       = dfs;
	params->read_size = read_size;

	/** set array location */
	if (iod == NULL) {
		params->arr_iod.arr_nr  = 1;
		params->rg.rg_len       = buf_size;
		params->rg.rg_idx       = off;
		params->arr_iod.arr_rgs = &params->rg;
	} else {
		params->arr_iod.arr_nr  = iod->iod_nr;
		params->arr_iod.arr_rgs = iod->iod_rgs;
	}

	args      = dc_task_get_args(task);
	args->oh  = obj->oh;
	args->th  = dfs->th;
	args->sgl = sgl;
	args->iod = &params->arr_iod;

	daos_task_set_priv(task, params);
	rc = tse_task_register_cbs(task, NULL, NULL, 0, read_cb, NULL, 0);
	if (rc)
		D_GOTO(err_params, rc);

	/*
	 * dc_task_schedule() calls tse_task_complete() even on error (which also calls the
	 * completion cb that frees params in this case, so we can just ignore the rc here.
	 */
	dc_task_schedule(task, true);

	return 0;

err_params:
	D_FREE(params);
err_task:
	tse_task_complete(task, rc);
	/** the event is completed with the proper rc */
	return 0;
}

#define MAX_NUM_REQ            (64)

/* the chunck of request aligned with data cache entry size */
typedef struct {
	daos_off_t       off_base;	/* cache entry size aligned */
	daos_off_t       off;	/* offset relative to off_base (the offset within a cache entry) */
	uint32_t         size;	/* the size to copy to user buffer */
	uint32_t         size_req;	/* the size to request from server */
	int              pre_req;	/* existing request in request list */
	char            *buf_usr;	/* the user buffer to receive data */
	char            *buf_cache;	/* the buffer to receive data from server */
	shm_lru_node_t  *node_found;	/* the pointer to the LRU node */
} dat_req;

#define DEFAULT_CACHE_DATA_SIZE         (512 * 1024)

static int
request_in_batch(dfs_t *dfs, dfs_obj_t *obj, int num_req, dat_req req_list[], cache_data_key_t *key,
		 daos_size_t *file_size, daos_size_t *short_read_size)
{
	int              i;
	int              rc;
	int              num_rg  = 0;
	int              num_sgl = 0;
	daos_size_t      byte_to_copy;
	daos_size_t      byte_to_cache;
	daos_size_t      byte_copied   = 0;
	daos_size_t      byte_to_fetch = 0;
	daos_array_iod_t iod;
	d_sg_list_t      sgl = {0};
	daos_range_t     rg_req[MAX_NUM_REQ];
	d_iov_t          iov_list[MAX_NUM_REQ];
	dat_req         *req;
	dat_req         *req_pre;
	daos_size_t      byte_left;
	daos_off_t       offset;

	*short_read_size = 0;

	req_list[0].pre_req = 0;
	for (i = 1; i < num_req; i++) {
		if (req_list[i].off_base == req_list[i - 1].off_base)
			req_list[i].pre_req = req_list[i - 1].pre_req;
		else
			req_list[i].pre_req = i;
	}

	/* pre-allocate buffer to receive data from server. They will be used by data cache */
	for (i = 0; i < num_req; i++) {
		if (req_list[i].pre_req != i)
			/* the request is a duplicate of a previous one */
			continue;

		/* no previous req existing */
		req_list[i].buf_cache = shm_alloc(DEFAULT_CACHE_DATA_SIZE);
		if (req_list[i].buf_cache == NULL)
			D_GOTO(err, rc = ENOMEM);
		req_list[i].node_found = NULL;
	}

	/* need to 1) get data from server 2) copy data into user buffer 3) add data into cache */

	/* consolidate range list */
	iod.arr_rgs      = rg_req;
	rg_req[0].rg_idx = req_list[0].off_base;
	rg_req[0].rg_len = req_list[0].size_req;
	sgl.sg_iovs      = iov_list;
	d_iov_set(&iov_list[0], req_list[0].buf_cache, req_list[0].size_req);
	byte_to_fetch = rg_req[0].rg_len;
	num_sgl       = 1;

	for (i = 1; i < num_req; i++) {
		if (req_list[i].pre_req != i)
			/* the request is a duplicate of a previous request */
			continue;

		d_iov_set(&iov_list[num_sgl], req_list[i].buf_cache, req_list[i].size_req);
		byte_to_fetch += req_list[i].size_req;

		if (req_list[i].off_base != (req_list[i - 1].off_base + req_list[i - 1].size_req)) {
			rg_req[num_rg].rg_idx = req_list[i].off_base;
			rg_req[num_rg].rg_len = req_list[i].size_req;
			num_rg++;
		} else if (req_list[i].off_base == (req_list[i - 1].off_base + req_list[i - 1].size_req)) {
			rg_req[num_rg].rg_len += req_list[i].size_req;
		}
		num_sgl++;
	}
	num_rg++;
	iod.arr_nr = num_rg;
	sgl.sg_nr  = num_sgl;

	/* get data from server */
	rc = daos_array_read(obj->oh, DAOS_TX_NONE, &iod, &sgl, NULL);
	if (rc) {
		D_GOTO(err, rc);
	} else {
		if (iod.arr_nr_read < byte_to_fetch)
			/* reached the end of the file, save file size */
			*file_size = iod.file_size;
	}

	for (i = 0; i < num_req; i++) {
		req          = &req_list[i];
		/* set the boundary if short read is detected or file size known */
		offset       = req->off_base + req->off;
		byte_left    = (*file_size >= offset) ? (*file_size - offset) : (0);
		byte_to_copy = min(req->size, byte_left);

		if (req_list[i].pre_req != i) {
			req_pre = &req_list[req_list[i].pre_req];
				
			/* copy data into user buffer */
			/* the number of bytes left until reaching EOF */
			if (byte_to_copy > 0)
				memcpy(req->buf_usr, req_pre->buf_cache + req->off, byte_to_copy);
		} else {
			/* copy data into user buffer */
			/* the number of bytes left until reaching EOF */
			if (byte_to_copy > 0)
				memcpy(req->buf_usr, req->buf_cache + req->off, byte_to_copy);

			/* add data into cache */
			/* key = oid + offset */
			if (req_list[i].off_base < *file_size) {
				/* offset is not larger than file size */
				key->off      = req_list[i].off_base;
				byte_to_cache = min(*file_size - req->off_base,
						    DEFAULT_CACHE_DATA_SIZE);
				rc = shm_lru_put_shallow_cp(dfs->datacache, key,
							    KEY_SIZE_FILE_ID_OFF,
							    req_list[i].buf_cache, byte_to_cache,
							    &req->node_found);
				if (rc) {
					printf("Warning: fail to cache data rc = %d\n", rc);
					D_GOTO(err, rc);
				}
			}
		}
		byte_copied += byte_to_copy;
		*short_read_size += (req->size - byte_to_copy);
	}

	for (i = 0; i < num_req; i++) {
		if (req_list[i].pre_req == i) {
			if (req_list[i].node_found)
				shm_lru_node_dec_ref(req_list[i].node_found);
		}
	}

	return 0;

err:
	for (i = 0; i < num_req; i++) {
		if (req_list[i].pre_req == i)
			shm_free(req_list[i].buf_cache);
	}
	return rc;
}

static int
daos_array_read_cached(dfs_t *dfs, dfs_obj_t *obj, daos_array_iod_t *iod, d_sg_list_t *sgl)
{
	int                  rc;
	int                  num_req = 0;
	int                  idx_rg  = 0;
	int                  idx_sg  = 0;
	daos_off_t           off;
	daos_off_t           off_aligned;
	daos_off_t           off_in_rec;	/* off % DEFAULT_CACHE_DATA_SIZE */
	daos_size_t          off_in_sg;
	daos_size_t          left_in_sg;
	daos_size_t          byte_rg_sum;
	daos_size_t          byte_copied;
	daos_size_t          byte_short_read    = 0;
	daos_size_t          byte_short_read_rg = 0;
	daos_size_t          byte_read          = 0;
	daos_size_t          file_size;
	daos_size_t          tmp_file_size;
	daos_size_t          short_read_batch;
	daos_size_t          size_diff;
	/* hold the LRU data cache node */
	shm_lru_node_t      *node_data = NULL;
	char                *cache_data;
	/* the key used for data cache query. oid + read offset (aligned) */
	cache_data_key_t     key;
	dat_req              req_list[MAX_NUM_REQ];
	dat_req             *req;
	uint32_t             rec_data_size;
	/* number of bytes left in file from current offset to EOF */
	daos_size_t          byte_left;

	iod->arr_nr_short_read = 0;
	iod->arr_nr_read       = 0;

	file_size = obj->dc_file_size;
	if (obj->dc_file_size == ULONG_MAX)
		query_cached_file_size(dfs, obj);

	key.pool_cont_hash = dfs->pool_cont_hash;
	dfs_obj2id(obj, &key.oid);

	/* used to determine each contiguous range */
	byte_rg_sum = 0;
	off         = iod->arr_rgs[idx_rg].rg_idx;
	off_in_rec  = off % DEFAULT_CACHE_DATA_SIZE;
	off_aligned = off - off_in_rec;

	/* loop over all sgl */
	off_in_sg  = 0;
	left_in_sg = sgl->sg_iovs[idx_sg].iov_buf_len;
	while (idx_sg < sgl->sg_nr) {
		if (off >= file_size) {
			/* reached the end of file in current sg, need to skip current sg */
			byte_short_read_rg += left_in_sg;
			byte_short_read    += left_in_sg;
			left_in_sg          = 0;
		}

		/* loop until reaching the end of current sg or req_list is full */
		while (left_in_sg > 0 && num_req < MAX_NUM_REQ) {
			if (off >= file_size) {
				/* reached the end of file in current sg, need to skip current sg */
				byte_short_read_rg += left_in_sg;
				byte_short_read    += left_in_sg;
				left_in_sg          = 0;
				break;
			}
			byte_left = file_size - off;

			/* lookup data in cache, if not existing append the request to the request
			 * list
			 */

			/* key = oid + offset */
			key.off = off_aligned;
			rc = shm_lru_get(dfs->datacache, &key, KEY_SIZE_FILE_ID_OFF, &node_data,
					 (void **)&cache_data);
			if (rc) {
				req           = &req_list[num_req];
				req->off_base = off_aligned;
				req->off      = off_in_rec;
				byte_copied   = min(DEFAULT_CACHE_DATA_SIZE - off_in_rec, left_in_sg);
				if (byte_copied > byte_left) {
					byte_copied = byte_left;
					left_in_sg  = 0;
					size_diff   = left_in_sg - byte_copied;
					byte_short_read_rg += size_diff;
					byte_short_read    += size_diff;
				}
				req->size     = byte_copied;
				/* avoid short read in case file size is known */
				req->size_req = min(DEFAULT_CACHE_DATA_SIZE, file_size - off_aligned);
				req->buf_usr  = sgl->sg_iovs[idx_sg].iov_buf + off_in_sg;
				num_req++;
			} else {
				rec_data_size = shm_lru_rec_data_size(node_data);
				if (file_size == ULONG_MAX && rec_data_size <
				    DEFAULT_CACHE_DATA_SIZE) {
					/* file size is unknown in current process yet, but it was
					 * determined previously read with short read detected.
					 */
					file_size         = off_aligned + rec_data_size;
					obj->dc_file_size = file_size;
					byte_left         = file_size - off;
				}
				/* data are found in cache, copy to user buffer */
				byte_copied = rec_data_size - off_in_rec;
				if (byte_copied < left_in_sg) {
					/* reached the end of file */
					byte_short_read    += (left_in_sg - byte_copied);
					byte_short_read_rg += (left_in_sg - byte_copied);
					/* adjust left_in_sg to make it zero after executing "left_in_sg -= byte_copied" */
					left_in_sg         -= (left_in_sg - byte_copied);
				} else {
					byte_copied = left_in_sg;
				}
				if (byte_copied > 0)
					memcpy(sgl->sg_iovs[idx_sg].iov_buf + off_in_sg, cache_data +
					       off_in_rec, byte_copied);
				shm_lru_node_dec_ref(node_data);
			}
			off_in_sg  += byte_copied;
			left_in_sg -= byte_copied;

			off         += byte_copied;
			off_in_rec   = off % DEFAULT_CACHE_DATA_SIZE;
			off_aligned  = off - off_in_rec;
			byte_rg_sum += byte_copied;
			byte_read   += byte_copied;
		}

		/* num_req > 0 and req_list is full or have processed all sgl */
		if ((num_req == MAX_NUM_REQ || ((idx_sg == (sgl->sg_nr -1)) && (left_in_sg == 0))) && (num_req > 0)) {
			tmp_file_size = file_size;
			rc = request_in_batch(dfs, obj, num_req, req_list, &key, &tmp_file_size, &short_read_batch);
			if (rc)
				goto org;
			/* reset num_req */
			num_req = 0;
			byte_short_read += short_read_batch;
			/* adjust the number of bytes read */
			byte_read       -= short_read_batch;

			if (file_size == ULONG_MAX && tmp_file_size != ULONG_MAX) {
				file_size = tmp_file_size;
				/* update file size on first short read detection */
				cache_file_size(dfs, obj, file_size);
			}
		}

		if ((byte_rg_sum + byte_short_read_rg) > iod->arr_rgs[idx_rg].rg_len) {
			/* inconsistent iod and sgl */
			D_GOTO(err, rc = -DER_IO_INVAL);
		} else if ((byte_rg_sum + byte_short_read_rg) == iod->arr_rgs[idx_rg].rg_len) {
			idx_rg++;
			byte_rg_sum        = 0;
			byte_short_read_rg = 0;
			if (idx_rg == iod->arr_nr)
				/* finished all range */
				break;
			else if (idx_rg < iod->arr_nr) {
				if (left_in_sg != 0)
					/* inconsistent sgl and iod */
					D_GOTO(err, rc = -DER_IO_INVAL);

				off         = iod->arr_rgs[idx_rg].rg_idx;
				off_in_rec  = off % DEFAULT_CACHE_DATA_SIZE;
				off_aligned = off - off_in_rec;
			}
		}

		if (left_in_sg == 0) {
			/* reached the end of current sg, progress to next sg */
			idx_sg++;
			if (idx_sg < sgl->sg_nr) {
				off_in_sg  = 0;
				left_in_sg = sgl->sg_iovs[idx_sg].iov_buf_len;
			}
		}
	}

	if (idx_sg != (sgl->sg_nr - 1) || idx_rg < iod->arr_nr)
		/* inconsistent iod and sgl */
		D_GOTO(err, rc = -DER_IO_INVAL);

	iod->arr_nr_short_read += byte_short_read;
	iod->arr_nr_read       += byte_read;

	return 0;

org:
	/* fall back to call the read without caching. might be improved later to read only the
	 * unfinished part
	 */
	iod->arr_nr_short_read = 0;
	iod->arr_nr_read       = 0;
	return daos_array_read(obj->oh, DAOS_TX_NONE, iod, sgl, NULL);

err:
	return rc;
}

int
dfs_read(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t *sgl, daos_off_t off, daos_size_t *read_size,
	 daos_event_t *ev)
{
	daos_size_t buf_size;
	int         i, rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if (read_size == NULL)
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_WRONLY)
		return EPERM;

	buf_size = 0;
	for (i = 0; i < sgl->sg_nr; i++)
		buf_size += sgl->sg_iovs[i].iov_len;
	if (buf_size == 0) {
		*read_size = 0;
		if (ev) {
			daos_event_launch(ev);
			daos_event_complete(ev, 0);
		}
		DFS_OP_STAT_INCR(dfs, DOS_READ);
		return 0;
	}

	D_DEBUG(DB_TRACE, "DFS Read: Off %" PRIu64 ", Len %zu\n", off, buf_size);

	if (ev == NULL) {
		daos_array_iod_t iod;
		daos_range_t     rg;

		/** set array location */
		iod.arr_nr  = 1;
		rg.rg_len   = buf_size;
		rg.rg_idx   = off;
		iod.arr_rgs = &rg;

		rc = (dfs->datacache == NULL) ? daos_array_read(obj->oh, dfs->th, &iod, sgl, NULL) :
			 daos_array_read_cached(dfs, obj, &iod, sgl);
		if (rc) {
			if (dfs->datacache == NULL)
				D_ERROR("daos_array_read() failed, " DF_RC "\n", DP_RC(rc));
			else
				D_ERROR("daos_array_read_cached() failed, " DF_RC "\n", DP_RC(rc));
			return daos_der2errno(rc);
		}

		DFS_OP_STAT_INCR(dfs, DOS_READ);
		*read_size = iod.arr_nr_read;
		dfs_update_file_metrics(dfs, iod.arr_nr_read, 0);
		return 0;
	}

	return dfs_read_int(dfs, obj, off, NULL, sgl, buf_size, read_size, ev);
}

int
dfs_readx(dfs_t *dfs, dfs_obj_t *obj, dfs_iod_t *iod, d_sg_list_t *sgl, daos_size_t *read_size,
	  daos_event_t *ev)
{
	int rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if (read_size == NULL)
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_WRONLY)
		return EPERM;

	if (iod->iod_nr == 0) {
		if (ev) {
			daos_event_launch(ev);
			daos_event_complete(ev, 0);
		}
		DFS_OP_STAT_INCR(dfs, DOS_READ);
		return 0;
	}

	if (ev == NULL) {
		daos_array_iod_t arr_iod;

		/** set array location */
		arr_iod.arr_nr  = iod->iod_nr;
		arr_iod.arr_rgs = iod->iod_rgs;

		rc = (dfs->datacache == NULL) ? daos_array_read(obj->oh, dfs->th, &arr_iod, sgl, ev) :
			 daos_array_read_cached(dfs, obj, &arr_iod, sgl);
		if (rc) {
			if (dfs->datacache == NULL)
				D_ERROR("daos_array_read() failed (%d)\n", rc);
			else
				D_ERROR("daos_array_read_cached() failed (%d)\n", rc);
			return daos_der2errno(rc);
		}

		DFS_OP_STAT_INCR(dfs, DOS_READ);
		*read_size = arr_iod.arr_nr_read;
		dfs_update_file_metrics(dfs, arr_iod.arr_nr_read, 0);
		return 0;
	}

	return dfs_read_int(dfs, obj, 0, iod, sgl, 0, read_size, ev);
}

int
dfs_write(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t *sgl, daos_off_t off, daos_event_t *ev)
{
	daos_array_iod_t iod;
	daos_range_t     rg;
	daos_size_t      buf_size;
	int              i;
	int              rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_RDONLY)
		return EPERM;

	buf_size = 0;
	if (sgl)
		for (i = 0; i < sgl->sg_nr; i++)
			buf_size += sgl->sg_iovs[i].iov_len;
	if (buf_size == 0) {
		if (ev) {
			daos_event_launch(ev);
			daos_event_complete(ev, 0);
		}
		DFS_OP_STAT_INCR(dfs, DOS_WRITE);
		return 0;
	}

	/** set array location */
	iod.arr_nr  = 1;
	rg.rg_len   = buf_size;
	rg.rg_idx   = off;
	iod.arr_rgs = &rg;

	D_DEBUG(DB_TRACE, "DFS Write: Off %" PRIu64 ", Len %zu\n", off, buf_size);

	if (ev)
		daos_event_errno_rc(ev);

	rc = daos_array_write(obj->oh, DAOS_TX_NONE, &iod, sgl, ev);
	if (rc == 0) {
		DFS_OP_STAT_INCR(dfs, DOS_WRITE);
		dfs_update_file_metrics(dfs, 0, buf_size);
	} else {
		D_ERROR("daos_array_write() failed, " DF_RC "\n", DP_RC(rc));
	}

	return daos_der2errno(rc);
}

int
dfs_writex(dfs_t *dfs, dfs_obj_t *obj, dfs_iod_t *iod, d_sg_list_t *sgl, daos_event_t *ev)
{
	daos_array_iod_t arr_iod;
	daos_size_t      buf_size;
	int              i;
	int              rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_RDONLY)
		return EPERM;
	if (iod == NULL)
		return EINVAL;

	if (iod->iod_nr == 0) {
		if (ev) {
			daos_event_launch(ev);
			daos_event_complete(ev, 0);
		}
		DFS_OP_STAT_INCR(dfs, DOS_WRITE);
		return 0;
	}

	/** set array location */
	arr_iod.arr_nr  = iod->iod_nr;
	arr_iod.arr_rgs = iod->iod_rgs;

	if (ev)
		daos_event_errno_rc(ev);

	buf_size = 0;
	if (dfs->metrics != NULL && sgl != NULL)
		for (i = 0; i < sgl->sg_nr; i++)
			buf_size += sgl->sg_iovs[i].iov_len;

	rc = daos_array_write(obj->oh, DAOS_TX_NONE, &arr_iod, sgl, ev);
	if (rc == 0) {
		DFS_OP_STAT_INCR(dfs, DOS_WRITE);
		dfs_update_file_metrics(dfs, 0, buf_size);
	} else {
		D_ERROR("daos_array_write() failed (%d)\n", rc);
	}

	return daos_der2errno(rc);
}
