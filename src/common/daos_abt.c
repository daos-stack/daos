/**
 * (C) Copyright 2024-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdbool.h>
#include <abt.h>

#include <daos/ult_stack_mmap.h>
#include <daos_srv/daos_engine.h>
#include "daos_abt.h"

/** True iff ULT mmap'ed() stack are enabled */
static bool g_is_usm_enabled = false;

static int (*g_thread_create_on_pool)(ABT_pool, void (*)(void *), void *, ABT_thread_attr,
				      ABT_thread *)            = ABT_thread_create;
static int (*g_thread_create_on_xstream)(ABT_xstream, void (*)(void *), void *, ABT_thread_attr,
					 ABT_thread *)         = ABT_thread_create_on_xstream;
static int (*g_thread_get_func)(ABT_thread, void (**)(void *)) = ABT_thread_get_thread_func;
static int (*g_thread_get_arg)(ABT_thread, void **)            = ABT_thread_get_arg;

int
da_initialize(int argc, char *argv[])
{
	bool is_usm_enabled = false;
	int  rc;

	rc = ABT_init(argc, argv);
	if (unlikely(rc != ABT_SUCCESS)) {
		D_ERROR("Failed to init Argobot: " AF_RC "\n", AP_RC(rc));
		D_GOTO(out, rc);
	}

	d_getenv_bool("DAOS_ULT_STACK_MMAP", &is_usm_enabled);
	if (!is_usm_enabled) {
		D_INFO("ULT mmap()'ed stack allocation is disabled\n");
		D_GOTO(out, rc = ABT_SUCCESS);
	}

	rc = usm_initialize();
	if (unlikely(rc != ABT_SUCCESS)) {
		D_ERROR("Failed to initialize ULT mmap()'ed stack allocator: " AF_RC "\n",
			AP_RC(rc));
		D_GOTO(out, rc);
	}

	g_thread_create_on_pool    = usm_thread_create_on_pool;
	g_thread_create_on_xstream = usm_thread_create_on_xstream;
	g_thread_get_func          = usm_thread_get_func;
	g_thread_get_arg           = usm_thread_get_arg;
	g_is_usm_enabled           = true;
	D_INFO("ULT mmap()'ed stack allocation is enabled\n");

out:
	return rc;
}

void
da_finalize(void)
{
	if (g_is_usm_enabled)
		usm_finalize();
	ABT_finalize();
}

int
da_thread_create_on_pool(ABT_pool pool, void (*thread_func)(void *), void *arg,
			 ABT_thread_attr attr, ABT_thread *newthread)
{
	return g_thread_create_on_pool(pool, thread_func, arg, attr, newthread);
}

int
da_thread_create_on_xstream(ABT_xstream xstream, void (*thread_func)(void *), void *arg,
			    ABT_thread_attr attr, ABT_thread *newthread)
{
	return g_thread_create_on_xstream(xstream, thread_func, arg, attr, newthread);
}

int
da_thread_get_func(ABT_thread thread, void (**thread_func)(void *))
{
	return g_thread_get_func(thread, thread_func);
}

int
da_thread_get_arg(ABT_thread thread, void **arg)
{
	return g_thread_get_arg(thread, arg);
}
