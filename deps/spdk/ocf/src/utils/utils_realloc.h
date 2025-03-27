/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef UTILS_REALLOC_H_
#define UTILS_REALLOC_H_

/**
 * @file utils_realloc.h
 * @brief OCF realloc
 */

void ocf_realloc_init(void **mem, size_t *limit);

int ocf_realloc(void **mem, size_t size, size_t count, size_t *limit);

int ocf_realloc_cp(void **mem, size_t size, size_t count, size_t *limit);

/**
 * @brief Initialize memory pointer and limit before reallocator usage
 *
 * @param[inout] mem - Pointer to the memory
 * @param[inout] limit - Variable used internally by reallocator and indicates
 * last allocation size
 */
#define OCF_REALLOC_INIT(mem, limit) \
		ocf_realloc_init((void **)mem, limit)

/**
 * @brief De-Initialize memory pointer and limit, free memory
 *
 * @param[inout] mem - Pointer to the memory
 * @param[inout] limit - Variable used internally by reallocator and indicates
 * last allocation size
 */
#define OCF_REALLOC_DEINIT(mem, limit) \
		ocf_realloc((void **)mem, 0, 0, limit)

/**
 * @brief Reallocate referenced memory if it is required.
 *
 * @param[inout] mem - Pointer to the memory
 * @param[in] size - Size of particular element
 * @param[in] count - Counts of element
 * @param[inout] limit - Variable used internally by reallocator and indicates
 * last allocation size
 *
 * @return 0 - Reallocation successful, Non zero - Realocation ERROR
 */
#define OCF_REALLOC(mem, size, count, limit) \
		ocf_realloc((void **)mem, size, count, limit)

/**
 * @brief Reallocate referenced memory if it is required and copy old content
 * into new memory space, new memory space is set to '0'
 *
 * @param[inout] mem - Pointer to the memory
 * @param[in] size - Size of particular element
 * @param[in] count - Counts of element
 * @param[inout] limit - Variable used internally by reallocator and indicates
 * last allocation size
 *
 * @return 0 - Reallocation successful, Non zero - Realocation ERROR
 */
#define OCF_REALLOC_CP(mem, size, count, limit) \
		ocf_realloc_cp((void **)mem, size, count, limit)

#endif /* UTILS_REALLOC_H_ */
