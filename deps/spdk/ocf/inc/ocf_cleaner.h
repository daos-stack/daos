/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef OCF_CLEANER_H_
#define OCF_CLEANER_H_

/**
 * @file
 * @brief OCF cleaner API for synchronization dirty data
 *
 */

/**
 * @brief OCF Cleaner completion
 *
 * @note Completion function for cleaner
 *
 * @param[in] cleaner Cleaner instance
 * @param[in] interval Time to sleep before next cleaner iteration
 */
typedef void (*ocf_cleaner_end_t)(ocf_cleaner_t cleaner, uint32_t interval);

/**
 * @brief Set cleaner completion function
 *
 * @param[in] cleaner Cleaner instance
 * @param[in] fn Completion function
 */
void ocf_cleaner_set_cmpl(ocf_cleaner_t cleaner, ocf_cleaner_end_t fn);

/**
 * @brief Run cleaner
 *
 * @param[in] c Cleaner instance to run
 * @param[in] queue IO queue handle
 */
void ocf_cleaner_run(ocf_cleaner_t c, ocf_queue_t queue);

/**
 * @brief Set cleaner private data
 *
 * @param[in] c Cleaner handle
 * @param[in] priv Private data
 */
void ocf_cleaner_set_priv(ocf_cleaner_t c, void *priv);

/**
 * @brief Get cleaner private data
 *
 * @param[in] c Cleaner handle
 *
 * @retval Cleaner private data
 */
void *ocf_cleaner_get_priv(ocf_cleaner_t c);

/**
 * @brief Get cache instance to which cleaner belongs
 *
 * @param[in] c Cleaner handle
 *
 * @retval Cache instance
 */
ocf_cache_t ocf_cleaner_get_cache(ocf_cleaner_t c);

#endif
