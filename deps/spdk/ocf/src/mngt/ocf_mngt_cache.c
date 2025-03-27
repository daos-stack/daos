/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf.h"
#include "ocf_mngt_common.h"
#include "ocf_mngt_core_priv.h"
#include "../ocf_priv.h"
#include "../ocf_core_priv.h"
#include "../ocf_queue_priv.h"
#include "../metadata/metadata.h"
#include "../metadata/metadata_io.h"
#include "../metadata/metadata_partition_structs.h"
#include "../engine/cache_engine.h"
#include "../utils/utils_user_part.h"
#include "../utils/utils_cache_line.h"
#include "../utils/utils_io.h"
#include "../utils/utils_cache_line.h"
#include "../utils/utils_pipeline.h"
#include "../utils/utils_refcnt.h"
#include "../utils/utils_async_lock.h"
#include "../concurrency/ocf_concurrency.h"
#include "../ocf_lru.h"
#include "../ocf_ctx_priv.h"
#include "../cleaning/cleaning.h"
#include "../promotion/ops.h"

#define OCF_ASSERT_PLUGGED(cache) ENV_BUG_ON(!(cache)->device)

#define DIRTY_SHUTDOWN_ERROR_MSG "Please use --load option to restore " \
	"previous cache state (Warning: data corruption may happen)"  \
	"\nOr initialize your cache using --force option. " \
	"Warning: All dirty data will be lost!\n"

#define DIRTY_NOT_FLUSHED_ERROR_MSG "Cache closed w/ no data flushing\n" \
	"Restart with --load or --force option\n"

/**
 * @brief Helpful struct to start cache
 */
struct ocf_cache_mngt_init_params {
	ocf_ctx_t ctx;
		/*!< OCF context */

	ocf_cache_t cache;
		/*!< cache that is being initialized */

	uint8_t locked;
		/*!< Keep cache locked */

	bool metadata_volatile;

	/**
	 * @brief initialization state (in case of error, it is used to know
	 * which assets have to be deallocated in premature exit from function
	 */
	struct {
		bool cache_alloc : 1;
			/*!< cache is allocated and added to list */

		bool metadata_inited : 1;
			/*!< Metadata is inited to valid state */

		bool added_to_list : 1;
			/*!< Cache is added to context list */

		bool cache_locked : 1;
			/*!< Cache has been locked */
	} flags;

	struct ocf_metadata_init_params {
		ocf_cache_line_size_t line_size;
		/*!< Metadata cache line size */

		ocf_metadata_layout_t layout;
		/*!< Metadata layout (striping/sequential) */

		ocf_cache_mode_t cache_mode;
		/*!< cache mode */

		ocf_promotion_t promotion_policy;
	} metadata;
};

typedef void (*_ocf_mngt_cache_attach_end_t)(ocf_cache_t, void *priv1,
	void *priv2, int error);

struct ocf_cache_attach_context {
	ocf_cache_t cache;
		/*!< cache that is being initialized */

	struct ocf_mngt_cache_device_config cfg;

	uint64_t volume_size;
		/*!< size of the device in cache lines */

	/**
	 * @brief initialization state (in case of error, it is used to know
	 * which assets have to be deallocated in premature exit from function
	 */
	struct {
		bool device_alloc : 1;
			/*!< data structure allocated */

		bool volume_inited : 1;
			/*!< uuid for cache device is allocated */

		bool attached_metadata_inited : 1;
			/*!< attached metadata sections initialized */

		bool device_opened : 1;
			/*!< underlying device volume is open */

		bool cleaner_started : 1;
			/*!< Cleaner has been started */

		bool promotion_initialized : 1;
			/*!< Promotion policy has been started */

		bool cores_opened : 1;
			/*!< underlying cores are opened (happens only during
			 * load or recovery
			 */

		bool concurrency_inited : 1;
	} flags;

	struct {
		ocf_cache_line_size_t line_size;
		/*!< Metadata cache line size */

		ocf_metadata_layout_t layout;
		/*!< Metadata layout (striping/sequential) */

		ocf_cache_mode_t cache_mode;
		/*!< cache mode */

		enum ocf_metadata_shutdown_status shutdown_status;
		/*!< dirty or clean */

		uint8_t dirty_flushed;
		/*!< is dirty data fully flushed */
	} metadata;

	struct {
		void *rw_buffer;
		void *cmp_buffer;
		unsigned long reserved_lba_addr;
		ocf_pipeline_t pipeline;
	} test;

	_ocf_mngt_cache_attach_end_t cmpl;
	void *priv1;
	void *priv2;

	ocf_pipeline_t pipeline;
};

static void __init_partitions(ocf_cache_t cache)
{
	ocf_part_id_t i_part;

	/* Init default Partition */
	ENV_BUG_ON(ocf_mngt_add_partition_to_cache(cache, PARTITION_DEFAULT,
			"unclassified", 0, PARTITION_SIZE_MAX,
			OCF_IO_CLASS_PRIO_LOWEST, true));

	/* Add other partition to the cache and make it as dummy */
	for (i_part = 0; i_part < OCF_USER_IO_CLASS_MAX; i_part++) {
		ocf_refcnt_freeze(&cache->user_parts[i_part].cleaning.counter);

		if (i_part == PARTITION_DEFAULT)
			continue;

		/* Init default Partition */
		ENV_BUG_ON(ocf_mngt_add_partition_to_cache(cache, i_part,
				"Inactive", 0, PARTITION_SIZE_MAX,
				OCF_IO_CLASS_PRIO_LOWEST, false));
	}
}

static void __init_parts_attached(ocf_cache_t cache)
{
	ocf_part_id_t part_id;

	for (part_id = 0; part_id < OCF_USER_IO_CLASS_MAX; part_id++)
		ocf_lru_init(cache, &cache->user_parts[part_id].part);

	ocf_lru_init(cache, &cache->free);
}

static void __populate_free(ocf_cache_t cache)
{
	uint64_t free_clines = ocf_metadata_collision_table_entries(cache) -
			ocf_get_cache_occupancy(cache);

	ocf_lru_populate(cache, free_clines);
}

static ocf_error_t __init_cleaning_policy(ocf_cache_t cache)
{
	ocf_cleaning_t cleaning_policy = ocf_cleaning_default;
	int i;

	OCF_ASSERT_PLUGGED(cache);

	ocf_refcnt_init(&cache->cleaner.refcnt);

	for (i = 0; i < ocf_cleaning_max; i++)
		ocf_cleaning_setup(cache, i);

	cache->conf_meta->cleaning_policy_type = ocf_cleaning_default;

	return ocf_cleaning_initialize(cache, cleaning_policy, 1);
}

static void __deinit_cleaning_policy(ocf_cache_t cache)
{
	ocf_cleaning_deinitialize(cache);
}

static void __setup_promotion_policy(ocf_cache_t cache)
{
	int i;

	OCF_CHECK_NULL(cache);

	for (i = 0; i < ocf_promotion_max; i++) {
		if (ocf_promotion_policies[i].setup)
			ocf_promotion_policies[i].setup(cache);
	}
}

static void __deinit_promotion_policy(ocf_cache_t cache)
{
	ocf_promotion_deinit(cache->promotion_policy);
	cache->promotion_policy = NULL;
}

static void __init_free(ocf_cache_t cache)
{
	cache->free.id = PARTITION_FREELIST;
}

static void __init_cores(ocf_cache_t cache)
{
	/* No core devices yet */
	cache->conf_meta->core_count = 0;
	ENV_BUG_ON(env_memset(cache->conf_meta->valid_core_bitmap,
			sizeof(cache->conf_meta->valid_core_bitmap), 0));
}

static void __init_metadata_version(ocf_cache_t cache)
{
	cache->conf_meta->metadata_version = METADATA_VERSION();
}

static void __reset_stats(ocf_cache_t cache)
{
	ocf_core_t core;
	ocf_core_id_t core_id;
	ocf_part_id_t i;

	for_each_core_all(cache, core, core_id) {
		env_atomic_set(&core->runtime_meta->cached_clines, 0);
		env_atomic_set(&core->runtime_meta->dirty_clines, 0);
		env_atomic64_set(&core->runtime_meta->dirty_since, 0);

		for (i = 0; i != OCF_USER_IO_CLASS_MAX; i++) {
			env_atomic_set(&core->runtime_meta->
					part_counters[i].cached_clines, 0);
			env_atomic_set(&core->runtime_meta->
					part_counters[i].dirty_clines, 0);
		}
	}
}

static ocf_error_t init_attached_data_structures(ocf_cache_t cache)
{
	ocf_error_t result;

	/* Lock to ensure consistency */

	ocf_metadata_init_hash_table(cache);
	ocf_metadata_init_collision(cache);
	__init_parts_attached(cache);
	__populate_free(cache);

	result = __init_cleaning_policy(cache);
	if (result) {
		ocf_cache_log(cache, log_err,
				"Cannot initialize cleaning policy\n");
		return result;
	}

	__setup_promotion_policy(cache);

	return 0;
}

static void init_attached_data_structures_recovery(ocf_cache_t cache)
{
	ocf_metadata_init_hash_table(cache);
	ocf_metadata_init_collision(cache);
	__init_parts_attached(cache);
	__reset_stats(cache);
	__init_metadata_version(cache);
}

/****************************************************************
 * Function for removing all uninitialized core objects		*
 * from the cache instance.					*
 * Used in case of cache initialization errors.			*
 ****************************************************************/
static void _ocf_mngt_close_all_uninitialized_cores(
		ocf_cache_t cache)
{
	ocf_volume_t volume;
	int j, i;

	for (j = cache->conf_meta->core_count, i = 0; j > 0; ++i) {
		if (!env_bit_test(i, cache->conf_meta->valid_core_bitmap))
			continue;

		volume = &(cache->core[i].volume);
		ocf_volume_close(volume);

		--j;

		if (cache->core[i].seq_cutoff)
			ocf_core_seq_cutoff_deinit(&cache->core[i]);

		env_free(cache->core[i].counters);
		cache->core[i].counters = NULL;

		env_bit_clear(i, cache->conf_meta->valid_core_bitmap);
	}

	cache->conf_meta->core_count = 0;
}

/**
 * @brief routine loading metadata from cache device
 *  - attempts to open all the underlying cores
 */
static int _ocf_mngt_load_add_cores(
		struct ocf_cache_attach_context *context)
{
	ocf_cache_t cache = context->cache;
	ocf_core_t core;
	ocf_core_id_t core_id;
	int ret = -1;
	uint64_t hd_lines = 0;
	uint64_t length;

	OCF_ASSERT_PLUGGED(cache);

	/* Count value will be re-calculated on the basis of 'valid' flag */
	cache->conf_meta->core_count = 0;

	/* Check in metadata which cores were saved in cache metadata */
	for_each_core_metadata(cache, core, core_id) {
		ocf_volume_t tvolume = NULL;

		if (!core->volume.type)
			goto err;

		tvolume = ocf_mngt_core_pool_lookup(ocf_cache_get_ctx(cache),
				&core->volume.uuid, core->volume.type);
		if (tvolume) {
			/*
			 * Attach bottom device to core structure
			 * in cache
			 */
			ocf_volume_move(&core->volume, tvolume);
			ocf_mngt_core_pool_remove(cache->owner, tvolume);

			core->opened = true;
			ocf_cache_log(cache, log_info,
					"Attached core %u from pool\n",
					core_id);
		} else if (context->cfg.open_cores) {
			ret = ocf_volume_open(&core->volume, NULL);
			if (ret == -OCF_ERR_NOT_OPEN_EXC) {
				ocf_cache_log(cache, log_warn,
						"Cannot open core %u. "
						"Cache is busy", core_id);
			} else if (ret) {
				ocf_cache_log(cache, log_warn,
						"Cannot open core %u", core_id);
			} else {
				core->opened = true;
			}
		}

		env_bit_set(core_id, cache->conf_meta->valid_core_bitmap);
		core->added = true;
		cache->conf_meta->core_count++;
		core->volume.cache = cache;

		if (ocf_mngt_core_init_front_volume(core))
			goto err;

		core->counters =
			env_zalloc(sizeof(*core->counters), ENV_MEM_NORMAL);
		if (!core->counters)
			goto err;

		ret = ocf_core_seq_cutoff_init(core);
		if (ret < 0)
			goto err;

		if (!core->opened) {
			env_bit_set(ocf_cache_state_incomplete,
					&cache->cache_state);
			cache->ocf_core_inactive_count++;
			ocf_cache_log(cache, log_warn,
					"Cannot find core %u in pool"
					", core added as inactive\n", core_id);
			continue;
		}

		length = ocf_volume_get_length(&core->volume);
		if (length != core->conf_meta->length) {
			ocf_cache_log(cache, log_err,
					"Size of core volume doesn't match with"
					" the size stored in cache metadata!");
			goto err;
		}

		hd_lines = ocf_bytes_2_lines(cache, length);

		if (hd_lines) {
			ocf_cache_log(cache, log_info,
				"Disk lines = %" ENV_PRIu64 "\n", hd_lines);
		}
	}

	context->flags.cores_opened = true;
	return 0;

err:
	_ocf_mngt_close_all_uninitialized_cores(cache);

	return -OCF_ERR_START_CACHE_FAIL;
}

void _ocf_mngt_load_init_instance_complete(void *priv, int error)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;
	ocf_cleaning_t cleaning_policy;
	ocf_error_t result;

	if (error) {
		ocf_cache_log(cache, log_err,
				"Cannot read cache metadata\n");
		OCF_PL_FINISH_RET(context->pipeline, -OCF_ERR_START_CACHE_FAIL);
	}

	if (context->metadata.shutdown_status != ocf_metadata_clean_shutdown)
		__populate_free(cache);

	cleaning_policy = cache->conf_meta->cleaning_policy_type;

	if (context->metadata.shutdown_status == ocf_metadata_clean_shutdown)
		result = ocf_cleaning_initialize(cache, cleaning_policy, 0);
	else
		result = ocf_cleaning_initialize(cache, cleaning_policy, 1);

	if (result) {
		ocf_cache_log(cache, log_err,
				"Cannot initialize cleaning policy\n");
		OCF_PL_FINISH_RET(context->pipeline, result);
	}

	ocf_pipeline_next(context->pipeline);
}

/**
 * handle load variant
 */
static void _ocf_mngt_load_init_instance_clean_load(
		struct ocf_cache_attach_context *context)
{
	ocf_cache_t cache = context->cache;

	ocf_metadata_load_all(cache,
			_ocf_mngt_load_init_instance_complete, context);
}

/**
 * handle recovery variant
 */
static void _ocf_mngt_load_init_instance_recovery(
		struct ocf_cache_attach_context *context)
{
	ocf_cache_t cache = context->cache;

	init_attached_data_structures_recovery(cache);

	ocf_cache_log(cache, log_warn,
			"ERROR: Cache device did not shut down properly!\n");

	ocf_cache_log(cache, log_info, "Initiating recovery sequence...\n");

	ocf_metadata_load_recovery(cache,
			_ocf_mngt_load_init_instance_complete, context);
}

static void _ocf_mngt_load_init_instance(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;
	int ret;

	OCF_ASSERT_PLUGGED(cache);

	ret = _ocf_mngt_load_add_cores(context);
	if (ret)
		OCF_PL_FINISH_RET(pipeline, ret);

	if (context->metadata.shutdown_status == ocf_metadata_clean_shutdown)
		_ocf_mngt_load_init_instance_clean_load(context);
	else
		_ocf_mngt_load_init_instance_recovery(context);
}

/**
 * @brief allocate memory for new cache, add it to cache queue, set initial
 * values and running state
 */
static int _ocf_mngt_init_new_cache(struct ocf_cache_mngt_init_params *params)
{
	ocf_cache_t cache = env_vzalloc(sizeof(*cache));
	int result;

	if (!cache)
		return -OCF_ERR_NO_MEM;

	if (ocf_mngt_cache_lock_init(cache)) {
		result = -OCF_ERR_NO_MEM;
		goto alloc_err;
	}

	/* Lock cache during setup - this trylock should always succeed */
	ENV_BUG_ON(ocf_mngt_cache_trylock(cache));

	if (env_mutex_init(&cache->flush_mutex)) {
		result = -OCF_ERR_NO_MEM;
		goto lock_err;
	}

	ENV_BUG_ON(!ocf_refcnt_inc(&cache->refcnt.cache));

	/* start with freezed metadata ref counter to indicate detached device*/
	ocf_refcnt_freeze(&cache->refcnt.metadata);

	env_atomic_set(&(cache->last_access_ms),
			env_ticks_to_msecs(env_get_tick_count()));

	env_bit_set(ocf_cache_state_initializing, &cache->cache_state);

	params->cache = cache;
	params->flags.cache_alloc = true;

	return 0;

lock_err:
	ocf_mngt_cache_lock_deinit(cache);
alloc_err:
	env_vfree(cache);

	return result;
}

static void _ocf_mngt_attach_cache_device(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;
	ocf_volume_type_t type;
	int ret;

	cache->device = env_vzalloc(sizeof(*cache->device));
	if (!cache->device)
		OCF_PL_FINISH_RET(pipeline, -OCF_ERR_NO_MEM);

	context->flags.device_alloc = true;

	/* Prepare UUID of cache volume */
	type = ocf_ctx_get_volume_type(cache->owner, context->cfg.volume_type);
	if (!type) {
		OCF_PL_FINISH_RET(pipeline,
				-OCF_ERR_INVAL_VOLUME_TYPE);
	}

	ret = ocf_volume_init(&cache->device->volume, type,
			&context->cfg.uuid, true);
	if (ret)
		OCF_PL_FINISH_RET(pipeline, ret);

	cache->device->volume.cache = cache;
	context->flags.volume_inited = true;

	/*
	 * Open cache device, It has to be done first because metadata service
	 * need to know size of cache device.
	 */
	ret = ocf_volume_open(&cache->device->volume,
			context->cfg.volume_params);
	if (ret) {
		ocf_cache_log(cache, log_err, "ERROR: Cache not available\n");
		OCF_PL_FINISH_RET(pipeline, ret);
	}
	context->flags.device_opened = true;

	context->volume_size = ocf_volume_get_length(&cache->device->volume);

	/* Check minimum size of cache device */
	if (context->volume_size < OCF_CACHE_SIZE_MIN) {
		ocf_cache_log(cache, log_err, "ERROR: Cache cache size must "
			"be at least %llu [MiB]\n", OCF_CACHE_SIZE_MIN / MiB);
		OCF_PL_FINISH_RET(pipeline, -OCF_ERR_INVAL_CACHE_DEV);
	}

	ocf_pipeline_next(pipeline);
}

/**
 * @brief prepare cache for init. This is first step towards initializing
 *		the cache
 */
static int _ocf_mngt_init_prepare_cache(struct ocf_cache_mngt_init_params *param,
		struct ocf_mngt_cache_config *cfg)
{
	ocf_cache_t cache;
	int ret = 0;

	/* Check if cache with specified name exists */
	ret = ocf_mngt_cache_get_by_name(param->ctx, cfg->name,
					OCF_CACHE_NAME_SIZE, &cache);
	if (!ret) {
		ocf_mngt_cache_put(cache);
		/* Cache already exist */
		ret = -OCF_ERR_CACHE_EXIST;
		goto out;
	}

	ocf_log(param->ctx, log_info, "Inserting cache %s\n", cfg->name);

	ret = _ocf_mngt_init_new_cache(param);
	if (ret)
		goto out;

	cache = param->cache;

	cache->backfill.max_queue_size = cfg->backfill.max_queue_size;
	cache->backfill.queue_unblock_size = cfg->backfill.queue_unblock_size;

	param->flags.cache_locked = true;

	cache->pt_unaligned_io = cfg->pt_unaligned_io;
	cache->use_submit_io_fast = cfg->use_submit_io_fast;

	cache->metadata.is_volatile = cfg->metadata_volatile;

out:
	return ret;
}

static void _ocf_mngt_test_volume_initial_write_complete(void *priv, int error)
{
	struct ocf_cache_attach_context *context = priv;

	OCF_PL_NEXT_ON_SUCCESS_RET(context->test.pipeline, error);
}

static void _ocf_mngt_test_volume_initial_write(
		ocf_pipeline_t test_pipeline, void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;

	/*
	 * Write buffer filled with "1"
	 */

	ENV_BUG_ON(env_memset(context->test.rw_buffer, PAGE_SIZE, 1));

	ocf_submit_cache_page(cache, context->test.reserved_lba_addr,
			OCF_WRITE, context->test.rw_buffer,
			_ocf_mngt_test_volume_initial_write_complete, context);
}

static void _ocf_mngt_test_volume_first_read_complete(void *priv, int error)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;
	int ret, diff;

	if (error)
		OCF_PL_FINISH_RET(context->test.pipeline, error);

	ret = env_memcmp(context->test.rw_buffer, PAGE_SIZE,
			context->test.cmp_buffer, PAGE_SIZE, &diff);
	if (ret)
		OCF_PL_FINISH_RET(context->test.pipeline, ret);

	if (diff) {
		/* we read back different data than what we had just
		   written - this is fatal error */
		OCF_PL_FINISH_RET(context->test.pipeline, -OCF_ERR_IO);
	}

	if (!ocf_volume_is_atomic(&cache->device->volume)) {
		/* If not atomic, stop testing here */
		OCF_PL_FINISH_RET(context->test.pipeline, 0);
	}

	ocf_pipeline_next(context->test.pipeline);
}

static void _ocf_mngt_test_volume_first_read(
		ocf_pipeline_t test_pipeline, void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;

	/*
	 * First read
	 */

	ENV_BUG_ON(env_memset(context->test.rw_buffer, PAGE_SIZE, 0));
	ENV_BUG_ON(env_memset(context->test.cmp_buffer, PAGE_SIZE, 1));

	ocf_submit_cache_page(cache, context->test.reserved_lba_addr,
			OCF_READ, context->test.rw_buffer,
			_ocf_mngt_test_volume_first_read_complete, context);
}

static void _ocf_mngt_test_volume_discard_complete(void *priv, int error)
{
	struct ocf_cache_attach_context *context = priv;

	OCF_PL_NEXT_ON_SUCCESS_RET(context->test.pipeline, error);
}

static void _ocf_mngt_test_volume_discard(
		ocf_pipeline_t test_pipeline, void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;

	/*
	 * Submit discard request
	 */

	ocf_submit_volume_discard(&cache->device->volume,
			context->test.reserved_lba_addr, PAGE_SIZE,
			_ocf_mngt_test_volume_discard_complete, context);
}

static void _ocf_mngt_test_volume_second_read_complete(void *priv, int error)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;
	int ret, diff;

	if (error)
		OCF_PL_FINISH_RET(context->test.pipeline, error);

	ret = env_memcmp(context->test.rw_buffer, PAGE_SIZE,
			context->test.cmp_buffer, PAGE_SIZE, &diff);
	if (ret)
		OCF_PL_FINISH_RET(context->test.pipeline, ret);

	if (diff) {
		/* discard does not cause target adresses to return 0 on
		   subsequent read */
		cache->device->volume.features.discard_zeroes = 0;
	}

	ocf_pipeline_next(context->test.pipeline);
}

static void _ocf_mngt_test_volume_second_read(
		ocf_pipeline_t test_pipeline, void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;

	/*
	 * Second read
	 */

	ENV_BUG_ON(env_memset(context->test.rw_buffer, PAGE_SIZE, 1));
	ENV_BUG_ON(env_memset(context->test.cmp_buffer, PAGE_SIZE, 0));

	ocf_submit_cache_page(cache, context->test.reserved_lba_addr,
			OCF_READ, context->test.rw_buffer,
			_ocf_mngt_test_volume_second_read_complete, context);
}

static void _ocf_mngt_test_volume_finish(ocf_pipeline_t pipeline,
		void *priv, int error)
{
	struct ocf_cache_attach_context *context = priv;

	env_free(context->test.rw_buffer);
	env_free(context->test.cmp_buffer);

	ocf_pipeline_destroy(context->test.pipeline);

	OCF_PL_NEXT_ON_SUCCESS_RET(context->pipeline, error);
}

struct ocf_pipeline_properties _ocf_mngt_test_volume_pipeline_properties = {
	.priv_size = 0,
	.finish = _ocf_mngt_test_volume_finish,
	.steps = {
		OCF_PL_STEP(_ocf_mngt_test_volume_initial_write),
		OCF_PL_STEP(_ocf_mngt_test_volume_first_read),
		OCF_PL_STEP(_ocf_mngt_test_volume_discard),
		OCF_PL_STEP(_ocf_mngt_test_volume_second_read),
		OCF_PL_STEP_TERMINATOR(),
	},
};

static void _ocf_mngt_test_volume(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;
	ocf_pipeline_t test_pipeline;
	int result;

	cache->device->volume.features.discard_zeroes = 1;

	if (!context->cfg.perform_test)
		OCF_PL_NEXT_RET(pipeline);

	context->test.reserved_lba_addr = ocf_metadata_get_reserved_lba(cache);

	context->test.rw_buffer = env_malloc(PAGE_SIZE, ENV_MEM_NORMAL);
	if (!context->test.rw_buffer)
		OCF_PL_FINISH_RET(pipeline, -OCF_ERR_NO_MEM);

	context->test.cmp_buffer = env_malloc(PAGE_SIZE, ENV_MEM_NORMAL);
	if (!context->test.cmp_buffer)
		goto err_buffer;

	result = ocf_pipeline_create(&test_pipeline, cache,
			&_ocf_mngt_test_volume_pipeline_properties);
	if (result)
		goto err_pipeline;

	ocf_pipeline_set_priv(test_pipeline, context);

	context->test.pipeline = test_pipeline;

	OCF_PL_NEXT_RET(test_pipeline);

err_pipeline:
	env_free(context->test.rw_buffer);
err_buffer:
	env_free(context->test.cmp_buffer);
	OCF_PL_FINISH_RET(pipeline, -OCF_ERR_NO_MEM);
}

static void _ocf_mngt_attach_read_properties_end(void *priv, int error,
		struct ocf_metadata_load_properties *properties)
{
	struct ocf_cache_attach_context *context = priv;

	if (error != -OCF_ERR_NO_METADATA) {
		if (!error) {
			/*
			 * To prevent silent metadata overriding, return error if old
			 * metadata was detected when attempting to attach cache.
			 */
			OCF_PL_FINISH_RET(context->pipeline, -OCF_ERR_METADATA_FOUND);
		}
		OCF_PL_FINISH_RET(context->pipeline, error);
	}

	/* No metadata exists on the device */
	OCF_PL_NEXT_RET(context->pipeline);
}

static void _ocf_mngt_load_read_properties_end(void *priv, int error,
		struct ocf_metadata_load_properties *properties)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;

	if (error)
		OCF_PL_FINISH_RET(context->pipeline, error);

	/*
	 * Check if name loaded from disk is the same as present one.
	 */
	if (env_strncmp(cache->conf_meta->name, OCF_CACHE_NAME_SIZE,
			properties->cache_name, OCF_CACHE_NAME_SIZE)) {
		OCF_PL_FINISH_RET(context->pipeline, -OCF_ERR_CACHE_NAME_MISMATCH);
	}

	context->metadata.shutdown_status = properties->shutdown_status;
	context->metadata.dirty_flushed = properties->dirty_flushed;
	context->metadata.line_size = properties->line_size;
	cache->conf_meta->metadata_layout = properties->layout;
	cache->conf_meta->cache_mode = properties->cache_mode;

	ocf_pipeline_next(context->pipeline);
}

static void _ocf_mngt_init_properties(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;

	OCF_ASSERT_PLUGGED(cache);

	context->metadata.shutdown_status = ocf_metadata_clean_shutdown;
	context->metadata.dirty_flushed = DIRTY_FLUSHED;
	context->metadata.line_size = context->cfg.cache_line_size;

	ocf_pipeline_next(pipeline);
}

static void _ocf_mngt_attach_read_properties(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;

	if (context->cfg.force)
		OCF_PL_NEXT_RET(pipeline);

	ocf_metadata_load_properties(&cache->device->volume,
			_ocf_mngt_attach_read_properties_end, context);
}

static void _ocf_mngt_load_read_properties(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;

	ocf_metadata_load_properties(&cache->device->volume,
			_ocf_mngt_load_read_properties_end, context);
}

static void _ocf_mngt_attach_prepare_metadata(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;
	int ret;

	context->metadata.line_size = context->metadata.line_size ?:
			cache->metadata.settings.size;

	/*
	 * Initialize variable size metadata segments
	 */
	ret = ocf_metadata_init_variable_size(cache, context->volume_size,
			context->metadata.line_size,
			cache->conf_meta->metadata_layout);
	if (ret)
		OCF_PL_FINISH_RET(pipeline, ret);

	context->flags.attached_metadata_inited = true;

	ret = ocf_concurrency_init(cache);
	if (ret)
		OCF_PL_FINISH_RET(pipeline, ret);

	context->flags.concurrency_inited = 1;

	ocf_pipeline_next(pipeline);
}

/**
 * @brief initializing cache anew (not loading or recovering)
 */
static void _ocf_mngt_attach_init_instance(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;
	ocf_error_t result;

	result = init_attached_data_structures(cache);
	if (result)
		OCF_PL_FINISH_RET(pipeline, result);

	/* In initial cache state there is no dirty data, so all dirty data is
	   considered to be flushed
	 */
	cache->conf_meta->dirty_flushed = true;

	ocf_pipeline_next(pipeline);
}

uint64_t _ocf_mngt_calculate_ram_needed(ocf_cache_line_size_t line_size,
		uint64_t volume_size)
{
	uint64_t const_data_size;
	uint64_t cache_line_no;
	uint64_t data_per_line;
	uint64_t min_free_ram;

	/* Superblock + per core metadata */
	const_data_size = 100 * MiB;

	/* Cache metadata */
	cache_line_no = volume_size / line_size;
	data_per_line = (68 + (2 * (line_size / KiB / 4)));

	min_free_ram = const_data_size + cache_line_no * data_per_line;

	/* 110% of calculated value */
	min_free_ram = (11 * min_free_ram) / 10;

	return min_free_ram;
}

int ocf_mngt_get_ram_needed(ocf_cache_t cache,
		struct ocf_mngt_cache_device_config *cfg, uint64_t *ram_needed)
{
	ocf_volume_t volume;
	ocf_volume_type_t type;
	ocf_cache_line_size_t line_size;
	uint64_t volume_size;
	int result;

	OCF_CHECK_NULL(cache);
	OCF_CHECK_NULL(cfg);
	OCF_CHECK_NULL(ram_needed);

	type = ocf_ctx_get_volume_type(cache->owner, cfg->volume_type);
	if (!type)
		return -OCF_ERR_INVAL_VOLUME_TYPE;

	result = ocf_volume_create(&volume, type,
			&cfg->uuid);
	if (result)
		return result;

	result = ocf_volume_open(volume, cfg->volume_params);
	if (result) {
		ocf_volume_destroy(volume);
		return result;
	}

	line_size = ocf_line_size(cache);
	volume_size = ocf_volume_get_length(volume);
	*ram_needed = _ocf_mngt_calculate_ram_needed(line_size, volume_size);

	ocf_volume_close(volume);
	ocf_volume_destroy(volume);

	return 0;
}

/**
 * @brief for error handling do partial cleanup of datastructures upon
 * premature function exit.
 *
 * @param ctx OCF context
 * @param params - startup params containing initialization status flags.
 *
 */
static void _ocf_mngt_init_handle_error(ocf_ctx_t ctx,
		struct ocf_cache_mngt_init_params *params)
{
	ocf_cache_t cache = params->cache;

	if (!params->flags.cache_alloc)
		return;

	if (params->flags.metadata_inited)
		ocf_metadata_deinit(cache);

	if (!params->flags.added_to_list)
		return;

	env_rmutex_lock(&ctx->lock);

	list_del(&cache->list);
	env_vfree(cache);

	env_rmutex_unlock(&ctx->lock);
}

static void _ocf_mngt_attach_handle_error(
		struct ocf_cache_attach_context *context)
{
	ocf_cache_t cache = context->cache;

	if (context->flags.cleaner_started)
		ocf_stop_cleaner(cache);

	if (context->flags.promotion_initialized)
		__deinit_promotion_policy(cache);

	if (context->flags.cores_opened)
		_ocf_mngt_close_all_uninitialized_cores(cache);

	if (context->flags.attached_metadata_inited)
		ocf_metadata_deinit_variable_size(cache);

	if (context->flags.device_opened)
		ocf_volume_close(&cache->device->volume);

	if (context->flags.concurrency_inited)
		ocf_concurrency_deinit(cache);

	if (context->flags.volume_inited)
		ocf_volume_deinit(&cache->device->volume);

	if (context->flags.device_alloc)
		env_vfree(cache->device);

	ocf_pipeline_destroy(cache->stop_pipeline);
}

static void _ocf_mngt_cache_init(ocf_cache_t cache,
		struct ocf_cache_mngt_init_params *params)
{
	/*
	 * Super block elements initialization
	 */
	cache->conf_meta->cache_mode = params->metadata.cache_mode;
	cache->conf_meta->metadata_layout = params->metadata.layout;
	cache->conf_meta->promotion_policy_type = params->metadata.promotion_policy;

	INIT_LIST_HEAD(&cache->io_queues);

	/* Init Partitions */
	ocf_user_part_init(cache);
	__init_free(cache);

	__init_cores(cache);
	__init_metadata_version(cache);
	__init_partitions(cache);
}

static int _ocf_mngt_cache_start(ocf_ctx_t ctx, ocf_cache_t *cache,
		struct ocf_mngt_cache_config *cfg, void *priv)
{
	struct ocf_cache_mngt_init_params params;
	ocf_cache_t tmp_cache;
	int result;

	ENV_BUG_ON(env_memset(&params, sizeof(params), 0));

	params.ctx = ctx;
	params.metadata.cache_mode = cfg->cache_mode;
	params.metadata.layout = cfg->metadata_layout;
	params.metadata.line_size = cfg->cache_line_size;
	params.metadata_volatile = cfg->metadata_volatile;
	params.metadata.promotion_policy = cfg->promotion_policy;
	params.locked = cfg->locked;

	result = env_rmutex_lock_interruptible(&ctx->lock);
	if (result)
		goto _cache_mngt_init_instance_ERROR;

	/* Prepare cache */
	result = _ocf_mngt_init_prepare_cache(&params, cfg);
	if (result) {
		env_rmutex_unlock(&ctx->lock);
		goto _cache_mngt_init_instance_ERROR;
	}

	tmp_cache = params.cache;
	tmp_cache->owner = ctx;
	tmp_cache->priv = priv;

	/*
	 * Initialize metadata selected segments of metadata in memory
	 */
	result = ocf_metadata_init(tmp_cache, params.metadata.line_size);
	if (result) {
		env_rmutex_unlock(&ctx->lock);
		result =  -OCF_ERR_NO_MEM;
		goto _cache_mngt_init_instance_ERROR;
	}
	params.flags.metadata_inited = true;

	result = ocf_cache_set_name(tmp_cache, cfg->name, OCF_CACHE_NAME_SIZE);
	if (result) {
		env_rmutex_unlock(&ctx->lock);
		goto _cache_mngt_init_instance_ERROR;
	}

	list_add_tail(&tmp_cache->list, &ctx->caches);
	params.flags.added_to_list = true;
	env_rmutex_unlock(&ctx->lock);

	ocf_cache_log(tmp_cache, log_debug, "Metadata initialized\n");

	_ocf_mngt_cache_init(tmp_cache, &params);

	ocf_ctx_get(ctx);

	if (!params.locked) {
		/* User did not request to lock cache instance after creation -
		   unlock it here since we have acquired the lock to
		   perform management operations. */
		ocf_mngt_cache_unlock(tmp_cache);
		params.flags.cache_locked = false;
	}

	*cache = tmp_cache;

	return 0;

_cache_mngt_init_instance_ERROR:
	_ocf_mngt_init_handle_error(ctx, &params);
	*cache = NULL;
	return result;
}

static void _ocf_mngt_cache_set_valid(ocf_cache_t cache)
{
	/*
	 * Clear initialization state and set the valid bit so we know
	 * its in use.
	 */
	env_bit_clear(ocf_cache_state_initializing, &cache->cache_state);
	env_bit_set(ocf_cache_state_running, &cache->cache_state);
}

static void _ocf_mngt_init_attached_nonpersistent(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;

	env_atomic_set(&cache->fallback_pt_error_counter, 0);

	ocf_pipeline_next(pipeline);
}

static void _ocf_mngt_copy_uuid_data(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	struct ocf_mngt_cache_device_config *cfg = &context->cfg;
	void *data;
	int result;

	data = env_vmalloc(cfg->uuid.size);
	if (!data)
		OCF_PL_FINISH_RET(pipeline, -OCF_ERR_NO_MEM);

	result = env_memcpy(data, cfg->uuid.size, cfg->uuid.data,
			cfg->uuid.size);
	if (result) {
		env_vfree(data);
		OCF_PL_FINISH_RET(pipeline, -OCF_ERR_INVAL);
	}

	context->cfg.uuid.data = data;

	ocf_pipeline_next(pipeline);
}

static void _ocf_mngt_attach_check_ram(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;
	ocf_cache_line_size_t line_size = context->metadata.line_size;
	uint64_t volume_size = ocf_volume_get_length(&cache->device->volume);
	uint64_t min_free_ram;
	uint64_t free_ram;

	min_free_ram = _ocf_mngt_calculate_ram_needed(line_size, volume_size);

	free_ram = env_get_free_memory();

	if (free_ram < min_free_ram) {
		ocf_cache_log(cache, log_err, "Not enough free RAM for cache "
				"metadata to start cache\n");
		ocf_cache_log(cache, log_err,
				"Available RAM: %" ENV_PRIu64 " B\n", free_ram);
		ocf_cache_log(cache, log_err, "Needed RAM: %" ENV_PRIu64 " B\n",
				min_free_ram);
		OCF_PL_FINISH_RET(pipeline, -OCF_ERR_NO_FREE_RAM);
	}

	ocf_pipeline_next(pipeline);
}


static void _ocf_mngt_load_superblock_complete(void *priv, int error)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;

	if (cache->conf_meta->cachelines !=
			ocf_metadata_get_cachelines_count(cache)) {
		ocf_cache_log(cache, log_err,
				"ERROR: Cache device size mismatch!\n");
		OCF_PL_FINISH_RET(context->pipeline,
				-OCF_ERR_START_CACHE_FAIL);
	}

	if (error) {
		ocf_cache_log(cache, log_err,
				"ERROR: Cannot load cache state\n");
		OCF_PL_FINISH_RET(context->pipeline,
				-OCF_ERR_START_CACHE_FAIL);
	}

	ocf_pipeline_next(context->pipeline);
}

static void _ocf_mngt_load_superblock(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;

	ocf_cache_log(cache, log_info, "Loading cache state...\n");
	ocf_metadata_load_superblock(cache,
			_ocf_mngt_load_superblock_complete, context);
}

static void _ocf_mngt_init_cleaner(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;
	int result;

	result = ocf_start_cleaner(cache);
	if (result) {
		ocf_cache_log(cache, log_err,
				"Error while starting cleaner\n");
		OCF_PL_FINISH_RET(pipeline, result);
	}
	context->flags.cleaner_started = true;

	ocf_pipeline_next(pipeline);
}

static void _ocf_mngt_init_promotion(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;
	int result;

	result = ocf_promotion_init(cache, cache->conf_meta->promotion_policy_type);
	if (result) {
		ocf_cache_log(cache, log_err,
				"Cannot initialize promotion policy\n");
		OCF_PL_FINISH_RET(pipeline, result);
	}
	context->flags.promotion_initialized = true;

	ocf_pipeline_next(pipeline);
}

static void _ocf_mngt_attach_flush_metadata_complete(void *priv, int error)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;

	if (error) {
		ocf_cache_log(cache, log_err,
				"ERROR: Cannot save cache state\n");
		OCF_PL_FINISH_RET(context->pipeline, -OCF_ERR_WRITE_CACHE);
	}

	ocf_pipeline_next(context->pipeline);
}

static void _ocf_mngt_attach_flush_metadata(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;

	ocf_metadata_flush_all(cache,
			_ocf_mngt_attach_flush_metadata_complete, context);
}

static void _ocf_mngt_attach_discard_complete(void *priv, int error)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;
	bool discard = cache->device->volume.features.discard_zeroes;

	if (error) {
		ocf_cache_log(cache, log_warn, "%s failed\n",
				discard ? "Discarding whole cache device" :
					"Overwriting cache with zeroes");

		if (ocf_volume_is_atomic(&cache->device->volume)) {
			ocf_cache_log(cache, log_err, "This step is required"
					" for atomic mode!\n");
			OCF_PL_FINISH_RET(context->pipeline, error);
		}

		ocf_cache_log(cache, log_warn, "This may impact cache"
				" performance!\n");
	}

	ocf_pipeline_next(context->pipeline);
}

static void _ocf_mngt_attach_discard(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;
	uint64_t addr = cache->device->metadata_offset;
	uint64_t length = ocf_volume_get_length(&cache->device->volume) - addr;
	bool discard = cache->device->volume.features.discard_zeroes;

	if (!context->cfg.discard_on_start)
		OCF_PL_NEXT_RET(pipeline);

	if (!discard && ocf_volume_is_atomic(&cache->device->volume)) {
		/* discard doesn't zero data - need to explicitly write zeros */
		ocf_submit_write_zeros(&cache->device->volume, addr, length,
				_ocf_mngt_attach_discard_complete, context);
	} else {
		/* Discard volume after metadata */
		ocf_submit_volume_discard(&cache->device->volume, addr, length,
				_ocf_mngt_attach_discard_complete, context);
	}
}

static void _ocf_mngt_attach_flush_complete(void *priv, int error)
{
	struct ocf_cache_attach_context *context = priv;

	OCF_PL_NEXT_ON_SUCCESS_RET(context->pipeline, error);
}

static void _ocf_mngt_attach_flush(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;
	bool discard = cache->device->volume.features.discard_zeroes;

	if (!discard && ocf_volume_is_atomic(&cache->device->volume)) {
		ocf_submit_volume_flush(&cache->device->volume,
				_ocf_mngt_attach_flush_complete, context);
	} else {
		ocf_pipeline_next(pipeline);
	}
}

static void _ocf_mngt_attach_shutdown_status_complete(void *priv, int error)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;

	if (error) {
		ocf_cache_log(cache, log_err, "Cannot flush shutdown status\n");
		OCF_PL_FINISH_RET(context->pipeline, -OCF_ERR_WRITE_CACHE);
	}

	ocf_pipeline_next(context->pipeline);
}

static void _ocf_mngt_attach_shutdown_status(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;

	/* clear clean shutdown status */
	ocf_metadata_set_shutdown_status(cache, ocf_metadata_dirty_shutdown,
		_ocf_mngt_attach_shutdown_status_complete, context);
}

static void _ocf_mngt_attach_post_init(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_cache_attach_context *context = priv;
	ocf_cache_t cache = context->cache;

	ocf_cleaner_refcnt_unfreeze(cache);
	ocf_refcnt_unfreeze(&cache->refcnt.metadata);

	ocf_cache_log(cache, log_debug, "Cache attached\n");

	ocf_pipeline_next(pipeline);
}

static void _ocf_mngt_cache_attach_finish(ocf_pipeline_t pipeline,
		void *priv, int error)
{
	struct ocf_cache_attach_context *context = priv;

	if (error)
		_ocf_mngt_attach_handle_error(context);

	context->cmpl(context->cache, context->priv1, context->priv2, error);

	env_vfree(context->cfg.uuid.data);
	ocf_pipeline_destroy(context->pipeline);
}

struct ocf_pipeline_properties _ocf_mngt_cache_attach_pipeline_properties = {
	.priv_size = sizeof(struct ocf_cache_attach_context),
	.finish = _ocf_mngt_cache_attach_finish,
	.steps = {
		OCF_PL_STEP(_ocf_mngt_copy_uuid_data),
		OCF_PL_STEP(_ocf_mngt_init_attached_nonpersistent),
		OCF_PL_STEP(_ocf_mngt_attach_cache_device),
		OCF_PL_STEP(_ocf_mngt_init_properties),
		OCF_PL_STEP(_ocf_mngt_attach_read_properties),
		OCF_PL_STEP(_ocf_mngt_attach_check_ram),
		OCF_PL_STEP(_ocf_mngt_attach_prepare_metadata),
		OCF_PL_STEP(_ocf_mngt_test_volume),
		OCF_PL_STEP(_ocf_mngt_init_cleaner),
		OCF_PL_STEP(_ocf_mngt_init_promotion),
		OCF_PL_STEP(_ocf_mngt_attach_init_instance),
		OCF_PL_STEP(_ocf_mngt_attach_flush_metadata),
		OCF_PL_STEP(_ocf_mngt_attach_discard),
		OCF_PL_STEP(_ocf_mngt_attach_flush),
		OCF_PL_STEP(_ocf_mngt_attach_shutdown_status),
		OCF_PL_STEP(_ocf_mngt_attach_post_init),
		OCF_PL_STEP_TERMINATOR(),
	},
};

struct ocf_pipeline_properties _ocf_mngt_cache_load_pipeline_properties = {
	.priv_size = sizeof(struct ocf_cache_attach_context),
	.finish = _ocf_mngt_cache_attach_finish,
	.steps = {
		OCF_PL_STEP(_ocf_mngt_copy_uuid_data),
		OCF_PL_STEP(_ocf_mngt_init_attached_nonpersistent),
		OCF_PL_STEP(_ocf_mngt_attach_cache_device),
		OCF_PL_STEP(_ocf_mngt_init_properties),
		OCF_PL_STEP(_ocf_mngt_load_read_properties),
		OCF_PL_STEP(_ocf_mngt_attach_check_ram),
		OCF_PL_STEP(_ocf_mngt_attach_prepare_metadata),
		OCF_PL_STEP(_ocf_mngt_test_volume),
		OCF_PL_STEP(_ocf_mngt_load_superblock),
		OCF_PL_STEP(_ocf_mngt_init_cleaner),
		OCF_PL_STEP(_ocf_mngt_init_promotion),
		OCF_PL_STEP(_ocf_mngt_load_init_instance),
		OCF_PL_STEP(_ocf_mngt_attach_flush_metadata),
		OCF_PL_STEP(_ocf_mngt_attach_shutdown_status),
		OCF_PL_STEP(_ocf_mngt_attach_post_init),
		OCF_PL_STEP_TERMINATOR(),
	},
};

typedef void (*_ocf_mngt_cache_unplug_end_t)(void *context, int error);

struct _ocf_mngt_cache_unplug_context {
	_ocf_mngt_cache_unplug_end_t cmpl;
	void *priv;
	ocf_cache_t cache;
};

struct ocf_mngt_cache_stop_context {
	/* unplug context - this is private structure of _ocf_mngt_cache_unplug,
	 * it is member of stop context only to reserve memory in advance for
	 * _ocf_mngt_cache_unplug, eliminating the possibility of ENOMEM error
	 * at the point where we are effectively unable to handle it */
	struct _ocf_mngt_cache_unplug_context unplug_context;

	ocf_mngt_cache_stop_end_t cmpl;
	void *priv;
	ocf_pipeline_t pipeline;
	ocf_cache_t cache;
	ocf_ctx_t ctx;
	char cache_name[OCF_CACHE_NAME_SIZE];
	int cache_write_error;
};

static void ocf_mngt_cache_stop_wait_metadata_io_finish(void *priv)
{
	struct ocf_mngt_cache_stop_context *context = priv;

	ocf_pipeline_next(context->pipeline);
}

static void ocf_mngt_cache_stop_wait_metadata_io(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_mngt_cache_stop_context *context = priv;
	ocf_cache_t cache = context->cache;

	ocf_refcnt_freeze(&cache->refcnt.metadata);
	ocf_refcnt_register_zero_cb(&cache->refcnt.metadata,
			ocf_mngt_cache_stop_wait_metadata_io_finish, context);
}

static void ocf_mngt_cache_stop_check_dirty(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_mngt_cache_stop_context *context = priv;
	ocf_cache_t cache = context->cache;

	if (ocf_mngt_cache_is_dirty(cache)) {
		cache->conf_meta->dirty_flushed = DIRTY_NOT_FLUSHED;

		ocf_cache_log(cache, log_warn, "Cache is still dirty. "
				"DO NOT USE your core devices until flushing "
				"dirty data!\n");
	} else {
		cache->conf_meta->dirty_flushed = DIRTY_FLUSHED;
	}

	ocf_pipeline_next(context->pipeline);
}

static void _ocf_mngt_cache_stop_remove_cores(ocf_cache_t cache, bool attached)
{
	ocf_core_t core;
	ocf_core_id_t core_id;
	int no = cache->conf_meta->core_count;

	/* All exported objects removed, cleaning up rest. */
	for_each_core_all(cache, core, core_id) {
		if (!env_bit_test(core_id, cache->conf_meta->valid_core_bitmap))
			continue;

		cache_mngt_core_remove_from_cache(core);
		if (attached)
			cache_mngt_core_remove_from_cleaning_pol(core);
		cache_mngt_core_deinit(core);
		if (--no == 0)
			break;
	}
	ENV_BUG_ON(cache->conf_meta->core_count != 0);
}

static void ocf_mngt_cache_stop_remove_cores(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_mngt_cache_stop_context *context = priv;
	ocf_cache_t cache = context->cache;

	_ocf_mngt_cache_stop_remove_cores(cache, true);

	ocf_pipeline_next(pipeline);
}

static void ocf_mngt_cache_stop_unplug_complete(void *priv, int error)
{
	struct ocf_mngt_cache_stop_context *context = priv;

	if (error) {
		ENV_BUG_ON(error != -OCF_ERR_WRITE_CACHE);
		context->cache_write_error = error;
	}

	ocf_pipeline_next(context->pipeline);
}

static void _ocf_mngt_cache_unplug(ocf_cache_t cache, bool stop,
		struct _ocf_mngt_cache_unplug_context *context,
		_ocf_mngt_cache_unplug_end_t cmpl, void *priv);

static void ocf_mngt_cache_stop_unplug(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_mngt_cache_stop_context *context = priv;
	ocf_cache_t cache = context->cache;

	_ocf_mngt_cache_unplug(cache, true, &context->unplug_context,
			ocf_mngt_cache_stop_unplug_complete, context);
}

static void _ocf_mngt_cache_put_io_queues(ocf_cache_t cache)
{
	ocf_queue_t queue, tmp_queue;

	list_for_each_entry_safe(queue, tmp_queue, &cache->io_queues, list)
		ocf_queue_put(queue);
}

static void ocf_mngt_cache_stop_put_io_queues(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_mngt_cache_stop_context *context = priv;
	ocf_cache_t cache = context->cache;

	_ocf_mngt_cache_put_io_queues(cache);

	ocf_pipeline_next(pipeline);
}

static void ocf_mngt_cache_remove(ocf_ctx_t ctx, ocf_cache_t cache)
{
	/* Mark device uninitialized */
	ocf_refcnt_freeze(&cache->refcnt.cache);

	/* Deinitialize locks */
	ocf_mngt_cache_lock_deinit(cache);
	env_mutex_destroy(&cache->flush_mutex);

	/* Remove cache from the list */
	env_rmutex_lock(&ctx->lock);
	list_del(&cache->list);
	env_rmutex_unlock(&ctx->lock);
}

static void ocf_mngt_cache_stop_finish(ocf_pipeline_t pipeline,
		void *priv, int error)
{
	struct ocf_mngt_cache_stop_context *context = priv;
	ocf_cache_t cache = context->cache;
	ocf_ctx_t ctx = context->ctx;
	int pipeline_error;
	ocf_mngt_cache_stop_end_t pipeline_cmpl;
	void *completion_priv;

	if (!error) {
		ocf_mngt_cache_remove(context->ctx, cache);
	} else {
		/* undo metadata counter freeze */
		ocf_refcnt_unfreeze(&cache->refcnt.metadata);

		env_bit_clear(ocf_cache_state_stopping, &cache->cache_state);
		env_bit_set(ocf_cache_state_running, &cache->cache_state);
	}

	if (!error) {
		if (!context->cache_write_error) {
			ocf_log(ctx, log_info,
					"Cache %s successfully stopped\n",
					context->cache_name);
		} else {
			ocf_log(ctx, log_warn, "Stopped cache %s with errors\n",
					context->cache_name);
		}
	} else {
		ocf_log(ctx, log_err, "Stopping cache %s failed\n",
				context->cache_name);
	}

	/*
	 * FIXME: Destroying pipeline before completing management operation is a
	 * temporary workaround for insufficient object lifetime management in pyocf
	 * Context must not be referenced after destroying pipeline as this is
	 * typically freed upon pipeline destroy.
	 */
	pipeline_error = error ?: context->cache_write_error;
	pipeline_cmpl = context->cmpl;
	completion_priv = context->priv;

	ocf_pipeline_destroy(context->pipeline);

	pipeline_cmpl(cache, completion_priv, pipeline_error);

	if (!error) {
		/* Finally release cache instance */
		ocf_mngt_cache_put(cache);
	}
}

struct ocf_pipeline_properties ocf_mngt_cache_stop_pipeline_properties = {
	.priv_size = sizeof(struct ocf_mngt_cache_stop_context),
	.finish = ocf_mngt_cache_stop_finish,
	.steps = {
		OCF_PL_STEP(ocf_mngt_cache_stop_wait_metadata_io),
		OCF_PL_STEP(ocf_mngt_cache_stop_check_dirty),
		OCF_PL_STEP(ocf_mngt_cache_stop_remove_cores),
		OCF_PL_STEP(ocf_mngt_cache_stop_unplug),
		OCF_PL_STEP(ocf_mngt_cache_stop_put_io_queues),
		OCF_PL_STEP_TERMINATOR(),
	},
};


static void _ocf_mngt_cache_attach(ocf_cache_t cache,
		struct ocf_mngt_cache_device_config *cfg,
		_ocf_mngt_cache_attach_end_t cmpl, void *priv1, void *priv2)
{
	struct ocf_cache_attach_context *context;
	ocf_pipeline_t pipeline;
	int result;

	result = ocf_pipeline_create(&pipeline, cache,
			&_ocf_mngt_cache_attach_pipeline_properties);
	if (result)
		OCF_CMPL_RET(cache, priv1, priv2, -OCF_ERR_NO_MEM);

	result = ocf_pipeline_create(&cache->stop_pipeline, cache,
			&ocf_mngt_cache_stop_pipeline_properties);
	if (result) {
		ocf_pipeline_destroy(pipeline);
		OCF_CMPL_RET(cache, priv1, priv2, -OCF_ERR_NO_MEM);
	}

	context = ocf_pipeline_get_priv(pipeline);

	context->cmpl = cmpl;
	context->priv1 = priv1;
	context->priv2 = priv2;
	context->pipeline = pipeline;

	context->cache = cache;
	context->cfg = *cfg;

	OCF_PL_NEXT_RET(pipeline);
}

static void _ocf_mngt_cache_load(ocf_cache_t cache,
		struct ocf_mngt_cache_device_config *cfg,
		_ocf_mngt_cache_attach_end_t cmpl, void *priv1, void *priv2)
{
	struct ocf_cache_attach_context *context;
	ocf_pipeline_t pipeline;
	int result;

	result = ocf_pipeline_create(&pipeline, cache,
			&_ocf_mngt_cache_load_pipeline_properties);
	if (result)
		OCF_CMPL_RET(cache, priv1, priv2, -OCF_ERR_NO_MEM);

	result = ocf_pipeline_create(&cache->stop_pipeline, cache,
			&ocf_mngt_cache_stop_pipeline_properties);
	if (result) {
		ocf_pipeline_destroy(pipeline);
		OCF_CMPL_RET(cache, priv1, priv2, -OCF_ERR_NO_MEM);
	}

	context = ocf_pipeline_get_priv(pipeline);

	context->cmpl = cmpl;
	context->priv1 = priv1;
	context->priv2 = priv2;
	context->pipeline = pipeline;

	context->cache = cache;
	context->cfg = *cfg;

	OCF_PL_NEXT_RET(pipeline);
}

static int _ocf_mngt_cache_validate_cfg(struct ocf_mngt_cache_config *cfg)
{
	if (!strnlen(cfg->name, OCF_CACHE_NAME_SIZE))
		return -OCF_ERR_INVAL;

	if (!ocf_cache_mode_is_valid(cfg->cache_mode))
		return -OCF_ERR_INVALID_CACHE_MODE;

	if (cfg->promotion_policy >= ocf_promotion_max ||
			cfg->promotion_policy < 0 ) {
		return -OCF_ERR_INVAL;
	}

	if (!ocf_cache_line_size_is_valid(cfg->cache_line_size))
		return -OCF_ERR_INVALID_CACHE_LINE_SIZE;

	if (cfg->metadata_layout >= ocf_metadata_layout_max ||
			cfg->metadata_layout < 0) {
		return -OCF_ERR_INVAL;
	}

	if (cfg->backfill.queue_unblock_size > cfg->backfill.max_queue_size )
		return -OCF_ERR_INVAL;

	return 0;
}

static int _ocf_mngt_cache_validate_device_cfg(
		struct ocf_mngt_cache_device_config *device_cfg)
{
	if (!device_cfg->uuid.data)
		return -OCF_ERR_INVAL;

	if (device_cfg->uuid.size > OCF_VOLUME_UUID_MAX_SIZE)
		return -OCF_ERR_INVAL;

	if (device_cfg->cache_line_size != ocf_cache_line_size_none &&
		!ocf_cache_line_size_is_valid(device_cfg->cache_line_size))
		return -OCF_ERR_INVALID_CACHE_LINE_SIZE;

	return 0;
}

static const char *_ocf_cache_mode_names[ocf_cache_mode_max] = {
	[ocf_cache_mode_wt] = "wt",
	[ocf_cache_mode_wb] = "wb",
	[ocf_cache_mode_wa] = "wa",
	[ocf_cache_mode_pt] = "pt",
	[ocf_cache_mode_wi] = "wi",
	[ocf_cache_mode_wo] = "wo",
};

static const char *_ocf_cache_mode_get_name(ocf_cache_mode_t cache_mode)
{
	if (!ocf_cache_mode_is_valid(cache_mode))
		return NULL;

	return _ocf_cache_mode_names[cache_mode];
}

int ocf_mngt_cache_start(ocf_ctx_t ctx, ocf_cache_t *cache,
		struct ocf_mngt_cache_config *cfg, void *priv)
{
	int result;

	if (!ctx || !cache || !cfg)
		return -OCF_ERR_INVAL;

	result = _ocf_mngt_cache_validate_cfg(cfg);
	if (result)
		return result;

	result = _ocf_mngt_cache_start(ctx, cache, cfg, priv);
	if (!result) {
		_ocf_mngt_cache_set_valid(*cache);

		ocf_cache_log(*cache, log_info, "Successfully added\n");
		ocf_cache_log(*cache, log_info, "Cache mode : %s\n",
			_ocf_cache_mode_get_name(ocf_cache_get_mode(*cache)));
	} else
		ocf_log(ctx, log_err, "%s: Inserting cache failed\n", cfg->name);

	return result;
}

int ocf_mngt_cache_set_mngt_queue(ocf_cache_t cache, ocf_queue_t queue)
{
	OCF_CHECK_NULL(cache);
	OCF_CHECK_NULL(queue);

	if (cache->mngt_queue)
		return -OCF_ERR_INVAL;

	ocf_queue_get(queue);
	cache->mngt_queue = queue;

	return 0;
}

static void _ocf_mngt_cache_attach_complete(ocf_cache_t cache, void *priv1,
		void *priv2, int error)
{
	ocf_mngt_cache_attach_end_t cmpl = priv1;

	if (!error) {
		ocf_cache_log(cache, log_info, "Successfully attached\n");
	} else {
		ocf_cache_log(cache, log_err, "Attaching cache device "
				   "failed\n");
	}

	OCF_CMPL_RET(cache, priv2, error);
}

void ocf_mngt_cache_attach(ocf_cache_t cache,
		struct ocf_mngt_cache_device_config *cfg,
		ocf_mngt_cache_attach_end_t cmpl, void *priv)
{
	int result;

	OCF_CHECK_NULL(cache);
	OCF_CHECK_NULL(cfg);

	if (!cache->mngt_queue)
		OCF_CMPL_RET(cache, priv, -OCF_ERR_INVAL);

	result = _ocf_mngt_cache_validate_device_cfg(cfg);
	if (result)
		OCF_CMPL_RET(cache, priv, result);

	_ocf_mngt_cache_attach(cache, cfg, _ocf_mngt_cache_attach_complete, cmpl, priv);
}

static void _ocf_mngt_cache_unplug_complete(void *priv, int error)
{
	struct _ocf_mngt_cache_unplug_context *context = priv;
	ocf_cache_t cache = context->cache;

	ocf_volume_close(&cache->device->volume);

	ocf_metadata_deinit_variable_size(cache);
	ocf_concurrency_deinit(cache);

	ocf_volume_deinit(&cache->device->volume);

	env_vfree(cache->device);
	cache->device = NULL;

	/* TODO: this should be removed from detach after 'attached' stats
		are better separated in statistics */
	env_atomic_set(&cache->fallback_pt_error_counter, 0);

	context->cmpl(context->priv, error ? -OCF_ERR_WRITE_CACHE : 0);
}

/**
 * @brief Unplug caching device from cache instance. Variable size metadata
 *	  containers are deinitialiazed as well as other cacheline related
 *	  structures. Cache volume is closed.
 *
 * @param cache OCF cache instance
 * @param stop	- true if unplugging during stop - in this case we mark
 *			clean shutdown in metadata and flush all containers.
 *		- false if the device is to be detached from cache - loading
 *			metadata from this device will not be possible.
 * @param context - context for this call, must be zeroed
 * @param cmpl Completion callback
 * @param priv Completion context
 */
static void _ocf_mngt_cache_unplug(ocf_cache_t cache, bool stop,
		struct _ocf_mngt_cache_unplug_context *context,
		_ocf_mngt_cache_unplug_end_t cmpl, void *priv)
{
	ENV_BUG_ON(stop && cache->conf_meta->core_count != 0);

	context->cmpl = cmpl;
	context->priv = priv;
	context->cache = cache;

	ocf_stop_cleaner(cache);

	__deinit_cleaning_policy(cache);
	__deinit_promotion_policy(cache);

	if (!stop) {
		/* Just set correct shutdown status */
		ocf_metadata_set_shutdown_status(cache, ocf_metadata_detached,
				_ocf_mngt_cache_unplug_complete, context);
	} else {
		/* Flush metadata */
		ocf_metadata_flush_all(cache,
				_ocf_mngt_cache_unplug_complete, context);
	}
}

static int _ocf_mngt_cache_load_core_log(ocf_core_t core, void *cntx)
{
	if (ocf_core_state_active == ocf_core_get_state(core))
		ocf_core_log(core, log_info, "Successfully added\n");
	else
		ocf_core_log(core, log_warn, "Failed to initialize\n");

	return 0;
}

static void _ocf_mngt_cache_load_log(ocf_cache_t cache)
{
	ocf_cache_mode_t cache_mode = ocf_cache_get_mode(cache);
	ocf_cleaning_t cleaning_type = cache->conf_meta->cleaning_policy_type;
	ocf_promotion_t promotion_type = cache->conf_meta->promotion_policy_type;

	ocf_cache_log(cache, log_info, "Successfully loaded\n");
	ocf_cache_log(cache, log_info, "Cache mode : %s\n",
			_ocf_cache_mode_get_name(cache_mode));
	ocf_cache_log(cache, log_info, "Cleaning policy : %s\n",
			ocf_cleaning_get_name(cleaning_type));
	ocf_cache_log(cache, log_info, "Promotion policy : %s\n",
			ocf_promotion_policies[promotion_type].name);
	ocf_core_visit(cache, _ocf_mngt_cache_load_core_log,
			cache, false);
}

static void _ocf_mngt_cache_load_complete(ocf_cache_t cache, void *priv1,
		void *priv2, int error)
{
	ocf_mngt_cache_load_end_t cmpl = priv1;

	if (error)
		OCF_CMPL_RET(cache, priv2, error);

	_ocf_mngt_cache_set_valid(cache);
	_ocf_mngt_cache_load_log(cache);

	OCF_CMPL_RET(cache, priv2, 0);
}

void ocf_mngt_cache_load(ocf_cache_t cache,
		struct ocf_mngt_cache_device_config *cfg,
		ocf_mngt_cache_load_end_t cmpl, void *priv)
{
	int result;

	OCF_CHECK_NULL(cache);
	OCF_CHECK_NULL(cfg);

	if (!cache->mngt_queue)
		OCF_CMPL_RET(cache, priv, -OCF_ERR_INVAL);

	/* Load is not allowed in volatile metadata mode */
	if (cache->metadata.is_volatile)
		OCF_CMPL_RET(cache, priv, -OCF_ERR_INVAL);

	/* Load is not allowed with 'force' flag on */
	if (cfg->force) {
		ocf_cache_log(cache, log_err, "Using 'force' flag is forbidden "
				"for load operation.");
		OCF_CMPL_RET(cache, priv, -OCF_ERR_INVAL);
	}

	result = _ocf_mngt_cache_validate_device_cfg(cfg);
	if (result)
		OCF_CMPL_RET(cache, priv, result);

	_ocf_mngt_cache_load(cache, cfg, _ocf_mngt_cache_load_complete, cmpl, priv);
}

static void ocf_mngt_cache_stop_detached(ocf_cache_t cache,
		ocf_mngt_cache_stop_end_t cmpl, void *priv)
{
	_ocf_mngt_cache_stop_remove_cores(cache, false);
	_ocf_mngt_cache_put_io_queues(cache);
	ocf_mngt_cache_remove(cache->owner, cache);
	ocf_cache_log(cache, log_info, "Cache %s successfully stopped\n",
			ocf_cache_get_name(cache));
	cmpl(cache, priv, 0);
	ocf_mngt_cache_put(cache);
}

void ocf_mngt_cache_stop(ocf_cache_t cache,
		ocf_mngt_cache_stop_end_t cmpl, void *priv)
{
	struct ocf_mngt_cache_stop_context *context;
	ocf_pipeline_t pipeline;

	OCF_CHECK_NULL(cache);

	if (!ocf_cache_is_device_attached(cache)) {
		ocf_mngt_cache_stop_detached(cache, cmpl, priv);
		return;
	}

	ENV_BUG_ON(!cache->mngt_queue);

	pipeline = cache->stop_pipeline;
	context = ocf_pipeline_get_priv(pipeline);

	context->cmpl = cmpl;
	context->priv = priv;
	context->pipeline = pipeline;
	context->cache = cache;
	context->ctx = cache->owner;

	ENV_BUG_ON(env_strncpy(context->cache_name, sizeof(context->cache_name),
			ocf_cache_get_name(cache), sizeof(context->cache_name)));

	ocf_cache_log(cache, log_info, "Stopping cache\n");

	env_bit_set(ocf_cache_state_stopping, &cache->cache_state);
	env_bit_clear(ocf_cache_state_running, &cache->cache_state);

	ocf_pipeline_next(pipeline);
}

struct ocf_mngt_cache_save_context {
	ocf_mngt_cache_save_end_t cmpl;
	void *priv;
	ocf_pipeline_t pipeline;
	ocf_cache_t cache;
};

static void ocf_mngt_cache_save_finish(ocf_pipeline_t pipeline,
		void *priv, int error)
{
	struct ocf_mngt_cache_save_context *context = priv;

	context->cmpl(context->cache, context->priv, error);

	ocf_pipeline_destroy(context->pipeline);
}

struct ocf_pipeline_properties ocf_mngt_cache_save_pipeline_properties = {
	.priv_size = sizeof(struct ocf_mngt_cache_save_context),
	.finish = ocf_mngt_cache_save_finish,
	.steps = {
		OCF_PL_STEP_TERMINATOR(),
	},
};

static void ocf_mngt_cache_save_flush_sb_complete(void *priv, int error)
{
	struct ocf_mngt_cache_save_context *context = priv;
	ocf_cache_t cache = context->cache;

	if (error) {
		ocf_cache_log(cache, log_err,
				"Failed to flush superblock! Changes "
				"in cache config are not persistent!\n");
		OCF_PL_FINISH_RET(context->pipeline, -OCF_ERR_WRITE_CACHE);
	}

	ocf_pipeline_next(context->pipeline);
}

void ocf_mngt_cache_save(ocf_cache_t cache,
		ocf_mngt_cache_save_end_t cmpl, void *priv)
{
	struct ocf_mngt_cache_save_context *context;
	ocf_pipeline_t pipeline;
	int result;

	OCF_CHECK_NULL(cache);

	if (!cache->mngt_queue)
		OCF_CMPL_RET(cache, priv, -OCF_ERR_INVAL);

	result = ocf_pipeline_create(&pipeline, cache,
			&ocf_mngt_cache_save_pipeline_properties);
	if (result)
		OCF_CMPL_RET(cache, priv, result);

	context = ocf_pipeline_get_priv(pipeline);

	context->cmpl = cmpl;
	context->priv = priv;
	context->pipeline = pipeline;
	context->cache = cache;

	ocf_metadata_flush_superblock(cache,
			ocf_mngt_cache_save_flush_sb_complete, context);
}

static void _cache_mngt_update_initial_dirty_clines(ocf_cache_t cache)
{
	ocf_core_t core;
	ocf_core_id_t core_id;

	for_each_core(cache, core, core_id) {
		env_atomic_set(&core->runtime_meta->initial_dirty_clines,
				env_atomic_read(&core->runtime_meta->
						dirty_clines));
	}

}

static int _cache_mngt_set_cache_mode(ocf_cache_t cache, ocf_cache_mode_t mode)
{
	ocf_cache_mode_t mode_old = cache->conf_meta->cache_mode;

	/* Check if IO interface type is valid */
	if (!ocf_cache_mode_is_valid(mode))
		return -OCF_ERR_INVAL;

	if (mode == mode_old) {
		ocf_cache_log(cache, log_info, "Cache mode '%s' is already set\n",
				ocf_get_io_iface_name(mode));
		return 0;
	}

	cache->conf_meta->cache_mode = mode;

	if (ocf_mngt_cache_mode_has_lazy_write(mode_old) &&
			!ocf_mngt_cache_mode_has_lazy_write(mode)) {
		_cache_mngt_update_initial_dirty_clines(cache);
	}

	ocf_cache_log(cache, log_info, "Changing cache mode from '%s' to '%s' "
			"successful\n", ocf_get_io_iface_name(mode_old),
			ocf_get_io_iface_name(mode));

	return 0;
}

int ocf_mngt_cache_set_mode(ocf_cache_t cache, ocf_cache_mode_t mode)
{
	int result;

	OCF_CHECK_NULL(cache);

	if (!ocf_cache_mode_is_valid(mode)) {
		ocf_cache_log(cache, log_err, "Cache mode %u is invalid\n",
				mode);
		return -OCF_ERR_INVAL;
	}

	result = _cache_mngt_set_cache_mode(cache, mode);

	if (result) {
		const char *name = ocf_get_io_iface_name(mode);

		ocf_cache_log(cache, log_err, "Setting cache mode '%s' "
				"failed\n", name);
	}

	return result;
}

int ocf_mngt_cache_promotion_set_policy(ocf_cache_t cache, ocf_promotion_t type)
{
	int result;

	ocf_metadata_start_exclusive_access(&cache->metadata.lock);

	result = ocf_promotion_set_policy(cache->promotion_policy, type);

	ocf_metadata_end_exclusive_access(&cache->metadata.lock);

	return result;
}

ocf_promotion_t ocf_mngt_cache_promotion_get_policy(ocf_cache_t cache)
{
	ocf_promotion_t result;

	ocf_metadata_start_shared_access(&cache->metadata.lock, 0);

	result = cache->conf_meta->promotion_policy_type;

	ocf_metadata_end_shared_access(&cache->metadata.lock, 0);

	return result;
}

int ocf_mngt_cache_promotion_get_param(ocf_cache_t cache, ocf_promotion_t type,
		uint8_t param_id, uint32_t *param_value)
{
	int result;

	ocf_metadata_start_shared_access(&cache->metadata.lock, 0);

	result = ocf_promotion_get_param(cache, type, param_id, param_value);

	ocf_metadata_end_shared_access(&cache->metadata.lock, 0);

	return result;
}

int ocf_mngt_cache_promotion_set_param(ocf_cache_t cache, ocf_promotion_t type,
		uint8_t param_id, uint32_t param_value)
{
	int result;

	ocf_metadata_start_exclusive_access(&cache->metadata.lock);

	result = ocf_promotion_set_param(cache, type, param_id, param_value);

	ocf_metadata_end_exclusive_access(&cache->metadata.lock);

	return result;
}

int ocf_mngt_cache_reset_fallback_pt_error_counter(ocf_cache_t cache)
{
	OCF_CHECK_NULL(cache);

	if (ocf_fallback_pt_is_on(cache)) {
		ocf_cache_log(cache, log_info,
				"Fallback Pass Through inactive\n");
	}

	env_atomic_set(&cache->fallback_pt_error_counter, 0);

	return 0;
}

int ocf_mngt_cache_set_fallback_pt_error_threshold(ocf_cache_t cache,
		uint32_t new_threshold)
{
	bool old_fallback_pt_state, new_fallback_pt_state;

	OCF_CHECK_NULL(cache);

	if (new_threshold > OCF_CACHE_FALLBACK_PT_MAX_ERROR_THRESHOLD)
		return -OCF_ERR_INVAL;

	old_fallback_pt_state = ocf_fallback_pt_is_on(cache);

	cache->fallback_pt_error_threshold = new_threshold;

	new_fallback_pt_state = ocf_fallback_pt_is_on(cache);

	if (old_fallback_pt_state != new_fallback_pt_state) {
		if (new_fallback_pt_state) {
			ocf_cache_log(cache, log_info, "Error threshold reached. "
					"Fallback Pass Through activated\n");
		} else {
			ocf_cache_log(cache, log_info, "Fallback Pass Through "
					"inactive\n");
		}
	}

	return 0;
}

int ocf_mngt_cache_get_fallback_pt_error_threshold(ocf_cache_t cache,
		uint32_t *threshold)
{
	OCF_CHECK_NULL(cache);
	OCF_CHECK_NULL(threshold);

	*threshold = cache->fallback_pt_error_threshold;

	return 0;
}

struct ocf_mngt_cache_detach_context {
	/* unplug context - this is private structure of _ocf_mngt_cache_unplug,
	 * it is member of detach context only to reserve memory in advance for
	 * _ocf_mngt_cache_unplug, eliminating the possibility of ENOMEM error
	 * at the point where we are effectively unable to handle it */
	struct _ocf_mngt_cache_unplug_context unplug_context;

	ocf_mngt_cache_detach_end_t cmpl;
	void *priv;
	ocf_pipeline_t pipeline;
	ocf_cache_t cache;
	int cache_write_error;
	struct ocf_cleaner_wait_context cleaner_wait;
};

static void ocf_mngt_cache_detach_flush_cmpl(ocf_cache_t cache,
		void *priv, int error)
{
	struct ocf_mngt_cache_detach_context *context = priv;

	OCF_PL_NEXT_ON_SUCCESS_RET(context->pipeline, error);
}

static void ocf_mngt_cache_detach_flush(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_mngt_cache_detach_context *context = priv;
	ocf_cache_t cache = context->cache;

	ocf_mngt_cache_flush(cache, ocf_mngt_cache_detach_flush_cmpl, context);
}

static void ocf_mngt_cache_detach_stop_cache_io_finish(void *priv)
{
	struct ocf_mngt_cache_detach_context *context = priv;
	ocf_pipeline_next(context->pipeline);
}

static void ocf_mngt_cache_detach_stop_cache_io(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_mngt_cache_detach_context *context = priv;
	ocf_cache_t cache = context->cache;

	ocf_refcnt_freeze(&cache->refcnt.metadata);
	ocf_refcnt_register_zero_cb(&cache->refcnt.metadata,
			ocf_mngt_cache_detach_stop_cache_io_finish, context);
}

static void ocf_mngt_cache_detach_stop_cleaner_io_finish(void *priv)
{
	ocf_pipeline_t pipeline = priv;
	ocf_pipeline_next(pipeline);
}

static void ocf_mngt_cache_detach_stop_cleaner_io(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_mngt_cache_detach_context *context = priv;
	ocf_cache_t cache = context->cache;

	ocf_cleaner_refcnt_freeze(cache);
	ocf_cleaner_refcnt_register_zero_cb(cache, &context->cleaner_wait,
			ocf_mngt_cache_detach_stop_cleaner_io_finish,
			pipeline);
}

static void ocf_mngt_cache_detach_update_metadata(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_mngt_cache_detach_context *context = priv;
	ocf_cache_t cache = context->cache;
	ocf_core_t core;
	ocf_core_id_t core_id;
	int no = cache->conf_meta->core_count;

	/* remove cacheline metadata and cleaning policy meta for all cores */
	for_each_core_metadata(cache, core, core_id) {
		cache_mngt_core_deinit_attached_meta(core);
		cache_mngt_core_remove_from_cleaning_pol(core);
		if (--no == 0)
			break;
	}

	ocf_pipeline_next(context->pipeline);
}

static void ocf_mngt_cache_detach_unplug_complete(void *priv, int error)
{
	struct ocf_mngt_cache_detach_context *context = priv;

	if (error) {
		ENV_BUG_ON(error != -OCF_ERR_WRITE_CACHE);
		context->cache_write_error = error;
	}

	ocf_pipeline_next(context->pipeline);
}

static void ocf_mngt_cache_detach_unplug(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg)
{
	struct ocf_mngt_cache_detach_context *context = priv;
	ocf_cache_t cache = context->cache;

	ENV_BUG_ON(cache->conf_meta->dirty_flushed == DIRTY_NOT_FLUSHED);

	/* Do the actual detach - deinit cacheline metadata,
	 * stop cleaner thread and close cache bottom device */
	_ocf_mngt_cache_unplug(cache, false, &context->unplug_context,
			ocf_mngt_cache_detach_unplug_complete, context);
}

static void ocf_mngt_cache_detach_finish(ocf_pipeline_t pipeline,
		void *priv, int error)
{
	struct ocf_mngt_cache_detach_context *context = priv;
	ocf_cache_t cache = context->cache;

	ocf_refcnt_unfreeze(&cache->refcnt.dirty);

	if (!error) {
		if (!context->cache_write_error) {
			ocf_cache_log(cache, log_info,
				"Device successfully detached\n");
		} else {
			ocf_cache_log(cache, log_warn,
				"Device detached with errors\n");
		}
	} else {
		ocf_cache_log(cache, log_err,
				"Detaching device failed\n");
	}

	context->cmpl(cache, context->priv,
			error ?: context->cache_write_error);

	ocf_pipeline_destroy(context->pipeline);
	ocf_pipeline_destroy(cache->stop_pipeline);
}

struct ocf_pipeline_properties ocf_mngt_cache_detach_pipeline_properties = {
	.priv_size = sizeof(struct ocf_mngt_cache_detach_context),
	.finish = ocf_mngt_cache_detach_finish,
	.steps = {
		OCF_PL_STEP(ocf_mngt_cache_detach_flush),
		OCF_PL_STEP(ocf_mngt_cache_detach_stop_cache_io),
		OCF_PL_STEP(ocf_mngt_cache_detach_stop_cleaner_io),
		OCF_PL_STEP(ocf_mngt_cache_stop_check_dirty),
		OCF_PL_STEP(ocf_mngt_cache_detach_update_metadata),
		OCF_PL_STEP(ocf_mngt_cache_detach_unplug),
		OCF_PL_STEP_TERMINATOR(),
	},
};

void ocf_mngt_cache_detach(ocf_cache_t cache,
		ocf_mngt_cache_detach_end_t cmpl, void *priv)
{
	struct ocf_mngt_cache_detach_context *context;
	ocf_pipeline_t pipeline;
	int result;

	OCF_CHECK_NULL(cache);

	if (!cache->mngt_queue)
		OCF_CMPL_RET(cache, priv, -OCF_ERR_INVAL);

	if (!ocf_cache_is_device_attached(cache))
		OCF_CMPL_RET(cache, priv, -OCF_ERR_INVAL);

	result = ocf_pipeline_create(&pipeline, cache,
			&ocf_mngt_cache_detach_pipeline_properties);
	if (result)
		OCF_CMPL_RET(cache, priv, -OCF_ERR_NO_MEM);

	context = ocf_pipeline_get_priv(pipeline);

	context->cmpl = cmpl;
	context->priv = priv;
	context->pipeline = pipeline;
	context->cache = cache;

	/* prevent dirty io */
	ocf_refcnt_freeze(&cache->refcnt.dirty);

	ocf_pipeline_next(pipeline);
}
