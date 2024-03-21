/*
 * (C) Copyright 2016-2024 Intel Corporation.
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

#include <endian.h>

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
#include <daos_srv/rdb.h>

/* Pool service (opaque) */
struct ds_pool_svc;

/**
 * Each individual object layout format, like oid layout, dkey to group,
 * dkey to EC group start.
 */
#define DS_POOL_OBJ_VERSION		1

/* age of an entry in svc_ops KVS before it may be evicted */
#define DEFAULT_SVC_OPS_ENTRY_AGE_SEC_MAX 300ULL

/*
 * Pool object
 *
 * Caches per-pool information, such as the pool map.
 */
struct ds_pool {
	struct daos_llink	sp_entry;
	uuid_t			sp_uuid;	/* pool UUID */
	d_list_t		sp_hdls;
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
	/* Size threshold to store data on backend bdev */
	uint32_t		sp_data_thresh;
	ABT_mutex		sp_mutex;
	ABT_cond		sp_fetch_hdls_cond;
	ABT_cond		sp_fetch_hdls_done_cond;
	struct ds_iv_ns		*sp_iv_ns;
	uint32_t		*sp_states;	/* pool child state array */

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
	uint32_t sp_stopping : 1, sp_fetch_hdls : 1, sp_disable_rebuild : 1, sp_need_discard : 1;

	/* pool_uuid + map version + leader term + rebuild generation define a
	 * rebuild job.
	 */
	uint32_t		sp_rebuild_gen;

	int			sp_rebuilding;

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
	uint32_t		 sp_reint_mode;
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
	d_list_t		sph_pool_entry;
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

enum pool_child_state {
	POOL_CHILD_NEW	= 0,
	POOL_CHILD_STARTING,
	POOL_CHILD_STARTED,
	POOL_CHILD_STOPPING,
};

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

	uint64_t	spc_discard_done:1,
			spc_no_storage:1; /* The pool shard has no storage. */

	uint32_t	spc_reint_mode;
	uint32_t	*spc_state;	/* Pointer to ds_pool->sp_states[i] */
	/**
	 * Per-pool per-module metrics, see ${modname}_pool_metrics for the
	 * actual structure. Initialized only for modules that specified a
	 * set of handlers via dss_module::sm_metrics handlers and reported
	 * DAOS_TGT_TAG.
	 */
	void			*spc_metrics[DAOS_NR_MODULE];
};

struct ds_pool_svc_op_key {
	uint64_t ok_client_time;
	uuid_t   ok_client_id;
	/* TODO: add a (cart) opcode to the key? */
};

struct ds_pool_svc_op_val {
	int  ov_rc;
	char ov_resvd[60];
};

/* encode metadata RPC operation key: HLC time first, in network order, for keys sorted by time.
 * allocates the byte-stream, caller must free with D_FREE().
 */
static inline int
ds_pool_svc_op_key_encode(struct ds_pool_svc_op_key *in, d_iov_t *enc_out)
{
	struct ds_pool_svc_op_key *out;

	/* encoding is simple for this type, just another struct ds_pool_svc_op_key */
	D_ALLOC_PTR(out);
	if (out == NULL)
		return -DER_NOMEM;

	out->ok_client_time = htobe64(in->ok_client_time);
	uuid_copy(out->ok_client_id, in->ok_client_id);
	d_iov_set(enc_out, (void *)out, sizeof(*out));

	return 0;
}

static inline int
ds_pool_svc_op_key_decode(d_iov_t *enc_in, struct ds_pool_svc_op_key *out)
{
	struct ds_pool_svc_op_key *in = enc_in->iov_buf;

	if (enc_in->iov_len < sizeof(struct ds_pool_svc_op_key))
		return -DER_INVAL;

	out->ok_client_time = be64toh(in->ok_client_time);
	uuid_copy(out->ok_client_id, in->ok_client_id);

	return 0;
}

struct rdb_tx;
int
ds_pool_svc_ops_lookup(struct rdb_tx *tx, void *pool_svc, uuid_t pool_uuid, uuid_t *cli_uuidp,
		       uint64_t cli_time, bool *is_dup, struct ds_pool_svc_op_val *valp);
int
ds_pool_svc_ops_save(struct rdb_tx *tx, void *pool_svc, uuid_t pool_uuid, uuid_t *cli_uuidp,
		     uint64_t cli_time, bool dup_op, int rc_in, struct ds_pool_svc_op_val *op_valp);

/* Find ds_pool_child in cache, hold one reference */
struct ds_pool_child *ds_pool_child_lookup(const uuid_t uuid);
/* Put the reference held by ds_pool_child_lookup() */
void ds_pool_child_put(struct ds_pool_child *child);
/* Start ds_pool child */
int ds_pool_child_start(uuid_t pool_uuid, bool recreate);
/* Stop ds_pool_child */
int ds_pool_child_stop(uuid_t pool_uuid);
/* Query pool child state */
uint32_t ds_pool_child_state(uuid_t pool_uuid, uint32_t tgt_id);

int ds_pool_bcast_create(crt_context_t ctx, struct ds_pool *pool,
			 enum daos_module_id module, crt_opcode_t opcode,
			 uint32_t version, crt_rpc_t **rpc, crt_bulk_t bulk_hdl,
			 d_rank_list_t *excluded_list, void *priv);

int ds_pool_map_buf_get(uuid_t uuid, d_iov_t *iov, uint32_t *map_ver);

int ds_pool_tgt_exclude_out(uuid_t pool_uuid, struct pool_target_id_list *list);
int ds_pool_tgt_exclude(uuid_t pool_uuid, struct pool_target_id_list *list);
int ds_pool_tgt_add_in(uuid_t pool_uuid, struct pool_target_id_list *list);

int ds_pool_tgt_revert_rebuild(uuid_t pool_uuid, struct pool_target_id_list *list);
int ds_pool_tgt_finish_rebuild(uuid_t pool_uuid, struct pool_target_id_list *list);
int ds_pool_tgt_map_update(struct ds_pool *pool, struct pool_buf *buf,
			   unsigned int map_version);

int ds_pool_chk_post(uuid_t uuid);
int ds_pool_start_with_svc(uuid_t uuid);
int ds_pool_start(uuid_t uuid);
void ds_pool_stop(uuid_t uuid);
int dsc_pool_svc_extend(uuid_t pool_uuid, d_rank_list_t *svc_ranks, uint64_t deadline, int ntargets,
			const d_rank_list_t *rank_list, int ndomains, const uint32_t *domains);
int dsc_pool_svc_update_target_state(uuid_t pool_uuid, d_rank_list_t *ranks, uint64_t deadline,
				     struct pool_target_addr_list *target_list,
				     pool_comp_state_t state);

int
     ds_pool_svc_dist_create(const uuid_t pool_uuid, int ntargets, const char *group,
			     d_rank_list_t *target_addrs, int ndomains, uint32_t *domains,
			     daos_prop_t *prop, d_rank_list_t **svc_addrs);
int ds_pool_svc_stop(uuid_t pool_uuid);
int ds_pool_svc_rf_to_nreplicas(int svc_rf);
int ds_pool_svc_rf_from_nreplicas(int nreplicas);

int dsc_pool_svc_get_prop(uuid_t pool_uuid, d_rank_list_t *ranks, uint64_t deadline,
			  daos_prop_t *prop);
int dsc_pool_svc_set_prop(uuid_t pool_uuid, d_rank_list_t *ranks, uint64_t deadline,
			  daos_prop_t *prop);
int dsc_pool_svc_update_acl(uuid_t pool_uuid, d_rank_list_t *ranks, uint64_t deadline,
			    struct daos_acl *acl);
int dsc_pool_svc_delete_acl(uuid_t pool_uuid, d_rank_list_t *ranks, uint64_t deadline,
			    enum daos_acl_principal_type principal_type,
			    const char *principal_name);

int dsc_pool_svc_query(uuid_t pool_uuid, d_rank_list_t *ps_ranks, uint64_t deadline,
		       d_rank_list_t **ranks, daos_pool_info_t *pool_info,
		       uint32_t *pool_layout_ver, uint32_t *upgrade_layout_ver);
int dsc_pool_svc_query_target(uuid_t pool_uuid, d_rank_list_t *ps_ranks, uint64_t deadline,
			      d_rank_t rank, uint32_t tgt_idx, daos_target_info_t *ti);

int ds_pool_prop_fetch(struct ds_pool *pool, unsigned int bit,
		       daos_prop_t **prop_out);
int dsc_pool_svc_upgrade(uuid_t pool_uuid, d_rank_list_t *ranks, uint64_t deadline);
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
int ds_pool_svc_query_map_dist(uuid_t uuid, uint32_t *version, bool *idle);

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

int dsc_pool_svc_check_evict(uuid_t pool_uuid, d_rank_list_t *ranks, uint64_t deadline,
			     uuid_t *handles, size_t n_handles, uint32_t destroy, uint32_t force,
			     char *machine, uint32_t *count);

int ds_pool_target_status_check(struct ds_pool *pool, uint32_t id,
				uint8_t matched_status, struct pool_target **p_tgt);
int ds_pool_mark_connectable(struct ds_pool_svc *ds_svc);
int ds_pool_svc_load_map(struct ds_pool_svc *ds_svc, struct pool_map **map);
int ds_pool_svc_flush_map(struct ds_pool_svc *ds_svc, struct pool_map *map);
int ds_pool_svc_schedule_reconf(struct ds_pool_svc *svc);
int ds_pool_svc_update_label(struct ds_pool_svc *ds_svc, const char *label);
int ds_pool_svc_evict_all(struct ds_pool_svc *ds_svc);
struct ds_pool *ds_pool_svc2pool(struct ds_pool_svc *ds_svc);
struct cont_svc *ds_pool_ps2cs(struct ds_pool_svc *ds_svc);
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

struct dss_coll_args;
struct dss_coll_ops;

int
ds_pool_thread_collective_reduce(uuid_t pool_uuid, uint32_t ex_status, struct dss_coll_ops *coll_ops,
				 struct dss_coll_args *coll_args, uint32_t flags);
int
ds_pool_task_collective_reduce(uuid_t pool_uuid, uint32_t ex_status, struct dss_coll_ops *coll_ops,
			       struct dss_coll_args *coll_args, uint32_t flags);
int
ds_pool_thread_collective(uuid_t pool_uuid, uint32_t ex_status, int (*coll_func)(void *),
			  void *arg, uint32_t flags);
int
ds_pool_task_collective(uuid_t pool_uuid, uint32_t ex_status, int (*coll_func)(void *),
			void *arg, uint32_t flags);
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

/**
 * Pool service replica clue
 *
 * Pool service replica info gathered when glancing at a pool.
 */
struct ds_pool_svc_clue {
	struct rdb_clue	psc_db_clue;
	uint32_t	psc_map_version;	/**< if 0, empty DB replica */
};

/** Pool parent directory */
enum ds_pool_dir {
	DS_POOL_DIR_NORMAL,
	DS_POOL_DIR_NEWBORN,
	DS_POOL_DIR_ZOMBIE
};

enum ds_pool_tgt_status {
	DS_POOL_TGT_NONEXIST,
	DS_POOL_TGT_EMPTY,
	DS_POOL_TGT_NORMAL
};

int
ds_start_chkpt_ult(struct ds_pool_child *child);
void
    ds_stop_chkpt_ult(struct ds_pool_child *child);
int ds_pool_lookup_hdl_cred(struct rdb_tx *tx, uuid_t pool_uuid, uuid_t pool_hdl_uuid,
			    d_iov_t *cred);

/**
 * Pool clue
 *
 * Pool shard and service replica (if applicable) info gathered when glancing
 * at a pool. The pc_uuid, pc_dir, and pc_rc fields are always valid; the
 * pc_svc_clue field is valid only if pc_rc is positive value.
 */
struct ds_pool_clue {
	uuid_t				 pc_uuid;
	d_rank_t			 pc_rank;
	enum ds_pool_dir		 pc_dir;
	int				 pc_rc;
	int				 pc_tgt_nr;
	uint32_t			 pc_label_len;
	/*
	 * DAOS check phase for current pool shard. Different pool shards may claim different
	 * check phase because some shards may has ever missed the RPC for check phase update.
	 */
	uint32_t			 pc_phase;
	struct ds_pool_svc_clue		*pc_svc_clue;
	char				*pc_label;
	uint32_t			*pc_tgt_status;
};

void ds_pool_clue_init(uuid_t uuid, enum ds_pool_dir dir, struct ds_pool_clue *clue);
void ds_pool_clue_fini(struct ds_pool_clue *clue);

/** Array of ds_pool_clue objects */
struct ds_pool_clues {
	struct ds_pool_clue    *pcs_array;
	int			pcs_len;
	int			pcs_cap;
};

/**
 * If this callback returns 0, the pool with \a uuid will be glanced at;
 * otherwise, the pool with \a uuid will be skipped.
 */
typedef int (*ds_pool_clues_init_filter_t)(uuid_t uuid, void *arg, int *phase);

int ds_pool_clues_init(ds_pool_clues_init_filter_t filter, void *filter_arg,
		       struct ds_pool_clues *clues_out);
void ds_pool_clues_fini(struct ds_pool_clues *clues);
void ds_pool_clues_print(struct ds_pool_clues *clues);

int ds_pool_check_svc_clues(struct ds_pool_clues *clues, int *advice_out);

int ds_pool_svc_lookup_leader(uuid_t uuid, struct ds_pool_svc **ds_svcp, struct rsvc_hint *hint);

void ds_pool_svc_put_leader(struct ds_pool_svc *ds_svc);

#endif /* __DAOS_SRV_POOL_H__ */
