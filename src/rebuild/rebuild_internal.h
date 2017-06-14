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

struct rebuild_globals {
	/** pin the pool during the rebuild */
	struct ds_pool		*rg_pool;
	/** active rebuild pullers for each xstream */
	int			*rg_pullers;
	/** # xstreams */
	int			 rg_puller_nxs;
	/** total number of pullers */
	int			 rg_puller_total;
	bool			 rg_leader;
	bool			 rg_leader_barrier;
	uint32_t		 rg_rebuild_ver;
	uint32_t		 rg_bcast_ver;
	daos_list_t		 rg_task_list;
	ABT_mutex		 rg_lock;
	ABT_cond		 rg_cond;
	uuid_t			 rg_pool_uuid;
	/* reserved for now, move rebuild_pool_hdl_uuid to here */
	uuid_t			 rg_poh_uuid;
	/* reserved for now, move rebuild_cont_hdl_uuid to here */
	uuid_t			 rg_coh_uuid;
};

extern struct rebuild_globals rebuild_gst;

struct rebuild_tls {
	struct btr_root rebuild_local_root;
	daos_handle_t	rebuild_local_root_hdl;
	uuid_t		rebuild_pool_hdl_uuid;
	uuid_t		rebuild_cont_hdl_uuid;
	daos_handle_t	rebuild_pool_hdl;
	int		rebuild_status;
	int		rebuild_obj_count;
	int		rebuild_rec_count;
	daos_list_t	rebuild_task_list;
	daos_rank_list_t *rebuild_svc_list;

	unsigned int	rebuild_local_root_init:1,
			rebuild_task_init:1,
			rebuild_scanning:1;
};

struct rebuild_root {
	struct btr_root	btr_root;
	daos_handle_t	root_hdl;
	unsigned int	count;
};

extern struct dss_module_key rebuild_module_key;
static inline struct rebuild_tls *
rebuild_tls_get()
{
	return dss_module_key_get(dss_tls_get(), &rebuild_module_key);
}

struct pool_map *rebuild_pool_map_get(void);
void rebuild_pool_map_put(struct pool_map *map);

int ds_rebuild_scan_handler(crt_rpc_t *rpc);
int ds_rebuild_obj_handler(crt_rpc_t *rpc);

int
ds_rebuild_cont_obj_insert(daos_handle_t toh, uuid_t co_uuid,
			   daos_unit_oid_t oid, unsigned int shard);
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
		uuid_t *cookies, daos_hash_out_t *anchor, bool incr);

#endif /* __REBUILD_INTERNAL_H_ */
