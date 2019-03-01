/**
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
 * ds_mgmt: Internal Declarations
 *
 * This file contains all declarations that are only used by ds_mgmts.
 * All external variables and functions must have a "ds_mgmt_" prefix.
 */

#ifndef __SRV_MGMT_INTERNAL_H__
#define __SRV_MGMT_INTERNAL_H__

#include <gurt/list.h>
#include <daos/rpc.h>
#include <daos/common.h>
#include <daos_srv/daos_server.h>

#include "rpc.h"

/** srv.c */
void ds_mgmt_hdlr_svc_rip(crt_rpc_t *rpc);
void ds_mgmt_params_set_hdlr(crt_rpc_t *rpc);
void ds_mgmt_tgt_params_set_hdlr(crt_rpc_t *rpc);
void ds_mgmt_profile_hdlr(crt_rpc_t *rpc);

/** srv_pool.c */
void ds_mgmt_hdlr_pool_create(crt_rpc_t *rpc_req);
void ds_mgmt_hdlr_pool_destroy(crt_rpc_t *rpc_req);

/** srv_target.c */
int ds_mgmt_tgt_init(void);
void ds_mgmt_tgt_fini(void);
void ds_mgmt_hdlr_tgt_create(crt_rpc_t *rpc_req);
void ds_mgmt_hdlr_tgt_destroy(crt_rpc_t *rpc_req);
int ds_mgmt_tgt_create_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				  void *priv);
void ds_mgmt_tgt_profile_hdlr(crt_rpc_t *rpc);

#endif /* __SRV_MGMT_INTERNAL_H__ */
