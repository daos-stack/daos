/**
 * (C) Copyright 2018-2024 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
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

/*
 * Progressive layout (PL) splits a file's logical byte range across two array objects: the head
 * array holds logical bytes [0, split_off) and the tail array holds logical bytes [split_off, EOF)
 * reindexed to start at 0 (tail index == logical_off - split_off). The helpers below route a
 * logical IO to the proper array object(s) based on obj->f.split_off. Non-PL files (has_tail ==
 * false) never reach these helpers and keep their original single-object behavior.
 */

/** A head or tail portion of a logical array IO after splitting at split_off. */
struct dfs_io_part {
	daos_handle_t    dip_oh;
	daos_array_iod_t dip_iod;
	d_sg_list_t      dip_sgl;
	bool             dip_active;
};

/* Append each contiguous chunk that daos_sgl_processor() yields as an iov in dst_iovs. */
struct sgl_append_ctx {
	d_iov_t  *dst_iovs;
	uint32_t *dst_nr;
};

static int
sgl_append_iov_cb(uint8_t *buf, size_t len, void *args)
{
	struct sgl_append_ctx *ctx = args;

	d_iov_set(&ctx->dst_iovs[*ctx->dst_nr], buf, len);
	(*ctx->dst_nr)++;
	return 0;
}

/*
 * Append \a need bytes from \a src starting at the running cursor \a idx as a set of iovs into \a
 * dst_iovs, advancing the cursor. The cursor walks the source sgl byte stream (by iov_len) so that
 * successive calls carve out consecutive byte slices that map to consecutive array ranges.
 * daos_sgl_processor() stops at the end of the sgl, so a \a need larger than the bytes remaining in
 * \a src cannot walk past sg_nr.
 */
static void
sgl_copy_bytes(d_sg_list_t *src, struct daos_sgl_idx *idx, daos_size_t need, d_iov_t *dst_iovs,
	       uint32_t *dst_nr)
{
	struct sgl_append_ctx ctx = {.dst_iovs = dst_iovs, .dst_nr = dst_nr};

	daos_sgl_processor(src, false, idx, need, sgl_append_iov_cb, &ctx);
}

/* memset each contiguous chunk daos_sgl_processor() yields (used to zero a byte window). */
static int
zero_iov_cb(uint8_t *buf, size_t len, void *args)
{
	memset(buf, 0, len);
	return 0;
}

/* Zero a [off, off+len) byte window within the logical SGL stream (walking iov capacities). */
static void
sgl_zero_range(d_sg_list_t *sgl, daos_size_t off, daos_size_t len)
{
	struct daos_sgl_idx idx = {0};

	/* check_buf=true walks iov_buf_len (capacity): skip to off, then zero len bytes. */
	daos_sgl_processor(sgl, true, &idx, off, NULL, NULL);
	daos_sgl_processor(sgl, true, &idx, len, zero_iov_cb, NULL);
}

/*
 * Zero the SGL bytes that correspond to interior holes of a head-only read. For each requested
 * range the bytes at or beyond the head array's own EOF (\a head_size) were left untouched by the
 * fetch; when the tail holds data those bytes are interior holes of the logical file and must read
 * back as zeros. Ranges map into the SGL byte stream in iod order (a cursor advances by each range
 * length), so the per-range hole is zeroed at its own position regardless of how the ranges are
 * sorted. The bytes before head_size were already data- or zero-filled by the array fetch.
 */
static void
sgl_zero_head_holes(d_sg_list_t *sgl, daos_range_t *rgs, uint32_t rg_nr, daos_size_t head_size)
{
	daos_size_t cursor = 0;
	uint32_t    j;

	for (j = 0; j < rg_nr; j++) {
		daos_size_t idx = rgs[j].rg_idx;
		daos_size_t len = rgs[j].rg_len;
		daos_size_t hole_at;

		if (idx >= head_size)
			hole_at = 0; /* whole range is past the head EOF */
		else if (idx + len > head_size)
			hole_at = head_size - idx; /* range straddles the head EOF */
		else
			hole_at = len; /* range is fully within the head data */

		if (len > hole_at)
			sgl_zero_range(sgl, cursor + hole_at, len - hole_at);
		cursor += len;
	}
}

/*
 * Resolve a short read of the head array of a progressive-layout file. The head array reports a
 * short read (and leaves the buffer untouched) for any bytes past its OWN EOF, but it has no
 * knowledge of the tail. If the tail holds data, the logical file extends beyond split_off, so the
 * head's short region is an interior hole of the logical file rather than the file's EOF: the bytes
 * the head left untouched must read back as zeros and be counted as read. If the tail is empty the
 * short region is the genuine EOF: per POSIX the buffer past EOF is left untouched and the short
 * count stands. Only call this when the head actually short-fetched (nr_read < buf_size); the
 * tail-size lookup is then off the common (full-data) path.
 *
 * Zeros are placed per range using the head array size (\a head_size, which the head read already
 * reported via arr_array_size), so unsorted/non-contiguous dfs_readx() ranges (whose holes need not
 * be at the tail of the SGL) are handled correctly. Only the tail-size lookup is needed here, and
 * only for interior holes (tail has data), not for reads at EOF.
 */
static int
dfs_pl_head_short_read(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t *sgl, daos_range_t *rgs,
		       uint32_t rg_nr, daos_size_t buf_size, daos_size_t head_size,
		       daos_size_t *nr_read)
{
	daos_size_t tail_size = 0;
	int         rc;

	rc = daos_array_get_size(obj->f.tail_oh, dfs->th, &tail_size, NULL);
	if (rc) {
		D_ERROR("Failed to get tail array size: " DF_RC "\n", DP_RC(rc));
		return rc;
	}
	if (tail_size == 0)
		/* genuine EOF: leave the buffer past EOF untouched (POSIX), keep the short count */
		return 0;

	/* logical file continues into the tail: the head's untouched gaps are interior holes */
	sgl_zero_head_holes(sgl, rgs, rg_nr, head_size);
	*nr_read = buf_size;
	return 0;
}

static void
dfs_io_part_free(struct dfs_io_part *part)
{
	D_FREE(part->dip_iod.arr_rgs);
	D_FREE(part->dip_sgl.sg_iovs);
}

/*
 * Split the logical array ranges (and, when \a sgl is not NULL, the matching sgl byte stream) at
 * obj->f.split_off into a head portion (logical [0, split_off), routed to obj->oh) and a tail
 * portion (logical [split_off, EOF) reindexed to start at 0, routed to obj->f.tail_oh). The
 * produced ranges/iovs are allocated and must be released with dfs_io_part_free().
 */
static int
dfs_io_build_parts(dfs_obj_t *obj, daos_range_t *rgs, uint32_t rg_nr, d_sg_list_t *sgl,
		   struct dfs_io_part *head, struct dfs_io_part *tail)
{
	daos_size_t         split = obj->f.split_off;
	struct daos_sgl_idx cur   = {0};
	uint32_t            i;
	int                 rc = 0;

	memset(head, 0, sizeof(*head));
	memset(tail, 0, sizeof(*tail));
	head->dip_oh = obj->oh;
	tail->dip_oh = obj->f.tail_oh;

	D_ALLOC_ARRAY(head->dip_iod.arr_rgs, rg_nr);
	D_ALLOC_ARRAY(tail->dip_iod.arr_rgs, rg_nr);
	if (head->dip_iod.arr_rgs == NULL || tail->dip_iod.arr_rgs == NULL)
		D_GOTO(err, rc = -DER_NOMEM);

	if (sgl != NULL) {
		/*
		 * Each range can split at most one source iov, so head and tail together never need
		 * more than sg_nr + 2 * rg_nr iovs; size each side to that safe upper bound.
		 */
		uint32_t max_iovs = sgl->sg_nr + 2 * rg_nr;

		D_ALLOC_ARRAY(head->dip_sgl.sg_iovs, max_iovs);
		D_ALLOC_ARRAY(tail->dip_sgl.sg_iovs, max_iovs);
		if (head->dip_sgl.sg_iovs == NULL || tail->dip_sgl.sg_iovs == NULL)
			D_GOTO(err, rc = -DER_NOMEM);
	}

	for (i = 0; i < rg_nr; i++) {
		daos_size_t idx = rgs[i].rg_idx;
		daos_size_t len = rgs[i].rg_len;
		daos_size_t end = idx + len;
		daos_size_t hlen;

		if (len == 0)
			continue;

		hlen = 0;
		if (idx < split)
			hlen = (end < split ? end : split) - idx;

		if (hlen > 0) {
			daos_range_t *r = &head->dip_iod.arr_rgs[head->dip_iod.arr_nr++];

			r->rg_idx = idx;
			r->rg_len = hlen;
			if (sgl != NULL)
				sgl_copy_bytes(sgl, &cur, hlen, head->dip_sgl.sg_iovs,
					       &head->dip_sgl.sg_nr);
		}
		if (len - hlen > 0) {
			daos_range_t *r    = &tail->dip_iod.arr_rgs[tail->dip_iod.arr_nr++];
			daos_size_t   tidx = (idx > split ? idx : split) - split;

			r->rg_idx = tidx;
			r->rg_len = len - hlen;
			if (sgl != NULL)
				sgl_copy_bytes(sgl, &cur, len - hlen, tail->dip_sgl.sg_iovs,
					       &tail->dip_sgl.sg_nr);
		}
	}

	head->dip_active = head->dip_iod.arr_nr > 0;
	tail->dip_active = tail->dip_iod.arr_nr > 0;
	return 0;

err:
	dfs_io_part_free(head);
	dfs_io_part_free(tail);
	return rc;
}

/*
 * Tracking context for a logical IO that straddles split_off and is therefore issued as two
 * concurrent array operations (head + tail). It lives until both sub-tasks complete and is freed
 * by the parent task's completion callback (dfs_io_split_cb).
 */
struct dfs_io_split_args {
	dfs_t             *dfs;
	daos_size_t       *read_size;
	daos_size_t        buf_size;
	bool               write;
	struct dfs_io_part head;
	struct dfs_io_part tail;
};

/*
 * Completion callback on the parent task. It runs once both the head and tail sub-tasks have
 * completed, aggregates their results, updates stats/metrics, and releases the tracking context.
 */
static int
dfs_io_split_cb(tse_task_t *task, void *data)
{
	struct dfs_io_split_args *args = *((struct dfs_io_split_args **)data);
	int                       rc   = task->dt_result;

	if (rc != 0) {
		D_ERROR("Failed to %s split array object: " DF_RC "\n",
			args->write ? "write to" : "read from", DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (args->write) {
		DFS_OP_STAT_INCR(args->dfs, DOS_WRITE);
		dfs_update_file_metrics(args->dfs, 0, args->buf_size);
	} else {
		daos_size_t nr_read =
		    args->head.dip_iod.arr_nr_read + args->tail.dip_iod.arr_nr_read;

		/*
		 * arr_nr_read from an array read excludes only the trailing short-read (records
		 * past that object's local EOF); interior holes are counted and zero-filled. The
		 * head object has no knowledge of the tail object, so when a head range ends in a
		 * hole it is reported as a (trailing) short-read even though the logical file
		 * continues into the tail. The head region is within the logical EOF whenever the
		 * file extends into the tail -- i.e. when the tail array SIZE is non-zero -- which
		 * is NOT the same as the requested tail subrange returning data: a multi-range
		 * readx may target a tail range that is itself beyond EOF (arr_nr_read == 0) while
		 * the file still extends into the tail elsewhere. Both array reads report the size
		 * each used to resolve its own short reads (arr_array_size), so gate on the tail's
		 * reported size, count the head's full requested length, and zero the bytes the
		 * head read left untouched past its own EOF. Place those zeros per head range using
		 * the head's reported size -- the head ranges may be unsorted, so the untouched gap
		 * need not be at the tail of the head SGL.
		 */
		if (args->head.dip_active && args->tail.dip_active &&
		    args->tail.dip_iod.arr_array_size > 0) {
			daos_size_t head_len = 0;
			uint32_t    i;

			for (i = 0; i < args->head.dip_iod.arr_nr; i++)
				head_len += args->head.dip_iod.arr_rgs[i].rg_len;
			sgl_zero_head_holes(&args->head.dip_sgl, args->head.dip_iod.arr_rgs,
					    args->head.dip_iod.arr_nr,
					    args->head.dip_iod.arr_array_size);
			nr_read = head_len + args->tail.dip_iod.arr_nr_read;
		}

		DFS_OP_STAT_INCR(args->dfs, DOS_READ);
		if (args->read_size != NULL)
			*args->read_size = nr_read;
		dfs_update_file_metrics(args->dfs, nr_read, 0);
	}
out:
	dfs_io_part_free(&args->head);
	dfs_io_part_free(&args->tail);
	D_FREE(args);
	return rc;
}

/*
 * Parent task body. It spawns one array sub-task per active part (head and tail) on the parent's
 * scheduler, registers each as a dependency of the parent so the parent only completes after both
 * sub-tasks complete, and schedules the sub-tasks to run concurrently.
 */
static int
dfs_io_split_task(tse_task_t *task)
{
	struct dfs_io_split_args *args  = daos_task_get_priv(task);
	tse_sched_t              *sched = tse_task2sched(task);
	struct dfs_io_part       *parts[2];
	tse_task_func_t           io_func;
	d_list_t                  io_list;
	bool                      cb_registered = false;
	int                       i, rc;

	D_INIT_LIST_HEAD(&io_list);
	parts[0] = &args->head;
	parts[1] = &args->tail;
	io_func  = args->write ? dc_array_write : dc_array_read;

	rc = tse_task_register_comp_cb(task, dfs_io_split_cb, &args, sizeof(args));
	if (rc != 0)
		D_GOTO(err, rc);
	cb_registered = true;

	for (i = 0; i < 2; i++) {
		struct dfs_io_part *part = parts[i];
		tse_task_t         *io_task;
		daos_array_io_t    *io_arg;

		if (!part->dip_active)
			continue;

		rc = dc_task_create(io_func, sched, NULL, &io_task);
		if (rc != 0)
			D_GOTO(err, rc);

		io_arg      = dc_task_get_args(io_task);
		io_arg->oh  = part->dip_oh;
		io_arg->th  = args->write ? DAOS_TX_NONE : args->dfs->th;
		io_arg->iod = &part->dip_iod;
		io_arg->sgl = &part->dip_sgl;

		rc = tse_task_register_deps(task, 1, &io_task);
		if (rc != 0) {
			tse_task_complete(io_task, rc);
			D_GOTO(err, rc);
		}
		tse_task_list_add(io_task, &io_list);
	}

	tse_task_list_sched(&io_list, true);
	return 0;

err:
	D_ERROR("Failed to set up split IO sub-tasks: " DF_RC "\n", DP_RC(rc));
	tse_task_list_abort(&io_list, rc);
	if (!cb_registered) {
		dfs_io_part_free(&args->head);
		dfs_io_part_free(&args->tail);
		D_FREE(args);
	}
	tse_task_complete(task, rc);
	return rc;
}

/*
 * Issue a logical IO that straddles split_off by splitting it into a head and a tail array
 * operation that run concurrently. A parent task (bound to \a ev, or to a private event when \a ev
 * is NULL) tracks both sub-tasks and completes the event only after both finish. On the
 * asynchronous path this returns once the operations are submitted; on the synchronous path (no
 * user event) it blocks until both sub-tasks complete before returning. Returns an errno.
 */
static int
dfs_io_split(dfs_t *dfs, dfs_obj_t *obj, daos_range_t *rgs, uint32_t rg_nr, d_sg_list_t *sgl,
	     bool write, daos_size_t buf_size, daos_size_t *read_size, daos_event_t *ev)
{
	struct dfs_io_split_args *args;
	tse_task_t               *task = NULL;
	int                       rc;

	D_ALLOC_PTR(args);
	if (args == NULL)
		return ENOMEM;

	args->dfs       = dfs;
	args->read_size = read_size;
	args->buf_size  = buf_size;
	args->write     = write;

	rc = dfs_io_build_parts(obj, rgs, rg_nr, sgl, &args->head, &args->tail);
	if (rc != 0) {
		D_FREE(args);
		return daos_der2errno(rc);
	}

	if (ev != NULL)
		daos_event_errno_rc(ev);

	rc = dc_task_create(dfs_io_split_task, NULL, ev, &task);
	if (rc != 0) {
		D_ERROR("Failed to create split IO task: " DF_RC "\n", DP_RC(rc));
		dfs_io_part_free(&args->head);
		dfs_io_part_free(&args->tail);
		D_FREE(args);
		return daos_der2errno(rc);
	}

	daos_task_set_priv(task, args);

	/*
	 * dc_task_schedule() runs the parent body instantly. For the asynchronous case it returns
	 * after the sub-tasks are submitted and the user event tracks their completion; for the
	 * synchronous case (private event) it blocks until both sub-tasks complete before
	 * returning.
	 */
	rc = dc_task_schedule(task, true);
	return daos_der2errno(rc);
}

enum dfs_io_loc {
	DFS_IO_HEAD,
	DFS_IO_TAIL,
	DFS_IO_SPLIT,
};

/** Classify where a set of logical ranges land relative to split_off. */
static enum dfs_io_loc
classify_ranges(daos_range_t *rgs, uint32_t rg_nr, daos_size_t split)
{
	bool     head = false;
	bool     tail = false;
	uint32_t i;

	for (i = 0; i < rg_nr; i++) {
		if (rgs[i].rg_len == 0)
			continue;
		if (rgs[i].rg_idx < split)
			head = true;
		if (rgs[i].rg_idx + rgs[i].rg_len > split)
			tail = true;
		if (head && tail)
			return DFS_IO_SPLIT;
	}
	return tail ? DFS_IO_TAIL : DFS_IO_HEAD;
}

/*
 * Reject ranges whose [rg_idx, rg_idx + rg_len) span wraps past the 64-bit offset space. Such input
 * is invalid and would make the PL split classification (classify_ranges) and the head/tail length
 * math (dfs_io_build_parts) operate on wrapped offsets, misrouting the IO. dfs_punch() applies the
 * same guard to offset + len.
 */
static bool
pl_ranges_valid(daos_range_t *rgs, uint32_t rg_nr)
{
	uint32_t i;

	for (i = 0; i < rg_nr; i++) {
		if (rgs[i].rg_idx + rgs[i].rg_len < rgs[i].rg_idx)
			return false;
	}
	return true;
}

struct dfs_read_params {
	dfs_t           *dfs;
	daos_size_t     *read_size;
	daos_array_iod_t arr_iod;
	daos_range_t     rg;
	daos_range_t    *rgs_owned;
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
	D_FREE(params->rgs_owned);
	D_FREE(params);
	return rc;
}

/*
 * Asynchronous read of one array object (\a oh) over \a rg_nr ranges. When \a own_rgs is true, \a
 * rgs is a heap buffer whose ownership is transferred here and freed once the read completes (used
 * for the reindexed PL tail ranges). When \a own_rgs is false, the ranges are not owned: a single
 * range is copied into the params (so a stack range may be passed), while multiple ranges are
 * referenced in place and the caller must keep them alive until completion (e.g. the user-supplied
 * dfs_readx ranges). This avoids an extra allocation/copy on the common read paths.
 */
static int
dfs_read_int(dfs_t *dfs, daos_handle_t oh, daos_range_t *rgs, uint32_t rg_nr, bool own_rgs,
	     d_sg_list_t *sgl, daos_size_t *read_size, daos_event_t *ev)
{
	tse_task_t             *task = NULL;
	daos_array_io_t        *args;
	struct dfs_read_params *params;
	int                     rc;

	D_ASSERT(ev);
	daos_event_errno_rc(ev);

	rc = dc_task_create(dc_array_read, NULL, ev, &task);
	if (rc != 0) {
		if (own_rgs)
			D_FREE(rgs);
		return daos_der2errno(rc);
	}

	D_ALLOC_PTR(params);
	if (params == NULL)
		D_GOTO(err_task, rc = -DER_NOMEM);

	params->dfs            = dfs;
	params->read_size      = read_size;
	params->arr_iod.arr_nr = rg_nr;
	if (own_rgs) {
		/** take ownership of the caller's heap ranges; freed by read_cb at completion */
		params->rgs_owned       = rgs;
		params->arr_iod.arr_rgs = rgs;
	} else if (rg_nr == 1) {
		/** copy the single (possibly stack) range into the embedded storage */
		params->rg              = rgs[0];
		params->arr_iod.arr_rgs = &params->rg;
	} else {
		/** reference the caller-owned ranges in place; they persist for the async op */
		params->arr_iod.arr_rgs = rgs;
	}

	args      = dc_task_get_args(task);
	args->oh  = oh;
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
	/** read_cb was never registered, so free the owned ranges (if any) here */
	if (own_rgs)
		D_FREE(rgs);
	tse_task_complete(task, rc);
	/** the event is completed with the proper rc */
	return 0;
}

/*
 * Async head-only read of a progressive-layout file. The head array can only short-read against its
 * OWN EOF; it cannot know the tail holds data. So a short head read may actually be an interior
 * hole of the logical file (tail non-empty) that must read back as zeros and be counted, or it may
 * be the genuine EOF (tail empty) where the buffer past EOF must be left untouched (POSIX). This
 * issues the head read, then conditionally a tail get_size to tell an interior hole from EOF; the
 * head size needed to place the per-range zeros is the one the read already reported via
 * arr_array_size, so no separate head get_size is required. The tail get_size is gated on an actual
 * short read, so a full-data read pays nothing. The two tasks form a linear chain:
 *   read_task (head read) -> size_task (tail get_size, user-facing).
 */
struct dfs_pl_head_read_params {
	dfs_t           *dfs;
	dfs_obj_t       *obj;
	d_sg_list_t     *sgl;
	daos_size_t     *read_size;
	daos_size_t      buf_size;
	daos_size_t      tail_size;
	daos_array_iod_t arr_iod; /* head read iod; arr_nr_read/arr_array_size filled by the read */
	daos_range_t     rg;      /* embedded storage for the single-range case */
	daos_range_t    *rgs_owned; /* heap ranges whose ownership was transferred here, if any */
};

/* Record the read count, bump stats/metrics, and release the params. */
static void
pl_head_read_finalize(struct dfs_pl_head_read_params *p, daos_size_t nr_read)
{
	*p->read_size = nr_read;
	DFS_OP_STAT_INCR(p->dfs, DOS_READ);
	dfs_update_file_metrics(p->dfs, nr_read, 0);
}

/* Completion cb on the tail get_size; only registered for a possible short read. */
static int
pl_head_size_comp_cb(tse_task_t *task, void *data)
{
	struct dfs_pl_head_read_params *p  = daos_task_get_priv(task);
	int                             rc = task->dt_result;

	if (rc != 0) {
		D_ERROR("Failed to get tail size: " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (p->tail_size == 0) {
		/* genuine EOF: leave the buffer past EOF untouched (POSIX), keep the short count */
		pl_head_read_finalize(p, p->arr_iod.arr_nr_read);
	} else {
		/*
		 * The tail holds data, so every byte the head left untouched past its own EOF is an
		 * interior hole of the logical file. Zero each range's hole in place using the head
		 * array size the read already reported (ranges may be unsorted, so a hole can map
		 * anywhere in the SGL) and count the full requested length as read.
		 */
		sgl_zero_head_holes(p->sgl, p->arr_iod.arr_rgs, p->arr_iod.arr_nr,
				    p->arr_iod.arr_array_size);
		pl_head_read_finalize(p, p->buf_size);
	}
out:
	D_FREE(p->rgs_owned);
	D_FREE(p);
	return rc;
}

/*
 * Prep cb on the user-facing tail get_size task, gated behind the head read. It resolves a short
 * head read: a full read finalizes here and short-circuits the tail get_size body; a short read
 * arms the tail get_size so pl_head_size_comp_cb can tell an interior hole (tail has data, zero the
 * gaps) from the genuine EOF (tail empty, leave the buffer untouched per POSIX). The head array
 * size needed to place the zeros was already reported by the read (arr_array_size), so no separate
 * head get_size is issued.
 */
static int
pl_head_size_prep_cb(tse_task_t *task, void *data)
{
	struct dfs_pl_head_read_params *p = daos_task_get_priv(task);
	daos_array_get_size_t          *args;
	int                             rc = task->dt_result;

	if (rc != 0) {
		D_ERROR("Failed to read from head array object: " DF_RC "\n", DP_RC(rc));
		D_GOTO(done, rc);
	}

	if (p->arr_iod.arr_nr_read >= p->buf_size) {
		/* head satisfied the whole read; skip the tail lookup */
		pl_head_read_finalize(p, p->arr_iod.arr_nr_read);
		D_GOTO(done, rc = 0);
	}

	/* short head read: look up the tail size so the comp cb can classify the gap */
	rc = tse_task_register_comp_cb(task, pl_head_size_comp_cb, NULL, 0);
	if (rc != 0) {
		D_ERROR("Failed to register tail size comp cb: " DF_RC "\n", DP_RC(rc));
		D_GOTO(done, rc);
	}
	args       = daos_task_get_args(task);
	args->oh   = p->obj->f.tail_oh;
	args->th   = p->dfs->th;
	args->size = &p->tail_size;
	return 0;

done:
	D_FREE(p->rgs_owned);
	D_FREE(p);
	tse_task_complete(task, rc);
	return rc;
}

static int
dfs_pl_head_read_int(dfs_t *dfs, dfs_obj_t *obj, daos_range_t *rgs, uint32_t rg_nr, bool own_rgs,
		     d_sg_list_t *sgl, daos_size_t buf_size, daos_size_t *read_size,
		     daos_event_t *ev)
{
	tse_task_t                     *size_task = NULL;
	tse_task_t                     *read_task = NULL;
	daos_array_io_t                *rargs;
	struct dfs_pl_head_read_params *params;
	tse_sched_t                    *sched;
	int                             rc;

	D_ASSERT(ev);
	daos_event_errno_rc(ev);

	/*
	 * size_task is the user-facing task (bound to ev); it doubles as the conditional tail
	 * get_size used to classify a short head read. Its body only runs when the prep cb leaves
	 * it armed (the head short-read; the tail size then tells an interior hole from EOF).
	 */
	rc = dc_task_create(dc_array_get_size, NULL, ev, &size_task);
	if (rc != 0) {
		D_ERROR("Failed to create tail get_size task: " DF_RC "\n", DP_RC(rc));
		if (own_rgs)
			D_FREE(rgs);
		return daos_der2errno(rc);
	}
	sched = tse_task2sched(size_task);

	D_ALLOC_PTR(params);
	if (params == NULL)
		D_GOTO(err_size, rc = -DER_NOMEM);
	params->dfs            = dfs;
	params->obj            = obj;
	params->sgl            = sgl;
	params->read_size      = read_size;
	params->buf_size       = buf_size;
	params->arr_iod.arr_nr = rg_nr;
	if (own_rgs) {
		/** take ownership of the caller's heap ranges; freed by the cbs at completion */
		params->rgs_owned       = rgs;
		params->arr_iod.arr_rgs = rgs;
	} else if (rg_nr == 1) {
		/** copy the single (possibly stack) range into the embedded storage */
		params->rg              = rgs[0];
		params->arr_iod.arr_rgs = &params->rg;
	} else {
		/** reference the caller-owned ranges in place; they persist for the async op */
		params->arr_iod.arr_rgs = rgs;
	}

	/** child sub-task: the actual head array read */
	rc = dc_task_create(dc_array_read, sched, NULL, &read_task);
	if (rc != 0) {
		D_ERROR("Failed to create head read task: " DF_RC "\n", DP_RC(rc));
		D_GOTO(err_params, rc);
	}
	rargs      = dc_task_get_args(read_task);
	rargs->oh  = obj->oh;
	rargs->th  = dfs->th;
	rargs->sgl = sgl;
	rargs->iod = &params->arr_iod;

	/** chain the tasks: read -> tail get_size (user-facing) */
	rc = tse_task_register_deps(size_task, 1, &read_task);
	if (rc != 0) {
		D_ERROR("Failed to register head read dependency: " DF_RC "\n", DP_RC(rc));
		D_GOTO(err_read, rc);
	}

	daos_task_set_priv(size_task, params);
	rc = tse_task_register_cbs(size_task, pl_head_size_prep_cb, NULL, 0, NULL, NULL, 0);
	if (rc != 0) {
		D_ERROR("Failed to register head read prep cb: " DF_RC "\n", DP_RC(rc));
		D_GOTO(err_read, rc);
	}

	/*
	 * Schedule the parent before the child (the size_task waits on its dep). dc_task_schedule()
	 * completes a task even on error, driving the registered cbs (which free params), so the rc
	 * is handled through the normal completion path and can be ignored here.
	 */
	dc_task_schedule(size_task, true);
	dc_task_schedule(read_task, true);
	return 0;

err_read:
	tse_task_complete(read_task, rc);
err_params:
	D_FREE(params);
err_size:
	if (own_rgs)
		D_FREE(rgs);
	tse_task_complete(size_task, rc);
	/** the event is completed with the proper rc */
	return 0;
}

int
dfs_read(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t *sgl, daos_off_t off, daos_size_t *read_size,
	 daos_event_t *ev)
{
	daos_handle_t oh;
	daos_off_t    aoff;
	daos_size_t   buf_size;
	bool          pl_head = false;
	int           i, rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if (sgl == NULL)
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

	oh   = obj->oh;
	aoff = off;
	if (obj->f.has_tail) {
		daos_range_t    rg = {.rg_idx = off, .rg_len = buf_size};
		enum dfs_io_loc loc;

		if (!pl_ranges_valid(&rg, 1))
			return EINVAL;
		loc = classify_ranges(&rg, 1, obj->f.split_off);

		if (loc == DFS_IO_SPLIT)
			return dfs_io_split(dfs, obj, &rg, 1, sgl, false, 0, read_size, ev);
		if (loc == DFS_IO_TAIL) {
			oh   = obj->f.tail_oh;
			aoff = off - obj->f.split_off;
		} else {
			/** head-only read of a PL file: may need tail-aware hole handling */
			pl_head = true;
		}
	}

	if (ev == NULL) {
		daos_array_iod_t iod;
		daos_range_t     rg;

		/** set array location */
		iod.arr_nr  = 1;
		rg.rg_len   = buf_size;
		rg.rg_idx   = aoff;
		iod.arr_rgs = &rg;

		rc = daos_array_read(oh, dfs->th, &iod, sgl, NULL);
		if (rc) {
			D_ERROR("daos_array_read() failed, " DF_RC "\n", DP_RC(rc));
			return daos_der2errno(rc);
		}

		/*
		 * A short head read past the head's own EOF is an interior hole of the logical file
		 * when the tail has data; zero the gap and count it as read.
		 */
		if (pl_head && iod.arr_nr_read < buf_size) {
			rc = dfs_pl_head_short_read(dfs, obj, sgl, &rg, 1, buf_size,
						    iod.arr_array_size, &iod.arr_nr_read);
			if (rc) {
				D_ERROR("tail size lookup failed, " DF_RC "\n", DP_RC(rc));
				return daos_der2errno(rc);
			}
		}

		DFS_OP_STAT_INCR(dfs, DOS_READ);
		*read_size = iod.arr_nr_read;
		dfs_update_file_metrics(dfs, iod.arr_nr_read, 0);
		return 0;
	}

	{
		daos_range_t rg = {.rg_idx = aoff, .rg_len = buf_size};

		if (pl_head)
			return dfs_pl_head_read_int(dfs, obj, &rg, 1, false, sgl, buf_size,
						    read_size, ev);
		return dfs_read_int(dfs, oh, &rg, 1, false, sgl, read_size, ev);
	}
}

int
dfs_readx(dfs_t *dfs, dfs_obj_t *obj, dfs_iod_t *iod, d_sg_list_t *sgl, daos_size_t *read_size,
	  daos_event_t *ev)
{
	daos_handle_t oh        = DAOS_HDL_INVAL;
	daos_range_t *rgs       = NULL;
	uint32_t      rg_nr     = 0;
	bool          rgs_alloc = false;
	bool          pl_head   = false;
	int           rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if (iod == NULL)
		return EINVAL;
	if (sgl == NULL)
		return EINVAL;
	if (read_size == NULL)
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_WRONLY)
		return EPERM;

	if (iod->iod_nr == 0) {
		*read_size = 0;
		if (ev) {
			daos_event_launch(ev);
			daos_event_complete(ev, 0);
		}
		DFS_OP_STAT_INCR(dfs, DOS_READ);
		return 0;
	}

	oh    = obj->oh;
	rgs   = iod->iod_rgs;
	rg_nr = iod->iod_nr;
	if (obj->f.has_tail) {
		enum dfs_io_loc loc;

		if (!pl_ranges_valid(iod->iod_rgs, iod->iod_nr))
			return EINVAL;
		loc = classify_ranges(iod->iod_rgs, iod->iod_nr, obj->f.split_off);

		if (loc == DFS_IO_SPLIT)
			return dfs_io_split(dfs, obj, iod->iod_rgs, iod->iod_nr, sgl, false, 0,
					    read_size, ev);
		if (loc == DFS_IO_TAIL) {
			uint32_t j;

			D_ALLOC_ARRAY(rgs, iod->iod_nr);
			if (rgs == NULL)
				return ENOMEM;
			for (j = 0; j < iod->iod_nr; j++) {
				rgs[j].rg_idx = iod->iod_rgs[j].rg_idx - obj->f.split_off;
				rgs[j].rg_len = iod->iod_rgs[j].rg_len;
			}
			oh = obj->f.tail_oh;
			/** free in completion cb */
			rgs_alloc = true;
		} else {
			/** head-only read of a PL file: may need tail-aware hole handling */
			pl_head = true;
		}
	}

	if (ev == NULL) {
		daos_array_iod_t arr_iod;

		/** set array location */
		arr_iod.arr_nr  = rg_nr;
		arr_iod.arr_rgs = rgs;

		rc = daos_array_read(oh, dfs->th, &arr_iod, sgl, NULL);
		if (rc) {
			D_ERROR("daos_array_read() failed (%d)\n", rc);
			if (rgs_alloc)
				D_FREE(rgs);
			return daos_der2errno(rc);
		}

		/*
		 * A short head read past the head's own EOF is an interior hole of the logical file
		 * when the tail has data; zero the gap and count it as read.
		 */
		if (pl_head) {
			daos_size_t buf_size = 0;
			uint32_t    j;

			for (j = 0; j < rg_nr; j++)
				buf_size += rgs[j].rg_len;
			if (arr_iod.arr_nr_read < buf_size) {
				rc = dfs_pl_head_short_read(dfs, obj, sgl, rgs, rg_nr, buf_size,
							    arr_iod.arr_array_size,
							    &arr_iod.arr_nr_read);
				if (rc) {
					D_ERROR("tail size lookup failed, " DF_RC "\n", DP_RC(rc));
					return daos_der2errno(rc);
				}
			}
		}

		DFS_OP_STAT_INCR(dfs, DOS_READ);
		*read_size = arr_iod.arr_nr_read;
		dfs_update_file_metrics(dfs, arr_iod.arr_nr_read, 0);
		if (rgs_alloc)
			D_FREE(rgs);
		return 0;
	}

	if (pl_head) {
		daos_size_t buf_size = 0;
		uint32_t    j;

		for (j = 0; j < rg_nr; j++)
			buf_size += rgs[j].rg_len;
		return dfs_pl_head_read_int(dfs, obj, rgs, rg_nr, rgs_alloc, sgl, buf_size,
					    read_size, ev);
	}

	return dfs_read_int(dfs, oh, rgs, rg_nr, rgs_alloc, sgl, read_size, ev);
}

int
dfs_write(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t *sgl, daos_off_t off, daos_event_t *ev)
{
	daos_array_iod_t iod;
	daos_range_t     rg;
	daos_handle_t    oh;
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

	oh = obj->oh;
	if (obj->f.has_tail) {
		enum dfs_io_loc loc;

		if (!pl_ranges_valid(&rg, 1))
			return EINVAL;
		loc = classify_ranges(&rg, 1, obj->f.split_off);

		if (loc == DFS_IO_SPLIT)
			return dfs_io_split(dfs, obj, &rg, 1, sgl, true, buf_size, NULL, ev);
		if (loc == DFS_IO_TAIL) {
			oh        = obj->f.tail_oh;
			rg.rg_idx = off - obj->f.split_off;
		}
	}

	if (ev)
		daos_event_errno_rc(ev);

	rc = daos_array_write(oh, DAOS_TX_NONE, &iod, sgl, ev);
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
	daos_handle_t    oh        = DAOS_HDL_INVAL;
	daos_range_t    *rgs       = NULL;
	bool             rgs_alloc = false;
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

	buf_size = 0;
	if (dfs->metrics != NULL && sgl != NULL)
		for (i = 0; i < sgl->sg_nr; i++)
			buf_size += sgl->sg_iovs[i].iov_len;

	oh  = obj->oh;
	rgs = iod->iod_rgs;
	if (obj->f.has_tail) {
		enum dfs_io_loc loc;

		if (!pl_ranges_valid(iod->iod_rgs, iod->iod_nr))
			return EINVAL;
		loc = classify_ranges(iod->iod_rgs, iod->iod_nr, obj->f.split_off);

		if (loc == DFS_IO_SPLIT)
			return dfs_io_split(dfs, obj, iod->iod_rgs, iod->iod_nr, sgl, true,
					    buf_size, NULL, ev);
		if (loc == DFS_IO_TAIL) {
			uint32_t j;

			D_ALLOC_ARRAY(rgs, iod->iod_nr);
			if (rgs == NULL)
				return ENOMEM;
			for (j = 0; j < iod->iod_nr; j++) {
				rgs[j].rg_idx = iod->iod_rgs[j].rg_idx - obj->f.split_off;
				rgs[j].rg_len = iod->iod_rgs[j].rg_len;
			}
			oh        = obj->f.tail_oh;
			rgs_alloc = true;
		}
	}

	/** set array location */
	arr_iod.arr_nr  = iod->iod_nr;
	arr_iod.arr_rgs = rgs;

	if (ev)
		daos_event_errno_rc(ev);

	/*
	 * The array layer consumes the range array synchronously while issuing the write, so the
	 * reindexed tail ranges (when allocated) can be freed as soon as daos_array_write() returns
	 * even for the asynchronous case.
	 */
	rc = daos_array_write(oh, DAOS_TX_NONE, &arr_iod, sgl, ev);
	if (rc == 0) {
		DFS_OP_STAT_INCR(dfs, DOS_WRITE);
		dfs_update_file_metrics(dfs, 0, buf_size);
	} else {
		D_ERROR("daos_array_write() failed (%d)\n", rc);
	}

	if (rgs_alloc)
		D_FREE(rgs);

	return daos_der2errno(rc);
}
