/* Copyright (C) 2017-2019 Intel Corporation
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

#include "iof_common.h"
#include "ioc.h"
#include "log.h"

#define POOL_NAME close_pool
#define TYPE_NAME common_req
#define REQ_NAME request
#define STAT_KEY release
#include "ioc_ops.h"

/* Find a GAH from a inode, return 0 if found */
int
find_gah(struct iof_projection_info *fs_handle, ino_t ino, struct ios_gah *gah)
{
	struct ioc_inode_entry *ie;
	d_list_t *rlink;

	if (ino == 1) {
		D_MUTEX_LOCK(&fs_handle->gah_lock);
		*gah = fs_handle->gah;
		D_MUTEX_UNLOCK(&fs_handle->gah_lock);
		return 0;
	}

	rlink = d_hash_rec_find(&fs_handle->inode_ht, &ino, sizeof(ino));
	if (!rlink)
		return ENOENT;

	ie = container_of(rlink, struct ioc_inode_entry, ie_htl);

	IOF_TRACE_INFO(ie, "Inode %lu " GAH_PRINT_STR, ie->stat.st_ino,
		       GAH_PRINT_VAL(ie->gah));

	if (!H_GAH_IS_VALID(ie)) {
		d_hash_rec_decref(&fs_handle->inode_ht, rlink);
		return EHOSTDOWN;
	}

	D_MUTEX_LOCK(&fs_handle->gah_lock);
	*gah = ie->gah;
	D_MUTEX_UNLOCK(&fs_handle->gah_lock);

	/* Once the GAH has been copied drop the reference on the parent inode
	 */
	d_hash_rec_decref(&fs_handle->inode_ht, rlink);
	return 0;
}

int
find_inode(struct ioc_request *request)
{
	struct iof_projection_info *fs_handle = request->fsh;
	struct ioc_inode_entry *ie;
	d_list_t *rlink;

	rlink = d_hash_rec_find(&fs_handle->inode_ht,
				&request->ir_inode_num,
				sizeof(request->ir_inode_num));
	if (!rlink)
		return ENOENT;

	ie = container_of(rlink, struct ioc_inode_entry, ie_htl);

	if (!H_GAH_IS_VALID(ie)) {
		d_hash_rec_decref(&fs_handle->inode_ht, rlink);
		return EHOSTDOWN;
	}

	IOF_TRACE_INFO(ie, "Using inode %lu " GAH_PRINT_STR " parent %lu",
		       ie->stat.st_ino, GAH_PRINT_VAL(ie->gah), ie->parent);

	request->ir_inode = ie;
	return 0;
}

/* Drop a reference on the GAH in the hash table
 *
 * TODO: Merge this with ioc_forget_one()
 *
 */
static void
drop_ino_ref(struct iof_projection_info *fs_handle, ino_t ino)
{
	d_list_t *rlink;

	if (ino == 1)
		return;

	if (ino == 0)
		return;

	rlink = d_hash_rec_find(&fs_handle->inode_ht, &ino, sizeof(ino));

	if (!rlink) {
		IOF_TRACE_WARNING(fs_handle, "Could not find entry %lu", ino);
		return;
	}
	d_hash_rec_ndecref(&fs_handle->inode_ht, 2, rlink);
}

static bool
ie_close_cb(struct ioc_request *request)
{
	struct TYPE_NAME	*desc = CONTAINER(request);

	IOF_TRACE_DOWN(request);
	iof_pool_release(desc->request.fsh->close_pool, desc);
	return false;
}

static const struct ioc_request_api api = {
	.on_result	= ie_close_cb,
};

void ie_close(struct iof_projection_info *fs_handle, struct ioc_inode_entry *ie)
{
	struct TYPE_NAME	*desc = NULL;
	struct iof_gah_in	*in;
	struct iof_file_handle *fh;
	struct ioc_inode_entry *iec;
	int			rc;
	int			ref = atomic_load_consume(&ie->ie_ref);

	IOF_TRACE_DEBUG(ie, "closing, ref %u, parent %lu", ref, ie->parent);

	D_ASSERT(ref == 0);
	atomic_fetch_add(&ie->ie_ref, 1);

	D_MUTEX_LOCK(&fs_handle->of_lock);
	/* Check that all files opened for this inode have been released */
	while ((fh = d_list_pop_entry(&ie->ie_fh_list,
				      struct iof_file_handle,
				      fh_ino_list))) {
		IOF_TRACE_WARNING(ie, "open file %p", fh);
	}
	D_MUTEX_UNLOCK(&fs_handle->of_lock);

	D_MUTEX_LOCK(&fs_handle->gah_lock);
	while ((iec = d_list_pop_entry(&ie->ie_ie_children,
				       struct ioc_inode_entry,
				       ie_ie_list))) {
		IOF_TRACE_WARNING(ie, "child inode %p", iec);
	}

	d_list_del_init(&ie->ie_ie_list);
	D_MUTEX_UNLOCK(&fs_handle->gah_lock);

	drop_ino_ref(fs_handle, ie->parent);

	if (FS_IS_OFFLINE(fs_handle))
		D_GOTO(err, rc = fs_handle->offline_reason);

	if (!H_GAH_IS_VALID(ie))
		D_GOTO(out, 0);

	if (ie->gah.root != atomic_load_consume(&fs_handle->proj.grp->pri_srv_rank)) {
		IOF_TRACE_WARNING(ie,
				  "Gah with old root %lu " GAH_PRINT_STR,
				  ie->stat.st_ino, GAH_PRINT_VAL(ie->gah));
		D_GOTO(out, 0);
	}

	IOF_TRACE_INFO(ie, GAH_PRINT_STR, GAH_PRINT_VAL(ie->gah));

	IOC_REQ_INIT(desc, fs_handle, api, in, rc);
	if (rc)
		D_GOTO(err, 0);

	IOF_TRACE_UP(&desc->request, ie, "close_req");

	D_MUTEX_LOCK(&fs_handle->gah_lock);
	in->gah = ie->gah;
	D_MUTEX_UNLOCK(&fs_handle->gah_lock);

	rc = iof_fs_send(&desc->request);
	if (rc != 0)
		D_GOTO(err, 0);

	IOF_TRACE_DOWN(ie);
	return;

err:
	IOF_TRACE_ERROR(ie, "Failed to close " GAH_PRINT_STR " %d",
			GAH_PRINT_VAL(ie->gah), rc);
out:
	IOF_TRACE_DOWN(ie);
	if (desc)
		iof_pool_release(fs_handle->close_pool, desc);
}
