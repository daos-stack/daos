/*
 * (C) Copyright 2016-2019 Intel Corporation.
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
#include <daos_srv/rdb.h>
#include <daos_srv/rsvc.h>
#include <daos_srv/container.h>

/* To avoid including srv_layout.h for everybody. */
struct container_hdl;

/* To avoid including daos_srv/pool.h for everybody. */
struct ds_pool;
struct ds_pool_hdl;

/* ds_cont thread local storage structure */
struct dsm_tls {
	struct daos_lru_cache  *dt_cont_cache;
	struct d_hash_table	dt_cont_hdl_hash;
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
 * Identified by a number unique within the pool.
 */
struct cont_svc {
	uuid_t			cs_pool_uuid;
	uint64_t		cs_id;
	struct ds_rsvc	       *cs_rsvc;
	ABT_rwlock		cs_lock;
	rdb_path_t		cs_root;	/* root KVS */
	rdb_path_t		cs_conts;	/* container KVS */
	rdb_path_t		cs_hdls;	/* container handle KVS */
	struct ds_pool	       *cs_pool;
};

/* Container descriptor */
struct cont {
	uuid_t			c_uuid;
	struct cont_svc	       *c_svc;
	rdb_path_t		c_prop;		/* container properties KVS */
	rdb_path_t		c_lres;		/* LRE KVS */
	rdb_path_t		c_lhes;		/* LHE KVS */
	rdb_path_t		c_snaps;	/* Snapshots KVS */
	rdb_path_t		c_user;		/* user attributes KVS */
};

/* OID range for allocator */
struct oid_iv_range {
	uint64_t	oid;
	daos_size_t	num_oids;
};

/**
 * per-node container (memory) object
 */
struct ds_cont {
	struct daos_llink	sc_list;
	uuid_t			sc_uuid;
	uuid_t			sp_uuid;
	struct ds_iv_ns		*sc_iv_ns;
};

/* Container IV structure */
struct cont_iv_snapshot {
	int snap_cnt;
	uint64_t snaps[0];
};

struct cont_iv_capa {
	uint64_t	capas;
};

struct cont_iv_entry {
	uuid_t	cont_uuid;
	union {
		struct cont_iv_snapshot iv_snap;
		struct cont_iv_capa	iv_capa;
	};
};

struct cont_iv_key {
	uuid_t	cont_uuid;
};

/*
 * srv_container.c
 */
void ds_cont_op_handler(crt_rpc_t *rpc);
int ds_cont_bcast_create(crt_context_t ctx, struct cont_svc *svc,
			 crt_opcode_t opcode, crt_rpc_t **rpc);
int ds_cont_oid_fetch_add(uuid_t poh_uuid, uuid_t co_uuid, uuid_t coh_uuid,
			  uint64_t num_oids, uint64_t *oid);
int cont_svc_lookup_leader(uuid_t pool_uuid, uint64_t id,
			   struct cont_svc **svcp, struct rsvc_hint *hint);
int cont_lookup(struct rdb_tx *tx, const struct cont_svc *svc,
		const uuid_t uuid, struct cont **cont);
void cont_svc_put_leader(struct cont_svc *svc);

/*
 * srv_epoch.c
 */
int ds_cont_epoch_init_hdl(struct rdb_tx *tx, struct cont *cont,
			   uuid_t c_hdl, struct container_hdl *hdl);
int ds_cont_epoch_fini_hdl(struct rdb_tx *tx, struct cont *cont,
			   crt_context_t ctx, struct container_hdl *hdl);
int ds_cont_epoch_query(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
			struct cont *cont, struct container_hdl *hdl,
			crt_rpc_t *rpc);
int ds_cont_epoch_discard(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
			  struct cont *cont, struct container_hdl *hdl,
			  crt_rpc_t *rpc);
int ds_cont_epoch_commit(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
			 struct cont *cont, struct container_hdl *hdl,
			 crt_rpc_t *rpc, bool snapshot);
int ds_cont_epoch_aggregate(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
			    struct cont *cont, struct container_hdl *hdl,
			    crt_rpc_t *rpc);
int ds_cont_snap_list(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		      struct cont *cont, struct container_hdl *hdl,
		      crt_rpc_t *rpc);
int ds_cont_snap_destroy(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
			 struct cont *cont, struct container_hdl *hdl,
			 crt_rpc_t *rpc);

int
ds_cont_get_snapshots(uuid_t pool_uuid, uuid_t cont_uuid,
		      daos_epoch_t **snapshots, int *snap_count);
/**
 * srv_target.c
 */
void ds_cont_tgt_destroy_handler(crt_rpc_t *rpc);
int ds_cont_tgt_destroy_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				   void *priv);
void ds_cont_tgt_close_handler(crt_rpc_t *rpc);
int ds_cont_tgt_close_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				 void *priv);
void ds_cont_tgt_query_handler(crt_rpc_t *rpc);
int ds_cont_tgt_query_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				 void *priv);
void ds_cont_tgt_epoch_discard_handler(crt_rpc_t *rpc);
int ds_cont_tgt_epoch_discard_aggregator(crt_rpc_t *source, crt_rpc_t *result,
					 void *priv);
void ds_cont_tgt_epoch_aggregate_handler(crt_rpc_t *rpc);
int ds_cont_tgt_epoch_aggregate_aggregator(crt_rpc_t *source, crt_rpc_t *result,
					   void *priv);
int ds_cont_child_cache_create(struct daos_lru_cache **cache);
void ds_cont_child_cache_destroy(struct daos_lru_cache *cache);
int ds_cont_hdl_hash_create(struct d_hash_table *hash);
void ds_cont_hdl_hash_destroy(struct d_hash_table *hash);
void ds_cont_oid_alloc_handler(crt_rpc_t *rpc);

int ds_cont_lookup_create(const uuid_t uuid, void *arg,
			  struct ds_cont **cont_p);
struct ds_cont *ds_cont_lookup(const uuid_t uuid);
void ds_cont_put(struct ds_cont *cont);
int ds_cont_cache_init(void);
void ds_cont_cache_fini(void);

int ds_cont_tgt_open(uuid_t pool_uuid, uuid_t cont_hdl_uuid,
		     uuid_t cont_uuid, uint64_t capas);
/**
 * oid_iv.c
 */
int ds_oid_iv_init(void);
int ds_oid_iv_fini(void);
int oid_iv_reserve(void *ns, uuid_t poh_uuid, uuid_t co_uuid, uuid_t coh_uuid,
		   uint64_t num_oids, d_sg_list_t *value);

/* container_iv.c */
int ds_cont_iv_init(void);
int ds_cont_iv_fini(void);
int cont_iv_capability_update(void *ns, uuid_t cont_hdl_uuid, uuid_t cont_uuid,
			      uint64_t capas);
#endif /* __CONTAINER_SRV_INTERNAL_H__ */
