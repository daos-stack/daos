/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2014-2024, Intel Corporation */

/*
 * obj.h -- internal definitions for obj module
 */

#ifndef __DAOS_COMMON_OBJ_H
#define __DAOS_COMMON_OBJ_H 1

#include <stddef.h>
#include <stdint.h>

#include "dav_internal.h"
#include "stats.h"
#include "daos/mem.h"

#define OBJ_OFF_TO_PTR(pop, off) umem_cache_off2ptr(((dav_obj_t *)pop)->do_store, off)
#define OBJ_PTR_TO_OFF(pop, ptr) umem_cache_ptr2off(((dav_obj_t *)pop)->do_store, ptr)
#define OBJ_OFF_FROM_HEAP(pop, off)                                                                \
	(((off) >= (ALIGN_UP(sizeof(struct heap_header), 4096))) &&                                \
	 ((off) < ((dav_obj_t *)(pop))->do_size_meta))

#define OBJ_OFF_IS_VALID(pop, off) OBJ_OFF_FROM_HEAP(pop, off)

#define OBJ_PTR_FROM_POOL(pop, ptr)                                                                \
	((uintptr_t)(ptr) >= (uintptr_t)(((dav_obj_t *)pop)->do_base) &&                           \
	 (uintptr_t)(ptr) <                                                                        \
	     (uintptr_t)(((dav_obj_t *)pop)->do_base) + (((dav_obj_t *)pop)->do_size_mem))

#define OBJ_PTR_IS_VALID(pop, ptr) OBJ_PTR_FROM_POOL(pop, ptr)

#define OBJ_OFFRANGE_FROM_HEAP(pop, start, end)                                                    \
	(((start) >= (ALIGN_UP(sizeof(struct heap_header), 4096))) &&                              \
	 ((end) <= (((dav_obj_t *)pop)->do_size_meta)))

typedef uint64_t type_num_t;

#define CLASS_ID_FROM_FLAG(flag) ((uint16_t)((flag) >> 48))
#define EZONE_ID_FROM_FLAG(flag) ((uint32_t)((flag) >> 16))

#endif /* __DAOS_COMMON_OBJ_H */
