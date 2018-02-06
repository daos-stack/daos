/**
 * (C) Copyright 2018 Intel Corporation.
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

/*
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

#define DFS_MAX_PATH 128
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
 * \param poh	[IN]	Pool connection handle
 * \param coh	[IN]	Container open handle.
 * \param dfs	[OUT]	Pointer to the file system object created.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_mount(daos_handle_t poh, daos_handle_t coh, dfs_t **dfs);

/**
 * Unmount a DAOS file system. This closes open handles to the root object and
 * commits the latest epoch. The internal dfs struct is freed, so further access
 * to that dfs will be invalid.
 *
 * \param dfs   [IN]	Pointer to the mounted file system.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_umount(dfs_t *dfs);

/**
 * Lookup a path in the DFS and return the associated open object and mode.
 * The object must be released with dfs_release().
 *
 * \param dfs   [IN]	Pointer to the mounted file system.
 * \param path	[IN]	Path to lookup.
 * \param flags	[IN]	access flags to open with (O_RDONLY or O_RDWR).
 * \param obj	[OUT]	pointer to the object looked up.
 * \params mode	[OUT]	mode_t (permissions + type).
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_lookup(dfs_t *dfs, const char *path, int flags, dfs_obj_t **obj,
	   mode_t *mode);

/**
 * Create/Open a directory, file, or Symlink.
 * The object must be released with dfs_release().
 *
 * \param dfs   [IN]	Pointer to the mounted file system.
 * \param parent[IN]	Opened parent directory object.
 * \param name	[IN]	Link name of the object to create/open.
 * \param mode	[IN]	mode_t (permissions + type).
 * \param flags	[IN]	access flags (O_RDONLY, O_RDWR, O_EXCL).
 * \param value	[IN]	Symlink value (NULL if not syml).
 * \param obj	[OUT]	pointer to object opened.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_open(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode,
	 int flags, const char *value, dfs_obj_t **obj);

/*
 * Close/release open object.
 *
 * \param obj	[IN]	Object to release.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_release(dfs_obj_t *obj);

/**
 * Read data from the file object, and return actual data read.
 *
 * \param dfs		[IN]	Pointer to the mounted file system.
 * \param obj		[IN]	Opened file object.
 * \param sgl		[IN]	Scatter/Gather list for data buffer.
 * \param off		[IN]	Offset into the file to read from.
 * \param read_size	[OUT]	How much data is actually read.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_read(dfs_t *dfs, dfs_obj_t *obj, daos_sg_list_t sgl, daos_off_t off,
	 daos_size_t *read_size);

/**
 * Write data to the file object.
 *
 * \param dfs		[IN]	Pointer to the mounted file system.
 * \param obj		[IN]	Opened file object.
 * \param sgl		[IN]	Scatter/Gather list for data buffer.
 * \param off		[IN]	Offset into the file to write to.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_write(dfs_t *dfs, dfs_obj_t *obj, daos_sg_list_t sgl, daos_off_t off);

/**
 * Query size of file data.
 *
 * \param dfs	[IN]	Pointer to the mounted file system.
 * \param obj	[IN]	Opened file object.
 * \param size	[OUT]	Size of file.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_get_size(dfs_t *dfs, dfs_obj_t *obj, daos_size_t *size);

/**
 * Punch a hole in the file starting at offset to len. If len is set to
 * DFS_MAX_FSIZE, this will be a truncate operation to punch all bytes in the
 * file above offset. If the file size is smaller than offset, the file is
 * extended to offset and len is ignored.
 *
 * \param dfs	[IN]	Pointer to the mounted file system.
 * \param obj	[IN]	Opened file object.
 * \param offset[IN]	offset of file to punch at.
 * \param len	[IN]	number of bytes to punch.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_punch(dfs_t *dfs, dfs_obj_t *obj, daos_off_t offset, daos_size_t len);

/**
 * Query number of link in dir object.
 *
 * \param dfs	[IN]	Pointer to the mounted file system.
 * \param obj	[IN]	Opened directory object.
 * \param nlinks[OUT]	Number of links returned.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_nlinks(dfs_t *dfs, dfs_obj_t *obj, uint32_t *nlinks);

/**
 * directory readdir.
 *
 * \param dfs	[IN]	Pointer to the mounted file system.
 * \param obj	[IN]	Opened directory object.
 * \param anchor[IN/OUT]
 *			Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 * \param nr	[IN]	number of dirents allocated in \a dirs
 *		[OUT]	number of returned dirents.
 * \param dirs	[IN]	preallocated array of dirents.
 *		[OUT]	dirents returned with d_name filled only.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_readdir(dfs_t *dfs, dfs_obj_t *obj, daos_hash_out_t *anchor,
	    uint32_t *nr, struct dirent *dirs);

/**
 * Create a directory.
 *
 * \param dfs	[IN]	Pointer to the mounted file system.
 * \param parent[IN]	Opened parent directory object.
 * \param name	[IN]	Link name of new dir.
 * \param mode	[IN]	mkdir mode.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_mkdir(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode);

/**
 * Remove an object from parent directory. If object is a directory and is
 * non-empty; this will fail unless force option is true.
 *
 * \param dfs	[IN]	Pointer to the mounted file system.
 * \param parent[IN]	Opened parent directory object.
 * \param name	[IN]	Name of object to remove in parent dir.
 * \param force [IN]	If true, remove dir even if non-empty.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_remove(dfs_t *dfs, dfs_obj_t *parent, const char *name, bool force);

/**
 * Move an object possible between different dirs with a new link name
 *
 * \param dfs		[IN]	Pointer to the mounted file system.
 * \param parent	[IN]	Opened source parent directory object.
 * \param name		[IN]	Link name of object.
 * \param new_parent	[IN]	Opened target parent directory object.
 * \param name		[IN]	New link name of object.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_move(dfs_t *dfs, dfs_obj_t *parent, char *name, dfs_obj_t *new_parent,
	 char *new_name);

/**
 * Exchange an object possible between different dirs with a new link name
 *
 * \param dfs		[IN]	Pointer to the mounted file system.
 * \param parent1	[IN]	Opened parent directory object of name1.
 * \param name1		[IN]	Link name of first object.
 * \param parent2	[IN]	Opened parent directory object of name2.
 * \param name2		[IN]	link name of second object.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_exchange(dfs_t *dfs, dfs_obj_t *parent1, char *name1,
	     dfs_obj_t *parent2, char *name2);

/**
 * Retrieve mode of an open object.
 *
 * \param obj	[IN]	Open object to query.
 * \param mode	[OUT]	Returned mode_t.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_get_obj_type(dfs_obj_t *obj, mode_t *mode);

/**
 * Retrieve Symlink value of object if it's a symlink. If the buffer size passed
 * in is not large enough, we copy up to size of the buffer, and update the size
 * to actual value size.
 *
 * \param obj	[IN]	Open object to query.
 * \param buf	[IN]	user buffer to copy the symlink value in.
 * \param size	[IN/OUT]size of buffer pased in, return actual size of value.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_get_symlink_value(dfs_obj_t *obj, char *buf, daos_size_t *size);

/**
 * stat attributes of an entry. The following elements of the stat struct are
 * populated (the rest are set to 0):
 * mode_t    st_mode;
 * uid_t     st_uid;
 * gid_t     st_gid;
 * off_t     st_size;
 * struct timespec st_atim;
 * struct timespec st_mtim;
 * struct timespec st_ctim;
 *
 * \param dfs   [IN]	Pointer to the mounted file system.
 * \param parent[IN]	Opened parent directory object.
 * \param name	[IN]	Link name of the object to stat.
 * \param stbuf [IN/OUT] stat struct to fill the members of.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_stat(dfs_t *dfs, dfs_obj_t *parent, const char *name,
	 struct stat *stbuf);

/**
 * Same as dfs_stat but works directly on an open object.
 *
 * \param dfs   [IN]	Pointer to the mounted file system.
 * \param obj	[IN]	Open object (File, dir or syml) to stat.
 * \param stbuf [IN/OUT] stat struct to fill the members of.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_ostat(dfs_t *dfs, dfs_obj_t *obj, struct stat *stbuf);

/**
 * Sync to commit the latest epoch on the container. This applies to the entire
 * namespace and not to a particular file/directory.
 *
 * \param dfs   [IN]    Pointer to the mounted file system.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_sync(dfs_t *dfs);

/**
 * Set extended attribute on an open object (File, dir, syml).
 *
 * \param dfs   [IN]    Pointer to the mounted file system.
 * \param obj   [IN]    Open object where xattr will be added.
 * \param name	[IN]	Name of xattr to add.
 * \param value	[IN]	Value of xattr.
 * \param size	[IN]	Size in bytes of the value.
 * \param flags	[IN]	Set flags:
 *			XATTR_CREATE - create or fail if xattr exists.
 *			XATTR_REPLACE - replace or fail if xattr does not exist.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_setxattr(dfs_t *dfs, dfs_obj_t *obj, const char *name,
	     const void *value, daos_size_t size, int flags);

/**
 * Get extended attribute of an open object.
 *
 * \param dfs   [IN]    Pointer to the mounted file system.
 * \param obj   [IN]    Open object where xattr is checked.
 * \param name	[IN]	Name of xattr to get.
 * \param value	[OUT]	Buffer to place value of xattr.
 * \param size	[IN]	Size of buffer value,
 *		[OUT]	Actual size of xattr.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_getxattr(dfs_t *dfs, dfs_obj_t *obj, const char *name, void *value,
	     daos_size_t *size);

/**
 * Remove extended attribute of an open object.
 *
 * \param dfs   [IN]    Pointer to the mounted file system.
 * \param obj   [IN]    Open object where xattr will be removed.
 * \param name	[IN]	Name of xattr to remove.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_removexattr(dfs_t *dfs, dfs_obj_t *obj, const char *name);

/**
 * list extended attributes of an open object and place them all in a buffer
 * NULL terminated one after the other.
 *
 * \param dfs   [IN]    Pointer to the mounted file system.
 * \param obj   [IN]    Open object where xattrs will be listed.
 * \param list	[IN]	Allocated buffer for all xattr names.
 *		[OUT]	Xattr names placed after each other (null terminated).
 * \param size	[IN]	Size of buffer list,
 *		[OUT]	Actual size of list.
 *
 * \return		0 on Success. Negative on Failure.
 */
int
dfs_listxattr(dfs_t *dfs, dfs_obj_t *obj, char *list, daos_size_t *size);

#if defined(__cplusplus)
}
#endif
#endif /* __DAOS_FS_H__ */
