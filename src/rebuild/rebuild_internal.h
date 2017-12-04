/**
 * (C) Copyright 2017 Intel Corporation.
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
 * rebuild: rebuild internal.h
 *
 */

#ifndef __REBUILD_INTERNAL_H__
#define __REBUILD_INTERNAL_H__

#include <stdint.h>
#include <uuid/uuid.h>
#include <daos/rpc.h>
#include <daos/btree.h>

struct rebuild_dkey {
	daos_key_t	rd_dkey;
	daos_list_t	rd_list;
	uuid_t		rd_cont_uuid;
	daos_unit_oid_t	rd_oid;
	daos_epoch_t	rd_epoch;
	uint32_t	rd_map_ver;
};

struct rebuild_puller {
	unsigned int	rp_inflight;
	ABT_thread	rp_ult;
	ABT_mutex	rp_lock;
	/** serialize initialization of ULTs */
	ABT_cond	rp_fini_cond;
	daos_list_t	rp_dkey_list;
	unsigned int	rp_ult_running:1;
};

/* Each pool being rebuilt will have a rebuild_tracker */
struct rebuild_pool_tracker {
	/** pin the pool during the rebuild */
	struct ds_pool		*rt_pool;
	/** active rebuild pullers for each xstream */
	struct rebuild_puller	*rt_pullers;
	/** # xstreams */
	int			rt_puller_nxs;

	/** the current version being rebuilt, only used by leader */
	uint32_t		rt_rebuild_ver;

	/* Link it to the rebuild_global tracker_list */
	daos_list_t		rt_list;
	ABT_mutex		rt_lock;
	uuid_t			rt_pool_uuid;
	struct daos_rebuild_status rt_status;
	struct btr_root		rt_local_root;
	daos_handle_t		rt_local_root_hdl;
	d_rank_list_t		*rt_svc_list;
	d_rank_t		rt_rank;
	d_rank_t		rt_leader_rank;
	unsigned int		rt_lead_puller_running:1,
				rt_abort:1,
				rt_finishing:1,
				rt_done:1,
				rt_master:1,
				rt_prepared:1;
};

/* Global structure on all targets to track all pool rebuilding */
struct rebuild_global {
	/* Only operated by stream 0, no need lock */
	daos_list_t	rg_tracker_list;

	/* rebuild pool/container HDL uuid */
	uuid_t		rg_pool_hdl_uuid;
	uuid_t		rg_cont_hdl_uuid;

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
	daos_list_t	dst_list;
	uuid_t		dst_pool_uuid;
	d_rank_list_t	*dst_tgts_failed;
	d_rank_list_t	*dst_svc_list;
	uint32_t	dst_map_ver;
};

/* Per pool structure in TLS to check pool rebuild status
 * per xstream.
 */
struct rebuild_pool_tls {
	uuid_t		rebuild_pool_uuid;
	daos_handle_t	rebuild_pool_hdl;
	daos_list_t	rebuild_pool_list;
	uint64_t	rebuild_pool_obj_count;
	uint64_t	rebuild_pool_rec_count;
	unsigned int	rebuild_pool_ver;
	int		rebuild_pool_status;

	unsigned int	rebuild_pool_scanning:1;
};

/* per thread structure to track rebuild status for all pools */
struct rebuild_tls {
	/* rebuild_pool_tls will link here */
	daos_list_t	rebuild_pool_list;
};

struct rebuild_root {
	struct btr_root	btr_root;
	daos_handle_t	root_hdl;
	unsigned int	count;
};

struct rebuild_tgt_query_info {
	int scanning;
	int status;
	int rec_count;
	int obj_count;
	bool rebuilding;
	ABT_mutex lock;
};

struct rebuild_iv {
	uuid_t		riv_poh_uuid;
	uuid_t		riv_coh_uuid;
	uuid_t		riv_pool_uuid;
	uint64_t	riv_obj_count;
	uint64_t	riv_rec_count;
	unsigned int	riv_rank;
	unsigned int	riv_master_rank;
	unsigned int	riv_ver;
	unsigned int	riv_done;
	int		riv_status;
};

extern struct ds_iv_entry_ops rebuild_iv_ops;
extern struct dss_module_key rebuild_module_key;
static inline struct rebuild_tls *
rebuild_tls_get()
{
	return dss_module_key_get(dss_tls_get(), &rebuild_module_key);
}

struct rebuild_pool_tls *
rebuild_pool_tls_lookup(uuid_t pool_uuid, unsigned int ver);

struct pool_map *rebuild_pool_map_get(struct ds_pool *pool);
void rebuild_pool_map_put(struct pool_map *map);

void rebuild_obj_handler(crt_rpc_t *rpc);
void rebuild_tgt_prepare_handler(crt_rpc_t *rpc);
void rebuild_tgt_scan_handler(crt_rpc_t *rpc);

void rebuild_iv_ns_handler(crt_rpc_t *rpc);
int rebuild_iv_fetch(void *ns, struct rebuild_iv *rebuild_iv);
int rebuild_iv_update(void *ns, struct rebuild_iv *rebuild_iv,
		      unsigned int shortcut, unsigned int sync_mode);
int rebuild_iv_ns_create(struct ds_pool *pool, d_rank_list_t *exclude_tgts,
			 unsigned int master_rank);

void
rebuild_tgt_status_check(void *arg);

int
rebuild_tgt_prepare(uuid_t pool_uuid, d_rank_list_t *svc_list,
		    unsigned int pmap_ver, struct rebuild_pool_tracker **p_rpt);

int
rebuild_cont_obj_insert(daos_handle_t toh, uuid_t co_uuid,
			daos_unit_oid_t oid, unsigned int shard);

struct rebuild_pool_tracker *
rebuild_pool_tracker_lookup(uuid_t pool_uuid, unsigned int ver);

int ds_obj_open(daos_handle_t coh, daos_obj_id_t oid,
		daos_epoch_t epoch, unsigned int mode,
		daos_handle_t *oh);
int ds_obj_close(daos_handle_t obj_hl);

int ds_obj_single_shard_list_dkey(daos_handle_t oh, daos_epoch_t epoch,
			      uint32_t *nr, daos_key_desc_t *kds,
			      daos_sg_list_t *sgl, daos_hash_out_t *anchor);
int ds_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch,
		 daos_key_t *dkey, uint32_t *nr,
		 daos_key_desc_t *kds, daos_sg_list_t *sgl,
		 daos_hash_out_t *anchor);

int ds_obj_fetch(daos_handle_t oh, daos_epoch_t epoch,
		 daos_key_t *dkey, unsigned int nr,
		 daos_iod_t *iods, daos_sg_list_t *sgls,
		 daos_iom_t *maps);

int ds_obj_list_rec(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		daos_key_t *akey, daos_iod_type_t type, daos_size_t *size,
		uint32_t *nr, daos_recx_t *recxs, daos_epoch_range_t *eprs,
		uuid_t *cookies, uint32_t *versions, daos_hash_out_t *anchor,
		bool incr);

#endif /* __REBUILD_INTERNAL_H_ */
