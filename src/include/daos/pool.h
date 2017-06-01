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
#include <daos/pool_map.h>
#include <daos/rsvc.h>
#include <daos/scheduler.h>

int dc_pool_init(void);
void dc_pool_fini(void);

/* Client pool handle */
struct dc_pool {
	/* container list of the pool */
	daos_list_t		dp_co_list;
	/* lock for the container list */
	pthread_rwlock_t	dp_co_list_lock;
	/* pool uuid */
	uuid_t			dp_pool;
	crt_group_t	       *dp_group;
	pthread_mutex_t		dp_client_lock;
	struct rsvc_client	dp_client;
	uuid_t			dp_pool_hdl;
	uint64_t		dp_capas;
	pthread_rwlock_t	dp_map_lock;
	struct pool_map	       *dp_map;
	uint32_t		dp_ref;
	uint32_t		dp_disconnecting:1,
				dp_slave:1; /* generated via g2l */
};

struct dc_pool *dc_hdl2pool(daos_handle_t hdl);
void dc_pool_get(struct dc_pool *pool);
void dc_pool_put(struct dc_pool *pool);

int dc_pool_local2global(daos_handle_t poh, daos_iov_t *glob);
int dc_pool_global2local(daos_iov_t glob, daos_handle_t *poh);
int dc_pool_connect(struct daos_task *task);
int dc_pool_disconnect(struct daos_task *task);
int dc_pool_query(struct daos_task *task);
int dc_pool_target_query(struct daos_task *task);
int dc_pool_exclude(struct daos_task *task);
int dc_pool_exclude_out(struct daos_task *task);
int dc_pool_add(struct daos_task *task);
int dc_pool_evict(struct daos_task *task);
int dc_pool_svc_stop(struct daos_task *task);

int
dc_pool_map_version_get(daos_handle_t ph, unsigned int *map_ver);

int
dc_pool_local_open(uuid_t pool_uuid, uuid_t pool_hdl_uuid,
		   unsigned int flags, const char *grp,
		   struct pool_map *map, daos_handle_t *ph);
int
dc_pool_local_close(daos_handle_t ph);

/**
 * Only test program might use these two api for now, so let's
 * put the definition here temporarily XXX.
 */ 
/**
 * add a set of storage targets from a pool.
 *
 * \param poh	[IN]	Pool connection handle.
 * \param tgts	[IN]	Target rank array to be added from the pool.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid pool handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 */
int
daos_pool_tgt_add(daos_handle_t poh, daos_rank_list_t *tgts, daos_event_t *ev);

/**
 * Exclude completely a set of storage targets from a pool. Compared with
 * daos_pool_exclude(), this API will mark the targets to be DOWNOUT, i.e.
 * the rebuilding for this target is done, while daos_pool_exclude() only
 * mark the target to be DOWN, i.e. the rebuilding might not finished yet.
 *
 * \param poh	[IN]	Pool connection handle.
 * \param tgts	[IN]	Target rank array to be excluded from the pool.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid pool handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 */
int
daos_pool_exclude_out(daos_handle_t poh, daos_rank_list_t *tgts,
		      daos_event_t *ev);

/**
 * Stop the current pool service leader.
 *
 * \param poh	[IN]	Pool connection handle
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid pool handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 */
int
daos_pool_svc_stop(daos_handle_t poh, daos_event_t *ev);

#endif /* __DAOS_POOL_H__ */
