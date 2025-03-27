/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#ifndef __OCF_CLEANING_ALRU_H__
#define __OCF_CLEANING_ALRU_H__

/**
 * @file
 * @brief ALRU cleaning policy API
 */

enum ocf_cleaning_alru_parameters {
	ocf_alru_wake_up_time,
	ocf_alru_stale_buffer_time,
	ocf_alru_flush_max_buffers,
	ocf_alru_activity_threshold,
};

/**
 * @name ALRU cleaning policy parameters
 * @{
 */

/**
 * ALRU cleaning thread wake up time
 */

/** Wake up time minimum value */
#define OCF_ALRU_MIN_WAKE_UP			0
/** Wake up time maximum value */
#define OCF_ALRU_MAX_WAKE_UP			3600
/** Wake up time default value */
#define OCF_ALRU_DEFAULT_WAKE_UP		20

/**
 * ALRU cleaning thread staleness time
 */

/** Staleness time minimum value */
#define OCF_ALRU_MIN_STALENESS_TIME		1
/** Staleness time maximum value */
#define OCF_ALRU_MAX_STALENESS_TIME		3600
/** Staleness time default value*/
#define OCF_ALRU_DEFAULT_STALENESS_TIME		120

/**
 * ALRU cleaning thread number of dirty cache lines to be flushed in one cycle
 */

/** Dirty cache lines to be flushed in one cycle minimum value */
#define OCF_ALRU_MIN_FLUSH_MAX_BUFFERS		1
/** Dirty cache lines to be flushed in one cycle maximum value */
#define OCF_ALRU_MAX_FLUSH_MAX_BUFFERS		10000
/** Dirty cache lines to be flushed in one cycle default value */
#define OCF_ALRU_DEFAULT_FLUSH_MAX_BUFFERS	100

/**
 * ALRU cleaning thread cache idle time before flushing thread can start
 */

/** Idle time before flushing thread can start minimum value */
#define OCF_ALRU_MIN_ACTIVITY_THRESHOLD		0
/** Idle time before flushing thread can start maximum value */
#define OCF_ALRU_MAX_ACTIVITY_THRESHOLD		1000000
/** Idle time before flushing thread can start default value */
#define OCF_ALRU_DEFAULT_ACTIVITY_THRESHOLD	10000

/**
 * @}
 */


#endif /* __OCF_CLEANING_ALRU_H__ */
