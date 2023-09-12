/**
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This is an extension of the DAOS File System API
 *
 * src/client/dfs/dfs_internal.h
 */
#ifndef __DFS_INTERNAL_H__
#define __DFS_INTERNAL_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <sys/stat.h>
#include <daos.h>
#include <daos_fs.h>

/** enum for hash entry type */
enum {
	DFS_H_POOL,
	DFS_H_CONT,
};

/** hash entry for open pool/container handles */
struct dfs_mnt_hdls {
	d_list_t	entry;
	char		value[DAOS_PROP_LABEL_MAX_LEN * 2 + 1];
	daos_handle_t	handle;
	int		ref;
	int		type;
};

struct dfs_mnt_hdls *
dfs_hdl_lookup(const char *str, int type, const char *pool);
void
dfs_hdl_release(struct dfs_mnt_hdls *hdl);
int
dfs_hdl_insert(const char *str, int type, const char *pool, daos_handle_t *oh,
	       struct dfs_mnt_hdls **_hdl);
int
dfs_hdl_cont_destroy(const char *pool, const char *cont, bool force);
bool
dfs_is_init();

/*
 * Get the DFS superblock D-Key and A-Keys
 *
 * \param[out] dkey DFS superblock D-Key
 * \param[out] iods DFS superblock A-keys
 * \param[out] akey_count number of superblock A-keys
 * \param[out] dfs_entry_key_size key size of the inode entry
 * \param[out] dfs_entry_size size of the dfs entry
 *
 * \return              0 on success, errno code on failure.
 */
int
dfs_get_sb_layout(daos_key_t *dkey, daos_iod_t *iods[], int *akey_count,
		int *dfs_entry_key_size, int *dfs_entry_size);

/*
 * Releases the memory allocated by the dfs_get_sb_layout() function.
 *
 * \param[in] iods DFS superblock A-keys
*/
void
dfs_free_sb_layout(daos_iod_t *iods[]);

/** as dfs_open() but takes a stbuf to be populated if O_CREATE is specified
 *
 * If O_CREATE is set then entry->uid and entry->gid are popouated with the desired user,
 * otherwise read them from the calling process.
 */
int
dfs_open_stat(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode,
	      int flags, daos_oclass_id_t cid, daos_size_t chunk_size,
	      const char *value, dfs_obj_t **obj, struct stat *stbuf);

int
dfs_lookupx(dfs_t *dfs, dfs_obj_t *parent, const char *name, int flags,
	    dfs_obj_t **obj, mode_t *mode, struct stat *stbuf, int xnr,
	    char *xnames[], void *xvals[], daos_size_t *xsizes);

/* moid is moved oid, oid is clobbered file.
 * This isn't yet fully compatible with dfuse because we also want to pass in a flag for if the
 * destination exists.
 */
int
dfs_move_internal(dfs_t *dfs, unsigned int flags, dfs_obj_t *parent, const char *name,
		  dfs_obj_t *new_parent, const char *new_name, daos_obj_id_t *moid,
		  daos_obj_id_t *oid);

/* Set the in-memory parent, but takes the parent, rather than another file object */
void
dfs_update_parentfd(dfs_obj_t *obj, dfs_obj_t *new_parent, const char *name);

/** update chunk size and oclass of obj with the ones from new_obj */
void
dfs_obj_copy_attr(dfs_obj_t *dst_obj, dfs_obj_t *src_obj);

/*
 * Internal routine for the daos fs tool to update an existing chunk size of a file. Note that this
 * is just meant to be used for updating the chunk size on leaked oids for files that were relinked
 * in lost+found.
 */
int
dfs_file_update_chunk_size(dfs_t *dfs, dfs_obj_t *obj, daos_size_t csize);

/** Internal routine for the daos fs tool to fix a corrupted entry type in the mode bits */
int
dfs_obj_fix_type(dfs_t *dfs, dfs_obj_t *parent, const char *name);

/*
 * Internal routine to recreate a POSIX container if it was ever corrupted as part of a catastrophic
 * recovery event.
 */
int
dfs_recreate_sb(daos_handle_t coh, dfs_attr_t *attr);

/*
 * Internal routine to relink the root object into the SB in case the SB entry was removed as part
 * of a catastrophic recovery event.
 */
int
dfs_relink_root(daos_handle_t coh);

/** Internal routine for async ostat.*/
int
dfs_ostatx(dfs_t *dfs, dfs_obj_t *obj, struct stat *stbuf, daos_event_t *ev);

/** Internal pipeline readdir functionality */

/** DFS pipeline object */
typedef struct dfs_pipeline dfs_pipeline_t;

enum {
	DFS_FILTER_NAME		= (1 << 1),
	DFS_FILTER_NEWER	= (1 << 2),
	DFS_FILTER_INCLUDE_DIRS	= (1 << 3),
};

/** Predicate conditions for filter */
typedef struct {
	char	dp_name[DFS_MAX_NAME]; /** name condition for entry - regex */
	time_t	dp_newer; /** timestamp for newer condition */
	size_t	dp_size; /** size of files - not supported for now */
} dfs_predicate_t;

/**
 * Same as dfs_get_size() but using the OID of the file instead of the open handle. Note that the
 * chunk_size of the file is also required to be passed if the file was created with a different
 * chunk size than the default (passing other than 0 to dfs_open). Otherwise, 0 should be passed to
 * chunk size.
 *
 * \param[in]	dfs		Pointer to the mounted file system.
 * \param[in]	oid		Object ID of the file.
 * \param[in]	chunk_size	Chunk size of the file (pass 0 if it was created with default).
 * \param[out]	size		Returned size of the file.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_get_size_by_oid(dfs_t *dfs, daos_obj_id_t oid, daos_size_t chunk_size, daos_size_t *size);

/**
 * Create a pipeline object to be used during readdir with filter. Should be destroyed with
 * dfs_pipeline_destroy().
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	pred	Predicate condition values (name/regex, newer timestamp, etc.).
 * \param[in]	flags	Pipeline flags (conditions to apply).
 * \param[out]	dpipe	Pipeline object created.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_pipeline_create(dfs_t *dfs, dfs_predicate_t pred, uint64_t flags, dfs_pipeline_t **dpipe);

/**
 * Destroy pipeline object.
 *
 * \param[in]	dpipe	Pipeline object.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_pipeline_destroy(dfs_pipeline_t *dpipe);

/**
 * Same as dfs_readdir() but this additionally applies a filter created with dfs_pipeline_create()
 * on the entries that are enumerated. This function also optionally returns the object ID of each
 * dirent if requested through a pre-allocated OID input array.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	obj	Opened directory object.
 * \param[in]	dpipe	DFS pipeline filter.
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
 * \param[in,out]
 *		oids	[in] Optional preallocated array of object IDs.
 *			[out]: Object ID associated with each dirent that was read.
 * \param[in,out]
 *		csizes	[in] Optional preallocated array of sizes.
 *			[out]: chunk size associated with each dirent that was read.
 * \param[out]		Total number of entries scanned by readdir before returning.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_readdir_with_filter(dfs_t *dfs, dfs_obj_t *obj, dfs_pipeline_t *dpipe, daos_anchor_t *anchor,
			uint32_t *nr, struct dirent *dirs, daos_obj_id_t *oids, daos_size_t *csizes,
			uint64_t *nr_scanned);

#if defined(__cplusplus)
}
#endif
#endif /* __DFS_INTERNAL_H__ */
