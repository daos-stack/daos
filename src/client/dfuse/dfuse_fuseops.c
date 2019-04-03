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
dfuse_show_flags(void *handle, unsigned int in)
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
dfuse_fuse_init(void *arg, struct fuse_conn_info *conn)
{
	struct dfuse_projection_info *fs_handle = arg;

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

	dfuse_show_flags(fs_handle, conn->capable);

	/* This does not work as ioctl.c assumes fi->fh is a file handle */
	conn->want &= ~FUSE_CAP_IOCTL_DIR;

	IOF_TRACE_INFO(fs_handle, "Capability requested %#x", conn->want);

	dfuse_show_flags(fs_handle, conn->want);

	IOF_TRACE_INFO(fs_handle, "max_background %d", conn->max_background);
	IOF_TRACE_INFO(fs_handle,
		       "congestion_threshold %d", conn->congestion_threshold);
}

static void dfuse_fuse_destroy(void *userdata)
{
	IOF_TRACE_INFO(userdata, "destroy callback");
	IOF_TRACE_DOWN(userdata);
	D_FREE(userdata);
}

struct fuse_lowlevel_ops *dfuse_get_fuse_ops(uint64_t flags)
{
	struct fuse_lowlevel_ops *fuse_ops;

	D_ALLOC_PTR(fuse_ops);
	if (!fuse_ops)
		return NULL;

	fuse_ops->init = dfuse_fuse_init;
	fuse_ops->getattr = dfuse_cb_getattr;
	fuse_ops->lookup = dfuse_cb_lookup;
	fuse_ops->forget = dfuse_cb_forget;
	fuse_ops->forget_multi = dfuse_cb_forget_multi;
	fuse_ops->statfs = dfuse_cb_statfs;
	fuse_ops->readlink = dfuse_cb_readlink;
	fuse_ops->open = dfuse_cb_open;
	fuse_ops->read = dfuse_cb_read;
	fuse_ops->release = dfuse_cb_release;
	fuse_ops->opendir = dfuse_cb_opendir;
	fuse_ops->releasedir = dfuse_cb_releasedir;
	fuse_ops->readdir = dfuse_cb_readdir;
	fuse_ops->ioctl = dfuse_cb_ioctl;
	fuse_ops->destroy = dfuse_fuse_destroy;
	fuse_ops->symlink = dfuse_cb_symlink;
	fuse_ops->mkdir = dfuse_cb_mkdir;
	fuse_ops->unlink = dfuse_cb_unlink;
	fuse_ops->write = dfuse_cb_write;
	fuse_ops->rmdir = dfuse_cb_rmdir;
	fuse_ops->create = dfuse_cb_create;
	fuse_ops->setattr = dfuse_cb_setattr;
	fuse_ops->rename = dfuse_cb_rename;
	fuse_ops->fsync = dfuse_cb_fsync;
	fuse_ops->write = dfuse_cb_write;

	if (flags & IOF_FUSE_WRITE_BUF)
		fuse_ops->write_buf = dfuse_cb_write_buf;

	return fuse_ops;
}
