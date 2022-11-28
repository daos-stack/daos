/**
 * (C) Copyright 2015-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "dav_internal.h"
#include "memops.h"
#include "tx.h"

static void
clogs_extend_free(struct ulog *redo)
{
	D_FREE(redo);
}

static int
clogs_extend_redo(struct ulog **redo, uint64_t gen_num)
{
	size_t size = SIZEOF_ALIGNED_ULOG(LANE_REDO_EXTERNAL_SIZE);

	D_ALIGNED_ALLOC_NZ(*redo, CACHELINE_SIZE, size);
	if (*redo == NULL)
		return -1;

	size_t capacity = ALIGN_DOWN(size - sizeof(struct ulog), CACHELINE_SIZE);

	ulog_construct_new(*redo, capacity, gen_num, 0);
	return 0;
}

static int
clogs_extend_undo(struct ulog **undo, uint64_t gen_num)
{
	size_t size = TX_DEFAULT_RANGE_CACHE_SIZE;

	D_ALIGNED_ALLOC_NZ(*undo, CACHELINE_SIZE, size);
	if (*undo == NULL)
		return -1;

	size_t capacity = ALIGN_DOWN(size - sizeof(struct ulog), CACHELINE_SIZE);

	ulog_construct_new(*undo, capacity, gen_num, 0);
	return 0;
}

int
dav_create_clogs(dav_obj_t *hdl)
{

	ulog_construct_new((struct ulog *)&hdl->clogs.external,
		LANE_REDO_EXTERNAL_SIZE, 0, 0);
	ulog_construct_new((struct ulog *)&hdl->clogs.undo,
		LANE_UNDO_SIZE, 0, 0);

	hdl->external = operation_new((struct ulog *)&hdl->clogs.external,
		LANE_REDO_EXTERNAL_SIZE, clogs_extend_redo, clogs_extend_free,
		&hdl->p_ops, LOG_TYPE_REDO);
	if (hdl->external == NULL)
		return -1;
	hdl->undo = operation_new((struct ulog *)&hdl->clogs.undo,
		LANE_UNDO_SIZE, clogs_extend_undo, clogs_extend_free,
		&hdl->p_ops, LOG_TYPE_UNDO);
	if (hdl->undo == NULL) {
		operation_delete(hdl->external);
		return -1;
	}
	return 0;
}

void
dav_destroy_clogs(dav_obj_t *hdl)
{
	operation_free_logs(hdl->external);
	operation_delete(hdl->external);
	operation_free_logs(hdl->undo);
	operation_delete(hdl->undo);
}

int
dav_hold_clogs(dav_obj_t *hdl)
{
	if (hdl->nested_tx++ == 0) {
		operation_init(hdl->external);
		operation_init(hdl->undo);
	}
	return 0;
}

int
dav_release_clogs(dav_obj_t *hdl)
{
	if (hdl->nested_tx == 0)
		FATAL("release clogs");
	--hdl->nested_tx;
	return 0;
}
