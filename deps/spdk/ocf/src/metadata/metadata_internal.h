/*
 * Copyright(c) 2020-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __METADATA_INTERNAL_H__
#define __METADATA_INTERNAL_H__

#include <ocf/ocf_def.h>
#include "../ocf_cache_priv.h"
#include "metadata_segment.h"
#include "metadata_segment_id.h"
#include "metadata_raw.h"

#define METADATA_MEM_POOL(ctrl, section) ctrl->raw_desc[section].mem_pool

/*
 * Metadata control structure
 */
struct ocf_metadata_ctrl {
	ocf_cache_line_t cachelines;
	ocf_cache_line_t start_page;
	ocf_cache_line_t count_pages;
	uint32_t device_lines;
	size_t mapping_size;
	struct ocf_metadata_raw raw_desc[metadata_segment_max];
	struct ocf_metadata_segment *segment[metadata_segment_max];
};

struct ocf_metadata_context {
	ocf_metadata_end_t cmpl;
	void *priv;
	ocf_pipeline_t pipeline;
	ocf_cache_t cache;
	struct ocf_metadata_ctrl *ctrl;
	struct ocf_metadata_raw segment_copy[metadata_segment_fixed_size_max];
};

extern const char * const ocf_metadata_segment_names[];

#endif
