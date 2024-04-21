/**
 * (C) Copyright 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/** DFS lookup ops */

#define D_LOGFAC DD_FAC(dfs)

#include <daos/common.h>
#include <daos/object.h>

#include "dfs_internal.h"

int
lookup_rel_path(dfs_t *dfs, dfs_obj_t *root, const char *path, int flags, dfs_obj_t **_obj,
		mode_t *mode, struct stat *stbuf, size_t depth)
{
	dfs_obj_t        parent;
	dfs_obj_t       *obj = NULL;
	char            *token;
	char            *rem, *sptr = NULL; /* bogus compiler warning */
	bool             exists;
	bool             is_root = true;
	int              daos_mode;
	struct dfs_entry entry = {0};
	size_t           len;
	int              rc;
	bool             parent_fully_valid;

	/* Arbitrarily stop to avoid infinite recursion */
	if (depth >= DFS_MAX_RECURSION)
		return ELOOP;

	/* Only paths from root can be absolute */
	if (path[0] == '/' && daos_oid_cmp(root->oid, dfs->root.oid) != 0)
		return EINVAL;

	daos_mode = get_daos_obj_mode(flags);
	if (daos_mode == -1)
		return EINVAL;

	D_STRNDUP(rem, path, DFS_MAX_PATH - 1);
	if (rem == NULL)
		return ENOMEM;

	if (stbuf)
		memset(stbuf, 0, sizeof(struct stat));

	D_ALLOC_PTR(obj);
	if (obj == NULL)
		D_GOTO(out, rc = ENOMEM);

	oid_cp(&obj->oid, root->oid);
	oid_cp(&obj->parent_oid, root->parent_oid);
	obj->d.oclass     = root->d.oclass;
	obj->d.chunk_size = root->d.chunk_size;
	obj->mode         = root->mode;
	obj->dfs          = dfs;
	strncpy(obj->name, root->name, DFS_MAX_NAME + 1);

	rc = daos_obj_open(dfs->coh, obj->oid, daos_mode, &obj->oh, NULL);
	if (rc)
		D_GOTO(err_obj, rc = daos_der2errno(rc));

	parent.oh   = obj->oh;
	parent.mode = obj->mode;
	oid_cp(&parent.oid, obj->oid);
	oid_cp(&parent.parent_oid, obj->parent_oid);

	/** get the obj entry in the path */
	for (token = strtok_r(rem, "/", &sptr); token != NULL; token = strtok_r(NULL, "/", &sptr)) {
		is_root = false;
lookup_rel_path_loop:

		/*
		 * Open the directory object one level up.  Since fetch_entry does not support ".",
		 * we can't support ".." as the last entry, nor can we support "../.." because we
		 * don't have parent.parent_oid and parent.mode.  For now, represent this partial
		 * state with parent_fully_valid.
		 */
		parent_fully_valid = true;
		if (strcmp(token, "..") == 0) {
			parent_fully_valid = false;

			/* Cannot go outside the container */
			if (daos_oid_cmp(parent.oid, dfs->root.oid) == 0) {
				D_DEBUG(DB_TRACE, "Failed to lookup path outside container: %s\n",
					path);
				D_GOTO(err_obj, rc = ENOENT);
			}

			rc = daos_obj_close(obj->oh, NULL);
			if (rc) {
				D_ERROR("daos_obj_close() Failed (%d)\n", rc);
				D_GOTO(err_obj, rc = daos_der2errno(rc));
			}

			rc = daos_obj_open(dfs->coh, parent.parent_oid, daos_mode, &obj->oh, NULL);
			if (rc) {
				D_ERROR("daos_obj_open() Failed (%d)\n", rc);
				D_GOTO(err_obj, rc = daos_der2errno(rc));
			}

			oid_cp(&parent.oid, parent.parent_oid);
			parent.oh = obj->oh;

			/* TODO support fetch_entry(".") */
			token = strtok_r(NULL, "/", &sptr);
			if (!token || strcmp(token, "..") == 0)
				D_GOTO(err_obj, rc = ENOTSUP);
		}

		len = strlen(token);

		entry.chunk_size = 0;
		rc = fetch_entry(dfs->layout_v, parent.oh, dfs->th, token, len, true, &exists,
				 &entry, 0, NULL, NULL, NULL);
		if (rc)
			D_GOTO(err_obj, rc);

		rc = daos_obj_close(obj->oh, NULL);
		if (rc) {
			D_ERROR("daos_obj_close() Failed, " DF_RC "\n", DP_RC(rc));
			D_GOTO(err_obj, rc = daos_der2errno(rc));
		}

		if (!exists)
			D_GOTO(err_obj, rc = ENOENT);

		oid_cp(&obj->oid, entry.oid);
		oid_cp(&obj->parent_oid, parent.oid);
		strncpy(obj->name, token, len + 1);
		obj->mode = entry.mode;

		/** if entry is a file, open the array object and return */
		if (S_ISREG(entry.mode)) {
			/* if there are more entries, then file is not a dir */
			if (strtok_r(NULL, "/", &sptr) != NULL) {
				D_ERROR("%s is not a directory\n", obj->name);
				D_GOTO(err_obj, rc = ENOENT);
			}

			rc = daos_array_open_with_attr(dfs->coh, entry.oid, dfs->th, daos_mode, 1,
						       entry.chunk_size ? entry.chunk_size
									: dfs->attr.da_chunk_size,
						       &obj->oh, NULL);
			if (rc != 0) {
				D_ERROR("daos_array_open() Failed (%d)\n", rc);
				D_GOTO(err_obj, rc = daos_der2errno(rc));
			}
			if (flags & O_TRUNC) {
				rc = daos_array_set_size(obj->oh, dfs->th, 0, NULL);
				if (rc) {
					DL_ERROR(rc, "Failed to truncate file");
					daos_array_close(obj->oh, NULL);
					D_GOTO(err_obj, rc = daos_der2errno(rc));
				}
			}

			if (stbuf) {
				daos_array_stbuf_t array_stbuf = {0};

				rc = daos_array_stat(obj->oh, dfs->th, &array_stbuf, NULL);
				if (rc) {
					daos_array_close(obj->oh, NULL);
					D_GOTO(err_obj, rc = daos_der2errno(rc));
				}

				stbuf->st_size   = array_stbuf.st_size;
				stbuf->st_blocks = (stbuf->st_size + (1 << 9) - 1) >> 9;

				rc = update_stbuf_times(entry, array_stbuf.st_max_epoch, stbuf,
							NULL);
				if (rc) {
					daos_array_close(obj->oh, NULL);
					D_GOTO(err_obj, rc);
				}
			}
			break;
		}

		if (S_ISLNK(entry.mode)) {
			/*
			 * If there is a token after the sym link entry, treat
			 * the sym link as a directory and look up it's value.
			 */
			token = strtok_r(NULL, "/", &sptr);
			if (token) {
				dfs_obj_t *sym;

				if (!parent_fully_valid && strncmp(entry.value, "..", 2) == 0) {
					D_FREE(entry.value);
					D_GOTO(err_obj, rc = ENOTSUP);
				}

				rc = lookup_rel_path(dfs, &parent, entry.value, flags, &sym, NULL,
						     NULL, depth + 1);
				if (rc) {
					D_DEBUG(DB_TRACE, "Failed to lookup symlink %s\n",
						entry.value);
					D_FREE(entry.value);
					D_GOTO(err_obj, rc);
				}

				obj->oh     = sym->oh;
				parent.oh   = sym->oh;
				parent.mode = sym->mode;
				oid_cp(&parent.oid, sym->oid);
				oid_cp(&parent.parent_oid, sym->parent_oid);

				D_FREE(sym);
				D_FREE(entry.value);
				obj->value = NULL;
				/*
				 * need to go to to the beginning of loop but we
				 * already did the strtok.
				 */
				goto lookup_rel_path_loop;
			}

			/* Conditionally dereference leaf symlinks */
			if (!(flags & O_NOFOLLOW)) {
				dfs_obj_t *sym;

				if (!parent_fully_valid && strncmp(entry.value, "..", 2) == 0) {
					D_FREE(entry.value);
					D_GOTO(err_obj, rc = ENOTSUP);
				}

				rc = lookup_rel_path(dfs, &parent, entry.value, flags, &sym, mode,
						     stbuf, depth + 1);
				if (rc) {
					D_DEBUG(DB_TRACE, "Failed to lookup symlink %s\n",
						entry.value);
					D_FREE(entry.value);
					D_GOTO(err_obj, rc);
				}

				/* return this dereferenced obj */
				D_FREE(obj);
				obj = sym;
				D_FREE(entry.value);
				D_GOTO(out, rc);
			}

			/* Create a truncated version of the string */
			D_STRNDUP(obj->value, entry.value, entry.value_len + 1);
			if (obj->value == NULL) {
				D_FREE(entry.value);
				D_GOTO(out, rc = ENOMEM);
			}
			D_FREE(entry.value);
			if (stbuf)
				stbuf->st_size = entry.value_len;
			/** return the symlink obj if this is the last entry */
			break;
		}

		if (!S_ISDIR(entry.mode)) {
			D_ERROR("Invalid entry type in path.\n");
			D_GOTO(err_obj, rc = EINVAL);
		}

		/* open the directory object */
		rc = daos_obj_open(dfs->coh, entry.oid, daos_mode, &obj->oh, NULL);
		if (rc) {
			D_ERROR("daos_obj_open() Failed, " DF_RC "\n", DP_RC(rc));
			D_GOTO(err_obj, rc = daos_der2errno(rc));
		}

		obj->d.chunk_size = entry.chunk_size;
		obj->d.oclass     = entry.oclass;
		oid_cp(&parent.oid, obj->oid);
		oid_cp(&parent.parent_oid, obj->parent_oid);
		parent.oh   = obj->oh;
		parent.mode = entry.mode;

		if (stbuf) {
			daos_epoch_t ep;

			rc = daos_obj_query_max_epoch(obj->oh, dfs->th, &ep, NULL);
			if (rc) {
				daos_obj_close(obj->oh, NULL);
				D_GOTO(err_obj, rc = daos_der2errno(rc));
			}

			rc = update_stbuf_times(entry, ep, stbuf, NULL);
			if (rc) {
				daos_obj_close(obj->oh, NULL);
				D_GOTO(err_obj, rc = daos_der2errno(rc));
			}
			stbuf->st_size = sizeof(entry);
		}
	}

	if (mode)
		*mode = obj->mode;

	if (stbuf) {
		if (is_root) {
			daos_epoch_t ep;

			/** refresh possibly stale root stbuf */
			rc = fetch_entry(dfs->layout_v, dfs->super_oh, dfs->th, "/", 1, false,
					 &exists, &entry, 0, NULL, NULL, NULL);
			if (rc) {
				D_ERROR("fetch_entry() failed: %d (%s)\n", rc, strerror(rc));
				D_GOTO(err_obj, rc);
			}

			if (!exists || !S_ISDIR(entry.mode)) {
				/** something really bad happened! */
				D_ERROR("Root object corrupted!");
				D_GOTO(err_obj, rc = EIO);
			}

			if (mode)
				*mode = entry.mode;
			dfs->root_stbuf.st_mode = entry.mode;
			dfs->root_stbuf.st_uid  = entry.uid;
			dfs->root_stbuf.st_gid  = entry.gid;

			rc = daos_obj_query_max_epoch(dfs->root.oh, dfs->th, &ep, NULL);
			if (rc)
				D_GOTO(err_obj, rc = daos_der2errno(rc));

			/** object was updated since creation */
			rc = update_stbuf_times(entry, ep, &dfs->root_stbuf, NULL);
			if (rc)
				D_GOTO(err_obj, rc);
			if (tspec_gt(dfs->root_stbuf.st_ctim, dfs->root_stbuf.st_mtim)) {
				dfs->root_stbuf.st_atim.tv_sec  = entry.ctime;
				dfs->root_stbuf.st_atim.tv_nsec = entry.ctime_nano;
			} else {
				dfs->root_stbuf.st_atim.tv_sec  = entry.mtime;
				dfs->root_stbuf.st_atim.tv_nsec = entry.mtime_nano;
			}
			memcpy(stbuf, &dfs->root_stbuf, sizeof(struct stat));
		} else {
			stbuf->st_nlink = 1;
			stbuf->st_mode  = obj->mode;
			stbuf->st_uid   = entry.uid;
			stbuf->st_gid   = entry.gid;
			if (tspec_gt(stbuf->st_ctim, stbuf->st_mtim)) {
				stbuf->st_atim.tv_sec  = entry.ctime;
				stbuf->st_atim.tv_nsec = entry.ctime_nano;
			} else {
				stbuf->st_atim.tv_sec  = entry.mtime;
				stbuf->st_atim.tv_nsec = entry.mtime_nano;
			}
		}
	}

	obj->flags = flags;

out:
	D_FREE(rem);
	*_obj = obj;
	return rc;
err_obj:
	D_FREE(obj);
	goto out;
}

int
dfs_lookup(dfs_t *dfs, const char *path, int flags, dfs_obj_t **_obj, mode_t *mode,
	   struct stat *stbuf)
{
	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (flags & O_APPEND)
		return ENOTSUP;
	if (_obj == NULL)
		return EINVAL;
	if (path == NULL || strnlen(path, DFS_MAX_PATH) > DFS_MAX_PATH - 1)
		return EINVAL;
	if (path[0] != '/')
		return EINVAL;

	/** if we added a prefix, check and skip over it */
	if (dfs->prefix) {
		if (strncmp(dfs->prefix, path, dfs->prefix_len) != 0)
			return EINVAL;

		path += dfs->prefix_len;
	}

	return lookup_rel_path(dfs, &dfs->root, path, flags, _obj, mode, stbuf, 0);
}

static int
lookup_rel_int(dfs_t *dfs, dfs_obj_t *parent, const char *name, int flags, dfs_obj_t **_obj,
	       mode_t *mode, struct stat *stbuf, int xnr, char *xnames[], void *xvals[],
	       daos_size_t *xsizes)
{
	dfs_obj_t       *obj;
	struct dfs_entry entry = {0};
	bool             exists;
	int              daos_mode;
	size_t           len;
	int              rc = 0;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (_obj == NULL)
		return EINVAL;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return ENOTDIR;
	if (flags & O_APPEND)
		return ENOTSUP;

	rc = check_name(name, &len);
	if (rc)
		return rc;

	daos_mode = get_daos_obj_mode(flags);
	if (daos_mode == -1)
		return EINVAL;

	rc = fetch_entry(dfs->layout_v, parent->oh, dfs->th, name, len, true, &exists, &entry, xnr,
			 xnames, xvals, xsizes);
	if (rc)
		return rc;

	if (!exists)
		return ENOENT;

	if (stbuf)
		memset(stbuf, 0, sizeof(struct stat));

	D_ALLOC_PTR(obj);
	if (obj == NULL) {
		D_FREE(entry.value);
		return ENOMEM;
	}

	strncpy(obj->name, name, len + 1);
	oid_cp(&obj->parent_oid, parent->oid);
	oid_cp(&obj->oid, entry.oid);
	obj->mode = entry.mode;
	obj->dfs  = dfs;

	/** if entry is a file, open the array object and return */
	switch (entry.mode & S_IFMT) {
	case S_IFREG:
		rc = daos_array_open_with_attr(
		    dfs->coh, entry.oid, dfs->th, daos_mode, 1,
		    entry.chunk_size ? entry.chunk_size : dfs->attr.da_chunk_size, &obj->oh, NULL);
		if (rc != 0) {
			D_ERROR("daos_array_open_with_attr() Failed " DF_RC "\n", DP_RC(rc));
			D_GOTO(err_obj, rc = daos_der2errno(rc));
		}
		if (flags & O_TRUNC) {
			rc = daos_array_set_size(obj->oh, dfs->th, 0, NULL);
			if (rc) {
				DL_ERROR(rc, "Failed to truncate file");
				daos_array_close(obj->oh, NULL);
				D_GOTO(err_obj, rc = daos_der2errno(rc));
			}
		}

		/** we need the file size if stat struct is needed */
		if (stbuf) {
			daos_array_stbuf_t array_stbuf = {0};

			rc = daos_array_stat(obj->oh, dfs->th, &array_stbuf, NULL);
			if (rc) {
				daos_array_close(obj->oh, NULL);
				D_GOTO(err_obj, rc = daos_der2errno(rc));
			}
			stbuf->st_size   = array_stbuf.st_size;
			stbuf->st_blocks = (stbuf->st_size + (1 << 9) - 1) >> 9;

			rc = update_stbuf_times(entry, array_stbuf.st_max_epoch, stbuf, NULL);
			if (rc) {
				daos_array_close(obj->oh, NULL);
				D_GOTO(err_obj, rc);
			}
		}
		break;
	case S_IFLNK:
		if (flags & O_NOFOLLOW) {
			if (entry.value == NULL) {
				D_ERROR("Symlink entry found with no value\n");
				D_GOTO(err_obj, rc = EIO);
			}
			/* Create a truncated version of the string */
			D_STRNDUP(obj->value, entry.value, entry.value_len + 1);
			D_FREE(entry.value);
			if (obj->value == NULL)
				D_GOTO(err_obj, rc = ENOMEM);
			if (stbuf) {
				stbuf->st_size         = entry.value_len;
				stbuf->st_mtim.tv_sec  = entry.mtime;
				stbuf->st_mtim.tv_nsec = entry.mtime_nano;
				stbuf->st_ctim.tv_sec  = entry.ctime;
				stbuf->st_ctim.tv_nsec = entry.ctime_nano;
			}
		} else {
			dfs_obj_t *sym;

			/** this should not happen, but to silence coverity */
			if (entry.value == NULL)
				D_GOTO(err_obj, rc = EIO);
			/* dereference the symlink */
			rc = lookup_rel_path(dfs, parent, entry.value, flags, &sym, mode, stbuf, 0);
			if (rc) {
				D_DEBUG(DB_TRACE, "Failed to lookup symlink %s\n", entry.value);
				D_FREE(entry.value);
				D_GOTO(err_obj, rc);
			}
			D_FREE(obj);
			obj = sym;
			D_FREE(entry.value);
			D_GOTO(out, rc);
		}
		break;
	case S_IFDIR:
		rc = daos_obj_open(dfs->coh, entry.oid, daos_mode, &obj->oh, NULL);
		if (rc) {
			D_ERROR("daos_obj_open() Failed: " DF_RC "\n", DP_RC(rc));
			D_GOTO(err_obj, rc = daos_der2errno(rc));
		}

		obj->d.chunk_size = entry.chunk_size;
		obj->d.oclass     = entry.oclass;

		if (stbuf) {
			daos_epoch_t ep;

			rc = daos_obj_query_max_epoch(obj->oh, dfs->th, &ep, NULL);
			if (rc) {
				daos_obj_close(obj->oh, NULL);
				D_GOTO(err_obj, rc = daos_der2errno(rc));
			}

			rc = update_stbuf_times(entry, ep, stbuf, NULL);
			if (rc) {
				daos_obj_close(obj->oh, NULL);
				D_GOTO(err_obj, rc = daos_der2errno(rc));
			}
			stbuf->st_size = sizeof(entry);
		}
		break;
	default:
		rc = EINVAL;
		D_ERROR("Invalid entry type (not a dir, file, symlink): %d (%s)\n", rc,
			strerror(rc));
		D_GOTO(err_obj, rc);
	}

	if (mode)
		*mode = obj->mode;

	if (stbuf) {
		stbuf->st_nlink = 1;
		stbuf->st_mode  = obj->mode;
		stbuf->st_uid   = entry.uid;
		stbuf->st_gid   = entry.gid;
		if (tspec_gt(stbuf->st_ctim, stbuf->st_mtim)) {
			stbuf->st_atim.tv_sec  = stbuf->st_ctim.tv_sec;
			stbuf->st_atim.tv_nsec = stbuf->st_ctim.tv_nsec;
		} else {
			stbuf->st_atim.tv_sec  = stbuf->st_mtim.tv_sec;
			stbuf->st_atim.tv_nsec = stbuf->st_mtim.tv_nsec;
		}
	}

out:
	obj->flags = flags;
	*_obj      = obj;

	return rc;
err_obj:
	D_FREE(obj);
	return rc;
}

int
dfs_lookup_rel(dfs_t *dfs, dfs_obj_t *parent, const char *name, int flags, dfs_obj_t **obj,
	       mode_t *mode, struct stat *stbuf)
{
	return lookup_rel_int(dfs, parent, name, flags, obj, mode, stbuf, 0, NULL, NULL, NULL);
}

int
dfs_lookupx(dfs_t *dfs, dfs_obj_t *parent, const char *name, int flags, dfs_obj_t **obj,
	    mode_t *mode, struct stat *stbuf, int xnr, char *xnames[], void *xvals[],
	    daos_size_t *xsizes)
{
	return lookup_rel_int(dfs, parent, name, flags, obj, mode, stbuf, xnr, xnames, xvals,
			      xsizes);
}
