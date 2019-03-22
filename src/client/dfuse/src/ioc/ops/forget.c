/* Copyright (C) 2018 Intel Corporation
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

#include <fuse3/fuse.h>
#include "iof_common.h"
#include "ioc.h"
#include "log.h"

static void
ioc_forget_one(struct iof_projection_info *fs_handle,
	       fuse_ino_t ino, uintptr_t nlookup)
{
	d_list_t *rlink;
	int rc;

	/* One additional reference is needed because the rec_find() itself
	 * acquires one
	 */
	nlookup++;

	rlink = d_hash_rec_find(&fs_handle->inode_ht, &ino, sizeof(ino));
	if (!rlink) {
		IOF_TRACE_WARNING(fs_handle, "Unable to find ref for %lu %lu",
				  ino, nlookup);
		return;
	}

	IOF_TRACE_INFO(container_of(rlink, struct ioc_inode_entry, ie_htl),
		       "ino %lu count %lu",
		       ino, nlookup);

	rc = d_hash_rec_ndecref(&fs_handle->inode_ht, nlookup, rlink);
	if (rc != -DER_SUCCESS) {
		IOF_TRACE_ERROR(fs_handle, "Invalid refcount %lu on %p",
				nlookup,
				container_of(rlink, struct ioc_inode_entry, ie_htl));
	}
}

void
ioc_ll_forget(fuse_req_t req, fuse_ino_t ino, uintptr_t nlookup)
{
	struct iof_projection_info *fs_handle = fuse_req_userdata(req);

	STAT_ADD(fs_handle->stats, forget);

	fuse_reply_none(req);

	ioc_forget_one(fs_handle, ino, nlookup);
}

void
ioc_ll_forget_multi(fuse_req_t req, size_t count,
		    struct fuse_forget_data *forgets)
{
	struct iof_projection_info *fs_handle = fuse_req_userdata(req);
	int i;

	STAT_ADD(fs_handle->stats, forget);

	fuse_reply_none(req);

	IOF_TRACE_INFO(fs_handle, "Forgetting %zi", count);

	for (i = 0; i < count; i++)
		ioc_forget_one(fs_handle, forgets[i].ino, forgets[i].nlookup);
}
