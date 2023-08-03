/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * ds_pool: Pool Server API
 */

#ifndef __DAOS_SRV_POOL_H__
#define __DAOS_SRV_POOL_H__

#include <abt.h>
#include <daos/common.h>
#include <daos/lru.h>
#include <daos/pool_map.h>
#include <daos/rpc.h>
#include <daos/placement.h>
#include <daos_srv/vos_types.h>
#include <daos_pool.h>
#include <daos_security.h>
#include <gurt/telemetry_common.h>
#include <daos_srv/policy.h>

/**
 * Each individual object layout format, like oid layout, dkey to group,
 * dkey to EC group start.
 */
#define DS_POOL_OBJ_VERSION		1

/*
 * Pool object
 *
 * Caches per-pool information, such as the pool map.
 */
struct ds_pool {
	struct daos_llink	sp_entry;
	uuid_t			sp_uuid;	/* pool UUID */
	ABT_rwlock		sp_lock;
	struct pool_map		*sp_map;
	uint32_t		sp_map_version;	/* temporary */
	uint32_t		sp_ec_cell_sz;
	uint64_t		sp_reclaim;
	uint64_t		sp_redun_fac;
	/* Performance Domain Affinity Level of EC object. */
	uint32_t		sp_ec_pda;
	/* Performance Domain Affinity Level of replicated object */
	uint32_t		sp_rp_pda;
	/* Performance Domain level */
	uint32_t		sp_perf_domain;
	uint32_t		sp_global_version;
	uint32_t		sp_space_rb;
	crt_group_t	       *sp_group;
	struct policy_desc_t	sp_policy_desc;	/* tiering policy descriptor */
	ABT_mutex		sp_mutex;
	ABT_cond		sp_fetch_hdls_cond;
	ABT_cond		sp_fetch_hdls_done_cond;
	struct ds_iv_ns		*sp_iv_ns;

	/* structure related to EC aggregate epoch query */
	d_list_t		sp_ec_ephs_list;
	struct sched_request	*sp_ec_ephs_req;

	uint32_t		sp_dtx_resync_version;
	/* Special pool/container handle uuid, which are
	 * created on the pool leader step up, and propagated
	 * to all servers by IV. Then they will be used by server
	 * to access the data on other servers.
	 */
	uuid_t			sp_srv_cont_hdl;
	uuid_t			sp_srv_pool_hdl;
	uint32_t sp_stopping : 1, sp_fetch_hdls : 1, sp_disable_rebuild : 1, sp_need_discard : 1,
	    sp_checkpoint_props_changed : 1;

	/* pool_uuid + map version + leader term + rebuild generation define a
	 * rebuild job.
	 */
	uint32_t		sp_rebuild_gen;

	int			sp_reintegrating;

	int			sp_discard_status;
	/** path to ephemeral metrics */
	char			sp_path[D_TM_MAX_NAME_LEN];

	/**
	 * Per-pool per-module metrics, see ${modname}_pool_metrics for the
	 * actual structure. Initialized only for modules that specified a
	 * set of handlers via dss_module::sm_metrics handlers and reported
	 * DAOS_SYS_TAG.
	 */
	void			*sp_metrics[DAOS_NR_MODULE];
	/** checksum scrubbing properties */
	uint64_t		sp_scrub_mode;
	uint64_t		sp_scrub_freq_sec;
	uint64_t		sp_scrub_thresh;
	/** WAL checkpointing properties */
	uint32_t                 sp_checkpoint_mode;
	uint32_t                 sp_checkpoint_freq;
	uint32_t                 sp_checkpoint_thresh;
};

int ds_pool_lookup(const uuid_t uuid, struct ds_pool **pool);
void ds_pool_put(struct ds_pool *pool);
void ds_pool_get(struct ds_pool *pool);

/*
 * Pool handle object
 *
 * Stores per-handle information, such as the capabilities. References the pool
 * object.
 */
struct ds_pool_hdl {
	d_list_t		sph_entry;
	uuid_t			sph_uuid;	/* of the pool handle */
	uint64_t		sph_flags;	/* user-provided flags */
	uint64_t		sph_sec_capas;	/* access capabilities */
	uint32_t		sph_global_ver; /* pool global version */
	uint32_t		sph_obj_ver;	/* pool obj layout version */
	struct ds_pool	       *sph_pool;
	int			sph_ref;
	d_iov_t			sph_cred;
};

struct ds_pool_hdl *ds_pool_hdl_lookup(const uuid_t uuid);
void ds_pool_hdl_put(struct ds_pool_hdl *hdl);

/*
 * Per-thread pool object
 *
 * Stores per-thread, per-pool information, such as the vos pool handle. And,
 * caches per-pool information, such as the pool map version, so that DAOS
 * object I/Os do not need to access global, parent ds_pool objects.
 */
struct ds_pool_child {
	d_list_t		spc_list;
	daos_handle_t		spc_hdl;	/* vos_pool handle */
	struct ds_pool		*spc_pool;
	uuid_t			spc_uuid;	/* pool UUID */
	struct sched_request	*spc_gc_req;	/* Track GC ULT */
	struct sched_request	*spc_flush_req;	/* Dedicated VEA flush ULT */
	struct sched_request	*spc_scrubbing_req; /* Track scrubbing ULT*/
	struct sched_request    *spc_chkpt_req;     /* Track checkpointing ULT*/
	d_list_t		spc_cont_list;

	/* The current maxim rebuild epoch, (0 if there is no rebuild), so
	 * vos aggregation can not cross this epoch during rebuild to avoid
	 * interfering rebuild process.
	 */
	uint64_t	spc_rebuild_fence;

	/* The HLC when current rebuild ends, which will be used to compare
	 * with the aggregation full scan start HLC to know whether the
	 * aggregation needs to be restarted from 0. */
	uint64_t	spc_rebuild_end_hlc;
	uint32_t	spc_map_version;
	int		spc_ref;
	ABT_eventual	spc_ref_eventual;

	uint64_t	spc_discard_done:1;
	/**
	 * Per-pool per-module metrics, see ${modname}_pool_metrics for the
	 * actual structure. Initialized only for modules that specified a
	 * set of handlers via dss_module::sm_metrics handlers and reported
	 * DAOS_TGT_TAG.
	 */
	void			*spc_metrics[DAOS_NR_MODULE];
};

struct ds_pool_child *ds_pool_child_lookup(const uuid_t uuid);
struct ds_pool_child *ds_pool_child_get(struct ds_pool_child *child);
void ds_pool_child_put(struct ds_pool_child *child);

int ds_pool_bcast_create(crt_context_t ctx, struct ds_pool *pool,
			 enum daos_module_id module, crt_opcode_t opcode,
			 uint32_t version, crt_rpc_t **rpc, crt_bulk_t bulk_hdl,
			 d_rank_list_t *excluded_list);

int ds_pool_map_buf_get(uuid_t uuid, d_iov_t *iov, uint32_t *map_ver);

int ds_pool_tgt_exclude_out(uuid_t pool_uuid, struct pool_target_id_list *list);
int ds_pool_tgt_exclude(uuid_t pool_uuid, struct pool_target_id_list *list);
int ds_pool_tgt_add_in(uuid_t pool_uuid, struct pool_target_id_list *list);

int ds_pool_tgt_map_update(struct ds_pool *pool, struct pool_buf *buf,
			   unsigned int map_version);

int ds_pool_start(uuid_t uuid);
void ds_pool_stop(uuid_t uuid);
int ds_pool_extend(uuid_t pool_uuid, int ntargets, const d_rank_list_t *rank_list, int ndomains,
		   const uint32_t *domains, d_rank_list_t *svc_ranks);
int ds_pool_target_update_state(uuid_t pool_uuid, d_rank_list_t *ranks,
				struct pool_target_addr_list *target_list,
				pool_comp_state_t state);

int ds_pool_svc_dist_create(const uuid_t pool_uuid, int ntargets, const char *group,
			    const d_rank_list_t *target_addrs, int ndomains,
			    const uint32_t *domains, daos_prop_t *prop, d_rank_list_t **svc_addrs);
int ds_pool_svc_stop(uuid_t pool_uuid);
int ds_pool_svc_rf_to_nreplicas(int svc_rf);
int ds_pool_svc_rf_from_nreplicas(int nreplicas);

int ds_pool_svc_get_prop(uuid_t pool_uuid, d_rank_list_t *ranks,
			 daos_prop_t *prop);
int ds_pool_svc_set_prop(uuid_t pool_uuid, d_rank_list_t *ranks,
			 daos_prop_t *prop);
int ds_pool_svc_update_acl(uuid_t pool_uuid, d_rank_list_t *ranks,
			   struct daos_acl *acl);
int ds_pool_svc_delete_acl(uuid_t pool_uuid, d_rank_list_t *ranks,
			   enum daos_acl_principal_type principal_type,
			   const char *principal_name);

int dsc_pool_svc_query(uuid_t pool_uuid, d_rank_list_t *ps_ranks, uint64_t deadline,
		       d_rank_list_t **ranks, daos_pool_info_t *pool_info,
		       uint32_t *pool_layout_ver, uint32_t *upgrade_layout_ver);
int ds_pool_svc_query_target(uuid_t pool_uuid, d_rank_list_t *ps_ranks, d_rank_t rank,
			     uint32_t tgt_idx, daos_target_info_t *ti);

int ds_pool_prop_fetch(struct ds_pool *pool, unsigned int bit,
		       daos_prop_t **prop_out);
int ds_pool_svc_upgrade(uuid_t pool_uuid, d_rank_list_t *ranks);
int ds_pool_failed_add(uuid_t uuid, int rc);
void ds_pool_failed_remove(uuid_t uuid);
int ds_pool_failed_lookup(uuid_t uuid);

/*
 * Called by dmg on the pool service leader to list all pool handles of a pool.
 * Upon successful completion, "buf" returns an array of handle UUIDs if its
 * large enough, while "size" returns the size of all the handle UUIDs assuming
 * "buf" is large enough.
 */
int ds_pool_hdl_list(const uuid_t pool_uuid, uuid_t buf, size_t *size);

/*
 * Called by dmg on the pool service leader to evict one or all pool handles of
 * a pool. If "handle_uuid" is NULL, all pool handles of the pool are evicted.
 */
int ds_pool_hdl_evict(const uuid_t pool_uuid, const uuid_t handle_uuid);

struct cont_svc;
struct rsvc_hint;
int ds_pool_cont_svc_lookup_leader(uuid_t pool_uuid, struct cont_svc **svc,
				   struct rsvc_hint *hint);

void ds_pool_iv_ns_update(struct ds_pool *pool, unsigned int master_rank, uint64_t term);

int ds_pool_iv_map_update(struct ds_pool *pool, struct pool_buf *buf,
		       uint32_t map_ver);
int ds_pool_iv_prop_update(struct ds_pool *pool, daos_prop_t *prop);
int ds_pool_iv_prop_fetch(struct ds_pool *pool, daos_prop_t *prop);
int ds_pool_iv_svc_fetch(struct ds_pool *pool, d_rank_list_t **svc_p);

int ds_pool_iv_srv_hdl_fetch(struct ds_pool *pool, uuid_t *pool_hdl_uuid,
			     uuid_t *cont_hdl_uuid);

int ds_pool_svc_term_get(uuid_t uuid, uint64_t *term);
int ds_pool_svc_global_map_version_get(uuid_t uuid, uint32_t *global_ver);

int
ds_pool_child_map_refresh_sync(struct ds_pool_child *dpc);
int
ds_pool_child_map_refresh_async(struct ds_pool_child *dpc);

int
map_ranks_init(const struct pool_map *map, unsigned int status, d_rank_list_t *ranks);

void
map_ranks_fini(d_rank_list_t *ranks);

int ds_pool_get_ranks(const uuid_t pool_uuid, int status,
		      d_rank_list_t *ranks);
int ds_pool_get_tgt_idx_by_state(const uuid_t pool_uuid, unsigned int status, int **tgts,
				 unsigned int *tgts_cnt);
int ds_pool_get_failed_tgt_idx(const uuid_t pool_uuid, int **failed_tgts,
			       unsigned int *failed_tgts_cnt);
int ds_pool_svc_list_cont(uuid_t uuid, d_rank_list_t *ranks,
			  struct daos_pool_cont_info **containers,
			  uint64_t *ncontainers);

int ds_pool_svc_check_evict(uuid_t pool_uuid, d_rank_list_t *ranks,
			    uuid_t *handles, size_t n_handles,
			    uint32_t destroy, uint32_t force,
			    char *machine, uint32_t *count);

int ds_pool_target_status_check(struct ds_pool *pool, uint32_t id,
				uint8_t matched_status, struct pool_target **p_tgt);
void ds_pool_disable_exclude(void);
void ds_pool_enable_exclude(void);

extern bool ec_agg_disabled;

int dsc_pool_open(uuid_t pool_uuid, uuid_t pool_hdl_uuid,
		       unsigned int flags, const char *grp,
		       struct pool_map *map, d_rank_list_t *svc_list,
		       daos_handle_t *ph);
int dsc_pool_close(daos_handle_t ph);
int ds_pool_tgt_discard(uuid_t pool_uuid, uint64_t epoch);

int
ds_pool_mark_upgrade_completed(uuid_t pool_uuid, int ret);

/**
 * Verify if pool status satisfy Redundancy Factor requirement, by checking
 * pool map device status.
 */
static inline int
ds_pool_rf_verify(struct ds_pool *pool, uint32_t last_ver, uint32_t rlvl, uint32_t rf)
{
	int	rc = 0;

	ABT_rwlock_rdlock(pool->sp_lock);
	if (last_ver < pool_map_get_version(pool->sp_map))
		rc = pool_map_rf_verify(pool->sp_map, last_ver, rlvl, rf);
	ABT_rwlock_unlock(pool->sp_lock);

	return rc;
}

static inline uint32_t
ds_pool_get_version(struct ds_pool *pool)
{
	uint32_t	ver = 0;

	ABT_rwlock_rdlock(pool->sp_lock);
	if (pool->sp_map != NULL)
		ver = pool_map_get_version(pool->sp_map);
	ABT_rwlock_unlock(pool->sp_lock);

	return ver;
}

int
ds_start_chkpt_ult(struct ds_pool_child *child);
void
ds_stop_chkpt_ult(struct ds_pool_child *child);
struct rdb_tx;
int ds_pool_lookup_hdl_cred(struct rdb_tx *tx, uuid_t pool_uuid, uuid_t pool_hdl_uuid,
			    d_iov_t *cred);

#endif /* __DAOS_SRV_POOL_H__ */
