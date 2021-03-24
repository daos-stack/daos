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
} mmap_stack_desc_t;

void free_stack(void *arg);

int mmap_stack_thread_create(ABT_pool pool, void (*thread_func)(void *),
			     void *thread_arg, ABT_thread_attr attr,
			     ABT_thread *newthread);

int mmap_stack_thread_create_on_xstream(ABT_xstream xstream,
					void (*thread_func)(void *),
					void *thread_arg, ABT_thread_attr attr,
					ABT_thread *newthread);
#endif
