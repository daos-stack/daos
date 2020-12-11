/*
 * PROPOSAL - Both mpifileutils and daos fs copy have need to interface
 * with DFS in a manner more similar to POSIX I/O syscalls.
 * The main difference is that DFS uses object handles to the parent
 * with a string for just the leaf entry, whereas POSIX uses a string
 * for the entire path.
 *
 * For example, consider the difference between DFS mkdir and POSIX mkdir:
 *      // DFS version
 *      int
 *      dfs_mkdir(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode,
 *                daos_oclass_id_t cid);
 *
 *      // POSIX version
 *      int mkdir(const char *pathname, mode_t mode);
 *
 * For most of the calls, the boilerplate code of splitting a path into
 * dirname, basename and calling dfs_lookup on dirname could be abstracted
 * into a function.
 *
 * 1. It would be useful to wrap the dfs_ calls in a posix-equivalent manner,
 *    including errno, etc.
 *    Since most of these POSIX calls are #included from sys, we might want
 *    to use the prefix dfs_sys_ for these wrappers.
 *
 * 2. It would be nice if the calls used a d_hash for dfs_lookup calls,
 *    since the parent directory will likely appear many times.
 *    Since we need the dfs_t and dfs_obj_t in nearly all cases,
 *    perhaps we could put a dfs_t, dfs_obj_t, and d_hash in a struct
 *    that we can pass to each function.
 * */

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

/** struct holding attributes for the dfs_sys calls */
typedef struct dfs_sys dfs_sys_t;
struct dfs_sys {
	dfs_t			*dfs;		/** mounted filesystem */
	struct d_hash_table	*dfs_hash;	/** optional lookup hash */

	/* TODO pass obj separately to only the functions that need it? */
	dfs_obj_t		*obj;		/** the obj */
};

/** struct holding parsed dirname, name, and cached parent obj */
typedef struct dfs_sys_path {
	char		*dir_name;	/** dirname(path) */
	char		*name;		/** basename(path) */
	dfs_obj_t	*parent;	/** dir_name obj */
} dfs_sys_path_t;


/**
 * Hash handle for each entry.
 */
struct dfs_sys_hash_hdl {
	d_list_t	entry;
	dfs_obj_t	*oh;
	char		*name;
	size_t		num_refs; /* TODO datatype? */
};

/**
 * Allocate a new dfs_sys_hash_hdl.
 * TODO maybe just do this inline since only called once
 */
static struct dfs_sys_hash_hdl*
dfs_sys_hash_hdl_new(void)
{
	struct dfs_sys_hash_hdl	*hdl;

	hdl = malloc(sizeof(struct dfs_sys_hash_hdl));
	if (hdl == NULL)
		return NULL;
	hdl->oh = NULL;
	hdl->name = NULL;
	hdl->num_refs = 0;

	return hdl;
}

/*
 * Delete a dfs_sys_hash_hdl.
 */
static void
dfs_sys_hash_hdl_delete(struct dfs_sys_hash_hdl *hdl)
{
	if (hdl != NULL) {
		if (hdl->oh != NULL)
			dfs_release(hdl->oh);
		if (hdl->name != NULL)
			free(hdl->name);
		free(hdl);
	}
}

/*
 * Get a dfs_sys_hash_hdl from the d_list_t.
 */
static inline struct dfs_sys_hash_hdl*
dfs_sys_hash_hdl_obj(d_list_t *rlink)
{
	return container_of(rlink, struct dfs_sys_hash_hdl, entry);
}

/**
 * Compare hash entry key.
 * Simple string comparison of name.
 */
static bool
dfs_sys_hash_key_cmp(struct d_hash_table *table, d_list_t *rlink,
		     const void *key, unsigned int ksize)
{
	struct dfs_sys_hash_hdl	*hdl = dfs_sys_hash_hdl_obj(rlink);

	return (strcmp(hdl->name, (const char *)key) == 0);
}

/**
 * Add reference to hash entry.
 */
static void
dfs_sys_hash_rec_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfs_sys_hash_hdl *hdl = dfs_sys_hash_hdl_obj(rlink);

	hdl->num_refs++;
}

/**
 * Decrease reference to hash entry.
 */
static bool
dfs_sys_hash_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfs_sys_hash_hdl *hdl = dfs_sys_hash_hdl_obj(rlink);
	bool			no_refs;

	D_ASSERT(hdl->num_refs > 0);

	hdl->num_refs--;
	no_refs = (hdl->num_refs == 0);

	return no_refs;
}

/*
 * Free a hash entry.
 */
static void
dfs_sys_hash_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dfs_sys_hash_hdl *hdl = dfs_sys_hash_hdl_obj(rlink);

	/* TODO is the assert necessary? */
	D_ASSERT(d_hash_rec_unlinked(&hdl->entry));
	dfs_sys_hash_hdl_delete(hdl);
}

/**
 * Operations for the hash table.
 */
static d_hash_table_ops_t dfs_sys_hash_hdl_ops = {
	.hop_key_cmp	= dfs_sys_hash_key_cmp,
	.hop_rec_addref = dfs_sys_hash_rec_addref,
	.hop_rec_decref	= dfs_sys_hash_rec_decref,
	.hop_rec_free	= dfs_sys_hash_rec_free
};


/**
 * Mount a file system with dfs_mount and optionally initialize the hash.
 */
/* TODO to be more consistent with DFS, maybe this header */
// dfs_sys_mount(daos_handle_t poh, daos_handle_t coh, int flags, dfs_sys_t **_dfs_sys)
/* TODO maybe let a different flags be passed for dfs_sys hashing/locking/future */
int
dfs_sys_mount(dfs_sys_t *dfs_sys, daos_handle_t *poh, daos_handle_t *coh,
	      int flags, bool use_hash)
{
	int rc;

	rc = dfs_mount(*poh, *coh, flags, &dfs_sys->dfs);
	if (rc) {
	D_ERROR("dfs_mount() failed (%d)\n", rc);
		D_GOTO(out, rc);
	}

	/** TODO make this thread safe */
	/** TODO allow size to be passed in? Default size? */
	if (use_hash) {
		rc = d_hash_table_create(D_HASH_FT_NOLOCK, 16, NULL,
					 &dfs_sys_hash_hdl_ops,
					&dfs_sys->dfs_hash);
		if (rc) {
			/** TODO error handling */
			D_GOTO(err_hash, rc);
		}
	}

err_hash:
	dfs_umount(dfs_sys->dfs);
out:
	return rc;
}

/**
 * Unmount a file system with dfs_mount and destroy the hash.
 */
int
dfs_sys_umount(dfs_sys_t *dfs_sys)
{
	int rc;

	/** TODO check params to this function */
	rc = d_hash_table_destroy(dfs_sys->dfs_hash, true);
	/** TODO error handling */

	rc = dfs_umount(dfs_sys->dfs);
	/** TODO error handling */

	return rc;
}


/**
 * Try to get name from the hash.
 * If not found, call dfs_lookup on name
 * and store in the hash.
 * Stores the hashed obj in _obj.
 */
static int
dfs_sys_hash_lookup(const char *name, dfs_sys_t *dfs_sys, dfs_obj_t **_obj)
{
	struct dfs_sys_hash_hdl	*hdl;
	d_list_t		*rlink;
	size_t			name_len;
	int			rc;

	/** Make sure the hash is initialized */
	/** TODO optionally allow no caching? */
	if (dfs_sys->dfs_hash == NULL)
		return EINVAL;

	name_len = strnlen(name, PATH_MAX);
	if (name_len > PATH_MAX-1)
		return ENAMETOOLONG;

	/** If cached, return it */
	rlink = d_hash_rec_find(dfs_sys->dfs_hash, name, name_len);
	if (rlink != NULL) {
		hdl = dfs_sys_hash_hdl_obj(rlink);
		D_GOTO(out, rc = 0);
	}

	/** Not cached, so add it */
	hdl = dfs_sys_hash_hdl_new();
	if (hdl == NULL)
		return ENOMEM;

	D_STRNDUP(hdl->name, name, PATH_MAX);
	if (hdl->name == NULL)
		return ENOMEM;

	/** Lookup name in dfs */
	rc = dfs_lookup(dfs_sys->dfs, name, O_RDWR, &hdl->oh, NULL, NULL);
	if (rc) {
		D_ERROR("dfs_lookup() %s failed (%d)\n", name, rc);
		D_GOTO(err_hdl, rc);
	}

	/* Store this entry in the hash.
	 * Since we have already called d_hash_rec_find,
	 * pass exclusive=false to avoid another find being called */
	rc = d_hash_rec_insert(dfs_sys->dfs_hash, hdl->name, name_len,
			       &hdl->entry, false);
	if (rc)
		D_GOTO(err_hdl, rc = daos_der2errno(rc));

out:
	*_obj = hdl->oh;
	return rc;
err_hdl:
	dfs_sys_hash_hdl_delete(hdl);
	return rc;
}

static int parse_filename(const char* path, char** _obj_name, char** _cont_name)
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
	if (f1 == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	f2 = strdup(path);
	if (f2 == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	fname = basename(f1);
	cont_name = dirname(f2);

	if (cont_name[0] == '.' || cont_name[0] != '/') {
		char cwd[1024];

		if (getcwd(cwd, 1024) == NULL) {
			rc = -ENOMEM;
			goto out;
		}

		if (strcmp(cont_name, ".") == 0) {
			cont_name = strdup(cwd);
			if (cont_name == NULL) {
				rc = -ENOMEM;
				goto out;
			}
		} else {
			char *new_dir = calloc(strlen(cwd) + strlen(cont_name)
					       + 1, sizeof(char));
			if (new_dir == NULL) {
				rc = -ENOMEM;
				goto out;
			}

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
		if (*_cont_name == NULL) {
			rc = -ENOMEM;
			goto out;
		}
	}

	*_obj_name = strdup(fname);
	if (*_obj_name == NULL) {
		free(*_cont_name);
		*_cont_name = NULL;
		rc = -ENOMEM;
		goto out;
	}

out:
	if (f1)
		free(f1);
	if (f2)
		free(f2);
	return rc;
}

/**
 * Parse path into sys_path->dir_name and sys_path->name.
 */
static int
dfs_sys_path_parse(dfs_sys_path_t *sys_path, const char *path)
{
	/**
	 * TODO integrate with parse_filename so there is only one function.
	 */
	return parse_filename(path, &sys_path->name, &sys_path->dir_name);
}

/**
 * Free a dfs_sys_path_t.
 */
static void
dfs_sys_path_free(dfs_sys_path_t *sys_path)
{
	free(sys_path->dir_name);
	free(sys_path->name);

	/**
	 * TODO Decrement reference to parent.
	 */
}

/**
 * Initialize dfs_sys_path_t.
 */
static int
dfs_sys_path_init(dfs_sys_t *dfs_sys, dfs_sys_path_t *sys_path,
		  const char *path)
{
	int rc;

	rc = dfs_sys_path_parse(sys_path, path);
	if (rc)
		return rc;

	rc = dfs_sys_hash_lookup(path, dfs_sys, &sys_path->parent);
	if (rc) {
		dfs_sys_path_free(sys_path);
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
 * The calls below here should be as similar as possible to the
 * POSIX versions, but with a dfs_sys_t parameter.
 *
 * TODO Do we want these to set errno and return -1, like POSIX,
 * or do we want them to return errno, like DFS?
 */
int
dfs_sys_access(dfs_sys_t *dfs_sys, const char* path, int amode)
{
	int		rc;
	dfs_sys_path_t	sys_path;

	rc = dfs_sys_path_init(dfs_sys, &sys_path, path);
	if (rc)
		D_GOTO(out, rc);

	rc = dfs_access(dfs_sys->dfs, sys_path.parent, sys_path.name, amode);
	if (rc) {
		/* TODO do we want to log errors for the dfs_ calls? */
		D_ERROR("dfs_access %s failed\n", sys_path.name);
		D_GOTO(out_free_path, rc);
	}

out_free_path:
	dfs_sys_path_free(&sys_path);
out:
	/* If we want to set errno, we can do this */
	// errno = rc;
	// rc = errno ? 1 : 0;
	return rc;
}

int
dfs_sys_faccessat(dfs_sys_t *dfs_sys, int dirfd, const char* path, int amode,
		  int flags)
{
	int		rc;
	dfs_sys_path_t  sys_path;
	dfs_obj_t	*obj;
	mode_t		mode;
	int		lookup_flags = O_RDWR;

	if (dirfd != AT_FDCWD)
		D_GOTO(out, rc=ENOTSUP);

	if (flags & AT_EACCESS)
		D_GOTO(out, rc=ENOTSUP);

	rc = dfs_sys_path_init(dfs_sys, &sys_path, path);
	if (rc)
		D_GOTO(out, rc);

	if (flags & AT_SYMLINK_NOFOLLOW)
		lookup_flags |= O_NOFOLLOW;

	/* Lookup the obj and get it's mode */
	rc = dfs_lookup_rel(dfs_sys->dfs, sys_path.parent, sys_path.name,
			    lookup_flags, &obj, &mode, NULL);
	if (rc) {
		/* TODO do we want to log errors for the dfs_ calls? */
		D_ERROR("dfs_lookup_rel %s failed\n", sys_path.name);
		D_GOTO(out_free_path, rc);
	}

	/* A link itself is always accessible */
	if (S_ISLNK(mode))
		D_GOTO(out_free_obj, rc);

	rc = dfs_access(dfs_sys->dfs, sys_path.parent, sys_path.name, amode);
	if (rc) {
		/* TODO do we want to log errors for the dfs_ calls? */
		D_ERROR("dfs_access %s failed\n", sys_path.name);
		D_GOTO(out_free_obj, rc);
	}

out_free_obj:
	dfs_release(obj);
out_free_path:
	dfs_sys_path_free(&sys_path);
out:
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
