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
 * There is one stacks free-list per-engine to allow lock-less management.
 */

#ifdef ULT_MMAP_STACK
#include <sys/mman.h>
#include <search.h>
#include <abt.h>
#include <gurt/atomic.h>

/* the minimum value for vm.max_map_count to allow for mmap()'ed ULT stacks
 * usage. In fact, DEFAULT_MAX_MAP_COUNT, the Kernel's default value !!
 */
#define MIN_VM_MAX_MAP_COUNT 65530

/* max percent of free stacks vs total currently allocated */
#define MAX_PERCENT_FREE_STACKS 20

/* max nb of free stacks per-XStream */
#define MAX_NUMBER_FREE_STACKS 2000

/* per-engine max number of mmap()'ed ULTs stacks */
extern int max_nb_mmap_stacks;

/* engine's current number of mmap()'ed ULTs stacks */
extern ATOMIC int nb_mmap_stacks;

/* engine's current number of free mmap()'ed ULTs stacks */
extern ATOMIC int nb_free_stacks;

/* mmap()'ed stacks can allow for a bigger size with no impact on
 * memory footprint if unused
 */
#define MMAPED_ULT_STACK_SIZE (1 * 1024 * 1024)

/* ABT_key for mmap()'ed ULT stacks */
extern ABT_key stack_key;

extern bool daos_ult_mmap_stack;

/* pool of free stacks */
struct stack_pool {
	/* per-xstream pool/list of free stacks */
	d_list_t		sp_stack_free_list;
	/* nb of free stacks in pool/list */
	uint64_t		sp_free_stacks;
};

/* since being allocated before start of stack its size must be a
 * multiple of (void *) !!
 */
typedef struct {
	void *stack;
	size_t stack_size;
	/* ULT primary function */
	void (*thread_func)(void *);
	/* ULT arg */
	void *thread_arg;
	/* per-size free-list of stacks */
	d_list_t stack_list;
	/* ULT execution XStream, where to free stack */
	struct stack_pool *sp;
} mmap_stack_desc_t;

void free_stack(void *arg);

void mmap_stack_wrapper(void *arg);

int mmap_stack_thread_create(struct stack_pool *sp_alloc, struct stack_pool *sp_free,
			     ABT_pool pool, void (*thread_func)(void *), void *thread_arg,
			     ABT_thread_attr attr, ABT_thread *newthread);

int mmap_stack_thread_create_on_xstream(struct stack_pool *sp_alloc, struct stack_pool *sp_free,
					ABT_xstream xstream, void (*thread_func)(void *),
					void *thread_arg, ABT_thread_attr attr,
					ABT_thread *newthread);

int stack_pool_create(struct stack_pool **sp);

void stack_pool_destroy(struct stack_pool *sp);

#define daos_abt_thread_create mmap_stack_thread_create
#define daos_abt_thread_create_on_xstream mmap_stack_thread_create_on_xstream
#else /* !defined(ULT_MMAP_STACK) */
#define daos_abt_thread_create(sp_alloc, sp_free, ...) ABT_thread_create(__VA_ARGS__)
#define daos_abt_thread_create_on_xstream(sp_alloc, sp_free, ...) ABT_thread_create_on_xstream(__VA_ARGS__)
#endif
