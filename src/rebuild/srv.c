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
 * rebuild: rebuild service
 *
 * Rebuild service module api.
 */
#define DD_SUBSYS	DD_FAC(rebuild)

#include <daos/rpc.h>
#include <daos_srv/daos_server.h>
#include "rpc.h"
#include "rebuild_internal.h"

static int
init(void)
{
	return 0;
}

static int
fini(void)
{
	return 0;
}

/* Note: the rpc input/output parameters is defined in daos_rpc */
static struct daos_rpc_handler rebuild_handlers[] = {
	{
		.dr_opc		= REBUILD_OBJECTS_SCAN,
		.dr_hdlr	= ds_rebuild_scan_handler
	}, {
		.dr_opc		= REBUILD_OBJECTS,
		.dr_hdlr	= ds_rebuild_obj_handler
	}, {
		.dr_opc		= REBUILD_TGT,
		.dr_hdlr	= ds_rebuild_tgt_handler
	}, {
		.dr_opc		= REBUILD_QUERY,
		.dr_hdlr	= ds_rebuild_query_handler
	}, {
		.dr_opc		= REBUILD_TGT_QUERY,
		.dr_hdlr	= ds_rebuild_tgt_query_handler,
		.dr_corpc_ops	= {
			.co_aggregate	= ds_rebuild_tgt_query_aggregator,
		}
	}, {
		.dr_opc		= 0
	}
};

static void *
rebuild_tls_init(const struct dss_thread_local_storage *dtls,
		 struct dss_module_key *key)
{
	struct rebuild_tls *tls;

	D_ALLOC_PTR(tls);
	return tls;
}

static void
rebuild_tls_fini(const struct dss_thread_local_storage *dtls,
		 struct dss_module_key *key, void *data)
{
	struct rebuild_tls *tls = data;

	D_ASSERT(tls->rebuild_local_root_init == 0);
	D_FREE_PTR(tls);
}

bool
is_rebuild_container(uuid_t cont_hdl_uuid)
{
	struct rebuild_tls *tls = rebuild_tls_get();

	if (uuid_compare(tls->rebuild_cont_hdl_uuid, cont_hdl_uuid) == 0)
		return true;
	return false;
}

bool
is_rebuild_pool(uuid_t pool_hdl)
{
	struct rebuild_tls *tls = rebuild_tls_get();

	return !uuid_compare(tls->rebuild_pool_hdl_uuid, pool_hdl);
}

struct dss_module_key rebuild_module_key = {
	.dmk_tags = DAOS_SERVER_TAG,
	.dmk_index = -1,
	.dmk_init = rebuild_tls_init,
	.dmk_fini = rebuild_tls_fini,
};

struct dss_module rebuild_module =  {
	.sm_name	= "rebuild",
	.sm_mod_id	= DAOS_REBUILD_MODULE,
	.sm_ver		= 1,
	.sm_init	= init,
	.sm_fini	= fini,
	.sm_cl_rpcs	= rebuild_cli_rpcs,
	.sm_srv_rpcs	= rebuild_rpcs,
	.sm_handlers	= rebuild_handlers,
	.sm_key		= &rebuild_module_key,
};
