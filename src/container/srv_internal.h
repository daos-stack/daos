/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * ds_cont: Container Server Internal Declarations
 */

#ifndef __CONTAINER_SRV_INTERNAL_H__
#define __CONTAINER_SRV_INTERNAL_H__

#include <daos/lru.h>
#include <daos_security.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/rdb.h>
#include <daos_srv/rsvc.h>
#include <daos_srv/container.h>
#include <gurt/telemetry_common.h>

#include "srv_layout.h"

/* To avoid including srv_layout.h for everybody. */
struct container_hdl;

/* To avoid including daos_srv/pool.h for everybody. */
struct ds_pool;
struct ds_pool_hdl;

/* Container metrics */
struct cont_pool_metrics {
	struct d_tm_node_t	*open_total;
	struct d_tm_node_t	*close_total;
	struct d_tm_node_t	*query_total;
	struct d_tm_node_t	*create_total;
	struct d_tm_node_t	*destroy_total;
};

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

extern bool ec_agg_disabled;

struct ec_eph {
	d_rank_t	rank;
	daos_epoch_t	eph;
};

/* container EC aggregation epoch control descriptor, which is only on leader */
struct cont_ec_agg {
	uuid_t			ea_cont_uuid;
	daos_epoch_t		ea_current_eph;
	struct ec_eph		*ea_server_ephs;
	d_list_t		ea_list;
	int			ea_servers_num;
	uint32_t		ea_deleted:1;
};

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
	rdb_path_t		cs_uuids;	/* container UUIDs KVS */
	rdb_path_t		cs_conts;	/* container KVS */
	rdb_path_t		cs_hdls;	/* container handle KVS */
	struct ds_pool	       *cs_pool;

	/* Manage the EC aggregation epoch */
	struct sched_request   *cs_ec_leader_ephs_req;
	d_list_t		cs_ec_agg_list; /* link cont_ec_agg */
};

/* Container descriptor */
struct cont {
	uuid_t			c_uuid;
	struct cont_svc	       *c_svc;
	rdb_path_t		c_prop;		/* container property KVS */
	rdb_path_t		c_snaps;	/* snapshot KVS */
	rdb_path_t		c_user;		/* user attribute KVS */
	rdb_path_t		c_hdls;		/* handle index KVS */
	rdb_path_t		c_oit_oids;	/* snapshot oit oids */
};

/* OID range for allocator */
struct oid_iv_range {
	uint64_t	oid;
	daos_size_t	num_oids;
};

/* Container IV structure */
struct cont_iv_snapshot {
	uint64_t snap_cnt;
	uint64_t snaps[0];
};

struct cont_iv_capa {
	uint64_t	flags;
	uint64_t	sec_capas;
	/* the pool map_ver of updating DAOS_PROP_CO_STATUS property */
	uint32_t	status_pm_ver;
};

/* flattened container properties */
struct cont_iv_prop {
	char		cip_label[DAOS_PROP_MAX_LABEL_BUF_LEN];
	char		cip_owner[DAOS_ACL_MAX_PRINCIPAL_BUF_LEN];
	char		cip_owner_grp[DAOS_ACL_MAX_PRINCIPAL_BUF_LEN];
	uint64_t	cip_layout_type;
	uint64_t	cip_layout_ver;
	uint64_t	cip_csum;
	uint64_t	cip_csum_chunk_size;
	uint64_t	cip_csum_server_verify;
	uint64_t	cip_scrubbing_disabled;
	uint64_t	cip_dedup;
	uint64_t	cip_dedup_size;
	uint64_t	cip_alloced_oid;
	uint64_t	cip_redun_fac;
	uint64_t	cip_redun_lvl;
	uint64_t	cip_snap_max;
	uint64_t	cip_compress;
	uint64_t	cip_encrypt;
	uint64_t	cip_ec_cell_sz;
	uint32_t	cip_ec_pda;
	uint32_t	cip_rp_pda;
	uint32_t	cip_perf_domain;
	uint32_t	cip_global_version;
	uint32_t	cip_obj_version;
	uint64_t	cip_valid_bits;
	struct daos_prop_co_roots	cip_roots;
	struct daos_co_status		cip_co_status;
	/* MUST be the last member */
	struct daos_acl			cip_acl;
};

struct cont_iv_agg_eph {
	daos_epoch_t	eph;
	d_rank_t	rank;
};

struct cont_iv_entry {
	uuid_t	cont_uuid;
	union {
		struct cont_iv_snapshot iv_snap;
		struct cont_iv_capa	iv_capa;
		struct cont_iv_prop	iv_prop;
		struct cont_iv_agg_eph	iv_agg_eph;
	};
};

struct cont_iv_key {
	/* SNAP/PROP_IV the key is the container uuid.
	 * CAPA the key is the container hdl uuid.
	 */
	uuid_t		cont_uuid;
	/* IV class id, to differentiate SNAP/CAPA/PROP IV */
	uint32_t	class_id;
	uint32_t	entry_size;
};

/* srv_container.c */
void ds_cont_op_handler_v7(crt_rpc_t *rpc);
void ds_cont_op_handler_v6(crt_rpc_t *rpc);
void ds_cont_set_prop_handler(crt_rpc_t *rpc);
int ds_cont_bcast_create(crt_context_t ctx, struct cont_svc *svc,
			 crt_opcode_t opcode, crt_rpc_t **rpc);
int ds_cont_oid_fetch_add(uuid_t poh_uuid, uuid_t co_uuid, uint64_t num_oids, uint64_t *oid);
int cont_svc_lookup_leader(uuid_t pool_uuid, uint64_t id,
			   struct cont_svc **svcp, struct rsvc_hint *hint);
int cont_lookup(struct rdb_tx *tx, const struct cont_svc *svc,
		const uuid_t uuid, struct cont **cont);
void cont_put(struct cont *cont);
void cont_svc_put_leader(struct cont_svc *svc);
int ds_cont_prop_set(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		     struct cont *cont, struct container_hdl *hdl,
		     crt_rpc_t *rpc);
int ds_cont_acl_update(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		       struct cont *cont, struct container_hdl *hdl,
		       crt_rpc_t *rpc);
int ds_cont_acl_delete(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		       struct cont *cont, struct container_hdl *hdl,
		       crt_rpc_t *rpc);
int ds_cont_get_prop(uuid_t pool_uuid, uuid_t cont_uuid,
		     daos_prop_t **prop_out);
int ds_cont_leader_update_agg_eph(uuid_t pool_uuid, uuid_t cont_uuid,
				  d_rank_t rank, daos_epoch_t eph);

/* srv_epoch.c */
int ds_cont_snap_create(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
			struct cont *cont, struct container_hdl *hdl,
			crt_rpc_t *rpc);
int ds_cont_epoch_aggregate(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
			    struct cont *cont, struct container_hdl *hdl,
			    crt_rpc_t *rpc);
int ds_cont_snap_list(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		      struct cont *cont, struct container_hdl *hdl,
		      crt_rpc_t *rpc);
int ds_cont_snap_destroy(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
			 struct cont *cont, struct container_hdl *hdl,
			 crt_rpc_t *rpc);
int ds_cont_get_snapshots(uuid_t pool_uuid, uuid_t cont_uuid, daos_epoch_t **snapshots,
			  int *snap_count);
void ds_cont_update_snap_iv(struct cont_svc *svc, uuid_t cont_uuid);
int ds_cont_snap_oit_oid_get(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
			     struct cont *cont, struct container_hdl *hdl, crt_rpc_t *rpc);
int ds_cont_snap_oit_create(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
			    struct cont *cont, struct container_hdl *hdl, crt_rpc_t *rpc);
int ds_cont_snap_oit_destroy(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
			    struct cont *cont, struct container_hdl *hdl, crt_rpc_t *rpc);

/* srv_target.c */
int ds_cont_tgt_destroy(uuid_t pool_uuid, uuid_t cont_uuid);
void ds_cont_tgt_destroy_handler(crt_rpc_t *rpc);
int ds_cont_tgt_destroy_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				   void *priv);
void ds_cont_tgt_close_handler(crt_rpc_t *rpc);
int ds_cont_tgt_close_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				 void *priv);
void ds_cont_tgt_query_handler(crt_rpc_t *rpc);
int ds_cont_tgt_query_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				 void *priv);
void ds_cont_tgt_epoch_aggregate_handler(crt_rpc_t *rpc);
int ds_cont_tgt_epoch_aggregate_aggregator(crt_rpc_t *source, crt_rpc_t *result,
					   void *priv);
void ds_cont_tgt_snapshot_notify_handler(crt_rpc_t *rpc);
int ds_cont_tgt_snapshot_notify_aggregator(crt_rpc_t *source, crt_rpc_t *result,
					   void *priv);
int ds_cont_child_cache_create(struct daos_lru_cache **cache);
void ds_cont_child_cache_destroy(struct daos_lru_cache *cache);
int ds_cont_hdl_hash_create(struct d_hash_table *hash);
void ds_cont_hdl_hash_destroy(struct d_hash_table *hash);
void ds_cont_oid_alloc_handler(crt_rpc_t *rpc);
int ds_cont_tgt_open(uuid_t pool_uuid, uuid_t cont_hdl_uuid,
		     uuid_t cont_uuid, uint64_t flags, uint64_t sec_capas,
		     uint32_t status_pm_ver);
int ds_cont_tgt_snapshots_update(uuid_t pool_uuid, uuid_t cont_uuid,
				 uint64_t *snapshots, int snap_count);
int ds_cont_tgt_snapshots_refresh(uuid_t pool_uuid, uuid_t cont_uuid);
int ds_cont_tgt_close(uuid_t cont_hdl_uuid);
int ds_cont_tgt_refresh_agg_eph(uuid_t pool_uuid, uuid_t cont_uuid,
				daos_epoch_t eph);
int ds_cont_tgt_prop_update(uuid_t pool_uuid, uuid_t cont_uuid, daos_prop_t *prop);

/* oid_iv.c */
int ds_oid_iv_init(void);
int ds_oid_iv_fini(void);
int oid_iv_reserve(void *ns, uuid_t poh_uuid, uuid_t co_uuid, uint64_t num_oids,
		   d_sg_list_t *value);
int oid_iv_invalidate(void *ns, uuid_t pool_uuid, uuid_t cont_uuid);

/* container_iv.c */
int ds_cont_iv_init(void);
int ds_cont_iv_fini(void);
int cont_iv_capability_update(void *ns, uuid_t cont_hdl_uuid, uuid_t cont_uuid,
			      uint64_t flags, uint64_t sec_capas,
			      uint32_t pm_ver);
int cont_iv_capability_invalidate(void *ns, uuid_t cont_hdl_uuid,
				  int sync_mode);
int cont_iv_prop_fetch(uuid_t pool_uuid, uuid_t cont_uuid,
		       daos_prop_t *cont_prop);
int cont_iv_prop_update(void *ns, uuid_t cont_uuid, daos_prop_t *prop, bool sync);
int cont_iv_snapshots_refresh(void *ns, uuid_t cont_uuid);
int cont_iv_snapshots_update(void *ns, uuid_t cont_uuid,
			     uint64_t *snapshots, int snap_count);
int cont_iv_ec_agg_eph_update(void *ns, uuid_t cont_uuid, daos_epoch_t eph);
int cont_iv_ec_agg_eph_refresh(void *ns, uuid_t cont_uuid, daos_epoch_t eph);
int cont_iv_entry_delete(void *ns, uuid_t pool_uuid, uuid_t cont_uuid);

/* srv_metrics.c*/
void *ds_cont_metrics_alloc(const char *path, int tgt_id);
void ds_cont_metrics_free(void *data);
int ds_cont_metrics_count(void);

int cont_child_gather_oids(struct ds_cont_child *cont, uuid_t coh_uuid,
			   daos_epoch_t epoch, daos_obj_id_t oit_oid);

int ds_cont_hdl_rdb_lookup(uuid_t pool_uuid, uuid_t cont_hdl_uuid,
			   struct container_hdl *chdl);
#endif /* __CONTAINER_SRV_INTERNAL_H__ */
