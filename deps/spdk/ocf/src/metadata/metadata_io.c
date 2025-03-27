/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "metadata.h"
#include "metadata_io.h"
#include "../ocf_priv.h"
#include "../engine/cache_engine.h"
#include "../engine/engine_common.h"
#include "../engine/engine_bf.h"
#include "../utils/utils_cache_line.h"
#include "../utils/utils_io.h"
#include "../ocf_request.h"
#include "../ocf_def_priv.h"
#include "../concurrency/ocf_mio_concurrency.h"

#define OCF_METADATA_IO_DEBUG 0

#if 1 == OCF_METADATA_IO_DEBUG
#define OCF_DEBUG_TRACE(cache) \
	ocf_cache_log(cache, log_info, "[Metadata][IO] %s\n", __func__)

#define OCF_DEBUG_MSG(cache, msg) \
	ocf_cache_log(cache, log_info, "[Metadata][IO] %s - %s\n", \
			__func__, msg)

#define OCF_DEBUG_PARAM(cache, format, ...) \
	ocf_cache_log(cache, log_info, "[Metadata][IO] %s - "format"\n", \
			__func__, ##__VA_ARGS__)
#else
#define OCF_DEBUG_TRACE(cache)
#define OCF_DEBUG_MSG(cache, msg)
#define OCF_DEBUG_PARAM(cache, format, ...)
#endif

struct metadata_io_read_i_atomic_context {
	struct ocf_request *req;
	ctx_data_t *data;
	ocf_cache_t cache;
	uint64_t count;
	uint64_t curr_offset;
	uint64_t curr_count;

	ocf_metadata_atomic_io_event_t drain_hndl;
	ocf_metadata_io_end_t compl_hndl;
	void *priv;
};

enum ocf_mio_size {
	ocf_mio_size_1 = 0,
	ocf_mio_size_2,
	ocf_mio_size_4,
	ocf_mio_size_8,
	ocf_mio_size_16,
	ocf_mio_size_32,
	ocf_mio_size_64,
	ocf_mio_size_max,
};

#define METADATA_IO_REQS_LIMIT 128

static void metadata_io_read_i_atomic_complete(
		struct metadata_io_read_i_atomic_context *context, int error)
{
	context->compl_hndl(context->cache, context->priv, error);

	ctx_data_free(context->cache->owner, context->data);
	ocf_req_put(context->req);
	env_vfree(context);
}

/*
 * Iterative read end callback
 */
static void metadata_io_read_i_atomic_step_end(struct ocf_io *io, int error)
{
	struct metadata_io_read_i_atomic_context *context = io->priv1;

	OCF_DEBUG_TRACE(ocf_volume_get_cache(ocf_io_get_volume(io)));

	ocf_io_put(io);

	if (error) {
		metadata_io_read_i_atomic_complete(context, error);
		return;
	}

	context->drain_hndl(context->priv, context->curr_offset,
			context->curr_count, context->data);

	context->count -= context->curr_count;
	context->curr_offset += context->curr_count;

	if (context->count > 0)
		ocf_engine_push_req_front(context->req, true);
	else
		metadata_io_read_i_atomic_complete(context, 0);
}

int metadata_io_read_i_atomic_step(struct ocf_request *req)
{
	struct metadata_io_read_i_atomic_context *context = req->priv;
	ocf_cache_t cache = context->cache;
	uint64_t max_sectors_count = PAGE_SIZE / OCF_ATOMIC_METADATA_SIZE;
	struct ocf_io *io;
	int result = 0;

	/* Get sectors count of this IO iteration */
	context->curr_count = OCF_MIN(max_sectors_count, context->count);

	/* Reset position in data buffer */
	ctx_data_seek(cache->owner, context->data, ctx_data_seek_begin, 0);

	/* Allocate new IO */
	io = ocf_new_cache_io(cache, req->io_queue,
			cache->device->metadata_offset +
			SECTORS_TO_BYTES(context->curr_offset),
			SECTORS_TO_BYTES(context->curr_count), OCF_READ, 0, 0);

	if (!io) {
		metadata_io_read_i_atomic_complete(context, -OCF_ERR_NO_MEM);
		return 0;
	}

	/* Setup IO */
	ocf_io_set_cmpl(io, context, NULL, metadata_io_read_i_atomic_step_end);
	result = ocf_io_set_data(io, context->data, 0);
	if (result) {
		ocf_io_put(io);
		metadata_io_read_i_atomic_complete(context, result);
		return 0;
	}

	/* Submit IO */
	ocf_volume_submit_metadata(io);

	return 0;
}

static const struct ocf_io_if _io_if_metadata_io_read_i_atomic_step = {
	.read = metadata_io_read_i_atomic_step,
	.write = metadata_io_read_i_atomic_step,
};

/*
 * Iterative read request
 */
int metadata_io_read_i_atomic(ocf_cache_t cache, ocf_queue_t queue, void *priv,
		ocf_metadata_atomic_io_event_t drain_hndl,
		ocf_metadata_io_end_t compl_hndl)
{
	struct metadata_io_read_i_atomic_context *context;
	uint64_t io_sectors_count = cache->device->collision_table_entries *
					ocf_line_sectors(cache);

	OCF_DEBUG_TRACE(cache);

	context = env_vzalloc(sizeof(*context));
	if (!context)
		return -OCF_ERR_NO_MEM;

	context->req = ocf_req_new(queue, NULL, 0, 0, 0);
	if (!context->req) {
		env_vfree(context);
		return -OCF_ERR_NO_MEM;
	}

	context->req->info.internal = true;
	context->req->io_if = &_io_if_metadata_io_read_i_atomic_step;
	context->req->priv = context;

	/* Allocate one 4k page for metadata*/
	context->data = ctx_data_alloc(cache->owner, 1);
	if (!context->data) {
		ocf_req_put(context->req);
		env_vfree(context);
		return -OCF_ERR_NO_MEM;
	}

	context->cache = cache;
	context->count = io_sectors_count;
	context->curr_offset = 0;
	context->curr_count = 0;
	context->drain_hndl = drain_hndl;
	context->compl_hndl = compl_hndl;
	context->priv = priv;

	ocf_engine_push_req_front(context->req, true);

	return 0;
}

static void metadata_io_req_fill(struct metadata_io_request *m_req)
{
	ocf_cache_t cache = m_req->cache;
	struct metadata_io_request_asynch *a_req = m_req->asynch;
	int i;

	for (i = 0; i < m_req->count; i++) {
		a_req->on_meta_fill(cache, m_req->data,
			m_req->page + i, m_req->context);
	}
}

static void metadata_io_req_drain(struct metadata_io_request *m_req)
{
	ocf_cache_t cache = m_req->cache;
	struct metadata_io_request_asynch *a_req = m_req->asynch;
	int i;

	for (i = 0; i < m_req->count; i++) {
		a_req->on_meta_drain(cache, m_req->data,
			m_req->page + i, m_req->context);
	}
}

static void metadata_io_io_end(struct metadata_io_request *m_req, int error);

static void metadata_io_io_cmpl(struct ocf_io *io, int error)
{
	metadata_io_io_end(io->priv1, error);
	ocf_io_put(io);
}

static int metadata_io_do(struct ocf_request *req)
{
	struct metadata_io_request *m_req = req->priv;
	ocf_cache_t cache = req->cache;
	struct ocf_io *io;
	int ret;

	/* Fill with the latest metadata. */
	if (m_req->req.rw == OCF_WRITE) {
		ocf_metadata_start_shared_access(&cache->metadata.lock,
				m_req->page % OCF_NUM_GLOBAL_META_LOCKS);
		metadata_io_req_fill(m_req);
		ocf_metadata_end_shared_access(&cache->metadata.lock,
				 m_req->page % OCF_NUM_GLOBAL_META_LOCKS);
	}

	io = ocf_new_cache_io(cache, req->io_queue,
			PAGES_TO_BYTES(m_req->page),
			PAGES_TO_BYTES(m_req->count),
			m_req->req.rw, 0, m_req->asynch->flags);
	if (!io) {
		metadata_io_io_end(m_req, -OCF_ERR_NO_MEM);
		return 0;
	}

	/* Setup IO */
	ocf_io_set_cmpl(io, m_req, NULL, metadata_io_io_cmpl);
	ctx_data_seek(cache->owner, m_req->data, ctx_data_seek_begin, 0);
	ret = ocf_io_set_data(io, m_req->data, 0);
	if (ret) {
		ocf_io_put(io);
		metadata_io_io_end(m_req, ret);
		return ret;
	}
	ocf_volume_submit_io(io);
	return 0;
}

static struct ocf_io_if metadata_io_do_if = {
	.read = metadata_io_do,
	.write = metadata_io_do,
};

void metadata_io_req_finalize(struct metadata_io_request *m_req)
{
	struct metadata_io_request_asynch *a_req = m_req->asynch;

	if (env_atomic_dec_return(&a_req->req_active) == 0)
		env_mpool_del(m_req->cache->owner->resources.mio, a_req,
				a_req->alloc_req_count);
}

static void metadata_io_page_lock_acquired(struct ocf_request *req)
{
	ocf_engine_push_req_front(req, true);
}

static int metadata_io_restart_req(struct ocf_request *req)
{
	struct metadata_io_request *m_req = req->priv;
	struct metadata_io_request_asynch *a_req = m_req->asynch;
	int lock;

	m_req->req.io_if = &metadata_io_do_if;

	if (!a_req->mio_conc) {
		metadata_io_do(&m_req->req);
		return 0;
	}

	lock = ocf_mio_async_lock(a_req->mio_conc, m_req,
		metadata_io_page_lock_acquired);

	if (lock < 0) {
		a_req->error = lock;
		metadata_io_req_finalize(m_req);
		return 0;
	}

	if (lock == OCF_LOCK_ACQUIRED)
		metadata_io_do(&m_req->req);

	return 0;
}

static struct ocf_io_if metadata_io_restart_if = {
	.read = metadata_io_restart_req,
	.write = metadata_io_restart_req,
};

static void  metadata_io_req_advance(struct metadata_io_request *m_req);

/*
 * Iterative asynchronous write callback
 */
static void metadata_io_io_end(struct metadata_io_request *m_req, int error)
{
	struct metadata_io_request_asynch *a_req = m_req->asynch;

	OCF_CHECK_NULL(a_req);
	OCF_CHECK_NULL(a_req->on_complete);

	if (error) {
		a_req->error = a_req->error ?: error;
	} else {
		if (m_req->req.rw == OCF_READ)
			metadata_io_req_drain(m_req);
	}

	OCF_DEBUG_PARAM(cache, "Page = %u", m_req->page);

	if (a_req->mio_conc)
		ocf_mio_async_unlock(a_req->mio_conc, m_req);

	metadata_io_req_advance(m_req);

	metadata_io_req_complete(m_req);
}

void metadata_io_req_end(struct metadata_io_request *m_req)
{
	struct metadata_io_request_asynch *a_req = m_req->asynch;
	ocf_cache_t cache = m_req->cache;

	if (env_atomic_dec_return(&a_req->req_remaining) == 0)
		a_req->on_complete(cache, a_req->context, a_req->error);

	ctx_data_free(cache->owner, m_req->data);
}

static uint32_t metadata_io_max_page(ocf_cache_t cache)
{
	uint32_t volume_max_io_pages = ocf_volume_get_max_io_size(
			&cache->device->volume) / PAGE_SIZE;
	struct metadata_io_request *m_req;
	uint32_t request_map_capacity_pages = sizeof(m_req->alock_status) * 8;

	return OCF_MIN(volume_max_io_pages, request_map_capacity_pages);
}

static void metadata_io_req_advance(struct metadata_io_request *m_req)
{
	struct metadata_io_request_asynch *a_req = m_req->asynch;
	uint32_t max_count = metadata_io_max_page(m_req->cache);
	uint32_t curr;

	if (a_req->error) {
		metadata_io_req_end(m_req);
		return;
	}

	curr = env_atomic_inc_return(&a_req->req_current);

	if (curr >= OCF_DIV_ROUND_UP(a_req->count, max_count)) {
		m_req->count = 0;
		metadata_io_req_end(m_req);
		return;
	}

	m_req->page = a_req->page + curr * max_count;
	m_req->count = OCF_MIN(a_req->count - curr * max_count, max_count);
	m_req->req.core_line_count = m_req->count;
}

static void metadata_io_req_start(struct metadata_io_request *m_req)
{
	struct metadata_io_request_asynch *a_req = m_req->asynch;

	env_atomic_inc(&a_req->req_remaining);
	env_atomic_inc(&a_req->req_active);

	metadata_io_req_advance(m_req);

	if (m_req->count == 0) {
		metadata_io_req_finalize(m_req);
		return;
	}

	metadata_io_restart_req(&m_req->req);
}

void metadata_io_req_complete(struct metadata_io_request *m_req)
{
	struct metadata_io_request_asynch *a_req = m_req->asynch;

	if (m_req->count == 0 || a_req->error) {
		metadata_io_req_finalize(m_req);
		return;
	}

	m_req->req.io_if = &metadata_io_restart_if;
	ocf_engine_push_req_front(&m_req->req, true);
}

/*
 * Iterative write request asynchronously
 */
static int metadata_io_i_asynch(ocf_cache_t cache, ocf_queue_t queue, int dir,
		void *context, uint32_t page, uint32_t count, int flags,
		ocf_metadata_io_event_t io_hndl,
		ocf_metadata_io_end_t compl_hndl,
		struct ocf_alock *mio_conc)
{
	struct metadata_io_request_asynch *a_req;
	struct metadata_io_request *m_req;
	uint32_t max_count = metadata_io_max_page(cache);
	uint32_t io_count = OCF_DIV_ROUND_UP(count, max_count);
	uint32_t req_count = OCF_MIN(io_count, METADATA_IO_REQS_LIMIT);
	int i;
	struct env_mpool *mio_allocator = cache->owner->resources.mio;

	if (count == 0)
		return 0;


	a_req = env_mpool_new(mio_allocator, req_count);
	if (!a_req)
		return -OCF_ERR_NO_MEM;

	env_atomic_set(&a_req->req_remaining, 1);
	env_atomic_set(&a_req->req_active, 1);
	env_atomic_set(&a_req->req_current, -1);
	a_req->on_complete = compl_hndl;
	a_req->context = context;
	a_req->page = page;
	a_req->alloc_req_count = req_count;
	a_req->count = count;
	a_req->flags = flags;
	a_req->on_meta_fill = io_hndl;
	a_req->on_meta_drain = io_hndl;
	a_req->mio_conc = mio_conc;

	/* IO Requests initialization */
	for (i = 0; i < req_count; i++) {
		m_req = &a_req->reqs[i];

		m_req->asynch = a_req;
		m_req->cache = cache;
		m_req->context = context;
		m_req->req.io_if = &metadata_io_restart_if;
		m_req->req.io_queue = queue;
		m_req->req.cache = cache;
		m_req->req.priv = m_req;
		m_req->req.info.internal = true;
		m_req->req.rw = dir;
		m_req->req.map = LIST_POISON1;
		m_req->req.alock_status = (uint8_t*)&m_req->alock_status;

		/* If req_count == io_count and count is not multiple of
		 * max_count, for last we can allocate data smaller that
		 * max_count as we are sure it will never be resubmitted.
		 */
		m_req->data = ctx_data_alloc(cache->owner,
				OCF_MIN(max_count, count - i * max_count));
		if (!m_req->data)
			goto err;
	}


	for (i = 0; i < req_count; i++)
		metadata_io_req_start(&a_req->reqs[i]);

	if (env_atomic_dec_return(&a_req->req_remaining) == 0)
		compl_hndl(cache, context, a_req->error);

	if (env_atomic_dec_return(&a_req->req_active) == 0)
		env_mpool_del(mio_allocator, a_req, req_count);

	return 0;

err:
	while (i--)
		ctx_data_free(cache->owner, a_req->reqs[i].data);

	env_mpool_del(mio_allocator, a_req, req_count);

	return -OCF_ERR_NO_MEM;
}

int metadata_io_write_i_asynch(ocf_cache_t cache, ocf_queue_t queue,
		void *context, uint32_t page, uint32_t count, int flags,
		ocf_metadata_io_event_t fill_hndl,
		ocf_metadata_io_end_t compl_hndl,
		struct ocf_alock *mio_conc)
{
	return metadata_io_i_asynch(cache, queue, OCF_WRITE, context,
			page, count, flags, fill_hndl, compl_hndl, mio_conc);
}

int metadata_io_read_i_asynch(ocf_cache_t cache, ocf_queue_t queue,
		void *context, uint32_t page, uint32_t count, int flags,
		ocf_metadata_io_event_t drain_hndl,
		ocf_metadata_io_end_t compl_hndl)
{
	return metadata_io_i_asynch(cache, queue, OCF_READ, context,
			page, count, flags, drain_hndl, compl_hndl, NULL);
}

#define MIO_RPOOL_LIMIT 16
#define MIO_RPOOL_THRESHOLD ocf_mio_size_16 /* This is statically determined to
					       not exceed one page (4096B).
					       Change if apropriate. */

int ocf_metadata_io_ctx_init(struct ocf_ctx *ocf_ctx)
{
	uint32_t limits[] = {
		[0 ... MIO_RPOOL_THRESHOLD - 1] = -1,
		[MIO_RPOOL_THRESHOLD ... ocf_mio_size_max - 1] = MIO_RPOOL_LIMIT
	};

	ocf_ctx->resources.mio = env_mpool_create(
			sizeof(struct metadata_io_request_asynch),
			sizeof(struct metadata_io_request),
			ENV_MEM_NOIO, ocf_mio_size_max - 1, true,
			limits,
			"ocf_mio",
			true);
	if (ocf_ctx->resources.mio == NULL)
		return -1;

	return 0;
}

void ocf_metadata_io_ctx_deinit(struct ocf_ctx *ocf_ctx)
{
	env_mpool_destroy(ocf_ctx->resources.mio);
	ocf_ctx->resources.mio = NULL;
}
