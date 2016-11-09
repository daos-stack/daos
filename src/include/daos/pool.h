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

typedef void (*dc_pool_query_cb_t)(struct dc_pool *pool, void *arg, int rc,
				   daos_rank_list_t *tgts,
				   daos_pool_info_t *info);
int dc_pool_query(struct dc_pool *pool, crt_context_t ctx,
		  daos_rank_list_t *tgts, daos_pool_info_t *info,
		  dc_pool_query_cb_t cb, void *cb_arg);

int
dc_pool_connect(const uuid_t uuid, const char *grp,
		const daos_rank_list_t *svc, unsigned int flags,
		daos_handle_t *poh, daos_pool_info_t *info, daos_event_t *ev);
int
dc_pool_disconnect(daos_handle_t poh, daos_event_t *ev);
int
dc_pool_local2global(daos_handle_t poh, daos_iov_t *glob);
int
dc_pool_global2local(daos_iov_t glob, daos_handle_t *poh);
int
dc_pool_exclude(daos_handle_t poh, daos_rank_list_t *tgts, daos_event_t *ev);
int
dc_pool_target_query(daos_handle_t poh, daos_rank_list_t *tgts,
		     daos_rank_list_t *failed, daos_target_info_t *info_list,
		     daos_event_t *ev);
#endif /* __DAOS_POOL_H__ */
