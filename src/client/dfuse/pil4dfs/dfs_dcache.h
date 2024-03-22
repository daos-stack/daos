/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DFS_DCACHE_H__
#define __DFS_DCACHE_H__

#ifndef D_LOGFAC
#define D_LOGFAC DD_FAC(il)
#endif

#include <daos_fs.h>

/** DFS directory cache */
typedef struct dfs_dcache dfs_dcache_t;
/** Entry of a DFS directory cache */
typedef struct dcache_rec dcache_rec_t;

/**
 * Create a dfs new dir-cache.
 *
 * \param[out] dcache	The newly create dir-cache
 * \param[in] dfs	The DAOS File System to cache
 *
 * \return		0 on success, negative value on error
 */
int
dcache_create(dfs_dcache_t **dcache, dfs_t *dfs);

/**
 * Destroy a dfs dir-cache.
 *
 * \param[in] dcache	The dir-cache to be destroyed
 *
 * \return		0 on success, negative value on error
 */
int
dcache_destroy(dfs_dcache_t *dcache);

/*
 * Look up a path in hash table to get the dfs_obj for a dir. If not found in hash table, call to
 * dfs_lookup() to open the object on DAOS. If the object is corresponding is a dir, insert the
 * object into hash table. Since many DFS APIs need parent dir DFS object as parameter, we use a
 * hash table to cache parent objects for efficiency.
 *
 * path_len should be length of the string path, not including null terminating ('\0').
 */
int
dcache_find_insert(dcache_rec_t **rec, dfs_dcache_t *dcache, char *path, size_t path_len);

/**
 * Convert a dir cache record to a dfs object
 *
 * \param[in] rec	The dir-cache record to convert
 *
 * \return		The converted dfs object
 */
dfs_obj_t *
drec2obj(dcache_rec_t *rec);

/**
 * Increase the reference counter of a given dir-cache record.
 *
 * \param[in] dcache	The dir-cache holding the dir-cache record
 * \param[in] rec	The dir-cache record being referenced
 */
void
drec_incref(dfs_dcache_t *dcache, dcache_rec_t *rec);

/**
 * Decrease the reference counter of a given dir-cache record and eventually delete it when it is
 * equal to zero.
 *
 * \param[in] dcache	The dir-cache holding the dir-cache record
 * \param[in] rec	The dir-cache record being referenced
 */
void
drec_decref(dfs_dcache_t *dcache, dcache_rec_t *rec);

/**
 * Decrease two times the reference counter of a given dir-cache record and remove it from its
 * dir-cache.
 *
 * \param[in] dcache	The dir-cache holding the dir-cache record
 * \param[in] rec	The dir-cache record being referenced
 */
void
drec_del_at(dfs_dcache_t *dcache, dcache_rec_t *rec);

/**
 * Decrease the reference counter of a given dir-cache record and delete it.
 *
 * \param[in] dcache	The dir-cache holding the dir-cache record
 * \param[in] path	File path of the directory to delete
 * \param[in] parent	Parent record of the one to remove
 *
 * \return		0 on success, negative value on error
 */
int
drec_del(dfs_dcache_t *dcache, char *path, dcache_rec_t *parent);

#endif /* __DFS_DCACHE_H__ */
