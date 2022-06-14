/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * daos_dfs_hdlr.c - handler function for dfs ops (set/get chunk size, etc.)
 * invoked by daos(8) utility
 */

#define D_LOGFAC	DD_FAC(client)

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <libgen.h>
#include <daos.h>
#include <daos/common.h>
#include <daos/debug.h>

#include "daos_types.h"
#include "daos_fs.h"
#include "dfuse_ioctl.h"
#include "daos_uns.h"
#include "daos_hdlr.h"

static int
call_dfuse_ioctl(char *path, struct dfuse_il_reply *reply)
{
	int fd;
	int rc;

	fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (fd < 0)
		return errno;

	errno = 0;
	rc = ioctl(fd, DFUSE_IOCTL_IL, reply);
	if (rc != 0) {
		int err = errno;

		close(fd);
		return err;
	}
	close(fd);

	if (reply->fir_version != DFUSE_IOCTL_VERSION)
		return -EIO;

	return 0;
}

int
fs_dfs_resolve_path(struct cmd_args_s *ap)
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

			/** set pool/cont label or uuid */
			uuid_unparse(ap->p_uuid, ap->pool_str);
			uuid_unparse(ap->c_uuid, ap->cont_str);

			ap->oid = il_reply.fir_oid;
			D_GOTO(out, rc);
		}
	}

	if (rc) {
		fprintf(ap->errstream, "could not resolve pool, container by path %s: %s (%d)\n",
			ap->path, strerror(rc), rc);
		D_GOTO(out, rc);
	}

	ap->type = dattr.da_type;

	/** set pool/cont label or uuid */
	snprintf(ap->pool_str, DAOS_PROP_LABEL_MAX_LEN + 1, "%s", dattr.da_pool);
	snprintf(ap->cont_str, DAOS_PROP_LABEL_MAX_LEN + 1, "%s", dattr.da_cont);

	if (ap->fs_op != -1) {
		if (name) {
			if (dattr.da_rel_path) {
				if (asprintf(&ap->dfs_path, "%s/%s", dattr.da_rel_path, name) < 0)
					D_GOTO(out, rc = -ENOMEM);
			} else {
				if (asprintf(&ap->dfs_path, "/%s", name) < 0)
					D_GOTO(out, rc = -ENOMEM);
			}
		} else {
			if (dattr.da_rel_path)
				ap->dfs_path = strndup(dattr.da_rel_path, PATH_MAX);
			else
				ap->dfs_path = strndup("/", 1);
		}
		if (ap->dfs_path == NULL)
			D_GOTO(out, rc = ENOMEM);
	}

out:
	duns_destroy_attr(&dattr);
	D_FREE(dir_name);
	D_FREE(name);
	return daos_errno2der(rc);
}

int
fs_dfs_resolve_pool(struct cmd_args_s *ap)
{
	int			 rc = 0;
	char			*path = NULL;
	char			*dir = NULL;
	struct dfuse_il_reply	 il_reply = {0};

	if (ap->path == NULL)
		return -DER_INVAL;

	D_ASPRINTF(path, "%s", ap->path);
	if (path == NULL)
		return -DER_NOMEM;
	dir = dirname(path);

	rc = call_dfuse_ioctl(dir, &il_reply);
	D_FREE(path);

	switch (rc) {
	case 0:
		break;
	case ENOTTY: /* can happen if the path is not in a dfuse mount */
		return -DER_INVAL;
	default:
		return daos_errno2der(rc);
	}

	uuid_copy(ap->p_uuid, il_reply.fir_pool);

	return 0;
}

int
fs_dfs_hdlr(struct cmd_args_s *ap)
{
	int		flags;
	dfs_t		*dfs;
	dfs_obj_t	*obj;
	char            *name = NULL;
	char            *dir_name = NULL;
	int		rc, rc2;

	flags = O_RDWR;

	rc = dfs_mount(ap->pool, ap->cont, flags, &dfs);
	if (rc) {
		fprintf(ap->errstream, "failed to mount container %s: %s (%d)\n",
			ap->cont_str, strerror(rc), rc);
		return rc;
	}

	if (ap->dfs_prefix) {
		rc = dfs_set_prefix(dfs, ap->dfs_prefix);
		if (rc)
			D_GOTO(out_umount, rc);
	}

	switch (ap->fs_op) {
	case FS_RESET_ATTR:
	case FS_RESET_CHUNK_SIZE:
	case FS_RESET_OCLASS:
	{
		rc = dfs_lookup(dfs, ap->dfs_path, flags, &obj, NULL, NULL);
		if (rc) {
			fprintf(ap->errstream, "failed to lookup %s (%s)\n",
				ap->dfs_path, strerror(rc));
			D_GOTO(out_umount, rc);
		}

		if (ap->fs_op != FS_RESET_CHUNK_SIZE) {
			rc = dfs_obj_set_oclass(dfs, obj, 0, 0);
			if (rc) {
				fprintf(ap->errstream, "failed to set object class (%s)\n",
					strerror(rc));
				D_GOTO(out_release, rc);
			}
		}

		if (ap->fs_op != FS_RESET_OCLASS) {
			rc = dfs_obj_set_chunk_size(dfs, obj, 0, 0);
			if (rc) {
				fprintf(ap->errstream, "failed to set chunk size (%s)\n",
					strerror(rc));
				D_GOTO(out_release, rc);
			}
		}
		break;
	}
	case FS_SET_ATTR:
		/** try and lookup first */
		rc = dfs_lookup(dfs, ap->dfs_path, flags, &obj, NULL, NULL);
		if (rc && rc != ENOENT) {
			fprintf(ap->errstream, "failed to lookup %s (%s)\n",
				ap->dfs_path, strerror(rc));
			D_GOTO(out_umount, rc);
		}

		/** if path does not exist, create a file with the attrs */
		if (rc == ENOENT) {
			dfs_obj_t       *parent = NULL;

			parse_filename_dfs(ap->dfs_path, &name, &dir_name);

			rc = dfs_lookup(dfs, dir_name, O_RDWR, &parent,
					NULL, NULL);
			if (rc) {
				fprintf(ap->errstream, "dfs_lookup %s failed (%s)\n",
					dir_name, strerror(rc));
				D_GOTO(out_names, rc);
			}

			rc = dfs_open(dfs, parent, name,
				      S_IFREG | S_IWUSR | S_IRUSR | S_IRGRP | S_IWGRP | S_IROTH,
				      O_CREAT | O_EXCL | O_RDONLY, ap->oclass, ap->chunk_size,
				      NULL, &obj);
			if (rc)
				fprintf(ap->errstream, "dfs_open %s failed (%s)\n", name,
					strerror(rc));
			dfs_release(parent);
			break;
		}

		/** else set the attrs on the path, should be a dir */
		if (ap->oclass) {
			rc = dfs_obj_set_oclass(dfs, obj, 0, ap->oclass);
			if (rc) {
				fprintf(ap->errstream, "failed to set object class (%s)\n",
					strerror(rc));
				D_GOTO(out_release, rc);
			}
		}
		if (ap->chunk_size) {
			rc = dfs_obj_set_chunk_size(dfs, obj, 0, ap->chunk_size);
			if (rc) {
				fprintf(ap->errstream, "failed to set chunk size (%s) %d\n",
					strerror(rc), rc);
				D_GOTO(out_release, rc);
			}
		}
		break;
	default:
		D_ASSERT(0);
	}

out_release:
	rc2 = dfs_release(obj);
	if (rc2 != 0)
		fprintf(ap->errstream, "failed to release dfs obj\n");
out_names:
	D_FREE(name);
	D_FREE(dir_name);
out_umount:
	rc2 = dfs_umount(dfs);
	if (rc2 != 0)
		fprintf(ap->errstream, "failed to umount DFS container\n");
	return rc;
}

int
fs_dfs_get_attr_hdlr(struct cmd_args_s *ap, dfs_obj_info_t *attrs)
{
	int		 flags = O_RDONLY;
	int		 rc;
	int		 rc2;
	dfs_t		*dfs;
	dfs_obj_t	*obj;

	D_ASSERT(ap != NULL);
	D_ASSERT(attrs != NULL);

	rc = dfs_mount(ap->pool, ap->cont, flags, &dfs);
	if (rc) {
		fprintf(ap->errstream, "failed to mount container %s: %s (%d)\n",
			ap->cont_str, strerror(rc), rc);
		return rc;
	}

	if (ap->dfs_prefix) {
		rc = dfs_set_prefix(dfs, ap->dfs_prefix);
		if (rc)
			D_GOTO(out_umount, rc);
	}

	rc = dfs_lookup(dfs, ap->dfs_path, flags, &obj, NULL, NULL);
	if (rc) {
		fprintf(ap->errstream, "failed to lookup %s (%s)\n",
			ap->dfs_path, strerror(rc));
		D_GOTO(out_umount, rc);
	}

	rc = dfs_obj_get_info(dfs, obj, attrs);
	if (rc) {
		fprintf(ap->errstream, "failed to get obj info (%s)\n",
			strerror(rc));
		D_GOTO(out_release, rc);
	}

out_release:
	rc2 = dfs_release(obj);
	if (rc2 != 0)
		fprintf(ap->errstream, "failed to release dfs obj\n");
out_umount:
	rc2 = dfs_umount(dfs);
	if (rc2 != 0)
		fprintf(ap->errstream, "failed to umount DFS container\n");
	return rc;
}
