/*
 * (C) Copyright 2018-2019 Intel Corporation.
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
/**
 * \file
 *
 * DAOS File System API
 *
 * The DFS API provides an encapuslated namespace with a POSIX like API directly
 * on top of the DAOS API. The namespace is encapsulated under a single DAOS
 * container where directories and files are objects in that container.
 */

#ifndef __DAOS_FS_H__
#define __DAOS_FS_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <dirent.h>

#define DFS_MAX_PATH NAME_MAX
#define DFS_MAX_FSIZE (~0ULL)

typedef struct dfs_obj dfs_obj_t;
typedef struct dfs dfs_t;

/**
 * Mount a file system over DAOS. The pool and container handle must remain
 * connected/open until after dfs_umount() is called; otherwise access to the
 * dfs namespace will fail.
 *
 * The mount will create a root directory (DAOS object) for the file system. The
 * user will associate the dfs object returned with a mount point.
 *
 * \param[in]	poh	Pool connection handle
 * \param[in]	coh	Container open handle.
 * \param[in]	flags	Mount flags (O_RDONLY or O_RDWR).
 * \param[out]	dfs	Pointer to the file system object created.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_mount(daos_handle_t poh, daos_handle_t coh, int flags, dfs_t **dfs);

/**
 * Unmount a DAOS file system. This closes open handles to the root object and
 * commits the epoch at current timestamp. The internal dfs struct is freed, so
 * further access to that dfs will be invalid.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_umount(dfs_t *dfs);

/**
 * Convert from a dfs_obj_t to a daos_obj_id_t.
 *
 * \param[in]	obj	Object to convert
 * \param[out]	oid	Daos object ID.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_obj2id(dfs_obj_t *obj, daos_obj_id_t *oid);

/**
 * Lookup a path in the DFS and return the associated open object and mode.
 * The object must be released with dfs_release().
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	path	Path to lookup.
 * \param[in]	flags	Access flags to open with (O_RDONLY or O_RDWR).
 * \param[out]	obj	Pointer to the object looked up.
 * \params[out]	mode	mode_t (permissions + type).
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_lookup(dfs_t *dfs, const char *path, int flags, dfs_obj_t **obj,
	   mode_t *mode);

/**
 * Lookup an entry in the parent object and return the associated open object
 * and mode of that entry.  If the entry is a symlink, the symlink value is not
 * resolved and the user can decide what to do to further resolve the value of
 * the symlink. The object must be released with dfs_release().
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	parent	Opened parent directory object. If NULL, use root obj.
 *			This is useful in cases where the creator/opener is
 *			working in a flat namespace and doesn't need to
 *			lookup/release the root object.
 * \param[in]	name	Link name of the object to create/open.
 * \param[in]	flags	Access flags to open with (O_RDONLY or O_RDWR).
 * \param[out]	obj	Pointer to the object looked up.
 * \params[out]	mode	Optional mode_t (permissions + type).
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_lookup_rel(dfs_t *dfs, dfs_obj_t *parent, const char *name, int flags,
	       dfs_obj_t **_obj, mode_t *mode);

/**
 * Create/Open a directory, file, or Symlink.
 * The object must be released with dfs_release().
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	parent	Opened parent directory object. If NULL, use root obj.
 *			This is useful in cases where the creator/opener is
 *			working in a flat namespace and doesn't need to
 *			lookup/release the root object.
 * \param[in]	name	Link name of the object to create/open.
 * \param[in]	mode	mode_t (permissions + type).
 * \param[in]	flags	Access flags (O_RDONLY, O_RDWR, O_EXCL, O_CREAT).
 * \param[in]	cid	DAOS object class id (pass 0 for default MAX_RW).
 *			Valid on create only; ignored otherwise.
 * \param[in]	chunk_size
 *			Chunk size of the array object to be created.
 *			(pass 0 for default 1 MiB chunk size).
 *			Valid on file create only; ignored otherwise.
 * \param[in]	value	Symlink value (NULL if not syml).
 * \param[out]	obj	Pointer to object opened.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_open(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode,
	 int flags, daos_oclass_id_t cid, daos_size_t chunk_size,
	 const char *value, dfs_obj_t **obj);

/**
 * Duplicate the DFS object without any RPCs (locally) by using the existing
 * open handles. This is used mostly for low-level fuse to avoid re-opening. The
 * duplicated object must be released with dfs_release().
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Object to dup.
 * \param[in]	flags	Access flags to open with (O_RDONLY or O_RDWR).
 * \param[out]	new_obj	DFS object that is duplicated/opened.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_dup(dfs_t *dfs, dfs_obj_t *obj, int flags, dfs_obj_t **new_obj);

/**
 * Close/release open object.
 *
 * \param[in]	obj	Object to release.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_release(dfs_obj_t *obj);

/**
 * Read data from the file object, and return actual data read.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Opened file object.
 * \param[in]	sgl	Scatter/Gather list for data buffer.
 * \param[in]	off	Offset into the file to read from.
 * \param[out]	read_size
 *			How much data is actually read.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_read(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t sgl, daos_off_t off,
	 daos_size_t *read_size);

/**
 * Write data to the file object.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Opened file object.
 * \param[in]	sgl	Scatter/Gather list for data buffer.
 * \param[in]	off	Offset into the file to write to.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_write(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t sgl, daos_off_t off);

/**
 * Query size of file data.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Opened file object.
 * \param[out]	size	Size of file.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_get_size(dfs_t *dfs, dfs_obj_t *obj, daos_size_t *size);

/**
 * Punch a hole in the file starting at offset to len. If len is set to
 * DFS_MAX_FSIZE, this will be a truncate operation to punch all bytes in the
 * file above offset. If the file size is smaller than offset, the file is
 * extended to offset and len is ignored.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Opened file object.
 * \param[in]	offset	offset of file to punch at.
 * \param[in]	len	number of bytes to punch.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_punch(dfs_t *dfs, dfs_obj_t *obj, daos_off_t offset, daos_size_t len);

/**
 * Query number of link in dir object.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Opened directory object.
 * \param[out]	nlinks	Number of links returned.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_nlinks(dfs_t *dfs, dfs_obj_t *obj, uint32_t *nlinks);

/**
 * directory readdir.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Opened directory object.
 * \param[in,out]
 *		anchor	Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 * \param[in,out]
 *		nr	[in]: number of dirents allocated in \a dirs.
 *			[out]: number of returned dirents.
 * \param[in,out]
 *		dirs	[in] preallocated array of dirents.
 *			[out]: dirents returned with d_name filled only.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_readdir(dfs_t *dfs, dfs_obj_t *obj, daos_anchor_t *anchor,
	    uint32_t *nr, struct dirent *dirs);

/**
 * User callback defined for dfs_readdir_size.
 */
typedef int (*dfs_filler_cb_t)(dfs_t *dfs, dfs_obj_t *obj, const char name[],
			       void *_udata);

/**
 * Same as dfs_readdir, but this also adds a buffer size limitation when
 * enumerating. On every entry, it issues a user defined callback. If size
 * limitation is reached, function returns -DER_KEY2BIG.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Opened directory object.
 * \param[in,out]
 *		anchor	Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 * \param[in,out]
 *		nr	[in]: MAX number of entries to enumerate.
 *			[out]: Actual number of entries enumerated.
 * \param[in]	size	Max buffer size to be used internally before breaking.
 * \param[in]	op	Optional callback to be issued on every entry.
 * \param[in]	udata	Pointer to user data to be passed to \a op.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_iterate(dfs_t *dfs, dfs_obj_t *obj, daos_anchor_t *anchor,
	    uint32_t *nr, size_t size, dfs_filler_cb_t op, void *udata);

/**
 * Create a directory.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	parent	Opened parent directory object. If NULL, use root obj.
 * \param[in]	name	Link name of new dir.
 * \param[in]	mode	mkdir mode.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_mkdir(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode);

/**
 * Remove an object from parent directory. If object is a directory and is
 * non-empty; this will fail unless force option is true. If object is a
 * symlink, the symlink is removed.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	parent	Opened parent directory object. If NULL, use root obj.
 * \param[in]	name	Name of object to remove in parent dir.
 * \param[in]	force	If true, remove dir even if non-empty.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_remove(dfs_t *dfs, dfs_obj_t *parent, const char *name, bool force);

/**
 * Move an object possible between different dirs with a new link name.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	parent	Source parent directory object. If NULL, use root obj.
 * \param[in]	name	Link name of object.
 * \param[in]	new_parent
 *			Target parent directory object. If NULL, use root obj.
 * \param[in]	name	New link name of object.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_move(dfs_t *dfs, dfs_obj_t *parent, char *name, dfs_obj_t *new_parent,
	 char *new_name);

/**
 * Exchange an object possible between different dirs with a new link name
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	parent1	Parent directory object of name1. If NULL, use root obj.
 * \param[in]	name1	Link name of first object.
 * \param[in]	parent2	Parent directory object of name2. If NULL, use root obj.
 * \param[in]	name2	link name of second object.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_exchange(dfs_t *dfs, dfs_obj_t *parent1, char *name1,
	     dfs_obj_t *parent2, char *name2);

/**
 * Retrieve mode of an open object.
 *
 * \param[in]	obj	Open object to query.
 * \param[out]	mode	mode_t (permissions + type).
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_get_mode(dfs_obj_t *obj, mode_t *mode);

/**
 * Retrieve the DAOS open handle of a DFS file object. User should not close
 * this handle. This is used in cases like MPI-IO where 1 rank creates the file
 * with dfs, but wants to access the file with the array API directly rather
 * than the DFS API.
 *
 * \param[in]	obj	Open object.
 * \param[out]	oh	DAOS object open handle.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_get_file_oh(dfs_obj_t *obj, daos_handle_t *oh);

/**
 * Retrieve Symlink value of object if it's a symlink. If the buffer size passed
 * in is not large enough, we copy up to size of the buffer, and update the size
 * to actual value size.
 *
 * \param[in]	obj	Open object to query.
 * \param[in]	buf	user buffer to copy the symlink value in.
 * \param[in,out]
 *		size	[in]: Size of buffer pased in. [out]: Actual size of
 *			value.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_get_symlink_value(dfs_obj_t *obj, char *buf, daos_size_t *size);

/**
 * stat attributes of an entry. If object is a symlink, the link itself is
 * interogated. The following elements of the stat struct are populated
 * (the rest are set to 0):
 * mode_t    st_mode;
 * uid_t     st_uid;
 * gid_t     st_gid;
 * off_t     st_size;
 * blkcnt_t  st_blocks
 * struct timespec st_atim;
 * struct timespec st_mtim;
 * struct timespec st_ctim;
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	parent	Opened parent directory object. If NULL, use root obj.
 * \param[in]	name	Link name of the object. Can be NULL if parent is root,
 *			which means operation will be on root object.
 * \param[out]	stbuf	Stat struct with the members above filled.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_stat(dfs_t *dfs, dfs_obj_t *parent, const char *name,
	 struct stat *stbuf);

/**
 * Same as dfs_stat but works directly on an open object.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Open object (File, dir or syml) to stat.
 * \param[out]	stbuf	Stat struct with the members above filled.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_ostat(dfs_t *dfs, dfs_obj_t *obj, struct stat *stbuf);

/**
 * Check access permissions on an object. Similar to Linux access(2).
 * Symlinks are dereferenced.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	parent	Opened parent directory object. If NULL, use root obj.
 * \param[in]	name	Link name of the object. Can be NULL if parent is root,
 *			which means operation will be on root object.
 * \param[in]	mask	accessibility check(s) to be performed.
 *			It should be either the value F_OK, or a mask with
 *			bitwise OR of one or more of R_OK, W_OK, and X_OK.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_access(dfs_t *dfs, dfs_obj_t *parent, const char *name, int mask);

/**
 * Change permission access bits. Symlinks are dereferenced.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	parent	Opened parent directory object. If NULL, use root obj.
 * \param[in]	name	Link name of the object. Can be NULL if parent is root,
 *			which means operation will be on root object.
 * \param[in]	mode	New permission access modes. For now, we don't support
 *			the sticky bit, setuid, and setgid.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_chmod(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode);

/**
 * Sync to commit the latest epoch on the container. This applies to the entire
 * namespace and not to a particular file/directory.
 *
 * TODO: This should take a persistent snapshot at current timestamp.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_sync(dfs_t *dfs);

/**
 * Set extended attribute on an open object (File, dir, syml). If object is a
 * symlink, the value is set on the symlink itself.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Open object where xattr will be added.
 * \param[in]	name	Name of xattr to add.
 * \param[in]	value	Value of xattr.
 * \param[in]	size	Size in bytes of the value.
 * \param[in]	flags	Set flags. passing 0 does not check for xattr existence.
 *			XATTR_CREATE: create or fail if xattr exists.
 *			XATTR_REPLACE: replace or fail if xattr does not exist.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_setxattr(dfs_t *dfs, dfs_obj_t *obj, const char *name,
	     const void *value, daos_size_t size, int flags);

/**
 * Get extended attribute of an open object. If object is a symlink, the link
 * itself is interogated.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Open object where xattr is checked.
 * \param[in]	name	Name of xattr to get.
 * \param[out]	value	Buffer to place value of xattr.
 * \param[in,out]
 *		size	[in]: Size of buffer value. [out]: Actual size of xattr.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_getxattr(dfs_t *dfs, dfs_obj_t *obj, const char *name, void *value,
	     daos_size_t *size);

/**
 * Remove extended attribute of an open object. If object is a symlink, the link
 * itself is interogated.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Open object where xattr will be removed.
 * \param[in]	name	Name of xattr to remove.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_removexattr(dfs_t *dfs, dfs_obj_t *obj, const char *name);

/**
 * list extended attributes of an open object and place them all in a buffer
 * NULL terminated one after the other. If object is a symlink, the link itself
 * is interogated.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Open object where xattrs will be listed.
 * \param[in,out]
 *		list	[in]: Allocated buffer for all xattr names.
 *			[out]: Names placed after each other (null terminated).
 * \param[in,out]
 *		size    [in]: Size of list. [out]: Actual size of list.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_listxattr(dfs_t *dfs, dfs_obj_t *obj, char *list, daos_size_t *size);

/**
 * Mount a DFS namespace in a special container designated as the root
 * container. If the root container does not exist, this call creates it.
 *
 * \param[in]   poh     Pool connection handle
 * \param[out]  dfs     Pointer to the root DFS created.
 *
 * \return              0 on Success. Negative errno on Failure.
 */
int
dfs_mount_root_cont(daos_handle_t poh, dfs_t **dfs);

/**
 * Unmount the root DFS.
 *
 * \param[in]	dfs	Pointer to the root DFS file system.
 *
 * \return		0 on Success. Negative errno on Failure.
 */
int
dfs_umount_root_cont(dfs_t *dfs);

#if defined(__cplusplus)
}
#endif
#endif /* __DAOS_FS_H__ */
