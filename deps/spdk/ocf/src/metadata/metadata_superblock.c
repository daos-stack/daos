/*
 * Copyright(c) 2020-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "metadata.h"
#include "metadata_core.h"
#include "metadata_internal.h"
#include "metadata_segment_id.h"
#include "metadata_superblock.h"
#include "../ocf_priv.h"
#include "../utils/utils_io.h"

#define OCF_METADATA_SUPERBLOCK_DEBUG 0

#if 1 == OCF_METADATA_SUPERBLOCK_DEBUG
#define OCF_DEBUG_TRACE(cache) \
	ocf_cache_log(cache, log_info, "[Metadata][Superblock] %s\n", \
	__func__)
#define OCF_DEBUG_PARAM(cache, format, ...) \
	ocf_cache_log(cache, log_info, "[Metadata][Superblock] %s - " \
			format"\n", __func__, ##__VA_ARGS__)
#else
#define OCF_DEBUG_TRACE(cache)
#define OCF_DEBUG_PARAM(cache, format, ...)
#endif

int ocf_metadata_segment_init_in_place(
		struct ocf_metadata_segment *segment,
		struct ocf_cache *cache,
		struct ocf_metadata_raw *raw,
		ocf_flush_page_synch_t lock_page_pfn,
		ocf_flush_page_synch_t unlock_page_pfn,
		struct ocf_metadata_segment *superblock);

/**
 * @brief Super Block - Set Shutdown Status
 *
 * @param shutdown_status - status to be assigned to cache.
 *
 * @return Operation status (0 success, otherwise error)
 */
void ocf_metadata_set_shutdown_status(ocf_cache_t cache,
		enum ocf_metadata_shutdown_status shutdown_status,
		ocf_metadata_end_t cmpl, void *priv)
{
	struct ocf_metadata_ctrl *ctrl;
	struct ocf_superblock_config *superblock;

	OCF_DEBUG_TRACE(cache);

	/*
	 * Get metadata hash service control structure
	 */
	/* TODO: get metadata ctrl from args rather than via cache */
	ctrl = (struct ocf_metadata_ctrl *) cache->metadata.priv;

	/*
	 * Get super block
	 */
	superblock = ocf_metadata_raw_get_mem(
			&ctrl->raw_desc[metadata_segment_sb_config]);

	/* Set shutdown status */
	superblock->clean_shutdown = shutdown_status;
	superblock->magic_number = CACHE_MAGIC_NUMBER;

	/* Flush superblock */
	ocf_metadata_flush_superblock(cache, cmpl, priv);
}

static void ocf_metadata_store_segment(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_metadata_context *context = priv;
	int segment = ocf_pipeline_arg_get_int(arg);
	struct ocf_metadata_ctrl *ctrl;
	ocf_cache_t cache = context->cache;
	int error;

	ctrl = (struct ocf_metadata_ctrl *)cache->metadata.priv;

	context->segment_copy[segment].mem_pool =
		env_malloc(ctrl->raw_desc[segment].mem_pool_limit, ENV_MEM_NORMAL);
	if (!context->segment_copy[segment].mem_pool)
		OCF_PL_FINISH_RET(pipeline, -OCF_ERR_NO_MEM);

	error = env_memcpy(context->segment_copy[segment].mem_pool,
			ctrl->raw_desc[segment].mem_pool_limit, METADATA_MEM_POOL(ctrl, segment),
			ctrl->raw_desc[segment].mem_pool_limit);
	if (error) {
		env_free(context->segment_copy[segment].mem_pool);
		context->segment_copy[segment].mem_pool = NULL;
		OCF_PL_FINISH_RET(pipeline, error);
	}

	ocf_pipeline_next(pipeline);
}

static void ocf_metadata_check_crc_sb_config(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_metadata_context *context = priv;
	struct ocf_metadata_ctrl *ctrl;
	struct ocf_superblock_config *sb_config;
	ocf_cache_t cache = context->cache;
	int segment = metadata_segment_sb_config;
	uint32_t crc;

	ctrl = (struct ocf_metadata_ctrl *)cache->metadata.priv;
	sb_config = METADATA_MEM_POOL(ctrl, metadata_segment_sb_config);

	crc = env_crc32(0, (void *)sb_config,
			offsetof(struct ocf_superblock_config, checksum));

	if (crc != sb_config->checksum[segment]) {
		/* Checksum does not match */
		ocf_cache_log(cache, log_err,
				"Loading %s ERROR, invalid checksum\n",
				ocf_metadata_segment_names[segment]);
		OCF_PL_FINISH_RET(pipeline, -OCF_ERR_INVAL);
	}

	ocf_pipeline_next(pipeline);
}

static void ocf_metadata_load_superblock_post(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_metadata_context *context = priv;
	struct ocf_metadata_ctrl *ctrl;
	struct ocf_superblock_config *sb_config;
	ocf_cache_t cache = context->cache;
	struct ocf_metadata_uuid *muuid;
	struct ocf_volume_uuid uuid;
	ocf_volume_type_t volume_type;
	ocf_core_t core;
	ocf_core_id_t core_id;

	ctrl = (struct ocf_metadata_ctrl *)cache->metadata.priv;
	sb_config = METADATA_MEM_POOL(ctrl, metadata_segment_sb_config);

	for_each_core_metadata(cache, core, core_id) {
		muuid = ocf_metadata_get_core_uuid(cache, core_id);
		uuid.data = muuid->data;
		uuid.size = muuid->size;

		volume_type = ocf_ctx_get_volume_type(cache->owner,
				core->conf_meta->type);

		/* Initialize core volume */
		ocf_volume_init(&core->volume, volume_type, &uuid, false);
		core->has_volume = true;
	}

	/* Restore all dynamics items */

	if (sb_config->core_count > OCF_CORE_MAX) {
		ocf_cache_log(cache, log_err,
			"Loading cache state ERROR, invalid cores count\n");
		OCF_PL_FINISH_RET(pipeline, -OCF_ERR_INVAL);
	}

	if (sb_config->valid_parts_no > OCF_USER_IO_CLASS_MAX) {
		ocf_cache_log(cache, log_err,
			"Loading cache state ERROR, invalid partition count\n");
		OCF_PL_FINISH_RET(pipeline, -OCF_ERR_INVAL);
	}

	ocf_pipeline_next(pipeline);
}

static void ocf_metadata_load_sb_restore(
		struct ocf_metadata_context *context)
{
	ocf_cache_t cache = context->cache;
	struct ocf_metadata_ctrl *ctrl;
	int segment, error;

	ctrl = (struct ocf_metadata_ctrl *)cache->metadata.priv;

	for (segment = metadata_segment_sb_config;
			segment < metadata_segment_fixed_size_max; segment++) {
		if (!context->segment_copy[segment].mem_pool)
			continue;

		error = env_memcpy(METADATA_MEM_POOL(ctrl, segment),
				ctrl->raw_desc[segment].mem_pool_limit,
				context->segment_copy[segment].mem_pool,
				ctrl->raw_desc[segment].mem_pool_limit);
		ENV_BUG_ON(error);
	}
}

static void ocf_metadata_load_superblock_finish(ocf_pipeline_t pipeline,
		void *priv, int error)
{
	struct ocf_metadata_context *context = priv;
	ocf_cache_t cache = context->cache;
	int segment;

	if (error) {
		ocf_cache_log(cache, log_err, "Metadata read FAILURE\n");
		ocf_metadata_error(cache);
		ocf_metadata_load_sb_restore(context);
	}

	for (segment = metadata_segment_sb_config;
			segment < metadata_segment_fixed_size_max; segment++) {
		if (context->segment_copy[segment].mem_pool)
			env_free(context->segment_copy[segment].mem_pool);
	}

	context->cmpl(context->priv, error);
	ocf_pipeline_destroy(pipeline);
}

struct ocf_pipeline_arg ocf_metadata_load_sb_store_segment_args[] = {
	OCF_PL_ARG_INT(metadata_segment_sb_config),
	OCF_PL_ARG_INT(metadata_segment_sb_runtime),
	OCF_PL_ARG_INT(metadata_segment_part_config),
	OCF_PL_ARG_INT(metadata_segment_part_runtime),
	OCF_PL_ARG_INT(metadata_segment_core_config),
	OCF_PL_ARG_TERMINATOR(),
};

struct ocf_pipeline_arg ocf_metadata_load_sb_load_segment_args[] = {
	OCF_PL_ARG_INT(metadata_segment_sb_config),
	OCF_PL_ARG_INT(metadata_segment_sb_runtime),
	OCF_PL_ARG_INT(metadata_segment_part_config),
	OCF_PL_ARG_INT(metadata_segment_part_runtime),
	OCF_PL_ARG_INT(metadata_segment_core_config),
	OCF_PL_ARG_INT(metadata_segment_core_uuid),
	OCF_PL_ARG_TERMINATOR(),
};

struct ocf_pipeline_arg ocf_metadata_load_sb_check_crc_args[] = {
	OCF_PL_ARG_INT(metadata_segment_part_config),
	OCF_PL_ARG_INT(metadata_segment_core_config),
	OCF_PL_ARG_INT(metadata_segment_core_uuid),
	OCF_PL_ARG_TERMINATOR(),
};

struct ocf_pipeline_arg ocf_metadata_load_sb_check_crc_args_clean[] = {
	OCF_PL_ARG_INT(metadata_segment_sb_runtime),
	OCF_PL_ARG_INT(metadata_segment_part_runtime),
	OCF_PL_ARG_TERMINATOR(),
};

struct ocf_pipeline_properties ocf_metadata_load_sb_pipeline_props = {
	.priv_size = sizeof(struct ocf_metadata_context),
	.finish = ocf_metadata_load_superblock_finish,
	.steps = {
		OCF_PL_STEP_FOREACH(ocf_metadata_store_segment,
				ocf_metadata_load_sb_store_segment_args),
		OCF_PL_STEP_FOREACH(ocf_metadata_load_segment,
				ocf_metadata_load_sb_load_segment_args),
		OCF_PL_STEP(ocf_metadata_check_crc_sb_config),
		OCF_PL_STEP_FOREACH(ocf_metadata_check_crc,
				ocf_metadata_load_sb_check_crc_args),
		OCF_PL_STEP_FOREACH(ocf_metadata_check_crc_if_clean,
				ocf_metadata_load_sb_check_crc_args_clean),
		OCF_PL_STEP(ocf_metadata_load_superblock_post),
		OCF_PL_STEP_TERMINATOR(),
	},
};

/*
 * Super Block - Load, This function has to prevent to pointers overwrite
 */
void ocf_metadata_load_superblock(ocf_cache_t cache, ocf_metadata_end_t cmpl,
		void *priv)
{
	struct ocf_metadata_context *context;
	ocf_pipeline_t pipeline;
	struct ocf_metadata_ctrl *ctrl;
	struct ocf_superblock_config *sb_config;
	struct ocf_superblock_runtime *sb_runtime;
	int result;

	OCF_DEBUG_TRACE(cache);

	/* TODO: get ctrl from args rather than from cache */
	ctrl = cache->metadata.priv;
	ENV_BUG_ON(!ctrl);

	sb_config = METADATA_MEM_POOL(ctrl, metadata_segment_sb_config);
	ENV_BUG_ON(!sb_config);

	sb_runtime = METADATA_MEM_POOL(ctrl, metadata_segment_sb_runtime);
	ENV_BUG_ON(!sb_runtime);

	result = ocf_pipeline_create(&pipeline, cache,
			&ocf_metadata_load_sb_pipeline_props);
	if (result)
		OCF_CMPL_RET(priv, result);

	context = ocf_pipeline_get_priv(pipeline);

	context->cmpl = cmpl;
	context->priv = priv;
	context->pipeline = pipeline;
	context->cache = cache;
	context->ctrl = cache->metadata.priv;

	ocf_pipeline_next(pipeline);
}

static void ocf_metadata_flush_superblock_prepare(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_metadata_context *context = priv;
	ocf_cache_t cache = context->cache;
	ocf_core_t core;
	ocf_core_id_t core_id;

	/* Synchronize core objects types */
	for_each_core_metadata(cache, core, core_id) {
		core->conf_meta->type = ocf_ctx_get_volume_type_id(
				cache->owner, core->volume.type);
	}

	ocf_pipeline_next(pipeline);
}

static void ocf_metadata_calculate_crc_sb_config(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_metadata_context *context = priv;
	struct ocf_metadata_ctrl *ctrl;
	struct ocf_superblock_config *sb_config;

	ctrl = context->ctrl;
	sb_config = METADATA_MEM_POOL(ctrl, metadata_segment_sb_config);

	sb_config->checksum[metadata_segment_sb_config] = env_crc32(0,
			(void *)sb_config,
			offsetof(struct ocf_superblock_config, checksum));

	ocf_pipeline_next(pipeline);
}

static void ocf_metadata_flush_superblock_finish(ocf_pipeline_t pipeline,
		void *priv, int error)
{
	struct ocf_metadata_context *context = priv;
	ocf_cache_t cache = context->cache;

	if (error)
		ocf_metadata_error(cache);

	context->cmpl(context->priv, error);
	ocf_pipeline_destroy(pipeline);
}

static void ocf_metadata_flush_disk_end(void *priv, int error)
{
	struct ocf_metadata_context *context = priv;
	ocf_pipeline_t pipeline = context->pipeline;

	if (error) {
		OCF_PL_FINISH_RET(pipeline, error);
		return;
	}

	ocf_pipeline_next(pipeline);
}

static void ocf_metadata_flush_disk(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_metadata_context *context = priv;
	ocf_cache_t cache = context->cache;

	ocf_submit_volume_flush(ocf_cache_get_volume(cache),
		ocf_metadata_flush_disk_end, context);
}

struct ocf_pipeline_arg ocf_metadata_flush_sb_calculate_crc_args[] = {
	OCF_PL_ARG_INT(metadata_segment_part_config),
	OCF_PL_ARG_INT(metadata_segment_core_config),
	OCF_PL_ARG_INT(metadata_segment_core_uuid),
	OCF_PL_ARG_TERMINATOR(),
};

struct ocf_pipeline_arg ocf_metadata_flush_sb_flush_segment_args[] = {
	OCF_PL_ARG_INT(metadata_segment_sb_config),
	OCF_PL_ARG_INT(metadata_segment_part_config),
	OCF_PL_ARG_INT(metadata_segment_core_config),
	OCF_PL_ARG_INT(metadata_segment_core_uuid),
	OCF_PL_ARG_TERMINATOR(),
};

struct ocf_pipeline_properties ocf_metadata_flush_sb_pipeline_props = {
	.priv_size = sizeof(struct ocf_metadata_context),
	.finish = ocf_metadata_flush_superblock_finish,
	.steps = {
		OCF_PL_STEP(ocf_metadata_flush_superblock_prepare),
		OCF_PL_STEP(ocf_metadata_calculate_crc_sb_config),
		OCF_PL_STEP_FOREACH(ocf_metadata_calculate_crc,
				ocf_metadata_flush_sb_calculate_crc_args),
		OCF_PL_STEP_FOREACH(ocf_metadata_flush_segment,
				ocf_metadata_flush_sb_flush_segment_args),
		OCF_PL_STEP(ocf_metadata_flush_disk),
		OCF_PL_STEP_TERMINATOR(),
	},
};

/*
 * Super Block - FLUSH
 */
void ocf_metadata_flush_superblock(ocf_cache_t cache,
		ocf_metadata_end_t cmpl, void *priv)
{
	struct ocf_metadata_context *context;
	ocf_pipeline_t pipeline;
	int result;

	OCF_DEBUG_TRACE(cache);

	result = ocf_pipeline_create(&pipeline, cache,
			&ocf_metadata_flush_sb_pipeline_props);
	if (result)
		OCF_CMPL_RET(priv, result);

	context = ocf_pipeline_get_priv(pipeline);

	context->cmpl = cmpl;
	context->priv = priv;
	context->pipeline = pipeline;
	context->cache = cache;
	context->ctrl = cache->metadata.priv;

	ocf_pipeline_next(pipeline);
}

struct ocf_metadata_superblock
{
	struct ocf_metadata_segment segment;
	struct ocf_superblock_config *config;
};

#define _ocf_segment_to_sb(_segment) \
	container_of(_segment, struct ocf_metadata_superblock, segment);

int ocf_metadata_superblock_init(
		struct ocf_metadata_segment **self,
		struct ocf_cache *cache,
		struct ocf_metadata_raw *raw)
{
	struct ocf_metadata_superblock *sb = env_vzalloc(sizeof(*sb));
	int result;

	if (!sb)
		return -OCF_ERR_NO_MEM;

	result = ocf_metadata_segment_init_in_place(&sb->segment, cache,
			raw, NULL, NULL, &sb->segment);

	if (result) {
		env_vfree(sb);
		return result;
	}

	sb->config = ocf_metadata_raw_get_mem(sb->segment.raw);

	*self = &sb->segment;
	return 0;
}


void ocf_metadata_superblock_destroy(
		struct ocf_cache *cache,
		struct ocf_metadata_segment *self)
{
	ocf_metadata_segment_destroy(cache, self);
}

uint32_t ocf_metadata_superblock_get_checksum(
		struct ocf_metadata_segment *self,
		enum ocf_metadata_segment_id segment)
{
	struct ocf_metadata_superblock *sb = _ocf_segment_to_sb(self);

	return sb->config->checksum[segment];
}

void ocf_metadata_superblock_set_checksum(
		struct ocf_metadata_segment *self,
		enum ocf_metadata_segment_id segment,
		uint32_t csum)
{
	struct ocf_metadata_superblock *sb = _ocf_segment_to_sb(self);

	sb->config->checksum[segment] = csum;
}

bool ocf_metadata_superblock_get_clean_shutdown(
		struct ocf_metadata_segment *self)
{
	struct ocf_metadata_superblock *sb = _ocf_segment_to_sb(self);

	return sb->config->clean_shutdown;
}

