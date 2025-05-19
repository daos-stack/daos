/**
 * (C) Copyright 2023-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
#include <uuid/uuid.h>

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

int
ds_pool_thread_collective(uuid_t pool_uuid, uint32_t ex_status, int (*coll_func)(void *),
			  void *arg, uint32_t flags)
{
	assert_true(false);
	return -DER_NOMEM;
}

/** Status of a chore */
enum dss_chore_status {
	DSS_CHORE_NEW,		/**< ready to be scheduled for the first time (private) */
	DSS_CHORE_YIELD,	/**< ready to be scheduled again */
	DSS_CHORE_DONE		/**< no more scheduling required */
};

struct dss_chore;

/**
 * Must return either DSS_CHORE_YIELD (if yielding to other chores) or
 * DSS_CHORE_DONE (if terminating). If \a is_reentrance is true, this is not
 * the first time \a chore is scheduled. A typical implementation shall
 * initialize its internal state variables if \a is_reentrance is false. See
 * dtx_leader_exec_ops_chore for an example.
 */
typedef enum dss_chore_status (*dss_chore_func_t)(struct dss_chore *chore, bool is_reentrance);

void
dss_chore_diy(struct dss_chore *chore)
{
	assert_true(false);
}

int
dss_chore_register(struct dss_chore *chore)
{
	assert_true(false);
	return -DER_NOMEM;
}

void
dss_chore_deregister(struct dss_chore *chore)
{
	assert_true(false);
}
