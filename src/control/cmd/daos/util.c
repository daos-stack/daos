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
	char   *name = NULL, *dir_name = NULL;

	rc = duns_resolve_path(ap->path, &dattr);
	if (rc == ENOENT && ap->fs_op == FS_SET_ATTR) {
		/** we could be creating a new file, so try dirname */
		parse_filename_dfs(ap->path, &name, &dir_name);

		rc = duns_resolve_path(dir_name, &dattr);
	}

	if (rc && ap->fs_op == -1) {
		rc = call_dfuse_ioctl(ap->path, &il_reply);
		if (rc == 0) {
			ap->type = DAOS_PROP_CO_LAYOUT_POSIX;
			uuid_copy(ap->p_uuid, il_reply.fir_pool);
			uuid_copy(ap->c_uuid, il_reply.fir_cont);
			ap->oid = il_reply.fir_oid;
			D_GOTO(out, rc);
		}
	}

	if (rc) {
		fprintf(ap->errstream, "could not resolve pool, container by "
		"path: %d %s %s\n", rc, strerror(rc), ap->path);
		D_GOTO(out, rc);
	}

	ap->type = dattr.da_type;

	/** set pool/cont label or uuid */
	if (dattr.da_pool_label) {
		D_STRNDUP(ap->pool_label, dattr.da_pool_label,
			  DAOS_PROP_LABEL_MAX_LEN);
		if (ap->pool_label == NULL)
			D_GOTO(out, rc = ENOMEM);
	} else {
		uuid_copy(ap->p_uuid, dattr.da_puuid);
	}

	if (dattr.da_cont_label) {
		D_STRNDUP(ap->cont_label, dattr.da_cont_label,
			  DAOS_PROP_LABEL_MAX_LEN);
		if (ap->cont_label == NULL)
			D_GOTO(out, rc = ENOMEM);
	} else {
		uuid_copy(ap->c_uuid, dattr.da_cuuid);
	}

	if (ap->fs_op != -1) {
		if (name) {
			if (dattr.da_rel_path) {
				asprintf(&ap->dfs_path, "%s/%s",
					 dattr.da_rel_path, name);
			} else {
				asprintf(&ap->dfs_path, "/%s", name);
			}
		} else {
			if (dattr.da_rel_path) {
				ap->dfs_path = strndup(dattr.da_rel_path, PATH_MAX);
			} else {
				ap->dfs_path = strndup("/", 1);
			}
		}
		if (ap->dfs_path == NULL)
			D_GOTO(out, rc = ENOMEM);
	}

out:
	duns_destroy_attr(&dattr);
	D_FREE(dir_name);
	D_FREE(name);
	return rc;
}
