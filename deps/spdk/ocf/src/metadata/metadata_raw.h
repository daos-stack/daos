/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __METADATA_RAW_H__
#define __METADATA_RAW_H__

#include "metadata_segment_id.h"
#include "../concurrency/ocf_mio_concurrency.h"

/**
 * @file metadata_raw.h
 * @brief Metadata RAW container implementation
 */

/**
 * @brief Metadata raw type
 */
enum ocf_metadata_raw_type {
	/**
	 * @brief Default implementation with support of
	 * flushing to/landing from SSD
	 */
	metadata_raw_type_ram = 0,

	/**
	 * @brief Dynamic implementation, elements are allocated when first
	 * time called
	 */
	metadata_raw_type_dynamic,

	/**
	 * @brief This containers does not flush metadata on SSD and does not
	 * Support loading from SSD
	 */
	metadata_raw_type_volatile,

	/**
	 * @brief Implementation for atomic device used as cache
	 */
	metadata_raw_type_atomic,

	metadata_raw_type_max, /*!<  MAX */
	metadata_raw_type_min = metadata_raw_type_ram /*!<  MAX */
};

struct ocf_metadata_raw;

/**
 * @brief Container page lock/unlock callback
 */
typedef void (*ocf_flush_page_synch_t)(ocf_cache_t cache,
		struct ocf_metadata_raw *raw, uint32_t page);

/**
 * @brief RAW instance descriptor
 */
struct ocf_metadata_raw {
	/**
	 * @name Metadata and RAW types
	 */
	enum ocf_metadata_segment_id metadata_segment; /*!< Metadata segment */
	enum ocf_metadata_raw_type raw_type; /*!< RAW implementation type */

	/**
	 * @name Metdata elements description
	 */
	uint32_t entry_size; /*!< Size of particular entry */
	uint32_t entries_in_page; /*!< Numbers of entries in one page*/
	uint64_t entries; /*!< Numbers of entries */

	/**
	 * @name Location on cache device description
	 */
	uint64_t ssd_pages_offset; /*!< SSD (Cache device) Page offset */
	uint64_t ssd_pages; /*!< Numbers of pages that are required */

	const struct raw_iface *iface; /*!< RAW container interface*/

	/**
	 * @name Private RAW elements
	 */
	void *mem_pool; /*!< Private memory pool*/

	size_t mem_pool_limit; /*! Current memory pool size (limit) */

	void *priv; /*!< Private data - context */

	ocf_flush_page_synch_t lock_page; /*!< Page lock callback */
	ocf_flush_page_synch_t unlock_page; /*!< Page unlock callback */

	struct ocf_alock *mio_conc;
};

/**
 * RAW container interface
 */
struct raw_iface {
	int (*init)(ocf_cache_t cache,
			ocf_flush_page_synch_t lock_page_pfn,
			ocf_flush_page_synch_t unlock_page_pfn,
			struct ocf_metadata_raw *raw);

	int (*deinit)(ocf_cache_t cache,
			struct ocf_metadata_raw *raw);

	size_t (*size_of)(ocf_cache_t cache,
			struct ocf_metadata_raw *raw);

	/**
	 * @brief Return size which metadata take on cache device
	 *
	 * @param raw RAW container of metadata
	 *
	 * @return Number of pages (4 kiB) on cache device
	 */
	uint32_t (*size_on_ssd)(struct ocf_metadata_raw *raw);

	uint32_t (*checksum)(ocf_cache_t cache,
			struct ocf_metadata_raw *raw);

	uint32_t (*page)(struct ocf_metadata_raw *raw, uint32_t entry);

	void* (*access)(ocf_cache_t cache, struct ocf_metadata_raw *raw,
			uint32_t entry);

	void (*load_all)(ocf_cache_t cache, struct ocf_metadata_raw *raw,
			ocf_metadata_end_t cmpl, void *priv);

	void (*flush_all)(ocf_cache_t cache, struct ocf_metadata_raw *raw,
			ocf_metadata_end_t cmpl, void *priv);

	void (*flush_mark)(ocf_cache_t cache, struct ocf_request *req,
			uint32_t map_idx, int to_state, uint8_t start,
			uint8_t stop);

	int (*flush_do_asynch)(ocf_cache_t cache, struct ocf_request *req,
			struct ocf_metadata_raw *raw, ocf_req_end_t complete);
};

/**
 * @brief Initialize RAW instance
 *
 * @param cache - Cache instance
 * @param lock_page_pfn - Optional page lock callback
 * @param lock_page_pfn - Optional page unlock callback
 * @param raw - RAW descriptor
 * @return 0 - Operation success, otherwise error
 */
int ocf_metadata_raw_init(ocf_cache_t cache,
		ocf_flush_page_synch_t lock_page_pfn,
		ocf_flush_page_synch_t unlock_page_pfn,
		struct ocf_metadata_raw *raw);

/**
 * @brief De-Initialize RAW instance
 *
 * @param cache - Cache instance
 * @param raw - RAW descriptor
 * @return 0 - Operation success, otherwise error
 */
int ocf_metadata_raw_deinit(ocf_cache_t cache,
		struct ocf_metadata_raw *raw);

/**
 * @brief Get memory footprint
 *
 * @param cache Cache instance
 * @param raw RAW descriptor
 * @return Memory footprint
 */
static inline size_t ocf_metadata_raw_size_of(ocf_cache_t cache,
		struct ocf_metadata_raw *raw)
{
	if (!raw->iface)
		return 0;

	return raw->iface->size_of(cache, raw);
}

/**
 * @brief Get SSD footprint
 *
 * @param raw - RAW descriptor
 * @return Size on SSD
 */
size_t ocf_metadata_raw_size_on_ssd(struct ocf_metadata_raw* raw);

/**
 * @brief Calculate metadata checksum
 *
 * @param cache - Cache instance
 * @param raw - RAW descriptor
 * @return Checksum
 */
static inline uint32_t ocf_metadata_raw_checksum(struct ocf_cache* cache,
		struct ocf_metadata_raw* raw)
{
	return raw->iface->checksum(cache, raw);
}

/**
 * @brief Calculate entry page index
 *
 * @param raw - RAW descriptor
 * @param entry - Entry number
 * @return Page index
 */
static inline uint32_t ocf_metadata_raw_page(struct ocf_metadata_raw* raw,
		uint32_t entry)
{
	return raw->iface->page(raw, entry);
}

/**
 * @brief Access specified element of metadata directly
 *
 * @param cache - Cache instance
 * @param raw - RAW descriptor
 * @param entry - Entry to be get
 * @param data - Data where metadata entry will be copied into
 * @return 0 - Point to accessed data, in case of error NULL
 */
static inline void *ocf_metadata_raw_wr_access(ocf_cache_t cache,
		struct ocf_metadata_raw *raw, uint32_t entry)
{
	return raw->iface->access(cache, raw, entry);
}

/**
 * @brief Access specified element of metadata directly
 *
 * @param cache - Cache instance
 * @param raw - RAW descriptor
 * @param entry - Entry to be get
 * @param data - Data where metadata entry will be copied into
 * @return 0 - Point to accessed data, in case of error NULL
 */
static inline const void *ocf_metadata_raw_rd_access( ocf_cache_t cache,
		struct ocf_metadata_raw *raw, uint32_t entry)
{
	return raw->iface->access(cache, raw, entry);
}

/**
 * @brief Load all entries from SSD cache (cahce cache)
 *
 * @param cache - Cache instance
 * @param raw - RAW descriptor
 * @param cmpl - Completion callback
 * @param priv - Completion callback context
 */
static inline void ocf_metadata_raw_load_all(ocf_cache_t cache,
		struct ocf_metadata_raw *raw,
		ocf_metadata_end_t cmpl, void *priv)
{
	raw->iface->load_all(cache, raw, cmpl, priv);
}

/**
 * @brief Flush all entries for into SSD cache (cahce cache)
 *
 * @param cache - Cache instance
 * @param raw - RAW descriptor
 * @param cmpl - Completion callback
 * @param priv - Completion callback context
 */
static inline void ocf_metadata_raw_flush_all(ocf_cache_t cache,
		struct ocf_metadata_raw *raw,
		ocf_metadata_end_t cmpl, void *priv)
{
	raw->iface->flush_all(cache, raw, cmpl, priv);
}


static inline void ocf_metadata_raw_flush_mark(ocf_cache_t cache,
		struct ocf_metadata_raw *raw, struct ocf_request *req,
		uint32_t map_idx, int to_state, uint8_t start, uint8_t stop)
{
	raw->iface->flush_mark(cache, req, map_idx, to_state, start, stop);
}

static inline int ocf_metadata_raw_flush_do_asynch(ocf_cache_t cache,
		struct ocf_request *req, struct ocf_metadata_raw *raw,
		ocf_req_end_t complete)
{
	return raw->iface->flush_do_asynch(cache, req, raw, complete);
}

/*
 * Check if line is valid for specified RAW descriptor
 */
static inline bool _raw_is_valid(struct ocf_metadata_raw *raw, uint32_t entry)
{
	if (unlikely(!raw))
		return false;

	if (unlikely(entry >= raw->entries))
		return false;

	return true;
}

static inline void _raw_bug_on(struct ocf_metadata_raw *raw, uint32_t entry)
{
	ENV_BUG_ON(!_raw_is_valid(raw, entry));
}

#define MAX_STACK_TAB_SIZE 32

int _raw_ram_flush_do_page_cmp(const void *item1, const void *item2);

static inline void *ocf_metadata_raw_get_mem(struct ocf_metadata_raw *raw)
{
	return raw->mem_pool;
}

#endif /* METADATA_RAW_H_ */
