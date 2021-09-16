/**
 * (C) Copyright 2016-2021 Intel Corporation.
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

	if (ap->fs_op == FS_GET_ATTR)
		flags = O_RDONLY;
	else
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
	case FS_GET_ATTR:
	{
		dfs_obj_info_t	info;
		char		oclass_name[16];

		rc = dfs_lookup(dfs, ap->dfs_path, flags, &obj, NULL, NULL);
		if (rc) {
			fprintf(ap->errstream, "failed to lookup %s (%s)\n",
				ap->dfs_path, strerror(rc));
			D_GOTO(out_umount, rc);
		}

		rc = dfs_obj_get_info(dfs, obj, &info);
		if (rc) {
			fprintf(ap->errstream, "failed to get obj info (%s)\n", strerror(rc));
			D_GOTO(out_release, rc);
		}

		daos_oclass_id2name(info.doi_oclass_id, oclass_name);
		fprintf(ap->outstream, "Object Class = %s\n", oclass_name);
		fprintf(ap->outstream, "Object Chunk Size = %zu\n", info.doi_chunk_size);
		break;
	}
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
	if (rc == 0)
		rc = rc2;
out_names:
	D_FREE(name);
	D_FREE(dir_name);
out_umount:
	rc2 = dfs_umount(dfs);
	if (rc2 != 0)
		fprintf(ap->errstream, "failed to umount DFS container\n");
	if (rc == 0)
		rc = rc2;
	return rc;
}
