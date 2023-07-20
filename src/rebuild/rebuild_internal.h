/**
 * (C) Copyright 2017-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * rebuild: rebuild internal.h
 *
 */

#ifndef __REBUILD_INTERNAL_H__
#define __REBUILD_INTERNAL_H__

#include <stdint.h>
#include <abt.h>
#include <uuid/uuid.h>
#include <daos/rpc.h>
#include <daos/btree.h>
#include <daos/pool_map.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/rebuild.h>

/* Track the pool rebuild status on each target, which exists on
 * all server targets. Then each target will report its rebuild
 * status to the global pool tracker(see below) on the master node,
 * which is used to track the rebuild status globally.
 */
struct rebuild_tgt_pool_tracker {
	/** pin the pool during the rebuild */
	struct ds_pool		*rt_pool;
	struct sched_request	*rt_ult;

	/** the current version being rebuilt, only used by leader */
	uint32_t		rt_rebuild_ver;

	/* rebuild generation, will increase for each re-schedule */
	uint32_t		rt_rebuild_gen;

	/** the current rebuild operation */
	daos_rebuild_opc_t	rt_rebuild_op;

	/** rebuild pool/container hdl uuid */
	uuid_t			rt_poh_uuid;
	uuid_t			rt_coh_uuid;

	/* Link it to the rebuild_global tracker_list */
	d_list_t		rt_list;
	ABT_mutex		rt_lock;
	uuid_t			rt_pool_uuid;
	/* to be rebuilt tree */
	struct btr_root		rt_tobe_rb_root;
	daos_handle_t		rt_tobe_rb_root_hdl;
	/* already rebuilt tree, only used for initiator */
	struct btr_root		rt_rebuilt_root;
	daos_handle_t		rt_rebuilt_root_hdl;
	/* number of obj records in rebuilt tree */
	unsigned int		rt_rebuilt_obj_cnt;
	d_rank_list_t		*rt_svc_list;
	d_rank_t		rt_rank;
	int			rt_errno;
	int			rt_refcount;
	uint32_t		rt_tgts_num;
	uint64_t		rt_leader_term;
	/* Wait for other to release the rpt, so the target
	 * can be go ahead to finish the rebuild.
	 */
	ABT_cond		rt_fini_cond;

	/* # to-be-rebuilt objs */
	uint64_t		rt_reported_toberb_objs;
	/* reported # rebuilt objs */
	uint64_t		rt_reported_obj_cnt;
	uint64_t		rt_reported_rec_cnt;
	uint64_t		rt_reported_size;
	/* global stable epoch to use for rebuilding the data */
	uint64_t		rt_stable_epoch;

	/* Only used by reclaim job to discard those half-rebuild data */
	uint64_t		rt_reclaim_epoch;
	/* local rebuild epoch mainly to constrain the VOS aggregation
	 * to make sure aggregation will not cross the epoch
	 */
	uint64_t		rt_rebuild_fence;

	uint32_t		rt_leader_rank;

	/* Global dtx resync version */
	uint32_t		rt_global_dtx_resync_version;

	ABT_cond		rt_global_dtx_wait_cond;
	/* new layout version for upgrade rebuild */
	uint32_t		rt_new_layout_ver;

	unsigned int		rt_lead_puller_running:1,
				rt_abort:1,
				/* re-report #rebuilt cnt per master change */
				rt_re_report:1,
				rt_finishing:1,
				rt_scan_done:1,
				rt_global_scan_done:1,
				rt_global_done:1;
};

struct rebuild_server_status {
	d_rank_t	rank;
	uint32_t	dtx_resync_version;
	uint32_t	scan_done:1,
			pull_done:1;
};

/* Track the rebuild status globally */
struct rebuild_global_pool_tracker {
	/* rebuild status */
	struct daos_rebuild_status	rgt_status;

	struct sched_request	*rgt_ult;
	/* link to rebuild_global.rg_global_tracker_list */
	d_list_t	rgt_list;

	/* the pool uuid */
	uuid_t		rgt_pool_uuid;

	/** rebuild status for each server */
	struct rebuild_server_status *rgt_servers;

	/** the current version being rebuilt */
	uint32_t	rgt_rebuild_ver;

	/** global resync dtx version */
	uint32_t	rgt_dtx_resync_version;

	/** number of rgt_server_status */
	uint32_t	rgt_servers_number;

	uint32_t	rgt_rebuild_gen;

	/* The term of the current rebuild leader */
	uint64_t	rgt_leader_term;

	uint64_t	rgt_time_start;

	/* Stable epoch of the rebuild, the minimum epoch from
	 * all rebuilding targets
	 */
	uint64_t	rgt_stable_epoch;

	/* reclaim epoch of the rebuild, which is used to discard
	 * the half-rebuild data if rebuild fails
	 */
	uint64_t	rgt_reclaim_epoch;

	ABT_mutex	rgt_lock;
	/* The current rebuild is done on the leader */
	ABT_cond	rgt_done_cond;

	uint32_t	rgt_refcount;

	unsigned int	rgt_abort:1,
			rgt_init_scan:1;
};

/* Structure on raft replica nodes to serve completed rebuild status querying */
struct rebuild_status_completed {
	/* rebuild status */
	struct daos_rebuild_status	rsc_status;

	/* link to rebuild_global.rg_completed_list */
	d_list_t			rsc_list;

	/* the pool uuid */
	uuid_t				rsc_pool_uuid;
};

/* Structure on all targets to track all pool rebuilding */
struct rebuild_global {
	/* Link rebuild_tgt_pool_tracker on all targets.
	 * Only operated by stream 0, no need lock.
	 */
	d_list_t	rg_tgt_tracker_list;

	/* Linked rebuild_global_pool_tracker on the master node,
	 * empty on other nodes.
	 * Only operated by stream 0, no need lock.
	 */
	d_list_t	rg_global_tracker_list;

	/**
	 * Completed rebuild status list on raft replica nodes,
	 * empty on other nodes.
	 * Only operated by stream 0, no need lock.
	 */
	d_list_t	rg_completed_list;

	/* Rebuild task running list */
	d_list_t	rg_running_list;

	/* Rebuild task queued list, on which tasks to be scheduled
	 * are linked.
	 */
	d_list_t	rg_queue_list;

	ABT_mutex	rg_lock;
	ABT_cond	rg_stop_cond;
	/* how many pools is being rebuilt */
	unsigned int	rg_inflight;
	unsigned int	rg_rebuild_running:1,
			rg_abort:1;
};

/* Per target structure to track the rebuild status */
extern struct rebuild_global rebuild_gst;

struct rebuild_task {
	d_list_t			dst_list;
	uuid_t				dst_pool_uuid;
	struct pool_target_id_list	dst_tgts;
	daos_rebuild_opc_t		dst_rebuild_op;

	/* Epoch to use for reclaim job for discarding the data
	 * of half-rebuild/reintegrated job.
	 */
	daos_epoch_t			dst_reclaim_eph;
	uint64_t			dst_schedule_time;
	uint32_t			dst_map_ver;
	uint32_t			dst_new_layout_version;
	/* Once the task fail, it will use reclaim version to
	 * reclaim those half-rebuild/reintegrated job.
	 */
	uint32_t			dst_reclaim_ver;
};

/* Per pool structure in TLS to check pool rebuild status
 * per xstream.
 */
struct rebuild_pool_tls {
	uuid_t		rebuild_pool_uuid;
	daos_handle_t	rebuild_tree_hdl; /*hold objects being rebuilt */
	d_list_t	rebuild_pool_list;
	uint64_t	rebuild_pool_obj_count;
	uint64_t	rebuild_pool_reclaim_obj_count;
	unsigned int	rebuild_pool_ver;
	uint32_t	rebuild_pool_gen;
	uint64_t	rebuild_pool_leader_term;
	int		rebuild_pool_status;
	unsigned int	rebuild_pool_scanning:1,
			rebuild_pool_scan_done:1;
};

/* per thread structure to track rebuild status for all pools */
struct rebuild_tls {
	/* rebuild_pool_tls will link here */
	d_list_t	rebuild_pool_list;
};

struct rebuild_root {
	struct btr_root	btr_root;
	daos_handle_t	root_hdl;
	unsigned int	count;
};

struct rebuild_tgt_query_info {
	int scanning;
	int status;
	uint64_t obj_count;
	uint64_t tobe_obj_count;
	uint64_t rec_count;
	uint64_t size;
	bool rebuilding;
	ABT_mutex lock;
};

struct rebuild_iv {
	uuid_t		riv_pool_uuid;
	uint64_t	riv_toberb_obj_count;
	uint64_t	riv_obj_count;
	uint64_t	riv_rec_count;
	uint64_t	riv_size;
	uint64_t	riv_leader_term;
	uint64_t	riv_stable_epoch;
	uint32_t	riv_seconds;
	uint32_t	riv_dtx_resyc_version;
	uint32_t	riv_global_dtx_resyc_version;
	unsigned int	riv_rank;
	unsigned int	riv_master_rank;
	unsigned int	riv_ver;
	unsigned int	riv_rebuild_gen;
	uint32_t	riv_global_done:1,
			riv_global_scan_done:1,
			riv_scan_done:1,
			riv_pull_done:1,
			riv_sync:1;
	int		riv_status;

};

#define SCAN_YIELD_FREQ		4096
#define SCAN_OBJ_YIELD_CNT	128

extern struct dss_module_key rebuild_module_key;
static inline struct rebuild_tls *
rebuild_tls_get()
{
	return dss_module_key_get(dss_tls_get(), &rebuild_module_key);
}

void rpt_get(struct rebuild_tgt_pool_tracker *rpt);
void rpt_put(struct rebuild_tgt_pool_tracker *rpt);

struct rebuild_pool_tls *
rebuild_pool_tls_lookup(uuid_t pool_uuid, unsigned int ver, uint32_t gen);

struct pool_map *rebuild_pool_map_get(struct ds_pool *pool);
void rebuild_pool_map_put(struct pool_map *map);

void rebuild_tgt_scan_handler(crt_rpc_t *rpc);
int rebuild_tgt_scan_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				void *priv);
int rebuild_tgt_scan_pre_forward(crt_rpc_t *rpc, void *arg);

int rebuild_iv_fetch(void *ns, struct rebuild_iv *rebuild_iv);
int rebuild_iv_update(void *ns, struct rebuild_iv *rebuild_iv,
		      unsigned int shortcut, unsigned int sync_mode,
		      bool retry);
int rebuild_iv_ns_create(struct ds_pool *pool, uint32_t map_ver,
			 d_rank_list_t *exclude_tgts,
			 unsigned int master_rank);

int rebuild_iv_init(void);
int rebuild_iv_fini(void);

void
rebuild_tgt_status_check_ult(void *arg);

int
rebuild_tgt_prepare(crt_rpc_t *rpc, struct rebuild_tgt_pool_tracker **p_rpt);

int
rebuild_tgt_fini(struct rebuild_tgt_pool_tracker *rpt);

bool
rebuild_status_match(struct rebuild_tgt_pool_tracker *rpt,
		enum pool_comp_state states);

bool
is_current_tgt_unavail(struct rebuild_tgt_pool_tracker *rpt);

typedef int (*rebuild_obj_insert_cb_t)(struct rebuild_root *cont_root,
				       uuid_t co_uuid, daos_unit_oid_t oid,
				       daos_epoch_t epoch, unsigned int shard,
				       unsigned int tgt_idx, unsigned int *cnt,
				       int ref);
int
rebuild_obj_insert_cb(struct rebuild_root *cont_root, uuid_t co_uuid,
		      daos_unit_oid_t oid, daos_epoch_t eph, unsigned int shard,
		      unsigned int tgt_idx, unsigned int *cnt, int ref);

int
rebuild_cont_obj_insert(daos_handle_t toh, uuid_t co_uuid, daos_unit_oid_t oid,
			daos_epoch_t epoch, unsigned int shard,
			unsigned int tgt_idx, unsigned int *cnt, int ref,
			rebuild_obj_insert_cb_t obj_cb);
int
rebuilt_btr_destroy(daos_handle_t btr_hdl);

struct rebuild_tgt_pool_tracker *
rpt_lookup(uuid_t pool_uuid, unsigned int ver, uint32_t gen);

void
rgt_get(struct rebuild_global_pool_tracker *rgt);

void
rgt_put(struct rebuild_global_pool_tracker *rgt);

struct rebuild_global_pool_tracker *
rebuild_global_pool_tracker_lookup(const uuid_t pool_uuid, unsigned int ver, unsigned int gen);

int
rebuild_global_status_update(struct rebuild_global_pool_tracker *master_rpt,
			     struct rebuild_iv *iv);

int
rebuild_notify_ras_start(uuid_t *pool, uint32_t map_ver, char *op_str);

int
rebuild_notify_ras_end(uuid_t *pool, uint32_t map_ver, char *op_str, int op_rc);

void rebuild_leader_stop(const uuid_t pool_uuid, unsigned int version,
			 uint32_t rebuild_gen, uint64_t term);
int
rebuild_obj_tree_destroy(daos_handle_t btr_hdl);
#endif /* __REBUILD_INTERNAL_H_ */
