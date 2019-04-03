/**
 * (C) Copyright 2017-2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "dfuse_log.h"
#include "dfuse_bulk.h"

bool
dfuse_bulk_alloc(crt_context_t ctx, void *ptr, off_t bulk_offset, size_t len,
	       bool read_only)
{
	struct dfuse_local_bulk *bulk = (ptr + bulk_offset);
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

static void
bulk_free_helper(void *ptr, struct dfuse_local_bulk *bulk)
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

void
dfuse_bulk_free(void *ptr, off_t bulk_offset)
{
	struct dfuse_local_bulk *bulk = (ptr + bulk_offset);

	bulk_free_helper(ptr, bulk);

	bulk->handle = NULL;
	bulk->buf = NULL;
	bulk->len = 0;
}

