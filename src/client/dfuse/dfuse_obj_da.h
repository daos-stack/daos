/**
 * (C) Copyright 2017-2020 Intel Corporation.
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

#ifndef __DFUSE_OBJ_DA_H__
#define __DFUSE_OBJ_DA_H__

#include <daos_errno.h>

/* This data structure is intended for small objects */
#define MAX_POOL_OBJ_SIZE 256

typedef struct {
	char data[128];
} obj_da_t;

/* Initialize an object obj_da
 * \param da[out] Allocator to initialize
 * \param obj_size[in] Size of objects in da
 */
int
obj_da_initialize(obj_da_t *da, size_t obj_size);
/* Destroy a da and all objects in da */
int
obj_da_destroy(obj_da_t *da);

/* Get a zero initialized item from the da
 * \param [in] Allocator from which to get item
 * \param [out] Pointer in which to store pointer to item
 */
#define obj_da_get(da, itempp)             \
	obj_da_get_(da, (void **)(itempp), \
		      sizeof(**(itempp)))

/* Return an item to the da
 * \param [in] Item to return to da
 */
int
obj_da_put(obj_da_t *da, void *item);

/* Internal routine.  Use da_allocate instead */
int
obj_da_get_(obj_da_t *da, void **item, size_t size);

#endif /*  __DFUSE_OBJ_DA_H__ */
