/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf_env.h"
#include <sched.h>
#include <execinfo.h>

/* ALLOCATOR */
struct _env_allocator {
	/*!< Memory pool ID unique name */
	char *name;

	/*!< Size of specific item of memory pool */
	uint32_t item_size;

	/*!< Number of currently allocated items in pool */
	env_atomic count;

	/*!< Should buffer be zeroed while allocating */
	bool zero;
};

static inline size_t env_allocator_align(size_t size)
{
	if (size <= 2)
		return size;
	return (1ULL << 32) >> __builtin_clz(size - 1);
}

struct _env_allocator_item {
	uint32_t flags;
	uint32_t cpu;
	char data[];
};

void *env_allocator_new(env_allocator *allocator)
{
	struct _env_allocator_item *item = NULL;

	item = malloc(allocator->item_size);

	if (!item) {
		return NULL;
	}

	if (allocator->zero) {
		memset(item, 0, allocator->item_size);
	}

	item->cpu = 0;
	item->flags = 0;
	env_atomic_inc(&allocator->count);

	return &item->data;
}

env_allocator *env_allocator_create(uint32_t size, const char *name, bool zero)
{
	int error = -1;

	env_allocator *allocator = calloc(1, sizeof(*allocator));
	if (!allocator) {
		error = __LINE__;
		goto err;
	}

	allocator->item_size = size + sizeof(struct _env_allocator_item);
	allocator->zero = zero;

	allocator->name = strdup(name);

	if (!allocator->name) {
		error = __LINE__;
		goto err;
	}

	return allocator;

err:
	printf("Cannot create memory allocator, ERROR %d", error);
	env_allocator_destroy(allocator);

	return NULL;
}

void env_allocator_del(env_allocator *allocator, void *obj)
{
	struct _env_allocator_item *item =
		container_of(obj, struct _env_allocator_item, data);

	env_atomic_dec(&allocator->count);

	free(item);
}

void env_allocator_destroy(env_allocator *allocator)
{
	if (allocator) {
		if (env_atomic_read(&allocator->count)) {
			printf("Not all objects deallocated\n");
			ENV_WARN(true, OCF_PREFIX_SHORT" Cleanup problem\n");
		}

		free(allocator->name);
		free(allocator);
	}
}

/* DEBUGING */
#define ENV_TRACE_DEPTH	16

void env_stack_trace(void)
{
	void *trace[ENV_TRACE_DEPTH];
	char **messages = NULL;
	int i, size;

	size = backtrace(trace, ENV_TRACE_DEPTH);
	messages = backtrace_symbols(trace, size);
	printf("[stack trace]>>>\n");
	for (i = 0; i < size; ++i)
		printf("%s\n", messages[i]);
	printf("<<<[stack trace]\n");
	free(messages);
}

/* CRC */
uint32_t env_crc32(uint32_t crc, uint8_t const *data, size_t len)
{
	return crc32(crc, data, len);
}

/* EXECUTION CONTEXTS */
pthread_mutex_t *exec_context_mutex;

static void __attribute__((constructor)) init_execution_context(void)
{
	unsigned count = env_get_execution_context_count();
	unsigned i;

	ENV_BUG_ON(count == 0);
	exec_context_mutex = malloc(count * sizeof(exec_context_mutex[0]));
	ENV_BUG_ON(exec_context_mutex == NULL);
	for (i = 0; i < count; i++)
		ENV_BUG_ON(pthread_mutex_init(&exec_context_mutex[i], NULL));
}

static void __attribute__((destructor)) deinit_execution_context(void)
{
	unsigned count = env_get_execution_context_count();
	unsigned i;

	ENV_BUG_ON(count == 0);
	ENV_BUG_ON(exec_context_mutex == NULL);

	for (i = 0; i < count; i++)
		ENV_BUG_ON(pthread_mutex_destroy(&exec_context_mutex[i]));
	free(exec_context_mutex);
}

/* get_execuction_context must assure that after the call finishes, the caller
 * will not get preempted from current execution context. For userspace env
 * we simulate this behavior by acquiring per execution context mutex. As a
 * result the caller might actually get preempted, but no other thread will
 * execute in this context by the time the caller puts current execution ctx. */
unsigned env_get_execution_context(void)
{
	unsigned cpu;

	cpu = sched_getcpu();
	cpu = (cpu == -1) ?  0 : cpu;

	ENV_BUG_ON(pthread_mutex_lock(&exec_context_mutex[cpu]));

	return cpu;
}

void env_put_execution_context(unsigned ctx)
{
	pthread_mutex_unlock(&exec_context_mutex[ctx]);
}

unsigned env_get_execution_context_count(void)
{
	int num = sysconf(_SC_NPROCESSORS_ONLN);

	return (num == -1) ? 0 : num;
}
