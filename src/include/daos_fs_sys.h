/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * DAOS File System "Sys" API
 *
 * The DFS Sys API provides a simplified layer directly on top of the DFS API that is more
 * similar to the equivalent POSIX libraries. While the DFS Sys API stands on its own,
 * the DFS API can be used directly by getting the DFS Object with dfs_sys2base().
 */

#ifndef __DAOS_FS_SYS_H__
#define __DAOS_FS_SYS_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <dirent.h>

#include <daos.h>
#include <daos_fs.h>

/** Mount flags for dfs_sys_mount. By default, mount with caching and locking turned on. */
#define DFS_SYS_NO_CACHE 1 /**< Turn off directory caching */
#define DFS_SYS_NO_LOCK  2 /**< Turn off locking. Useful for single-threaded applications. */

/** struct holding attributes for the dfs_sys calls */
typedef struct dfs_sys dfs_sys_t;

/**
 * Wrapper around dfs_connect for the dfs_sys API.
 *
 * \param[in]	pool	Pool label.
 * \param[in]	sys	DAOS system name to use for the pool connect.
 *			Pass NULL to use the default system.
 * \param[in]	cont	Container label.
 * \param[in]	mflags	Mount flags (O_RDONLY or O_RDWR, O_CREAT). O_CREAT attempts to create the
 *			DFS container if it doesn't exists.
 * \param[in]	sflags	Sys flags (DFS_SYS_NO_CACHE or DFS_SYS_NO_LOCK)
 * \param[in]	attr	Optional set of properties and attributes to set on the container (if being
 *			created). Pass NULL to use default.
 * \param[out]	dfs_sys	Pointer to the file system object created.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_connect(const char *pool, const char *sys, const char *cont, int mflags, int sflags,
		dfs_attr_t *attr, dfs_sys_t **dfs_sys);

/**
 * Umount the DFS sys namespace, and release the ref count on the container and pool handles. This
 * should be called on a dfs_sys mount created with dfs_sys_connect() and not dfs_sys_mount().
 *
 * \param[in]	dfs_sys	Pointer to the mounted file system from dfs_sys_connect().
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_disconnect(dfs_sys_t *dfs_sys);

/**
 * Convert a local dfs_sys mount including the pool and container handles to global representation
 * data which can be shared with peer processes.
 * If glob->iov_buf is set to NULL, the actual size of the global handle is
 * returned through glob->iov_buf_len.
 * This function does not involve any communication and does not block.
 *
 * \param[in]	dfs_sys	valid dfs_sys mount to be shared.
 * \param[out]	glob	pointer to iov of the buffer to store mount information.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_local2global_all(dfs_sys_t *dfs_sys, d_iov_t *glob);

/**
 * Create a dfs_sys mount from global representation data. This has to be
 * closed with dfs_sys_disconnect().
 *
 * \param[in]	mflags	Mount flags (O_RDONLY or O_RDWR). If 0, inherit flags
 *			of serialized DFS Sys handle.
 * \param[in]	sflags	Sys flags (DFS_SYS_NO_CACHE or DFS_SYS_NO_LOCK).
 *			This is not inherited from the DFS Sys handle.
 * \param[in]	glob	Global (shared) representation of a collective handle to be extracted.
 * \param[out]	dfs_sys	Returned dfs_sys mount.
 */
int
dfs_sys_global2local_all(int mflags, int sflags, d_iov_t glob, dfs_sys_t **dfs_sys);

/**
 * Mount a file system with dfs_mount and optionally initialize a cache.
 *
 * \param[in]	poh	Pool connection handle.
 * \param[in]	coh	Container open handle.
 * \param[in]	mflags	Mount flags (O_RDONLY or O_RDWR).
 * \param[in]	sflags	Sys flags (DFS_SYS_NO_CACHE or DFS_SYS_NO_LOCK)
 * \param[out]	dfs_sys	Pointer to the file system object created.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_mount(daos_handle_t poh, daos_handle_t coh, int mflags, int sflags, dfs_sys_t **dfs_sys);

/**
 * Unmount a file system with dfs_mount.
 *
 * \param[in]	dfs_sys	Pointer to the mounted file system.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_umount(dfs_sys_t *dfs_sys);

/**
 * Convert a local dfs_sys mount to global representation data which can be
 * shared with peer processes.
 * If glob->iov_buf is set to NULL, the actual size of the global handle is
 * returned through glob->iov_buf_len.
 * This function does not involve any communication and does not block.
 *
 * \param[in]	dfs_sys	valid dfs_sys mount to be shared.
 * \param[out]	glob	pointer to iov of the buffer to store mount information.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_local2global(dfs_sys_t *dfs_sys, d_iov_t *glob);

/**
 * Create a dfs_sys mount from global representation data. This has to be
 * closed with dfs_sys_umount().
 *
 * \param[in]	poh	Pool connection handle.
 * \param[in]	coh	Container open handle.
 * \param[in]	mflags	Mount flags (O_RDONLY or O_RDWR). If 0, inherit flags
 *			of serialized DFS Sys handle.
 * \param[in]	sflags	Sys flags (DFS_SYS_NO_CACHE or DFS_SYS_NO_LOCK).
 *			This is not inherited from the DFS Sys handle.
 * \param[in]	glob	Global (shared) representation of a collective handle to be extracted.
 * \param[out]	dfs_sys	Returned dfs_sys mount.
 */
int
dfs_sys_global2local(daos_handle_t poh, daos_handle_t coh, int mflags, int sflags, d_iov_t glob,
		     dfs_sys_t **dfs_sys);

/**
 * Get the underlying dfs_t from the dfs_sys_t.
 * This should not be closed with dfs_umount().
 *
 * \param[in]	dfs_sys	Pointer to the mounted file system.
 * \param[out]	dfs	Pointer to the underlying dfs_t.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys2base(dfs_sys_t *dfs_sys, dfs_t **dfs);

/**
 * Check access permissions on a path. Similar to Linux access(2).
 * By default, symlinks are dereferenced.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	mask	accessibility check(s) to be performed.
 *			It should be either the value F_OK, or a mask with
 *			bitwise OR of one or more of R_OK, W_OK, and X_OK.
 * \param[in]	flags	Access flags (O_NOFOLLOW).
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_access(dfs_sys_t *dfs_sys, const char *path, int mask, int flags);

/**
 * Change permission access bits. Symlinks are dereferenced.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	mode	New permission access modes. For now, we don't support
 *			the sticky bit, setuid, and setgid.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_chmod(dfs_sys_t *dfs_sys, const char *path, mode_t mode);

/**
 * Change owner/group. Symlinks are dereferenced. Since uid and gid are not enforced at the DFS
 * level, we do not also enforce the process privileges to be able to change the uid and gid. Any
 * process with write access to the DFS container can make changes to the uid and gid using this
 * function.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	uid	change owner of file (-1 to leave unchanged).
 * \param[in]	gid	change group of file (-1 to leave unchanged).
 * \param[in]	flags	(O_NOFOLLOW)
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_chown(dfs_sys_t *dfs_sys, const char *path, uid_t uid, gid_t gid, int flags);

/**
 * set stat attributes for a file and fetch new values.
 * By default, if the object is a symlink the link itself is modified.
 * See dfs_sys_stat() for which entries are filled.
 *
 * \param[in]	dfs_sys	Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in,out]
 *		stbuf	[in]: Stat struct with the members set.
 *			[out]: Stat struct with all valid members filled.
 * \param[in]	flags	Bitmask of flags to set.
 * \param[in]	sflags	(O_NOFOLLOW)
 *
 * \return		0 on Success. errno code on Failure.
 */
int
dfs_sys_setattr(dfs_sys_t *dfs_sys, const char *path, struct stat *stbuf, int flags, int sflags);

/**
 * Set atime and mtime of a path. This currently does not set
 * nanosecond precision.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	times	[0]: atime to set
 *			[1]: mtime to set
 * \param[in]	flags	(O_NOFOLLOW)
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_utimens(dfs_sys_t *dfs_sys, const char *path, const struct timespec times[2], int flags);

/**
 * stat attributes of an entry. By default, if object is a symlink,
 * the link itself is interrogated. The following elements of the
 * stat struct are populated (the rest are set to 0):
 * mode_t	st_mode;
 * uid_t	st_uid;
 * gid_t	st_gid;
 * off_t	st_size;
 * blkcnt_t	st_blocks;
 * struct timespec st_atim;
 * struct timespec st_mtim;
 * struct timespec st_ctim;
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	flags	Stat flags (O_NOFOLLOW).
 * \param[out]	stbuf	Stat struct with the members above filled.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_stat(dfs_sys_t *dfs_sys, const char *path, int flags, struct stat *stbuf);
/**
 * Create a file or directory.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of new object.
 * \param[in]	mode	mode_t (permissions + type).
 * \param[in]	cid	DAOS object class id (pass 0 for default MAX_RW).
 *			Valid on create only; ignored otherwise.
 * \param[in]	chunk_size
 *			Chunk size of the array object to be created.
 *			(pass 0 for default 1 MiB chunk size).
 *			Valid on file create only; ignored otherwise.
 *
 * \return		0 on success, errno code on failure.
 *			EEXIST	If path already exists.
 */
int
dfs_sys_mknod(dfs_sys_t *dfs_sys, const char *path, mode_t mode, daos_oclass_id_t cid,
	      daos_size_t chunk_size);

/**
 * list extended attributes of a path and place them all in a buffer
 * NULL terminated one after the other. By default, if path is a
 * symlink, the link itself is interrogated.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in,out]
 *		list	[in]: Allocated buffer for all xattr names.
 *			[out]: Names placed after each other (null terminated).
 * \param[in,out]
 *		size	[in]: Size of list. [out]: Actual size of list.
 * \param[in]	flags	(O_NOFOLLOW)
 *
 * \return		0 on success, errno code on failure.
 *			ERANGE	If size is too small.
 */
int
dfs_sys_listxattr(dfs_sys_t *dfs_sys, const char *path, char *list, daos_size_t *size, int flags);

/**
 * Get extended attribute of a path. By default, if path is a symlink,
 * the link itself is interrogated.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	name	Name of xattr to get.
 * \param[out]	value	Buffer to place value of xattr.
 * \param[in,out]
 *		size	[in]: Size of buffer value. [out]: Actual size of xattr.
 * \param[in]	flags	(O_NOFOLLOW)
 *
 * \return		0 on success, errno code on failure.
 *			ERANGE	If size is too small.
 */
int
dfs_sys_getxattr(dfs_sys_t *dfs_sys, const char *path, const char *name, void *value,
		 daos_size_t *size, int flags);

/**
 * Set extended attribute on a path (file, dir, syml). By default, if path
 * is a symlink, the value is set on the symlink itself.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	name	Name of xattr to add.
 * \param[in]	value	Value of xattr.
 * \param[in]	size	Size in bytes of the value.
 * \param[in]	flags	Set flags. passing 0 does not check for xattr existence.
 *			XATTR_CREATE: create or fail if xattr exists.
 *			XATTR_REPLACE: replace or fail if xattr does not exist.
 * \param[in]	sflags	(O_NOFOLLOW)
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_setxattr(dfs_sys_t *dfs_sys, const char *path, const char *name, const void *value,
		 daos_size_t size, int flags, int sflags);

/**
 * Remove extended attribute of a path. By default, if path is a symlink,
 * the link itself is interogated.
 *
 * \param[in]	dfs_sys	Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	name	Name of xattr to remove.
 * \param[in]	flags	(O_NOFOLLOW)
 *
 * \return		0 on success, errno code on failure.
 */

int
dfs_sys_removexattr(dfs_sys_t *dfs_sys, const char *path, const char *name, int flags);

/**
 * Retrieve Symlink value of path if it's a symlink. If the buffer size passed
 * in is not large enough, we copy up to size of the buffer, and update the
 * size to actual value size. The size returned includes the null terminator.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in,out]
 *		buf	[in]: Allocated buffer for value.
 *			[out]: Symlink value.
 * \param[in,out]
 *		size	[in]: Size of buffer passed in. [out]: Actual size of value.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_readlink(dfs_sys_t *dfs_sys, const char *path, char *buf, daos_size_t *size);

/**
 * Create a symlink.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	target	Symlink value.
 * \param[in]	path	Path to the new symlink.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_symlink(dfs_sys_t *dfs_sys, const char *target, const char *path);

/**
 * Create/Open a directory, file, or Symlink.
 * The object must be released with dfs_sys_close().
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of the object to create/open.
 * \param[in]	mode	mode_t (permissions + type).
 * \param[in]	flags	Access flags (handles: O_RDONLY, O_RDWR, O_EXCL,
 *			O_CREAT, O_TRUNC, O_NOFOLLOW).
 * \param[in]	cid	DAOS object class id (pass 0 for default MAX_RW).
 *			Valid on create only; ignored otherwise.
 * \param[in]	chunk_size
 *			Chunk size of the array object to be created.
 *			(pass 0 for default 1 MiB chunk size).
 *			Valid on file create only; ignored otherwise.
 * \param[in]	value	Symlink value (NULL if not syml).
 * \param[out]	obj	Pointer to object opened.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_open(dfs_sys_t *dfs_sys, const char *path, mode_t mode, int flags, daos_oclass_id_t cid,
	     daos_size_t chunk_size, const char *value, dfs_obj_t **obj);

/**
 * Close/release open object.
 *
 * \param[in]	obj	Object to release.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_close(dfs_obj_t *obj);

/**
 * Read data from the file object, and return actual data read.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	obj	Opened file object.
 * \param[in,out]
 *		buf	[in]: Allocated buffer for data.
 *			[out]: Actual data read.
 * \param[in]	off	Offset into the file to read from.
 * \param[in,out]
 *		size	[in]: Size of buffer passed in. [out]: Actual size of data read.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_read(dfs_sys_t *dfs_sys, dfs_obj_t *obj, void *buf, daos_off_t off, daos_size_t *size,
	     daos_event_t *ev);

/**
 * Write data to the file object.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	obj	Opened file object.
 * \param[in]	buf	Data to write.
 * \param[in]	off	Offset into the file to write to.
 * \param[in,out]
 *		size	[in]: Size of buffer passed in. [out]: Actual size of data written.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_write(dfs_sys_t *dfs_sys, dfs_obj_t *obj, const void *buf, daos_off_t off,
	      daos_size_t *size, daos_event_t *ev);

/**
 * Punch a hole in the file starting at offset to len. If len is set to
 * DFS_MAX_FSIZE, this will be a truncate operation to punch all bytes in the
 * file above offset. If the file size is smaller than offset, the file is
 * extended to offset and len is ignored.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of file.
 * \param[in]	offset	offset of file to punch at.
 * \param[in]	len	number of bytes to punch.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_punch(dfs_sys_t *dfs_sys, const char *path, daos_off_t offset, daos_off_t len);

/**
 * Remove an object identified by path. If object is a directory and is
 * non-empty; this will fail unless force option is true. If object is a
 * symlink, the symlink is removed.
 * See dfs_remove() for details.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	force	If true, remove dir even if non-empty.
 * \param[in]	oid	Optionally return the DAOS Object ID of the removed obj.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_remove(dfs_sys_t *dfs_sys, const char *path, bool force, daos_obj_id_t *oid);

/**
 * Similar to dfs_sys_remove but optionally enforces a type check
 * on the entry.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	path	Link path of object.
 * \param[in]	force	If true, remove dir even if non-empty.
 * \param[in]	mode	mode_t (S_IFREG | S_IFDIR | S_IFLNK).
 *			Pass 0 skip the type check.
 * \param[out]	oid	Optionally return the DAOS Object ID of the removed obj.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_remove_type(dfs_sys_t *dfs_sys, const char *path, bool force, mode_t mode,
		    daos_obj_id_t *oid);

/**
 * Create a directory.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	dir	Link path of new dir.
 * \param[in]	mode	mkdir mode.
 * \param[in]	cid	DAOS object class id (pass 0 for default MAX_RW).
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_mkdir(dfs_sys_t *dfs_sys, const char *dir, mode_t mode, daos_oclass_id_t cid);

/**
 * Open a directory.
 * The directory must be closed with dfs_sys_closedir().
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	dir	Link path of dir.
 * \param[in]	flags	(O_NOFOLLOW)
 * \param[out]	dirp	Pointer to the open directory.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_opendir(dfs_sys_t *dfs_sys, const char *dir, int flags, DIR **dirp);

/**
 * Close a directory opened with dfs_sys_opendir().
 *
 * \param[in]	dirp	Pointer to the open directory.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_closedir(DIR *dirp);

/**
 * Return the next directory entry in a directory opened by
 * dfs_sys_readdir(), or NULL if there are no more entries.
 *
 * \param[in]	dfs_sys Pointer to the mounted file system.
 * \param[in]	dirp	Pointer to open directory.
 * \param[out]	dirent	Pointer to the next directory entry.
 *			This is NULL if there are no more directory entries.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_sys_readdir(dfs_sys_t *dfs_sys, DIR *dirp, struct dirent **dirent);

#if defined(__cplusplus)
}
#endif
#endif /* __DAOS_FS_SYS_H__ */
