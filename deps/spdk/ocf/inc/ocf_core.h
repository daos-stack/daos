/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * @file
 * @brief OCF core API
 */

#ifndef __OCF_CORE_H__
#define __OCF_CORE_H__

#include "ocf_volume.h"
#include "ocf_io.h"
#include "ocf_mngt.h"

struct ocf_core_info {
	/** Core size in cache line size unit */
	uint64_t core_size;

	/** Core size in bytes unit */
	uint64_t core_size_bytes;

	/** Fields refers ongoing flush operation */
	struct {
		/** Number of blocks flushed in ongoing flush operation */
		uint32_t flushed;

		/** Number of blocks left to flush in ongoing flush operation */
		uint32_t dirty;
	};

	/** How long core is dirty in seconds unit */
	uint64_t dirty_for;

	/** Sequential cutoff threshold (in bytes) */
	uint32_t seq_cutoff_threshold;

	/** Sequential cutoff policy */
	ocf_seq_cutoff_policy seq_cutoff_policy;
};

/**
 * @brief Get OCF core by name
 *
 * @param[in] cache OCF cache
 * @param[in] name Core name
 * @param[in] name_len Core name length
 * @param[out] core OCF core handle
 *
 * @retval 0 Get cache successfully
 * @retval -OCF_ERR_CORE_NOT_EXIST Core with given name doesn't exist
 */
int ocf_core_get_by_name(ocf_cache_t cache, const char *name, size_t name_len,
		ocf_core_t *core);

/**
 * @brief Obtain cache object from core
 *
 * @param[in] core Core object
 *
 * @retval Cache object
 */
ocf_cache_t ocf_core_get_cache(ocf_core_t core);

/**
 * @brief Obtain volume associated with core
 *
 * @param[in] core Core object
 *
 * @retval Volume
 */
ocf_volume_t ocf_core_get_volume(ocf_core_t core);

/**
 * @brief Obtain volume of the core
 *
 * @param[in] core Core object
 *
 * @retval Front volume
 */
ocf_volume_t ocf_core_get_front_volume(ocf_core_t core);

/**
 * @brief Get UUID of volume associated with core
 *
 * @param[in] core Core object
 *
 * @retval Volume UUID
 */
static inline const struct ocf_volume_uuid *ocf_core_get_uuid(ocf_core_t core)
{
	return ocf_volume_get_uuid(ocf_core_get_volume(core));
}

/**
 * @brief Get sequential cutoff threshold of given core object
 *
 * @param[in] core Core object
 *
 * @retval Sequential cutoff threshold [B]
 */
uint32_t ocf_core_get_seq_cutoff_threshold(ocf_core_t core);

/**
 * @brief Get sequential cutoff policy of given core object
 *
 * @param[in] core Core object
 *
 * @retval Sequential cutoff policy
 */
ocf_seq_cutoff_policy ocf_core_get_seq_cutoff_policy(ocf_core_t core);

/**
 * @brief Get sequential cutoff stream promotion req count of given core object
 *
 * @param[in] core Core object
 *
 * @retval Sequential cutoff stream promotion request count
 */
uint32_t ocf_core_get_seq_cutoff_promotion_count(ocf_core_t core);

/**
 * @brief Get name of given core object
 *
 * @param[in] core Core object
 *
 * @retval Core name
 */
const char *ocf_core_get_name(ocf_core_t core);

/**
 * @brief Get core state
 *
 * @param[in] core Core object
 *
 * @retval Core state
 */
ocf_core_state_t ocf_core_get_state(ocf_core_t core);

/**
 * @brief Allocate new ocf_io
 *
 * @param[in] core Core object
 * @param[in] queue IO queue handle
 * @param[in] addr OCF IO destination address
 * @param[in] bytes OCF IO size in bytes
 * @param[in] dir OCF IO direction
 * @param[in] io_class OCF IO destination class
 * @param[in] flags OCF IO flags
 *
 * @retval ocf_io object
 */
static inline struct ocf_io *ocf_core_new_io(ocf_core_t core, ocf_queue_t queue,
		uint64_t addr, uint32_t bytes, uint32_t dir,
		uint32_t io_class, uint64_t flags)
{
	ocf_volume_t volume = ocf_core_get_front_volume(core);

	return ocf_volume_new_io(volume, queue, addr, bytes, dir,
			io_class, flags);
}

/**
 * @brief Submit ocf_io
 *
 * @param[in] io IO to be submitted
 */
static inline void ocf_core_submit_io(struct ocf_io *io)
{
	ocf_volume_submit_io(io);
}

/**
 * @brief Submit ocf_io with flush command
 *
 * @param[in] io IO to be submitted
 */
static inline void ocf_core_submit_flush(struct ocf_io *io)
{
	ocf_volume_submit_flush(io);
}

/**
 * @brief Submit ocf_io with discard command
 *
 * @param[in] io IO to be submitted
 */
static inline void ocf_core_submit_discard(struct ocf_io *io)
{
	ocf_volume_submit_discard(io);
}

/**
 * @brief Core visitor function type which is called back when iterating over
 * cores.
 *
 * @param[in] core Core which is currently iterated (visited)
 * @param[in] cntx Visitor context
 *
 * @retval 0 continue visiting cores
 * @retval Non-zero stop iterating and return result
 */
typedef int (*ocf_core_visitor_t)(ocf_core_t core, void *cntx);

/**
 * @brief Run visitor function for each core of given cache
 *
 * @param[in] cache OCF cache instance
 * @param[in] visitor Visitor function
 * @param[in] cntx Visitor context
 * @param[in] only_opened Visit only opened cores
 *
 * @retval 0 Success
 * @retval Non-zero Fail
 */
int ocf_core_visit(ocf_cache_t cache, ocf_core_visitor_t visitor, void *cntx,
		bool only_opened);

/**
 * @brief Get info of given core object
 *
 * @param[in] core Core object
 * @param[out] info Core info structure
 *
 * @retval 0 Success
 * @retval Non-zero Fail
 */
int ocf_core_get_info(ocf_core_t core, struct ocf_core_info *info);

/**
 * @brief Set core private data
 *
 * @param[in] core Core object
 * @param[in] priv Private data
 */
void ocf_core_set_priv(ocf_core_t core, void *priv);

/**
 * @brief Get core private data
 *
 * @param[in] core Core object
 *
 * @retval Private data
 */
void *ocf_core_get_priv(ocf_core_t core);

#endif /* __OCF_CORE_H__ */
