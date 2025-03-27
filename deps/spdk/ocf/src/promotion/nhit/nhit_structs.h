/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#ifndef __PROMOTION_NHIT_STRUCTS_H_
#define __PROMOTION_NHIT_STRUCTS_H_

struct nhit_promotion_policy_config {
	uint32_t insertion_threshold;
	/*!< Number of hits */

	uint32_t trigger_threshold;
	/*!< Cache occupancy (percentage value) */
};

#endif
