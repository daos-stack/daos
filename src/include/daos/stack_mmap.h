/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * src/include/daos/stack_mmap.h
 */

/*
 * Implementation of an alternate and external way to allocate a stack
 * area for any Argobots ULT.
 * This aims to allow for a better way to detect/protect against stack
 * overflow situations along with automatic growth capability.
 * Each individual stack will be mmap()'ed with MAP_GROWSDOWN causing
 * the Kernel to reserve stack_guard_gap number of prior additional pages
 * that will be reserved for no other mapping and prevented to be accessed.
 * The stacks are managed as a pool, using the mmap_stack_desc_t struct
 * being located at the bottom (upper addresses) of each stack and being
 * linked as a list upon ULT exit for future re-use by a new ULT, based on
 * the requested stack size.
 * The free stacks list is drained upon a certain number of free stacks or
 * upon a certain percentage of free stacks.
 */

#ifdef ULT_MMAP_STACK
#include <sys/mman.h>
#include <abt.h>

/* mmap()'ed stacks can allow for a bigger size with no impact on
 * memory footprint if unused
 */
#define MMAPED_ULT_STACK_SIZE (2 * 1024 * 1024)

/* ABT_key for mmap()'ed ULT stacks */
extern ABT_key stack_key;

/* since being allocated before start of stack its size must be a
 * multiple of (void *) !!
 */
typedef struct {
	void *stack;
	size_t stack_size;
	void (*thread_func)(void *);
	void *thread_arg;
	d_list_t stack_list;
} mmap_stack_desc_t;

/* pool of free stacks */
extern pthread_mutex_t stack_free_list_lock;
extern d_list_t stack_free_list;

void free_stack(void *arg);

int mmap_stack_thread_create(ABT_pool pool, void (*thread_func)(void *),
			     void *thread_arg, ABT_thread_attr attr,
			     ABT_thread *newthread);

int mmap_stack_thread_create_on_xstream(ABT_xstream xstream,
					void (*thread_func)(void *),
					void *thread_arg, ABT_thread_attr attr,
					ABT_thread *newthread);

#define daos_abt_thread_create mmap_stack_thread_create
#define daos_abt_thread_create_on_xstream mmap_stack_thread_create_on_xstream
#else
#define daos_abt_thread_create ABT_thread_create
#define daos_abt_thread_create_on_xstream ABT_thread_create_on_xstream
#endif
