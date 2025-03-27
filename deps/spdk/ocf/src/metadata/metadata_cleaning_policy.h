/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __METADATA_CLEANING_POLICY_H__
#define __METADATA_CLEANING_POLICY_H__

struct cleaning_policy_meta *
ocf_metadata_get_cleaning_policy(struct ocf_cache *cache,
		ocf_cache_line_t line);


#endif /* METADATA_CLEANING_POLICY_H_ */
