/**
 * (C) Copyright 2023 Intel Corporation.
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

struct sched_request;
struct sched_req_attr;

void
sched_req_wakeup(struct sched_request *req)
{
	assert_true(false);
}

struct sched_request *
sched_req_get(struct sched_req_attr *attr, ABT_thread ult)
{
	assert_true(false);
	return NULL;
}

struct sched_request *
sched_create_ult(struct sched_req_attr *attr, void (*func)(void *), void *arg, size_t stack_size)
{
	assert_true(false);
	return NULL;
}

void
sched_req_sleep(struct sched_request *req, uint32_t msecs)
{
	assert_true(false);
}

void
sched_req_put(struct sched_request *req)
{
	assert_true(false);
}

bool
sched_req_is_aborted(struct sched_request *req)
{
	assert_true(false);
	return true;
}

void
sched_req_wait(struct sched_request *req, bool abort)
{
	assert_true(false);
}
