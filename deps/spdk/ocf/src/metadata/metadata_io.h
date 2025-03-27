/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __METADATA_IO_H__
#define __METADATA_IO_H__

#include "../concurrency/ocf_mio_concurrency.h"

/**
 * @file metadata_io.h
 * @brief Metadata IO utilities
 */

/**
 * @brief Metadata IO event
 *
 * The client of metadata IO service if informed trough this event:
 * - on completion of read from cache device
 * - on fill data which will be written into cache device
 *
 * @param data[in,out] Environment data for read ot write IO
 * @param page[in] Page which is issued
 * @param context[in] context caller
 *
 * @retval 0 Success
 * @retval Non-zero Error which will bee finally returned to the caller
 */
typedef int (*ocf_metadata_io_event_t)(ocf_cache_t cache,
		ctx_data_t *data, uint32_t page, void *context);

/**
 * @brief Metadata write end callback
 *
 * @param cache - Cache instance
 * @param context - Read context
 * @param error - error
 * @param page - page that was written
 */
typedef void (*ocf_metadata_io_end_t)(ocf_cache_t cache,
		void *context, int error);

struct metadata_io_request_asynch;

/*
 * IO request context
 */
struct metadata_io_request {
	struct ocf_request req;
	struct list_head list;
	ocf_cache_t cache;
	void *context;
	ctx_data_t *data;
	struct metadata_io_request_asynch *asynch;
	uint32_t page;
	uint32_t count;
	uint64_t alock_status;
};

/*
 * Asynchronous IO request context
 */
struct metadata_io_request_asynch {
	void *context;
	env_atomic req_remaining;
	env_atomic req_active;
	env_atomic req_current;
	ocf_metadata_io_event_t on_meta_fill;
	ocf_metadata_io_event_t on_meta_drain;
	ocf_metadata_io_end_t on_complete;
	struct ocf_alock *mio_conc;
	uint32_t page;
	uint32_t count;
	uint32_t alloc_req_count; /*< Number of allocated metadata_io_requests */
	int flags;
	int error;
	struct metadata_io_request reqs[];
};

void metadata_io_req_complete(struct metadata_io_request *m_req);

/**
 * @brief Metadata read end callback
 *
 * @param cache Cache instance
 * @param sector_addr Begin sector of metadata
 * @param sector_no Number of sectors
 * @param data Data environment buffer with atomic metadata
 *
 * @retval 0 Success
 * @retval Non-zero Error which will bee finally returned to the caller
 */
typedef int (*ocf_metadata_atomic_io_event_t)(void *priv, uint64_t sector_addr,
		uint32_t sector_no, ctx_data_t *data);

/**
 * @brief Iterative asynchronous read atomic metadata
 *
 * @param cache - Cache instance
 * @param queue - Queue to be used for IO
 * @param context - Read context
 * @param drain_hndl - Drain callback
 * @param compl_hndl - All IOs completed callback
 *
 * @return 0 - No errors, otherwise error occurred
 */
int metadata_io_read_i_atomic(ocf_cache_t cache, ocf_queue_t queue,
		void *context, ocf_metadata_atomic_io_event_t drain_hndl,
		ocf_metadata_io_end_t compl_hndl);

/**
 * @brief Iterative asynchronous pages write
 *
 * @param cache - Cache instance
 * @param queue - Queue to be used for IO
 * @param context - Read context
 * @param page - Start page of SSD (cache device) where data will be written
 * @param count - Counts of page to be processed
 * @param fill_hndl - Fill callback
 * @param compl_hndl - All IOs completed callback
 *
 * @return 0 - No errors, otherwise error occurred
 */
int metadata_io_write_i_asynch(ocf_cache_t cache, ocf_queue_t queue,
		void *context, uint32_t page, uint32_t count, int flags,
		ocf_metadata_io_event_t fill_hndl,
		ocf_metadata_io_end_t compl_hndl,
		struct ocf_alock *mio_conc);

/**
 * @brief Iterative asynchronous pages read
 *
 * @param cache - Cache instance
 * @param queue - Queue to be used for IO
 * @param context - Read context
 * @param page - Start page of SSD (cache device) where data will be read
 * @param count - Counts of page to be processed
 * @param drain_hndl - Drain callback
 * @param compl_hndl - All IOs completed callback
 *
 * @return 0 - No errors, otherwise error occurred
 */
int metadata_io_read_i_asynch(ocf_cache_t cache, ocf_queue_t queue,
		void *context, uint32_t page, uint32_t count, int flags,
		ocf_metadata_io_event_t drain_hndl,
		ocf_metadata_io_end_t compl_hndl);

/**
 * Initialize ocf_ctx related structures of metadata_io (mpool).
 */
int ocf_metadata_io_ctx_init(struct ocf_ctx *ocf_ctx);

/**
 * Deinitialize ocf_ctx related structures of metadata_io
 */
void ocf_metadata_io_ctx_deinit(struct ocf_ctx *ocf_ctx);

#endif /* METADATA_IO_UTILS_H_ */
