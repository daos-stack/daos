/**
 * (C) Copyright 2016-2023 Intel Corporation.
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
#include <fcntl.h>
#include <daos.h>
#include <daos/common.h>
#include <daos/debug.h>

#include "daos_types.h"
#include "daos_fs.h"
#include "daos_fs_sys.h"
#include "dfs_internal.h"
#include "daos_uns.h"
#include "daos_hdlr.h"

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

int
fs_fix_entry_hdlr(struct cmd_args_s *ap, bool fix_entry)
{
	dfs_t		*dfs;
	char            *name = NULL;
	char            *dir_name = NULL;
	int		rc, rc2;

	rc = dfs_mount(ap->pool, ap->cont, O_RDWR, &dfs);
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

	if (fix_entry) {
		dfs_obj_t       *parent = NULL;

		parse_filename_dfs(ap->dfs_path, &name, &dir_name);

		D_PRINT("Fixing entry type of: %s\n", ap->dfs_path);
		rc = dfs_lookup(dfs, dir_name, O_RDWR, &parent, NULL, NULL);
		if (rc) {
			fprintf(ap->errstream, "dfs_lookup %s failed (%s)\n", dir_name,
				strerror(rc));
			D_GOTO(out_names, rc);
		}

		rc = dfs_obj_fix_type(dfs, parent, name);
		if (rc) {
			fprintf(ap->errstream, "DFS fix object type failed (%s)\n", strerror(rc));
			dfs_release(parent);
			D_GOTO(out_names, rc);
		}

		rc = dfs_release(parent);
		if (rc)
			D_GOTO(out_names, rc);
	}

	if (ap->chunk_size) {
		dfs_obj_t	*obj;

		D_PRINT("Adjusting chunk size of %s to %zu\n", ap->dfs_path, ap->chunk_size);
		rc = dfs_lookup(dfs, ap->dfs_path, O_RDWR, &obj, NULL, NULL);
		if (rc) {
			fprintf(ap->errstream, "dfs_lookup %s failed (%s)\n", ap->dfs_path,
				strerror(rc));
			D_GOTO(out_names, rc);
		}

		rc = dfs_file_update_chunk_size(dfs, obj, ap->chunk_size);
		if (rc) {
			fprintf(ap->errstream, "DFS update chunk size failed (%s)\n", strerror(rc));
			dfs_release(obj);
			D_GOTO(out_names, rc);
		}

		rc = dfs_release(obj);
		if (rc)
			D_GOTO(out_names, rc);
	}

out_names:
	D_FREE(name);
	D_FREE(dir_name);
out_umount:
	rc2 = dfs_umount(dfs);
	if (rc == 0)
		rc = rc2;
	return rc;
}

int
fs_recreate_sb_hdlr(struct cmd_args_s *ap)
{
	dfs_attr_t	attr = {0};
	int		rc;

	attr.da_id = 0;
	attr.da_oclass_id = ap->oclass;
	attr.da_dir_oclass_id = ap->dir_oclass;
	attr.da_file_oclass_id = ap->file_oclass;
	attr.da_chunk_size = ap->chunk_size;
	attr.da_mode = ap->mode;
	if (ap->hints)
		strncpy(attr.da_hints, ap->hints, DAOS_CONT_HINT_MAX_LEN - 1);

	rc = dfs_recreate_sb(ap->cont, &attr);
	if (rc)
		D_ERROR("Failed to created DFS SB: %d (%s)\n", rc, strerror(rc));
	return rc;
}

int
fs_relink_root_hdlr(struct cmd_args_s *ap)
{
	return dfs_relink_root(ap->cont);
}

int
fs_chmod_hdlr(struct cmd_args_s *ap)
{
	const int mflags = O_RDWR;
	const int sflags = DFS_SYS_NO_LOCK | DFS_SYS_NO_CACHE;
	dfs_sys_t *dfs_sys;
	int rc = 0;
	int rc2 = 0;

	rc = dfs_sys_mount(ap->pool, ap->cont, mflags, sflags, &dfs_sys);
	if (rc) {
		fprintf(ap->errstream, "failed to mount container %s: %s (%d)\n",
			ap->cont_str, strerror(rc), rc);
		return rc;
	}

	if (ap->dfs_prefix) {
		rc = dfs_sys_set_prefix(dfs_sys, ap->dfs_prefix);
		if (rc) {
			fprintf(ap->errstream, "failed to set path prefix %s: %s (%d)\n",
				ap->dfs_prefix, strerror(rc), rc);
			D_GOTO(out_umount, rc);
		}
	}

	rc = dfs_sys_chmod(dfs_sys, ap->dfs_path, ap->object_mode);
	if (rc) {
		fprintf(ap->errstream, "failed to change mode bits for path %s: %s (%d)\n",
			ap->dfs_path, strerror(rc), rc);
	}

out_umount:
	rc2 = dfs_sys_umount(dfs_sys);
	if (rc2)
		fprintf(ap->errstream, "failed to umount DFS container\n");
	return rc;
}
