/*
 * (C) Copyright 2015-2019 Intel Corporation.
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
 * \file
 *
 * ds_cont: Container Server API
 */

#ifndef __DAOS_SRV_CONTAINER_H__
#define __DAOS_SRV_CONTAINER_H__

#include <daos/common.h>
#include <daos_types.h>
#include <daos_srv/pool.h>
#include <daos_srv/rsvc.h>
#include <daos_srv/vos_types.h>

void ds_cont_wrlock_metadata(struct cont_svc *svc);
void ds_cont_rdlock_metadata(struct cont_svc *svc);
void ds_cont_unlock_metadata(struct cont_svc *svc);
int ds_cont_init_metadata(struct rdb_tx *tx, const rdb_path_t *kvs,
			  const uuid_t pool_uuid);
int ds_cont_svc_init(struct cont_svc **svcp, const uuid_t pool_uuid,
		     uint64_t id, struct ds_rsvc *rsvc);
void ds_cont_svc_fini(struct cont_svc **svcp);
void ds_cont_svc_step_up(struct cont_svc *svc);
void ds_cont_svc_step_down(struct cont_svc *svc);

/*
 * Per-thread container (memory) object
 *
 * Stores per-thread, per-container information, such as the vos container
 * handle.
 */
struct ds_cont {
	struct daos_llink	 sc_list;
	daos_handle_t		 sc_hdl;
	uuid_t			 sc_uuid;
	void			*sc_dtx_flush_cbdata;
	void			*sc_dtx_resync_cbdata;
	/* The time for the latest DTX resync operation. */
	uint64_t		 sc_dtx_resync_time;
	uint32_t		 sc_dtx_resyncing:1,
				 sc_dtx_aggregating:1,
				 sc_closing:1;
	uint32_t		 sc_dtx_flush_wait_count;
};

/*
 * Per-thread container handle (memory) object
 *
 * Stores per-thread, per-handle information, such as the container
 * capabilities. References the ds_cont and the ds_pool_child objects.
 */
struct ds_cont_hdl {
	d_list_t		sch_entry;
	uuid_t			sch_uuid;	/* of the container handle */
	uint64_t		sch_capas;
	struct ds_pool_child   *sch_pool;
	struct ds_cont	       *sch_cont;
	int			sch_ref;
	uint32_t		sch_dtx_registered:1,
				sch_deleted:1;
};

struct ds_cont_hdl *ds_cont_hdl_lookup(const uuid_t uuid);
void ds_cont_hdl_put(struct ds_cont_hdl *hdl);
void ds_cont_hdl_get(struct ds_cont_hdl *hdl);

int ds_cont_close_by_pool_hdls(uuid_t pool_uuid, uuid_t *pool_hdls,
			       int n_pool_hdls, crt_context_t ctx);
int
ds_cont_local_open(uuid_t pool_uuid, uuid_t cont_hdl_uuid, uuid_t cont_uuid,
		   uint64_t capas, struct ds_cont_hdl **cont_hdl);
int
ds_cont_local_close(uuid_t cont_hdl_uuid);

int
ds_cont_lookup_or_create(struct ds_cont_hdl *hdl, uuid_t cont_uuid);
int
ds_cont_lookup(uuid_t pool_uuid, uuid_t cont_uuid, struct ds_cont **ds_cont);

void ds_cont_put(struct ds_cont *cont);
void ds_cont_get(struct ds_cont *cont);

int
ds_cont_iter(daos_handle_t ph, uuid_t co_uuid, ds_iter_cb_t callback,
	     void *arg, uint32_t type);
#endif /* ___DAOS_SRV_CONTAINER_H_ */
