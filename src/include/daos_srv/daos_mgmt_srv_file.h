/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 * (C) Copyright 2025 Vdura Inc.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_MGMT_WIP_H__
#define __DAOS_MGMT_WIP_H__

#include <uuid/uuid.h>

/**
 * Common file names used by each layer to store persistent data.
 */
#define VOS_FILE     "vos-" /** suffixed by thread id */
#define RDB_FILE     "rdb-"
#define DIR_NEWBORNS "NEWBORNS"
#define DIR_ZOMBIES  "ZOMBIES"

/**
 * \brief Generate a pool file path.
 *
 * Allocates a buffer and generates the path into it. The pointer the allocated buffer is returned
 * via \p fpath. The caller has to free the buffer.
 *
 * Rule: '${dir}/${pool_uuid}/${fname}${idx}'
 * e.g:  '/mnt/daos/xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx/vos-0'
 *
 * \param[in]	dir		Base path
 * \param[in]	pool_uuid	Pool uuid
 * \param[in]	fname		File name
 * \param[in]	idx		File index
 * \param[out]	fpath		Generated file path
 *
 * \retval -DER_NOMEM	Out of memory.
 * \retval DER_SUCCESS	Success.
 */
int
ds_mgmt_file(const char *dir, const uuid_t pool_uuid, const char *fname, int *idx, char **fpath);

/**
 * fsync(2) a directory.
 *
 * \param[in]	dir	Directory to sync.
 */
int
ds_mgmt_dir_fsync(const char *dir);

typedef void (*bind_cpu_fn_t)(int tgt_id);

/**
 * Recreate pool vos and rdb files on \p storage_path.
 *
 * \param[in] pool_uuid		Pool uuid
 * \param[in] scm_size		Per vos file size
 * \param[in] tgt_nr		Vos files number
 * \param[in] rdb_blob_sz	rdb file size (rdb file will not be recreated if size is zero)
 * \param[in] storage_path	Base path to store vos and rdb files
 * \param[in] bind_cpu_fn	Bind a separate cpu to each vos file allocation
 */
int
ds_mgmt_tgt_recreate(uuid_t pool_uuid, daos_size_t scm_size, int tgt_nr, daos_size_t rdb_blob_sz,
		     const char *storage_path, bind_cpu_fn_t bind_cpu_fn);

/**
 * Parallel recreate vos files.
 *
 * \param[in] uuid		Pool uuid
 * \param[in] scm_size		Per vos file size
 * \param[in] tgt_nr		Vos files number
 * \param[in] cancel_pending	If true, preallocate will abort
 * \param[in] newborns_path	Base path for store vos/rdb files
 * \param[in] bind_cpu_fn	e.g. `dss_bind_to_xstream_cpuset`
 */
int
ds_mgmt_tgt_preallocate_parallel(uuid_t uuid, daos_size_t scm_size, int tgt_nr,
				 bool *cancel_pending, const char *newborns_path,
				 bind_cpu_fn_t bind_cpu_fn);

/**
 * Sequential recreate vos files.
 *
 * \param[in] uuid		Pool uuid
 * \param[in] scm_size		Per vos file size
 * \param[in] tgt_nr		Vos files number
 * \param[in] newborns_path	Base path for store vos/rdb files
 */
int
ds_mgmt_tgt_preallocate_sequential(uuid_t uuid, daos_size_t scm_size, int tgt_nr,
				   const char *newborns_path);

#endif /* __DAOS_MGMT_WIP_H__ */
