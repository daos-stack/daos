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

/**
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

/**
 * Releases the memory allocated by the dfs_get_sb_layout() function.
 *
 * \param[in] iods DFS superblock A-keys
*/
void
dfs_free_sb_layout(daos_iod_t *iods[]);

/* as dfs_open() but takes a stbuf to be populated if O_CREATE is specified */
int
dfs_open_stat(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode,
	      int flags, daos_oclass_id_t cid, daos_size_t chunk_size,
	      const char *value, dfs_obj_t **obj, struct stat *stbuf);

#if defined(__cplusplus)
}
#endif
#endif /* __DFS_INTERNAL_H__ */
