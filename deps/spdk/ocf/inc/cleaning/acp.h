/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#ifndef __OCF_CLEANING_ACP_H__
#define __OCF_CLEANING_ACP_H__

/**
 * @file
 * @brief ACP cleaning policy API
 */

enum ocf_cleaning_acp_parameters {
	ocf_acp_wake_up_time,
	ocf_acp_flush_max_buffers,
};

/**
 * @name ACP cleaning policy parameters
 * @{
 */

/**
 * ACP cleaning policy time between flushing cycles (in ms)
 */

/**< Wake up time minimum value */
#define OCF_ACP_MIN_WAKE_UP			0
/**< Wake up time maximum value */
#define OCF_ACP_MAX_WAKE_UP			10000
/**< Wake up time default value */
#define OCF_ACP_DEFAULT_WAKE_UP			10

/**
 * ACP cleaning thread number of dirty cache lines to be flushed in one cycle
 */

/** Dirty cache lines to be flushed in one cycle minimum value */
#define OCF_ACP_MIN_FLUSH_MAX_BUFFERS		1
/** Dirty cache lines to be flushed in one cycle maximum value */
#define OCF_ACP_MAX_FLUSH_MAX_BUFFERS		10000
/** Dirty cache lines to be flushed in one cycle default value */
#define OCF_ACP_DEFAULT_FLUSH_MAX_BUFFERS	128

/**
 * @}
 */

#endif /* __OCF_CLEANING_ACP_H__ */
