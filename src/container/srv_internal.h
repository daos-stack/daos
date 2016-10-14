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
 * ds_cont: Client Server Internal Declarations
 */

#ifndef __CONTAINER_SERVER_INTERNAL_H__
#define __CONTAINER_SERVER_INTERNAL_H__

#include <daos/lru.h>
#include <daos/transport.h>
#include <daos_srv/daos_server.h>

/* ds_cont thread local storage structure */
struct dsm_tls {
	struct daos_lru_cache  *dt_cont_cache;
	struct dhash_table	dt_cont_hdl_hash;
};

extern struct dss_module_key cont_module_key;

static inline struct dsm_tls *
dsm_tls_get()
{
	struct dsm_tls			*tls;
	struct dss_thread_local_storage	*dtc;

	dtc = dss_tls_get();
	tls = (struct dsm_tls *)dss_module_key_get(dtc, &cont_module_key);
	return tls;
}

/*
 * srv.c
 */
int ds_cont_corpc_create(dtp_context_t ctx, dtp_group_t *group,
			 dtp_opcode_t opcode, dtp_rpc_t **rpc);

/*
 * srv_pool.c
 */
int dsms_hdlr_pool_connect(dtp_rpc_t *rpc);
int dsms_hdlr_pool_disconnect(dtp_rpc_t *rpc);

/*
 * srv_container.c
 */
int dsms_hdlr_cont_create(dtp_rpc_t *rpc);
int dsms_hdlr_cont_destroy(dtp_rpc_t *rpc);
int dsms_hdlr_cont_open(dtp_rpc_t *rpc);
int dsms_hdlr_cont_close(dtp_rpc_t *rpc);
int dsms_hdlr_cont_op(dtp_rpc_t *rpc);

/*
 * srv_target.c
 */
int dsms_hdlr_tgt_pool_connect(dtp_rpc_t *rpc);
int dsms_hdlr_tgt_pool_connect_aggregate(dtp_rpc_t *source, dtp_rpc_t *result,
					 void *priv);
int dsms_hdlr_tgt_pool_disconnect(dtp_rpc_t *rpc);
int dsms_hdlr_tgt_pool_disconnect_aggregate(dtp_rpc_t *source,
					    dtp_rpc_t *result, void *priv);
int dsms_hdlr_tgt_cont_destroy(dtp_rpc_t *rpc);
int dsms_hdlr_tgt_cont_destroy_aggregate(dtp_rpc_t *source, dtp_rpc_t *result,
					 void *priv);
int dsms_hdlr_tgt_cont_open(dtp_rpc_t *rpc);
int dsms_hdlr_tgt_cont_open_aggregate(dtp_rpc_t *source, dtp_rpc_t *result,
				      void *priv);
int dsms_hdlr_tgt_cont_close(dtp_rpc_t *rpc);
int dsms_hdlr_tgt_cont_close_aggregate(dtp_rpc_t *source, dtp_rpc_t *result,
				       void *priv);
int ds_cont_cache_create(struct daos_lru_cache **cache);
void ds_cont_cache_destroy(struct daos_lru_cache *cache);
int ds_cont_hdl_hash_create(struct dhash_table *hash);
void ds_cont_hdl_hash_destroy(struct dhash_table *hash);

#endif /* __CONTAINER_SERVER_INTERNAL_H__ */
