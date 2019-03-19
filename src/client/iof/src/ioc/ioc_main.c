/* Copyright (C) 2016-2019 Intel Corporation
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
#include "iof_ioctl.h"
#include "iof_pool.h"

int
iof_fs_resend(struct ioc_request *request);

struct query_cb_r {
	struct iof_tracker tracker;
	int err;
};

bool
ioc_gen_cb(struct ioc_request *request)
{
	struct iof_status_out *out = crt_reply_get(request->rpc);

	IOC_REQUEST_RESOLVE(request, out);
	if (request->rc) {
		IOC_REPLY_ERR(request, request->rc);
		D_GOTO(out, 0);
	}

	IOC_REPLY_ZERO(request);

out:
	/* Clean up the two refs this code holds on the rpc */
	crt_req_decref(request->rpc);
	crt_req_decref(request->rpc);

	D_FREE(request);
	return false;
}

int ioc_simple_resend(struct ioc_request *request)
{
	struct iof_projection_info *fs_handle = request->fsh;
	crt_rpc_t *resend_rpc;
	int rc;

	IOF_TRACE_INFO(fs_handle,
		       "Performing simple resend of %p", request);

	request->ir_rs = RS_RESET;
	request->rc = 0;

	rc = crt_req_create(request->rpc->cr_ctx, NULL,
			    request->rpc->cr_opc, &resend_rpc);
	if (rc) {
		/* TODO: Handle this case better, possibly by calling a
		 * request callback
		 */
		IOF_TRACE_ERROR(request, "Failed to create retry RPC");
		return EIO;
	}
	memcpy(resend_rpc->cr_input,
	       request->rpc->cr_input,
	       request->rpc->cr_input_size);
	/* Clean up old RPC */
	crt_req_decref(request->rpc);
	crt_req_decref(request->rpc);
	request->rpc = resend_rpc;
	/* Second addref is called in iof_fs_resend */
	crt_req_addref(request->rpc);
	return iof_fs_resend(request);
}

static void ih_addref(struct d_hash_table *htable, d_list_t *rlink);

/*
 * inode_check() callback.  Called for every open inode as part of failover.
 *
 * If the inode is not to be used for failover add it to the inval list for
 * later processing, and take a reference.  As this is called by the hash
 * table traverse function we have to call ih_addref() directly here rather
 * than c_hash_rec_addref() to avoid deadlock.
 */
static int
inode_check_cb(d_list_t *rlink, void *arg)
{
	struct iof_projection_info *fs_handle = arg;
	struct ioc_inode_entry *ie = container_of(rlink,
						  struct ioc_inode_entry,
						  ie_htl);

	IOF_TRACE_INFO(ie,
		       "check inode %lu parent %lu failover %s",
		       ie->stat.st_ino, ie->parent, ie->failover ? "yes" : "no");

	if (ie->failover)
		return -DER_SUCCESS;

	H_GAH_SET_INVALID(ie);

	d_list_add(&ie->ie_ie_list, &fs_handle->p_inval_list);

	ih_addref(NULL, rlink);

	return -DER_SUCCESS;
}

/* Helper function for mark_fh_inode and mark_dh_inode.
 *
 * Walk the filesystem hierarchy upwards from inode until either
 * a inode already marked as failover is found, or to the root,
 * marking all inodes as required for failover.
 */
static void mark_inode_tree(struct iof_projection_info *fs_handle,
			    struct ioc_inode_entry *ie)
{
	struct ioc_inode_entry *iep;
	d_list_t *rlink;

	while (ie->parent != 1) {
		IOF_TRACE_DEBUG(fs_handle,
				"Looking up %lu", ie->parent);
		rlink = d_hash_rec_find(&fs_handle->inode_ht,
					&ie->parent, sizeof(ie->parent));
		if (!rlink) {
			IOF_TRACE_WARNING(fs_handle,
					  "Unable to find inode %lu",
					  ie->parent);
			return;
		}

		iep = container_of(rlink, struct ioc_inode_entry, ie_htl);

		IOF_TRACE_DEBUG(fs_handle,
				"Found %p for %lu %d",
				iep, ie->stat.st_ino, iep->failover);

		d_list_add(&ie->ie_ie_list, &iep->ie_ie_children);
		if (iep->failover) {
			d_hash_rec_decref(&fs_handle->inode_ht, rlink);
			return;
		}

		iep->failover = true;
		ie = iep;
		/* Remove the reference added by rec_find */
		d_hash_rec_decref(&fs_handle->inode_ht, rlink);
	}

	IOF_TRACE_INFO(ie,
		       "Child of root %lu %lu",
		       ie->stat.st_ino, ie->parent);
	d_list_add(&ie->ie_ie_list, &fs_handle->p_ie_children);

}

/* Process open file handle for failover.
 *
 * Identify inode entry for file, add file to inode entry list,
 * and walk inode tree marking all entries for failover.
 */
static void mark_fh_inode(struct iof_file_handle *fh)
{
	struct ioc_inode_entry *ie;
	d_list_t *rlink;

	rlink = d_hash_rec_find(&fh->open_req.fsh->inode_ht,
				&fh->inode_num, sizeof(fh->inode_num));

	if (!rlink) {
		IOF_TRACE_WARNING(fh,
				  "Unable to find inode %lu", fh->inode_num);
		return;
	}
	ie = container_of(rlink, struct ioc_inode_entry, ie_htl);

	d_list_add(&fh->fh_ino_list, &ie->ie_fh_list);
	ie->failover = true;

	mark_inode_tree(fh->open_req.fsh, ie);
	/* Drop the reference taken by rec_find() */
	d_hash_rec_decref(&fh->open_req.fsh->inode_ht, rlink);
}

/* Process open directory handle for failover.
 *
 * Identify inode entry for directory.
 * and walk inode tree marking all entries for failover.
 */
static void mark_dh_inode(struct iof_dir_handle *dh)
{
	struct ioc_inode_entry *ie;
	d_list_t *rlink;

	rlink = d_hash_rec_find(&dh->open_req.fsh->inode_ht,
				&dh->inode_num, sizeof(dh->inode_num));

	if (!rlink) {
		IOF_TRACE_WARNING(dh->open_req.fsh,
				  "Unable to find inode %lu", dh->inode_num);
		return;
	}
	ie = container_of(rlink, struct ioc_inode_entry, ie_htl);

	ie->failover = true;

	mark_inode_tree(dh->open_req.fsh, ie);
	/* Drop the reference taken by rec_find() */
	d_hash_rec_decref(&dh->open_req.fsh->inode_ht, rlink);
}

/* Add a reference to the GAH counter */
static void gah_addref(struct iof_projection_info *fs_handle)
{
	int oldref;

	oldref = atomic_fetch_add(&fs_handle->p_gah_update_count, 1);

	IOF_TRACE_DEBUG(fs_handle, "addref to %u", oldref + 1);
}

/* Safely call the on_result callback for a request
 * Note that the on_request() callback may free request so take a copy
 * of ir_ht and ir_inode before invoking the callback, so the inode
 * reference can be dropped without accessing request.
 */
static void
request_on_result(struct ioc_request *request)
{
	struct iof_projection_info	*fsh = request->fsh;
	struct ioc_inode_entry		*ir_inode = NULL;
	bool				keep_ref;

	if (request->ir_ht == RHS_INODE) {
		ir_inode = request->ir_inode;
	}

	keep_ref = request->ir_api->on_result(request);

	if (ir_inode && !keep_ref) {
		d_hash_rec_decref(&fsh->inode_ht, &ir_inode->ie_htl);
	}
}

/* Remove a reference to the GAH counter, and if it drops to zero
 * then complete the failover activities
 */
static void gah_decref(struct iof_projection_info *fs_handle)
{
	struct ioc_request *request;
	struct ioc_inode_entry *ie;
	int oldref;

	oldref = atomic_fetch_sub(&fs_handle->p_gah_update_count, 1);
	IOF_TRACE_DEBUG(fs_handle, "decref to %u", oldref - 1);

	if (oldref != 1) {
		return;
	}

	IOF_TRACE_INFO(fs_handle,
		       "GAH migration complete, marking as on-line");

	D_MUTEX_UNLOCK(&fs_handle->gah_lock);
	fs_handle->failover_state = iof_failover_complete;

	/* Now the gah_lock has been dropped, and fuse requests are
	 * being processed again it's safe to start invalidating
	 * inodes, so walk the inval list doing so.
	 * This triggers a number of forget() callbacks from the kernel
	 * so only call inval if the reference count > 1 to avoid
	 * activity on already deleted inodes.
	 */
	while ((ie = d_list_pop_entry(&fs_handle->p_inval_list,
				      struct ioc_inode_entry,
				      ie_ie_list))) {
		int ref = atomic_load_consume(&ie->ie_ref);
		int drop_count = 1;

		IOF_TRACE_INFO(ie,
			       "Invalidating " GAH_PRINT_STR " ref %d",
			       GAH_PRINT_VAL(ie->gah), ref);

		if (ref > 1) {
			int rc;

			rc = fuse_lowlevel_notify_inval_entry(fs_handle->session,
							      ie->parent,
							      ie->name,
							      strlen(ie->name));

			IOF_TRACE_INFO(ie, "inval returned %d", rc);
			if (rc == -ENOENT) {
				drop_count += ref - 1;
			}

		}
		d_hash_rec_ndecref(&fs_handle->inode_ht,
				   drop_count,
				   &ie->ie_htl);

	}

	/* Finally, start processing requests which need resending to
	 * new ranks
	 */
	D_MUTEX_LOCK(&fs_handle->p_request_lock);
	while ((request = d_list_pop_entry(&fs_handle->p_requests_pending,
					   struct ioc_request,
					   ir_list))) {
		int rc;

		rc = ioc_simple_resend(request);
		if (rc != 0) {
			request->rc = rc;
			request_on_result(request);
		}
	}
	D_MUTEX_UNLOCK(&fs_handle->p_request_lock);
	IOF_TRACE_INFO(fs_handle, "Failover complete");
}

static void imigrate_cb(const struct crt_cb_info *cb_info);

static void imigrate_send(struct iof_projection_info *fs_handle,
			  struct ioc_inode_entry *ie,
			  struct ioc_inode_entry *iep)
{
	crt_rpc_t *rpc = NULL;
	struct iof_imigrate_in *in;
	struct ioc_inode_migrate *im;
	struct ioc_inode_entry *iec;
	crt_endpoint_t ep;
	d_rank_t rank;
	int rc;

	if (!ie->failover) {
		IOF_TRACE_INFO(ie, "Not marked for failover, skipping");
		return;
	}

	rank = atomic_load_consume(&fs_handle->proj.grp->pri_srv_rank);

	ep.ep_tag = 0;
	ep.ep_rank = rank;
	ep.ep_grp = fs_handle->proj.grp->dest_grp;

	IOF_TRACE_INFO(ie, "child inode %p %lu %lu",
		       ie, ie->stat.st_ino, ie->parent);

	D_ALLOC_PTR(im);
	if (!im)
		D_GOTO(traverse, 0);

	rc = crt_req_create(fs_handle->proj.crt_ctx, &ep,
			    FS_TO_OP(fs_handle, imigrate), &rpc);
	if (rc != -DER_SUCCESS || rpc == NULL) {
		IOF_TRACE_ERROR(fs_handle, "Failed to allocate RPC");
		D_FREE(im);
		D_GOTO(traverse, 0);
	}

	im->im_ie = ie;
	im->im_fsh = fs_handle;
	in = crt_req_get(rpc);
	if (iep) {
		/* If there is a parent and it is valid then try and load
		 * from that, if it is not valid they try anyway using the
		 * root as there's a chance the inode will be open anyway
		 * but do not send the filename in this case.
		 */
		if (H_GAH_IS_VALID(iep)) {
			in->gah = iep->gah;
			strncpy(in->name.name, ie->name, NAME_MAX);
		} else {
			in->gah = fs_handle->gah;
		}
	} else {
		in->gah = fs_handle->gah;
		strncpy(in->name.name, ie->name, NAME_MAX);
	}
	in->inode = ie->stat.st_ino;
	gah_addref(fs_handle);
	rc = crt_req_send(rpc, imigrate_cb, im);
	if (rc != 0) {
		IOF_TRACE_ERROR(fs_handle, "Failed to send RPC");
		D_FREE(im);
		gah_decref(fs_handle);
		D_GOTO(traverse, 0);
	}
	return;

traverse:
	d_list_for_each_entry(iec, &ie->ie_ie_children, ie_ie_list)
		imigrate_send(fs_handle, iec, ie);
}

/* Callback for inode migrate RPC.
 *
 * If the RPC succeeded then update the GAH for the inode, else log an error.
 *
 * TODO: ADD gah_ok to inode handles.
 */
static void imigrate_cb(const struct crt_cb_info *cb_info)
{
	struct ioc_inode_migrate *im = cb_info->cci_arg;
	struct iof_entry_out	*out = crt_reply_get(cb_info->cci_rpc);
	struct ioc_inode_entry *iec;

	IOF_TRACE_INFO(im->im_ie, "reply %d '%s' %d -%s",
		       out->rc, strerror(out->rc),
		       out->err, d_errstr(out->err));

	if (cb_info->cci_rc != -DER_SUCCESS) {
		IOF_TRACE_WARNING(im->im_ie,
				  "RPC failure %d, inode %lu going offline",
				  cb_info->cci_rc, im->im_ie->stat.st_ino);
		H_GAH_SET_INVALID(im->im_ie);
		goto out;
	}

	if (out->rc != 0 || out->err != -DER_SUCCESS) {
		IOF_TRACE_WARNING(im->im_ie,
				  "inode %lu going offline %d %d",
				  im->im_ie->stat.st_ino,
				  out->rc, out->err);
		H_GAH_SET_INVALID(im->im_ie);
		goto out;
	}

	IOF_TRACE_INFO(im->im_ie, GAH_PRINT_STR " -> " GAH_PRINT_STR,
		       GAH_PRINT_VAL(im->im_ie->gah), GAH_PRINT_VAL(out->gah));
	im->im_ie->gah = out->gah;

out:
	d_list_for_each_entry(iec, &im->im_ie->ie_ie_children, ie_ie_list)
		imigrate_send(im->im_fsh, iec, im->im_ie);

	gah_decref(im->im_fsh);
	D_FREE(im);
}

/* Update projection to identify inodes which relate to open files.
 */
static void inode_check(struct iof_projection_info *fs_handle)
{
	struct ioc_inode_entry *ie;
	struct iof_file_handle *fh;
	struct iof_dir_handle *dh;
	int rc;

	IOF_TRACE_INFO(fs_handle,
		       "Migrating open files");

	D_MUTEX_LOCK(&fs_handle->of_lock);
	d_list_for_each_entry(fh, &fs_handle->openfile_list, fh_of_list) {
		IOF_TRACE_INFO(fs_handle,
			       "Inspecting file " GAH_PRINT_STR " %lu %p",
			       GAH_PRINT_VAL(fh->common.gah), fh->inode_num,
			       fh->ie);
		mark_fh_inode(fh);
	}
	D_MUTEX_UNLOCK(&fs_handle->of_lock);
	D_MUTEX_LOCK(&fs_handle->od_lock);
	d_list_for_each_entry(dh, &fs_handle->opendir_list, dh_od_list) {
		IOF_TRACE_INFO(fs_handle,
			       "Inspecting dir " GAH_PRINT_STR " %p",
			       GAH_PRINT_VAL(dh->gah), dh);
		mark_dh_inode(dh);
	}
	D_MUTEX_UNLOCK(&fs_handle->od_lock);

	/* Traverse the entire inode table, and add any not touched by
	 * the above loops to the p_inval_list to be invalidated after
	 * the gah_lock is dropped later
	 */
	rc = d_hash_table_traverse(&fs_handle->inode_ht, inode_check_cb,
				   fs_handle);
	IOF_TRACE_DEBUG(fs_handle,
			"traverse returned %d", rc);

	d_list_for_each_entry(ie, &fs_handle->p_ie_children, ie_ie_list)
		imigrate_send(fs_handle, ie, NULL);
}

/* Helper function to set all projections off-line.
 *
 * This is to be called when something catastrophic happens that means the
 * client cannot continue in any form.
 */
static void
set_all_offline(struct iof_state *iof_state, int reason, bool unlock)
{
	struct iof_projection_info *fs_handle;

	d_list_for_each_entry(fs_handle, &iof_state->fs_list, link) {
		IOF_TRACE_INFO(fs_handle,
			       "Changing offline reason from %d to %d",
			       fs_handle->offline_reason, reason);
		fs_handle->offline_reason = reason;
		if (unlock)
			D_MUTEX_UNLOCK(&fs_handle->gah_lock);
	}
}

/* Callback function for re-register RPC.
 *
 * This is called after failover when the re-register RPC completes.
 *
 * TODO: Pause all on-going filesystem activity after failover until
 * this function is called.  This would require generic_cb() and iof_fs_resend()
 * putting requests on pending lists rather than immediately sending them onto
 * the network, however it will be required to handle the multiple-failure
 * case.
 */
static void
rereg_cb(const struct crt_cb_info *cb_info)
{
	struct iof_state *iof_state = cb_info->cci_arg;
	struct iof_query_out *query = crt_reply_get(cb_info->cci_rpc);
	struct iof_projection_info *fs_handle;
	struct iof_fs_info *fs_info;

	IOF_TRACE_INFO(iof_state, "rc %d", cb_info->cci_rc);

	if (cb_info->cci_rc != -DER_SUCCESS) {
		set_all_offline(iof_state, EHOSTDOWN, true);
		return;
	}

	if (query->info.ca_count != iof_state->num_proj) {
		IOF_TRACE_ERROR(iof_state,
				"Unexpected projection count %ld %d",
				query->info.ca_count, iof_state->num_proj);
		set_all_offline(iof_state, EINVAL, true);
		return;
	}

	fs_info = query->info.ca_arrays;

	d_list_for_each_entry(fs_handle, &iof_state->fs_list, link) {

		IOF_TRACE_DEBUG(fs_handle,
				"Local projection dir is '%s'",
				fs_handle->mnt_dir.name);

		IOF_TRACE_DEBUG(fs_handle,
				"Remote projection dir is '%s'",
				fs_info->dir_name.name);

		if (strncmp(fs_handle->mnt_dir.name,
			    fs_info->dir_name.name,
			    NAME_MAX) != 0) {
			IOF_TRACE_ERROR(fs_handle,
					"Projection directory incorrect");
			fs_handle->offline_reason = EIO;
		}

		atomic_store_release(&fs_handle->p_gah_update_count, 1);
		if (!fs_handle->offline_reason) {

			/* Set the new GAH for the root inode */
			fs_handle->gah = fs_info->gah;

			inode_check(fs_handle);
		}

		gah_decref(fs_handle);
		fs_info++;
	}
}

/* The eviction handler atomically updates the PSR of the group for which
 * this eviction occurred; or disables the group if no more PSRs remain.
 * It then locates all the projections corresponding to the group; if the
 * group was previously disabled, it marks them offline. Else, it migrates all
 * open handles to the new PSR. The PSR update and migration must be completed
 * before the callbacks for individual failed RPCs are invoked, so they may be
 * able to correctly re-target the RPCs and also use valid handles.
 */
static void ioc_eviction_cb(crt_group_t *group, d_rank_t rank, void *arg)
{
	struct iof_state		*iof_state = arg;
	d_rank_t			updated_psr;
	d_rank_list_t			*psr_list = NULL;
	struct iof_group_info		*g = &iof_state->group;
	struct iof_projection_info	*fs_handle;
	crt_rpc_t			*rpc = NULL;
	int				active = 0;
	int crc, rc;

	IOF_TRACE_INFO(iof_state,
		       "Eviction handler, Group: %s; Rank: %u",
		       group->cg_grpid, rank);

	if (strncmp(group->cg_grpid,
		    iof_state->group.grp.dest_grp->cg_grpid,
		    CRT_GROUP_ID_MAX_LEN)) {
		IOF_TRACE_INFO(iof_state,
			       "Group ID wrong %s %s",
			       group->cg_grpid,
			       iof_state->group.grp.dest_grp->cg_grpid);
		return;
	}

	crc = crt_lm_group_psr(group, &psr_list);
	if (crc == -DER_SUCCESS) {
		d_rank_t new_psr = psr_list->rl_ranks[0];
		d_rank_t evicted_psr = rank;

		d_rank_list_free(psr_list);

		atomic_compare_exchange(&g->grp.pri_srv_rank,
					evicted_psr, new_psr);
		updated_psr = atomic_load_consume(&g->grp.pri_srv_rank);
		IOF_TRACE_INFO(iof_state,
			       "Updated: %d, Evicted: %d, New: %d",
			       updated_psr, evicted_psr, new_psr);
		/* TODO: This is needed for FUSE operations which are
		 * not yet using the failover codepath to send RPCs.
		 * This must be removed once all the FUSE ops have been
		 * ported. This code is not thread safe, so a FUSE call
		 * when this is being updated will cause a race condition.
		 */
		g->grp.psr_ep.ep_rank = new_psr;
	} else {
		IOF_TRACE_WARNING(iof_state,
				  "Invalid rank list, ret = %d", crc);
		g->grp.enabled = false;
		if (crc == -DER_NONEXIST)
			rc = EHOSTDOWN;
		else
			rc = EINVAL;

		IOF_TRACE_WARNING(iof_state,
				  "Group %s disabled, rc=%d",
				  group->cg_grpid, rc);
	}

	d_list_for_each_entry(fs_handle, &iof_state->fs_list, link) {
		struct iof_file_handle *fh;
		struct iof_dir_handle *dh;

		D_MUTEX_LOCK(&fs_handle->gah_lock);

		if (fs_handle->proj.grp != &g->grp)
			continue;

		if (fs_handle->offline_reason)
			continue;

		/* Mark all local GAH entries as invalid */

		if (!g->grp.enabled || !IOF_HAS_FAILOVER(fs_handle->flags)) {
			IOF_TRACE_WARNING(fs_handle,
					  "Marking projection %d offline: %s",
					  fs_handle->fs_id,
					  fs_handle->mnt_dir.name);
			if (!g->grp.enabled)
				fs_handle->offline_reason = rc;
			else
				fs_handle->offline_reason = EHOSTDOWN;
			fs_handle->failover_state = iof_failover_offline;
		} else {
			fs_handle->failover_state = iof_failover_in_progress;
			active++;
		}

		D_MUTEX_LOCK(&fs_handle->of_lock);
		d_list_for_each_entry(fh, &fs_handle->openfile_list,
				      fh_of_list) {
			if (fh->common.gah.root != rank)
				continue;
			IOF_TRACE_INFO(fs_handle,
				       "Invalidating file " GAH_PRINT_STR " %p",
				       GAH_PRINT_VAL(fh->common.gah), fh);
			H_GAH_SET_INVALID(fh);
		}
		D_MUTEX_UNLOCK(&fs_handle->of_lock);
		D_MUTEX_LOCK(&fs_handle->od_lock);
		d_list_for_each_entry(dh, &fs_handle->opendir_list, dh_od_list) {
			if (dh->gah.root != rank)
				continue;
			IOF_TRACE_INFO(fs_handle,
				       "Invalidating dir " GAH_PRINT_STR " %p",
				       GAH_PRINT_VAL(dh->gah), dh);
			H_GAH_SET_INVALID(dh);
		}
		D_MUTEX_UNLOCK(&fs_handle->od_lock);
	}

	/* If there are no potentially active projections then do not send the
	 * re-attach RPC at all but just release the lock directly
	 */
	if (!active) {
		d_list_for_each_entry(fs_handle, &iof_state->fs_list, link) {
			D_MUTEX_UNLOCK(&fs_handle->gah_lock);
		}

		return;
	}
	/* Send a RPC to register with the new server.
	 *
	 * Currently this doesn't do much other than help with the shutdown
	 * process, however re-sending of failed RPCs should really be blocked
	 * until the re-register succeeds.
	 *
	 */
	rc = crt_req_create(iof_state->iof_ctx.crt_ctx, &g->grp.psr_ep,
			CRT_PROTO_OPC(iof_state->handshake_proto->cpf_base,
				iof_state->handshake_proto->cpf_ver,
				0),
			&rpc);
	if (rc != -DER_SUCCESS) {
		set_all_offline(iof_state, EHOSTDOWN, true);
		return;
	}

	rc = crt_req_send(rpc, rereg_cb, iof_state);
	if (rc != -DER_SUCCESS)
		set_all_offline(iof_state, EHOSTDOWN, true);
}

/* Check if a remote host is down.  Used in RPC callback to check the cb_info
 * for permanent failure of the remote ep.
 */
#define IOC_HOST_IS_DOWN(CB_INFO) ((CB_INFO->cci_rc == -DER_EVICTED) || \
					(CB_INFO->cci_rc == -DER_OOG))

/* Check if the error is recoverable.  If there is a network problem not
 * not resulting in eviction, or a memory allocation error at either
 * end then retry.
 */
#define IOC_SHOULD_RESEND(CB_INFO) ((CB_INFO->cci_rc == -DER_UNREACH) || \
					(CB_INFO->cci_rc == -DER_NOMEM) || \
					(CB_INFO->cci_rc == -DER_DOS))

/* A generic callback function to handle completion of RPCs sent from FUSE,
 * and replay the RPC to a different end point in case the target has been
 * evicted (denoted by an "Out Of Group" return code). For all other failures
 * and in case of success, it invokes a custom handler (if defined).
 */
static void generic_cb(const struct crt_cb_info *cb_info)
{
	struct ioc_request *request = cb_info->cci_arg;
	struct iof_projection_info *fs_handle = request->fsh;
	int rc;

	D_ASSERT(request->ir_rs == RS_RESET);
	request->ir_rs = RS_LIVE;

	/* No Error */
	if (cb_info->cci_rc == -DER_SUCCESS) {
		IOF_TRACE_DEBUG(request,
				"cci_rc %d -%s",
				cb_info->cci_rc,
				d_errstr(cb_info->cci_rc));
		D_GOTO(done, 0);
	}

	IOF_TRACE_INFO(request, "cci_rc %d -%s",
		       cb_info->cci_rc, d_errstr(cb_info->cci_rc));

	if (fs_handle->offline_reason) {
		IOF_TRACE_ERROR(request, "Projection Offline");
		D_GOTO(done, request->rc = fs_handle->offline_reason);
	} else if (IOC_SHOULD_RESEND(cb_info)) {
		rc = ioc_simple_resend(request);
		if (rc != -DER_SUCCESS)
			D_GOTO(done, request->rc = rc);
		return;
	} else if (!IOC_HOST_IS_DOWN(cb_info)) {
		/* Errors other than evictions */
		D_GOTO(done, request->rc = EIO);
	}

	if (fs_handle->failover_state == iof_failover_in_progress) {
		/* Add to list for deferred execution */
		D_MUTEX_LOCK(&fs_handle->p_request_lock);
		d_list_add_tail(&request->ir_list,
				&fs_handle->p_requests_pending);
		D_MUTEX_UNLOCK(&fs_handle->p_request_lock);
	} else {
		rc = ioc_simple_resend(request);
		if (rc != 0)
			D_GOTO(done, request->rc = rc);
	}
	return;

done:
	request_on_result(request);
}

/*
 * Wrapper function that is called from FUSE to send RPCs. The idea is to
 * decouple the FUSE implementation from the actual sending of RPCs. The
 * FUSE callbacks only need to specify the inputs and outputs for the RPC,
 * without bothering about how RPCs are sent. This function is also intended
 * for abstracting various other features related to RPCs such as fail-over
 * and load balance, at the same time preventing code duplication.
 *
 */
int
iof_fs_send(struct ioc_request *request)
{
	int rc;

	D_ASSERT(request->ir_api->on_result);
	/* If the API has passed in a simple inode number then translate it
	 * to either root, or do a hash table lookup on the inode number.
	 * Keep a reference on the inode open which will be dropped after
	 * a call to on_result().
	 */
	if (request->ir_ht == RHS_INODE_NUM) {

		D_ASSERT(request->ir_api->have_gah);

		if (request->ir_inode_num == 1) {
			request->ir_ht = RHS_ROOT;
		} else {
			rc = find_inode(request);
			if (rc != 0) {
				D_GOTO(err, 0);
			}
			request->ir_ht = RHS_INODE;
		}
	}
	rc = iof_fs_resend(request);
	if (rc) {
		D_GOTO(err, 0);
	}
	return 0;
err:
	IOF_TRACE_ERROR(request, "Could not send rpc, rc = %d", rc);

	return rc;
}

int
iof_fs_resend(struct ioc_request *request)
{
	struct iof_projection_info *fs_handle = request->fsh;
	crt_endpoint_t ep;
	int ret;
	int rc;

	if (request->ir_api->have_gah) {
		void *in = crt_req_get(request->rpc);
		struct ios_gah *gah = in + request->ir_api->gah_offset;

		IOF_TRACE_DEBUG(request,
				"loading gah from %d %p", request->ir_ht,
				request->ir_inode);

		D_MUTEX_LOCK(&request->fsh->gah_lock);

		switch (request->ir_ht) {
		case RHS_ROOT:
			*gah = request->fsh->gah;
			break;
		case RHS_INODE:
			*gah = request->ir_inode->gah;
			break;
		case RHS_FILE:
			*gah = request->ir_file->common.gah;
			break;
		case RHS_DIR:
			*gah = request->ir_dir->gah;
			break;
		default:
			IOF_TRACE_ERROR(request,
					"Invalid request type %d",
					request->ir_ht);
			D_MUTEX_UNLOCK(&request->fsh->gah_lock);
			D_GOTO(err, ret = EIO);
		}

		D_MUTEX_UNLOCK(&request->fsh->gah_lock);
		IOF_TRACE_DEBUG(request, GAH_PRINT_STR, GAH_PRINT_VAL(*gah));
	}

	ep.ep_tag = 0;
	ep.ep_grp = fs_handle->proj.grp->dest_grp;

	/* Pick an appropriate rank, for most cases this is the root of the GAH
	 * however if that is not known then send to the PSR
	 */
	switch (request->ir_ht) {
	case RHS_INODE:
		if (!H_GAH_IS_VALID(request->ir_inode)) {
			D_GOTO(err, ret = EHOSTDOWN);
		}
		ep.ep_rank = request->ir_inode->gah.root;
		break;
	case RHS_FILE:
		if (!F_GAH_IS_VALID(request->ir_file)) {
			D_GOTO(err, ret = EHOSTDOWN);
		}
		ep.ep_rank = request->ir_file->common.gah.root;
		break;
	case RHS_DIR:
		if (!H_GAH_IS_VALID(request->ir_dir)) {
			D_GOTO(err, ret = EHOSTDOWN);
		}
		if (!request->ir_dir->handle_valid) {
			D_GOTO(err, ret = EHOSTDOWN);
		}
		ep.ep_rank = request->ir_dir->gah.root;
		break;
	case RHS_ROOT:
	default:
		ep.ep_rank = fs_handle->gah.root;
	}

	/* Defer clean up until the output is copied. */
	rc = crt_req_set_endpoint(request->rpc, &ep);
	if (rc) {
		D_GOTO(err, ret = EIO);
	}
	IOF_TRACE_INFO(request, "Sending RPC to rank %d",
		       request->rpc->cr_ep.ep_rank);

	crt_req_addref(request->rpc);
	rc = crt_req_send(request->rpc, generic_cb, request);
	if (rc) {
		D_GOTO(err, ret = EIO);
	}
	return 0;
err:
	IOF_TRACE_ERROR(request, "Could not send rpc, rc = %d", ret);

	return ret;
}

static void
query_cb(const struct crt_cb_info *cb_info)
{
	struct query_cb_r *reply = cb_info->cci_arg;

	reply->err = cb_info->cci_rc;
	iof_tracker_signal(&reply->tracker);
}

/*
 * Send RPC to PSR to get information about projected filesystems
 *
 * Returns CaRT error code.
 */
static int
get_info(struct iof_state *iof_state, struct iof_group_info *group,
	 crt_rpc_t **query_rpc)
{
	struct query_cb_r reply = {0};
	crt_rpc_t *rpc = NULL;
	int rc;

	*query_rpc = NULL;

	iof_tracker_init(&reply.tracker, 1);
	rc = crt_req_create(iof_state->iof_ctx.crt_ctx, &group->grp.psr_ep,
			CRT_PROTO_OPC(iof_state->handshake_proto->cpf_base,
				iof_state->handshake_proto->cpf_ver,
				0),
			&rpc);
	if (rc != -DER_SUCCESS || !rpc) {
		IOF_TRACE_ERROR(iof_state,
				"failed to create query rpc request, rc = %d",
				rc);
		return rc;
	}

	IOF_TRACE_LINK(rpc, iof_state, "query_rpc");

	/* decref in query_projections */
	crt_req_addref(rpc);

	rc = crt_req_send(rpc, query_cb, &reply);
	if (rc != -DER_SUCCESS) {
		IOF_TRACE_ERROR(iof_state, "Could not send query RPC, rc = %d",
				rc);
		crt_req_decref(rpc);
		return rc;
	}

	/*make on-demand progress*/
	iof_wait(iof_state->iof_ctx.crt_ctx, &reply.tracker);

	if (reply.err) {
		IOF_TRACE_INFO(iof_state,
			       "Bad RPC reply %d -%s",
			       reply.err,
			       d_errstr(reply.err));
		/* Matches decref in this function */
		crt_req_decref(rpc);

		return reply.err;
	}

	*query_rpc = rpc;

	return -DER_SUCCESS;
}

static int iof_uint_read(char *buf, size_t buflen, void *arg)
{
	uint *value = arg;

	snprintf(buf, buflen, "%u", *value);
	return CNSS_SUCCESS;
}

static int iof_uint64_read(char *buf, size_t buflen, void *arg)
{
	uint64_t *value = arg;

	snprintf(buf, buflen, "%lu", *value);
	return CNSS_SUCCESS;
}

/* Attach to a CaRT group
 *
 * Returns true on success.
 */
static bool
attach_group(struct iof_state *iof_state, struct iof_group_info *group)
{
	struct cnss_plugin_cb *cb = iof_state->cb;
	struct ctrl_dir *ionss_dir = NULL;
	d_rank_list_t *psr_list = NULL;
	int ret;

	/* First check for the IONSS process set, and if it does not
	 * exist then * return cleanly to allow the rest of the CNSS
	 * code to run
	 */
	ret = crt_group_attach(group->grp_name, &group->grp.dest_grp);
	if (ret) {
		IOF_TRACE_ERROR(iof_state,
				"crt_group_attach failed with ret = %d", ret);
		return false;
	}

	ret = iof_lm_attach(group->grp.dest_grp, NULL);
	if (ret != 0) {
		IOF_TRACE_ERROR(iof_state,
				"Could not initialize failover, ret = %d", ret);
		return false;
	}

	ret = crt_group_config_save(group->grp.dest_grp, true);
	if (ret) {
		IOF_TRACE_ERROR(iof_state, "crt_group_config_save failed for "
				"ionss with ret = %d", ret);
		return false;
	}

	/*initialize destination endpoint*/
	group->grp.psr_ep.ep_grp = group->grp.dest_grp;
	ret = crt_lm_group_psr(group->grp.dest_grp, &psr_list);
	if (ret != -DER_SUCCESS) {
		IOF_TRACE_ERROR(group, "Unable to access "
				"PSR list, ret = %d", ret);
		return false;
	}

	/* First element in the list is the PSR */
	atomic_store_release(&group->grp.pri_srv_rank, psr_list->rl_ranks[0]);
	group->grp.psr_ep.ep_rank = psr_list->rl_ranks[0];
	group->grp.psr_ep.ep_tag = 0;
	d_rank_list_free(psr_list);
	IOF_TRACE_INFO(iof_state, "Primary Service Rank: %d",
		       atomic_load_consume(&group->grp.pri_srv_rank));

	ret = cb->create_ctrl_subdir(iof_state->ionss_dir, "0",
				     &ionss_dir);
	if (ret != 0) {
		IOF_TRACE_ERROR(iof_state, "Failed to create control dir for "
				"ionss info (rc = %d)\n", ret);
		return false;
	}
	cb->register_ctrl_constant_uint64(ionss_dir, "psr_rank",
					  group->grp.psr_ep.ep_rank);
	cb->register_ctrl_constant_uint64(ionss_dir, "psr_tag",
					  group->grp.psr_ep.ep_tag);
	/* Fix this when we actually have multiple IONSS apps */
	cb->register_ctrl_constant(ionss_dir, "name", group->grp_name);

	group->grp.enabled = true;

	return true;
}

static bool ih_key_cmp(struct d_hash_table *htable, d_list_t *rlink,
		       const void *key, unsigned int ksize)
{
	const struct ioc_inode_entry *ie;
	const ino_t *ino = key;

	ie = container_of(rlink, struct ioc_inode_entry, ie_htl);

	return *ino == ie->stat.st_ino;
}

static void ih_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ioc_inode_entry *ie;
	int oldref;

	ie = container_of(rlink, struct ioc_inode_entry, ie_htl);
	oldref = atomic_fetch_add(&ie->ie_ref, 1);
	IOF_TRACE_DEBUG(ie, "addref to %u", oldref + 1);
}

static bool ih_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ioc_inode_entry *ie;
	int oldref;

	ie = container_of(rlink, struct ioc_inode_entry, ie_htl);
	oldref = atomic_fetch_sub(&ie->ie_ref, 1);
	IOF_TRACE_DEBUG(ie, "decref to %u", oldref - 1);
	return oldref == 1;
}

static void ih_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct iof_projection_info *fs_handle = htable->ht_priv;
	struct ioc_inode_entry *ie;

	ie = container_of(rlink, struct ioc_inode_entry, ie_htl);

	IOF_TRACE_DEBUG(ie, "parent %lu", ie->parent);
	ie_close(fs_handle, ie);
	D_FREE(ie);
}

d_hash_table_ops_t hops = {.hop_key_cmp = ih_key_cmp,
			   .hop_rec_addref = ih_addref,
			   .hop_rec_decref = ih_decref,
			   .hop_rec_free = ih_free,
};

static void
dh_init(void *arg, void *handle)
{
	struct iof_dir_handle *dh = arg;

	IOC_REQUEST_INIT(&dh->open_req, handle);
	IOC_REQUEST_INIT(&dh->close_req, handle);
	dh->rpc = NULL;
}

/* Reset a RPC in a re-usable descriptor.  If the RPC pointer is valid
 * then drop the two references and zero the pointer.
 */
#define CHECK_AND_RESET_RPC(HANDLE, RPC)			\
	do {							\
		if ((HANDLE)->RPC) {				\
			crt_req_decref((HANDLE)->RPC);		\
			crt_req_decref((HANDLE)->RPC);		\
			(HANDLE)->RPC = NULL;			\
		}						\
	} while (0)

/* As CHECK_AND_RESET_RPC but take a ioc_request as the second option
 * and work on the RPC in the request
 */
#define CHECK_AND_RESET_RRPC(HANDLE, REQUEST)			\
	CHECK_AND_RESET_RPC(HANDLE, REQUEST.rpc)

static bool
dh_reset(void *arg)
{
	struct iof_dir_handle *dh = arg;
	int rc;

	dh->reply_count = 0;

	/* If there has been an error on the local handle, or readdir() is not
	 * exhausted then ensure that all resources are freed correctly
	 */
	if (dh->rpc)
		crt_req_decref(dh->rpc);
	dh->rpc = NULL;

	if (dh->open_req.rpc)
		crt_req_decref(dh->open_req.rpc);

	if (dh->close_req.rpc)
		crt_req_decref(dh->close_req.rpc);

	rc = crt_req_create(dh->open_req.fsh->proj.crt_ctx, NULL,
			    FS_TO_OP(dh->open_req.fsh, opendir),
			    &dh->open_req.rpc);
	if (rc || !dh->open_req.rpc)
		return false;

	rc = crt_req_create(dh->open_req.fsh->proj.crt_ctx, NULL,
			    FS_TO_OP(dh->open_req.fsh, closedir),
			    &dh->close_req.rpc);
	if (rc || !dh->close_req.rpc) {
		crt_req_decref(dh->open_req.rpc);
		return false;
	}

	IOC_REQUEST_RESET(&dh->open_req);
	IOC_REQUEST_RESET(&dh->close_req);

	dh->open_req.ir_ht = RHS_INODE_NUM;
	dh->close_req.ir_ht = RHS_DIR;
	dh->close_req.ir_dir = dh;

	return true;
}

static void
dh_release(void *arg)
{
	struct iof_dir_handle *dh = arg;

	crt_req_decref(dh->open_req.rpc);
	crt_req_decref(dh->close_req.rpc);
}

/* Create a getattr descriptor for use with mempool.
 *
 * Two pools of descriptors are used here, one for getattr and a second
 * for getfattr.  The only difference is the RPC id so the datatypes are
 * the same, as are the init and release functions.
 */
static void
fh_init(void *arg, void *handle)
{
	struct iof_file_handle *fh = arg;

	IOC_REQUEST_INIT(&fh->open_req, handle);
	IOC_REQUEST_INIT(&fh->creat_req, handle);
	IOC_REQUEST_INIT(&fh->release_req, handle);
	fh->ie = NULL;
}

static bool
fh_reset(void *arg)
{
	struct iof_file_handle *fh = arg;
	int rc;

	IOC_REQUEST_RESET(&fh->open_req);
	CHECK_AND_RESET_RRPC(fh, open_req);

	fh->open_req.ir_ht = RHS_INODE_NUM;

	IOC_REQUEST_RESET(&fh->creat_req);
	CHECK_AND_RESET_RRPC(fh, creat_req);

	fh->creat_req.ir_ht = RHS_INODE_NUM;

	IOC_REQUEST_RESET(&fh->release_req);
	CHECK_AND_RESET_RRPC(fh, release_req);

	fh->release_req.ir_ht = RHS_FILE;
	fh->release_req.ir_file = fh;

	/* Used by creat but not open */
	fh->common.ep = fh->open_req.fsh->proj.grp->psr_ep;

	if (!fh->ie) {
		D_ALLOC_PTR(fh->ie);
		if (!fh->ie)
			return false;
		atomic_fetch_add(&fh->ie->ie_ref, 1);
	}

	rc = crt_req_create(fh->open_req.fsh->proj.crt_ctx, NULL,
			    FS_TO_OP(fh->open_req.fsh, open), &fh->open_req.rpc);
	if (rc || !fh->open_req.rpc) {
		D_FREE(fh->ie);
		return false;
	}

	rc = crt_req_create(fh->open_req.fsh->proj.crt_ctx, NULL,
			    FS_TO_OP(fh->open_req.fsh, create), &fh->creat_req.rpc);
	if (rc || !fh->creat_req.rpc) {
		D_FREE(fh->ie);
		crt_req_decref(fh->open_req.rpc);
		return false;
	}

	rc = crt_req_create(fh->open_req.fsh->proj.crt_ctx, NULL,
			    FS_TO_OP(fh->open_req.fsh, close), &fh->release_req.rpc);
	if (rc || !fh->release_req.rpc) {
		D_FREE(fh->ie);
		crt_req_decref(fh->open_req.rpc);
		crt_req_decref(fh->creat_req.rpc);
		return false;
	}

	crt_req_addref(fh->open_req.rpc);
	crt_req_addref(fh->creat_req.rpc);
	crt_req_addref(fh->release_req.rpc);
	D_INIT_LIST_HEAD(&fh->fh_ino_list);
	return true;
}

static void
fh_release(void *arg)
{
	struct iof_file_handle *fh = arg;

	crt_req_decref(fh->open_req.rpc);
	crt_req_decref(fh->open_req.rpc);
	crt_req_decref(fh->creat_req.rpc);
	crt_req_decref(fh->creat_req.rpc);
	crt_req_decref(fh->release_req.rpc);
	crt_req_decref(fh->release_req.rpc);
	D_FREE(fh->ie);
}

#define COMMON_INIT(type)						\
	static void type##_common_init(void *arg, void *handle)		\
	{								\
		struct common_req *req = arg;				\
		IOC_REQUEST_INIT(&req->request, handle);		\
		req->opcode = FS_TO_OP(req->request.fsh, type);		\
	}
COMMON_INIT(getattr);
COMMON_INIT(setattr);
COMMON_INIT(close);

/* Reset and prepare for use a common descriptor */
static bool
common_reset(void *arg)
{
	struct common_req *req = arg;
	int rc;

	req->request.req = NULL;

	IOC_REQUEST_RESET(&req->request);
	CHECK_AND_RESET_RRPC(req, request);

	rc = crt_req_create(req->request.fsh->proj.crt_ctx, NULL,
			    req->opcode, &req->request.rpc);
	if (rc || !req->request.rpc) {
		IOF_TRACE_ERROR(req, "Could not create request, rc = %d", rc);
		return false;
	}
	crt_req_addref(req->request.rpc);

	return true;
}

/* Destroy a descriptor which could be either getattr or close */
static void
common_release(void *arg)
{
	struct common_req *req = arg;

	crt_req_decref(req->request.rpc);
	crt_req_decref(req->request.rpc);
}

#define ENTRY_INIT(type)						\
	static void type##_entry_init(void *arg, void *handle)		\
	{								\
		struct entry_req *req = arg;				\
		IOC_REQUEST_INIT(&req->request, handle);		\
		req->opcode = FS_TO_OP(req->request.fsh, type);		\
		req->dest = NULL;					\
		req->ie = NULL;						\
	}
ENTRY_INIT(lookup);
ENTRY_INIT(mkdir);
ENTRY_INIT(symlink);

static bool
entry_reset(void *arg)
{
	struct entry_req *req = arg;
	int rc;

	/* If this descriptor has previously been used then destroy the
	 * existing RPC
	 */
	IOC_REQUEST_RESET(&req->request);
	CHECK_AND_RESET_RRPC(req, request);

	req->request.ir_ht = RHS_INODE_NUM;
	/* Free any destination string on this descriptor.  This is only used
	 * for symlink to store the link target whilst the RPC is being sent
	 */
	D_FREE(req->dest);

	if (!req->ie) {
		D_ALLOC_PTR(req->ie);
		if (!req->ie)
			return false;
		atomic_fetch_add(&req->ie->ie_ref, 1);
	}

	/* Create a new RPC ready for later use.  Take an initial reference
	 * to the RPC so that it is not cleaned up after a successful send.
	 *
	 * After calling send the lookup code will re-take the dropped
	 * reference which means that on all subsequent calls to reset()
	 * or release() the ref count will be two.
	 *
	 * This means that both descriptor creation and destruction are
	 * done off the critical path.
	 */
	rc = crt_req_create(req->request.fsh->proj.crt_ctx, NULL, req->opcode,
			    &req->request.rpc);
	if (rc || !req->request.rpc) {
		IOF_TRACE_ERROR(req, "Could not create request, rc = %d", rc);
		D_FREE(req->ie);
		return false;
	}
	crt_req_addref(req->request.rpc);

	return true;
}

/* Destroy a descriptor which could be either getattr or getfattr */
static void
entry_release(void *arg)
{
	struct entry_req *req = arg;

	crt_req_decref(req->request.rpc);
	crt_req_decref(req->request.rpc);
	D_FREE(req->ie);
}

static void
rb_page_init(void *arg, void *handle)
{
	struct iof_rb *rb = arg;

	IOC_REQUEST_INIT(&rb->rb_req, handle);
	rb->buf_size = 4096;
	rb->fbuf.count = 1;
	rb->fbuf.buf[0].fd = -1;
	rb->failure = false;
	rb->lb.buf = NULL;
}

static void
rb_large_init(void *arg, void *handle)
{
	struct iof_rb *rb = arg;

	rb_page_init(arg, handle);
	rb->buf_size = rb->rb_req.fsh->max_read;
}

static bool
rb_reset(void *arg)
{
	struct iof_rb *rb = arg;
	int rc;

	IOC_REQUEST_RESET(&rb->rb_req);
	CHECK_AND_RESET_RRPC(rb, rb_req);

	rb->rb_req.ir_ht = RHS_FILE;

	if (rb->failure) {
		IOF_BULK_FREE(rb, lb);
		rb->failure = false;
	}

	if (!rb->lb.buf) {
		IOF_BULK_ALLOC(rb->rb_req.fsh->proj.crt_ctx, rb, lb,
			       rb->buf_size, false);
		if (!rb->lb.buf)
			return false;
	}

	rc = crt_req_create(rb->rb_req.fsh->proj.crt_ctx, NULL,
			    FS_TO_IOOP(rb->rb_req.fsh, 0), &rb->rb_req.rpc);
	if (rc || !rb->rb_req.rpc) {
		IOF_TRACE_ERROR(rb, "Could not create request, rc = %d", rc);
		IOF_BULK_FREE(rb, lb);
		return false;
	}
	crt_req_addref(rb->rb_req.rpc);

	return true;
}

static void
rb_release(void *arg)
{
	struct iof_rb *rb = arg;

	IOF_BULK_FREE(rb, lb);

	crt_req_decref(rb->rb_req.rpc);
	crt_req_decref(rb->rb_req.rpc);
}

static void
wb_init(void *arg, void *handle)
{
	struct iof_wb *wb = arg;

	IOC_REQUEST_INIT(&wb->wb_req, handle);
	wb->failure = false;
	wb->lb.buf = NULL;
}

static bool
wb_reset(void *arg)
{
	struct iof_wb *wb = arg;
	int rc;

	IOC_REQUEST_RESET(&wb->wb_req);
	CHECK_AND_RESET_RRPC(wb, wb_req);

	wb->wb_req.ir_ht = RHS_FILE;

	if (wb->failure) {
		IOF_BULK_FREE(wb, lb);
		wb->failure = false;
	}

	if (!wb->lb.buf) {
		IOF_BULK_ALLOC(wb->wb_req.fsh->proj.crt_ctx, wb, lb,
			       wb->wb_req.fsh->proj.max_write, true);
		if (!wb->lb.buf)
			return false;
	}

	rc = crt_req_create(wb->wb_req.fsh->proj.crt_ctx, NULL,
			    FS_TO_IOOP(wb->wb_req.fsh, 1), &wb->wb_req.rpc);
	if (rc || !wb->wb_req.rpc) {
		IOF_TRACE_ERROR(wb, "Could not create request, rc = %d", rc);
		IOF_BULK_FREE(wb, lb);
		return false;
	}
	crt_req_addref(wb->wb_req.rpc);

	return true;
}

static void
wb_release(void *arg)
{
	struct iof_wb *wb = arg;

	crt_req_decref(wb->wb_req.rpc);
	crt_req_decref(wb->wb_req.rpc);

	IOF_BULK_FREE(wb, lb);
}

static int
iof_check_complete(void *arg)
{
	struct iof_tracker *tracker = arg;

	return iof_tracker_test(tracker);
}

/* Call crt_progress() on a context until it returns timeout
 * or an error.
 *
 * Returns -DER_SUCCESS on timeout or passes through any other errors.
 */
static int
iof_progress_drain(struct iof_ctx *iof_ctx)
{
	int ctx_rc;

	if (!iof_ctx->crt_ctx) {
		IOF_TRACE_WARNING(iof_ctx, "Null context");
		return -DER_SUCCESS;
	}

	do {
		ctx_rc = crt_progress(iof_ctx->crt_ctx, 1000000, NULL, NULL);

		if (ctx_rc != -DER_TIMEDOUT && ctx_rc != -DER_SUCCESS) {
			IOF_TRACE_WARNING(iof_ctx, "progress returned %d",
					  ctx_rc);
			return ctx_rc;
		}

	} while (ctx_rc != -DER_TIMEDOUT);
	return -DER_SUCCESS;
}

static void *
iof_thread(void *arg)
{
	struct iof_ctx	*iof_ctx = arg;
	int		rc;

	iof_tracker_signal(&iof_ctx->thread_start_tracker);
	do {
		rc = crt_progress(iof_ctx->crt_ctx,
				  iof_ctx->poll_interval,
				  iof_ctx->callback_fn,
				  &iof_ctx->thread_stop_tracker);

		if (rc == -DER_TIMEDOUT) {
			rc = 0;
			sched_yield();
		}

		if (rc != 0)
			IOF_TRACE_ERROR(iof_ctx, "crt_progress failed rc: %d",
					rc);

	} while (!iof_tracker_test(&iof_ctx->thread_stop_tracker));

	if (rc != 0)
		IOF_TRACE_ERROR(iof_ctx, "crt_progress error on shutdown "
				"rc: %d", rc);

	return (void *)(uintptr_t)rc;
}

/* Start a progress thread, return true on success */
static bool
iof_thread_start(struct iof_ctx *iof_ctx)
{
	int rc;

	iof_tracker_init(&iof_ctx->thread_start_tracker, 1);
	iof_tracker_init(&iof_ctx->thread_stop_tracker, 1);

	rc = pthread_create(&iof_ctx->thread, NULL,
			    iof_thread, iof_ctx);

	if (rc != 0) {
		IOF_TRACE_ERROR(iof_ctx, "Could not start progress thread");
		return false;
	}

	rc = pthread_setname_np(iof_ctx->thread, "IOF thread");
	if (rc != 0)
		IOF_TRACE_ERROR(iof_ctx, "Could not set thread name");

	iof_tracker_wait(&iof_ctx->thread_start_tracker);
	return true;
}

/* Stop the progress thread, and destroy the cart context
 *
 * Returns the return code of crt_context_destroy()
 */
static int
iof_thread_stop(struct iof_ctx *iof_ctx)
{
	void *rtn;

	if (!iof_ctx->thread)
		return 0;

	IOF_TRACE_INFO(iof_ctx, "Stopping CRT thread");
	iof_tracker_signal(&iof_ctx->thread_stop_tracker);
	pthread_join(iof_ctx->thread, &rtn);
	IOF_TRACE_INFO(iof_ctx,
		       "CRT thread stopped with %d",
		       (int)(uintptr_t)rtn);

	iof_ctx->thread = 0;

	return (int)(uintptr_t)rtn;
}

static int iof_reg(void *arg, struct cnss_plugin_cb *cb, size_t cb_size)
{
	struct iof_state *iof_state = arg;
	struct iof_group_info *group;
	int ret;

	iof_state->cb = cb;

	iof_state->group.grp_name = IOF_DEFAULT_SET;

	D_INIT_LIST_HEAD(&iof_state->fs_list);

	cb->register_ctrl_constant_uint64(cb->plugin_dir, "ionss_count", 1);
	ret = cb->create_ctrl_subdir(cb->plugin_dir, "ionss",
				     &iof_state->ionss_dir);
	if (ret != 0) {
		IOF_TRACE_ERROR(iof_state, "Failed to create control dir for "
				"ionss info (rc = %d)", ret);
		return 1;
	}

	ret = crt_context_create(&iof_state->iof_ctx.crt_ctx);
	if (ret != -DER_SUCCESS) {
		IOF_TRACE_ERROR(iof_state, "Context not created");
		return 1;
	}

	IOF_TRACE_UP(&iof_state->iof_ctx, iof_state, "iof_ctx");

	ret = crt_context_set_timeout(iof_state->iof_ctx.crt_ctx, 7);
	if (ret != -DER_SUCCESS) {
		IOF_TRACE_ERROR(iof_state, "Context timeout not set");
		return 1;
	}

	if (!iof_thread_start(&iof_state->iof_ctx)) {
		IOF_TRACE_ERROR(iof_state, "Failed to create progress thread");
		return 1;
	}

	/* Despite the hard coding above, now we can do attaches in a loop */
	group = &iof_state->group;

	if (!attach_group(iof_state, group)) {
		IOF_TRACE_ERROR(iof_state,
				"Failed to attach to service group '%s'",
				group->grp_name);
		return 1;
	}
	group->crt_attached = true;

	cb->register_ctrl_constant_uint64(cb->plugin_dir, "ioctl_version",
					  IOF_IOCTL_VERSION);

	/*registrations*/
	ret = crt_register_eviction_cb(ioc_eviction_cb, iof_state);
	if (ret) {
		IOF_TRACE_ERROR(iof_state, "Eviction callback registration "
				"failed with ret: %d", ret);
		return ret;
	}

	ret = iof_client_register(&group->grp.psr_ep,
				  &iof_state->handshake_proto,
				  &iof_state->proto,
				  &iof_state->io_proto);
	if (ret) {
		IOF_TRACE_ERROR(iof_state,
				"RPC registration failed with ret: %d", ret);
		return ret;
	}

	return ret;
}

static int failover_state_cb(char *buf, size_t buflen, void *arg)
{
	struct iof_projection_info *fs_handle = arg;
	char *output;

	switch (fs_handle->failover_state) {
	case iof_failover_running:
		output = "running";
		break;
	case iof_failover_offline:
		output = "offline";
		break;
	case iof_failover_in_progress:
		output = "in_progress";
		break;
	case iof_failover_complete:
		output = "complete";
		break;
	default:
		output = "unknown";
		IOF_TRACE_ERROR(fs_handle, "Unknown failover state %d",
				fs_handle->failover_state);
	}

	strncpy(buf, output, buflen);
	return CNSS_SUCCESS;
}

static uint64_t online_read_cb(void *arg)
{
	struct iof_projection_info *fs_handle = arg;

	return !FS_IS_OFFLINE(fs_handle);
}

static int online_write_cb(uint64_t value, void *arg)
{
	struct iof_projection_info *fs_handle = arg;

	if (value > 1)
		return EINVAL;

	if (value)
		fs_handle->offline_reason = 0;
	else
		fs_handle->offline_reason = EHOSTDOWN;

	return CNSS_SUCCESS;
}

#define REGISTER_STAT(_STAT) cb->register_ctrl_variable(	\
		fs_handle->stats_dir,				\
		#_STAT,						\
		iof_uint_read,					\
		NULL, NULL,					\
		&fs_handle->stats->_STAT)
#define REGISTER_STAT64(_STAT) cb->register_ctrl_variable(	\
		fs_handle->stats_dir,				\
		#_STAT,						\
		iof_uint64_read,				\
		NULL, NULL,					\
		&fs_handle->stats->_STAT)

static bool
initialize_projection(struct iof_state *iof_state,
		      struct iof_group_info *group,
		      struct iof_fs_info *fs_info,
		      struct iof_query_out *query,
		      int id)
{
	struct iof_projection_info	*fs_handle;
	struct cnss_plugin_cb		*cb;
	struct fuse_args		args = {0};
	bool				writeable = false;
	int				ret;
	struct fuse_lowlevel_ops	*fuse_ops = NULL;
	int i;

	struct iof_pool_reg pt = {.init = dh_init,
				  .reset = dh_reset,
				  .release = dh_release,
				  POOL_TYPE_INIT(iof_dir_handle, dh_od_list)};

	struct iof_pool_reg fh = {.init = fh_init,
				  .reset = fh_reset,
				  .release = fh_release,
				  POOL_TYPE_INIT(iof_file_handle, fh_of_list)};

	struct iof_pool_reg common_t = {.reset = common_reset,
					.release = common_release,
					POOL_TYPE_INIT(common_req, list)};

	struct iof_pool_reg entry_t = {.reset = entry_reset,
				       .release = entry_release,
				       POOL_TYPE_INIT(entry_req, list)};

	struct iof_pool_reg rb_page = {.init = rb_page_init,
				       .reset = rb_reset,
				       .release = rb_release,
				       POOL_TYPE_INIT(iof_rb, rb_req.ir_list)};

	struct iof_pool_reg rb_large = {.init = rb_large_init,
					.reset = rb_reset,
					.release = rb_release,
					POOL_TYPE_INIT(iof_rb, rb_req.ir_list)};

	struct iof_pool_reg wb = {.init = wb_init,
				  .reset = wb_reset,
				  .release = wb_release,
				  POOL_TYPE_INIT(iof_wb, wb_req.ir_list)};

	cb = iof_state->cb;

	/* TODO: This is presumably wrong although it's not
	 * clear how best to handle it
	 */
	if (!iof_is_mode_supported(fs_info->flags))
		return false;

	if (fs_info->flags & IOF_WRITEABLE)
		writeable = true;

	D_ALLOC_PTR(fs_handle);
	if (!fs_handle)
		return false;

	IOF_TRACE_UP(fs_handle, iof_state, "iof_projection");

	fs_handle->ctx_num = fs_info->cnss_thread_count;
	if (fs_handle->ctx_num == 0) {
		fs_handle->ctx_num = 1;
	}
	if ((fs_info->flags & IOF_FAILOVER) && (fs_handle->ctx_num < 2)) {
		fs_handle->ctx_num = 2;
	}

	D_ALLOC_ARRAY(fs_handle->ctx_array, fs_handle->ctx_num);
	if (!fs_handle->ctx_array) {
		IOF_TRACE_DOWN(fs_handle);
		D_FREE(fs_handle);
		return false;
	}

	for (i = 0; i < fs_handle->ctx_num; i++) {
		IOF_TRACE_UP(&fs_handle->ctx_array[i], fs_handle, "iof_ctx");
	}

	ret = iof_pool_init(&fs_handle->pool, fs_handle);
	if (ret != -DER_SUCCESS)
		D_GOTO(err, 0);

	fs_handle->iof_state = iof_state;
	fs_handle->flags = fs_info->flags;
	fs_handle->proj.io_proto = iof_state->io_proto;
	fs_handle->failover_state = iof_failover_running;
	IOF_TRACE_INFO(fs_handle, "Filesystem mode: Private; "
			"Access: Read-%s | Fail Over: %s",
			fs_handle->flags & IOF_WRITEABLE
					 ? "Write" : "Only",
			fs_handle->flags & IOF_FAILOVER
					 ? "Enabled" : "Disabled");
	IOF_TRACE_INFO(fs_handle, "FUSE: %sthreaded | API => "
			"Write: ioc_ll_write%s, Read: fuse_reply_%s",
			fs_handle->flags & IOF_CNSS_MT
					 ? "Multi-" : "Single ",
			fs_handle->flags & IOF_FUSE_WRITE_BUF ? "_buf" : "",
			fs_handle->flags & IOF_FUSE_READ_BUF ? "buf" : "data");

	IOF_TRACE_INFO(fs_handle, "%d cart threads",
		       fs_handle->ctx_num);

	ret = d_hash_table_create_inplace(D_HASH_FT_RWLOCK |
					  D_HASH_FT_EPHEMERAL,
					  fs_info->htable_size,
					  fs_handle, &hops,
					  &fs_handle->inode_ht);
	if (ret != 0)
		D_GOTO(err, 0);

	/* Keep a list of open file and directory handles
	 *
	 * Handles are added to these lists as the open call succeeds,
	 * and removed from the list when a release request is received,
	 * therefore this is a list of handles held locally by the
	 * kernel, not a list of handles the CNSS holds on the IONSS.
	 *
	 * Used during shutdown so that we can iterate over the list after
	 * terminating the FUSE thread to send close RPCs for any handles
	 * the server didn't close.
	 */
	D_INIT_LIST_HEAD(&fs_handle->opendir_list);
	ret = D_MUTEX_INIT(&fs_handle->od_lock, NULL);
	if (ret != 0)
		D_GOTO(err, 0);
	D_INIT_LIST_HEAD(&fs_handle->openfile_list);
	ret = D_MUTEX_INIT(&fs_handle->of_lock, NULL);
	if (ret != 0)
		D_GOTO(err, 0);

	D_INIT_LIST_HEAD(&fs_handle->p_inval_list);

	ret = D_MUTEX_INIT(&fs_handle->gah_lock, NULL);
	if (ret != 0)
		D_GOTO(err, 0);

	ret = D_MUTEX_INIT(&fs_handle->p_request_lock, NULL);
	if (ret != 0)
		D_GOTO(err, 0);

	D_INIT_LIST_HEAD(&fs_handle->p_ie_children);
	D_INIT_LIST_HEAD(&fs_handle->p_requests_pending);

	fs_handle->max_read = fs_info->max_read;
	fs_handle->max_iov_read = fs_info->max_iov_read;
	fs_handle->proj.max_write = fs_info->max_write;
	fs_handle->proj.max_iov_write = fs_info->max_iov_write;
	fs_handle->readdir_size = fs_info->readdir_size;
	fs_handle->gah = fs_info->gah;

	strncpy(fs_handle->mnt_dir.name, fs_info->dir_name.name, NAME_MAX);

	IOF_TRACE_DEBUG(fs_handle,
			"Projected Mount %s", fs_handle->mnt_dir.name);

	IOF_TRACE_INFO(fs_handle, "Mountpoint for this projection: '%s'",
		       fs_handle->mnt_dir.name);

	fs_handle->fs_id = fs_info->id;
	fs_handle->proj.cli_fs_id = id;
	fs_handle->proj.progress_thread = 1;

	D_ALLOC_PTR(fs_handle->stats);
	if (!fs_handle->stats)
		D_GOTO(err, 0);

	snprintf(fs_handle->ctrl_dir.name,
		 NAME_MAX,
		 "%d",
		 fs_handle->proj.cli_fs_id);

	cb->create_ctrl_subdir(iof_state->projections_dir,
			       fs_handle->ctrl_dir.name,
			       &fs_handle->fs_dir);

	/* Register the mount point with the control
	 * filesystem
	 */
	D_ASPRINTF(fs_handle->mount_point,
		   "%s/%s",
		   cb->prefix,
		   fs_handle->mnt_dir.name);
	if (!fs_handle->mount_point)
		D_GOTO(err, 0);

	cb->register_ctrl_constant(fs_handle->fs_dir,
				   "mount_point",
				   fs_handle->mount_point);

	cb->register_ctrl_constant(fs_handle->fs_dir, "mode",
				   "private");

	cb->register_ctrl_constant_uint64(fs_handle->fs_dir,
					  "fs_id",
					  fs_handle->fs_id);

	cb->register_ctrl_constant_uint64(fs_handle->fs_dir,
					  "max_read",
					  fs_handle->max_read);

	cb->register_ctrl_constant_uint64(fs_handle->fs_dir,
					  "max_iov_read",
					  fs_handle->max_iov_read);

	cb->register_ctrl_constant_uint64(fs_handle->fs_dir,
					  "max_write",
					  fs_handle->proj.max_write);

	cb->register_ctrl_constant_uint64(fs_handle->fs_dir,
					  "max_iov_write",
					  fs_handle->proj.max_iov_write);

	cb->register_ctrl_constant_uint64(fs_handle->fs_dir,
				  "readdir_size",
				  fs_handle->readdir_size);

	cb->register_ctrl_uint64_variable(fs_handle->fs_dir, "online",
					  online_read_cb,
					  online_write_cb,
					  fs_handle);

	cb->register_ctrl_variable(fs_handle->fs_dir, "failover_state",
				   failover_state_cb, NULL, NULL, fs_handle);

	cb->create_ctrl_subdir(fs_handle->fs_dir, "stats",
			       &fs_handle->stats_dir);

	REGISTER_STAT(opendir);
	REGISTER_STAT(readdir);
	REGISTER_STAT(closedir);
	REGISTER_STAT(getattr);
	REGISTER_STAT(readlink);
	REGISTER_STAT(statfs);
	REGISTER_STAT(ioctl);
	REGISTER_STAT(open);
	REGISTER_STAT(release);
	REGISTER_STAT(read);
	REGISTER_STAT(il_ioctl);
	REGISTER_STAT(lookup);
	REGISTER_STAT(forget);
	REGISTER_STAT64(read_bytes);

	if (writeable) {
		REGISTER_STAT(create);
		REGISTER_STAT(mkdir);
		REGISTER_STAT(unlink);
		REGISTER_STAT(symlink);
		REGISTER_STAT(rename);
		REGISTER_STAT(write);
		REGISTER_STAT(fsync);
		REGISTER_STAT(setattr);
		REGISTER_STAT64(write_bytes);
	}

	IOF_TRACE_INFO(fs_handle, "Filesystem ID srv:%d cli:%d",
		       fs_handle->fs_id,
		       fs_handle->proj.cli_fs_id);

	fs_handle->proj.grp = &group->grp;

	ret = crt_context_create(&fs_handle->proj.crt_ctx);
	if (ret) {
		IOF_TRACE_ERROR(fs_handle, "Could not create context");
		D_GOTO(err, 0);
	}

	IOF_TRACE_DEBUG(fs_handle, "Setting timeout to %d", fs_info->timeout);

	ret = crt_context_set_timeout(fs_handle->proj.crt_ctx, fs_info->timeout);
	if (ret != -DER_SUCCESS) {
		IOF_TRACE_ERROR(iof_state, "Context timeout not set");
		D_GOTO(err, 0);
	}

	for (i = 0; i < fs_handle->ctx_num; i++) {
		fs_handle->ctx_array[i].crt_ctx       = fs_handle->proj.crt_ctx;
		fs_handle->ctx_array[i].poll_interval = iof_state->iof_ctx.poll_interval;
		fs_handle->ctx_array[i].callback_fn   = iof_state->iof_ctx.callback_fn;

		/* TODO: Much better error checking is required here, not least
		 * terminating the thread if there are any failures in the rest
		 * of this function
		 */
		if (!iof_thread_start(&fs_handle->ctx_array[i])) {
			IOF_TRACE_ERROR(fs_handle, "Could not create thread");
			D_GOTO(err, 0);
		}
	}

	args.argc = 4;
	if (!writeable)
		args.argc++;

	args.allocated = 1;
	D_ALLOC_ARRAY(args.argv, args.argc);
	if (!args.argv)
		D_GOTO(err, 0);

	D_STRNDUP(args.argv[0], "", 1);
	if (!args.argv[0])
		D_GOTO(err, 0);

	D_STRNDUP(args.argv[1], "-ofsname=IOF", 32);
	if (!args.argv[1])
		D_GOTO(err, 0);

	D_STRNDUP(args.argv[2], "-osubtype=pam", 32);
	if (!args.argv[2])
		D_GOTO(err, 0);

	D_ASPRINTF(args.argv[3], "-omax_read=%u", fs_handle->max_read);
	if (!args.argv[3])
		D_GOTO(err, 0);

	if (!writeable) {
		D_STRNDUP(args.argv[4], "-oro", 32);
		if (!args.argv[4])
			D_GOTO(err, 0);
	}

	fuse_ops = iof_get_fuse_ops(fs_handle->flags);
	if (!fuse_ops)
		D_GOTO(err, 0);

	/* Register the directory handle type
	 *
	 * This is done late on in the registration as the dh_int() and
	 * dh_reset() functions require access to fs_handle.
	 */
	fs_handle->dh_pool = iof_pool_register(&fs_handle->pool, &pt);
	if (!fs_handle->dh_pool)
		D_GOTO(err, 0);

	common_t.init = getattr_common_init;
	fs_handle->fgh_pool = iof_pool_register(&fs_handle->pool, &common_t);
	if (!fs_handle->fgh_pool)
		D_GOTO(err, 0);

	common_t.init = setattr_common_init;
	fs_handle->fsh_pool = iof_pool_register(&fs_handle->pool, &common_t);
	if (!fs_handle->fsh_pool)
		D_GOTO(err, 0);

	common_t.init = close_common_init;
	fs_handle->close_pool = iof_pool_register(&fs_handle->pool, &common_t);
	if (!fs_handle->close_pool)
		D_GOTO(err, 0);

	entry_t.init = lookup_entry_init;
	fs_handle->lookup_pool = iof_pool_register(&fs_handle->pool, &entry_t);
	if (!fs_handle->lookup_pool)
		D_GOTO(err, 0);

	entry_t.init = mkdir_entry_init;
	fs_handle->mkdir_pool = iof_pool_register(&fs_handle->pool, &entry_t);
	if (!fs_handle->mkdir_pool)
		D_GOTO(err, 0);

	entry_t.init = symlink_entry_init;
	fs_handle->symlink_pool = iof_pool_register(&fs_handle->pool, &entry_t);
	if (!fs_handle->symlink_pool)
		D_GOTO(err, 0);

	fs_handle->fh_pool = iof_pool_register(&fs_handle->pool, &fh);
	if (!fs_handle->fh_pool)
		D_GOTO(err, 0);

	fs_handle->rb_pool_page = iof_pool_register(&fs_handle->pool, &rb_page);
	if (!fs_handle->rb_pool_page)
		D_GOTO(err, 0);

	fs_handle->rb_pool_large = iof_pool_register(&fs_handle->pool, &rb_large);
	if (!fs_handle->rb_pool_large)
		D_GOTO(err, 0);

	fs_handle->write_pool = iof_pool_register(&fs_handle->pool, &wb);
	if (!fs_handle->write_pool)
		D_GOTO(err, 0);

	if (!cb->register_fuse_fs(cb->handle,
				  NULL,
				  fuse_ops,
				  &args,
				  fs_handle->mnt_dir.name,
				 (fs_handle->flags & IOF_CNSS_MT) != 0,
				  fs_handle,
				  &fs_handle->session)) {
		IOF_TRACE_ERROR(fs_handle, "Unable to register FUSE fs");
		D_GOTO(err, 0);
	}

	D_FREE(fuse_ops);

	IOF_TRACE_DEBUG(fs_handle, "Fuse mount installed at: '%s'",
			fs_handle->mnt_dir.name);

	d_list_add_tail(&fs_handle->link, &iof_state->fs_list);

	return true;
err:
	iof_pool_destroy(&fs_handle->pool);
	D_FREE(fuse_ops);
	D_FREE(fs_handle);
	return false;
}

static bool
query_projections(struct iof_state *iof_state,
		  struct iof_group_info *group,
		  int *total, int *active)
{
	crt_rpc_t *query_rpc = NULL;
	struct iof_query_out *query;
	int rc;
	int i;

	*total = *active = 0;

	/* Query the IONSS for initial information, including projection list
	 *
	 * Do this in a loop, until success, if there is a eviction then select
	 * a new endpoint and try again.  As this is the first RPC that IOF
	 * sends there is no cleanup to perform if this fails, as there is no
	 * server side-state or RPCs created at this point.
	 */
	do {
		rc = get_info(iof_state, group, &query_rpc);

		if (rc == -DER_OOG || rc == -DER_EVICTED) {
			d_rank_list_t *psr_list = NULL;

			rc = crt_lm_group_psr(group->grp.dest_grp, &psr_list);
			if (rc != -DER_SUCCESS)
				return false;

			IOF_TRACE_WARNING(iof_state,
					  "Changing IONNS rank from %d to %d",
					  group->grp.psr_ep.ep_rank,
					  psr_list->rl_ranks[0]);

			atomic_store_release(&group->grp.pri_srv_rank,
					     psr_list->rl_ranks[0]);
			group->grp.psr_ep.ep_rank = psr_list->rl_ranks[0];
			d_rank_list_free(psr_list);

		} else if (rc != -DER_SUCCESS) {
			IOF_TRACE_ERROR(iof_state,
					"Query operation failed: %d",
					rc);
			return false;
		}

	} while (rc != -DER_SUCCESS);

	if (!query_rpc) {
		IOF_TRACE_ERROR(iof_state, "Query operation failed");
		return false;
	}

	query = crt_reply_get(query_rpc);

	iof_state->iof_ctx.poll_interval = query->poll_interval;
	iof_state->iof_ctx.callback_fn = query->progress_callback ?
					 iof_check_complete : NULL;
	IOF_TRACE_INFO(iof_state,
		       "Poll Interval: %u microseconds; Progress Callback: %s",
		       query->poll_interval,
		       query->progress_callback ? "Enabled" : "Disabled");

	IOF_TRACE_DEBUG(iof_state, "Number of filesystems projected by %s: %ld",
			group->grp_name, query->info.ca_count);

	for (i = 0; i < query->info.ca_count; i++) {

		if (!initialize_projection(iof_state, group,
					   &query->info.ca_arrays[i], query,
					   (*total)++)) {
			IOF_TRACE_ERROR(iof_state,
					"Could not initialize projection '%s' from %s",
					query->info.ca_arrays[i].dir_name.name,
					group->grp_name);
			return false;
		}

		(*active)++;
	}

	crt_req_decref(query_rpc);

	return true;
}

static int iof_post_start(void *arg)
{
	struct cnss_plugin_cb	*cb;
	struct iof_state	*iof_state = arg;
	struct iof_group_info	*group = &iof_state->group;
	int			active_projections = 0;
	int			total_projections = 0;
	int active;
	int ret;

	cb = iof_state->cb;

	ret = cb->create_ctrl_subdir(cb->plugin_dir, "projections",
				     &iof_state->projections_dir);
	if (ret != 0) {
		IOF_TRACE_ERROR(iof_state, "Failed to create control dir for "
				"PA mode (rc = %d)\n", ret);
		return 1;
	}

	if (!group->crt_attached)
		return 1;

	if (!query_projections(iof_state, group, &total_projections, &active)) {
		IOF_TRACE_ERROR(iof_state,
				"Couldn't mount projections from %s",
				group->grp_name);
		return 1;
	}
	active_projections += active;

	group->iof_registered = true;

	cb->register_ctrl_constant_uint64(cb->plugin_dir,
					  "projection_count",
					  total_projections);

	if (total_projections == 0) {
		IOF_TRACE_ERROR(iof_state, "No projections found");
		return 1;
	}

	if (active_projections == 0) {
		IOF_TRACE_ERROR(iof_state, "No projections found");
		return 1;
	}

	iof_state->num_proj = total_projections;
	return 0;
}

static int
ino_flush(d_list_t *rlink, void *arg)
{
	struct iof_projection_info *fs_handle = arg;
	struct ioc_inode_entry *ie = container_of(rlink,
						  struct ioc_inode_entry,
						  ie_htl);
	int rc;

	/* Only evict entries that are direct children of the root, the kernel
	 * will walk the tree for us
	 */
	if (ie->parent != 1)
		return 0;

	rc = fuse_lowlevel_notify_inval_entry(fs_handle->session,
					      ie->parent,
					      ie->name,
					      strlen(ie->name));
	if (rc != 0)
		IOF_TRACE_WARNING(ie,
				  "%lu %lu '%s': %d %s",
				  ie->parent, ie->stat.st_ino, ie->name, rc,
				  strerror(-rc));
	else
		IOF_TRACE_INFO(ie,
			       "%lu %lu '%s': %d %s",
			       ie->parent, ie->stat.st_ino, ie->name, rc,
			       strerror(-rc));

	/* If the FUSE connection is dead then do not traverse further, it doesn't
	 * matter what gets returned here, as long as it's negative
	 */
	if (rc == -EBADF)
		return -DER_NO_HDL;

	return -DER_SUCCESS;
}

/* Called once per projection, before the FUSE filesystem has been torn down */
static void iof_flush_fuse(void *arg)
{
	struct iof_projection_info *fs_handle = arg;
	int rc;

	IOF_TRACE_INFO(fs_handle, "Flushing inode table");

	rc = d_hash_table_traverse(&fs_handle->inode_ht, ino_flush,
				   fs_handle);

	IOF_TRACE_INFO(fs_handle, "Flush complete: %d", rc);
}

/* Called once per projection, after the FUSE filesystem has been torn down */
static int iof_deregister_fuse(void *arg)
{
	struct iof_projection_info *fs_handle = arg;
	d_list_t *rlink = NULL;
	struct iof_file_handle *fh, *fh2;
	struct iof_dir_handle *dh, *dh2;
	uint64_t refs = 0;
	int handles = 0;
	int rc;
	int rcp = 0;
	int i;

	IOF_TRACE_INFO(fs_handle, "Draining inode table");
	do {
		struct ioc_inode_entry *ie;
		uint ref;

		rlink = d_hash_rec_first(&fs_handle->inode_ht);

		if (!rlink)
			break;

		ie = container_of(rlink, struct ioc_inode_entry, ie_htl);

		ref = atomic_load_consume(&ie->ie_ref);

		IOF_TRACE_DEBUG(ie, "Dropping %d", ref);

		refs += ref;
		ie->parent = 0;
		d_hash_rec_ndecref(&fs_handle->inode_ht, ref, rlink);
		handles++;
	} while (rlink);

	if (handles) {
		IOF_TRACE_WARNING(fs_handle,
				  "dropped %lu refs on %u inodes",
				  refs, handles);
	} else {
		IOF_TRACE_INFO(fs_handle,
			       "dropped %lu refs on %u inodes",
			       refs, handles);
	}

	rc = d_hash_table_destroy_inplace(&fs_handle->inode_ht, false);
	if (rc) {
		IOF_TRACE_WARNING(fs_handle, "Failed to close inode handles");
		rcp = EINVAL;
	}

	/* This code does not need to hold the locks as the fuse progression
	 * thread is no longer running so no more calls to open()/opendir()
	 * or close()/releasedir() can race with this code.
	 */
	handles = 0;
	d_list_for_each_entry_safe(dh, dh2, &fs_handle->opendir_list, dh_od_list) {
		IOF_TRACE_INFO(fs_handle, "Closing directory " GAH_PRINT_STR
			       " %p", GAH_PRINT_VAL(dh->gah), dh);
		ioc_int_releasedir(dh);
		handles++;
	}
	IOF_TRACE_INFO(fs_handle, "Closed %d directory handles", handles);

	handles = 0;
	d_list_for_each_entry_safe(fh, fh2, &fs_handle->openfile_list,
				   fh_of_list) {
		IOF_TRACE_INFO(fs_handle, "Closing file " GAH_PRINT_STR
			       " %p", GAH_PRINT_VAL(fh->common.gah), fh);
		ioc_int_release(fh);
		handles++;
	}
	IOF_TRACE_INFO(fs_handle, "Closed %d file handles", handles);

	/* Stop the progress thread for this projection and delete the context
	 */

	for (i = 0; i < fs_handle->ctx_num; i++) {
		rc = iof_thread_stop(&fs_handle->ctx_array[i]);
		if (rc != 0)
			IOF_TRACE_ERROR(fs_handle,
					"thread[%d] stop returned %d", i, rc);
	}

	do {
		/* If this context has a pool associated with it then reap
		 * any descriptors with it so there are no pending RPCs when
		 * we call context_destroy.
		 */
		bool active;

		do {
			rc = iof_progress_drain(&fs_handle->ctx_array[0]);

			active = iof_pool_reclaim(&fs_handle->pool);

			if (!active)
				break;

			IOF_TRACE_INFO(fs_handle,
					  "Active descriptors, waiting for one second");

		} while (active && rc == -DER_SUCCESS);

		rc = crt_context_destroy(fs_handle->proj.crt_ctx, false);
		if (rc == -DER_BUSY)
			IOF_TRACE_INFO(fs_handle, "RPCs in flight, waiting");
		else if (rc != DER_SUCCESS)
			IOF_TRACE_ERROR(fs_handle,
					"Could not destroy context %d",
					rc);
	} while (rc == -DER_BUSY);

	if (rc != -DER_SUCCESS)
		IOF_TRACE_ERROR(fs_handle, "Count not destroy context");

	iof_pool_destroy(&fs_handle->pool);

	rc = pthread_mutex_destroy(&fs_handle->od_lock);
	if (rc != 0) {
		IOF_TRACE_ERROR(fs_handle,
				"Failed to destroy lock %d %s",
				rc, strerror(rc));
		rcp = rc;
	}

	rc = pthread_mutex_destroy(&fs_handle->of_lock);
	if (rc != 0) {
		IOF_TRACE_ERROR(fs_handle,
				"Failed to destroy lock %d %s",
				rc, strerror(rc));
		rcp = rc;
	}

	rc = pthread_mutex_destroy(&fs_handle->gah_lock);
	if (rc != 0) {
		IOF_TRACE_ERROR(fs_handle,
				"Failed to destroy lock %d %s",
				rc, strerror(rc));
		rcp = rc;
	}

	rc = pthread_mutex_destroy(&fs_handle->p_request_lock);
	if (rc != 0) {
		IOF_TRACE_ERROR(fs_handle,
				"Failed to destroy lock %d %s",
				rc, strerror(rc));
		rcp = rc;
	}

	for (i = 0; i < fs_handle->ctx_num; i++) {
		IOF_TRACE_DOWN(&fs_handle->ctx_array[i]);
	}
	d_list_del_init(&fs_handle->link);

	D_FREE(fs_handle->ctx_array);

	D_FREE(fs_handle->mount_point);

	D_FREE(fs_handle->stats);
	return rcp;
}

static void
detach_cb(const struct crt_cb_info *cb_info)
{
	struct iof_tracker *tracker = cb_info->cci_arg;

	if (cb_info->cci_rc != -DER_SUCCESS)
		IOF_TRACE_WARNING(cb_info->cci_rpc,
				"detach RPC failed %d",
				cb_info->cci_rc);

	iof_tracker_signal(tracker);
}

static void iof_finish(void *arg)
{
	struct iof_state *iof_state = arg;
	struct iof_group_info *group = &iof_state->group;
	int rc;
	struct iof_tracker tracker;
	crt_rpc_t *rpc = NULL;

	iof_tracker_init(&tracker, 1);

	if (!group->iof_registered) {
		iof_tracker_signal(&tracker);
		D_GOTO(tracker_stop, 0);
		return;
	}

	/*send a detach RPC to IONSS*/
	rc = crt_req_create(iof_state->iof_ctx.crt_ctx,
			    &group->grp.psr_ep,
			    CRT_PROTO_OPC(iof_state->handshake_proto->cpf_base,
					  iof_state->handshake_proto->cpf_ver,
					  1),
			    &rpc);
	if (rc != -DER_SUCCESS || !rpc) {
		IOF_TRACE_ERROR(iof_state,
				"Could not create detach req rc = %d",
				rc);
		iof_tracker_signal(&tracker);
		D_GOTO(tracker_stop, 0);
	}

	rc = crt_req_send(rpc, detach_cb, &tracker);
	if (rc != -DER_SUCCESS) {
		IOF_TRACE_ERROR(iof_state, "Detach RPC not sent");
		iof_tracker_signal(&tracker);
	}

tracker_stop:
	/* If an error occurred above, there will be no need to call
	 * progress
	 */
	if (!iof_tracker_test(&tracker))
		iof_wait(iof_state->iof_ctx.crt_ctx, &tracker);

	if (group->crt_attached) {
		rc = crt_group_detach(group->grp.dest_grp);
		if (rc != -DER_SUCCESS)
			IOF_TRACE_ERROR(iof_state, "crt_group_detach failed "
					"with rc = %d", rc);
	}

	/* Stop progress thread */
	rc = iof_thread_stop(&iof_state->iof_ctx);
	if (rc != 0)
		IOF_TRACE_ERROR(iof_state,
				"thread stop returned %d", rc);

	if (iof_state->iof_ctx.crt_ctx) {

		rc = iof_progress_drain(&iof_state->iof_ctx);
		if (rc != 0)
			IOF_TRACE_ERROR(iof_state,
					"could not drain context %d", rc);

		rc = crt_context_destroy(iof_state->iof_ctx.crt_ctx, false);
		if (rc != -DER_SUCCESS)
			IOF_TRACE_ERROR(iof_state, "Could not destroy context %d",
					rc);
		IOF_TRACE_DOWN(&iof_state->iof_ctx);
	}

	IOF_TRACE_DOWN(iof_state);
	D_FREE(iof_state);
}

struct cnss_plugin self = {.name		= "iof",
			   .version		= CNSS_PLUGIN_VERSION,
			   .require_service	= 0,
			   .start		= iof_reg,
			   .post_start		= iof_post_start,
			   .deregister_fuse	= iof_deregister_fuse,
			   .flush_fuse		= iof_flush_fuse,
			   .destroy_plugin_data	= iof_finish};

int iof_plugin_init(struct cnss_plugin **fns, size_t *size)
{
	struct iof_state *state;

	D_ALLOC_PTR(state);
	if (!state)
		return CNSS_ERR_NOMEM;

	*size = sizeof(struct cnss_plugin);

	self.handle = state;
	*fns = &self;
	return CNSS_SUCCESS;
}
