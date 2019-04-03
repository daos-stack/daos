/**
 * (C) Copyright 2017-2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#ifndef __DFUSE_OBJ_POOL_H__
#define __DFUSE_OBJ_POOL_H__

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
int
obj_pool_initialize(obj_pool_t *pool, size_t obj_size);
/* Destroy a pool and all objects in pool */
int
obj_pool_destroy(obj_pool_t *pool);

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
int
obj_pool_put(obj_pool_t *pool, void *item);

/* Internal routine.  Use pool_allocate instead */
int
obj_pool_get_(obj_pool_t *pool, void **item, size_t size);

#endif /*  __DFUSE_OBJ_POOL_H__ */
