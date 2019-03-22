/* Copyright (C) 2016-2018 Intel Corporation
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
#include "ios_gah.h"

struct readdir_cb_r {
	crt_rpc_t *rpc;
	struct iof_tracker tracker;
	int err;
	struct iof_readdir_out *out;
};

/* The callback of the readdir RPC.
 *
 * All this function does is take a reference on the data and return.
 */
static void
readdir_cb(const struct crt_cb_info *cb_info)
{
	struct readdir_cb_r *reply = cb_info->cci_arg;

	if (cb_info->cci_rc != 0) {
		/* Error handling, as directory handles are stateful if there
		 * is any error then we have to disable the local dir_handle
		 *
		 */
		IOF_LOG_ERROR("Error from RPC %d", cb_info->cci_rc);
		if (cb_info->cci_rc == -DER_EVICTED)
			reply->err = EHOSTDOWN;
		else
			reply->err = EIO;
		iof_tracker_signal(&reply->tracker);
		return;
	}

	crt_req_addref(cb_info->cci_rpc);

	reply->out = crt_reply_get(cb_info->cci_rpc);
	reply->rpc = cb_info->cci_rpc;
	iof_tracker_signal(&reply->tracker);
}

/*
 * Send, and wait for a readdir() RPC.  Populate the dir_handle with the
 * replies, count and rpc which a reference is held on.
 *
 * If this function returns a non-zero status then that status is returned to
 * FUSE and the handle is marked as invalid.
 */
static int readdir_get_data(struct iof_dir_handle *dir_handle, off_t offset)
{
	struct iof_projection_info *fs_handle = dir_handle->open_req.fsh;
	struct iof_readdir_in *in;
	struct readdir_cb_r reply = {0};
	crt_rpc_t *rpc = NULL;
	crt_bulk_t bulk = 0;
	d_iov_t iov = {0};
	size_t len = fs_handle->readdir_size;
	int ret = 0;
	int rc;

	rc = crt_req_create(fs_handle->proj.crt_ctx, &dir_handle->ep,
			    FS_TO_OP(fs_handle, readdir), &rpc);
	if (rc || !rpc) {
		IOF_TRACE_ERROR(dir_handle,
				"Could not create request, rc = %d", rc);
		return EIO;
	}

	in = crt_req_get(rpc);
	D_MUTEX_LOCK(&fs_handle->gah_lock);
	in->gah = dir_handle->gah;
	D_MUTEX_UNLOCK(&fs_handle->gah_lock);
	in->offset = offset;

	iov.iov_len = len;
	iov.iov_buf_len = len;
	D_ALLOC(iov.iov_buf, len);

	if (iov.iov_buf) {
		d_sg_list_t sgl = {0};

		sgl.sg_iovs = &iov;
		sgl.sg_nr = 1;
		rc = crt_bulk_create(fs_handle->proj.crt_ctx, &sgl, CRT_BULK_RW,
				     &in->bulk);
		if (rc) {
			IOF_TRACE_ERROR(dir_handle,
					"Failed to make local bulk handle %d",
					rc);
			D_FREE(iov.iov_buf);
			crt_req_decref(rpc);
			D_GOTO(out, ret = EIO);
		}
		bulk = in->bulk;
	}

	iof_tracker_init(&reply.tracker, 1);
	rc = crt_req_send(rpc, readdir_cb, &reply);
	if (rc) {
		IOF_TRACE_ERROR(dir_handle,
				"Could not send rpc, rc = %d", rc);
		return EIO;
	}

	iof_fs_wait(&fs_handle->proj, &reply.tracker);

	if (reply.err != 0)
		D_GOTO(out, ret = reply.err);

	if (reply.out->err != 0) {
		if (reply.out->err == -DER_NONEXIST)
			H_GAH_SET_INVALID(dir_handle);
		IOF_TRACE_ERROR(dir_handle,
				"Error from target %d", reply.out->err);
		D_GOTO(out, ret = EIO);
	}

	IOF_TRACE_DEBUG(dir_handle,
			"Reply received iov: %d bulk: %d", reply.out->iov_count,
			reply.out->bulk_count);

	if (reply.out->iov_count > 0) {
		dir_handle->reply_count = reply.out->iov_count;

		if (reply.out->replies.iov_len != reply.out->iov_count *
			sizeof(struct iof_readdir_reply)) {
			IOF_TRACE_ERROR(dir_handle, "Incorrect iov reply");
			D_GOTO(out, ret = EIO);
		}
		dir_handle->replies = reply.out->replies.iov_buf;
		dir_handle->rpc = reply.rpc;
		dir_handle->last_replies = reply.out->last;
		goto out_with_rpc;
	} else if (reply.out->bulk_count > 0) {
		dir_handle->reply_count = reply.out->bulk_count;
		dir_handle->last_replies = reply.out->last;
		dir_handle->replies = iov.iov_buf;
		dir_handle->rpc = NULL;
		dir_handle->replies_base = iov.iov_buf;
	} else {
		dir_handle->reply_count = 0;
		dir_handle->replies = NULL;
		dir_handle->rpc = NULL;
	}

out:
	if (reply.rpc)
		crt_req_decref(reply.rpc);

out_with_rpc:
	if (iov.iov_buf && iov.iov_buf != dir_handle->replies)
		D_FREE(iov.iov_buf);

	if (bulk) {
		rc = crt_bulk_free(bulk);
		if (rc)
			ret = EIO;
	}

	return ret;
}

/* Mark a previously fetched handle complete
 *
 * Returns True if the consumed entry is the last one.
 */
static int readdir_next_reply_consume(struct iof_dir_handle *dir_handle)
{
	if (dir_handle->reply_count != 0) {
		dir_handle->replies++;
		dir_handle->reply_count--;
	}

	if (dir_handle->reply_count == 0) {
		if (dir_handle->rpc) {
			crt_req_decref(dir_handle->rpc);
			dir_handle->rpc = NULL;
		} else if (dir_handle->replies_base) {
			D_FREE(dir_handle->replies_base);
		}
	}
	if (dir_handle->reply_count == 0 && dir_handle->last_replies)
		return 1;
	return 0;
}

/* Fetch a pointer to the next reply entry from the target
 *
 * Replies are read from the server in batches, configurable on the server side,
 * the client keeps a array of received but unprocessed replies.  This function
 * fetches a new reply if possible, either from the from the front of the local
 * array, or if the array is empty by sending a new RPC.
 *
 * If this function returns a non-zero status then that status is returned to
 * FUSE and the handle is marked as invalid.
 *
 * There is no caching on the server, and when the server responds to a RPC it
 * can include zero or more replies.
 */
static int readdir_next_reply(struct iof_dir_handle *dir_handle,
			      off_t offset,
			      struct iof_readdir_reply **reply)
{
	int rc;

	*reply = NULL;

	/* Check for available data and fetch more if none */
	if (dir_handle->reply_count == 0) {
		IOF_TRACE_DEBUG(dir_handle, "Fetching more data");
		if (dir_handle->rpc) {
			crt_req_decref(dir_handle->rpc);
			dir_handle->rpc = NULL;
		}
		rc = readdir_get_data(dir_handle, offset);
		if (rc != 0) {
			dir_handle->handle_valid = 0;
			return rc;
		}
	}

	if (dir_handle->reply_count == 0) {
		IOF_TRACE_DEBUG(dir_handle, "No more replies");
		if (dir_handle->rpc) {
			crt_req_decref(dir_handle->rpc);
			dir_handle->rpc = NULL;
		}
		return 0;
	}

	*reply = dir_handle->replies;

	IOF_TRACE_INFO(dir_handle,
		       "Next offset %zi count %d %s",
		       (*reply)->nextoff,
		       dir_handle->reply_count,
		       dir_handle->last_replies ? "EOF" : "More");

	return 0;
}

void
ioc_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t offset,
	       struct fuse_file_info *fi)
{
	struct iof_dir_handle *dir_handle = (struct iof_dir_handle *)fi->fh;
	struct iof_projection_info *fs_handle = dir_handle->open_req.fsh;
	off_t next_offset = offset;
	void *buf = NULL;
	size_t b_offset = 0;
	int ret = EIO;
	int rc;

	STAT_ADD(fs_handle->stats, readdir);

	IOF_TRACE_UP(req, dir_handle, "readdir_fuse_req");

	if (FS_IS_OFFLINE(fs_handle))
		D_GOTO(out_err, ret = fs_handle->offline_reason);

	IOF_TRACE_INFO(req, GAH_PRINT_STR " offset %zi",
		       GAH_PRINT_VAL(dir_handle->gah), offset);

	if (!H_GAH_IS_VALID(dir_handle))
		/* If the server has reported that the GAH is invalid
		 * then do not send a RPC to close it
		 */
		D_GOTO(out_err, ret = EHOSTDOWN);

	/* If the handle has been reported as invalid in the past then do not
	 * process any more requests at this stage.
	 */
	if (!dir_handle->handle_valid)
		D_GOTO(out_err, ret = EHOSTDOWN);

	D_ALLOC(buf, size);
	if (!buf)
		D_GOTO(out_err, ret = ENOMEM);

	do {
		struct iof_readdir_reply *dir_reply;

		rc = readdir_next_reply
			(dir_handle, next_offset,
					 &dir_reply);

		IOF_TRACE_DEBUG(dir_handle, "err %d buf %p", rc, dir_reply);

		if (rc != 0)
			D_GOTO(out_err, ret = rc);

		/* Check for end of directory.  This is the code-path taken
		 * where a RPC contains 0 replies, either because a directory
		 * is empty, or where the number of entries fits exactly in
		 * the last RPC.
		 * In this case there is no next entry to consume.
		 */
		if (!dir_reply) {
			IOF_TRACE_INFO(dir_handle,
				       "No more directory contents");
			goto out;
		}

		IOF_TRACE_DEBUG(dir_handle, "reply rc %d stat_rc %d",
				dir_reply->read_rc,
				dir_reply->stat_rc);

		/* Check for error.  Error on the remote readdir() call exits
		 * here
		 */
		if (dir_reply->read_rc != 0) {
			ret = dir_reply->read_rc;
			readdir_next_reply_consume(dir_handle);
			goto out_err;
		}

		/* Process any new information received in this RPC.  The
		 * server will have returned a directory entry name and
		 * possibly a struct stat.
		 *
		 * POSIX: If the directory has been renamed since the opendir()
		 * call and before the readdir() then the remote stat() may
		 * have failed so check for that here.
		 */

		if (dir_reply->stat_rc != 0) {
			IOF_TRACE_ERROR(req, "Stat rc is non-zero");
			D_GOTO(out_err, ret = EIO);
		}

		ret = fuse_add_direntry(req, buf + b_offset, size - b_offset,
					dir_reply->d_name,
					&dir_reply->stat,
					dir_reply->nextoff);

		IOF_TRACE_DEBUG(dir_handle,
				"New file '%s' %d next off %zi size %d (%lu)",
				dir_reply->d_name, ret, dir_reply->nextoff,
				ret,
				size - b_offset);

		/* Check for this being the last entry in a directory, this is
		 * the typical end-of-directory case where readdir() returned
		 * no more information on the sever
		 */

		if (ret > size - b_offset) {
			IOF_TRACE_DEBUG(req,
					"Output buffer is full");
			goto out;
		}

		next_offset = dir_reply->nextoff;
		readdir_next_reply_consume(dir_handle);
		b_offset += ret;

	} while (1);

out:
	IOF_TRACE_DEBUG(req, "Returning %zi bytes", b_offset);

	rc = fuse_reply_buf(req, buf, b_offset);
	if (rc != 0)
		IOF_TRACE_ERROR(req, "fuse_reply_error returned %d", rc);

	IOF_TRACE_DOWN(req);

	D_FREE(buf);
	return;

out_err:
	IOF_FUSE_REPLY_ERR(req, ret);

	D_FREE(buf);
}
