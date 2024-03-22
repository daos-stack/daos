/**
 * (C) Copyright 2023-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <abt.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos_errno.h>

int
dss_ult_create_all(void (*func)(void *), void *arg, bool main)
{
	assert_true(false);
	return -DER_NOMEM;
}

int
dss_ult_create(void (*func)(void *), void *arg, int xs_type, int tgt_idx, size_t stack_size,
	       ABT_thread *ult)
{
	assert_true(false);
	return -DER_NOMEM;
}

int
dss_thread_collective(int (*func)(void *), void *arg, unsigned int flags)
{
	assert_true(false);
	return -DER_NOMEM;
}

struct dss_coll_ops;
struct dss_coll_args;

int
dss_thread_collective_reduce(struct dss_coll_ops *ops, struct dss_coll_args *args,
			     unsigned int flags)
{
	assert_true(false);
	return -DER_NOMEM;
}
