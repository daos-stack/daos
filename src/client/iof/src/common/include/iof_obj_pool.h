/* Copyright (C) 2017-2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * A simple, efficient pool for allocating small objects of equal size.
 */
#ifndef __IOF_OBJ_POOL_H__
#define __IOF_OBJ_POOL_H__

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

#include <gurt/errno.h>

/* This data structure is intended for small objects */
#define MAX_POOL_OBJ_SIZE 256

typedef struct {
	char data[128];
} obj_pool_t;

/* Initialize an object obj_pool
 * \param pool[out] Pool to initialize
 * \param obj_size[in] Size of objects in pool
 */
int obj_pool_initialize(obj_pool_t *pool, size_t obj_size);
/* Destroy a pool and all objects in pool */
int obj_pool_destroy(obj_pool_t *pool);

/* Get a zero initialized item from the pool
 * \param [in] Pool from which to get item
 * \param [out] Pointer in which to store pointer to item
 */
#define obj_pool_get(pool, itempp)             \
	obj_pool_get_(pool, (void **)(itempp), \
		      sizeof(**(itempp)))

/* Return an item to the pool
 * \param [in] Item to return to pool
 */
int obj_pool_put(obj_pool_t *pool, void *item);

/* Internal routine.  Use pool_allocate instead */
int obj_pool_get_(obj_pool_t *pool, void **item, size_t size);

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /*  __IOF_OBJ_POOL_H__ */
