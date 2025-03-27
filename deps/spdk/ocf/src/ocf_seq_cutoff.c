/*
 * Copyright(c) 2020-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf_seq_cutoff.h"
#include "ocf_cache_priv.h"
#include "ocf_core_priv.h"
#include "ocf_queue_priv.h"
#include "ocf_priv.h"
#include "ocf/ocf_debug.h"
#include "utils/utils_cache_line.h"

#define SEQ_CUTOFF_FULL_MARGIN 512

static inline bool ocf_seq_cutoff_is_on(ocf_cache_t cache,
		struct ocf_request *req)
{
	if (!ocf_cache_is_device_attached(cache))
		return false;

	return (ocf_lru_num_free(cache) <= SEQ_CUTOFF_FULL_MARGIN +
			req->core_line_count);
}

static int ocf_seq_cutoff_stream_cmp(struct ocf_rb_node *n1,
		struct ocf_rb_node *n2)
{
	struct ocf_seq_cutoff_stream *stream1 = container_of(n1,
			struct ocf_seq_cutoff_stream, node);
	struct ocf_seq_cutoff_stream *stream2 = container_of(n2,
			struct ocf_seq_cutoff_stream, node);

	if (stream1->valid < stream2->valid)
		return -1;

	if (stream1->valid > stream2->valid)
		return 1;

	if (stream1->rw < stream2->rw)
		return -1;

	if (stream1->rw > stream2->rw)
		return 1;

	if (stream1->last < stream2->last)
		return -1;

	if (stream1->last > stream2->last)
		return 1;

	return 0;
}

static struct ocf_rb_node *ocf_seq_cutoff_stream_list_find(
		struct list_head *node_list)
{
	struct ocf_seq_cutoff_stream *stream, *max_stream = NULL;
	struct ocf_rb_node *node;

	node = list_entry(node_list, struct ocf_rb_node, list);
	stream = container_of(node, struct ocf_seq_cutoff_stream, node);
	list_for_each_entry(node, node_list, list) {
		stream = container_of(node, struct ocf_seq_cutoff_stream, node);
		if (!max_stream)
			max_stream = stream;
		if (stream->bytes > max_stream->bytes)
			max_stream = stream;
	}

	return max_stream ? &max_stream->node : NULL;
}

static void ocf_seq_cutoff_base_init(struct ocf_seq_cutoff *base, int nstreams)
{
	struct ocf_seq_cutoff_stream *stream;
	int i;

	env_rwlock_init(&base->lock);
	ocf_rb_tree_init(&base->tree, ocf_seq_cutoff_stream_cmp,
			ocf_seq_cutoff_stream_list_find);
	INIT_LIST_HEAD(&base->lru);

	for (i = 0; i < nstreams; i++) {
		stream = &base->streams[i];
		stream->last = 4096 * i;
		stream->bytes = 0;
		stream->rw = 0;
		stream->valid = false;
		ocf_rb_tree_insert(&base->tree, &stream->node);
		list_add_tail(&stream->list, &base->lru);
	}
}

void ocf_seq_cutoff_base_deinit(struct ocf_seq_cutoff *base)
{
	env_rwlock_destroy(&base->lock);
}

int ocf_core_seq_cutoff_init(ocf_core_t core)
{
	ocf_core_log(core, log_info, "Seqential cutoff init\n");

	core->seq_cutoff = env_vmalloc(sizeof(struct ocf_seq_cutoff_percore));
	if (!core->seq_cutoff)
		return -OCF_ERR_NO_MEM;

	ocf_seq_cutoff_base_init(core->seq_cutoff,
			OCF_SEQ_CUTOFF_PERCORE_STREAMS);

	return 0;
}

void ocf_core_seq_cutoff_deinit(ocf_core_t core)
{
	ocf_seq_cutoff_base_deinit(core->seq_cutoff);
	env_vfree(core->seq_cutoff);
}

int ocf_queue_seq_cutoff_init(ocf_queue_t queue)
{
	queue->seq_cutoff = env_vmalloc(sizeof(struct ocf_seq_cutoff_perqueue));
	if (!queue->seq_cutoff)
		return -OCF_ERR_NO_MEM;

	ocf_seq_cutoff_base_init(queue->seq_cutoff,
			OCF_SEQ_CUTOFF_PERQUEUE_STREAMS);

	return 0;
}

void ocf_queue_seq_cutoff_deinit(ocf_queue_t queue)
{
	ocf_seq_cutoff_base_deinit(queue->seq_cutoff);
	env_vfree(queue->seq_cutoff);
}

void ocf_dbg_get_seq_cutoff_status(ocf_core_t core,
		struct ocf_dbg_seq_cutoff_status *status)
{
	struct ocf_seq_cutoff_stream *stream;
	uint32_t threshold;
	int i = 0;

	OCF_CHECK_NULL(core);
	OCF_CHECK_NULL(status);

	threshold = ocf_core_get_seq_cutoff_threshold(core);

	env_rwlock_read_lock(&core->seq_cutoff->lock);
	list_for_each_entry(stream, &core->seq_cutoff->lru, list) {
		status->streams[i].last = stream->last;
		status->streams[i].bytes = stream->bytes;
		status->streams[i].rw = stream->rw;
		status->streams[i].active = (stream->bytes >= threshold);
		i++;
	}
	env_rwlock_read_unlock(&core->seq_cutoff->lock);
}

static bool ocf_core_seq_cutoff_base_check(struct ocf_seq_cutoff *seq_cutoff,
		uint64_t addr, uint32_t len, int rw, uint32_t threshold,
		struct ocf_seq_cutoff_stream **out_stream)
{
	struct ocf_seq_cutoff_stream item = {
		.last = addr, .rw = rw, .valid = true
	};
	struct ocf_seq_cutoff_stream *stream;
	struct ocf_rb_node *node;
	bool result = false;

	node = ocf_rb_tree_find(&seq_cutoff->tree, &item.node);
	if (node) {
		stream = container_of(node, struct ocf_seq_cutoff_stream, node);
		if (stream->bytes + len >= threshold)
			result = true;

		if (out_stream)
			*out_stream = stream;
	}

	return result;
}

bool ocf_core_seq_cutoff_check(ocf_core_t core, struct ocf_request *req)
{
	ocf_seq_cutoff_policy policy = ocf_core_get_seq_cutoff_policy(core);
	uint32_t threshold = ocf_core_get_seq_cutoff_threshold(core);
	ocf_cache_t cache = ocf_core_get_cache(core);
	struct ocf_seq_cutoff_stream *queue_stream = NULL;
	struct ocf_seq_cutoff_stream *core_stream = NULL;
	bool result;

	switch (policy) {
		case ocf_seq_cutoff_policy_always:
			break;
		case ocf_seq_cutoff_policy_full:
			if (ocf_seq_cutoff_is_on(cache, req))
				break;
			return false;

		case ocf_seq_cutoff_policy_never:
			return false;
		default:
			ENV_WARN(true, "Invalid sequential cutoff policy!");
			return false;
	}

	env_rwlock_read_lock(&req->io_queue->seq_cutoff->lock);
	result = ocf_core_seq_cutoff_base_check(req->io_queue->seq_cutoff,
			req->byte_position, req->byte_length, req->rw,
			threshold, &queue_stream);
	env_rwlock_read_unlock(&req->io_queue->seq_cutoff->lock);
	if (queue_stream)
		return result;

	env_rwlock_read_lock(&core->seq_cutoff->lock);
	result = ocf_core_seq_cutoff_base_check(core->seq_cutoff,
			req->byte_position, req->byte_length, req->rw,
			threshold, &core_stream);
	env_rwlock_read_unlock(&core->seq_cutoff->lock);

	if (core_stream)
		req->seq_cutoff_core = true;

	return result;
}

static struct ocf_seq_cutoff_stream *ocf_core_seq_cutoff_base_update(
		struct ocf_seq_cutoff *seq_cutoff,
		uint64_t addr, uint32_t len, int rw, bool insert)
{
	struct ocf_seq_cutoff_stream item = {
		.last = addr, .rw = rw, .valid = true
	};
	struct ocf_seq_cutoff_stream *stream;
	struct ocf_rb_node *node;
	bool can_update;

	node = ocf_rb_tree_find(&seq_cutoff->tree, &item.node);
	if (node) {
		stream = container_of(node, struct ocf_seq_cutoff_stream, node);
		item.last = addr + len;
		can_update = ocf_rb_tree_can_update(&seq_cutoff->tree,
				node, &item.node);
		stream->last = addr + len;
		stream->bytes += len;
		stream->req_count++;
		if (!can_update) {
			ocf_rb_tree_remove(&seq_cutoff->tree, node);
			ocf_rb_tree_insert(&seq_cutoff->tree, node);
		}
		list_move_tail(&stream->list, &seq_cutoff->lru);

		return stream;
	}

	if (insert) {
		stream = list_first_entry(&seq_cutoff->lru,
				struct ocf_seq_cutoff_stream, list);
		ocf_rb_tree_remove(&seq_cutoff->tree, &stream->node);
		stream->rw = rw;
		stream->last = addr + len;
		stream->bytes = len;
		stream->req_count = 1;
		stream->valid = true;
		ocf_rb_tree_insert(&seq_cutoff->tree, &stream->node);
		list_move_tail(&stream->list, &seq_cutoff->lru);

		return stream;
	}

	return NULL;
}

static void ocf_core_seq_cutoff_base_promote(
		struct ocf_seq_cutoff *dst_seq_cutoff,
		struct ocf_seq_cutoff *src_seq_cutoff,
		struct ocf_seq_cutoff_stream *src_stream)
{
	struct ocf_seq_cutoff_stream *dst_stream;

	dst_stream = list_first_entry(&dst_seq_cutoff->lru,
			struct ocf_seq_cutoff_stream, list);
	ocf_rb_tree_remove(&dst_seq_cutoff->tree, &dst_stream->node);
	dst_stream->rw = src_stream->rw;
	dst_stream->last = src_stream->last;
	dst_stream->bytes = src_stream->bytes;
	dst_stream->req_count = src_stream->req_count;
	dst_stream->valid = true;
	ocf_rb_tree_insert(&dst_seq_cutoff->tree, &dst_stream->node);
	list_move_tail(&dst_stream->list, &dst_seq_cutoff->lru);
	src_stream->valid = false;
	list_move(&src_stream->list, &src_seq_cutoff->lru);
}

void ocf_core_seq_cutoff_update(ocf_core_t core, struct ocf_request *req)
{
	ocf_seq_cutoff_policy policy = ocf_core_get_seq_cutoff_policy(core);
	uint32_t threshold = ocf_core_get_seq_cutoff_threshold(core);
	uint32_t promotion_count =
			ocf_core_get_seq_cutoff_promotion_count(core);
	struct ocf_seq_cutoff_stream *stream;
	bool promote = false;

	if (policy == ocf_seq_cutoff_policy_never)
		return;

	if (req->byte_length >= threshold)
		promote = true;

	if (promotion_count == 1)
		promote = true;

	if (req->seq_cutoff_core || promote) {
		env_rwlock_write_lock(&core->seq_cutoff->lock);
		stream = ocf_core_seq_cutoff_base_update(core->seq_cutoff,
				req->byte_position, req->byte_length, req->rw,
				promote);
		env_rwlock_write_unlock(&core->seq_cutoff->lock);

		if (stream)
			return;
	}

	env_rwlock_write_lock(&req->io_queue->seq_cutoff->lock);
	stream = ocf_core_seq_cutoff_base_update(req->io_queue->seq_cutoff,
			req->byte_position, req->byte_length, req->rw, true);
	env_rwlock_write_unlock(&req->io_queue->seq_cutoff->lock);

	if (stream->bytes >= threshold)
		promote = true;

	if (stream->req_count >= promotion_count)
		promote = true;

	if (promote) {
		env_rwlock_write_lock(&core->seq_cutoff->lock);
		env_rwlock_write_lock(&req->io_queue->seq_cutoff->lock);
		ocf_core_seq_cutoff_base_promote(core->seq_cutoff,
				req->io_queue->seq_cutoff, stream);
		env_rwlock_write_unlock(&req->io_queue->seq_cutoff->lock);
		env_rwlock_write_unlock(&core->seq_cutoff->lock);
	}
}
