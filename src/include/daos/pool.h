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
#include <daos/hash.h>
#include <daos/scheduler.h>
#include <daos/pool_map.h>

int dc_pool_init(void);
void dc_pool_fini(void);

/* Client pool handle */
struct dc_pool {
	/* link to daos_client_hhash */
	struct daos_hlink	dp_hlink;
	/* container list of the pool */
	daos_list_t		dp_co_list;
	/* lock for the container list */
	pthread_rwlock_t	dp_co_list_lock;
	/* pool uuid */
	uuid_t			dp_pool;
	crt_group_t	       *dp_group;
	uuid_t			dp_pool_hdl;
	uint64_t		dp_capas;
	pthread_rwlock_t	dp_map_lock;
	struct pool_map	       *dp_map;
	uint32_t		dp_disconnecting:1,
				dp_slave:1; /* generated via g2l */
};

static inline struct dc_pool *
dc_pool_lookup(daos_handle_t poh)
{
	struct daos_hlink *dlink;

	if (daos_hhash_key_type(poh.cookie) != DAOS_HTYPE_POOL)
		return NULL;

	dlink = daos_hhash_link_lookup(daos_client_hhash, poh.cookie);
	if (dlink == NULL)
		return NULL;

	return container_of(dlink, struct dc_pool, dp_hlink);
}

static inline void
dc_pool_get(struct dc_pool *pool)
{
	daos_hhash_link_getref(daos_client_hhash, &pool->dp_hlink);
}

static inline void
dc_pool_put(struct dc_pool *pool)
{
	daos_hhash_link_putref(daos_client_hhash, &pool->dp_hlink);
}

int dc_pool_local2global(daos_handle_t poh, daos_iov_t *glob);
int dc_pool_global2local(daos_iov_t glob, daos_handle_t *poh);

int dc_pool_connect(struct daos_task *task);
int dc_pool_disconnect(struct daos_task *task);
int dc_pool_exclude(struct daos_task *task);
int dc_pool_query(struct daos_task *task);
int dc_pool_target_query(struct daos_task *task);

int
dc_pool_map_version_get(daos_handle_t ph, unsigned int *map_ver);

int
dc_pool_local_open(uuid_t pool_uuid, uuid_t pool_hdl_uuid,
		   unsigned int flags, const char *grp,
		   struct pool_map *map, daos_handle_t *ph);
int
dc_pool_local_close(daos_handle_t ph);

#endif /* __DAOS_POOL_H__ */
