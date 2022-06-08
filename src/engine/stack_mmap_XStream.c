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
#include "srv_internal.h"
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
	struct dss_xstream *dx = dss_current_xstream();
	struct stack_pool *sp;

	/* XXX
	 * We may need to reevaluate stack size since a growth may
	 * have occurred during previous context life time, if initial
	 * stack size has overflowed when there was no previous mapping
	 * in address space to trigger Kernel's stack guard gap
	 * (stack_guard_gap) ? This for both munmap() or queuing in
	 * free pool cases.
	 */

	/* desc->sp should have been set to executing XStream's stacks
	 * pool, but double-check just in case ULT has been migrated to an
	 * other XStream in-between ...
	 */
	if (dx == NULL)
		sp = desc->sp;
	else
		sp = dx->dx_sp;

	free_stack_in_pool(desc, sp);
}

/* wrapper for ULT main function, mainly to register mmap()'ed stack
 * descriptor as ABT_key to ensure stack pooling or munmap() upon ULT exit
 */
void mmap_stack_wrapper(void *arg)
{
	mmap_stack_desc_t *desc = (mmap_stack_desc_t *)arg;
	struct dss_thread_local_storage *dtc = dss_tls_get();
	struct dss_xstream *dx = NULL;

	ABT_key_set(stack_key, desc);

	/* try to ensure stack-pool to be the one of execution XStream */
	if (dtc != NULL) {
		dx = dss_current_xstream();
		if (dx != NULL) {
			D_DEBUG(DB_MEM,
				"changing current XStream stack pool from %p to %p in stack descriptor %p\n",
				desc->sp, dx->dx_sp, desc);
			desc->sp = dx->dx_sp;
		} else {
			D_DEBUG(DB_MEM,
				"Can't get current XStream because its value has still not been set in its TLS\n");
		}
	} else {
		D_DEBUG(DB_MEM,
			"Can't get current XStream because its TLS has still not been initialized\n");
	}

	D_DEBUG(DB_MEM,
		"New ULT with stack_desc %p starting on XStream %p running on CPU=%d\n",
		desc, dx, sched_getcpu());
	desc->thread_func(desc->thread_arg);
}
#endif
