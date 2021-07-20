/**
 * (C) Copyright 2019-2021 Intel Corporation.
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

/** as dfs_open() but takes a stbuf to be populated if O_CREATE is specified */
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
dfs_move_internal(dfs_t *dfs, dfs_obj_t *parent, char *name, dfs_obj_t *new_parent, char *new_name,
		  daos_obj_id_t *moid, daos_obj_id_t *oid);

/* Set the in-memory parent, but takes the parent, rather than another file object */
void
dfs_update_parentfd(dfs_obj_t *obj, dfs_obj_t *new_parent, const char *name);

/** update chunk size and oclass of obj with the ones from new_obj */
void
dfs_obj_copy_attr(dfs_obj_t *dst_obj, dfs_obj_t *src_obj);

#if defined(__cplusplus)
}
#endif
#endif /* __DFS_INTERNAL_H__ */
