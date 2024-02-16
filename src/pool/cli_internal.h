/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dc_pool: Pool Client Internal Declarations
 */

#ifndef __POOL_CLIENT_INTERNAL_H__
#define __POOL_CLIENT_INTERNAL_H__

#include <daos/pool.h>

void dc_pool_hdl_link(struct dc_pool *pool);
void dc_pool_hdl_unlink(struct dc_pool *pool);
struct dc_pool *dc_pool_alloc(unsigned int nr);

int dc_pool_map_update(struct dc_pool *pool, struct pool_map *map, bool connect);

#endif /* __POOL_CLIENT_INTERNAL_H__ */
