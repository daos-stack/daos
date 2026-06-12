/**
 * (C) Copyright 2019-2023 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This is an extension of the DAOS File System API that is used internally in the DAOS library,
 * mainly in dfuse and the daos fs handler.
 */
#ifndef __DFS_LIB_INT_H__
#define __DFS_LIB_INT_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <daos.h>
#include <daos_fs.h>

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
dfs_get_sb_layout(daos_key_t *dkey, daos_iod_t *iods[], int *akey_count, int *dfs_entry_key_size,
		  int *dfs_entry_size);

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
dfs_open_stat(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode, int flags,
	      daos_oclass_id_t cid, daos_size_t chunk_size, const char *value, dfs_obj_t **obj,
	      struct stat *stbuf);

int
dfs_lookupx(dfs_t *dfs, dfs_obj_t *parent, const char *name, int flags, dfs_obj_t **obj,
	    mode_t *mode, struct stat *stbuf, int xnr, char *xnames[], void *xvals[],
	    daos_size_t *xsizes);

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

/**
 * Query the max epoch of an opened DFS object without exposing dfs_obj_t internals to callers.
 */
int
dfs_obj_query_max_epoch(dfs_obj_t *obj, daos_epoch_t *epoch);

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

/** Fixed-size inode metadata returned by batched internal readdir helpers. */
struct dfs_readdir_attrs {
	mode_t        dra_mode;
	daos_obj_id_t dra_oid;
};

/**
 * Lightweight readdir helper: return the mode and object ID of an entry without opening the
 * underlying object. Unlike dfs_lookup_rel(), this does NOT issue an array/object open RPC per
 * entry, so it is meant for callers (e.g. plain readdir) that only need the entry type and inode
 * number and do not need an open object handle or the file size. The backing object is therefore
 * not validated.
 *
 * \param[in]	dfs	Pointer to the mounted file system.
 * \param[in]	parent	Opened parent directory object. If NULL, use root obj.
 * \param[in]	name	Link name of the entry to look up.
 * \param[out]	mode	Mode (permission and type bits) of the entry.
 * \param[out]	oid	Object ID of the entry.
 *
 * \return		0 on success, errno code on failure.
 */
int
dfs_lookup_rel_entry(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t *mode,
		     daos_obj_id_t *oid);

/**
 * Internal readdir helper that batches name + mode + oid fetches using the pipeline enumeration
 * path without opening each object.
 */
int
dfs_readdirx(dfs_t *dfs, dfs_obj_t *obj, daos_anchor_t *anchor, uint32_t *nr, struct dirent *dirs,
	     struct dfs_readdir_attrs *attrs, uint64_t *nr_scanned);

#if defined(__cplusplus)
}
#endif
#endif /* __DFS_LIB_INT_H__ */
