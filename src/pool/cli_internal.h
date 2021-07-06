/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
void dc_pool_hdl_unlink(struct dc_pool *pool);
struct dc_pool *dc_pool_alloc(unsigned int nr);

int dc_pool_map_update(struct dc_pool *pool, struct pool_map *map,
		       unsigned int map_version, bool connect);

#endif /* __POOL_CLIENT_INTERNAL_H__ */
