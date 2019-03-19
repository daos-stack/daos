/* Copyright (C) 2017-2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "iof_bulk.h"
#include "log.h"

bool iof_bulk_alloc(crt_context_t ctx, void *ptr, off_t bulk_offset, size_t len,
		    bool read_only)
{
	struct iof_local_bulk *bulk = (ptr + bulk_offset);
	d_sg_list_t sgl = {0};
	d_iov_t iov = {0};
	int flags = CRT_BULK_RW;
	int rc;

	bulk->buf = mmap(NULL, len, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (bulk->buf == MAP_FAILED) {
		bulk->buf = NULL;
		IOF_TRACE_ERROR(ptr, "mmap failed: %s", strerror(errno));
		return false;
	}

	iov.iov_len = len;
	iov.iov_buf = bulk->buf;
	iov.iov_buf_len = len;
	sgl.sg_iovs = &iov;
	sgl.sg_nr = 1;

	if (read_only)
		flags = CRT_BULK_RO;

	rc = crt_bulk_create(ctx, &sgl, flags, &bulk->handle);
	if (rc) {
		rc = munmap(bulk->buf, len);
		if (rc == -1)
			IOF_TRACE_DEBUG(ptr, "munmap failed: %p: %s", bulk->buf,
					strerror(errno));
		bulk->buf = NULL;
		bulk->handle = NULL;
		return false;
	}
	bulk->len = len;

	IOF_TRACE_DEBUG(ptr, "mapped bulk range: %p-%p", bulk->buf,
			bulk->buf + len - 1);

	return true;
}

static void bulk_free_helper(void *ptr, struct iof_local_bulk *bulk)
{
	void *addr;
	int rc;

	rc = crt_bulk_free(bulk->handle);

	if (rc != 0) {
		/* Something is messed up with the handle.   Leak the virtual
		 * memory space here but disallow access to it.   Using mmap
		 * should cause the network driver to disallow access.  If it
		 * crashes due to an access to this memory region, then it
		 * indicates a bug in the stack.
		 */
		IOF_TRACE_DEBUG(ptr, "Bulk free failed, remapping: %p, rc = %d",
				bulk->buf, rc);
		addr = mmap(bulk->buf, bulk->len, PROT_NONE,
			    MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (addr == MAP_FAILED)
			IOF_TRACE_ERROR(ptr, "remap failed: %p: %s", bulk->buf,
					strerror(errno));
		return;
	}
	IOF_TRACE_DEBUG(ptr, "unmapped bulk %p", bulk->buf);
	rc = munmap(bulk->buf, bulk->len);
	if (rc == -1)
		IOF_TRACE_DEBUG(ptr, "munmap failed: %p: %s", bulk->buf,
				strerror(errno));
}

void iof_bulk_free(void *ptr, off_t bulk_offset)
{
	struct iof_local_bulk *bulk = (ptr + bulk_offset);

	bulk_free_helper(ptr, bulk);

	bulk->handle = NULL;
	bulk->buf = NULL;
	bulk->len = 0;
}

