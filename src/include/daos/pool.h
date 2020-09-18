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
 * dc_pool: Pool Client API
 *
 * This consists of dc_pool methods that do not belong to DAOS API.
 */

#ifndef __DD_POOL_H__
#define __DD_POOL_H__

#include <daos/common.h>
#include <gurt/hash.h>
#include <daos/pool_map.h>
#include <daos/rsvc.h>
#include <daos/tse.h>
#include <daos_types.h>
#include <daos_pool.h>

/** pool query request bits */
#define DAOS_PO_QUERY_SPACE		(1ULL << 0)
#define DAOS_PO_QUERY_REBUILD_STATUS	(1ULL << 1)

#define DAOS_PO_QUERY_PROP_LABEL	(1ULL << 16)
#define DAOS_PO_QUERY_PROP_SPACE_RB	(1ULL << 17)
#define DAOS_PO_QUERY_PROP_SELF_HEAL	(1ULL << 18)
#define DAOS_PO_QUERY_PROP_RECLAIM	(1ULL << 19)
#define DAOS_PO_QUERY_PROP_ACL		(1ULL << 20)
#define DAOS_PO_QUERY_PROP_OWNER	(1ULL << 21)
#define DAOS_PO_QUERY_PROP_OWNER_GROUP	(1ULL << 22)
#define DAOS_PO_QUERY_PROP_SVC_LIST	(1ULL << 23)

#define DAOS_PO_QUERY_PROP_ALL						\
	(DAOS_PO_QUERY_PROP_LABEL | DAOS_PO_QUERY_PROP_SPACE_RB |	\
	 DAOS_PO_QUERY_PROP_SELF_HEAL | DAOS_PO_QUERY_PROP_RECLAIM |	\
	 DAOS_PO_QUERY_PROP_ACL | DAOS_PO_QUERY_PROP_OWNER |		\
	 DAOS_PO_QUERY_PROP_OWNER_GROUP | DAOS_PO_QUERY_PROP_SVC_LIST)


int dc_pool_init(void);
void dc_pool_fini(void);

/* Client pool handle */
struct dc_pool {
	/* link chain in the global handle hash table */
	struct d_hlink		dp_hlink;
	/* container list of the pool */
	d_list_t		dp_co_list;
	/* lock for the container list */
	pthread_rwlock_t	dp_co_list_lock;
	/* pool uuid */
	uuid_t			dp_pool;
	struct dc_mgmt_sys     *dp_sys;
	pthread_mutex_t		dp_client_lock;
	struct rsvc_client	dp_client;
	uuid_t			dp_pool_hdl;
	uint64_t		dp_capas;
	pthread_rwlock_t	dp_map_lock;
	struct pool_map	       *dp_map;
	uint32_t		dp_ver;
	uint32_t		dp_disconnecting:1,
				dp_slave:1; /* generated via g2l */
	/* required/allocated pool map size */
	size_t			dp_map_sz;
};

static inline unsigned int
dc_pool_get_version(struct dc_pool *pool)
{
	unsigned int	ver;

	D_RWLOCK_RDLOCK(&pool->dp_map_lock);
	ver = pool_map_get_version(pool->dp_map);
	D_RWLOCK_UNLOCK(&pool->dp_map_lock);

	return ver;
}

struct dc_pool *dc_hdl2pool(daos_handle_t hdl);
void dc_pool_get(struct dc_pool *pool);
void dc_pool_put(struct dc_pool *pool);

int dc_pool_local2global(daos_handle_t poh, d_iov_t *glob);
int dc_pool_global2local(d_iov_t glob, daos_handle_t *poh);
int dc_pool_connect(tse_task_t *task);
int dc_pool_disconnect(tse_task_t *task);
int dc_pool_query(tse_task_t *task);
int dc_pool_query_target(tse_task_t *task);
int dc_pool_list_attr(tse_task_t *task);
int dc_pool_get_attr(tse_task_t *task);
int dc_pool_set_attr(tse_task_t *task);
int dc_pool_del_attr(tse_task_t *task);
int dc_pool_exclude(tse_task_t *task);
int dc_pool_exclude_out(tse_task_t *task);
int dc_pool_add(tse_task_t *task);
int dc_pool_drain(tse_task_t *task);
int dc_pool_evict(tse_task_t *task);
int dc_pool_stop_svc(tse_task_t *task);
int dc_pool_list_cont(tse_task_t *task);

int dc_pool_add_replicas(tse_task_t *task);
int dc_pool_remove_replicas(tse_task_t *task);

int dc_pool_map_version_get(daos_handle_t ph, unsigned int *map_ver);
int dc_pool_update_map(daos_handle_t ph, struct pool_map *map);

#endif /* __DD_POOL_H__ */
