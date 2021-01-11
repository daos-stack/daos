/**
 * (C) Copyright 2018-2021 Intel Corporation.
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

/* TODO evaluate includes */
#include <libgen.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <linux/xattr.h>
#include <daos/checksum.h>
#include <daos/common.h>
#include <daos/event.h>
#include <daos/container.h>
#include <daos/array.h>

#include "daos.h"
#include "daos_fs.h"

#include "daos_fs_sys.h"

/* TODO use DF_RC and DP_RC where possible */

/* TODO Any function that takes a char * should take a len as well,
 * and this applies to public as well as internal interfaces. */

/** struct holding attributes for the dfs_sys calls */
/* TODO inline for performance */
struct dfs_sys {
	dfs_t			*dfs;		/** mounted filesystem */
	struct d_hash_table	*dfs_hash;	/** optional lookup hash */
	bool			use_cache;	/** whether to use the cache */
};

/** struct holding parsed dirname, name, and cached parent obj */
/* TODO
 * One thing that would work well is if this simply saved two pointers,
 * and two lengths, but into the same allocation, and used the len to
 * control the length of the dir_name string rather than a \0 character. */
typedef struct sys_path {
	char		*dir_name;	/** dirname(path) */
	char		*name;		/** basename(path) */
	dfs_obj_t	*parent;	/** dir_name obj */
} sys_path_t;


/**
 * Hash handle for each entry.
 */
/* TODO the entry should not be first in this structure,
 * as it turns the containerof into a noop, meaning code that should use it
 * but doesn't will still work.
 * For safety you should put this at a non-zero offset. */
struct hash_hdl {
	d_list_t	entry;
	dfs_obj_t	*obj;
	char		*name;
	size_t		name_len;
	size_t		num_refs; /* TODO datatype? */
};

/*
 * Delete a hash_hdl.
 */
/* TODO inline? */
static void
hash_hdl_free(struct hash_hdl *hdl)
{
	if (hdl != NULL) {
		if (hdl->obj != NULL)
			dfs_release(hdl->obj);
		if (hdl->name != NULL)
			D_FREE(hdl->name);
		D_FREE(hdl);
	}
}

/*
 * Get a hash_hdl from the d_list_t.
 */
static inline struct hash_hdl*
hash_hdl_obj(d_list_t *rlink)
{
	return container_of(rlink, struct hash_hdl, entry);
}

/**
 * Compare hash entry key.
 * Simple string comparison of name.
 */
static bool
hash_key_cmp(struct d_hash_table *table, d_list_t *rlink,
	     const void *key, unsigned int ksize)
{
	struct hash_hdl	*hdl = hash_hdl_obj(rlink);

	if (hdl->name_len != ksize)
		return false;

	return (strncmp(hdl->name, (const char *)key, ksize) == 0);
}

/**
 * Add reference to hash entry.
 */
static void
hash_rec_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct hash_hdl *hdl = hash_hdl_obj(rlink);

	/* TODO use atomics */
	hdl->num_refs++;
}

/**
 * Decrease reference to hash entry.
 */
static bool
hash_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct hash_hdl *hdl = hash_hdl_obj(rlink);
	bool		no_refs;

	D_ASSERT(hdl->num_refs > 0);

	hdl->num_refs--;
	no_refs = (hdl->num_refs == 0);

	return no_refs;
}

/*
 * Free a hash entry.
 */
static void
hash_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct hash_hdl *hdl = hash_hdl_obj(rlink);

	/* TODO is the assert necessary? */
	D_ASSERT(d_hash_rec_unlinked(&hdl->entry));
	hash_hdl_free(hdl);
}

/**
 * Operations for the hash table.
 */
static d_hash_table_ops_t hash_hdl_ops = {
	.hop_key_cmp	= hash_key_cmp,
	.hop_rec_addref = hash_rec_addref,
	.hop_rec_decref	= hash_rec_decref,
	.hop_rec_free	= hash_rec_free
};

/**
 * Try to get name from the hash.
 * If not found, call dfs_lookup on name
 * and store in the hash.
 * Stores the hashed obj in _obj.
 */
static int
hash_lookup(const char *name, dfs_sys_t *dfs_sys, dfs_obj_t **_obj)
{
	struct hash_hdl	*hdl;
	d_list_t	*rlink;
	size_t		name_len;
	mode_t		mode;
	int		rc;

	/* If we aren't caching, just call dfs_lookup */
	if (!dfs_sys->use_cache) {
		rc = dfs_lookup(dfs_sys->dfs, name, O_RDWR, _obj, NULL, NULL);
		if (rc)
			D_ERROR("dfs_lookup() %s failed (%d)\n", name, rc);
		return rc;
	}

	/* Make sure the hash is initialized */
	if (dfs_sys->dfs_hash == NULL)
		return EINVAL;

	name_len = strnlen(name, PATH_MAX);
	if (name_len > PATH_MAX-1)
		return ENAMETOOLONG;

	/* If cached, return it */
	rlink = d_hash_rec_find(dfs_sys->dfs_hash, name, name_len);
	if (rlink != NULL) {
		hdl = hash_hdl_obj(rlink);
		D_GOTO(out, rc = 0);
	}

	/* Not cached, so create an entry and add it */
	D_ALLOC_PTR(hdl);
	if (hdl == NULL)
		return ENOMEM;

	hdl->name_len = name_len;
	D_STRNDUP(hdl->name, name, name_len);
	if (hdl->name == NULL)
		D_GOTO(err_hdl, ENOMEM);

	/* TODO start at 1 ?? */
	hdl->num_refs = 0;

	/* Lookup name in dfs */
	rc = dfs_lookup(dfs_sys->dfs, name, O_RDWR, &hdl->obj, &mode, NULL);
	if (rc) {
		D_ERROR("dfs_lookup() %s failed (%d)\n", name, rc);
		D_GOTO(err_hdl, rc);
	}

	/* We only cache directories. Since we only call this function
	 * with the dirname of a path, anything else is invalid. */
	if (!S_ISDIR(mode))
		D_GOTO(err_hdl, rc = ENOTDIR);

	/* Store this entry in the hash.
	 * Since we have already called d_hash_rec_find,
	 * pass exclusive=false to avoid another find being called */
	/* TODO threadsafe */
	rc = d_hash_rec_insert(dfs_sys->dfs_hash, hdl->name, name_len,
			       &hdl->entry, false);
	/* TODO assert to check rc? can only fail with exclusive=true */
	if (rc)
		D_GOTO(err_hdl, rc = daos_der2errno(rc));

out:
	*_obj = hdl->obj;
	return rc;
err_hdl:
	hash_hdl_free(hdl);
	return rc;
}

static int parse_filename(const char* path, char** _name, char** _dir_name)
{
	/**
	 * TODO clean up this function for this context.
	 * It was copied from somewhere else, so some parts may not be relevant.
	 * Better error handling.
	 * Double check for memory leaks.
	 * Possible efficiency improvements?
	 * Don't support "." and cwd? Since it doesn't make sense for a cont
	 */
	char	*f1 = NULL;
	char	*f2 = NULL;
	char	*fname = NULL;
	char	*cont_name = NULL;
	int	rc = 0;

	if (path == NULL || _name == NULL || _dir_name == NULL)
		return EINVAL;

	if (strcmp(path, "/") == 0) {
		*_dir_name = strdup("/");
		if (*_dir_name == NULL)
			return ENOMEM;
		*_name = NULL;
		return 0;
	}

	f1 = strdup(path);
	if (f1 == NULL)
		D_GOTO(out, rc = ENOMEM);

	f2 = strdup(path);
	if (f2 == NULL)
		D_GOTO(out, rc = ENOMEM);

	/* TODO understand implications of these functions */
	/* TODO basename directly into _name? */
	fname = basename(f1);
	cont_name = dirname(f2);

	if (cont_name[0] != '/') {
		char cwd[1024];

		/* TODO what if cwd is  > 1024? */
		if (getcwd(cwd, 1024) == NULL)
			D_GOTO(out, rc = ENOMEM);

		if (strcmp(cont_name, ".") == 0) {
			cont_name = strdup(cwd);
			if (cont_name == NULL)
				D_GOTO(out, rc = ENOMEM);
		} else {
			/* TODO
			 * STRNDUP or ASPRINTF depending on what you're trying to achieve here. */
			char *new_dir = calloc(strlen(cwd) + strlen(cont_name)
					       + 1, sizeof(char));
			if (new_dir == NULL)
				D_GOTO(out, rc = ENOMEM);

			strcpy(new_dir, cwd);
			if (cont_name[0] == '.') {
				strcat(new_dir, &cont_name[1]);
			} else {
				strcat(new_dir, "/");
				strcat(new_dir, cont_name);
			}
			cont_name = new_dir;
		}
		*_dir_name = cont_name;
	} else {
		*_dir_name = strdup(cont_name);
		if (*_dir_name == NULL)
			D_GOTO(out, rc = ENOMEM);
	}

	*_name = strdup(fname);
	if (*_name == NULL) {
		D_FREE(*_dir_name);
		D_GOTO(out, rc = ENOMEM);
	}

out:
	if (f1)
		D_FREE(f1);
	if (f2)
		D_FREE(f2);
	return rc;
}

/**
 * Parse path into sys_path->dir_name and sys_path->name.
 */
static int
sys_path_parse(sys_path_t *sys_path, const char *path)
{
	/**
	 * TODO integrate with parse_filename so there is only one function.
	 */
	return parse_filename(path, &sys_path->name, &sys_path->dir_name);
}

/**
 * Free a sys_path_t.
 */
static void
sys_path_free(sys_path_t *sys_path)
{
	/* TODO null chekc */
	D_FREE(sys_path->dir_name);
	D_FREE(sys_path->name);

	/**
	 * TODO Decrement reference to parent.
	 */
}

/**
 * Initialize sys_path_t.
 */
/* TODO don't call this "init" */
static int
sys_path_init(dfs_sys_t *dfs_sys, sys_path_t *sys_path,
	      const char *path)
{
	int rc;

	rc = sys_path_parse(sys_path, path);
	if (rc)
		return rc;

	rc = hash_lookup(path, dfs_sys, &sys_path->parent);
	if (rc) {
		sys_path_free(sys_path);
		return rc;
	}

	/**
	 * Handle the case of root "/".
	 * TODO double check this
	 */
	if (sys_path->name == NULL) {
		sys_path->parent = NULL; /* TODO set to root explicitly? */
		sys_path->name = sys_path->dir_name;
		sys_path->dir_name = NULL;
	}

	return rc;
}

/**
 * Mount a file system with dfs_mount and optionally initialize the hash.
 */
int
dfs_sys_mount(daos_handle_t poh, daos_handle_t coh, int flags, int sys_flags,
	      dfs_sys_t **_dfs_sys)
{
	dfs_sys_t	*dfs_sys;
	int		rc;

	if (_dfs_sys == NULL)
		return EINVAL;

	D_ALLOC_PTR(dfs_sys);
	if (dfs_sys == NULL)
		return ENOMEM;

	/* Handle sys_flags */
	dfs_sys->use_cache = !(sys_flags & DFS_SYS_NO_CACHE);

	/* Mount dfs */
	rc = dfs_mount(poh, coh, flags, &dfs_sys->dfs);
	if (rc) {
	D_ERROR("dfs_mount() failed (%d)\n", rc);
		D_GOTO(err_dfs_sys, rc);
	}

	/** TODO make this thread safe */
	/** TODO allow size to be passed in? Default size? */
	/* Initialize the hash */
	if (dfs_sys->use_cache) {
		D_DEBUG(DB_ALL, "DFS_SYS mount with caching.\n");
		rc = d_hash_table_create(D_HASH_FT_NOLOCK, 16, NULL,
					 &hash_hdl_ops,
					 &dfs_sys->dfs_hash);
		if (rc) {
			/** TODO error handling */
			D_GOTO(err_hash, rc);
		}
	}

	return rc;

err_hash:
	dfs_umount(dfs_sys->dfs);
err_dfs_sys:
	D_FREE(dfs_sys);
	return rc;
}

/**
 * Unmount a file system with dfs_mount and destroy the hash.
 */
int
dfs_sys_umount(dfs_sys_t *dfs_sys)
{
	int rc;

	if (dfs_sys->use_cache) {
		rc = d_hash_table_destroy(dfs_sys->dfs_hash, false);
		if (rc)
			D_ERROR("d_hash_table_destroy() failed, " DF_RC "\n",
				DP_RC(rc));
	}

	rc = dfs_umount(dfs_sys->dfs);
	if (rc)
		D_ERROR("dfs_umount() failed, " DF_RC "\n",
			DP_RC(rc));

	return rc;
}

/**
 * The calls below here should be as similar as possible to the
 * POSIX versions, but with a dfs_sys_t parameter.
 */
/* TODO just add flags here for symlink? */
int
dfs_sys_access(dfs_sys_t *dfs_sys, const char *path, int amode)
{
	int		rc;
	sys_path_t	sys_path;

	rc = sys_path_init(dfs_sys, &sys_path, path);
	if (rc)
		return rc;

	rc = dfs_access(dfs_sys->dfs, sys_path.parent, sys_path.name, amode);
	if (rc) {
		/* TODO do we want to log errors for the dfs_ calls? */
		D_ERROR("dfs_access() %s failed\n", sys_path.name);
		D_GOTO(out_free_path, rc);
	}

out_free_path:
	sys_path_free(&sys_path);
	return rc;
}

/* TODO Either add flags to access or implement this one properly */
int
dfs_sys_faccessat(dfs_sys_t *dfs_sys, int dirfd, const char *path, int amode,
		  int flags)
{
	int		rc;
	sys_path_t  sys_path;
	dfs_obj_t	*obj;
	mode_t		mode;
	int		lookup_flags = O_RDWR;

	if (dirfd != AT_FDCWD)
		return ENOTSUP;

	if (flags & AT_EACCESS)
		return ENOTSUP;

	rc = sys_path_init(dfs_sys, &sys_path, path);
	if (rc)
		return rc;

	if (flags & AT_SYMLINK_NOFOLLOW)
		lookup_flags |= O_NOFOLLOW;

	/* Lookup the obj and get it's mode */
	rc = dfs_lookup_rel(dfs_sys->dfs, sys_path.parent, sys_path.name,
			    lookup_flags, &obj, &mode, NULL);
	if (rc) {
		/* TODO do we want to log errors for the dfs_ calls? */
		D_ERROR("dfs_lookup_rel() %s failed\n", sys_path.name);
		D_GOTO(out_free_path, rc);
	}

	/* A link itself is always accessible */
	if (S_ISLNK(mode))
		D_GOTO(out_free_obj, rc);

	rc = dfs_access(dfs_sys->dfs, sys_path.parent, sys_path.name, amode);
	if (rc) {
		/* TODO do we want to log errors for the dfs_ calls? */
		D_ERROR("dfs_access() %s failed\n", sys_path.name);
		D_GOTO(out_free_obj, rc);
	}

out_free_obj:
	dfs_release(obj);
out_free_path:
	sys_path_free(&sys_path);
	return rc;
}

int
dfs_sys_chmod(dfs_sys_t *dfs_sys, const char* path, mode_t mode);

int
dfs_sys_utimensat(dfs_sys_t *dfs_sys, const char *pathname,
		  const struct timespec times[2], int flags);

int
dfs_sys_lstat(dfs_sys_t *dfs_sys, const char* path, struct stat* buf);

int
dfs_sys_stat(dfs_sys_t *dfs_sys, const char* path, struct stat* buf);

int
dfs_sys_mknod(dfs_sys_t *dfs_sys, const char* path, mode_t mode, dev_t dev);

/* TODO Will initially only have pread and pwrite.
 * no lseek functions.
 * Caller will be responsible for offsets. */
/**
 * TODO - Many more functions:
 * mknod
 * listxattr
 * llistxattr
 * getxattr
 * lgetxattr
 * lsetxattr
 * readlink
 * symlink
 * open
 * close
 * lseek
 * read
 * pread
 * write
 * pwrite
 * truncate
 * ftruncate
 * unlink
 * mkdir
 * opendir
 * closedir
 */
