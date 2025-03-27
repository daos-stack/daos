/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __METADATA_PARTITION_H__
#define __METADATA_PARTITION_H__

#include "metadata_partition_structs.h"
#include "../ocf_cache_priv.h"

#define PARTITION_DEFAULT		0
#define PARTITION_UNSPECIFIED		((ocf_part_id_t)-1)
#define PARTITION_FREELIST		OCF_USER_IO_CLASS_MAX + 1
#define PARTITION_SIZE_MIN		0
#define PARTITION_SIZE_MAX		100

ocf_part_id_t ocf_metadata_get_partition_id(struct ocf_cache *cache,
		ocf_cache_line_t line);

void ocf_metadata_set_partition_id(
		struct ocf_cache *cache, ocf_cache_line_t line,
		ocf_part_id_t part_id);

#endif /* __METADATA_PARTITION_H__ */
