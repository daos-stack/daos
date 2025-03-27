/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __OCF_PROMOTION_NHIT_H__
#define __OCF_PROMOTION_NHIT_H__

enum ocf_nhit_param {
	ocf_nhit_insertion_threshold,
	ocf_nhit_trigger_threshold,
	ocf_nhit_param_max
};

#define OCF_NHIT_MIN_THRESHOLD 2
#define OCF_NHIT_MAX_THRESHOLD 1000
#define OCF_NHIT_THRESHOLD_DEFAULT 3

#define OCF_NHIT_MIN_TRIGGER 0
#define OCF_NHIT_MAX_TRIGGER 100
#define OCF_NHIT_TRIGGER_DEFAULT 80

#endif /* __OCF_PROMOTION_NHIT_H__ */
