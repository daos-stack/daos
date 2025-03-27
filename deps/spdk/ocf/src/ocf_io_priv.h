/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __OCF_IO_PRIV_H__
#define __OCF_IO_PRIV_H__

#include "ocf/ocf.h"
#include "utils/utils_io_allocator.h"

struct ocf_io_meta {
	ocf_volume_t volume;
	const struct ocf_io_ops *ops;
	env_atomic ref_count;
	struct ocf_request *req;
};


struct ocf_io_internal {
	struct ocf_io_meta meta;
	struct ocf_io io;
};

int ocf_io_allocator_init(ocf_io_allocator_t allocator, ocf_io_allocator_type_t type,
		uint32_t priv_size, const char *name);


struct ocf_io *ocf_io_new(ocf_volume_t volume, ocf_queue_t queue,
		uint64_t addr, uint32_t bytes, uint32_t dir,
		uint32_t io_class, uint64_t flags);

static inline void ocf_io_start(struct ocf_io *io)
{
	/*
	 * We want to call start() callback only once, so after calling
	 * we set it to NULL to prevent multiple calls.
	 */
	if (io->start) {
		io->start(io);
		io->start = NULL;
	}
}

static inline void ocf_io_end(struct ocf_io *io, int error)
{
	if (io->end)
		io->end(io, error);

}

#endif /* __OCF_IO_PRIV_H__ */
