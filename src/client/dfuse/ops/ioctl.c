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

#include <sys/ioctl.h>

#include "dfuse_ioctl.h"

static void
handle_gah_ioctl(struct dfuse_obj_hdl *oh, fuse_req_t req)
{
	struct dfuse_il_reply	il_reply = {0};
	int			rc;

	DFUSE_TRA_INFO(oh, "Requested");

	/* TODO: Check rc */;
	rc = dfs_obj2id(oh->doh_ie->ie_obj, &il_reply.fir_oid);
	if (rc)
		D_GOTO(err, rc);

	il_reply.fir_version = DFUSE_IOCTL_VERSION;

	if (uuid_parse(oh->doh_ie->ie_dfs->dfs_pool, il_reply.fir_pool) < 0)
		D_GOTO(err, rc = EIO);

	if (uuid_parse(oh->doh_ie->ie_dfs->dfs_cont, il_reply.fir_cont) < 0)
		D_GOTO(err, rc = EIO);

	DFUSE_REPLY_IOCTL(oh, req, il_reply);
	return;
err:
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
}

void dfuse_cb_ioctl(fuse_req_t req, fuse_ino_t ino, unsigned int cmd, void *arg,
		    struct fuse_file_info *fi, unsigned int flags,
		    const void *in_buf, size_t in_bufsz, size_t out_bufsz)
{
	struct dfuse_obj_hdl	*oh = (struct dfuse_obj_hdl *)fi->fh;
	int			rc;

	DFUSE_TRA_INFO(oh, "ioctl cmd=%#x", cmd);

	if (cmd == TCGETS) {
		DFUSE_TRA_DEBUG(oh, "Ignoring TCGETS ioctl");
		D_GOTO(out_err, rc = ENOTTY);
	}

	if (cmd != DFUSE_IOCTL_GAH) {
		DFUSE_TRA_INFO(oh, "Real ioctl support is not implemented");
		D_GOTO(out_err, rc = ENOTSUP);
	}

	if (out_bufsz < sizeof(struct dfuse_il_reply))
		D_GOTO(out_err, rc = EIO);

	handle_gah_ioctl(oh, req);

	return;

out_err:
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
}
