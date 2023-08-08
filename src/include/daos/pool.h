/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#define DAOS_PO_QUERY_SPACE			(1ULL << 0)
#define DAOS_PO_QUERY_REBUILD_STATUS		(1ULL << 1)
#define PROP_BIT_START				16
#define DAOS_PO_QUERY_PROP_BIT_START		PROP_BIT_START
#define DAOS_PO_QUERY_PROP_LABEL		(1ULL << (PROP_BIT_START + 0))
#define DAOS_PO_QUERY_PROP_SPACE_RB		(1ULL << (PROP_BIT_START + 1))
#define DAOS_PO_QUERY_PROP_SELF_HEAL		(1ULL << (PROP_BIT_START + 2))
#define DAOS_PO_QUERY_PROP_RECLAIM		(1ULL << (PROP_BIT_START + 3))
#define DAOS_PO_QUERY_PROP_ACL			(1ULL << (PROP_BIT_START + 4))
#define DAOS_PO_QUERY_PROP_OWNER		(1ULL << (PROP_BIT_START + 5))
#define DAOS_PO_QUERY_PROP_OWNER_GROUP		(1ULL << (PROP_BIT_START + 6))
#define DAOS_PO_QUERY_PROP_SVC_LIST		(1ULL << (PROP_BIT_START + 7))
#define DAOS_PO_QUERY_PROP_EC_CELL_SZ		(1ULL << (PROP_BIT_START + 8))
#define DAOS_PO_QUERY_PROP_REDUN_FAC		(1ULL << (PROP_BIT_START + 9))
#define DAOS_PO_QUERY_PROP_EC_PDA		(1ULL << (PROP_BIT_START + 10))
#define DAOS_PO_QUERY_PROP_RP_PDA		(1ULL << (PROP_BIT_START + 11))
#define DAOS_PO_QUERY_PROP_POLICY		(1ULL << (PROP_BIT_START + 12))
#define DAOS_PO_QUERY_PROP_GLOBAL_VERSION	(1ULL << (PROP_BIT_START + 13))
#define DAOS_PO_QUERY_PROP_UPGRADE_STATUS	(1ULL << (PROP_BIT_START + 14))
#define DAOS_PO_QUERY_PROP_SCRUB_MODE		(1ULL << (PROP_BIT_START + 15))
#define DAOS_PO_QUERY_PROP_SCRUB_FREQ		(1ULL << (PROP_BIT_START + 16))
#define DAOS_PO_QUERY_PROP_SCRUB_THRESH		(1ULL << (PROP_BIT_START + 17))
#define DAOS_PO_QUERY_PROP_SVC_REDUN_FAC	(1ULL << (PROP_BIT_START + 18))
#define DAOS_PO_QUERY_PROP_OBJ_VERSION		(1ULL << (PROP_BIT_START + 19))
#define DAOS_PO_QUERY_PROP_PERF_DOMAIN		(1ULL << (PROP_BIT_START + 20))
#define DAOS_PO_QUERY_PROP_CHECKPOINT_MODE      (1ULL << (PROP_BIT_START + 21))
#define DAOS_PO_QUERY_PROP_CHECKPOINT_FREQ      (1ULL << (PROP_BIT_START + 22))
#define DAOS_PO_QUERY_PROP_CHECKPOINT_THRESH    (1ULL << (PROP_BIT_START + 23))
#define DAOS_PO_QUERY_PROP_BIT_END              39

#define DAOS_PO_QUERY_PROP_ALL                                                                     \
	(DAOS_PO_QUERY_PROP_LABEL | DAOS_PO_QUERY_PROP_SPACE_RB | DAOS_PO_QUERY_PROP_SELF_HEAL |   \
	 DAOS_PO_QUERY_PROP_RECLAIM | DAOS_PO_QUERY_PROP_ACL | DAOS_PO_QUERY_PROP_OWNER |          \
	 DAOS_PO_QUERY_PROP_OWNER_GROUP | DAOS_PO_QUERY_PROP_SVC_LIST |                            \
	 DAOS_PO_QUERY_PROP_EC_CELL_SZ | DAOS_PO_QUERY_PROP_EC_PDA | DAOS_PO_QUERY_PROP_RP_PDA |   \
	 DAOS_PO_QUERY_PROP_REDUN_FAC | DAOS_PO_QUERY_PROP_POLICY |                                \
	 DAOS_PO_QUERY_PROP_GLOBAL_VERSION | DAOS_PO_QUERY_PROP_UPGRADE_STATUS |                   \
	 DAOS_PO_QUERY_PROP_SCRUB_MODE | DAOS_PO_QUERY_PROP_SCRUB_FREQ |                           \
	 DAOS_PO_QUERY_PROP_SCRUB_THRESH | DAOS_PO_QUERY_PROP_SVC_REDUN_FAC |                      \
	 DAOS_PO_QUERY_PROP_OBJ_VERSION | DAOS_PO_QUERY_PROP_PERF_DOMAIN |                         \
	 DAOS_PO_QUERY_PROP_CHECKPOINT_MODE | DAOS_PO_QUERY_PROP_CHECKPOINT_FREQ |                 \
	 DAOS_PO_QUERY_PROP_CHECKPOINT_THRESH)

/*
 * Aggregation of pool/container/object/keys disk format change.
 * 1 for 2.2
 * 2 for 2.4
 * 3 for 2.6
 */
#define DAOS_POOL_GLOBAL_VERSION		3

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
	tse_task_t	       *dp_map_task;
	/* highest known pool map version */
	uint32_t		dp_map_version_known;
	uint32_t		dp_disconnecting:1,
				dp_slave:1, /* generated via g2l */
				dp_rf_valid:1;
	/* required/allocated pool map size */
	size_t			dp_map_sz;

	/* pool redunc factor */
	uint32_t		dp_rf;
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

static inline void
dc_pool2hdl(struct dc_pool *pool, daos_handle_t *hdl)
{
	daos_hhash_link_getref(&pool->dp_hlink);
	daos_hhash_link_key(&pool->dp_hlink, &hdl->cookie);
}

static inline void
dc_pool2hdl_noref(struct dc_pool *pool, daos_handle_t *hdl)
{
	daos_hhash_link_key(&pool->dp_hlink, &hdl->cookie);
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
int dc_pool_reint(tse_task_t *task);
int dc_pool_drain(tse_task_t *task);
int dc_pool_stop_svc(tse_task_t *task);
int dc_pool_list_cont(tse_task_t *task);
int dc_pool_filter_cont(tse_task_t *task);
int dc_pool_tgt_idx2ptr(struct dc_pool *pool, uint32_t tgt_idx,
			struct pool_target **tgt);

int dc_pool_get_redunc(daos_handle_t poh);

int dc_pool_map_version_get(daos_handle_t ph, unsigned int *map_ver);
int dc_pool_choose_svc_rank(const char *label, uuid_t puuid,
			    struct rsvc_client *cli, pthread_mutex_t *cli_lock,
			    struct dc_mgmt_sys *sys,
			    crt_endpoint_t *ep);
int dc_pool_create_map_refresh_task(daos_handle_t pool_hdl, uint32_t map_version,
				    tse_sched_t *sched, tse_task_t **task);
void dc_pool_abandon_map_refresh_task(tse_task_t *task);

#endif /* __DD_POOL_H__ */
