/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * @file
 * @brief OCF types
 */
#ifndef __OCF_TYPES_H_
#define __OCF_TYPES_H_

#include "ocf_env_headers.h"

/**
 * @brief cache line type (by default designated as 32 bit unsigned integer)
 */
typedef uint32_t ocf_cache_line_t;

/**
 * @brief core id type (by default designated as 16 bit unsigned integer)
 */
typedef uint16_t ocf_core_id_t;

/**
 * @brief core sequence number type (by default designated as 16 bit unsigned integer)
 */
typedef uint16_t ocf_seq_no_t;

/**
 * @brief partition id type (by default designated as 16 bit unsigned integer)
 */
typedef uint16_t ocf_part_id_t;

/**
 * @brief handle to object designating ocf context
 */
typedef struct ocf_ctx *ocf_ctx_t;

struct ocf_cache;
/**
 * @brief handle to object designating ocf cache device
 */
typedef struct ocf_cache *ocf_cache_t;

struct ocf_core;
/**
 * @brief handle to object designating ocf core object
 */
typedef struct ocf_core *ocf_core_t;

struct ocf_volume;
/**
 * @brief handle to object designating ocf volume
 */
typedef struct ocf_volume *ocf_volume_t;


struct ocf_volume_type;
/**
 * @brief handle to volume type
 */
typedef struct ocf_volume_type *ocf_volume_type_t;

/**
 * @brief handle to volume uuid
 */
typedef struct ocf_volume_uuid *ocf_uuid_t;

/**
 * @brief handle to object designating ocf context object
 */
typedef void ctx_data_t;

/**
 * @brief handle to I/O queue
 */
typedef struct ocf_queue *ocf_queue_t;

/**
 * @brief handle to cleaner
 */
typedef struct ocf_cleaner *ocf_cleaner_t;

/**
 * @brief handle to metadata_updater
 */
typedef struct ocf_metadata_updater *ocf_metadata_updater_t;

/**
 * @brief handle to logger
 */
typedef struct ocf_logger *ocf_logger_t;

#endif
