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

#include <fuse3/fuse_lowlevel.h>

#include "dfuse_common.h"
#include "dfuse.h"

#define SHOW_FLAG(HANDLE, FLAGS, FLAG)					\
	do {								\
		IOF_TRACE_INFO(HANDLE, "Flag " #FLAG " %s",		\
			(FLAGS & FLAG) ? "enabled" : "disabled");	\
		FLAGS &= ~FLAG;						\
	} while (0)

static void
ioc_show_flags(void *handle, unsigned int in)
{
	SHOW_FLAG(handle, in, FUSE_CAP_ASYNC_READ);
	SHOW_FLAG(handle, in, FUSE_CAP_POSIX_LOCKS);
	SHOW_FLAG(handle, in, FUSE_CAP_ATOMIC_O_TRUNC);
	SHOW_FLAG(handle, in, FUSE_CAP_EXPORT_SUPPORT);
	SHOW_FLAG(handle, in, FUSE_CAP_DONT_MASK);
	SHOW_FLAG(handle, in, FUSE_CAP_SPLICE_WRITE);
	SHOW_FLAG(handle, in, FUSE_CAP_SPLICE_MOVE);
	SHOW_FLAG(handle, in, FUSE_CAP_SPLICE_READ);
	SHOW_FLAG(handle, in, FUSE_CAP_FLOCK_LOCKS);
	SHOW_FLAG(handle, in, FUSE_CAP_IOCTL_DIR);
	SHOW_FLAG(handle, in, FUSE_CAP_AUTO_INVAL_DATA);
	SHOW_FLAG(handle, in, FUSE_CAP_READDIRPLUS);
	SHOW_FLAG(handle, in, FUSE_CAP_READDIRPLUS_AUTO);
	SHOW_FLAG(handle, in, FUSE_CAP_ASYNC_DIO);
	SHOW_FLAG(handle, in, FUSE_CAP_WRITEBACK_CACHE);
	SHOW_FLAG(handle, in, FUSE_CAP_NO_OPEN_SUPPORT);
	SHOW_FLAG(handle, in, FUSE_CAP_PARALLEL_DIROPS);
	SHOW_FLAG(handle, in, FUSE_CAP_POSIX_ACL);
	SHOW_FLAG(handle, in, FUSE_CAP_HANDLE_KILLPRIV);

	if (in)
		IOF_TRACE_ERROR(handle, "Unknown flags %#x", in);
}

/* Called on filesystem init.  It has the ability to both observe configuration
 * options, but also to modify them.  As we do not use the FUSE command line
 * parsing this is where we apply tunables.
 */
static void
ioc_fuse_init(void *arg, struct fuse_conn_info *conn)
{
	struct iof_projection_info *fs_handle = arg;

	IOF_TRACE_INFO(fs_handle,
		       "Fuse configuration for projection srv:%d cli:%d",
		       fs_handle->fs_id, fs_handle->proj.cli_fs_id);

	IOF_TRACE_INFO(fs_handle, "Proto %d %d",
		       conn->proto_major, conn->proto_minor);

	/* This value has to be set here to the same value passed to
	 * register_fuse().  Fuse always sets this value to zero so
	 * set it before reporting the value.
	 */
	conn->max_read = fs_handle->max_read;
	conn->max_write = fs_handle->proj.max_write;

	IOF_TRACE_INFO(fs_handle, "max read %#x", conn->max_read);
	IOF_TRACE_INFO(fs_handle, "max write %#x", conn->max_write);
	IOF_TRACE_INFO(fs_handle, "readahead %#x", conn->max_readahead);

	IOF_TRACE_INFO(fs_handle, "Capability supported %#x", conn->capable);

	ioc_show_flags(fs_handle, conn->capable);

	/* This does not work as ioctl.c assumes fi->fh is a file handle */
	conn->want &= ~FUSE_CAP_IOCTL_DIR;

	IOF_TRACE_INFO(fs_handle, "Capability requested %#x", conn->want);

	ioc_show_flags(fs_handle, conn->want);

	IOF_TRACE_INFO(fs_handle, "max_background %d", conn->max_background);
	IOF_TRACE_INFO(fs_handle,
		       "congestion_threshold %d", conn->congestion_threshold);
}

static void ioc_fuse_destroy(void *userdata)
{
	IOF_TRACE_INFO(userdata, "destroy callback");
	IOF_TRACE_DOWN(userdata);
	D_FREE(userdata);
}

struct fuse_lowlevel_ops *iof_get_fuse_ops(uint64_t flags)
{
	struct fuse_lowlevel_ops *fuse_ops;

	D_ALLOC_PTR(fuse_ops);
	if (!fuse_ops)
		return NULL;

	fuse_ops->init = ioc_fuse_init;
	fuse_ops->getattr = ioc_ll_getattr;
	fuse_ops->lookup = ioc_ll_lookup;
	fuse_ops->forget = ioc_ll_forget;
	fuse_ops->forget_multi = ioc_ll_forget_multi;
	fuse_ops->statfs = ioc_ll_statfs;
	fuse_ops->readlink = ioc_ll_readlink;
	fuse_ops->open = ioc_ll_open;
	fuse_ops->read = ioc_ll_read;
	fuse_ops->release = ioc_ll_release;
	fuse_ops->opendir = ioc_ll_opendir;
	fuse_ops->releasedir = ioc_ll_releasedir;
	fuse_ops->readdir = ioc_ll_readdir;
	fuse_ops->ioctl = ioc_ll_ioctl;
	fuse_ops->destroy = ioc_fuse_destroy;
	fuse_ops->symlink = ioc_ll_symlink;
	fuse_ops->mkdir = ioc_ll_mkdir;
	fuse_ops->unlink = ioc_ll_unlink;
	fuse_ops->write = ioc_ll_write;
	fuse_ops->rmdir = ioc_ll_rmdir;
	fuse_ops->create = ioc_ll_create;
	fuse_ops->setattr = ioc_ll_setattr;
	fuse_ops->rename = ioc_ll_rename;
	fuse_ops->fsync = ioc_ll_fsync;
	fuse_ops->write = ioc_ll_write;

	if (flags & IOF_FUSE_WRITE_BUF)
		fuse_ops->write_buf = ioc_ll_write_buf;

	return fuse_ops;
}
