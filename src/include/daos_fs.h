/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include <sys/stat.h>

/** Maximum Name length */
#define DFS_MAX_NAME		NAME_MAX
/** Maximum PATH length */
#define DFS_MAX_PATH		PATH_MAX
/** Maximum file size */
#define DFS_MAX_FSIZE		(~0ULL)

/** Maximum xattr name */
#define DFS_MAX_XATTR_NAME	255
/** Maximum xattr value */
#define DFS_MAX_XATTR_LEN	65536

/** File/Directory/Symlink object handle struct */
typedef struct dfs_obj dfs_obj_t;
/** DFS mount handle struct */
typedef struct dfs dfs_t;

/*
 * Consistency modes of the DFS container. A container created with balanced
 * mode, can only be accessed with balanced mode with dfs_mount. A container
 * created with relaxed mode, can be accessed with either mode in the future.
 *
 * Reserve bit 3 in the access flags for dfs_mount() - bits 1 and 2 are used
 * for read / write access (O_RDONLY, O_RDRW).
 */
/** DFS container balanced consistency mode. DFS operations using a DTX */
#define DFS_BALANCED	4
/** DFS container relaxed consistency mode. DFS operations do not use a DTX (default mode) */
#define DFS_RELAXED	0
/** read-only access */
#define DFS_RDONLY	O_RDONLY
/** read/write access */
#define DFS_RDWR	O_RDWR

/** struct holding attributes for a DFS container */
typedef struct {
	/** Optional user ID for DFS container. */
	uint64_t		da_id;
	/** Default Chunk size for all files in container */
	daos_size_t		da_chunk_size;
	/** Default Object Class for all objects in the container */
	daos_oclass_id_t	da_oclass_id;
	/** DAOS properties on the DFS container */
	daos_prop_t		*da_props;
	/**
	 * Consistency mode for the DFS container: DFS_RELAXED, DFS_BALANCED.
	 * If set to 0 or more generally not set to balanced explicitly, relaxed
	 * mode will be used. In the future, Balanced mode will be the default.
	 */
	uint32_t		da_mode;
} dfs_attr_t;

/** IO descriptor of ranges in a file to access */
typedef struct {
	/** Number of entries in dfs_rgs */
	daos_size_t		iod_nr;
	/** Array of ranges; each range defines a starting index and length. */
	daos_range_t	       *iod_rgs;
} dfs_iod_t;

/** DFS object information */
typedef struct {
	/** object class */
	daos_oclass_id_t	doi_oclass_id;
	/** chunk size */
	daos_size_t		doi_chunk_size;
} dfs_obj_info_t;

/**
 * Initialize the DAOS and DFS library. Typically this is called at the beginning of a user program
 * or in IO middleware initialization. This is required to be called if using the
 * dfs_connect/disconnect calls to setup the DFS cache for the pool and container handles. There is
 * no harm however in calling it whenever using any of the DFS API (mount/umount) and can be
 * equivalent to calling daos_init() instead.
 *
 * \return              0 on success, errno code on failure.
 */
int
dfs_init();

/**
 * Finalize the DAOS and DFS library. Typically this is called at the end of a user program or in IO
 * middleware finalization. This is required to be called if dfs_init() was called and closes all
 * cached open pool and container handles that resulted from dfs_connect() calls.
 *
 * \return              0 on success, errno code on failure.
 */
int
dfs_fini();

/**
 * Mount a DFS namespace over the specified pool and container. The container can be optionally
 * created if it doesn't exist, and O_CREAT is passed in flags. The handle must be released using
 * dfs_disconnect() and not dfs_umount(). Using the latter in this case will leak open handles for
 * the pool and container.
 *
 * This function works only if dfs_init() is called, otherwise would return EACCES. In addition to
 * setting up the pool and container handles for the user, this also utilizes an internal cache for
 * keeping the pool and container handles open internally with a ref count and closes those handles
 * on dfs_finalize().
 *
 * \param[in]	pool	Pool label.
 * \param[in]	sys	DAOS system name to use for the pool connect.
 *			Pass NULL to use the default system.
 * \param[in]	cont	Container label.
 * \param[in]	flags	Mount flags (O_RDONLY or O_RDWR, O_CREAT). O_CREAT attempts to create the
 *			DFS container if it doesn't exists.
 * \param[in]	attr	Optional set of properties and attributes to set on the container (if being
 *			created). Pass NULL to use default.
 * \param[out]	dfs	Pointer to the created DFS mount point.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_connect(const char *pool, const char *sys, const char *cont, int flags, dfs_attr_t *attr,
	    dfs_t **dfs);

/**
 * Umount the DFS namespace, and release the ref count on the container and pool handles. This
 * should be called on a dfs mount created with dfs_connect() and not dfs_mount().
 *
 * \param[in]	dfs	Pointer to the mounted file system from dfs_connect().
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_disconnect(dfs_t *dfs);

/**
 * Create a DFS container with the POSIX property layout set.  Optionally set attributes for hints
 * on the container.
 *
 * \param[in]	poh	Pool open handle.
 * \param[out]	uuid	Pointer to uuid_t to hold the implementation-generated container UUID.
 * \param[in]	attr	Optional set of properties and attributes to set on the container.
 *			Pass NULL if none.
 * \param[out]	coh	Optionally leave the container open and return its hdl.
 * \param[out]	dfs	Optionally mount DFS on the container and return the dfs handle.
 *
 * \return              0 on success, errno code on failure.
 */
int
dfs_cont_create(daos_handle_t poh, uuid_t *uuid, dfs_attr_t *attr, daos_handle_t *coh, dfs_t **dfs);

/**
 * Create a DFS container with label \a label. This is the same as dfs_container_create() with the
 * label property set in \a attr->da_props.
 *
 * \param[in]	poh	Pool open handle.
 * \param[in]	label	Required, label property of the new container.
 *			Supersedes any label specified in \a cont_prop.
 * \param[in]	attr	Optional set of properties and attributes to set on the container.
 *			Pass NULL if none.
 * \param[out]	uuid	Optional pointer to uuid_t to hold the implementation-generated container
 *			UUID.
 * \param[out]	coh	Optionally leave the container open and return its hdl.
 * \param[out]	dfs	Optionally mount DFS on the container and return the dfs handle.
 *
 * \return              0 on success, errno code on failure.
 */
int
dfs_cont_create_with_label(daos_handle_t poh, const char *label, dfs_attr_t *attr,
			   uuid_t *uuid, daos_handle_t *coh, dfs_t **dfs);

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
 * \return		0 on success, errno code on failure.
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
 * \return		0 on success, errno code on failure.
 */
int
dfs_umount(dfs_t *dfs);

/**
 * Retrieve the open pool handle on the DFS mount. This is refcounted internally and must be
 * released with dfs_pool_put().
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[out]	poh	Open pool handle.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_pool_get(dfs_t *dfs, daos_handle_t *poh);

/**
 * Release refcount of pool handle taken by dfs_pool_get().
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[out]	poh	Pool handle that was returned from dfs_pool_get().
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_pool_put(dfs_t *dfs, daos_handle_t poh);

/**
 * Retrieve the open cont handle on the DFS mount. This is refcounted internally and must be
 * released with dfs_cont_put().
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[out]	coh	Open cont handle.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_cont_get(dfs_t *dfs, daos_handle_t *coh);

/**
 * Release refcount of cont handle taken by dfs_cont_get().
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[out]	coh	Cont handle that was returned from dfs_cont_get().
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_cont_put(dfs_t *dfs, daos_handle_t coh);

/**
 * Query attributes of a DFS mount.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[out]	attr	Attributes on the DFS container.
 *
 * \return              0 on success, errno code on failure.
 */
int
dfs_query(dfs_t *dfs, dfs_attr_t *attr);

/**
 * Convert a local dfs mount to global representation data which can be
 * shared with peer processes.
 * If glob->iov_buf is set to NULL, the actual size of the global handle is
 * returned through glob->iov_buf_len.
 * This function does not involve any communication and does not block.
 *
 * \param[in]	dfs	valid dfs mount to be shared
 * \param[out]	glob	pointer to iov of the buffer to store mount information
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_local2global(dfs_t *dfs, d_iov_t *glob);

/**
 * Create a dfs mount from global representation data. This has to be closed
 * with dfs_umount().
 *
 * \param[in]	poh	Pool connection handle
 * \param[in]	coh	Container open handle.
 * \param[in]	flags	Mount flags (O_RDONLY or O_RDWR). If 0, inherit flags
 *			of serialized DFS handle.
 * \param[in]	glob	Global (shared) representation of a collective handle
 *			to be extracted
 * \param[out]	dfs	Returned dfs mount
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_global2local(daos_handle_t poh, daos_handle_t coh, int flags, d_iov_t glob,
		 dfs_t **dfs);

/**
 * Convert a local dfs mount including the pool and container handles to global representation data
 * which can be shared with peer processes.
 * If glob->iov_buf is set to NULL, the actual size of the global handle is returned through
 * glob->iov_buf_len.  This function does not involve any communication and does not block.
 *
 * \param[in]	dfs	valid dfs mount to be shared
 * \param[out]	glob	pointer to iov of the buffer to store mount information
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_local2global_all(dfs_t *dfs, d_iov_t *glob);

/**
 * Create a dfs mount from global representation data. This has to be closed with dfs_disconnect()
 * since the pool and container connections are established with it.
 *
 * \param[in]	flags	Mount flags (O_RDONLY or O_RDWR). If 0, inherit flags
 *			of serialized DFS handle.
 * \param[in]	glob	Global (shared) representation of a collective handle to be extracted.
 * \param[out]	dfs	Returned dfs mount
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_global2local_all(int flags, d_iov_t glob, dfs_t **dfs);

/**
 * Optionally set a prefix on the dfs mount where all paths passed to dfs_lookup
 * are trimmed off that prefix. This is helpful when using DFS API with a dfuse
 * mount and the user would like to reference files in the dfuse mount instead
 * of the absolute path from the root of the DFS container.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	prefix	absolute prefix to trim off path to dfs_lookup.
 *			Passing NULL unsets the prefix.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_set_prefix(dfs_t *dfs, const char *prefix);

/**
 * Convert from a dfs_obj_t to a daos_obj_id_t.
 *
 * \param[in]	obj	Object to convert
 * \param[out]	oid	Daos object ID.
 *
 * \return		0 on success, errno code on failure.
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
 * \param[out]	mode	Optional mode_t of object looked up.
 * \param[out]	stbuf	Optional stat struct of object looked up.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_lookup(dfs_t *dfs, const char *path, int flags, dfs_obj_t **obj,
	   mode_t *mode, struct stat *stbuf);

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
 * \param[out]	mode	Optional mode_t of object looked up.
 * \param[out]	stbuf	Optional stat struct of object looked up.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_lookup_rel(dfs_t *dfs, dfs_obj_t *parent, const char *name, int flags,
	       dfs_obj_t **obj, mode_t *mode, struct stat *stbuf);

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
 * \param[in]	flags	Access flags (handles: O_RDONLY, O_RDWR, O_EXCL,
 *			O_CREAT, O_TRUNC).
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
 * \return		0 on success, errno code on failure.
 */
int
dfs_dup(dfs_t *dfs, dfs_obj_t *obj, int flags, dfs_obj_t **new_obj);

/**
 * Convert a local DFS object to global representation data which can be
 * shared with peer processes.
 * If glob->iov_buf is set to NULL, the actual size of the global handle is
 * returned through glob->iov_buf_len.
 * This function does not involve any communication and does not block.
 *
 * \param[in]	dfs     Pointer to the mounted file system.
 * \param[in]	obj	DFS Object to serialize
 * \param[out]	glob	pointer to iov of the buffer to store obj information
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_obj_local2global(dfs_t *dfs, dfs_obj_t *obj, d_iov_t *glob);

/**
 * Create a dfs object from global representation data. This has to be closed
 * with dfs_release().
 *
 * \param[in]   dfs     Pointer to the mounted file system.
 * \param[in]	flags	Access flags (O_RDONLY/O_RDWR/O_WRONLY). If 0, inherit
 *			flags of serialized object handle.
 * \param[in]	glob	Global (shared) representation of a collective handle
 *			to be extracted
 * \param[out]	obj	Returned open object handle
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_obj_global2local(dfs_t *dfs, int flags, d_iov_t glob, dfs_obj_t **obj);

/**
 * Close/release open object.
 *
 * \param[in]	obj	Object to release.
 *
 * \return		0 on success, errno code on failure.
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
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_read(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t *sgl, daos_off_t off,
	 daos_size_t *read_size, daos_event_t *ev);

/**
 * Non-contiguous read interface to a DFS file.
 * Same as dfs_read with the ability to have a segmented file layout to read.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Opened file object.
 * \param[in]	iod	IO descriptor for list-io.
 * \param[in]	sgl	Scatter/Gather list for data buffer.
 * \param[out]	read_size
 *			How much data is actually read.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_readx(dfs_t *dfs, dfs_obj_t *obj, dfs_iod_t *iod, d_sg_list_t *sgl,
	  daos_size_t *read_size, daos_event_t *ev);

/**
 * Write data to the file object.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Opened file object.
 * \param[in]	sgl	Scatter/Gather list for data buffer.
 * \param[in]	off	Offset into the file to write to.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_write(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t *sgl, daos_off_t off,
	  daos_event_t *ev);

/**
 * Non-contiguous write interface to a DFS file.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Opened file object.
 * \param[in]	iod	IO descriptor of file view.
 * \param[in]	sgl	Scatter/Gather list for data buffer.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_writex(dfs_t *dfs, dfs_obj_t *obj, dfs_iod_t *iod, d_sg_list_t *sgl,
	   daos_event_t *ev);

/**
 * Query size of file data.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Opened file object.
 * \param[out]	size	Size of file.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_get_size(dfs_t *dfs, dfs_obj_t *obj, daos_size_t *size);

/**
 * Punch a hole in the file starting at offset to len. If len is set to
 * DFS_MAX_FSIZE, this will be equivalent to a truncate operation to shrink or
 * extend the file to \a offset bytes depending on the file size.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Opened file object.
 * \param[in]	offset	offset of file to punch at.
 * \param[in]	len	number of bytes to punch.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_punch(dfs_t *dfs, dfs_obj_t *obj, daos_off_t offset, daos_size_t len);

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
 * \return		0 on success, errno code on failure.
 */
int
dfs_readdir(dfs_t *dfs, dfs_obj_t *obj, daos_anchor_t *anchor,
	    uint32_t *nr, struct dirent *dirs);

/**
 * User callback defined for dfs_readdir_size.
 */
typedef int (*dfs_filler_cb_t)(dfs_t *dfs, dfs_obj_t *obj, const char name[],
			       void *arg);

/**
 * Same as dfs_readdir, but this also adds a buffer size limitation when
 * enumerating. On every entry, it issues a user defined callback. If size
 * limitation is reached, function returns E2BIG
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
 * \param[in]	arg	Pointer to user data to be passed to \a op.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_iterate(dfs_t *dfs, dfs_obj_t *obj, daos_anchor_t *anchor,
	    uint32_t *nr, size_t size, dfs_filler_cb_t op, void *arg);

/**
 * Provide a function for large directories to split an anchor to be able to
 * execute a parallel readdir or iterate. This routine suggests the optimal
 * number of anchors to use instead of just 1 and optionally returns all those
 * anchors. The user would allocate the array of anchors after querying the
 * number of anchors needed. Alternatively, user does not provide an array and
 * can call dfs_obj_anchor_set() for every anchor to set.
 *
 * The user could suggest how many anchors to split the iteration over. This
 * feature is not supported yet.
 *
 * \param[in]	obj	Dir object to split anchor for.
 * \param[in,out]
 *		nr	[in]: Number of anchors requested and allocated in
 *			\a anchors. Pass 0 for DAOS to recommend split num.
 *			[out]: Number of anchors recommended if 0 is passed in.
 * \param[in]	anchors	Optional array of anchors that are split.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_obj_anchor_split(dfs_obj_t *obj, uint32_t *nr, daos_anchor_t *anchors);

/**
 * Set an anchor with an index based on split done with dfs_obj_anchor_split.
 * The anchor passed will be re-intialized and set to start and finish iteration
 * based on the specified index.
 *
 * \param[in]   obj     Dir object to split anchor for.
 * \param[in]	index	Index of set this anchor for iteration.
 * \param[in,out]
 *		anchor	Hash anchor to set.
 *
 * \return              0 on success, errno code on failure.
 */
int
dfs_obj_anchor_set(dfs_obj_t *obj, uint32_t index, daos_anchor_t *anchor);

/**
 * Create a directory.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	parent	Opened parent directory object. If NULL, use root obj.
 * \param[in]	name	Link name of new dir.
 * \param[in]	mode	mkdir mode.
 * \param[in]	cid	DAOS object class id (pass 0 for default MAX_RW).
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_mkdir(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode,
	  daos_oclass_id_t cid);

/**
 * Remove an object from parent directory. If object is a directory and is
 * non-empty; this will fail unless force option is true. If object is a
 * symlink, the symlink is removed.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	parent	Opened parent directory object. If NULL, use root obj.
 * \param[in]	name	Name of object to remove in parent dir.
 * \param[in]	force	If true, remove dir even if non-empty.
 * \param[in]	oid	Optionally return the DAOS Object ID of the removed obj.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_remove(dfs_t *dfs, dfs_obj_t *parent, const char *name, bool force,
	   daos_obj_id_t *oid);

/**
 * Move/rename an object.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	parent	Source parent directory object. If NULL, use root obj.
 * \param[in]	name	Link name of object.
 * \param[in]	new_parent
 *			Target parent directory object. If NULL, use root obj.
 * \param[in]	new_name
 *			New link name of object.
 * \param[out]	oid	Optional: return the intenal object ID of the removed obj
 *			if the move clobbered it.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_move(dfs_t *dfs, dfs_obj_t *parent, const char *name, dfs_obj_t *new_parent,
	 const char *new_name, daos_obj_id_t *oid);

/**
 * Exchange two objects.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	parent1	Parent directory object of name1. If NULL, use root obj.
 * \param[in]	name1	Link name of first object.
 * \param[in]	parent2	Parent directory object of name2. If NULL, use root obj.
 * \param[in]	name2	link name of second object.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_exchange(dfs_t *dfs, dfs_obj_t *parent1, const char *name1, dfs_obj_t *parent2,
	     const char *name2);

/**
 * Retrieve mode of an open object.
 *
 * \param[in]	obj	Open object to query.
 * \param[out]	mode	mode_t (permissions + type).
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_get_mode(dfs_obj_t *obj, mode_t *mode);

/**
 * Retrieve some attributes of DFS object. Those include the object class and
 * the chunk size.
 *
 * \param[in]   dfs     Pointer to the mounted file system.
 * \param[in]   obj	Open object handle to query.
 * \param[out]  info	info object container object class, chunks size, etc.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_obj_get_info(dfs_t *dfs, dfs_obj_t *obj, dfs_obj_info_t *info);

/**
 * Set the object class on a directory for new files or sub-dirs that are
 * created in that dir.  This does not change the chunk size for existing files
 * or dirs in that directory, nor it does change the object class of the
 * directory itself. Note that this is only supported on directories and will
 * fail if called on non-directory objects.
 *
 * \param[in]   dfs     Pointer to the mounted file system.
 * \param[in]   obj	Open object handle to access.
 * \param[in]	flags	Flags for setting oclass (currently ignored)
 * \param[in]   cid	object class.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_obj_set_oclass(dfs_t *dfs, dfs_obj_t *obj, int flags, daos_oclass_id_t cid);

/**
 * Set the chunk size on a directory for new files or sub-dirs that are created
 * in that dir.  This does not change the chunk size for existing files or dirs
 * in that directory. Note that this is only supported on directories and will
 * fail if called on non-directory objects.
 *
 * \param[in]   dfs     Pointer to the mounted file system.
 * \param[in]   obj	Open object handle to access.
 * \param[in]	flags	Flags for setting chunk size (currently ignored)
 * \param[in]   csize	Chunk size to set object to.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_obj_set_chunk_size(dfs_t *dfs, dfs_obj_t *obj, int flags,
		       daos_size_t csize);

/**
 * Retrieve the DAOS open handle of a DFS file object. User should not close
 * this handle. This is used in cases like MPI-IO where 1 rank creates the file
 * with dfs, but wants to access the file with the array API directly rather
 * than the DFS API.
 *
 * \param[in]	obj	Open object.
 * \param[out]	oh	DAOS object open handle.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_get_file_oh(dfs_obj_t *obj, daos_handle_t *oh);

/**
 * Retrieve the chunk size of a DFS file object.
 *
 * \param[in]	obj	Open object.
 * \param[out]	chunk_size
 *			Chunk size of array object.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_get_chunk_size(dfs_obj_t *obj, daos_size_t *chunk_size);

/**
 * Retrieve Symlink value of object if it's a symlink. If the buffer size passed
 * in is not large enough, we copy up to size of the buffer, and update the size
 * to actual value size. The size returned includes the null terminator.
 *
 * \param[in]	obj	Open object to query.
 * \param[in]	buf	user buffer to copy the symlink value in.
 * \param[in,out]
 *		size	[in]: Size of buffer passed in. [out]: Actual size of
 *			value.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_get_symlink_value(dfs_obj_t *obj, char *buf, daos_size_t *size);

/**
 * A DFS object open handle has links to its parent (oid) and the entry name of
 * that object in that parent. In some cases a user would want to update the oh
 * of an object in case of a rename. This API would allow modifying an existing
 * open handle of an object to change it's parent and it's entry name. Note this
 * is a local operation and doesn't change anything on the storage.
 *
 * \param[in]	obj	Open object handle to update.
 * \param[in]	src_obj
 *			Open object handle of the object whose parent will be
 *			used as the new parent of \a obj.
 * \param[in]	name	Optional new name of entry in parent. Pass NULL to leave
 *			the entry name unchanged.
 *
 * \return		0 on Success. errno code on Failure.
 */
int
dfs_update_parent(dfs_obj_t *obj, dfs_obj_t *src_obj, const char *name);

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
 * \return		0 on success, errno code on failure.
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
 * \return		0 on success, errno code on failure.
 */
int
dfs_ostat(dfs_t *dfs, dfs_obj_t *obj, struct stat *stbuf);

/** Option to set the mode_t on an entry */
#define DFS_SET_ATTR_MODE	(1 << 0)
/** Option to set the access time on an entry */
#define DFS_SET_ATTR_ATIME	(1 << 1)
/** Option to set the modify time on an entry */
#define DFS_SET_ATTR_MTIME	(1 << 2)
/** Option to set size of a file */
#define DFS_SET_ATTR_SIZE	(1 << 3)
/** Option to set uid of object */
#define DFS_SET_ATTR_UID	(1 << 4)
/** Option to set gid of object */
#define DFS_SET_ATTR_GID	(1 << 5)

/**
 * set stat attributes for a file and fetch new values.  If the object is a
 * symlink the link itself is modified.  See dfs_stat() for which entries
 * are filled.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Open object (File, dir or syml) to modify.
 * \param[in,out]
 *		stbuf	[in]: Stat struct with the members set.
 *			[out]: Stat struct with all valid members filled.
 * \param[in]	flags	Bitmask of flags to set
 *
 * \return		0 on Success. errno code on Failure.
 */
int
dfs_osetattr(dfs_t *dfs, dfs_obj_t *obj, struct stat *stbuf, int flags);

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
 * \return		0 on success, errno code on failure.
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
 * \return		0 on success, errno code on failure.
 */
int
dfs_chmod(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode);

/**
 * Change owner and group. Since uid and gid are not enforced
 * at the DFS level, we do not also enforce the process privileges to be able to change the uid and
 * gid. Any process with write access to the DFS container can make changes to the uid and gid using
 * this function.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	parent	Opened parent directory object. If NULL, use root obj.
 * \param[in]	name	Link name of the object. Can be NULL if parent is root,
 *			which means operation will be on root object.
 * \param[in]	uid	change owner of file (-1 to leave unchanged).
 * \param[in]	gid	change group of file (-1 to leave unchanged).
 * \param[in]	flags	if 0, symlinks are dereferenced. Pass O_NOFOLLOW to not dereference.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_chown(dfs_t *dfs, dfs_obj_t *parent, const char *name, uid_t uid, gid_t gid, int flags);

/**
 * Sync to commit the latest epoch on the container. This applies to the entire
 * namespace and not to a particular file/directory.
 *
 * TODO: This should take a persistent snapshot at current timestamp.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 *
 * \return		0 on success, errno code on failure.
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
 * \return		0 on success, errno code on failure.
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
 * \return		0 on success, errno code on failure.
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
 * \return		0 on success, errno code on failure.
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
 * \return		0 on success, errno code on failure.
 */
int
dfs_listxattr(dfs_t *dfs, dfs_obj_t *obj, char *list, daos_size_t *size);

#if defined(__cplusplus)
}
#endif /* __cplusplus */
#endif /* __DAOS_FS_H__ */
