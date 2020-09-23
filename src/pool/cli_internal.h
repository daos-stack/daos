/**
 * (C) Copyright 2016-2020 Intel Corporation.
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

static inline void
dc_pool2hdl(struct dc_pool *pool, daos_handle_t *hdl)
{
	daos_hhash_link_getref(&pool->dp_hlink);
	daos_hhash_link_key(&pool->dp_hlink, &hdl->cookie);
}

void dc_pool_hdl_link(struct dc_pool *pool);
struct dc_pool *dc_pool_alloc(unsigned int nr);

int dc_pool_map_update(struct dc_pool *pool, struct pool_map *map,
		       unsigned int map_version, bool connect);

#endif /* __POOL_CLIENT_INTERNAL_H__ */
