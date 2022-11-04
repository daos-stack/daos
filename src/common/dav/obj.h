/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2014-2021, Intel Corporation */

/*
 * obj.h -- internal definitions for obj module
 */

#ifndef __DAOS_COMMON_OBJ_H
#define __DAOS_COMMON_OBJ_H 1

#include <stddef.h>
#include <stdint.h>

#include "dav_internal.h"
#include "stats.h"

#define OBJ_OFF_TO_PTR(pop, off) ((void *)((uintptr_t)(((dav_obj_t *)(pop))->do_base) + (off)))
#define OBJ_PTR_TO_OFF(pop, ptr) ((uintptr_t)(ptr) - (uintptr_t)(((dav_obj_t *)(pop))->do_base))
#define OBJ_OFF_FROM_HEAP(pop, off)\
	((off) >= ((dav_obj_t *)(pop))->do_phdr->dp_heap_offset &&\
	(off) < ((dav_obj_t *)(pop))->do_phdr->dp_heap_offset +\
		((dav_obj_t *)(pop))->do_phdr->dp_heap_size)

#define OBJ_OFF_IS_VALID(pop, off)\
	(OBJ_OFF_FROM_HEAP(pop, off) ||\
	(OBJ_PTR_TO_OFF(pop, &((dav_obj_t *)(pop))->do_phdr->dp_root_offset) == (off)) ||\
	(OBJ_PTR_TO_OFF(pop, &((dav_obj_t *)(pop))->do_phdr->dp_root_size) == (off)))

#define OBJ_PTR_IS_VALID(pop, ptr)\
	OBJ_OFF_IS_VALID(pop, OBJ_PTR_TO_OFF(pop, ptr))

#define OBJ_PTR_FROM_POOL(pop, ptr)\
	((uintptr_t)(ptr) >= (uintptr_t)(((dav_obj_t *)pop)->do_base) &&\
	(uintptr_t)(ptr) < (uintptr_t)(((dav_obj_t *)pop)->do_base) +\
		(((dav_obj_t *)pop)->do_phdr->dp_heap_offset +\
		 ((dav_obj_t *)pop)->do_phdr->dp_heap_size))

#define	OBJ_OFFRANGE_FROM_HEAP(pop, start, end)\
	(((start) >= ((dav_obj_t *)pop)->do_phdr->dp_heap_offset) &&\
	 ((end) <=  (((dav_obj_t *)pop)->do_phdr->dp_heap_offset + \
		     ((dav_obj_t *)pop)->do_phdr->dp_heap_size)))

typedef uint64_t type_num_t;

#define CLASS_ID_FROM_FLAG(flag)\
((uint16_t)((flag) >> 48))

#define ARENA_ID_FROM_FLAG(flag)\
((uint16_t)((flag) >> 32))

#endif /* __DAOS_COMMON_OBJ_H */
