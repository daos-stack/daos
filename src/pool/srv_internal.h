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
 * ds_pool: Pool Server Internal Declarations
 */

#ifndef __POOL_SRV_INTERNAL_H__
#define __POOL_SRV_INTERNAL_H__

#include <daos/list.h>
#include <daos_srv/daos_server.h>

/**
 * DSM server thread local storage structure
 */
struct pool_tls {
	/* in-memory structures TLS instance */
	struct daos_list_head	dt_pool_list;
};

extern struct dss_module_key pool_module_key;

static inline struct pool_tls *
pool_tls_get()
{
	struct pool_tls			*tls;
	struct dss_thread_local_storage	*dtc;

	dtc = dss_tls_get();
	tls = (struct pool_tls *)dss_module_key_get(dtc, &pool_module_key);
	return tls;
}

/*
 * srv_pool.c
 */
int ds_pool_svc_hash_init(void);
void ds_pool_svc_hash_fini(void);
int ds_pool_connect_handler(crt_rpc_t *rpc);
int ds_pool_disconnect_handler(crt_rpc_t *rpc);
int ds_pool_query_handler(crt_rpc_t *rpc);
int ds_pool_tgt_update_handler(crt_rpc_t *rpc);
int ds_pool_evict_handler(crt_rpc_t *rpc);

/*
 * srv_target.c
 */
int ds_pool_cache_init(void);
void ds_pool_cache_fini(void);
int ds_pool_hdl_hash_init(void);
void ds_pool_hdl_hash_fini(void);
int ds_pool_tgt_connect_handler(crt_rpc_t *rpc);
int ds_pool_tgt_connect_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				   void *priv);
int ds_pool_tgt_disconnect_handler(crt_rpc_t *rpc);
int ds_pool_tgt_disconnect_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				      void *priv);
int ds_pool_tgt_update_map_handler(crt_rpc_t *rpc);
int ds_pool_tgt_update_map_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				      void *priv);
struct ds_pool_create_arg {
	struct pool_buf	       *pca_map_buf;
	uint32_t		pca_map_version;
	int			pca_create_group;
};
int ds_pool_lookup_create(const uuid_t uuid, struct ds_pool_create_arg *arg,
			  struct ds_pool **pool);
void ds_pool_child_purge(struct pool_tls *tls);

/*
 * srv_util.c
 */
int ds_pool_group_create(const uuid_t pool_uuid, const struct pool_map *map,
			 crt_group_t **group);
int ds_pool_group_destroy(const uuid_t pool_uuid, crt_group_t *group);
int ds_pool_map_tgts_update(struct pool_map *map, daos_rank_list_t *tgts,
			    daos_rank_list_t *tgts_failed, int opc);

#endif /* __POOL_SRV_INTERNAL_H__ */
