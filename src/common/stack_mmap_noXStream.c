/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * This file contains the engine/XStream specific part of the
 * mmap()'ed ULT stack allocation feature.
 */

#ifdef ULT_MMAP_STACK
#include <daos/common.h>
#include <daos/stack_mmap.h>
#include <errno.h>
#include <string.h>

/* XXX both thresholds may need to be dynamically determined based on the
 * number of free stack pool (one per-XStream), and max_nb_mmap_stacks (see
 * below).
 */
#define MAX_PERCENT_FREE_STACKS 20
#define MAX_NUMBER_FREE_STACKS 2000

/* callback to free stack upon ULT exit during stack_key deregister */
void free_stack(void *arg)
{
	mmap_stack_desc_t *desc = (mmap_stack_desc_t *)arg;
	struct stack_pool *sp;

	sp = desc->sp;
	free_stack_in_pool(desc, sp);
}

/* wrapper for ULT main function, mainly to register mmap()'ed stack
 * descriptor as ABT_key to ensure stack pooling or munmap() upon ULT exit
 */
void mmap_stack_wrapper(void *arg)
{
	mmap_stack_desc_t *desc = (mmap_stack_desc_t *)arg;

	ABT_key_set(stack_key, desc);

	D_DEBUG(DB_MEM,
		"New ULT with stack_desc %p running on CPU=%d\n",
		desc, sched_getcpu());
	desc->thread_func(desc->thread_arg);
}
#endif
