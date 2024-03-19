/**
 * (C) Copyright 2019-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

#include <daos_uns.h>

static int
_dfuse_attr_create(char *type, uuid_t pool, uuid_t cont, char **_value, daos_size_t *_out_size)
{
	char *value;
	char  pool_str[37];
	char  cont_str[37];

	uuid_unparse(pool, pool_str);
	uuid_unparse(cont, cont_str);

	D_ASPRINTF(value, DUNS_XATTR_FMT, type, pool_str, cont_str);
	if (value == NULL)
		return ENOMEM;

	*_out_size = strnlen(value, DUNS_MAX_XATTR_LEN);

	*_value = value;

	return 0;
}

void
dfuse_cb_getxattr(fuse_req_t req, struct dfuse_inode_entry *inode, const char *name, size_t size)
{
	size_t out_size = 0;
	char  *value    = NULL;
	int    rc;

	DFUSE_TRA_DEBUG(inode, "Attribute '%s' size %#lx", name, size);

	if (inode->ie_root) {
		if (strncmp(name, DUNS_XATTR_NAME, sizeof(DUNS_XATTR_NAME)) == 0) {
			rc = _dfuse_attr_create("POSIX", inode->ie_dfs->dfs_dfp->dfp_uuid,
						inode->ie_dfs->dfc_uuid, &value, &out_size);
			if (rc != 0)
				goto err;

			if (size == 0) {
				DFUSE_REPLY_XATTR(inode, req, out_size);
				D_FREE(value);
				return;
			}

			if (size < out_size)
				D_GOTO(free, rc = ERANGE);

			goto reply;
		}
	}

	rc = dfs_getxattr(inode->ie_dfs->dfs_ns, inode->ie_obj, name, NULL, &out_size);
	if (rc != 0)
		D_GOTO(err, rc);

	if (size == 0) {
		DFUSE_REPLY_XATTR(inode, req, out_size);
		return;
	}

	if (size < out_size)
		D_GOTO(err, rc = ERANGE);

	D_ALLOC(value, out_size);
	if (!value)
		D_GOTO(err, rc = ENOMEM);

	rc = dfs_getxattr(inode->ie_dfs->dfs_ns, inode->ie_obj, name, value, &out_size);
	if (rc != 0)
		D_GOTO(free, rc);

reply:
	DFUSE_REPLY_BUF(inode, req, value, out_size);
	D_FREE(value);
	return;
free:
	D_FREE(value);
err:
	DFUSE_REPLY_ERR_RAW(inode, req, rc);
}
