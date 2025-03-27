/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */


#ifndef __OCF_DEF_H__
#define __OCF_DEF_H__

#include "ocf_cfg.h"
/**
 * @file
 * @brief OCF definitions
 */

/**
 * @name OCF cache definitions
 */
/**
 * Minimum value of a valid cache ID
 */
#define OCF_CACHE_ID_MIN 1
/**
 * Maximum value of a valid cache ID
 */
#define OCF_CACHE_ID_MAX 16384
/**
 * Invalid value of cache id
 */
#define OCF_CACHE_ID_INVALID 0
/**
 * Minimum cache size in bytes
 */
#define OCF_CACHE_SIZE_MIN	(20 * MiB)
/**
 * Size of cache name
 */
#define OCF_CACHE_NAME_SIZE 32
/**
 * Value to turn off fallback pass through
 */
#define OCF_CACHE_FALLBACK_PT_INACTIVE	0
/**
 * Minimum value of io error threshold
 */
#define OCF_CACHE_FALLBACK_PT_MIN_ERROR_THRESHOLD	\
	OCF_CACHE_FALLBACK_PT_INACTIVE
/**
 * Maximum value of io error threshold
 */
#define OCF_CACHE_FALLBACK_PT_MAX_ERROR_THRESHOLD	1000000
/**
 * @}
 */

/**
 * @name OCF cores definitions
 */
/**
 * Maximum numbers of cores per cache instance
 */
#define OCF_CORE_MAX OCF_CONFIG_MAX_CORES
/**
 * Minimum value of a valid core ID
 */
#define OCF_CORE_ID_MIN 0
/**
 * Maximum value of a valid core ID
 */
#define OCF_CORE_ID_MAX (OCF_CORE_MAX - 1)
/**
 * Invalid value of core id
 */
#define OCF_CORE_ID_INVALID OCF_CORE_MAX
/**
 * Size of core name
 */
#define OCF_CORE_NAME_SIZE 32
/**
 * Minimum value of valid core sequence number
 */
#define OCF_SEQ_NO_MIN 1
/**
 * Maximum value of a valid core sequence number
 */
#define OCF_SEQ_NO_MAX (65535UL)
/*
 * Invalid value of core sequence number
 */
#define OCF_SEQ_NO_INVALID 0
/**
 * @}
 */

/**
 * @name Miscellaneous defines
 * @{
 */
#define KiB (1ULL << 10)
#define MiB (1ULL << 20)
#define GiB (1ULL << 30)

#if OCF_CONFIG_DEBUG_STATS == 1
/** Macro which indicates that extended debug statistics shall be on*/
#define OCF_DEBUG_STATS
#endif
/**
 * @}
 */

/**
 * This Enumerator describes OCF cache instance state
 */
typedef enum {
	ocf_cache_state_running = 0,     //!< ocf_cache_state_running
		/*!< OCF is currently running */

	ocf_cache_state_stopping = 1,    //!< ocf_cache_state_stopping
		/*!< OCF cache instance is stopping */

	ocf_cache_state_initializing = 2, //!< ocf_cache_state_initializing
		/*!< OCF cache instance during initialization */

	ocf_cache_state_incomplete = 3, //!< ocf_cache_state_incomplete
		/*!< OCF cache has at least one inactive core */

	ocf_cache_state_max              //!< ocf_cache_state_max
		/*!< Stopper of cache state enumerator */
} ocf_cache_state_t;

/**
 * This Enumerator describes OCF core instance state
 */
typedef enum {
	ocf_core_state_active = 0,
		/*!< Core is active */

	ocf_core_state_inactive,
		/*!< Core is inactive (not attached) */

	ocf_core_state_max,
		/*!< Stopper of core state enumerator */
} ocf_core_state_t;


/**
 * OCF supported cache modes
 */
typedef enum {
	ocf_cache_mode_wt = 0,
		/*!< Write-through cache mode */

	ocf_cache_mode_wb,
		/*!< Write-back cache mode */

	ocf_cache_mode_wa,
		/*!< Write-around cache mode */

	ocf_cache_mode_pt,
		/*!< Pass-through cache mode */

	ocf_cache_mode_wi,
		/*!< Write invalidate cache mode */

	ocf_cache_mode_wo,
		/*!< Write-only cache mode */

	ocf_cache_mode_max,
		/*!< Stopper of cache mode enumerator */

	ocf_cache_mode_default = ocf_cache_mode_wt,
		/*!< Default cache mode */

	ocf_cache_mode_none = -1,
		/*!< Current cache mode of given cache instance */
} ocf_cache_mode_t;

#define OCF_SEQ_CUTOFF_PERCORE_STREAMS 128
#define OCF_SEQ_CUTOFF_PERQUEUE_STREAMS 64
#define OCF_SEQ_CUTOFF_MIN_THRESHOLD 1
#define OCF_SEQ_CUTOFF_MAX_THRESHOLD 4294841344
#define OCF_SEQ_CUTOFF_MIN_PROMOTION_COUNT 1
#define OCF_SEQ_CUTOFF_MAX_PROMOTION_COUNT 65535

typedef enum {
	ocf_seq_cutoff_policy_always = 0,
		/*!< Sequential cutoff always on */

	ocf_seq_cutoff_policy_full,
		/*!< Sequential cutoff when occupancy is 100% */

	ocf_seq_cutoff_policy_never,
		/*!< Sequential cutoff disabled */

	ocf_seq_cutoff_policy_max,
		/*!< Stopper of sequential cutoff policy enumerator */

	ocf_seq_cutoff_policy_default = ocf_seq_cutoff_policy_full,
		/*!< Default sequential cutoff policy*/
} ocf_seq_cutoff_policy;

/**
 * OCF supported promotion policy types
 */
typedef enum {
	ocf_promotion_always = 0,
		/*!< No promotion policy. Cache inserts are not filtered */

	ocf_promotion_nhit,
		/*!< Line can be inserted after N requests for it */

	ocf_promotion_max,
		/*!< Stopper of enumerator */

	ocf_promotion_default = ocf_promotion_always,
		/*!< Default promotion policy */
} ocf_promotion_t;

/**
 * OCF supported Write-Back cleaning policies type
 */
typedef enum {
	ocf_cleaning_nop = 0,
		/*!< Cleaning won't happen in background. Only on eviction or
		 * during cache stop
		 */

	ocf_cleaning_alru,
		/*!< Approximately recently used. Cleaning thread in the
		 * background enabled which cleans dirty data during IO
		 * inactivity.
		 */

	ocf_cleaning_acp,
		/*!< Cleaning algorithm attempts to reduce core device seek
		 * distance. Cleaning thread runs concurrently with I/O.
		 */

	ocf_cleaning_max,
		/*!< Stopper of enumerator */

	ocf_cleaning_default = ocf_cleaning_alru,
		/*!< Default cleaning policy type */
} ocf_cleaning_t;

/**
 * OCF supported cache line sizes in bytes
 */
typedef enum {
	ocf_cache_line_size_none = 0,
		/*!< None */

	ocf_cache_line_size_4 = 4 * KiB,
		/*!< 4 kiB */

	ocf_cache_line_size_8 = 8 * KiB,
		/*!< 8 kiB */

	ocf_cache_line_size_16 = 16 * KiB,
		/*!< 16 kiB */

	ocf_cache_line_size_32 = 32 * KiB,
		/*!< 32 kiB */

	ocf_cache_line_size_64 = 64 * KiB,
		/*!< 64 kiB */

	ocf_cache_line_size_default = ocf_cache_line_size_4,
		/*!< Default cache line size */

	ocf_cache_line_size_min = ocf_cache_line_size_4,
		/*!< Minimum cache line size */

	ocf_cache_line_size_max = ocf_cache_line_size_64,
		/*!< Maximal cache line size */

	ocf_cache_line_size_inf = ~0ULL,
		/*!< Force enum to be 64-bit */
} ocf_cache_line_size_t;

/**
 * Metadata layout
 */
typedef enum {
	ocf_metadata_layout_striping = 0,
	ocf_metadata_layout_seq = 1,
	ocf_metadata_layout_max,
	ocf_metadata_layout_default = ocf_metadata_layout_striping
} ocf_metadata_layout_t;

/**
 * @name OCF IO class definitions
 */
/**
 * Maximum numbers of IO classes per cache instance
 */
#define OCF_USER_IO_CLASS_MAX OCF_CONFIG_MAX_IO_CLASSES
/**
 * Minimum value of a valid IO class ID
 */
#define OCF_IO_CLASS_ID_MIN 0
/**
 * Maximum value of a valid IO class ID
 */
#define OCF_IO_CLASS_ID_MAX (OCF_USER_IO_CLASS_MAX - 1)
/**
 * Invalid value of IO class id
 */
#define OCF_IO_CLASS_INVALID OCF_USER_IO_CLASS_MAX

/** Maximum size of the IO class name */
#define OCF_IO_CLASS_NAME_MAX 1024

/** IO class priority which indicates pinning */
#define OCF_IO_CLASS_PRIO_PINNED -1

/** The highest IO class priority  */
#define OCF_IO_CLASS_PRIO_HIGHEST 0

/** The lowest IO class priority */
#define OCF_IO_CLASS_PRIO_LOWEST 255

/** Default IO class priority */
#define OCF_IO_CLASS_PRIO_DEFAULT OCF_IO_CLASS_PRIO_LOWEST
/**
 * @}
 */

/**
 * @name I/O operations
 * @{
 */
#define OCF_READ		0
#define OCF_WRITE		1
/**
 * @}
 */

/**
 * @name OCF cleaner definitions
 * @{
 */
#define OCF_CLEANER_DISABLE ~0U
/**
 * @}
 */

#define MAX_TRIM_RQ_SIZE	(512 * KiB)

#endif /* __OCF_DEF_H__ */
