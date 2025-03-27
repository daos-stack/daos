/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __OCF_TRACE_H__
#define __OCF_TRACE_H__

#include "ocf_def.h"
#include "ocf_types.h"

typedef uint64_t log_sid_t;

#define OCF_EVENT_VERSION	1
#define OCF_TRACING_STOP	1

/**
 * @brief OCF trace (event) type
 */
typedef enum {
	/** IO trace description, this event is pushed first to indicate version
	 * of traces, number of cores and provides details about cache */
	ocf_event_type_cache_desc,

	/** Event describing ocf core */
	ocf_event_type_core_desc,

	/** IO */
	ocf_event_type_io,

	/** IO completion */
	ocf_event_type_io_cmpl,

	/** IO in file domain */
	ocf_event_type_io_file,
} ocf_event_type;

/**
 * @brief Generic OCF trace event
 */
struct ocf_event_hdr {
	/** Event sequence ID */
	log_sid_t sid;

	/** Time stamp */
	uint64_t timestamp;

	/** Trace event type */
	ocf_event_type type;

	/** Size of this event */
	uint32_t size;
};

/**
 *  @brief Cache trace description
*/
struct ocf_event_cache_desc {
	/** Event header */
	struct ocf_event_hdr hdr;

	/** Cache name */
	const char *name;

	/** Cache line size */
	ocf_cache_line_size_t cache_line_size;

	/** Cache mode */
	ocf_cache_mode_t cache_mode;

	/** Cache size in bytes*/
	uint64_t cache_size;

	/** Number of cores */
	uint32_t cores_no;

	/** Trace version */
	uint32_t version;
};

/**
 *  @brief Core trace description
*/
struct ocf_event_core_desc {
	/** Event header */
	struct ocf_event_hdr hdr;

	/** Core name */
	const char *name;

	/** Core size in bytes */
	uint64_t core_size;
};

/** @brief IO operation */
typedef enum {
	/** Read */
	ocf_event_operation_rd = 'R',

	/** Write */
	ocf_event_operation_wr = 'W',

	/** Flush */
	ocf_event_operation_flush = 'F',

	/** Discard */
	ocf_event_operation_discard = 'D',
} ocf_event_operation_t;

/**
 * @brief IO trace event
 */
struct ocf_event_io {
	/** Trace event header */
	struct ocf_event_hdr hdr;

	/** Address of IO in bytes */
	uint64_t addr;

	/** Size of IO in bytes */
	uint32_t len;

	/** IO class of IO */
	uint32_t io_class;

	/** Core name */
	const char *core_name;

	/** Operation type: read, write, trim or flush **/
	ocf_event_operation_t operation;
};

/**
 * @brief IO completion event
 */
struct ocf_event_io_cmpl {
	/** Trace event header */
	struct ocf_event_hdr hdr;

	/** Reference event sequence ID */
	log_sid_t rsid;

	/** Was IO a cache hit or miss */
	bool is_hit;
};


/** @brief Push log callback.
 *
 * @param[in] cache OCF cache
 * @param[in] trace_ctx Tracing context
 * @param[in] queue Queue handle
 * @param[out] trace Event log
 * @param[out] size Size of event log
 *
 * @return 0 If pushing trace succeeded
 * @return Non-zero error
 */
typedef void (*ocf_trace_callback_t)(ocf_cache_t cache, void *trace_ctx,
		ocf_queue_t queue, const void* trace, const uint32_t size);

/**
 * @brief Start tracing
 *
 * @param[in] cache OCF cache
 * @param[in] trace_ctx Tracing context
 * @param[in] trace_callback Callback used for pushing logs
 *
 * @retval 0 Tracing started successfully
 * @retval Non-zero Error
 */
int ocf_mngt_start_trace(ocf_cache_t cache, void *trace_ctx,
		ocf_trace_callback_t trace_callback);

/**
 * @brief Stop tracing
 *
 * @param[in] cache OCF cache
 *
 * @retval 0 Tracing stopped successfully
 * @retval Non-zero Error
 */
int ocf_mngt_stop_trace(ocf_cache_t cache);

#endif /* __OCF_TRACE_H__ */
