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
 * See src/include/daos_srv/pool.h for the overall mpool storage layout. In the
 * "ds_pool root tree", simply called the "root tree" in ds_pool, we have this
 * layout:
 *
 *     Root tree (NV):
 *       Pool handle tree (UV)
 */

#ifndef __POOL_SRV_LAYOUT_H__
#define __POOL_SRV_LAYOUT_H__

#include <stdint.h>

/*
 * Root tree (DBTREE_CLASS_NV): pool attributes
 *
 * The pool map is stored in pool_buf format. Because version and target UUID
 * are absent from pool_buf, they have to be stored separately. The target
 * UUIDs are stored in target ID order.
 *
 * POOL_MODE stores three sets of the capability bits: user, group, and other.
 * Each set consists of DAOS_PC_NBITS bits, for DAOS_PC_*. Let N be
 * DAOS_PC_NBITS:
 *
 *        Bit: 31      3N    2N      N      0
 *              v       v     v      v      v
 *  POOL_MODE:  [padding][user][group][other]
 */
#define POOL_UID		"pool_uid"		/* uint32_t */
#define POOL_GID		"pool_gid"		/* uint32_t */
#define POOL_MODE		"pool_mode"		/* uint32_t */
#define POOL_MAP_VERSION	"pool_map_version"	/* uint32_t */
#define POOL_MAP_BUFFER		"pool_map_buffer"	/* pool_buf */
#define POOL_MAP_TARGET_UUIDS	"pool_map_target_uuids"	/* uuid_t[] */
#define POOL_NHANDLES		"pool_nhandles"		/* uint32_t */
#define POOL_HANDLES		"pool_handles"		/* btr_root (pool */
							/* handle tree) */

/* Pool handle tree (DBTREE_CLASS_UV) */
struct pool_hdl {
	uint64_t	ph_capas;
};

#endif /* __POOL_SRV_LAYOUT_H__ */
