/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __METADATA_HASH_H__
#define __METADATA_HASH_H__

/**
 * @file metadata_.h
 * @brief Metadata Service - Hash Implementation
 */

#include "../ocf_request.h"
/**
 * @brief Metada hash elements type
 */
enum ocf_metadata_segment_id {
	metadata_segment_sb_config = 0,	/*!< Super block conf */
	metadata_segment_sb_runtime,	/*!< Super block runtime */
	metadata_segment_reserved,	/*!< Reserved space on disk */
	metadata_segment_part_config,	/*!< Part Config Metadata */
	metadata_segment_part_runtime,	/*!< Part Runtime Metadata */
	metadata_segment_core_config,	/*!< Core Config Metadata */
	metadata_segment_core_runtime,	/*!< Core Runtime Metadata */
	metadata_segment_core_uuid,	/*!< Core UUID */
	/* .... new fixed size sections go here */

	metadata_segment_fixed_size_max,
	metadata_segment_variable_size_start = metadata_segment_fixed_size_max,

	/* sections with size dependent on cache device size go here: */
	metadata_segment_cleaning =	/*!< Cleaning policy */
			metadata_segment_variable_size_start,
	metadata_segment_lru,	/*!< Eviction policy */
	metadata_segment_collision,	/*!< Collision */
	metadata_segment_list_info,	/*!< Collision */
	metadata_segment_hash,		/*!< Hash */
	/* .... new variable size sections go here */

	metadata_segment_max,		/*!< MAX */
};

#endif /* METADATA_HASH_H_ */
