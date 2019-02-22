/**
 * (C) Copyright 2016 Intel Corporation.
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
 * Server-side management API offering the following functionalities:
 * - manage storage allocation (PMEM files, disk partitions, ...)
 * - initialize pool and target service
 * - provide fault domains
 */

#ifndef __MGMT_SRV_H__
#define __MGMT_SRV_H__

/**
 * Common file names used by each layer to store persistent data
 */
#define	VOS_FILE	"vos-" /* suffixed by thread id */
#define	DSM_META_FILE	"meta"
#define RDB_FILE	"rdb-"

int
ds_mgmt_tgt_file(const uuid_t pool_uuid, const char *fname, int *idx,
		 char **fpath);
int
ds_mgmt_tgt_pool_iterate(int (*cb)(uuid_t uuid, void *arg), void *arg);

#endif /* __MGMT_SRV_H__ */
