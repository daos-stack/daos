/*
 * Copyright(c) 2020-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __METADATA_SEGMENT_OPS_H__
#define __METADATA_SEGMENT_OPS_H__

#include "../utils/utils_pipeline.h"
#include "metadata_raw.h"
#include <ocf/ocf_def.h>

struct ocf_metadata_segment
{
	struct ocf_metadata_raw *raw;
	struct ocf_metadata_segment *superblock;
};

int ocf_metadata_segment_init(
		struct ocf_metadata_segment **self,
		struct ocf_cache *cache,
		struct ocf_metadata_raw *raw,
		ocf_flush_page_synch_t lock_page_pfn,
		ocf_flush_page_synch_t unlock_page_pfn,
		struct ocf_metadata_segment *superblock);

void ocf_metadata_segment_destroy(struct ocf_cache *cache,
		struct ocf_metadata_segment *self);

void ocf_metadata_check_crc_if_clean(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg);

void ocf_metadata_check_crc(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg);

void ocf_metadata_calculate_crc(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg);

void ocf_metadata_flush_segment(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg);

void ocf_metadata_load_segment(ocf_pipeline_t pipeline,
		void *priv, ocf_pipeline_arg_t arg);

#endif
