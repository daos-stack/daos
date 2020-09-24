/**
 * (C) Copyright 2019-2020 Intel Corporation.
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

#if defined(__cplusplus)
}
#endif
#endif /* __DFS_INTERNAL_H__ */
