/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __OCF_MNGT_H__
#define __OCF_MNGT_H__

#include "ocf_cache.h"
#include "ocf_core.h"

/**
 * @file
 * @brief OCF management operations definitions
 */

/**
 * @brief Core start configuration
 */
struct ocf_mngt_core_config {
	/**
	 * @brief OCF core name
	 */
	char name[OCF_CORE_NAME_SIZE];

	/**
	 * @brief OCF core volume UUID
	 */
	struct ocf_volume_uuid uuid;

	/**
	 * @brief OCF core volume type
	 */
	uint8_t volume_type;

	/**
	 * @brief Add core to pool if cache isn't present or add core to
	 *	earlier loaded cache
	 */
	bool try_add;

	uint32_t seq_cutoff_threshold;
		/*!< Sequential cutoff threshold (in bytes) */

	uint32_t seq_cutoff_promotion_count;
		/*!< Sequential cutoff promotion request count */

	struct {
		void *data;
		size_t size;
	} user_metadata;
};

/**
 * @brief Initialize core config to default values
 *
 * @note This function doesn't initialize name, uuid and volume_type fields
 *       which have no default values and are required to be set by user.
 *
 * @param[in] cfg Core config stucture
 */
static inline void ocf_mngt_core_config_set_default(
		struct ocf_mngt_core_config *cfg)
{
	cfg->try_add = false;
	cfg->seq_cutoff_threshold = 1024;
	cfg->seq_cutoff_promotion_count = 8;
	cfg->user_metadata.data = NULL;
	cfg->user_metadata.size = 0;
}

/**
 * @brief Get number of OCF caches
 *
 * @param[in] ctx OCF context
 *
 * @retval Number of caches in given OCF instance
 */
uint32_t ocf_mngt_cache_get_count(ocf_ctx_t ctx);

/* Cache instances getters */

/**
 * @brief Get OCF cache by name
 *
 * @note This function on success also increases reference counter
 *       in given cache
 *
 * @param[in] ctx OCF context
 * @param[in] name OCF cache name
 * @param[in] name_len Cache name length
 * @param[out] cache OCF cache handle
 *
 * @retval 0 Get cache successfully
 * @retval -OCF_ERR_CACHE_NOT_EXIST Cache with given name doesn't exist
 */
int ocf_mngt_cache_get_by_name(ocf_ctx_t ctx, const char* name, size_t name_len,
		ocf_cache_t *cache);

/**
 * @brief Increment reference counter of cache
 *
 * @param[in] cache OCF cache handle
 *
 * @retval 0 Reference counter incremented
 * @retval -OCF_ERR_CACHE_NOT_AVAIL cache isn't initialised yet
 */
int ocf_mngt_cache_get(ocf_cache_t cache);

/**
 * @brief Decrease reference counter in cache
 *
 * @note If cache don't have any reference - deallocate it
 *
 * @param[in] cache Handle to cache
 */
void ocf_mngt_cache_put(ocf_cache_t cache);

/**
 * @brief Lock cache for management oparations (write lock, exclusive)

 * @param[in] cache Handle to cache
 * @param[in] error Status error code. Can be one of the following:
 *	0 Cache successfully locked
 *	-OCF_ERR_CACHE_NOT_EXIST Can not lock cache - cache is already stopping
 *	-OCF_ERR_NO_MEM Cannot allocate needed memory
 *	-OCF_ERR_INTR Wait operation interrupted
 */
typedef void (*ocf_mngt_cache_lock_end_t)(ocf_cache_t cache,
		void *priv, int error);

/**
 * @brief Lock cache for management oparations (write lock, exclusive)
 *
 * @param[in] cache Handle to cache
 * @param[in] cmpl Completion callback
 * @param[in] priv Private context of completion callback
 */
void ocf_mngt_cache_lock(ocf_cache_t cache,
		ocf_mngt_cache_lock_end_t cmpl, void *priv);

/**
 * @brief Lock cache for read - assures cache config does not change while
 *		lock is being held, while allowing other users to acquire
 *		read lock in parallel.
 *
 * @param[in] cache Handle to cache
 * @param[in] cmpl Completion callback
 * @param[in] priv Private context of completion callback
 */
void ocf_mngt_cache_read_lock(ocf_cache_t cache,
		ocf_mngt_cache_lock_end_t cmpl, void *priv);

/**
 * @brief Lock cache for management oparations (write lock, exclusive)
 *
 * @param[in] cache Handle to cache
 *
 * @retval 0 Cache successfully locked
 * @retval -OCF_ERR_CACHE_NOT_EXIST Can not lock cache - cache is already
 *					stopping
 * @retval -OCF_ERR_NO_LOCK Lock not acquired
 */
int ocf_mngt_cache_trylock(ocf_cache_t cache);

/**
 * @brief Lock cache for read - assures cache config does not change while
 *		lock is being held, while allowing other users to acquire
 *		read lock in parallel.
 *
 * @param[in] cache Handle to cache
 *
 * @retval 0 Cache successfully locked
 * @retval -OCF_ERR_CACHE_NOT_EXIST Can not lock cache - cache is already
 *					stopping
 * @retval -OCF_ERR_NO_LOCK Lock not acquired
 */
int ocf_mngt_cache_read_trylock(ocf_cache_t cache);

/**
 * @brief Write-unlock cache
 *
 * @param[in] cache Handle to cache
 */
void ocf_mngt_cache_unlock(ocf_cache_t cache);

/**
 * @brief Read-unlock cache
 *
 * @param[in] cache Handle to cache
 */
void ocf_mngt_cache_read_unlock(ocf_cache_t cache);

/**
 * @brief Cache visitor function
 *
 * @param[in] cache Handle to cache
 * @param[in] cntx Visitor function context
 *
 * @retval 0 Success
 * @retval Non-zero Error
 */
typedef int (*ocf_mngt_cache_visitor_t)(ocf_cache_t cache, void *cntx);

/**
 * @brief Loop for each cache
 *
 * @note Visitor function is called for each cache
 *
 * @param[in] ctx OCF context
 * @param[in] visitor OCF cache visitor function
 * @param[in] cntx Context for cache visitor function
 *
 * @retval 0 Success
 * @retval Non-zero Error
 */
int ocf_mngt_cache_visit(ocf_ctx_t ctx, ocf_mngt_cache_visitor_t visitor,
		void *cntx);

/**
 * @brief Loop for each cache reverse
 *
 * @note Visitor function is called for each cache
 *
 * @param[in] ctx OCF context
 * @param[in] visitor OCF cache visitor function
 * @param[in] cntx Context for cache visitor function
 *
 * @retval 0 Success
 * @retval Non-zero Error
 */
int ocf_mngt_cache_visit_reverse(ocf_ctx_t ctx, ocf_mngt_cache_visitor_t visitor,
		void *cntx);

/**
 * @brief Cache start configuration
 */
struct ocf_mngt_cache_config {
	/**
	 * @brief Cache name
	 */
	char name[OCF_CACHE_NAME_SIZE];

	/**
	 * @brief Cache mode
	 */
	ocf_cache_mode_t cache_mode;

	/**
	 * @brief Promotion policy type
	 */
	ocf_promotion_t promotion_policy;

	/**
	 * @brief Cache line size
	 */
	ocf_cache_line_size_t cache_line_size;

	/**
	 * @brief Metadata layout (stripping/sequential)
	 */
	ocf_metadata_layout_t metadata_layout;

	bool metadata_volatile;

	/**
	 * @brief Start cache and keep it locked
	 *
	 * @note In this case caller is able to perform additional activities
	 *		and then shall unlock cache
	 */
	bool locked;

	/**
	 * @brief Use pass-through mode for I/O requests unaligned to 4KiB
	 */
	bool pt_unaligned_io;

	/**
	 * @brief If set, try to submit all I/O in fast path.
	 */
	bool use_submit_io_fast;

	/**
	 * @brief Backfill configuration
	 */
	struct {
		 uint32_t max_queue_size;
		 uint32_t queue_unblock_size;
	} backfill;
};

/**
 * @brief Initialize core config to default values
 *
 * @note This function doesn't initialize name field which has no default
 *       value and is required to be set by user.
 *
 * @param[in] cfg Cache config stucture
 */
static inline void ocf_mngt_cache_config_set_default(
		struct ocf_mngt_cache_config *cfg)
{
	cfg->cache_mode = ocf_cache_mode_default;
	cfg->promotion_policy = ocf_promotion_default;
	cfg->cache_line_size = ocf_cache_line_size_4;
	cfg->metadata_layout = ocf_metadata_layout_default;
	cfg->metadata_volatile = false;
	cfg->backfill.max_queue_size = 65536;
	cfg->backfill.queue_unblock_size = 60000;
	cfg->locked = false;
	cfg->pt_unaligned_io = false;
	cfg->use_submit_io_fast = false;
}

/**
 * @brief Start cache instance
 *
 * @param[in] ctx OCF context
 * @param[out] cache Cache handle
 * @param[in] cfg Starting cache configuration
 * @param[in] priv initial value of priv field in cache
 *
 * @retval 0 Cache started successfully
 * @retval Non-zero Error occurred and starting cache failed
 */
int ocf_mngt_cache_start(ocf_ctx_t ctx, ocf_cache_t *cache,
		struct ocf_mngt_cache_config *cfg, void *priv);

/**
 * @brief Set queue to be used during management operations
 *
 * @param[in] cache Cache object
 * @param[in] queue Queue object
 *
 * @retval 0 Success
 * @retval Non-zero Error occurred
 */
int ocf_mngt_cache_set_mngt_queue(ocf_cache_t cache, ocf_queue_t queue);

/**
 * @brief Completion callback of cache stop operation
 *
 * @param[in] cache Cache handle
 * @param[in] priv Callback context
 * @param[in] error Error code (zero on success)
 */
typedef void (*ocf_mngt_cache_stop_end_t)(ocf_cache_t cache,
		void *priv, int error);

/**
 * @brief Stop cache instance
 *
 * @param[in] cache Cache handle
 * @param[in] cmpl Completion callback
 * @param[in] priv Completion callback context
 */
void ocf_mngt_cache_stop(ocf_cache_t cache,
		ocf_mngt_cache_stop_end_t cmpl, void *priv);

/**
 * @brief Cache attach configuration
 */
struct ocf_mngt_cache_device_config {
	/**
	 * @brief Cache volume UUID
	 */
	struct ocf_volume_uuid uuid;

	/**
	 * @brief Cache line size
	 */
	ocf_cache_line_size_t cache_line_size;

	/**
	 * @brief Cache volume type
	 */
	uint8_t volume_type;

	/**
	 * @brief Automatically open core volumes when loading cache
	 *
	 * If set to false, cache load will not attempt to open core volumes,
	 * and so cores will be marked "inactive" unless their volumes were
	 * earlier added to the core pool. In such case user will be expected
	 * to add cores later using function ocf_mngt_cache_add_core().
	 *
	 * @note This option is meaningful only with ocf_mngt_cache_load().
	 *       When used with ocf_mngt_cache_attach() it's ignored.
	 */
	bool open_cores;

	/**
	 * @brief Ignore warnings and initialize new cache instance
	 *
	 * If set to true, it will force initializing new cache despite the
	 * existing metadata from previous cache instance.
	 *
	 * @note This flag is not allowed when loading existing cache instance.
	 */
	bool force;

	/**
	 * @brief If set, cache features (like discard) are tested
	 *		before starting cache
	 */
	bool perform_test;

	/**
	 * @brief If set, cache device will be discarded on cache start
	 */
	bool discard_on_start;

	/**
	 * @brief Optional opaque volume parameters, passed down to cache volume
	 * open callback
	 */
	void *volume_params;
};

/**
 * @brief Initialize core config to default values
 *
 * @note This function doesn't initiialize uuid and volume_type fields
 *       which have no default values and are required to be set by user.
 *
 * @param[in] cfg Cache device config stucture
 */
static inline void ocf_mngt_cache_device_config_set_default(
		struct ocf_mngt_cache_device_config *cfg)
{
	cfg->cache_line_size = ocf_cache_line_size_none;
	cfg->open_cores = true;
	cfg->force = false;
	cfg->perform_test = true;
	cfg->discard_on_start = true;
	cfg->volume_params = NULL;
}

/**
 * @brief Get amount of free RAM needed to attach cache volume
 *
 * @param[in] cache Cache handle
 * @param[in] cfg Caching device configuration
 * @param[out] ram_needed Amount of RAM needed in bytes
 *
 * @retval 0 Success
 * @retval Non-zero Error occurred
 */
int ocf_mngt_get_ram_needed(ocf_cache_t cache,
		struct ocf_mngt_cache_device_config *cfg, uint64_t *ram_needed);

/**
 * @brief Completion callback of cache attach operation
 *
 * @param[in] cache Cache handle
 * @param[in] priv Callback context
 * @param[in] error Error code (zero on success)
 */
typedef void (*ocf_mngt_cache_attach_end_t)(ocf_cache_t cache,
		void *priv, int error);

/**
 * @brief Attach caching device to cache instance
 *
 * @param[in] cache Cache handle
 * @param[in] cfg Caching device configuration
 * @param[in] cmpl Completion callback
 * @param[in] priv Completion callback context
 */
void ocf_mngt_cache_attach(ocf_cache_t cache,
		struct ocf_mngt_cache_device_config *cfg,
		ocf_mngt_cache_attach_end_t cmpl, void *priv);

/**
 * @brief Completion callback of cache detach operation
 *
 * @param[in] cache Cache handle
 * @param[in] priv Callback context
 * @param[in] error Error code (zero on success)
 */
typedef void (*ocf_mngt_cache_detach_end_t)(ocf_cache_t cache,
		void *priv, int error);

/**
 * @brief Detach caching cache
 *
 * @param[in] cache Cache handle
 * @param[in] cmpl Completion callback
 * @param[in] priv Completion callback context
 */
void ocf_mngt_cache_detach(ocf_cache_t cache,
		ocf_mngt_cache_detach_end_t cmpl, void *priv);

/**
 * @brief Completion callback of cache load operation
 *
 * @param[in] cache Cache handle
 * @param[in] priv Callback context
 * @param[in] error Error code (zero on success)
 */
typedef void (*ocf_mngt_cache_load_end_t)(ocf_cache_t cache,
		void *priv, int error);

/**
 * @brief Load cache instance
 *
 * @param[in] cache Cache handle
 * @param[in] cfg Caching device configuration
 * @param[in] cmpl Completion callback
 * @param[in] priv Completion callback context
 */
void ocf_mngt_cache_load(ocf_cache_t cache,
		struct ocf_mngt_cache_device_config *cfg,
		ocf_mngt_cache_load_end_t cmpl, void *priv);

/* Adding and removing cores */

/**
 * @brief Completion callback of add core operation
 *
 * @param[in] cache Cache handle
 * @param[in] core Core handle on success or NULL on failure
 * @param[in] priv Callback context
 * @param[in] error Error code (zero on success)
 */
typedef void (*ocf_mngt_cache_add_core_end_t)(ocf_cache_t cache,
		ocf_core_t core, void *priv, int error);

/**
 * @brief Add core to cache instance
 *
 * @param[in] cache Cache handle
 * @param[in] cfg Core configuration
 * @param[in] cmpl Completion callback
 * @param[in] priv Completion callback context
 */
void ocf_mngt_cache_add_core(ocf_cache_t cache,
		struct ocf_mngt_core_config *cfg,
		ocf_mngt_cache_add_core_end_t cmpl, void *priv);

/**
 * @brief Completion callback of remove core operation
 *
 * @param[in] priv Callback context
 * @param[in] error Error code (zero on success)
 */
typedef void (*ocf_mngt_cache_remove_core_end_t)(void *priv, int error);

/**
 * @brief Remove core from cache instance
 *
 * @param[in] core Core handle
 * @param[in] cmpl Completion callback
 * @param[in] priv Completion callback context
 */
void ocf_mngt_cache_remove_core(ocf_core_t core,
		ocf_mngt_cache_remove_core_end_t cmpl, void *priv);

/**
 * @brief Completion callback of detach core operation
 *
 * @param[in] priv Callback context
 * @param[in] error Error code (zero on success)
 */
typedef void (*ocf_mngt_cache_detach_core_end_t)(void *priv, int error);

/**
 * @brief Detach core from cache instance
 *
 * @param[in] core Core handle
 * @param[in] cmpl Completion callback
 * @param[in] priv Completion callback context
 */
void ocf_mngt_cache_detach_core(ocf_core_t core,
		ocf_mngt_cache_detach_core_end_t cmpl, void *priv);

/* Flush operations */

/**
 * @brief Completion callback of cache flush operation
 *
 * @param[in] cache Cache handle
 * @param[in] priv Callback context
 * @param[in] error Error code (zero on success)
 */
typedef void (*ocf_mngt_cache_flush_end_t)(ocf_cache_t cache,
		void *priv, int error);

/**
 * @brief Flush data from given cache
 *
 * @param[in] cache Cache handle
 * @param[in] cmpl Completion callback
 * @param[in] priv Completion callback context
 */
void ocf_mngt_cache_flush(ocf_cache_t cache,
		ocf_mngt_cache_flush_end_t cmpl, void *priv);

/**
 * @brief Check if core is dirty
 *
 * @param[in] core Core handle
 *
 * @retval true if core is dirty, false otherwise
 */
bool ocf_mngt_core_is_dirty(ocf_core_t core);

/**
 * @brief Check if cache is dirty
 *
 * @param[in] cache Cache handle
 *
 * @retval true if cache is dirty, false otherwise
 */
bool ocf_mngt_cache_is_dirty(ocf_cache_t cache);

/**
 * @brief Completion callback of core flush operation
 *
 * @param[in] core Core handle
 * @param[in] priv Callback context
 * @param[in] error Error code (zero on success)
 */
typedef void (*ocf_mngt_core_flush_end_t)(ocf_core_t core,
		void *priv, int error);

/**
 * @brief Flush data to given core
 *
 * @param[in] core Core handle
 * @param[in] cmpl Completion callback
 * @param[in] priv Completion callback context
 */
void ocf_mngt_core_flush(ocf_core_t core,
		ocf_mngt_core_flush_end_t cmpl, void *priv);

/**
 * @brief Completion callback of cache purge operation
 *
 * @param[in] cache Cache handle
 * @param[in] priv Callback context
 * @param[in] error Error code (zero on success)
 */
typedef void (*ocf_mngt_cache_purge_end_t)(ocf_cache_t cache,
		void *priv, int error);

/**
 * @brief Purge data from given cache
 *
 * @param[in] cache Cache handle
 * @param[in] cmpl Completion callback
 * @param[in] priv Completion callback context
 */
void ocf_mngt_cache_purge(ocf_cache_t cache,
		ocf_mngt_cache_purge_end_t cmpl, void *priv);

/**
 * @brief Completion callback of core purge operation
 *
 * @param[in] core Core handle
 * @param[in] priv Callback context
 * @param[in] error Error code (zero on success)
 */
typedef void (*ocf_mngt_core_purge_end_t)(ocf_core_t core,
		void *priv, int error);

/**
 * @brief Purge data to given core
 *
 * @param[in] core Core handle
 * @param[in] cmpl Completion callback
 * @param[in] priv Completion callback context
 */
void ocf_mngt_core_purge(ocf_core_t core,
		ocf_mngt_core_purge_end_t cmpl, void *priv);

/**
 * @brief Interrupt existing flushing of cache or core
 *
 * @param[in] cache Cache instance
 */
void ocf_mngt_cache_flush_interrupt(ocf_cache_t cache);

/**
 * @brief Completion callback of save operation
 *
 * @param[in] cache Cache handle
 * @param[in] priv Callback context
 * @param[in] error Error code (zero on success)
 */
typedef void (*ocf_mngt_cache_save_end_t)(ocf_cache_t cache,
		void *priv, int error);

/**
 * @brief Save cache configuration data on cache volume
 *
 * This function should be called after changing cache or core parameters
 * in order to make changes persistent.
 *
 * @param[in] cache Cache handle
 * @param[in] cmpl Completion callback
 * @param[in] priv Completion callback context
 */
void ocf_mngt_cache_save(ocf_cache_t cache,
		ocf_mngt_cache_save_end_t cmpl, void *priv);

/**
 * @brief Determines whether given cache mode has write-back semantics, i.e. it
 * allows for writes to be serviced in cache and lazily propagated to core.
 *
 * @param[in] mode input cache mode
 */
static inline bool ocf_mngt_cache_mode_has_lazy_write(ocf_cache_mode_t mode)
{
	return mode == ocf_cache_mode_wb || mode == ocf_cache_mode_wo;
}

/**
 * @brief Set cache mode in given cache
 *
 * @attention This changes only runtime state. To make changes persistent
 *            use function ocf_mngt_cache_save().
 *
 * @param[in] cache Cache handle
 * @param[in] mode Cache mode to set
 *
 * @retval 0 Cache mode have been set successfully
 * @retval Non-zero Error occurred and cache mode not been set
 */
int ocf_mngt_cache_set_mode(ocf_cache_t cache, ocf_cache_mode_t mode);

/**
 * @brief Completion callback of switch cleaning policy operation
 *
 * @param[in] priv Callback context
 * @param[in] error Error code (zero on success)
 */
typedef void (*ocf_mngt_cache_set_cleaning_policy_end_t)( void *priv,
		int error);

/**
 * @brief Set cleaning policy in given cache
 *
 * @attention This changes only runtime state. To make changes persistent
 *            use function ocf_mngt_cache_save().
 *
 * @param[in] cache Cache handle
 * @param[out] new_policy New cleaning policy
 * @param[in] cmpl Completion callback
 * @param[in] priv Completion callback context
 *
 */
void ocf_mngt_cache_cleaning_set_policy(ocf_cache_t cache,
		ocf_cleaning_t new_policy,
		ocf_mngt_cache_set_cleaning_policy_end_t cmpl, void *priv);

/**
 * @brief Get current cleaning policy from given cache
 *
 * @param[in] cache Cache handle
 * @param[out] type Variable to store current cleaning policy type
 *
 * @retval 0 Policy has been get successfully
 * @retval Non-zero Error occurred and policy has not been get
 */
int ocf_mngt_cache_cleaning_get_policy(ocf_cache_t cache, ocf_cleaning_t *type);

/**
 * @brief Set cleaning parameter in given cache
 *
 * @attention This changes only runtime state. To make changes persistent
 *            use function ocf_mngt_cache_save().
 *
 * @param[in] cache Cache handle
 * @param[in] type Cleaning policy type
 * @param[in] param_id Cleaning policy parameter id
 * @param[in] param_value Cleaning policy parameter value
 *
 * @retval 0 Parameter has been set successfully
 * @retval Non-zero Error occurred and parameter has not been set
 */
int ocf_mngt_cache_cleaning_set_param(ocf_cache_t cache, ocf_cleaning_t type,
		uint32_t param_id, uint32_t param_value);

/**
 * @brief Get cleaning parameter from given cache
 *
 * @param[in] cache Cache handle
 * @param[in] type Cleaning policy type
 * @param[in] param_id Cleaning policy parameter id
 * @param[out] param_value Variable to store parameter value
 *
 * @retval 0 Parameter has been get successfully
 * @retval Non-zero Error occurred and parameter has not been get
 */
int ocf_mngt_cache_cleaning_get_param(ocf_cache_t cache,ocf_cleaning_t type,
		uint32_t param_id, uint32_t *param_value);

/**
 * @brief Set promotion policy in given cache
 *
 * @attention This changes only runtime state. To make changes persistent
 *            use function ocf_mngt_cache_save().
 *
 * @param[in] cache Cache handle
 * @param[in] type Promotion policy type
 *
 * @retval 0 Policy has been set successfully
 * @retval Non-zero Error occurred and policy has not been set
 */
int ocf_mngt_cache_promotion_set_policy(ocf_cache_t cache, ocf_promotion_t type);

/**
 * @brief Get promotion policy in given cache
 *
 * @param[in] cache Cache handle
 *
 * @retval Currently set promotion policy type
 */
ocf_promotion_t ocf_mngt_cache_promotion_get_policy(ocf_cache_t cache);

/**
 * @brief Set promotion policy parameter for given cache
 *
 * @param[in] cache Cache handle
 * @param[in] type Promotion policy type
 * @param[in] param_id Promotion policy parameter id
 * @param[in] param_value Promotion policy parameter value
 *
 * @retval 0 Parameter has been set successfully
 * @retval Non-zero Error occurred and parameter has not been set
 */
int ocf_mngt_cache_promotion_set_param(ocf_cache_t cache, ocf_promotion_t type,
		uint8_t param_id, uint32_t param_value);

/**
 * @brief Get promotion policy parameter for given cache
 *
 * @param[in] cache Cache handle
 * @param[in] type Promotion policy type
 * @param[in] param_id Promotion policy parameter id
 * @param[out] param_value Variable to store parameter value
 *
 * @retval 0 Parameter has been retrieved successfully
 * @retval Non-zero Error occurred and parameter has not been retrieved
 */
int ocf_mngt_cache_promotion_get_param(ocf_cache_t cache, ocf_promotion_t type,
		uint8_t param_id, uint32_t *param_value);

/**
 * @brief IO class configuration
 */
struct ocf_mngt_io_class_config {
	/**
	 * @brief IO class ID
	 */
	uint32_t class_id;

	/**
	 * @brief IO class maximum size
	 */
	uint32_t max_size;

	/**
	 * @brief IO class name
	 */
	const char *name;

	/**
	 * @brief IO class cache mode
	 */
	ocf_cache_mode_t cache_mode;

	/**
	 * @brief IO class eviction priority
	 */
	int16_t prio;
};

struct ocf_mngt_io_classes_config {
	struct ocf_mngt_io_class_config config[OCF_USER_IO_CLASS_MAX];
};

/**
 * @brief Configure IO classes in given cache
 *
 * @attention This changes only runtime state. To make changes persistent
 *            use function ocf_mngt_cache_save().
 *
 * @param[in] cache Cache handle
 * @param[in] cfg IO class configuration
 *
 * @retval 0 Configuration have been set successfully
 * @retval Non-zero Error occurred and configuration not been set
 */
int ocf_mngt_cache_io_classes_configure(ocf_cache_t cache,
		const struct ocf_mngt_io_classes_config *cfg);

/**
 * @brief Asociate new UUID value with given core
 *
 * @attention This changes only runtime state. To make changes persistent
 *            use function ocf_mngt_cache_save().
 *
 * @param[in] core Core object
 * @param[in] uuid new core uuid
 *
 * @retval 0 Success
 * @retval Non-zero Fail
 */
int ocf_mngt_core_set_uuid(ocf_core_t core, const struct ocf_volume_uuid *uuid);

/**
 * @brief Set persistent user metadata for given core
 *
 * @attention This changes only runtime state. To make changes persistent
 *            use function ocf_mngt_cache_save().
 *
 * @param[in] core Core object
 * @param[in] data User data buffer
 * @param[in] size Size of user data buffer
 *
 * @retval 0 Success
 * @retval Non-zero Core getting failed
 */
int ocf_mngt_core_set_user_metadata(ocf_core_t core, void *data, size_t size);

/**
 * @brief Get persistent user metadata from given core
 *
 * @param[in] core Core object
 * @param[out] data User data buffer
 * @param[in] size Size of user data buffer
 *
 * @retval 0 Success
 * @retval Non-zero Core getting failed
 */
int ocf_mngt_core_get_user_metadata(ocf_core_t core, void *data, size_t size);

/**
 * @brief Set core sequential cutoff threshold
 *
 * @attention This changes only runtime state. To make changes persistent
 *            use function ocf_mngt_cache_save().
 *
 * @param[in] core Core handle
 * @param[in] thresh threshold in bytes for sequential cutoff
 *
 * @retval 0 Sequential cutoff threshold has been set successfully
 * @retval Non-zero Error occured and threshold hasn't been updated
 */
int ocf_mngt_core_set_seq_cutoff_threshold(ocf_core_t core, uint32_t thresh);

/**
 * @brief Set sequential cutoff threshold for all cores in cache
 *
 * @attention This changes only runtime state. To make changes persistent
 *            use function ocf_mngt_cache_save().
 *
 * @param[in] cache Cache handle
 * @param[in] thresh threshold in bytes for sequential cutoff
 *
 * @retval 0 Sequential cutoff threshold has been set successfully
 * @retval Non-zero Error occured and threshold hasn't been updated
 */
int ocf_mngt_core_set_seq_cutoff_threshold_all(ocf_cache_t cache,
		uint32_t thresh);

/**
 * @brief Get core sequential cutoff threshold
 *
 * @param[in] core Core handle
 * @param[in] thresh threshold in bytes for sequential cutoff
 *
 * @retval 0 Sequential cutoff threshold has been get successfully
 * @retval Non-zero Error occured
 */
int ocf_mngt_core_get_seq_cutoff_threshold(ocf_core_t core, uint32_t *thresh);

/**
 * @brief Set core sequential cutoff policy
 *
 * @attention This changes only runtime state. To make changes persistent
 *            use function ocf_mngt_cache_save().
 *
 * @param[in] core Core handle
 * @param[in] policy sequential cutoff policy
 *
 * @retval 0 Sequential cutoff policy has been set successfully
 * @retval Non-zero Error occured and policy hasn't been updated
 */
int ocf_mngt_core_set_seq_cutoff_policy(ocf_core_t core,
		ocf_seq_cutoff_policy policy);

/**
 * @brief Set sequential cutoff policy for all cores in cache
 *
 * @attention This changes only runtime state. To make changes persistent
 *            use function ocf_mngt_cache_save().
 *
 * @param[in] cache Cache handle
 * @param[in] policy sequential cutoff policy
 *
 * @retval 0 Sequential cutoff policy has been set successfully
 * @retval Non-zero Error occured and policy hasn't been updated
 */
int ocf_mngt_core_set_seq_cutoff_policy_all(ocf_cache_t cache,
		ocf_seq_cutoff_policy policy);

/**
 * @brief Get core sequential cutoff policy
 *
 * @param[in] core Core handle
 * @param[in] policy sequential cutoff policy
 *
 * @retval 0 Sequential cutoff policy has been get successfully
 * @retval Non-zero Error occured
 */
int ocf_mngt_core_get_seq_cutoff_policy(ocf_core_t core,
		ocf_seq_cutoff_policy *policy);

/**
 * @brief Set core sequential cutoff promotion request count
 *
 * @attention This changes only runtime state. To make changes persistent
 *            use function ocf_mngt_cache_save().
 *
 * @param[in] core Core handle
 * @param[in] count promotion request count
 *
 * @retval 0 Sequential cutoff promotion requets count has been set successfully
 * @retval Non-zero Error occured and request count hasn't been updated
 */
int ocf_mngt_core_set_seq_cutoff_promotion_count(ocf_core_t core,
		uint32_t count);

/**
 * @brief Set sequential cutoff promotion request count for all cores in cache
 *
 * @attention This changes only runtime state. To make changes persistent
 *            use function ocf_mngt_cache_save().
 *
 * @param[in] cache Cache handle
 * @param[in] count promotion request count
 *
 * @retval 0 Sequential cutoff promotion request count has been set successfully
 * @retval Non-zero Error occured and request count hasn't been updated
 */
int ocf_mngt_core_set_seq_cutoff_promotion_count_all(ocf_cache_t cache,
		uint32_t count);
/**
 * @brief Get core sequential cutoff promotion threshold
 *
 * @param[in] core Core handle
 * @param[out] count promotion request count
 *
 * @retval 0 Sequential cutoff promotion request count has been get successfully
 * @retval Non-zero Error occured
 */
int ocf_mngt_core_get_seq_cutoff_promotion_count(ocf_core_t core,
		uint32_t *count);

/**
 * @brief Set cache fallback Pass Through error threshold
 *
 * @param[in] cache Cache handle
 * @param[in] threshold Value to be set as threshold
 *
 * @retval 0 Fallback-PT threshold have been set successfully
 * @retval Non-zero Error occurred
 */
int ocf_mngt_cache_set_fallback_pt_error_threshold(ocf_cache_t cache,
		uint32_t threshold);

/**
 * @brief Get cache fallback Pass Through error threshold
 *
 * @param[in] cache Cache handle
 * @param[out] threshold Fallback-PT threshold
 *
 * @retval 0 Fallback-PT threshold have been get successfully
 * @retval Non-zero Error occurred
 */
int ocf_mngt_cache_get_fallback_pt_error_threshold(ocf_cache_t cache,
		uint32_t *threshold);

/**
 * @brief Reset cache fallback Pass Through error counter
 *
 * @param[in] cache Cache handle
 *
 * @retval 0 Threshold have been reset successfully
 */
int ocf_mngt_cache_reset_fallback_pt_error_counter(ocf_cache_t cache);

/**
 * @brief Get core pool count
 *
 * @param[in] ctx OCF context
 *
 * @retval Number of cores in core pool
 */
int ocf_mngt_core_pool_get_count(ocf_ctx_t ctx);

/**
 * @brief Add core to pool
 *
 * @param[in] ctx OCF context
 * @param[in] uuid Cache volume UUID
 * @param[in] type OCF core volume type
 *
 * @retval 0 Core added to pool successfully
 * @retval Non-zero Error occurred and adding core to poll failed
 */
int ocf_mngt_core_pool_add(ocf_ctx_t ctx, ocf_uuid_t uuid, uint8_t type);

/**
 * @brief Add core to pool
 *
 * @param[in] ctx OCF context
 * @param[in] uuid Cache volume UUID
 * @param[in] type OCF core volume type
 *
 * @retval Handler to object with same UUID
 * @retval NULL Not found object with that id
 */
ocf_volume_t ocf_mngt_core_pool_lookup(ocf_ctx_t ctx, ocf_uuid_t uuid,
		ocf_volume_type_t type);
/**
 * @brief Iterate over all object in pool and call visitor callback
 *
 * @param[in] ctx OCF context
 * @param[in] visitor Visitor callback
 * @param[in] visior_ctx CContext for visitor callback
 *
 * @retval Handler to object with same UUID
 * @retval NULL Not found object with that id
 */
int ocf_mngt_core_pool_visit(ocf_ctx_t ctx,
		int (*visitor)(ocf_uuid_t, void *), void *visitor_ctx);

/**
 * @brief Remove volume from pool
 *
 * Important: This function destroys volume instance but doesn't close it,
 * so it should be either moved or closed before calling this function.
 *
 * @param[in] ctx OCF context
 * @param[in] volume Core volume
 */
void ocf_mngt_core_pool_remove(ocf_ctx_t ctx, ocf_volume_t volume);

#endif /* __OCF_CACHE_H__ */
