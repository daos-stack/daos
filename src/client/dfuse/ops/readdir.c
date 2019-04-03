/**
 * (C) Copyright 2016-2019 Intel Corporation.
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

#include "dfuse_common.h"
#include "dfuse.h"

struct readdir_cb_r {
	crt_rpc_t *rpc;
	struct dfuse_tracker tracker;
	int err;
	struct dfuse_readdir_out *out;
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
		DFUSE_LOG_ERROR("Error from RPC %d", cb_info->cci_rc);
		if (cb_info->cci_rc == -DER_EVICTED)
			reply->err = EHOSTDOWN;
		else
			reply->err = EIO;
		dfuse_tracker_signal(&reply->tracker);
		return;
	}

	crt_req_addref(cb_info->cci_rpc);

	reply->out = crt_reply_get(cb_info->cci_rpc);
	reply->rpc = cb_info->cci_rpc;
	dfuse_tracker_signal(&reply->tracker);
}

/*
 * Send, and wait for a readdir() RPC.  Populate the dir_handle with the
 * replies, count and rpc which a reference is held on.
 *
 * If this function returns a non-zero status then that status is returned to
 * FUSE and the handle is marked as invalid.
 */
static int
readdir_get_data(struct dfuse_dir_handle *dir_handle, off_t offset)
{
	struct dfuse_projection_info *fs_handle = dir_handle->open_req.fsh;
	struct dfuse_readdir_in *in;
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
		DFUSE_TRA_ERROR(dir_handle,
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
			DFUSE_TRA_ERROR(dir_handle,
					"Failed to make local bulk handle %d",
					rc);
			D_FREE(iov.iov_buf);
			crt_req_decref(rpc);
			D_GOTO(out, ret = EIO);
		}
		bulk = in->bulk;
	}

	dfuse_tracker_init(&reply.tracker, 1);
	rc = crt_req_send(rpc, readdir_cb, &reply);
	if (rc) {
		DFUSE_TRA_ERROR(dir_handle,
				"Could not send rpc, rc = %d", rc);
		return EIO;
	}

	dfuse_fs_wait(&fs_handle->proj, &reply.tracker);

	if (reply.err != 0)
		D_GOTO(out, ret = reply.err);

	if (reply.out->err != 0) {
		DFUSE_TRA_ERROR(dir_handle,
				"Error from target %d", reply.out->err);
		D_GOTO(out, ret = EIO);
	}

	DFUSE_TRA_DEBUG(dir_handle,
			"Reply received iov: %d bulk: %d", reply.out->iov_count,
			reply.out->bulk_count);

	if (reply.out->iov_count > 0) {
		dir_handle->reply_count = reply.out->iov_count;

		if (reply.out->replies.iov_len != reply.out->iov_count *
			sizeof(struct dfuse_readdir_reply)) {
			DFUSE_TRA_ERROR(dir_handle, "Incorrect iov reply");
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
static int
readdir_next_reply_consume(struct dfuse_dir_handle *dir_handle)
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
static int
readdir_next_reply(struct dfuse_dir_handle *dir_handle, off_t offset,
		   struct dfuse_readdir_reply **reply)
{
	int rc;

	*reply = NULL;

	/* Check for available data and fetch more if none */
	if (dir_handle->reply_count == 0) {
		DFUSE_TRA_DEBUG(dir_handle, "Fetching more data");
		if (dir_handle->rpc) {
			crt_req_decref(dir_handle->rpc);
			dir_handle->rpc = NULL;
		}
		rc = readdir_get_data(dir_handle, offset);
		if (rc != 0) {
			return rc;
		}
	}

	if (dir_handle->reply_count == 0) {
		DFUSE_TRA_DEBUG(dir_handle, "No more replies");
		if (dir_handle->rpc) {
			crt_req_decref(dir_handle->rpc);
			dir_handle->rpc = NULL;
		}
		return 0;
	}

	*reply = dir_handle->replies;

	DFUSE_TRA_INFO(dir_handle,
		       "Next offset %zi count %d %s",
		       (*reply)->nextoff,
		       dir_handle->reply_count,
		       dir_handle->last_replies ? "EOF" : "More");

	return 0;
}

void
dfuse_cb_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t offset,
		 struct fuse_file_info *fi)
{
	struct dfuse_dir_handle *dir_handle = (struct dfuse_dir_handle *)fi->fh;
	off_t next_offset = offset;
	void *buf = NULL;
	size_t b_offset = 0;
	int ret = EIO;
	int rc;

	DFUSE_TRA_UP(req, dir_handle, "readdir_fuse_req");

	DFUSE_TRA_INFO(req, GAH_PRINT_STR " offset %zi",
		       GAH_PRINT_VAL(dir_handle->gah), offset);

	D_ALLOC(buf, size);
	if (!buf)
		D_GOTO(out_err, ret = ENOMEM);

	do {
		struct dfuse_readdir_reply *dir_reply;

		rc = readdir_next_reply
			(dir_handle, next_offset,
					 &dir_reply);

		DFUSE_TRA_DEBUG(dir_handle, "err %d buf %p", rc, dir_reply);

		if (rc != 0)
			D_GOTO(out_err, ret = rc);

		/* Check for end of directory.  This is the code-path taken
		 * where a RPC contains 0 replies, either because a directory
		 * is empty, or where the number of entries fits exactly in
		 * the last RPC.
		 * In this case there is no next entry to consume.
		 */
		if (!dir_reply) {
			DFUSE_TRA_INFO(dir_handle,
				       "No more directory contents");
			goto out;
		}

		DFUSE_TRA_DEBUG(dir_handle, "reply rc %d stat_rc %d",
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
			DFUSE_TRA_ERROR(req, "Stat rc is non-zero");
			D_GOTO(out_err, ret = EIO);
		}

		ret = fuse_add_direntry(req, buf + b_offset, size - b_offset,
					dir_reply->d_name,
					&dir_reply->stat,
					dir_reply->nextoff);

		DFUSE_TRA_DEBUG(dir_handle,
				"New file '%s' %d next off %zi size %d (%lu)",
				dir_reply->d_name, ret, dir_reply->nextoff,
				ret,
				size - b_offset);

		/* Check for this being the last entry in a directory, this is
		 * the typical end-of-directory case where readdir() returned
		 * no more information on the sever
		 */

		if (ret > size - b_offset) {
			DFUSE_TRA_DEBUG(req,
					"Output buffer is full");
			goto out;
		}

		next_offset = dir_reply->nextoff;
		readdir_next_reply_consume(dir_handle);
		b_offset += ret;

	} while (1);

out:
	DFUSE_TRA_DEBUG(req, "Returning %zi bytes", b_offset);

	rc = fuse_reply_buf(req, buf, b_offset);
	if (rc != 0)
		DFUSE_TRA_ERROR(req, "fuse_reply_error returned %d", rc);

	DFUSE_TRA_DOWN(req);

	D_FREE(buf);
	return;

out_err:
	DFUSE_FUSE_REPLY_ERR(req, ret);

	D_FREE(buf);
}
