/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __OCF_METADATA_H__
#define __OCF_METADATA_H__

/**
 * @file
 * @brief OCF metadata helper function
 *
 * Those functions can be used by volume implementation.
 */

/**
 * @brief Atomic metadata for extended sector
 *
 * @warning The size of this structure has to be equal 8 bytes
 */
struct ocf_atomic_metadata {
	/** Core line of core (in cache line size unit) which are cached */
	uint64_t core_line : 46;

	/** Core sequence number to which this line belongs to*/
	uint32_t core_seq_no : 16;

	/** Set bit indicates that given sector is valid (is cached) */
	uint32_t valid : 1;

	/** Set bit indicates that sector is dirty */
	uint32_t dirty : 1;
} __attribute__((packed));

#define OCF_ATOMIC_METADATA_SIZE	sizeof(struct ocf_atomic_metadata)

/**
 * @brief Get metadata entry (cache mapping) for specified sector of cache
 * device
 *
 * Metadata has sector granularity. It might be used by volume which
 * supports atomic writes - (write of data and metadata in one buffer)
 *
 * @param[in] cache OCF cache instance
 * @param[in] addr Sector address in bytes
 * @param[out] entry Metadata entry
 *
 * @retval 0 Metadata retrieved successfully
 * @retval Non-zero Error
 */
int ocf_metadata_get_atomic_entry(ocf_cache_t cache, uint64_t addr,
		struct ocf_atomic_metadata *entry);

/**
 * @brief Metadata probe status
 */
struct ocf_metadata_probe_status {
	/** Cache was graceful stopped */
	bool clean_shutdown;

	/** Cache contains dirty data */
	bool cache_dirty;

	/** Loaded name of cache instance */
	char cache_name[OCF_CACHE_NAME_SIZE];
};

/**
 * @brief Metadata probe completion callback
 *
 * @param[in] priv Completion context
 * @param[in] error Error code (zero on success)
 * @param[in] status Structure describing metadata probe status
 */
typedef void (*ocf_metadata_probe_end_t)(void *priv, int error,
		struct ocf_metadata_probe_status *status);

/**
 * @brief Probe cache device
 *
 * @param[in] ctx handle to object designating ocf context
 * @param[in] volume Cache volume
 * @param[in] cmpl Completion callback
 * @param[in] priv Completion context
 */
void ocf_metadata_probe(ocf_ctx_t ctx, ocf_volume_t volume,
		ocf_metadata_probe_end_t cmpl, void *priv);

/**
 * @brief Metadata probe for cores completion callback
 *
 * @param[in] priv Completion context
 * @param[in] error Error code (zero on success)
 * @param[in] num_cores Number of cores in cache metadata
 */
typedef void (*ocf_metadata_probe_cores_end_t)(void *priv, int error,
		unsigned int num_cores);

/**
 * @brief Probe cache device for associated cores
 *
 * @param[in] ctx handle to object designating ocf context
 * @param[in] volume Cache volume
 * @param[in,out] uuids Array of uuids
 * @param[in] uuid_count Size of @uuid array
 * @param[in] cmpl Completion callback
 * @param[in] priv Completion context
 */
void ocf_metadata_probe_cores(ocf_ctx_t ctx, ocf_volume_t volume,
		struct ocf_volume_uuid *uuids, uint32_t uuid_count,
		ocf_metadata_probe_cores_end_t cmpl, void *priv);

/**
 * @brief Check if sectors in cache line before given address are invalid
 *
 * It might be used by volume which supports
 * atomic writes - (write of data and metadata in one buffer)
 *
 * @param[in] cache OCF cache instance
 * @param[in] addr Sector address in bytes
 *
 * @retval 0 Not all sectors before given address are invalid
 * @retval Non-zero Number of sectors before given address
 */
int ocf_metadata_check_invalid_before(ocf_cache_t cache, uint64_t addr);

/**
 * @brief Check if sectors in cache line after given end address are invalid
 *
 * It might be used by volume which supports
 * atomic writes - (write of data and metadata in one buffer)
 *
 * @param[in] cache OCF cache instance
 * @param[in] addr Sector address in bytes
 * @param[in] bytes IO size in bytes
 *
 * @retval 0 Not all sectors after given end address are invalid
 * @retval Non-zero Number of sectors after given end address
 */
int ocf_metadata_check_invalid_after(ocf_cache_t cache, uint64_t addr,
		uint32_t bytes);

#endif /* __OCF_METADATA_H__ */
