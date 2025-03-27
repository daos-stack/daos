/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __OCF_STATS_PRIV_H__
#define __OCF_STATS_PRIV_H__

struct ocf_counters_block {
	env_atomic64 read_bytes;
	env_atomic64 write_bytes;
};

struct ocf_counters_error {
	env_atomic read;
	env_atomic write;
};

struct ocf_counters_req {
	env_atomic64 partial_miss;
	env_atomic64 full_miss;
	env_atomic64 total;
	env_atomic64 pass_through;
};

/**
 * @brief OCF requests statistics like hit, miss, etc...
 *
 * @note To calculate number of hits request do:
 * total - (partial_miss + full_miss)
 */
struct ocf_stats_req {
	/** Number of partial misses */
	uint64_t partial_miss;

	/** Number of full misses */
	uint64_t full_miss;

	/** Total of requests */
	uint64_t total;

	/** Pass-through requests */
	uint64_t pass_through;
};

/**
 * @brief OCF error statistics
 */
struct ocf_stats_error {
	/** Read errors */
	uint32_t read;

	/** Write errors */
	uint32_t write;
};

/**
 * @brief OCF block statistics in bytes
 */
struct ocf_stats_block {
	/** Number of blocks read */
	uint64_t read;

	/** Number of blocks written */
	uint64_t write;
};

/**
 * Statistics appropriate for given IO class
 */
struct ocf_stats_io_class {
	/** Number of cache lines available for given partition */
	uint64_t free_clines;

	/** Number of cache lines within lru list */
	uint64_t occupancy_clines;

	/** Number of dirty cache lines assigned to specific partition */
	uint64_t dirty_clines;

	/** Read requests statistics */
	struct ocf_stats_req read_reqs;

	/** Writes requests statistics */
	struct ocf_stats_req write_reqs;

	/** Block requests for ocf volume statistics */
	struct ocf_stats_block blocks;

	/** Block requests for cache volume statistics */
	struct ocf_stats_block cache_blocks;

	/** Block requests for core volume statistics */
	struct ocf_stats_block core_blocks;
};

#define IO_PACKET_NO 12
#define IO_ALIGN_NO 4

/**
 * @brief Core debug statistics
 */
struct ocf_stats_core_debug {
	/** I/O sizes being read (grouped by packets) */
	uint64_t read_size[IO_PACKET_NO];

	/** I/O sizes being written (grouped by packets) */
	uint64_t write_size[IO_PACKET_NO];

	/** I/O alignment for reads */
	uint64_t read_align[IO_ALIGN_NO];

	/** I/O alignment for writes */
	uint64_t write_align[IO_ALIGN_NO];
};

/**
 * @brief OCF core statistics
 */
struct ocf_stats_core {
	/** Number of cache lines allocated in the cache for this core */
	uint32_t cache_occupancy;

	/** Number of dirty cache lines allocated in the cache for this core */
	uint32_t dirty;

	/** Read requests statistics */
	struct ocf_stats_req read_reqs;

	/** Write requests statistics */
	struct ocf_stats_req write_reqs;

	/** Block requests for cache volume statistics */
	struct ocf_stats_block cache_volume;

	/** Block requests for core volume statistics */
	struct ocf_stats_block core_volume;

	/** Block requests submitted by user to this core */
	struct ocf_stats_block core;

	/** Cache volume error statistics */
	struct ocf_stats_error cache_errors;

	/** Core volume error statistics */
	struct ocf_stats_error core_errors;

	/** Debug statistics */
	struct ocf_stats_core_debug debug_stat;
};

/**
 * statistics appropriate for given io class.
 */
struct ocf_counters_part {
	struct ocf_counters_req read_reqs;
	struct ocf_counters_req write_reqs;

	struct ocf_counters_block blocks;

	struct ocf_counters_block core_blocks;
	struct ocf_counters_block cache_blocks;
};

#ifdef OCF_DEBUG_STATS
struct ocf_counters_debug {
	env_atomic64 write_size[IO_PACKET_NO];
	env_atomic64 read_size[IO_PACKET_NO];

	env_atomic64 read_align[IO_ALIGN_NO];
	env_atomic64 write_align[IO_ALIGN_NO];
};
#endif

struct ocf_counters_core {
	struct ocf_counters_error core_errors;
	struct ocf_counters_error cache_errors;

	struct ocf_counters_part part_counters[OCF_USER_IO_CLASS_MAX];
#ifdef OCF_DEBUG_STATS
	struct ocf_counters_debug debug_stats;
#endif
};

void ocf_core_stats_core_block_update(ocf_core_t core, ocf_part_id_t part_id,
		int dir, uint64_t bytes);
void ocf_core_stats_cache_block_update(ocf_core_t core, ocf_part_id_t part_id,
		int dir, uint64_t bytes);
void ocf_core_stats_vol_block_update(ocf_core_t core, ocf_part_id_t part_id,
		int dir, uint64_t bytes);

void ocf_core_stats_request_update(ocf_core_t core, ocf_part_id_t part_id,
		uint8_t dir, uint64_t hit_no, uint64_t core_line_count);
void ocf_core_stats_request_pt_update(ocf_core_t core, ocf_part_id_t part_id,
		uint8_t dir, uint64_t hit_no, uint64_t core_line_count);

void ocf_core_stats_core_error_update(ocf_core_t core, uint8_t dir);
void ocf_core_stats_cache_error_update(ocf_core_t core, uint8_t dir);

/**
 * @brief ocf_core_io_class_get_stats retrieve io class statistics
 *			for given core
 *
 * Retrieve buffer of cache statistics for given cache instance.
 *
 * @param[in] core core handle to which request pertains
 * @param[in] part_id IO class, stats of which are requested
 * @param[out] stats statistic structure that shall be filled as
 *             a result of this function invocation.
 *
 * @result zero upon successful completion; error code otherwise
 */
int ocf_core_io_class_get_stats(ocf_core_t core, ocf_part_id_t part_id,
		struct ocf_stats_io_class *stats);

/**
 * @brief retrieve core stats
 *
 * Retrieve ocf per core stats (for all IO classes together)
 *
 * @param[in] core core ID to which request pertains
 * @param[out] stats statistics structure that shall be filled as
 *             a result of this function invocation.
 *
 * @result zero upon successful completion; error code otherwise
 */
int ocf_core_get_stats(ocf_core_t core, struct ocf_stats_core *stats);

/**
 * @brief update DEBUG stats given IO request
 *
 * Function meant to update DEBUG stats for IO request.
 *
 * @note This function shall be invoked for each IO request processed
 *
 * @param[in] core to which request pertains
 * @param[in] io request for which stats are being updated
 */
void ocf_core_update_stats(ocf_core_t core, struct ocf_io *io);

#endif
