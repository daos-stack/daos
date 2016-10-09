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
 * dc_pool: Pool Client Internal Declarations
 */

#ifndef __POOL_CLIENT_INTERNAL_H__
#define __POOL_CLIENT_INTERNAL_H__

#include <daos/pool_map.h>

#define DC_POOL_GLOB_MAGIC	(0x16da0386)

/* Structure of global buffer for dmsc_pool */
struct dsmc_pool_glob {
	/* magic number, DC_POOL_GLOB_MAGIC */
	uint32_t	dpg_magic;
	uint32_t	dpg_padding;
	/* pool uuid and capas */
	uuid_t		dpg_pool;
	uuid_t		dpg_pool_hdl;
	uint64_t	dpg_capas;
	/* poolmap version */
	uint32_t	dpg_map_version;
	/* number of component of poolbuf, same as pool_buf::pb_nr */
	uint32_t	dpg_map_pb_nr;
	struct pool_buf	dpg_map_buf[0];
};

static inline daos_size_t
dsmc_pool_glob_buf_size(unsigned int pb_nr)
{
	return offsetof(struct dsmc_pool_glob, dpg_map_buf) +
	       pool_buf_size(pb_nr);
}

static inline int
dsmc_handle_type(daos_handle_t hdl)
{
	return daos_hhash_key_type(hdl.cookie);
}

static inline void
dsmc_pool_add_cache(struct dsmc_pool *pool, daos_handle_t *hdl)
{
	/* add pool to hash and assign the cookie to hdl */
	daos_hhash_link_insert(dsmc_hhash, &pool->dp_hlink, DAOS_HTYPE_POOL);
	daos_hhash_link_key(&pool->dp_hlink, &hdl->cookie);
}

static inline void
dsmc_pool_del_cache(struct dsmc_pool *pool)
{
	daos_hhash_link_delete(dsmc_hhash, &pool->dp_hlink);
}

#endif /* __POOL_CLIENT_INTERNAL_H__ */
