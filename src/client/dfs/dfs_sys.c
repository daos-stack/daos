/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <libgen.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <linux/xattr.h>
#include <daos/common.h>
#include <daos/event.h>

#include <gurt/atomic.h>

#include "daos.h"
#include "daos_fs.h"

#include "daos_fs_sys.h"

/** Number of entries for readdir */
#define DFS_SYS_NUM_DIRENTS 24

/** Size of the hash table */
#define DFS_SYS_HASH_SIZE 12

struct dfs_sys {
	dfs_t			*dfs;	/* mounted filesystem */
	struct d_hash_table	*hash;	/* optional lookup hash */
};

/** struct holding parsed dirname, name, and cached parent obj */
struct sys_path {
	const char	*path;		/* original, full path */
	size_t		path_len;	/* length of path */
	char		*alloc;		/* allocation of dir and name */
	char		*dir_name;	/* dirname(path) */
	size_t		dir_name_len;	/* length of dir_name */
	char		*name;		/* basename(path) */
	size_t		name_len;	/* length of name */
	dfs_obj_t	*parent;	/* dir_name obj */
	d_list_t	*rlink;		/* hash link */
};

struct dfs_sys_dir {
	dfs_obj_t	*obj;
	struct dirent	ents[DFS_SYS_NUM_DIRENTS];
	daos_anchor_t	anchor;
	uint32_t	num_ents;
};

/**
 * Hash handle for each entry.
 */
struct hash_hdl {
	dfs_obj_t	*obj;
	d_list_t	entry;
	char		*name;
	size_t		name_len;
	ATOMIC uint	ref;
};

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
	struct hash_hdl *hdl;
	uint		oldref;

	hdl = hash_hdl_obj(rlink);
	oldref = atomic_fetch_add_relaxed(&hdl->ref, 1);
	D_DEBUG(DB_TRACE, "%s, oldref = %u\n",
		hdl->name, oldref);
}

/**
 * Decrease reference to hash entry.
 */
static bool
hash_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct hash_hdl	*hdl;
	uint		oldref;

	hdl = hash_hdl_obj(rlink);
	oldref = atomic_fetch_sub_relaxed(&hdl->ref, 1);
	D_DEBUG(DB_TRACE, "%s, oldref = %u\n",
		hdl->name, oldref);
	D_ASSERT(oldref > 0);

	return oldref == 1;
}

/*
 * Free a hash entry.
 */
static void
hash_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct hash_hdl *hdl = hash_hdl_obj(rlink);

	D_DEBUG(DB_TRACE, "name=%s\n", hdl->name);

	dfs_release(hdl->obj);
	D_FREE(hdl->name);
	D_FREE(hdl);
}

static uint32_t
hash_rec_hash(struct d_hash_table *htable, d_list_t *rlink)
{
	struct hash_hdl *hdl = hash_hdl_obj(rlink);

	return d_hash_string_u32(hdl->name, hdl->name_len);
}

/**
 * Operations for the hash table.
 */
static d_hash_table_ops_t hash_hdl_ops = {
	.hop_key_cmp	= hash_key_cmp,
	.hop_rec_addref = hash_rec_addref,
	.hop_rec_decref	= hash_rec_decref,
	.hop_rec_free	= hash_rec_free,
	.hop_rec_hash	= hash_rec_hash
};

/**
 * Try to get dir_name from the hash.
 * If not found, call dfs_lookup on name
 * and store in the hash.
 * Stores the hashed obj in parent.
 */
/* TODO
 * We could recursively cache a path instead of blindly calling
 * dfs_lookup. For example, consider lookup on these paths in succession:
 *   dfs_lookup("/path/to/dir1")
 *   dfs_lookup("/path/to/dir2")
 * Internally, dfs_lookup will be fetching and iterating over the same
 * entries along the path. We could more efficiently do this:
 *   dfs_lookup_rel("/", "path")
 *   dfs_lookup_rel("/path", "to")
 *   dfs_lookup_rel("/path/to", "dir1")
 *   dfs_lookup_rel("/path/to", "dir2")
 */
static int
hash_lookup(dfs_sys_t *dfs_sys, struct sys_path *sys_path)
{
	struct hash_hdl	*hdl;
	d_list_t	*rlink;
	mode_t		mode;
	int		rc = 0;

	/* If we aren't caching, just call dfs_lookup */
	if (dfs_sys->hash == NULL) {
		rc = dfs_lookup(dfs_sys->dfs, sys_path->dir_name, O_RDWR,
				&sys_path->parent, &mode, NULL);
		if (rc != 0) {
			D_DEBUG(DB_TRACE, "failed to lookup %s: (%d)\n",
				sys_path->dir_name, rc);
			return rc;
		}

		/* We only cache directories */
		if (!S_ISDIR(mode)) {
			dfs_release(sys_path->parent);
			return ENOTDIR;
		}
		return rc;
	}

	/* If cached, return it */
	rlink = d_hash_rec_find(dfs_sys->hash, sys_path->dir_name,
				sys_path->dir_name_len);
	if (rlink != NULL) {
		hdl = hash_hdl_obj(rlink);
		D_GOTO(out, rc = 0);
	}

	/* Not cached, so create an entry and add it */
	D_ALLOC_PTR(hdl);
	if (hdl == NULL)
		return ENOMEM;

	hdl->name_len = sys_path->dir_name_len;
	D_STRNDUP(hdl->name, sys_path->dir_name, sys_path->dir_name_len);
	if (hdl->name == NULL)
		D_GOTO(free_hdl, rc = ENOMEM);

	/* Start with 2 so we have exactly 1 reference left
	 * when dfs_sys_umount is called.
	 */
	atomic_store_relaxed(&hdl->ref, 2);

	/* Lookup name in dfs */
	rc = dfs_lookup(dfs_sys->dfs, sys_path->dir_name, O_RDWR, &hdl->obj,
			&mode, NULL);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "failed to lookup %s: (%d)\n", sys_path->dir_name, rc);
		D_GOTO(free_hdl_name, rc);
	}

	/* We only cache directories */
	if (!S_ISDIR(mode))
		D_GOTO(free_hdl_obj, rc = ENOTDIR);

	/* call find_insert in case another thread added the same entry
	 * after calling find.
	 */
	rlink = d_hash_rec_find_insert(dfs_sys->hash, hdl->name,
				       sys_path->dir_name_len, &hdl->entry);
	if (rlink != &hdl->entry) {
		/* another thread beat us. Use the existing entry. */
		sys_path->parent = hash_hdl_obj(rlink)->obj;
		sys_path->rlink = rlink;
		D_GOTO(free_hdl_obj, rc = 0);
	}

out:
	sys_path->parent = hdl->obj;
	sys_path->rlink = rlink;
	return rc;

free_hdl_obj:
	dfs_release(hdl->obj);
free_hdl_name:
	D_FREE(hdl->name);
free_hdl:
	D_FREE(hdl);
	return rc;
}

/**
 * Free a struct sys_path.
 */
static void
sys_path_free(dfs_sys_t *dfs_sys, struct sys_path *sys_path)
{
	D_FREE(sys_path->alloc);
	if (dfs_sys->hash == NULL)
		dfs_release(sys_path->parent);
	else if (sys_path->rlink != NULL)
		d_hash_rec_decref(dfs_sys->hash, sys_path->rlink);
}

/**
 * Set up a struct sys_path.
 * Parse path into dirname and basename stored in a single
 * allocation separated by null terminators.
 * Lookup dir_name in the hash.
 */
static int
sys_path_parse(dfs_sys_t *dfs_sys, struct sys_path *sys_path,
	       const char *path)
{
	int	rc;
	char	*new_path = NULL;
	char	*dir_name = NULL;
	char	*name = NULL;
	size_t	path_len;
	size_t	dir_name_len = 0;
	size_t	name_len = 0;
	size_t	i;
	size_t	end_idx = 0;
	size_t	slash_idx = 0;

	if (path == NULL)
		return EINVAL;
	if (path[0] != '/')
		return EINVAL;

	path_len = strnlen(path, PATH_MAX);
	if (path_len > PATH_MAX - 1)
		return ENAMETOOLONG;

	/** Find end, not including trailing slashes */
	for (i = path_len - 1; i > 0; i--) {
		if (path[i] != '/') {
			end_idx = i;
			break;
		}
	}

	/** Find last slash */
	for (; i > 0; i--) {
		if (path[i] == '/') {
			slash_idx = i;
			break;
		}
	}

	/** Build a single path of the format:
	 * <dir> + '\0' + <name> + '\0'
	 */
	D_ALLOC(new_path, end_idx + 3);
	if (new_path == NULL) {
		return ENOMEM;
	}

	/** Copy the dirname */
	if (slash_idx == 0)
		dir_name_len = 1;
	else
		dir_name_len = slash_idx;
	strncpy(new_path, path, dir_name_len);
	dir_name = new_path;

	/** Copy the basename */
	name_len = end_idx - slash_idx;
	if (name_len > 0) {
		new_path[dir_name_len] = 0;
		strncpy(new_path + dir_name_len + 1, path + slash_idx + 1,
			name_len);
		name = new_path + dir_name_len + 1;
	}

	sys_path->path = path;
	sys_path->path_len = path_len;
	sys_path->alloc = new_path;
	sys_path->dir_name = dir_name;
	sys_path->dir_name_len = dir_name_len;
	sys_path->name = name;
	sys_path->name_len = name_len;
	sys_path->parent = NULL;
	sys_path->rlink = NULL;

	rc = hash_lookup(dfs_sys, sys_path);
	if (rc != 0) {
		sys_path_free(dfs_sys, sys_path);
		return rc;
	}

	return rc;
}

static int
init_sys(int mflags, int sflags, dfs_sys_t **_dfs_sys)
{
	dfs_sys_t	*dfs_sys;
	int		rc;
	uint32_t	hash_feats = D_HASH_FT_EPHEMERAL;
	bool		no_cache = false;
	bool		no_lock = false;

	if (_dfs_sys == NULL)
		return EINVAL;

	if (sflags & DFS_SYS_NO_CACHE) {
		D_DEBUG(DB_TRACE, "mount: DFS_SYS_NO_CACHE.\n");
		no_cache = true;
		sflags &= ~DFS_SYS_NO_CACHE;
	}
	if (sflags & DFS_SYS_NO_LOCK) {
		D_DEBUG(DB_TRACE, "mount: DFS_SYS_NO_LOCK.\n");
		no_lock = true;
		sflags &= ~DFS_SYS_NO_LOCK;
	}

	if (sflags != 0) {
		D_DEBUG(DB_TRACE, "mount: invalid sflags.\n");
		return EINVAL;
	}

	D_ALLOC_PTR(dfs_sys);
	if (dfs_sys == NULL)
		return ENOMEM;

	*_dfs_sys = dfs_sys;

	if (no_cache)
		return 0;

	/* Initialize the hash */
	if (no_lock)
		hash_feats |= D_HASH_FT_NOLOCK;
	else
		hash_feats |= D_HASH_FT_RWLOCK;

	rc = d_hash_table_create(hash_feats, DFS_SYS_HASH_SIZE, NULL, &hash_hdl_ops,
				 &dfs_sys->hash);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "failed to create hash table "
			DF_RC"\n", DP_RC(rc));
		D_GOTO(err_dfs_sys, rc = daos_der2errno(rc));
	}

	return 0;

err_dfs_sys:
	D_FREE(dfs_sys);
	return rc;
}

int
dfs_sys_connect(const char *pool, const char *sys, const char *cont, int mflags, int sflags,
		dfs_attr_t *attr, dfs_sys_t **_dfs_sys)
{
	dfs_sys_t	*dfs_sys;
	int		rc;

	rc = init_sys(mflags, sflags, &dfs_sys);
	if (rc)
		return rc;

	/* Mount dfs */
	rc = dfs_connect(pool, sys, cont, mflags, attr, &dfs_sys->dfs);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "dfs_connect() failed (%d)\n", rc);
		D_GOTO(err_dfs_sys, rc);
	}

	*_dfs_sys = dfs_sys;
	return rc;

err_dfs_sys:
	if (dfs_sys->hash != NULL)
		d_hash_table_destroy(dfs_sys->hash, false);
	D_FREE(dfs_sys);
	return rc;
}

static int
fini_sys(dfs_sys_t *dfs_sys, bool disconnect)
{
	int		rc;
	d_list_t	*rlink;

	if (dfs_sys == NULL)
		return EINVAL;

	if (dfs_sys->hash != NULL) {
		/* Decrease each reference by one. */
		while (1) {
			rlink = d_hash_rec_first(dfs_sys->hash);
			if (rlink == NULL)
				break;

			d_hash_rec_decref(dfs_sys->hash, rlink);
		}

		rc = d_hash_table_destroy(dfs_sys->hash, false);
		if (rc) {
			D_DEBUG(DB_TRACE, "failed to destroy hash table: "DF_RC"\n", DP_RC(rc));
			return daos_der2errno(rc);
		}
		dfs_sys->hash = NULL;
	}

	if (dfs_sys->dfs != NULL) {
		if (disconnect) {
			rc = dfs_disconnect(dfs_sys->dfs);
			if (rc) {
				D_DEBUG(DB_TRACE, "dfs_disconnect() failed (%d)\n", rc);
				return rc;
			}
		} else {
			rc = dfs_umount(dfs_sys->dfs);
			if (rc) {
				D_DEBUG(DB_TRACE, "dfs_umount() failed (%d)\n", rc);
				return rc;
			}
		}
		dfs_sys->dfs = NULL;
	}

	/* Only free if umount was successful */
	D_FREE(dfs_sys);

	return 0;
}

int
dfs_sys_disconnect(dfs_sys_t *dfs_sys)
{
	return fini_sys(dfs_sys, true);
}

int
dfs_sys_mount(daos_handle_t poh, daos_handle_t coh, int mflags, int sflags,
	      dfs_sys_t **_dfs_sys)
{
	dfs_sys_t	*dfs_sys;
	int		rc;

	rc = init_sys(mflags, sflags, &dfs_sys);
	if (rc)
		return rc;

	/* Mount dfs */
	rc = dfs_mount(poh, coh, mflags, &dfs_sys->dfs);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "dfs_mount() failed (%d)\n", rc);
		D_GOTO(err_dfs_sys, rc);
	}

	*_dfs_sys = dfs_sys;
	return rc;

err_dfs_sys:
	if (dfs_sys->hash != NULL)
		d_hash_table_destroy(dfs_sys->hash, false);
	D_FREE(dfs_sys);
	return rc;
}

/**
 * Unmount a file system with dfs_mount and destroy the hash.
 */
int
dfs_sys_umount(dfs_sys_t *dfs_sys)
{
	return fini_sys(dfs_sys, false);
}

int
dfs_sys_local2global_all(dfs_sys_t *dfs_sys, d_iov_t *glob)
{
	if (dfs_sys == NULL)
		return EINVAL;

	/* TODO serialize the dfs_sys flags as well */
	return dfs_local2global_all(dfs_sys->dfs, glob);
}

int
dfs_sys_global2local_all(int mflags, int sflags, d_iov_t glob, dfs_sys_t **_dfs_sys)
{
	dfs_sys_t	*dfs_sys;
	int		rc;

	rc = init_sys(mflags, sflags, &dfs_sys);
	if (rc)
		return rc;

	rc = dfs_global2local_all(mflags, glob, &dfs_sys->dfs);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "dfs_global2local() failed (%d)\n", rc);
		D_GOTO(err_dfs_sys, rc);
	}

	*_dfs_sys = dfs_sys;
	return rc;

err_dfs_sys:
	if (dfs_sys->hash != NULL)
		d_hash_table_destroy(dfs_sys->hash, false);
	D_FREE(dfs_sys);
	return rc;
}

int
dfs_sys_local2global(dfs_sys_t *dfs_sys, d_iov_t *glob)
{
	if (dfs_sys == NULL)
		return EINVAL;

	/* TODO serialize the dfs_sys flags as well */
	return dfs_local2global(dfs_sys->dfs, glob);
}

int
dfs_sys_global2local(daos_handle_t poh, daos_handle_t coh, int mflags,
		     int sflags, d_iov_t glob, dfs_sys_t **_dfs_sys)
{
	dfs_sys_t	*dfs_sys;
	int		rc;

	rc = init_sys(mflags, sflags, &dfs_sys);
	if (rc)
		return rc;

	rc = dfs_global2local(poh, coh, mflags, glob, &dfs_sys->dfs);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "dfs_global2local() failed (%d)\n", rc);
		D_GOTO(err_dfs_sys, rc);
	}

	*_dfs_sys = dfs_sys;
	return rc;

err_dfs_sys:
	if (dfs_sys->hash != NULL)
		d_hash_table_destroy(dfs_sys->hash, false);
	D_FREE(dfs_sys);
	return rc;
}

int
dfs_sys2base(dfs_sys_t *dfs_sys, dfs_t **_dfs)
{
	if (dfs_sys == NULL)
		return EINVAL;

	*_dfs = dfs_sys->dfs;
	return 0;
}

int
dfs_sys_access(dfs_sys_t *dfs_sys, const char *path, int mask, int flags)
{
	int		rc;
	struct sys_path	sys_path;
	dfs_obj_t	*obj;
	mode_t		mode;
	int		lookup_flags = O_RDWR;

	if (dfs_sys == NULL)
		return EINVAL;
	if (path == NULL)
		return EINVAL;
	if (flags & AT_EACCESS)
		return ENOTSUP;

	rc = sys_path_parse(dfs_sys, &sys_path, path);
	if (rc != 0)
		return rc;

	/* If not following symlinks then lookup the obj first
	 * and return success for a symlink.
	 */
	if ((sys_path.name != NULL) && (flags & O_NOFOLLOW)) {
		lookup_flags |= O_NOFOLLOW;

		/* Lookup the obj and get it's mode */
		rc = dfs_lookup_rel(dfs_sys->dfs, sys_path.parent,
				    sys_path.name, lookup_flags, &obj,
				    &mode, NULL);
		if (rc != 0) {
			D_DEBUG(DB_TRACE, "failed to lookup %s: (%d)\n",
				sys_path.name, rc);
			D_GOTO(out_free_path, rc);
		}

		dfs_release(obj);

		/* A link itself is always accessible */
		if (S_ISLNK(mode))
			D_GOTO(out_free_path, rc = 0);
	}

	/* Either we are following symlinks, the obj is root,
	 * or the obj is not a symlink. So just call dfs_access.
	 */
	rc = dfs_access(dfs_sys->dfs, sys_path.parent, sys_path.name, mask);

out_free_path:
	sys_path_free(dfs_sys, &sys_path);
	return rc;
}

int
dfs_sys_chmod(dfs_sys_t *dfs_sys, const char *path, mode_t mode)
{
	int		rc;
	struct sys_path	sys_path;

	if (dfs_sys == NULL)
		return EINVAL;
	if (path == NULL)
		return EINVAL;

	rc = sys_path_parse(dfs_sys, &sys_path, path);
	if (rc != 0)
		return rc;

	rc = dfs_chmod(dfs_sys->dfs, sys_path.parent, sys_path.name, mode);

	sys_path_free(dfs_sys, &sys_path);

	return rc;
}

int
dfs_sys_chown(dfs_sys_t *dfs_sys, const char *path, uid_t uid, gid_t gid, int flags)
{
	int		rc;
	struct sys_path	sys_path;

	if (dfs_sys == NULL)
		return EINVAL;
	if (path == NULL)
		return EINVAL;

	rc = sys_path_parse(dfs_sys, &sys_path, path);
	if (rc != 0)
		return rc;

	rc = dfs_chown(dfs_sys->dfs, sys_path.parent, sys_path.name, uid, gid, flags);

	sys_path_free(dfs_sys, &sys_path);

	return rc;
}

int
dfs_sys_setattr(dfs_sys_t *dfs_sys, const char *path, struct stat *stbuf,
		int flags, int sflags)
{
	int		rc;
	struct sys_path	sys_path;
	dfs_obj_t	*obj;
	int		lookup_flags = O_RDWR;

	if (dfs_sys == NULL)
		return EINVAL;
	if (path == NULL)
		return EINVAL;

	rc = sys_path_parse(dfs_sys, &sys_path, path);
	if (rc != 0)
		return rc;

	/* No need to lookup the root */
	if (sys_path.name == NULL) {
		obj = sys_path.parent;
		goto setattr;
	}

	if (sflags & O_NOFOLLOW)
		lookup_flags |= O_NOFOLLOW;

	rc = dfs_lookup_rel(dfs_sys->dfs, sys_path.parent,
			    sys_path.name, lookup_flags, &obj, NULL, NULL);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "failed to lookup %s: (%d)\n",
			sys_path.name, rc);
		D_GOTO(out_free_path, rc);
	}

setattr:
	rc = dfs_osetattr(dfs_sys->dfs, obj, stbuf, flags);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "failed to setattr %s: (%d)\n",
			sys_path.name, rc);
		D_GOTO(out_close_obj, rc);
	}

out_close_obj:
	if (sys_path.name != NULL)
		dfs_release(obj);
out_free_path:
	sys_path_free(dfs_sys, &sys_path);
	return rc;
}

int
dfs_sys_utimens(dfs_sys_t *dfs_sys, const char *path,
		const struct timespec times[2], int flags)
{
	int		rc;
	struct stat	stbuf;

	stbuf.st_atim = times[0];
	stbuf.st_mtim = times[1];

	rc = dfs_sys_setattr(dfs_sys, path, &stbuf,
			     DFS_SET_ATTR_ATIME | DFS_SET_ATTR_MTIME,
			     flags);
	return rc;
}

int
dfs_sys_stat(dfs_sys_t *dfs_sys, const char *path, int flags,
	     struct stat *buf)
{
	int             rc;
	struct sys_path sys_path;
	dfs_obj_t      *obj;
	int             lookup_flags = O_RDWR;

	if (dfs_sys == NULL)
		return EINVAL;
	if (path == NULL)
		return EINVAL;

	rc = sys_path_parse(dfs_sys, &sys_path, path);
	if (rc != 0)
		return rc;

	/* No need to lookup root */
	if (sys_path.name == NULL) {
		rc = dfs_ostat(dfs_sys->dfs, sys_path.parent, buf);
		D_GOTO(out_free_path, rc);
	}

	if (flags & O_NOFOLLOW)
		lookup_flags |= O_NOFOLLOW;

	rc = dfs_lookup_rel(dfs_sys->dfs, sys_path.parent,
			    sys_path.name, lookup_flags, &obj, NULL, buf);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "failed to lookup %s: (%d)\n",
			sys_path.name, rc);
		D_GOTO(out_free_path, rc);
	}
	dfs_release(obj);

out_free_path:
	sys_path_free(dfs_sys, &sys_path);
	return rc;
}

int
dfs_sys_mknod(dfs_sys_t *dfs_sys, const char *path, mode_t mode,
	      daos_oclass_id_t cid, daos_size_t chunk_size)
{
	int		rc;
	struct sys_path	sys_path;
	dfs_obj_t	*obj;

	if (dfs_sys == NULL)
		return EINVAL;
	if (path == NULL)
		return EINVAL;

	rc = sys_path_parse(dfs_sys, &sys_path, path);
	if (rc != 0)
		return rc;

	rc = dfs_open(dfs_sys->dfs, sys_path.parent, sys_path.name, mode,
		      O_CREAT | O_EXCL, cid, chunk_size, NULL, &obj);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "failed to open %s: (%d)\n",
			sys_path.name, rc);
		D_GOTO(out_free_path, rc);
	}

	dfs_release(obj);

out_free_path:
	sys_path_free(dfs_sys, &sys_path);
	return rc;
}

int
dfs_sys_listxattr(dfs_sys_t *dfs_sys, const char *path, char *list,
		  daos_size_t *size, int flags)
{
	int		rc;
	struct sys_path	sys_path;
	dfs_obj_t	*obj;
	daos_size_t	got_size = *size;
	int		lookup_flags = O_RDONLY;

	if (dfs_sys == NULL)
		return EINVAL;
	if (path == NULL)
		return EINVAL;

	rc = sys_path_parse(dfs_sys, &sys_path, path);
	if (rc != 0)
		return rc;

	/* No need to lookup root */
	if (sys_path.name == NULL) {
		obj = sys_path.parent;
		goto listxattr;
	}

	if (flags & O_NOFOLLOW)
		lookup_flags |= O_NOFOLLOW;

	rc = dfs_lookup_rel(dfs_sys->dfs, sys_path.parent, sys_path.name,
			    lookup_flags, &obj, NULL, NULL);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "failed to lookup %s: (%d)\n",
			sys_path.name, rc);
		D_GOTO(out_free_path, rc);
	}

listxattr:
	rc = dfs_listxattr(dfs_sys->dfs, obj, list, &got_size);
	if (rc != 0)
		D_GOTO(out_free_obj, rc);

	if (*size < got_size)
		rc = ERANGE;

	*size = got_size;

out_free_obj:
	if (sys_path.name != NULL)
		dfs_release(obj);
out_free_path:
	sys_path_free(dfs_sys, &sys_path);
	return rc;
}

int
dfs_sys_getxattr(dfs_sys_t *dfs_sys, const char *path, const char *name,
		 void *value, daos_size_t *size, int flags)
{
	int		rc;
	struct sys_path	sys_path;
	dfs_obj_t	*obj;
	daos_size_t	got_size = *size;
	int		lookup_flags = O_RDONLY;

	if (dfs_sys == NULL)
		return EINVAL;
	if (path == NULL)
		return EINVAL;

	rc = sys_path_parse(dfs_sys, &sys_path, path);
	if (rc != 0)
		return rc;

	/* No need to lookup root */
	if (sys_path.name == NULL) {
		obj = sys_path.parent;
		goto getxattr;
	}

	if (flags & O_NOFOLLOW)
		lookup_flags |= O_NOFOLLOW;

	rc = dfs_lookup_rel(dfs_sys->dfs, sys_path.parent, sys_path.name,
			    lookup_flags, &obj, NULL, NULL);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "failed to lookup %s (%d)\n",
			sys_path.name, rc);
		D_GOTO(out_free_path, rc);
	}

getxattr:
	rc = dfs_getxattr(dfs_sys->dfs, obj, name, value, &got_size);
	if (rc != 0)
		D_GOTO(out_free_obj, rc);

	if (*size < got_size)
		rc = ERANGE;

	*size = got_size;

out_free_obj:
	if (sys_path.name != NULL)
		dfs_release(obj);
out_free_path:
	sys_path_free(dfs_sys, &sys_path);
	return rc;
}

int
dfs_sys_setxattr(dfs_sys_t *dfs_sys, const char *path, const char *name,
		 const void *value, daos_size_t size, int flags, int sflags)
{
	int		rc;
	struct sys_path	sys_path;
	dfs_obj_t	*obj;
	int		lookup_flags = O_RDWR;

	if (dfs_sys == NULL)
		return EINVAL;
	if (path == NULL)
		return EINVAL;

	rc = sys_path_parse(dfs_sys, &sys_path, path);
	if (rc != 0)
		return rc;

	/* No need to lookup root */
	if (sys_path.name == NULL) {
		obj = sys_path.parent;
		goto setxattr;
	}

	if (sflags & O_NOFOLLOW)
		lookup_flags |= O_NOFOLLOW;

	rc = dfs_lookup_rel(dfs_sys->dfs, sys_path.parent, sys_path.name,
			    lookup_flags, &obj, NULL, NULL);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "failed to lookup %s: (%d)\n",
			sys_path.name, rc);
		D_GOTO(out_free_path, rc);
	}

setxattr:
	rc = dfs_setxattr(dfs_sys->dfs, obj, name, value, size, flags);
	if (rc != 0)
		D_GOTO(out_free_obj, rc);

out_free_obj:
	if (sys_path.name != NULL)
		dfs_release(obj);
out_free_path:
	sys_path_free(dfs_sys, &sys_path);
	return rc;
}

int
dfs_sys_removexattr(dfs_sys_t *dfs_sys, const char *path, const char *name,
		    int flags)
{
	int		rc;
	struct sys_path	sys_path;
	dfs_obj_t	*obj;
	int		lookup_flags = O_RDWR;

	if (dfs_sys == NULL)
		return EINVAL;
	if (path == NULL)
		return EINVAL;

	rc = sys_path_parse(dfs_sys, &sys_path, path);
	if (rc != 0)
		return rc;

	/* No need to lookup root */
	if (sys_path.name == NULL) {
		obj = sys_path.parent;
		goto removexattr;
	}

	if (flags & O_NOFOLLOW)
		lookup_flags |= O_NOFOLLOW;

	rc = dfs_lookup_rel(dfs_sys->dfs, sys_path.parent, sys_path.name,
			    lookup_flags, &obj, NULL, NULL);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "failed to lookup %s: (%d)\n",
			sys_path.name, rc);
		D_GOTO(out_free_path, rc);
	}

removexattr:
	rc = dfs_removexattr(dfs_sys->dfs, obj, name);
	if (rc != 0)
		D_GOTO(out_free_obj, rc);

out_free_obj:
	if (sys_path.name != NULL)
		dfs_release(obj);
out_free_path:
	sys_path_free(dfs_sys, &sys_path);
	return rc;
}

int
dfs_sys_readlink(dfs_sys_t *dfs_sys, const char *path, char *buf,
		 daos_size_t *size)
{
	int		rc;
	struct sys_path	sys_path;
	dfs_obj_t	*obj;
	int		lookup_flags = O_RDONLY | O_NOFOLLOW;

	if (dfs_sys == NULL)
		return EINVAL;
	if (path == NULL)
		return EINVAL;

	rc = sys_path_parse(dfs_sys, &sys_path, path);
	if (rc != 0)
		return rc;

	rc = dfs_lookup_rel(dfs_sys->dfs, sys_path.parent, sys_path.name,
			    lookup_flags, &obj, NULL, NULL);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "failed to lookup %s: (%d)\n",
			sys_path.name, rc);
		D_GOTO(out_free_path, rc);
	}

	rc = dfs_get_symlink_value(obj, buf, size);

	dfs_release(obj);

out_free_path:
	sys_path_free(dfs_sys, &sys_path);
	return rc;
}

int
dfs_sys_symlink(dfs_sys_t *dfs_sys, const char *target, const char *path)
{
	int		rc;
	struct sys_path	sys_path;
	dfs_obj_t	*obj = NULL;

	if (dfs_sys == NULL)
		return EINVAL;
	if (path == NULL)
		return EINVAL;

	rc = sys_path_parse(dfs_sys, &sys_path, path);
	if (rc != 0)
		return rc;

	rc = dfs_open(dfs_sys->dfs, sys_path.parent, sys_path.name,
		      S_IFLNK, O_CREAT | O_EXCL,
		      0, 0, target, &obj);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "failed to open %s: (%d)\n",
			sys_path.name, rc);
		D_GOTO(out_free_path, rc);
	}

	if (obj)
		dfs_release(obj);

out_free_path:
	sys_path_free(dfs_sys, &sys_path);
	return rc;
}

int
dfs_sys_open(dfs_sys_t *dfs_sys, const char *path, mode_t mode, int flags,
	     daos_oclass_id_t cid, daos_size_t chunk_size,
	     const char *value, dfs_obj_t **_obj)
{
	int		rc;
	struct sys_path	sys_path;
	mode_t		actual_mode;
	dfs_obj_t	*obj;

	if (dfs_sys == NULL)
		return EINVAL;
	if (path == NULL)
		return EINVAL;

	if ((mode & S_IFMT) == 0)
		mode |= S_IFREG;

	rc = sys_path_parse(dfs_sys, &sys_path, path);
	if (rc != 0)
		return rc;

	/* If this is root, just dup the handle */
	if (sys_path.name == NULL) {
		if (flags & (O_CREAT | O_EXCL))
			D_GOTO(out_free_path, rc = EEXIST);
		if (!S_ISDIR(mode))
			D_GOTO(out_free_path, rc = ENOTDIR);

		rc = dfs_dup(dfs_sys->dfs, sys_path.parent, mode,
			     _obj);
		if (rc != 0) {
			D_DEBUG(DB_TRACE, "failed to dup %s: (%d)\n",
				sys_path.name, rc);
		}
		D_GOTO(out_free_path, rc);
	}

	/* If creating or not following symlinks, call dfs_open.
	 * Note that this does not handle the case of following a symlink
	 * and creating the target pointed to.
	 */
	if ((flags & O_CREAT) || (flags & O_NOFOLLOW)) {
		rc = dfs_open(dfs_sys->dfs, sys_path.parent, sys_path.name,
			      mode, flags, cid, chunk_size, value, _obj);
		if (rc != 0) {
			D_DEBUG(DB_TRACE, "failed to open %s: (%d)\n",
				sys_path.name, rc);
		}
		D_GOTO(out_free_path, rc);
	}

	/* Call dfs_lookup_rel to follow symlinks */
	rc = dfs_lookup_rel(dfs_sys->dfs, sys_path.parent, sys_path.name,
			    flags, &obj, &actual_mode, NULL);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "failed to lookup %s: (%d)\n",
			sys_path.name, rc);
		D_GOTO(out_free_path, rc);
	}

	/* Make sure the type is the same */
	if ((mode & S_IFMT) != (actual_mode & S_IFMT)) {
		dfs_release(obj);
		D_GOTO(out_free_path, rc = EINVAL);
	}

	*_obj = obj;

out_free_path:
	sys_path_free(dfs_sys, &sys_path);
	return rc;
}

int
dfs_sys_close(dfs_obj_t *obj)
{
	int rc = 0;

	rc = dfs_release(obj);
	return rc;
}

int
dfs_sys_read(dfs_sys_t *dfs_sys, dfs_obj_t *obj, void *buf, daos_off_t off,
	     daos_size_t *size, daos_event_t *ev)
{
	d_iov_t		iov;
	d_sg_list_t	sgl;

	if (dfs_sys == NULL)
		return EINVAL;
	if (buf == NULL)
		return EINVAL;
	if (size == NULL)
		return EINVAL;

	d_iov_set(&iov, buf, *size);
	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;
	sgl.sg_nr_out = 1;

	return dfs_read(dfs_sys->dfs, obj, &sgl, off, size, ev);
}

int
dfs_sys_write(dfs_sys_t *dfs_sys, dfs_obj_t *obj, const void *buf,
	      daos_off_t off, daos_size_t *size, daos_event_t *ev)
{
	d_iov_t		iov;
	d_sg_list_t	sgl;

	if (dfs_sys == NULL)
		return EINVAL;
	if (buf == NULL)
		return EINVAL;
	if (size == NULL)
		return EINVAL;

	d_iov_set(&iov, (void *)buf, *size);
	sgl.sg_nr = 1;
	sgl.sg_iovs = &iov;
	sgl.sg_nr_out = 1;

	return dfs_write(dfs_sys->dfs, obj, &sgl, off, ev);
}

int
dfs_sys_punch(dfs_sys_t *dfs_sys, const char *path,
	      daos_off_t offset, daos_off_t len)
{
	int		rc;
	struct sys_path	sys_path;
	dfs_obj_t	*obj;

	if (dfs_sys == NULL)
		return EINVAL;
	if (path == NULL)
		return EINVAL;

	rc = sys_path_parse(dfs_sys, &sys_path, path);
	if (rc != 0)
		return rc;

	rc = dfs_open(dfs_sys->dfs, sys_path.parent, sys_path.name,
		      S_IFREG, O_RDWR, 0, 0, NULL, &obj);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "failed to open %s: (%d)\n",
			sys_path.name, rc);
		D_GOTO(out, rc);
	}

	rc = dfs_punch(dfs_sys->dfs, obj, offset, len);

	dfs_release(obj);
out:
	sys_path_free(dfs_sys, &sys_path);
	return rc;
}

int
dfs_sys_remove(dfs_sys_t *dfs_sys, const char *path, bool force,
	       daos_obj_id_t *oid)
{
	return dfs_sys_remove_type(dfs_sys, path, force, 0, oid);
}

int
dfs_sys_remove_type(dfs_sys_t *dfs_sys, const char *path, bool force,
		    mode_t mode, daos_obj_id_t *oid)
{
	int		rc;
	struct sys_path	sys_path;
	struct stat	stbuf;
	mode_t		type_mask = mode & S_IFMT;

	if (dfs_sys == NULL)
		return EINVAL;
	if (path == NULL)
		return EINVAL;

	rc = sys_path_parse(dfs_sys, &sys_path, path);
	if (rc != 0)
		return rc;

	/* Can't delete root */
	if (sys_path.name == NULL)
		D_GOTO(out_free_path, rc = EBUSY);

	/* Only check the type if passed in */
	if (type_mask == 0)
		goto remove;

	/* Stat the object and make sure it is the correct type */
	rc = dfs_stat(dfs_sys->dfs, sys_path.parent, sys_path.name, &stbuf);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "failed to stat %s: (%d)\n",
			sys_path.name, rc);
		D_GOTO(out_free_path, rc);
	}

	if (type_mask != (mode & S_IFMT))
		D_GOTO(out_free_path, rc = EINVAL);

remove:
	rc = dfs_remove(dfs_sys->dfs, sys_path.parent, sys_path.name,
			force, oid);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "failed to remove %s: (%d)\n",
			sys_path.name, rc);
		D_GOTO(out_free_path, rc);
	}

	if ((dfs_sys->hash != NULL) &&
	    (type_mask == 0 || type_mask == S_IFDIR)) {
		bool deleted;

		/* TODO - Ideally, we should return something like EBUSY
		 * if there are still open handles to the directory.
		 * TODO - if force=true then we need to delete any child
		 * directories as well.
		 */
		deleted = d_hash_rec_delete(dfs_sys->hash, sys_path.path,
					    sys_path.path_len);
		D_DEBUG(DB_TRACE, "d_hash_rec_delete() %s = %d\n",
			sys_path.path, deleted);
	}

out_free_path:
	sys_path_free(dfs_sys, &sys_path);
	return rc;
}

int
dfs_sys_mkdir(dfs_sys_t *dfs_sys, const char *dir, mode_t mode,
	      daos_oclass_id_t cid)
{
	int		rc;
	struct sys_path	sys_path;

	if (dfs_sys == NULL)
		return EINVAL;
	if (dir == NULL)
		return EINVAL;

	rc = sys_path_parse(dfs_sys, &sys_path, dir);
	if (rc != 0)
		return rc;

	/* Root always already exists */
	if (sys_path.name == NULL)
		D_GOTO(out_free_path, rc = EEXIST);

	rc = dfs_mkdir(dfs_sys->dfs, sys_path.parent, sys_path.name,
		       mode, cid);

out_free_path:
	sys_path_free(dfs_sys, &sys_path);
	return rc;
}

int
dfs_sys_opendir(dfs_sys_t *dfs_sys, const char *dir, int flags, DIR **_dirp)
{
	int			rc;
	struct sys_path		sys_path;
	struct dfs_sys_dir	*sys_dir;
	mode_t			mode;
	int			lookup_flags = O_RDWR;

	if (dfs_sys == NULL)
		return EINVAL;
	if (dir == NULL)
		return EINVAL;

	D_ALLOC_PTR(sys_dir);
	if (sys_dir == NULL)
		return ENOMEM;

	rc = sys_path_parse(dfs_sys, &sys_path, dir);
	if (rc != 0)
		D_GOTO(out_free_dir, rc);

	/* If this is root, just dup the handle */
	if (sys_path.name == NULL) {
		rc = dfs_dup(dfs_sys->dfs, sys_path.parent, O_RDWR,
			     &sys_dir->obj);
		if (rc != 0) {
			D_DEBUG(DB_TRACE, "failed to dup %s: (%d)\n",
				sys_path.name, rc);
			D_GOTO(out_free_path, rc);
		}
		D_GOTO(out, rc);
	}

	if (flags & O_NOFOLLOW)
		lookup_flags |= O_NOFOLLOW;

	rc = dfs_lookup_rel(dfs_sys->dfs, sys_path.parent, sys_path.name,
			    lookup_flags, &sys_dir->obj, &mode, NULL);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "failed to lookup %s: (%d)\n",
			sys_path.name, rc);
		D_GOTO(out_free_path, rc);
	}

	if (!S_ISDIR(mode)) {
		dfs_release(sys_dir->obj);
		D_GOTO(out_free_path, rc = ENOTDIR);
	}

out:
	*_dirp = (DIR *)sys_dir;
	sys_path_free(dfs_sys, &sys_path);
	return rc;
out_free_path:
	sys_path_free(dfs_sys, &sys_path);
out_free_dir:
	D_FREE(sys_dir);
	return rc;
}

int
dfs_sys_closedir(DIR *dirp)
{
	int			rc;
	struct dfs_sys_dir	*sys_dir;

	if (dirp == NULL)
		return EINVAL;

	sys_dir = (struct dfs_sys_dir *)dirp;

	rc = dfs_release(sys_dir->obj);
	D_FREE(sys_dir);

	return rc;
}

int
dfs_sys_readdir(dfs_sys_t *dfs_sys, DIR *dirp, struct dirent **_dirent)
{
	int			rc = 0;
	struct dfs_sys_dir	*sys_dir;

	if (dfs_sys == NULL)
		return EINVAL;
	if (dirp == NULL)
		return EINVAL;

	sys_dir = (struct dfs_sys_dir *)dirp;

	if (sys_dir->num_ents)
		D_GOTO(out, rc = 0);

	sys_dir->num_ents = DFS_SYS_NUM_DIRENTS;
	while (!daos_anchor_is_eof(&sys_dir->anchor)) {
		rc = dfs_readdir(dfs_sys->dfs, sys_dir->obj,
				 &sys_dir->anchor, &sys_dir->num_ents,
				 sys_dir->ents);
		if (rc != 0)
			D_GOTO(out_null, rc);

		/* We have an entry, so return it */
		if (sys_dir->num_ents != 0)
			D_GOTO(out, rc);
	}

out_null:
	sys_dir->num_ents = 0;
	*_dirent = NULL;
	return rc;
out:
	sys_dir->num_ents--;
	*_dirent = &sys_dir->ents[sys_dir->num_ents];
	return rc;
}
