/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf_io.h"
#include "ocf/ocf_core.h"

struct ocf_io *ocf_core_new_io_wrapper(ocf_core_t core, ocf_queue_t queue,
		uint64_t addr, uint32_t bytes, uint32_t dir,
		uint32_t io_class, uint64_t flags)
{
	return ocf_core_new_io(core, queue, addr, bytes, dir, io_class, flags);
}

void ocf_io_set_cmpl_wrapper(struct ocf_io *io, void *context,
		void *context2, ocf_end_io_t fn)
{
	ocf_io_set_cmpl(io, context, context2, fn);
}

void ocf_io_set_start_wrapper(struct ocf_io *io, ocf_start_io_t fn)
{
	ocf_io_set_start(io, fn);
}

void ocf_io_set_handle_wrapper(struct ocf_io *io, ocf_handle_io_t fn)
{
	ocf_io_set_handle(io, fn);
}

void ocf_core_submit_io_wrapper(struct ocf_io *io)
{
	ocf_core_submit_io(io);
}

