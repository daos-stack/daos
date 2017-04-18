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

#ifndef __CONTAINER_SRV_INTERNAL_H__
#define __CONTAINER_SRV_INTERNAL_H__

#include <daos/lru.h>
#include <daos_srv/daos_server.h>

/* To avoid including srv_layout.h for everybody. */
struct container_hdl;

/* To avoid including daos_srv/pool.h for everybody. */
struct ds_pool;
struct ds_pool_hdl;
struct ds_pool_mpool;

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
 * Container service
 *
 * References the ds_pool_mpool descriptor. Identified by a number unique
 * within the pool.
 *
 * TODO: After moving to LRU, we are still not evicting cont_svc objects based
 * on their numbers of container handles yet.
 */
struct cont_svc {
	struct daos_llink	cs_entry;
	uuid_t			cs_pool_uuid;
	uint64_t		cs_id;
	struct ds_pool_mpool   *cs_mpool;
	struct ds_pool	       *cs_pool;
	ABT_rwlock		cs_lock;
	daos_handle_t		cs_root;	/* root tree */
	daos_handle_t		cs_containers;	/* container tree */
	daos_handle_t		cs_hdls;	/* container handle tree */
};

/* Container descriptor */
struct cont {
	uuid_t			c_uuid;
	struct cont_svc	       *c_svc;
	daos_handle_t		c_cont;		/* container attribute tree */
	daos_handle_t		c_lres;		/* LRE tree */
	daos_handle_t		c_lhes;		/* LHE tree */
};

/*
 * srv.c
 */

/*
 * srv_container.c
 */
int ds_cont_op_handler(crt_rpc_t *rpc);
int ds_cont_svc_cache_init(void);
void ds_cont_svc_cache_fini(void);
int ds_cont_bcast_create(crt_context_t ctx, struct cont_svc *svc,
			 crt_opcode_t opcode, crt_rpc_t **rpc);

/*
 * srv_epoch.c
 */
int ds_cont_epoch_init_hdl(struct cont *cont, struct container_hdl *hdl,
			   daos_epoch_state_t *state);
int ds_cont_epoch_read_state(struct cont *cont, struct container_hdl *hdl,
			     daos_epoch_state_t *state);
int ds_cont_epoch_fini_hdl(struct cont *cont, struct container_hdl *hdl);
int ds_cont_epoch_query(struct ds_pool_hdl *pool_hdl, struct cont *cont,
			struct container_hdl *hdl, crt_rpc_t *rpc);
int ds_cont_epoch_hold(struct ds_pool_hdl *pool_hdl, struct cont *cont,
		       struct container_hdl *hdl, crt_rpc_t *rpc);
int ds_cont_epoch_slip(struct ds_pool_hdl *pool_hdl, struct cont *cont,
		       struct container_hdl *hdl, crt_rpc_t *rpc);
int ds_cont_epoch_discard(struct ds_pool_hdl *pool_hdl, struct cont *cont,
			  struct container_hdl *hdl, crt_rpc_t *rpc);
int ds_cont_epoch_commit(struct ds_pool_hdl *pool_hdl, struct cont *cont,
			 struct container_hdl *hdl, crt_rpc_t *rpc);
int ds_cont_epoch_query(struct ds_pool_hdl *pool_hdl, struct cont *cont,
			struct container_hdl *hdl, crt_rpc_t *rpc);

/*
 * srv_target.c
 */
int ds_cont_tgt_destroy_handler(crt_rpc_t *rpc);
int ds_cont_tgt_destroy_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				   void *priv);
int ds_cont_tgt_open_handler(crt_rpc_t *rpc);
int ds_cont_tgt_open_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				void *priv);
int ds_cont_tgt_close_handler(crt_rpc_t *rpc);
int ds_cont_tgt_close_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				 void *priv);
int ds_cont_tgt_query_handler(crt_rpc_t *rpc);
int ds_cont_tgt_query_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				 void *priv);
int ds_cont_tgt_epoch_discard_handler(crt_rpc_t *rpc);
int ds_cont_tgt_epoch_discard_aggregator(crt_rpc_t *source, crt_rpc_t *result,
					 void *priv);
int ds_cont_cache_create(struct daos_lru_cache **cache);
void ds_cont_cache_destroy(struct daos_lru_cache *cache);
int ds_cont_hdl_hash_create(struct dhash_table *hash);
void ds_cont_hdl_hash_destroy(struct dhash_table *hash);

#endif /* __CONTAINER_SRV_INTERNAL_H__ */
