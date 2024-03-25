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
 * \param[out] dcache		The newly created dir-cache
 * \param[in] dfs		The DAOS File System to cache
 * \param[in] bits		Power2(bits) is the size of cache
 * \param[in] rec_timeout	Timeout in seconds of a dir-cache entry.  When this value is equal
 *				to zero, the dir-cache is deactivated.
 *
 * \return			0 on success, negative value on error
 */
int
dcache_create(dfs_dcache_t **dcache, dfs_t *dfs, uint32_t bits, uint32_t rec_timeout);

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
 * Look up a path in a given dir-cache to get the dfs_obj for a dir. If not found, call to
 * dfs_lookup() to open the object on DAOS. If the corresponding object is a dir, insert the
 * object into the dir-cache.
 *
 * \param[out] rec	The matched dir-cache record.
 * \param[in] dcache	The dir-cache being lookup or inserted
 * \param[in] path	File path of the directory to lookup or insert
 * \param[in] path_len	Length of the file path, not including null terminating ('\0').
 *
 * \return		0 on success, negative value on error
 */
int
dcache_find_insert(dcache_rec_t **rec, dfs_dcache_t *dcache, char *path, size_t path_len);

/**
 * Convert a dir-cache record to a dfs object
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
 * Decrease two times the reference counter of a given dir-cache record and remove it from its
 * dir-cache.
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
