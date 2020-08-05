/**
 * (C) Copyright 2019-2020 Intel Corporation.
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

#include "daos_uns.h"

void
dfuse_cb_setxattr(fuse_req_t req, struct dfuse_inode_entry *inode,
		  const char *name, const char *value, size_t size,
		  int flags)
{
	int rc;

	DFUSE_TRA_DEBUG(inode, "Attribute '%s'", name);

	if (strcmp(name, DUNS_XATTR_NAME) == 0) {
		struct duns_attr_t	dattr = {};

		rc = duns_parse_attr((char *)value, size, &dattr);
		if (rc)
			D_GOTO(err, rc);

		if (dattr.da_type != DAOS_PROP_CO_LAYOUT_POSIX &&
			dattr.da_type != DAOS_PROP_CO_LAYOUT_HDF5)
			D_GOTO(err, rc = ENOTSUP);
	}

	rc = dfs_setxattr(inode->ie_dfs->dfs_ns, inode->ie_obj, name, value,
			  size, flags);
	if (rc == 0) {
		DFUSE_REPLY_ZERO(inode, req);
		return;
	}
err:
	DFUSE_REPLY_ERR_RAW(inode, req, rc);
}
