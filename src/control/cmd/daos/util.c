/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "util.h"

static int
call_dfuse_ioctl(char *path, struct dfuse_il_reply *reply)
{
	int fd;
	int rc;

	fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (fd < 0)
		return ENOENT;

	errno = 0;
	rc = ioctl(fd, DFUSE_IOCTL_IL, reply);
	if (rc != 0) {
		int err = errno;

		close(fd);
		return err;
	}
	close(fd);

	if (reply->fir_version != DFUSE_IOCTL_VERSION)
		return EIO;

	return 0;
}

int
resolve_duns_path(struct cmd_args_s *ap)
{
	int rc = 0;
	struct duns_attr_t dattr = {0};
	struct dfuse_il_reply il_reply = {0};

	rc = duns_resolve_path(ap->path, &dattr);
	if (rc) {
		rc = call_dfuse_ioctl(ap->path, &il_reply);
		if (rc != 0) {
			fprintf(ap->errstream, "could not resolve "
			"pool, container by "
			"path: %d %s %s\n",
			rc, strerror(rc), ap->path);

			return rc;
		}

		ap->type = DAOS_PROP_CO_LAYOUT_POSIX;
		uuid_copy(ap->p_uuid, il_reply.fir_pool);
		uuid_copy(ap->c_uuid, il_reply.fir_cont);
		ap->oid = il_reply.fir_oid;
	} else {
		ap->type = dattr.da_type;
		uuid_copy(ap->p_uuid, dattr.da_puuid);
		uuid_copy(ap->c_uuid, dattr.da_cuuid);
	}		

	return 0;
}