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

typedef struct {
	/** DAOS object ID */
	daos_obj_id_t oid;
	daos_off_t    off;
} cache_data_key_t;

typedef struct {
	int iov_min;
	int iov_max;
} iov_range_t;

static int
daos_array_read_cached(dfs_t *dfs, dfs_obj_t *obj, daos_array_iod_t *iod, d_sg_list_t *sgl)
{
	/* offset of current read request */
	daos_off_t       off;
	/* index of range */
	int              idx_rg;
	/* index of iov */
	int              idx_iov;
	/* counter of number of range */
	int              rg_cnt;
	int              i;
	int              j;
	int              rc;
	/* hold the LRU node of cache query */
	shm_lru_node_t  *node_found = NULL;
	/* the key used for data cache query. oid + read offset (aligned) */
	cache_data_key_t key;
	/* offset aligned to caching block size */
	daos_off_t       off_aligned;
	daos_off_t       off_aligned_pre = -1;
	/* current offset in current caching block. offset % DEFAULT_CACHE_DATA_SIZE */
	daos_off_t       off_data;
	/* dynamic buffer that holds data and iov_range_t list */
	char            *buf;
	/* dynamic buffer for daos_array_read() */
	char            *data;
	/* the cached data for the queried key */
	char            *cache_data;
	/* list of ranges with min and max iov */
	iov_range_t     *iov_range_list;
	d_sg_list_t	     sgl_loc;
	d_iov_t		     iov;
	/* total size of read */
	daos_size_t      read_size;
	/* the total size of read in current range */
	daos_size_t      read_size_rg;
	daos_size_t      byte_to_read_iov;
	daos_size_t      byte_read_loc;
	daos_size_t      byte_rg_sum;
	daos_size_t      byte_to_read_real;
	daos_size_t      byte_to_prefetch;
	daos_size_t      byte_cached;
	daos_size_t      byte_to_cache;
	bool             is_eof = false;
	daos_array_iod_t iod_loc;
	daos_range_t     rg;

	/* this buffer is shared by iov_range_list and data */
	buf = shm_alloc(sizeof(iov_range_t) * iod->arr_nr + MAX_PREFETCH_READ_SIZE);
	if (buf == NULL)
		return ENOMEM;
	iov_range_list = (iov_range_t *)buf;
	data           = buf + sizeof(iov_range_t) * iod->arr_nr;

	/* to determine the min and max iov for each contiguous range */
	rg_cnt                         = 0;
	byte_rg_sum                    = 0;
	iov_range_list[rg_cnt].iov_min = 0;
	/* loop over all sgl */
	for (i = 0; i < sgl->sg_nr; i++) {
		byte_rg_sum += sgl->sg_iovs[i].iov_len;
		if (byte_rg_sum > iod->arr_rgs[rg_cnt].rg_len) {
			/* inconsistent iod an sgl */
			D_GOTO(err, rc = EINVAL);
		} else if (byte_rg_sum == iod->arr_rgs[rg_cnt].rg_len) {
			iov_range_list[rg_cnt].iov_max = i;
			rg_cnt++;
			if (rg_cnt >= iod->arr_nr)
				break;
			iov_range_list[rg_cnt].iov_min = i + 1;
			byte_rg_sum                    = 0;
		}
	}

	if (i != (sgl->sg_nr - 1) || rg_cnt < iod->arr_nr)
		/* inconsistent iod an sgl */
		D_GOTO(err, rc = EINVAL);

	read_size = 0;

	/* loop over range list */
	for (idx_rg = 0; idx_rg < iod->arr_nr; idx_rg++) {
		off          = iod->arr_rgs[idx_rg].rg_idx;
		read_size_rg = 0;

		/* loop over iov list for current contiguous range */
		for (idx_iov = iov_range_list[idx_rg].iov_min;
			 idx_iov <= iov_range_list[idx_rg].iov_max; idx_iov++) {
			off_data    = (off + read_size_rg) % DEFAULT_CACHE_DATA_SIZE;
			off_aligned = (off + read_size_rg) - off_data;
			/* the number of bytes read for one request */
			byte_to_read_iov = sgl->sg_iovs[idx_iov].iov_len;
			byte_read_loc    = 0;

			/* loop until the requested size is completed or reaching the end of the file */
			while (byte_to_read_iov > 0) {
				byte_to_read_real = min(DEFAULT_CACHE_DATA_SIZE - off_data, byte_to_read_iov);
				if (off_aligned != off_aligned_pre) {
					/* need to get data from cache */
					dfs_obj2id(obj, &key.oid);
					key.off = off_aligned;
					if (node_found) {
						/* need to decrease the reference of the LRU node accessed previously */
						shm_lru_node_dec_ref(node_found);
						node_found = NULL;
					}

					/* find the data in cache, read from server then cache it if uncached */
					rc = shm_lru_get(dfs->datacache, &key, KEY_SIZE_FILE_ID_OFF, &node_found,
									 (void **)&cache_data);
					if (rc) {
						/* need to determine a consolidated read size, then read data from server and
						 * store in cache for later use
						 */
						byte_to_prefetch = off_data + sgl->sg_iovs[idx_iov].iov_len - byte_read_loc;
						for (j = idx_iov + 1; j <= iov_range_list[idx_rg].iov_max; j++) {
							if ((byte_to_prefetch + sgl->sg_iovs[j].iov_len) >=
								MAX_PREFETCH_READ_SIZE) {
								byte_to_prefetch += sgl->sg_iovs[j].iov_len;
								break;
							}
						}
						if (byte_to_prefetch > MAX_PREFETCH_READ_SIZE)
							byte_to_prefetch = MAX_PREFETCH_READ_SIZE;
						d_iov_set(&iov, data, byte_to_prefetch);

						/** set array location */
						iod_loc.arr_nr  = 1;
						rg.rg_len       = byte_to_prefetch;
						rg.rg_idx       = off_aligned;
						iod_loc.arr_rgs = &rg;

						sgl_loc.sg_nr     = 1;
						sgl_loc.sg_nr_out = 1;
						sgl_loc.sg_iovs   = &iov;
						rc = daos_array_read(obj->oh, dfs->th, &iod_loc, &sgl_loc, NULL);
						if (rc)
							D_GOTO(err, rc);
						/* caching data */
						byte_cached = 0;
						while (byte_cached < iod_loc.arr_nr_read) {
							key.off = off_aligned + byte_cached;
							byte_to_cache = min(iod_loc.arr_nr_read - byte_cached,
												DEFAULT_CACHE_DATA_SIZE);
							rc = shm_lru_put(dfs->datacache, &key, KEY_SIZE_FILE_ID_OFF, data +
											 byte_cached, byte_to_cache);
							if (rc)
								D_GOTO(err, rc);
							byte_cached += byte_to_cache;
						}
						/* read data from cache */
						key.off = off_aligned;
						rc = shm_lru_get(dfs->datacache, &key, KEY_SIZE_FILE_ID_OFF, &node_found,
										 (void **)&cache_data);
						if (rc)
							return rc;
					}
					if (node_found->data_size < DEFAULT_CACHE_DATA_SIZE) {
						/* reached the end of file */
						byte_to_read_real = max(min(node_found->data_size - off_data,
												byte_to_read_iov), 0);
						is_eof = true;
					}
					off_aligned_pre = off_aligned;
				}

				memcpy(sgl->sg_iovs[idx_iov].iov_buf + sgl->sg_iovs[idx_iov].iov_buf_len -
					   byte_to_read_iov, cache_data + off_data, byte_to_read_real);
				read_size        += byte_to_read_real;
				read_size_rg     += byte_to_read_real;
				byte_read_loc    += byte_to_read_real;
				byte_to_read_iov -= byte_to_read_real;

				if (is_eof && (off_data + byte_to_read_real == node_found->data_size))
					/* all data have been consumed. */
					goto done;

				off_data = (off + read_size_rg) % DEFAULT_CACHE_DATA_SIZE;
				off_aligned = (off + read_size_rg) - off_data;
			}
		}
	}

done:
	shm_free(buf);
	if (node_found)
		/* need to decrease the reference of the LRU node accessed previously */
		shm_lru_node_dec_ref(node_found);
	iod->arr_nr_read = read_size;
	return 0;

err:
	shm_free(buf);
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
