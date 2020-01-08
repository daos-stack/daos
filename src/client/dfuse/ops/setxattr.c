/**
 * (C) Copyright 2019 Intel Corporation.
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

/* Check if buffer contains a UUID
 */
static bool
check_uuid(const char *value, size_t size)
{
	char uuid_str[40] = {};
	uuid_t uuid;

	if (size != 36)
		return false;

	/* Make a copy of the string so it's NULL terminated otherwise
	 * uuid_parse will throw an error because of the missing \0
	 */
	strncpy(uuid_str, value, size);

	DFUSE_TRA_DEBUG(NULL, "%zi '%s'", size, uuid_str);

	if (uuid_parse(uuid_str, uuid) < 0)
		return false;

	return true;
}

/* Check if pool uuid is set correctly
 *
 * To do this check the size of the pool attr, then check
 * it's a valid uuid.
 */
static bool
check_uns_attr(struct dfuse_inode_entry *inode)
{
	char uuid_str[40] = {};
	size_t size = 40;
	int rc;

	rc = dfs_getxattr(inode->ie_dfs->dfs_ns, inode->ie_obj,
			  DFUSE_UNS_POOL_ATTR, &uuid_str, &size);

	if (rc || size != 36)
		return false;

	if (!check_uuid(uuid_str, size)) {
		DFUSE_TRA_DEBUG(inode, "pool attr failed check");
		return false;
	}

	return true;
}

void
dfuse_cb_setxattr(fuse_req_t req, struct dfuse_inode_entry *inode,
		  const char *name, const char *value, size_t size,
		  int flags)
{
	int rc;

	DFUSE_TRA_DEBUG(inode, "Attribute '%s'", name);

	if (strcmp(name, DFUSE_UNS_POOL_ATTR) == 0) {
		if (!check_uuid(value, size))
			D_GOTO(err, rc = EINVAL);
	}

	if (strcmp(name, DFUSE_UNS_CONTAINER_ATTR) == 0) {
		if (!check_uuid(value, size))
			D_GOTO(err, rc = EINVAL);

		if (!check_uns_attr(inode))
			D_GOTO(err, rc = EINVAL);
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
