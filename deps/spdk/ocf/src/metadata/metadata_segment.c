/*
 * Copyright(c) 2020-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "metadata_internal.h"
#include "metadata_superblock.h"
#include "metadata_raw.h"

int ocf_metadata_segment_init_in_place(
		struct ocf_metadata_segment *segment,
		struct ocf_cache *cache,
		struct ocf_metadata_raw *raw,
		ocf_flush_page_synch_t lock_page_pfn,
		ocf_flush_page_synch_t unlock_page_pfn,
		struct ocf_metadata_segment *superblock)
{
	int result;

	result = ocf_metadata_raw_init(cache, lock_page_pfn, unlock_page_pfn, raw);
	if (result)
		return result;

	segment->raw = raw;
	segment->superblock = superblock;

	return 0;

}

int ocf_metadata_segment_init(
		struct ocf_metadata_segment **self,
		struct ocf_cache *cache,
		struct ocf_metadata_raw *raw,
		ocf_flush_page_synch_t lock_page_pfn,
		ocf_flush_page_synch_t unlock_page_pfn,
		struct ocf_metadata_segment *superblock)
{
	struct ocf_metadata_segment *segment;
	int result;

	segment = env_vzalloc(sizeof(*segment));
	if (!segment)
		return -OCF_ERR_NO_MEM;

	result = ocf_metadata_segment_init_in_place(segment,
			cache, raw, lock_page_pfn, unlock_page_pfn,
			superblock);

	if (result)
		env_vfree(segment);
	else
		*self = segment;

	return result;
}

void ocf_metadata_segment_destroy(struct ocf_cache *cache,
		struct ocf_metadata_segment *self)
{
	if (!self)
		return;

	ocf_metadata_raw_deinit(cache, self->raw);
	env_vfree(self);
}

static void ocf_metadata_generic_complete(void *priv, int error)
{
	struct ocf_metadata_context *context = priv;

	OCF_PL_NEXT_ON_SUCCESS_RET(context->pipeline, error);
}

static void ocf_metadata_check_crc_skip(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg, bool skip_on_dirty_shutdown)
{
	struct ocf_metadata_context *context = priv;
	int segment_id = ocf_pipeline_arg_get_int(arg);
	struct ocf_metadata_segment *segment = context->ctrl->segment[segment_id];
	ocf_cache_t cache = context->cache;
	uint32_t crc;
	uint32_t superblock_crc;
	bool clean_shutdown;

	clean_shutdown = ocf_metadata_superblock_get_clean_shutdown(
			segment->superblock);
	if (!clean_shutdown && skip_on_dirty_shutdown)
		OCF_PL_NEXT_RET(pipeline);

	crc = ocf_metadata_raw_checksum(cache, segment->raw);
	superblock_crc = ocf_metadata_superblock_get_checksum(segment->superblock,
			segment_id);

	if (crc != superblock_crc) {
		/* Checksum does not match */
		if (!clean_shutdown) {
			ocf_cache_log(cache, log_warn,
					"Loading %s WARNING, invalid checksum\n",
					ocf_metadata_segment_names[segment_id]);
		} else {
			ocf_cache_log(cache, log_err,
					"Loading %s ERROR, invalid checksum\n",
					ocf_metadata_segment_names[segment_id]);
			OCF_PL_FINISH_RET(pipeline, -OCF_ERR_INVAL);
		}
	}

	ocf_pipeline_next(pipeline);
}

void ocf_metadata_check_crc(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	ocf_metadata_check_crc_skip(pipeline, priv, arg, false);
}

void ocf_metadata_check_crc_if_clean(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	ocf_metadata_check_crc_skip(pipeline, priv, arg, true);
}


void ocf_metadata_calculate_crc(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_metadata_context *context = priv;
	int segment_id = ocf_pipeline_arg_get_int(arg);
	struct ocf_metadata_segment *segment = context->ctrl->segment[segment_id];

	ocf_metadata_superblock_set_checksum(segment->superblock, segment_id,
			ocf_metadata_raw_checksum(context->cache, segment->raw));

	ocf_pipeline_next(pipeline);
}

void ocf_metadata_flush_segment(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_metadata_context *context = priv;
	int segment = ocf_pipeline_arg_get_int(arg);
	struct ocf_metadata_ctrl *ctrl = context->ctrl;
	ocf_cache_t cache = context->cache;

	ocf_metadata_raw_flush_all(cache, &ctrl->raw_desc[segment],
			ocf_metadata_generic_complete, context);
}

void ocf_metadata_load_segment(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_metadata_context *context = priv;
	int segment = ocf_pipeline_arg_get_int(arg);
	struct ocf_metadata_ctrl *ctrl = context->ctrl;
	ocf_cache_t cache = context->cache;

	ocf_metadata_raw_load_all(cache, &ctrl->raw_desc[segment],
			ocf_metadata_generic_complete, context);
}
