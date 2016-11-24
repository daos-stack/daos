/**
 * (C) Copyright 2016 Intel Corporation.
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
 * This file is part of the DAOS server. It implements the DAOS storage
 * management interface that covers:
 * - storage detection;
 * - storage allocation;
 * - DAOS pool initialization.
 *
 * The storage manager is a first-class server module (like dsm/r server-side
 * library) and can be unloaded/reloaded.
 */

#include "srv_internal.h"

static struct daos_rpc_handler ds_mgmt_handlers[] = {
	{
		.dr_opc		= MGMT_POOL_CREATE,
		.dr_hdlr	= ds_mgmt_hdlr_pool_create,
	}, {
		.dr_opc		= MGMT_POOL_DESTROY,
		.dr_hdlr	= ds_mgmt_hdlr_pool_destroy,
	}, {
		.dr_opc		= MGMT_TGT_CREATE,
		.dr_hdlr	= ds_mgmt_hdlr_tgt_create,
	}, {
		.dr_opc		= MGMT_TGT_DESTROY,
		.dr_hdlr	= ds_mgmt_hdlr_tgt_destroy,
	}, {
		.dr_opc = 0,
	}
};

static int
ds_mgmt_init()
{
	int rc;

	rc = ds_mgmt_tgt_init();
	if (rc)
		return rc;

	D_DEBUG(DF_MGMT, "successfull init call\n");
	return 0;
}

static int
ds_mgmt_fini()
{
	D_DEBUG(DF_MGMT, "successfull fini call\n");
	return 0;
}

struct dss_module mgmt_module = {
	.sm_name	= "mgmt",
	.sm_mod_id	= DAOS_MGMT_MODULE,
	.sm_ver		= 1,
	.sm_init	= ds_mgmt_init,
	.sm_fini	= ds_mgmt_fini,
	.sm_cl_rpcs	= mgmt_rpcs,
	.sm_handlers	= ds_mgmt_handlers,
};
