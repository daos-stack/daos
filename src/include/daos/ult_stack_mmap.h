/**
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * Stack Memory Map allocator for Argobots ULT.
 *
 * Implementation of an alternate and external way to allocate a stack
 * area for any Argobots ULT.
 * This aims to allow for a better way to detect/protect against stack
 * overflow situations along with automatic growth capability.
 * Each individual stack will be mmap()'ed with MAP_GROWSDOWN causing
 * the Kernel to reserve stack_guard_gap number of prior additional pages
 * that will be reserved for no other mapping and prevented to be accessed.
 * The stacks are managed as a pool, using the mmap_stack_desc_t struct
 * being located at the bottom (upper addresses) of each stack and being
 * linked as a list upon ULT exit for future reuse by a new ULT, based on
 * the requested stack size.
 * The free stacks list is drained upon a certain number of free stacks or
 * upon a certain percentage of free stacks.
 * There is one stacks free-list per-engine to allow lock-less management.
 */

#ifndef __ULT_STACK_MMAP_H__
#define __ULT_STACK_MMAP_H__

#include <abt.h>

#include "daos_abt.h"

int
usm_initialize(void);
void
usm_finalize(void);

int
usm_thread_create_on_pool(ABT_pool pool, void (*thread_func)(void *), void *thread_arg,
			  ABT_thread_attr attr, ABT_thread *newthread);
int
usm_thread_create_on_xstream(ABT_xstream xstream, void (*thread_func)(void *), void *thread_arg,
			     ABT_thread_attr attr, ABT_thread *newthread);
int
usm_thread_get_func(ABT_thread thread, void (**func)(void *));

int
usm_thread_get_arg(ABT_thread thread, void **arg);

#endif /* __ULT_STACK_MMAP_H__ */
