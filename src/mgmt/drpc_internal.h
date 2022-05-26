/*
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
ds_mgmt_drpc_set_log_masks(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

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
ds_mgmt_drpc_pool_get_prop(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_get_acl(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_overwrite_acl(Drpc__Call *drpc_req,
				Drpc__Response *drpc_resp);
void
ds_mgmt_drpc_pool_upgrade(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_update_acl(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_delete_acl(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_query(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_pool_query_targets(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

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
ds_mgmt_drpc_dev_replace(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_dev_identify(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

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

void
ds_mgmt_drpc_check_start(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_check_stop(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_check_query(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_check_prop(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

void
ds_mgmt_drpc_check_act(Drpc__Call *drpc_req, Drpc__Response *drpc_resp);

#endif /* __MGMT_DRPC_INTERNAL_H__ */
