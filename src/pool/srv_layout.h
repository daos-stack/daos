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
 * ds_pool: Pool Server Storage Layout
 *
 * This header assembles (hopefully) everything related to the persistent
 * storage layout of pool metadata used by ds_pool.
 *
 * In the rdb, we have this layout:
 *
 *     Root KVS (GENERIC):
 *       Pool handle KVS (GENERIC)
 */

#ifndef __POOL_SRV_LAYOUT_H__
#define __POOL_SRV_LAYOUT_H__

#include <daos_types.h>

/*
 * Root KVS (RDB_KVS_GENERIC): pool attributes
 *
 * The pool map is stored in pool_buf format. Because version and target UUID
 * are absent from pool_buf, they have to be stored in ds_pool_attr_map_version
 * and ds_pool_attr_map_uuids, respectively. The target UUIDs are stored in
 * target ID order.
 *
 * ds_pool_attr_mode stores three sets of the capability bits: user, group, and
 * other.  Each set consists of DAOS_PC_NBITS bits, for DAOS_PC_*. Let N be
 * DAOS_PC_NBITS:
 *
 *                 Bit: 31      3N    2N      N      0
 *                       v       v     v      v      v
 *   ds_pool_attr_mode:  [padding][user][group][other]
 */
extern daos_iov_t ds_pool_attr_uid;		/* uint32_t */
extern daos_iov_t ds_pool_attr_gid;		/* uint32_t */
extern daos_iov_t ds_pool_attr_mode;		/* uint32_t */
extern daos_iov_t ds_pool_attr_map_version;	/* uint32_t */
extern daos_iov_t ds_pool_attr_map_buffer;	/* pool_buf */
extern daos_iov_t ds_pool_attr_map_uuids;	/* uuid_t[] (unused now) */
extern daos_iov_t ds_pool_attr_nhandles;	/* uint32_t */
extern daos_iov_t ds_pool_attr_handles;		/* pool handle KVS */

/* Pool handle KVS (RDB_KVS_GENERIC) */
struct pool_hdl {
	uint64_t	ph_capas;
};

#endif /* __POOL_SRV_LAYOUT_H__ */
