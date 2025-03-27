/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "ocf/ocf.h"
#include "ocf/ocf_queue.h"
#include "ocf_priv.h"
#include "ocf_queue_priv.h"
#include "ocf_cache_priv.h"
#include "ocf_ctx_priv.h"
#include "ocf_request.h"
#include "mngt/ocf_mngt_common.h"
#include "engine/cache_engine.h"
#include "ocf_def_priv.h"

int ocf_queue_create(ocf_cache_t cache, ocf_queue_t *queue,
		const struct ocf_queue_ops *ops)
{
	ocf_queue_t tmp_queue;
	int result;

	OCF_CHECK_NULL(cache);

	result = ocf_mngt_cache_get(cache);
	if (result)
		return result;

	tmp_queue = env_zalloc(sizeof(*tmp_queue), ENV_MEM_NORMAL);
	if (!tmp_queue) {
		ocf_mngt_cache_put(cache);
		return -OCF_ERR_NO_MEM;
	}

	env_atomic_set(&tmp_queue->io_no, 0);
	result = env_spinlock_init(&tmp_queue->io_list_lock);
	if (result) {
		ocf_mngt_cache_put(cache);
		env_free(tmp_queue);
		return result;
	}

	INIT_LIST_HEAD(&tmp_queue->io_list);
	env_atomic_set(&tmp_queue->ref_count, 1);
	tmp_queue->cache = cache;
	tmp_queue->ops = ops;

	result = ocf_queue_seq_cutoff_init(tmp_queue);
	if (result) {
		ocf_mngt_cache_put(cache);
		env_free(tmp_queue);
		return result;
	}

	list_add(&tmp_queue->list, &cache->io_queues);

	*queue = tmp_queue;

	return 0;
}

void ocf_queue_get(ocf_queue_t queue)
{
	OCF_CHECK_NULL(queue);

	env_atomic_inc(&queue->ref_count);
}

void ocf_queue_put(ocf_queue_t queue)
{
	OCF_CHECK_NULL(queue);

	if (env_atomic_dec_return(&queue->ref_count) == 0) {
		list_del(&queue->list);
		queue->ops->stop(queue);
		ocf_queue_seq_cutoff_deinit(queue);
		ocf_mngt_cache_put(queue->cache);
		env_spinlock_destroy(&queue->io_list_lock);
		env_free(queue);
	}
}

void ocf_io_handle(struct ocf_io *io, void *opaque)
{
	struct ocf_request *req = opaque;

	OCF_CHECK_NULL(req);

	if (req->rw == OCF_WRITE)
		req->io_if->write(req);
	else
		req->io_if->read(req);
}

void ocf_queue_run_single(ocf_queue_t q)
{
	struct ocf_request *io_req = NULL;

	OCF_CHECK_NULL(q);

	io_req = ocf_engine_pop_req(q);

	if (!io_req)
		return;

	if (io_req->ioi.io.handle)
		io_req->ioi.io.handle(&io_req->ioi.io, io_req);
	else
		ocf_io_handle(&io_req->ioi.io, io_req);
}

void ocf_queue_run(ocf_queue_t q)
{
	unsigned char step = 0;

	OCF_CHECK_NULL(q);

	while (env_atomic_read(&q->io_no) > 0) {
		ocf_queue_run_single(q);

		OCF_COND_RESCHED(step, 128);
	}
}

void ocf_queue_set_priv(ocf_queue_t q, void *priv)
{
	OCF_CHECK_NULL(q);
	q->priv = priv;
}

void *ocf_queue_get_priv(ocf_queue_t q)
{
	OCF_CHECK_NULL(q);
	return q->priv;
}

uint32_t ocf_queue_pending_io(ocf_queue_t q)
{
	OCF_CHECK_NULL(q);
	return env_atomic_read(&q->io_no);
}

ocf_cache_t ocf_queue_get_cache(ocf_queue_t q)
{
	OCF_CHECK_NULL(q);
	return q->cache;
}
