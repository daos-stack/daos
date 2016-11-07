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
 * dc_pool: Pool Client API
 *
 * This consists of dc_pool methods that do not belong to DAOS API.
 */

#ifndef __DAOS_POOL_H__
#define __DAOS_POOL_H__

#include <daos_types.h>
#include <daos/client.h>
#include <daos/common.h>
#include <crt_util/hash.h>

int dc_pool_init(void);
void dc_pool_fini(void);

/* Client pool handle */
struct dc_pool {
	/* link to daos_client_hhash */
	struct crt_hlink	dp_hlink;
	/* container list of the pool */
	crt_list_t		dp_co_list;
	/* lock for the container list */
	pthread_rwlock_t	dp_co_list_lock;
	/* pool uuid */
	uuid_t			dp_pool;
	uuid_t			dp_pool_hdl;
	uint64_t		dp_capas;
	struct pool_map	       *dp_map;
	struct pool_buf	       *dp_map_buf;	/* TODO: pool_map => pool_buf */
	uint32_t		dp_disconnecting:1,
				dp_slave:1; /* generated via g2l */
};

static inline struct dc_pool *
dc_pool_lookup(daos_handle_t poh)
{
	struct crt_hlink *dlink;

	if (crt_hhash_key_type(poh.cookie) != CRT_HTYPE_POOL)
		return NULL;

	dlink = crt_hhash_link_lookup(daos_client_hhash, poh.cookie);
	if (dlink == NULL)
		return NULL;

	return container_of(dlink, struct dc_pool, dp_hlink);
}

static inline void
dc_pool_put(struct dc_pool *pool)
{
	crt_hhash_link_putref(daos_client_hhash, &pool->dp_hlink);
}

#endif /* __DAOS_POOL_H__ */
