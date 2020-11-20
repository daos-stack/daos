/**
 * (C) Copyright 2018-2020 Intel Corporation.
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

#define D_LOGFAC	DD_FAC(dfs)

#include <fuse3/fuse.h>
#include <errno.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>

#include <daos/object.h>
#include "daos.h"
#include "daos_fs.h"

struct dfuse_data {
	int		show_help;
	int		show_version;
	int		debug;
	int		foreground;
	int		singlethread;
	char		*mountpoint;
	char		*pool;
	char		*group;
	char		*cont;
	bool		root_cont;
	struct fuse	*fuse;
};

static struct dfuse_data dfuse_fs;
static dfs_t *dfs;

#define FUNC_ENTER(fmt, ...)					\
do {								\
	if (dfuse_fs.debug)					\
		fprintf(stderr, "%s [%d]: "fmt, __func__,	\
			__LINE__, ##__VA_ARGS__);		\
} while (0)

static int
parse_filename(const char *path, char **_obj_name, char **_cont_name)
{
	char *f1 = NULL;
	char *f2 = NULL;
	char *fname = NULL;
	char *cont_name = NULL;
	int rc = 0;

	if (path == NULL || _obj_name == NULL || _cont_name == NULL)
		return -EINVAL;

	if (strcmp(path, "/") == 0) {
		*_cont_name = strdup("/");
		if (*_cont_name == NULL)
			return -ENOMEM;
		*_obj_name = NULL;
		return 0;
	}

	f1 = strdup(path);
	if (f1 == NULL)
		D_GOTO(out, rc = -ENOMEM);

	f2 = strdup(path);
	if (f2 == NULL)
		D_GOTO(out, rc = -ENOMEM);

	fname = basename(f1);
	cont_name = dirname(f2);

	if (cont_name[0] == '.' || cont_name[0] != '/') {
		char cwd[1024];

		if (getcwd(cwd, 1024) == NULL)
			D_GOTO(out, rc = -ENOMEM);

		if (strcmp(cont_name, ".") == 0) {
			cont_name = strdup(cwd);
			if (cont_name == NULL)
				D_GOTO(out, rc = -ENOMEM);
		} else {
			char *new_dir = calloc(strlen(cwd) + strlen(cont_name)
					       + 1, sizeof(char));
			if (new_dir == NULL)
				D_GOTO(out, rc = -ENOMEM);

			strcpy(new_dir, cwd);
			if (cont_name[0] == '.') {
				strcat(new_dir, &cont_name[1]);
			} else {
				strcat(new_dir, "/");
				strcat(new_dir, cont_name);
			}
			cont_name = new_dir;
		}
		*_cont_name = cont_name;
	} else {
		*_cont_name = strdup(cont_name);
		if (*_cont_name == NULL)
			D_GOTO(out, rc = -ENOMEM);
	}

	*_obj_name = strdup(fname);
	if (*_obj_name == NULL) {
		free(*_cont_name);
		*_cont_name = NULL;
		D_GOTO(out, rc = -ENOMEM);
	}

out:
	if (f1)
		free(f1);
	if (f2)
		free(f2);
	return rc;
}

static int
dfuse_access(const char *path, int mask)
{
	char		*name = NULL, *dir_name = NULL;
	dfs_obj_t	*parent = NULL;
	mode_t		pmode;
	int		rc;

	FUNC_ENTER("path = %s\n", path);

	if (path == NULL)
		return -ENOENT;

	rc = parse_filename(path, &name, &dir_name);
	if (rc)
		return rc;

	D_ASSERT(dir_name != NULL);
	if (strcmp(dir_name, "/") != 0) {
		rc = dfs_lookup(dfs, dir_name, O_RDONLY, &parent, &pmode, NULL);
		if (rc) {
			fprintf(stderr, "Failed to lookup path %s (%d)\n",
				dir_name, rc);
			D_GOTO(out, rc = -rc);
		}
		if (!S_ISDIR(pmode)) {
			fprintf(stderr, "%s does not resolve to a dir\n",
				dir_name);
			D_GOTO(out, rc = -ENOTDIR);
		}
	}

	rc = dfs_access(dfs, parent, name, mask);
	if (rc)
		D_GOTO(out, rc = -rc);

out:
	if (name)
		free(name);
	free(dir_name);
	if (parent)
		dfs_release(parent);
	return rc;
}

static int
dfuse_chmod(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	char		*name = NULL, *dir_name = NULL;
	dfs_obj_t	*parent = NULL;
	mode_t		pmode;
	int		rc;

	FUNC_ENTER("path = %s\n", path);

#if 0
	/** TODO - maybe add a dfs_fchmod() for this */
	if (fi != NULL) {
		rc = dfs_chmod(dfs, (dfs_obj_t *)fi->fh, mode);
		return -rc;
	}
#endif

	if (path == NULL)
		return -ENOENT;

	rc = parse_filename(path, &name, &dir_name);
	if (rc)
		return rc;

	D_ASSERT(dir_name != NULL);
	if (strcmp(dir_name, "/") != 0) {
		rc = dfs_lookup(dfs, dir_name, O_RDWR, &parent, &pmode, NULL);
		if (rc) {
			fprintf(stderr, "Failed to lookup path %s (%d)\n",
				dir_name, rc);
			D_GOTO(out, rc = -rc);
		}
		if (!S_ISDIR(pmode)) {
			fprintf(stderr, "%s does not resolve to a dir\n",
				dir_name);
			D_GOTO(out, rc = -ENOTDIR);
		}
	}

	rc = dfs_chmod(dfs, parent, name, mode);
	if (rc)
		D_GOTO(out, rc = -rc);

out:
	if (name)
		free(name);
	free(dir_name);
	if (parent)
		dfs_release(parent);
	return rc;
}

static int
dfuse_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
	char *name = NULL, *dir_name = NULL;
	dfs_obj_t *parent = NULL;
	mode_t pmode;
	int rc;

	FUNC_ENTER("path = %s\n", path);

	if (fi != NULL) {
		rc = dfs_ostat(dfs, (dfs_obj_t *)fi->fh, stbuf);
		return -rc;
	}

	if (path == NULL)
		return -ENOENT;

	rc = parse_filename(path, &name, &dir_name);
	if (rc)
		return rc;

	rc = dfs_lookup(dfs, dir_name, O_RDONLY, &parent, &pmode, NULL);
	if (rc) {
		fprintf(stderr, "Failed path lookup %s (%d)\n", dir_name, rc);
		D_GOTO(out, rc = -rc);
	}
	if (!S_ISDIR(pmode)) {
		fprintf(stderr, "%s does not resolve to a dir\n", dir_name);
		D_GOTO(out, rc = -ENOTDIR);
	}

	rc = dfs_stat(dfs, parent, name, stbuf);
	if (rc)
		D_GOTO(out, rc = -rc);

out:
	if (name)
		free(name);
	if (dir_name)
		free(dir_name);
	if (parent)
		dfs_release(parent);
	return rc;
}

static int
dfuse_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
	dfs_obj_t *obj = NULL;
	int rc;

	FUNC_ENTER("path = %s\n", path);

	if (fi != NULL) {
		rc = dfs_punch(dfs, (dfs_obj_t *)fi->fh, size, DFS_MAX_FSIZE);
		return -rc;
	}

	if (path == NULL)
		return -ENOENT;

	rc = dfs_lookup(dfs, path, O_RDWR, &obj, NULL, NULL);
	if (rc)
		D_GOTO(out, rc = -rc);

	rc = dfs_punch(dfs, obj, size, DFS_MAX_FSIZE);
	if (rc)
		D_GOTO(out, rc = -rc);

out:
	if (obj)
		dfs_release(obj);
	return rc;
}

#define NUM_DIRENTS 10

static int
dfuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
	      off_t offset, struct fuse_file_info *fi,
	      enum fuse_readdir_flags flags)
{
	dfs_obj_t	*obj = NULL;
	bool		 release = false;
	daos_anchor_t	 anchor = {0};
	int rc = 0;

	(void) offset;
	(void) fi;

	FUNC_ENTER("path = %s\n", path);

	if (fi != NULL) {
		obj = (dfs_obj_t *)fi->fh;
	} else {
		if (path == NULL)
			return -ENOENT;
		rc = dfs_lookup(dfs, path, O_RDONLY, &obj, NULL, NULL);
		if (rc) {
			fprintf(stderr, "Failed to lookup path %s (%d)\n",
				path, rc);
			return -rc;
		}
		release = true;
	}

	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);

	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t i, nr = NUM_DIRENTS;
		struct dirent dirs[NUM_DIRENTS];

		rc = dfs_readdir(dfs, obj, &anchor, &nr, dirs);
		if (rc) {
			fprintf(stderr, "Failed to iterate path %s (%d)\n",
				path, rc);
			D_GOTO(out, rc = -rc);
		}
		for (i = 0; i < nr; i++)
			filler(buf, dirs[i].d_name, NULL, 0, 0);
	}

out:
	if (release && obj)
		dfs_release(obj);
	return rc;
}

static int
dfuse_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	char *name = NULL, *dir_name = NULL;
	dfs_obj_t *obj, *parent = NULL;
	mode_t pmode;
	int rc;

	FUNC_ENTER("path = %s\n", path);

	rc = parse_filename(path, &name, &dir_name);
	if (rc)
		return rc;

	D_ASSERT(dir_name);
	if (name == NULL)
		D_GOTO(out, rc = -EINVAL);

	rc = dfs_lookup(dfs, dir_name, O_RDWR, &parent, &pmode, NULL);
	if (rc) {
		fprintf(stderr, "Failed path lookup %s (%d)\n", dir_name, rc);
		D_GOTO(out, rc = -rc);
	}
	if (!S_ISDIR(pmode)) {
		fprintf(stderr, "%s does not resolve to a dir\n", dir_name);
		D_GOTO(out, rc = -ENOTDIR);
	}

	mode = S_IFREG | mode;
	/** TODO - set the oclass and array chunk size using the UNS */
	rc = dfs_open(dfs, parent, name, mode, fi->flags, OC_SX, 0,
		      NULL, &obj);
	if (rc)
		D_GOTO(out, rc = -rc);

	fi->direct_io = 1;
	fi->fh = (uint64_t)obj;

out:
	if (name)
		free(name);
	if (dir_name)
		free(dir_name);
	if (parent)
		dfs_release(parent);
	return rc;
}

static int
dfuse_open(const char *path, struct fuse_file_info *fi)
{
	char *name = NULL, *dir_name = NULL;
	dfs_obj_t *obj, *parent = NULL;
	mode_t pmode;
	int rc;

	FUNC_ENTER("path = %s\n", path);

	rc = parse_filename(path, &name, &dir_name);
	if (rc)
		return rc;

	D_ASSERT(dir_name);
	if (name == NULL)
		D_GOTO(out, rc = -EINVAL);

	rc = dfs_lookup(dfs, dir_name, O_RDWR, &parent, &pmode, NULL);
	if (rc) {
		fprintf(stderr, "Failed path lookup %s (%d)\n", dir_name, rc);
		D_GOTO(out, rc = -rc);
	}
	if (!S_ISDIR(pmode)) {
		fprintf(stderr, "%s does not resolve to a dir\n", dir_name);
		D_GOTO(out, rc = -ENOTDIR);
	}

	/** TODO - set the oclass and array chunk size using the UNS */
	rc = dfs_open(dfs, parent, name, S_IFREG, fi->flags, OC_SX,
		      0, NULL, &obj);
	if (rc)
		D_GOTO(out, rc = -rc);

	fi->direct_io = 1;
	fi->fh = (uint64_t)obj;

out:
	if (name)
		free(name);
	if (dir_name)
		free(dir_name);
	if (parent)
		dfs_release(parent);
	return rc;
}

static int
dfuse_opendir(const char *path, struct fuse_file_info *fi)
{
	char *name = NULL, *dir_name = NULL;
	dfs_obj_t *obj, *parent = NULL;
	mode_t pmode;
	bool free_parent = true;
	int rc;

	FUNC_ENTER("path = %s\n", path);

	rc = parse_filename(path, &name, &dir_name);
	if (rc)
		return rc;

	D_ASSERT(dir_name);

	rc = dfs_lookup(dfs, dir_name, O_RDONLY, &parent, &pmode, NULL);
	if (rc) {
		fprintf(stderr, "Failed path lookup %s (%d)\n", dir_name, rc);
		D_GOTO(out, rc = -rc);
	}
	if (!S_ISDIR(pmode)) {
		fprintf(stderr, "%s does not resolve to a dir\n", dir_name);
		D_GOTO(out, rc = -ENOTDIR);
	}

	if (name == NULL) {
		fi->fh = (uint64_t)parent;
		free_parent = false;
	} else {
		/** TODO - set the oclass and array chunk size using the UNS */
		rc = dfs_open(dfs, parent, name, S_IFDIR, O_RDONLY, 0, 0, NULL,
			      &obj);
		if (rc)
			D_GOTO(out, rc = -rc);
		fi->fh = (uint64_t)obj;
	}

out:
	if (name)
		free(name);
	if (dir_name)
		free(dir_name);
	if (free_parent && parent)
		dfs_release(parent);
	return rc;
}

static int
dfuse_read(const char *path, char *buf, size_t size, off_t offset,
	   struct fuse_file_info *fi)
{
	dfs_obj_t *obj;
	daos_size_t actual;
	d_iov_t iov;
	d_sg_list_t sgl;
	int rc;

	FUNC_ENTER("path = %s\n", path);

	D_ASSERT(fi != NULL);
	obj = (dfs_obj_t *)fi->fh;

	/** set memory location */
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, buf, size);
	sgl.sg_iovs = &iov;

	rc = dfs_read(dfs, obj, &sgl, offset, &actual, NULL);
	if (rc)
		return -rc;

	return actual;
}

static int
dfuse_write(const char *path, const char *buf, size_t size, off_t offset,
	    struct fuse_file_info *fi)
{
	dfs_obj_t *obj;
	d_iov_t iov;
	d_sg_list_t sgl;
	int rc;

	FUNC_ENTER("path = %s\n", path);

	D_ASSERT(fi != NULL);
	obj = (dfs_obj_t *)fi->fh;

	/** set memory location */
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, (void *)buf, size);
	sgl.sg_iovs = &iov;

	rc = dfs_write(dfs, obj, &sgl, offset, NULL);
	if (rc)
		return -rc;

	return size;
}

static int
dfuse_mkdir(const char *path, mode_t mode)
{
	dfs_obj_t *parent = NULL;
	mode_t pmode;
	char *name = NULL, *dir_name = NULL;
	int rc;

	FUNC_ENTER("path = %s\n", path);

	rc = parse_filename(path, &name, &dir_name);
	if (rc)
		return rc;

	D_ASSERT(dir_name);
	if (name == NULL)
		D_GOTO(out, rc = -EINVAL);

	rc = dfs_lookup(dfs, dir_name, O_RDWR, &parent, &pmode, NULL);
	if (rc) {
		fprintf(stderr, "Failed path lookup %s (%d)\n", dir_name, rc);
		D_GOTO(out, rc = -rc);
	}
	if (!S_ISDIR(pmode)) {
		fprintf(stderr, "%s does not resolve to a dir\n", dir_name);
		D_GOTO(out, rc = -ENOTDIR);
	}

	rc = dfs_mkdir(dfs, parent, name, mode, 0);
	if (rc)
		D_GOTO(out, rc = -rc);

out:
	if (name)
		free(name);
	if (dir_name)
		free(dir_name);
	if (parent)
		dfs_release(parent);
	return rc;
}

static int
dfuse_symlink(const char *from, const char *to)
{
	dfs_obj_t *parent = NULL;
	mode_t pmode;
	dfs_obj_t *sym;
	char *name = NULL, *dir_name = NULL;
	int rc;

	FUNC_ENTER("from = %s, to = %s\n", from, to);

	rc = parse_filename(to, &name, &dir_name);
	if (rc)
		return rc;

	D_ASSERT(dir_name);
	if (name == NULL)
		D_GOTO(out, rc = -EINVAL);

	rc = dfs_lookup(dfs, dir_name, O_RDWR, &parent, &pmode, NULL);
	if (rc) {
		fprintf(stderr, "Failed path lookup %s (%d)\n", dir_name, rc);
		D_GOTO(out, rc = -rc);
	}
	if (!S_ISDIR(pmode)) {
		fprintf(stderr, "%s does not resolve to a dir\n", dir_name);
		D_GOTO(out, rc = -ENOTDIR);
	}

	/** TODO - set the object class and array chunk size using the UNS */
	rc = dfs_open(dfs, parent, name, S_IFLNK, O_CREAT, 0, 0, from, &sym);
	if (rc)
		D_GOTO(out, rc = -rc);

	dfs_release(sym);

out:
	if (name)
		free(name);
	if (dir_name)
		free(dir_name);
	if (parent)
		dfs_release(parent);
	return rc;
}

static int
dfuse_readlink(const char *path, char *buf, size_t size)
{
	dfs_obj_t *obj = NULL;
	mode_t mode;
	int rc;

	FUNC_ENTER("path = %s\n", path);

	rc = dfs_lookup(dfs, path, O_RDONLY, &obj, &mode, NULL);
	if (rc) {
		fprintf(stderr, "Failed path lookup %s (%d)\n", path, rc);
		D_GOTO(out, rc = -rc);
	}
	if (!S_ISLNK(mode)) {
		fprintf(stderr, "%s does not resolve to a symlink\n", path);
		D_GOTO(out, rc = -EINVAL);
	}

	rc = dfs_get_symlink_value(obj, buf, &size);
	if (rc)
		D_GOTO(out, rc = -rc);

out:
	if (obj)
		dfs_release(obj);
	return rc;
}

static int
dfuse_unlink(const char *path)
{
	dfs_obj_t *parent = NULL;
	mode_t pmode;
	char *name = NULL, *dir_name = NULL;
	int rc;

	FUNC_ENTER("path = %s\n", path);

	rc = parse_filename(path, &name, &dir_name);
	if (rc)
		return rc;

	D_ASSERT(dir_name);
	if (name == NULL)
		D_GOTO(out, rc = -EINVAL);

	rc = dfs_lookup(dfs, dir_name, O_RDWR, &parent, &pmode, NULL);
	if (rc) {
		fprintf(stderr, "Failed path lookup %s (%d)\n", dir_name, rc);
		D_GOTO(out, rc = -rc);
	}
	if (!S_ISDIR(pmode)) {
		fprintf(stderr, "%s does not resolve to a dir\n", dir_name);
		D_GOTO(out, rc = -ENOTDIR);
	}

	rc = dfs_remove(dfs, parent, name, false, NULL);
	if (rc) {
		fprintf(stderr, "Failed to remove file %s (%d)\n", name, rc);
		D_GOTO(out, rc = -rc);
	}

out:
	if (name)
		free(name);
	if (dir_name)
		free(dir_name);
	if (parent)
		dfs_release(parent);
	return rc;
}

static int
dfuse_rmdir(const char *path)
{
	dfs_obj_t *parent = NULL;
	mode_t pmode;
	char *name = NULL, *dir_name = NULL;
	int rc;

	FUNC_ENTER("path = %s\n", path);

	rc = parse_filename(path, &name, &dir_name);
	if (rc)
		return rc;

	D_ASSERT(dir_name);
	if (name == NULL)
		D_GOTO(out, rc = -EINVAL);

	rc = dfs_lookup(dfs, dir_name, O_RDWR, &parent, &pmode, NULL);
	if (rc) {
		fprintf(stderr, "Failed path lookup %s (%d)\n", dir_name, rc);
		D_GOTO(out, rc = -rc);
	}
	if (!S_ISDIR(pmode)) {
		fprintf(stderr, "%s does not resolve to a dir\n", dir_name);
		D_GOTO(out, rc = -ENOTDIR);
	}

	rc = dfs_remove(dfs, parent, name, false, NULL);
	if (rc) {
		fprintf(stderr, "Failed to remove dir %s (%d)\n", name, rc);
		D_GOTO(out, rc = -rc);
	}

out:
	if (name)
		free(name);
	if (dir_name)
		free(dir_name);
	if (parent)
		dfs_release(parent);
	return rc;
}

static int
dfuse_release(const char *path, struct fuse_file_info *fi)
{
	int rc;

	FUNC_ENTER("path = %s\n", path);

	if (fi != NULL) {
		rc = dfs_release((dfs_obj_t *)fi->fh);
		return -rc;
	}

	return 0;
}

static int
dfuse_rename(const char *old_path, const char *new_path, unsigned int flags)
{
	char *name = NULL, *dir_name = NULL;
	char *new_name = NULL, *new_dir_name = NULL;
	dfs_obj_t *parent = NULL, *new_parent = NULL;
	mode_t pmode;
	int rc;

	FUNC_ENTER("old path = %s,  new path = %s\n", old_path, new_path);

	rc = parse_filename(old_path, &name, &dir_name);
	if (rc)
		return rc;

	D_ASSERT(dir_name);
	if (name == NULL)
		D_GOTO(out, rc = -EINVAL);

	rc = dfs_lookup(dfs, dir_name, O_RDWR, &parent, &pmode, NULL);
	if (rc) {
		fprintf(stderr, "Failed path lookup %s (%d)\n", dir_name, rc);
		D_GOTO(out, rc = -rc);
	}
	if (!S_ISDIR(pmode)) {
		fprintf(stderr, "%s does not resolve to a dir\n", dir_name);
		D_GOTO(out, rc = -ENOTDIR);
	}

	rc = parse_filename(new_path, &new_name, &new_dir_name);
	if (rc)
		D_GOTO(out, rc);

	D_ASSERT(new_dir_name);
	if (new_name == NULL)
		D_GOTO(out, rc = -EINVAL);

	rc = dfs_lookup(dfs, new_dir_name, O_RDWR, &new_parent, &pmode, NULL);
	if (rc) {
		fprintf(stderr, "Failed path lookup %s (%d)\n", new_dir_name,
			rc);
		D_GOTO(out, rc = -rc);
	}
	if (!S_ISDIR(pmode)) {
		fprintf(stderr, "%s does not resolve to a dir\n", new_dir_name);
		D_GOTO(out, rc = -ENOTDIR);
	}

#ifdef RENAME_NOREPLACE
	if (flags & RENAME_EXCHANGE) {
		if (flags & RENAME_NOREPLACE)
			D_GOTO(out, rc = -EINVAL);

		rc = dfs_exchange(dfs, parent, name, new_parent, new_name);
		if (rc) {
			fprintf(stderr, "Failed to exchange %s with %s (%d)\n",
				old_path, new_path, rc);
			D_GOTO(out, rc = -rc);
		}
	} else {
		if (flags & RENAME_NOREPLACE) {
			dfs_obj_t *obj = NULL;

			rc = dfs_lookup(dfs, new_path, O_RDWR, &obj, &pmode,
					NULL);
			if (rc != ENOENT) {
				if (rc == 0)
					dfs_release(obj);
				D_GOTO(out, rc = -rc);
			}
		}

		rc = dfs_move(dfs, parent, name, new_parent, new_name, NULL);
		if (rc) {
			fprintf(stderr, "Failed to move %s to %s (%d)\n",
				old_path, new_path, rc);
			D_GOTO(out, rc = -rc);
		}
	}
#else
	rc = dfs_move(dfs, parent, name, new_parent, new_name, NULL);
	if (rc) {
		fprintf(stderr, "Failed to move %s to %s (%d)\n",
			old_path, new_path, rc);
		D_GOTO(out, rc = -rc);
	}
#endif

out:
	if (name)
		free(name);
	if (dir_name)
		free(dir_name);
	if (new_name)
		free(new_name);
	if (new_dir_name)
		free(new_dir_name);
	if (parent)
		dfs_release(parent);
	if (new_parent)
		dfs_release(new_parent);
	return rc;
}

static int
dfuse_sync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
	int rc;

	FUNC_ENTER("path = %s\n", path);

	rc = dfs_sync(dfs);
	return -rc;
}

static int
dfuse_setxattr(const char *path, const char *name, const char *val,
	       size_t size, int flags)
{
	dfs_obj_t *obj = NULL;
	int rc;

	FUNC_ENTER("path = %s, xattr name = %s, val = %s\n", path, name, val);

	rc = dfs_lookup(dfs, path, O_RDWR, &obj, NULL, NULL);
	if (rc)
		return -rc;

	rc = dfs_setxattr(dfs, obj, name, val, size, flags);
	dfs_release(obj);
	return -rc;
}

static int
dfuse_getxattr(const char *path, const char *name, char *val, size_t size)
{
	dfs_obj_t *obj = NULL;
	int rc;

	FUNC_ENTER("path = %s, xattr name = %s\n", path, name);

	rc = dfs_lookup(dfs, path, O_RDONLY, &obj, NULL, NULL);
	if (rc)
		return -rc;

	rc = dfs_getxattr(dfs, obj, name, val, &size);
	if (rc)
		D_GOTO(out, rc);

	rc = (int)size;
out:
	dfs_release(obj);
	return rc;
}

static int
dfuse_listxattr(const char *path, char *list, size_t size)
{
	dfs_obj_t *obj = NULL;
	int rc;

	FUNC_ENTER("path = %s\n", path);

	rc = dfs_lookup(dfs, path, O_RDONLY, &obj, NULL, NULL);
	if (rc)
		return -rc;

	rc = dfs_listxattr(dfs, obj, list, &size);
	if (rc)
		D_GOTO(out, rc = -rc);

	rc = (int)size;
out:
	dfs_release(obj);
	return rc;
}

static int
dfuse_removexattr(const char *path, const char *name)
{
	dfs_obj_t *obj = NULL;
	int rc;

	FUNC_ENTER("path = %s, xattr name = %s\n", path, name);

	rc = dfs_lookup(dfs, path, O_RDWR, &obj, NULL, NULL);
	if (rc)
		return -rc;

	rc = dfs_removexattr(dfs, obj, name);
	dfs_release(obj);
	return -rc;
}

static void *
dfuse_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	struct fuse_context *context = fuse_get_context();
	void *handle = context->private_data;

	cfg->nullpath_ok = 1;

	return handle;
}

static struct fuse_operations dfuse_ops = {
	.access		= dfuse_access,
	.chmod		= dfuse_chmod,
	.create		= dfuse_create,
	.fsync		= dfuse_sync,
	.fsyncdir	= dfuse_sync,
	.getattr	= dfuse_getattr,
	.init		= dfuse_init,
	.mkdir		= dfuse_mkdir,
	.open		= dfuse_open,
	.opendir	= dfuse_opendir,
	.read		= dfuse_read,
	.readdir	= dfuse_readdir,
	.readlink	= dfuse_readlink,
	.release	= dfuse_release,
	.releasedir	= dfuse_release,
	.rmdir		= dfuse_rmdir,
	.rename		= dfuse_rename,
	.symlink	= dfuse_symlink,
	.truncate	= dfuse_truncate,
	.unlink		= dfuse_unlink,
	.write		= dfuse_write,
	.setxattr	= dfuse_setxattr,
	.getxattr	= dfuse_getxattr,
	.listxattr	= dfuse_listxattr,
	.removexattr	= dfuse_removexattr,
};

static void usage(const char *progname)
{
	printf(
"usage: %s mountpoint [options]\n"
"\n"
"	-h, --help	print help\n"
"	-V, --version	print version\n"
"	-f		foreground operation\n"
"	-s		disable multi-threaded operation\n"
"	-d, --debug	print some debugging information (implies -f)\n"
"	-p		DAOS pool uuid to connect with\n"
"	-l		DAOS pool service rank list\n"
"	-g		DAOS server group name to connect to\n"
"	-c		DAOS cont uuid to create/mount\n"
"	--root-cont	mount the special root container on the pool\n"
"\n"
"FUSE Options:\n",
progname);
}

#define DFUSE_OPT(t, p, v) { t, offsetof(struct dfuse_data, p), v }

static struct fuse_opt dfuse_opts[] = {
	DFUSE_OPT("-h", show_help, 1),
	DFUSE_OPT("--help", show_help, 1),
	DFUSE_OPT("-V", show_version, 1),
	DFUSE_OPT("--version", show_version, 1),
	DFUSE_OPT("-d", debug, 1),
	DFUSE_OPT("--debug", debug, 1),
	DFUSE_OPT("-f", foreground, 1),
	DFUSE_OPT("-s", singlethread, 1),
	DFUSE_OPT("-p %s", pool, 0),
	DFUSE_OPT("-g %s", group, 0),
	DFUSE_OPT("-c %s", cont, 0),
	DFUSE_OPT("--root-cont", root_cont, 1),
	FUSE_OPT_END
};

static int
dfuse_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
	switch (key) {
	case FUSE_OPT_KEY_NONOPT:
		dfuse_fs.mountpoint = strdup(arg);
		return 0;
	}
	return 1;
}

int main(int argc, char *argv[])
{
	struct fuse_args	args = FUSE_ARGS_INIT(argc, argv);
	bool			free_pool = true;
	bool			free_grp = true;
	uuid_t			pool_uuid, co_uuid;
	daos_pool_info_t	pool_info = {};
	daos_cont_info_t	co_info;
	daos_handle_t		poh, coh;
	int			rc;

	memset(&dfuse_fs, 0, sizeof(dfuse_fs));
	fuse_opt_parse(&args, &dfuse_fs, dfuse_opts, dfuse_opt_proc);

	if (dfuse_fs.show_version) {
		fprintf(stderr, "FUSE library version %s\n", fuse_pkgversion());
		exit(0);
	}
	if (dfuse_fs.show_help) {
		usage(args.argv[0]);
		fuse_lib_help(&args);
		exit(0);
	}
	if (!dfuse_fs.singlethread) {
		fprintf(stderr, "multi-threaded execution is not supported\n");
		fprintf(stderr, "try `%s -s' for single threaded\n", argv[0]);
		exit(1);
	}

	if (!dfuse_fs.mountpoint) {
		fprintf(stderr, "missing mountpoint\n");
		fprintf(stderr, "see `%s -h' for usage\n", argv[0]);
		exit(1);
	}

	/** Parse DAOS pool uuid */
	if (!dfuse_fs.pool) {
		dfuse_fs.pool = getenv("DAOS_POOL");
		free_pool = false;
	}
	if (!dfuse_fs.pool) {
		fprintf(stderr, "missing pool uuid\n");
		fprintf(stderr, "see `%s -h' for usage\n", argv[0]);
		D_GOTO(out_str, rc = 1);
	}
	if (uuid_parse(dfuse_fs.pool, pool_uuid) < 0) {
		fprintf(stderr, "Invalid pool uuid\n");
		D_GOTO(out_str, rc = 1);
	}

	/** Check if server group is passed */
	if (!dfuse_fs.group) {
		dfuse_fs.group = getenv("DAOS_GROUP");
		free_grp = false;
	}

	/** Initialize DAOS stack */
	rc = daos_init();
	if (rc) {
		fprintf(stderr, "daos_init() failed with %d\n", rc);
		D_GOTO(out_str, rc = 1);
	}

	if (dfuse_fs.debug) {
		fprintf(stderr, "Pool Connect...\n");
		fprintf(stderr, "DFS Pool = %s\n", dfuse_fs.pool);
	}

	/** Connect to DAOS pool */
	rc = daos_pool_connect(pool_uuid, dfuse_fs.group, NULL, DAOS_PC_RW,
			       &poh, &pool_info, NULL);
	if (rc < 0) {
		fprintf(stderr, "Failed to connect to pool (%d)\n", rc);
		D_GOTO(out_daos, rc = 1);
	}

	if (dfuse_fs.root_cont) {
		if (dfuse_fs.debug)
			fprintf(stderr, "Mounting root Container\n");

		rc = dfs_mount_root_cont(poh, &dfs);
		if (rc) {
			fprintf(stderr, "failed to mount root cont (%d)\n", rc);
			D_GOTO(out_disc, rc = 1);
		}
	} else {
		if (!dfuse_fs.cont) {
			fprintf(stderr, "Missing Container UUID\n");
			D_GOTO(out_disc, rc = 1);
		}

		if (dfuse_fs.debug)
			fprintf(stderr, "DFS Container: %s\n", dfuse_fs.cont);

		rc = uuid_parse(dfuse_fs.cont, co_uuid);
		if (rc) {
			fprintf(stderr, "Failed to parse Container uuid\n");
			D_GOTO(out_disc, rc = 1);
		}

		/** Open the DAOS container first (the mountpoint) */
		rc = daos_cont_open(poh, co_uuid, DAOS_COO_RW, &coh, &co_info,
				    NULL);
		if (rc) {
			fprintf(stderr, "Failed container open (%d)\n", rc);
			D_GOTO(out_disc, rc = 1);
		}

		rc = dfs_mount(poh, coh, O_RDWR, &dfs);
		if (rc) {
			daos_cont_close(coh, NULL);
			fprintf(stderr, "Failed dfs mount (%d)\n", rc);
			D_GOTO(out_disc, rc = 1);
		}
	}

	dfuse_fs.fuse = fuse_new(&args, &dfuse_ops, sizeof(dfuse_ops), NULL);
	if (dfuse_fs.fuse == NULL) {
		fprintf(stderr, "Could not initialize dfuse fs");
		D_GOTO(out_dmount, rc = 1);
	}

	rc = fuse_mount(dfuse_fs.fuse, dfuse_fs.mountpoint);
	if (rc) {
		fprintf(stderr, "Could not mount dfuse fs");
		D_GOTO(out_fdest, rc = 1);
	}
	fuse_opt_free_args(&args);

	rc = fuse_daemonize(dfuse_fs.foreground);
	if (rc == -1)
		D_GOTO(out_fmount, rc = 1);

	D_ASSERT(dfuse_fs.singlethread);
	rc = fuse_loop(dfuse_fs.fuse);

out_fmount:
	fuse_unmount(dfuse_fs.fuse);
out_fdest:
	fuse_destroy(dfuse_fs.fuse);
out_dmount:
	if (dfuse_fs.root_cont)
		dfs_umount_root_cont(dfs);
	else
		dfs_umount(dfs);
	if (!dfuse_fs.root_cont)
		daos_cont_close(coh, NULL);
out_disc:
	daos_pool_disconnect(poh, NULL);
out_daos:
	daos_fini();
out_str:
	if (dfuse_fs.mountpoint)
		free(dfuse_fs.mountpoint);
	if (dfuse_fs.pool && free_pool)
		free(dfuse_fs.pool);
	if (dfuse_fs.group && free_grp)
		free(dfuse_fs.group);
	return rc;
}
