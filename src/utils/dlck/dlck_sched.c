/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <abt.h>
#include <stdbool.h>
#include <assert.h>

struct sched_request;
struct sched_req_attr;

void
sched_req_wakeup(struct sched_request *req)
{
	assert(false);
}

void
sched_req_abort(struct sched_request *req)
{
	assert(false);
}

struct sched_request *
sched_req_get(struct sched_req_attr *attr, ABT_thread ult)
{
	assert(false);
	return NULL;
}

struct sched_request *
sched_create_ult(struct sched_req_attr *attr, void (*func)(void *), void *arg, size_t stack_size)
{
	assert(false);
	return NULL;
}

void
sched_req_sleep(struct sched_request *req, uint32_t msecs)
{
	assert(false);
}

void
sched_req_put(struct sched_request *req)
{
	assert(false);
}

bool
sched_req_is_aborted(struct sched_request *req)
{
	assert(false);
	return true;
}

void
sched_req_wait(struct sched_request *req, bool abort)
{
	assert(false);
}

uint64_t
sched_cur_seq(void)
{
	return 0; /** standalone */
}
