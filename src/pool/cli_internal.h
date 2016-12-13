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

#include <daos/client.h>

static inline void
dc_pool_add_cache(struct dc_pool *pool, daos_handle_t *hdl)
{
	/* add pool to hash and assign the cookie to hdl */
	daos_hhash_link_insert(daos_client_hhash, &pool->dp_hlink,
			       DAOS_HTYPE_POOL);
	daos_hhash_link_key(&pool->dp_hlink, &hdl->cookie);
}

static inline void
dc_pool_del_cache(struct dc_pool *pool)
{
	daos_hhash_link_delete(daos_client_hhash, &pool->dp_hlink);
}

#endif /* __POOL_CLIENT_INTERNAL_H__ */
