/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __UTILS_IO_ALLOCATOR_H__
#define __UTILS_IO_ALLOCATOR_H__

#include "ocf/ocf_types.h"

typedef struct ocf_io_allocator *ocf_io_allocator_t;

struct ocf_io_allocator_ops {
	int (*allocator_init)(ocf_io_allocator_t allocator,
			uint32_t priv_size, const char *name);
	void (*allocator_deinit)(ocf_io_allocator_t allocator);
	void *(*allocator_new)(ocf_io_allocator_t allocator,
			ocf_volume_t volume, ocf_queue_t queue,
			uint64_t addr, uint32_t bytes, uint32_t dir);
	void (*allocator_del)(ocf_io_allocator_t allocator, void *obj);
};

struct ocf_io_allocator_type {
	struct ocf_io_allocator_ops ops;
};

typedef const struct ocf_io_allocator_type *ocf_io_allocator_type_t;

struct ocf_io_allocator {
	const struct ocf_io_allocator_type *type;
	void *priv;
};

static inline void *ocf_io_allocator_new(ocf_io_allocator_t allocator,
		ocf_volume_t volume, ocf_queue_t queue,
		uint64_t addr, uint32_t bytes, uint32_t dir)
{
	return allocator->type->ops.allocator_new(allocator, volume, queue,
			addr, bytes, dir);
}

static inline void ocf_io_allocator_del(ocf_io_allocator_t allocator, void *obj)
{
	allocator->type->ops.allocator_del(allocator, obj);
}

static inline int ocf_io_allocator_init(ocf_io_allocator_t allocator,
		ocf_io_allocator_type_t type, uint32_t size, const char *name)

{
	allocator->type = type;
	return allocator->type->ops.allocator_init(allocator, size, name);
}

static inline void ocf_io_allocator_deinit(ocf_io_allocator_t allocator)
{
	allocator->type->ops.allocator_deinit(allocator);
}

ocf_io_allocator_type_t ocf_io_allocator_get_type_default(void);

#endif /* __UTILS_IO_ALLOCATOR__ */
