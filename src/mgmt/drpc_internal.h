/*
 * (C) Copyright 2019-2020 Intel Corporation.
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * ds_mgmt_drpc: Internal definitions for dRPC handlers. These functions
 * accept a Drpc__Call, process it for input parameters, perform the desired
 * function, and return the results as a Drpc__Response.
 */

#ifndef __MGMT_DRPC_INTERNAL_H__
#define __MGMT_DRPC_INTERNAL_H__

#include <daos/drpc.pb-c.h>

void
ds_mgmt_drpc_prep_shutdown(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_ping_rank(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_set_rank(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_create_mgmt_svc(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_start_mgmt_svc(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_get_attach_info(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_join(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_create(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_destroy(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_evict(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_exclude(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_drain(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_extend(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_reintegrate(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_set_prop(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_get_acl(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_overwrite_acl(Drpc__Call *drpc_req,
				Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_update_acl(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_delete_acl(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_query(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_smd_list_devs(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_smd_list_pools(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_bio_health_query(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_dev_state_query(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_dev_set_faulty(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_set_up(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_list_pools(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_list_cont(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_cont_set_owner(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_group_update(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

#endif /* __MGMT_DRPC_INTERNAL_H__ */
