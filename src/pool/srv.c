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
 * ds_pool: Pool Server
 *
 * This is part of daos_server. It exports the pool RPC handlers and implements
 * Pool Server API.
 */
#define DD_SUBSYS	DD_FAC(pool)

#include <daos_srv/pool.h>

#include <daos/rpc.h>
#include <daos_srv/daos_server.h>
#include "rpc.h"
#include "srv_internal.h"

static int
init(void)
{
	int rc;

	rc = ds_pool_mpool_cache_init();
	if (rc != 0)
		D_GOTO(err, rc);

	rc = ds_pool_svc_cache_init();
	if (rc != 0)
		D_GOTO(err_storage, rc);

	rc = ds_pool_cache_init();
	if (rc != 0)
		D_GOTO(err_pool_svc, rc);

	rc = ds_pool_hdl_hash_init();
	if (rc != 0)
		D_GOTO(err_pool_cache, rc);

	return 0;

err_pool_cache:
	ds_pool_cache_fini();
err_pool_svc:
	ds_pool_svc_cache_fini();
err_storage:
	ds_pool_mpool_cache_fini();
err:
	return rc;
}

static int
fini(void)
{
	ds_pool_hdl_hash_fini();
	ds_pool_cache_fini();
	ds_pool_svc_cache_fini();
	ds_pool_mpool_cache_fini();
	return 0;
}

/* Note: the rpc input/output parameters is defined in daos_rpc */
static struct daos_rpc_handler pool_handlers[] = {
	{
		.dr_opc		= POOL_CONNECT,
		.dr_hdlr	= ds_pool_connect_handler
	}, {
		.dr_opc		= POOL_DISCONNECT,
		.dr_hdlr	= ds_pool_disconnect_handler
	}, {
		.dr_opc		= POOL_QUERY,
		.dr_hdlr	= ds_pool_query_handler
	}, {
		.dr_opc		= POOL_EXCLUDE,
		.dr_hdlr	= ds_pool_exclude_handler
	}, {
		.dr_opc		= POOL_EVICT,
		.dr_hdlr	= ds_pool_evict_handler
	}, {
		.dr_opc		= POOL_TGT_CONNECT,
		.dr_hdlr	= ds_pool_tgt_connect_handler,
		.dr_corpc_ops	= {
			.co_aggregate	= ds_pool_tgt_connect_aggregator
		}
	}, {
		.dr_opc		= POOL_TGT_DISCONNECT,
		.dr_hdlr	= ds_pool_tgt_disconnect_handler,
		.dr_corpc_ops	= {
			.co_aggregate	= ds_pool_tgt_disconnect_aggregator
		}
	}, {
		.dr_opc		= POOL_TGT_UPDATE_MAP,
		.dr_hdlr	= ds_pool_tgt_update_map_handler,
		.dr_corpc_ops	= {
			.co_aggregate	= ds_pool_tgt_update_map_aggregator
		}
	}, {
		.dr_opc		= 0
	}
};

static void *
dsm_tls_init(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key)
{
	struct dsm_tls *tls;

	D_ALLOC_PTR(tls);
	if (tls == NULL)
		return NULL;

	DAOS_INIT_LIST_HEAD(&tls->dt_pool_list);
	return tls;
}

static void
dsm_tls_fini(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key, void *data)
{
	struct dsm_tls *tls = data;

	ds_pool_child_purge(tls);
	D_ASSERT(daos_list_empty(&tls->dt_pool_list));
	D_FREE_PTR(tls);
}

struct dss_module_key dsm_module_key = {
	.dmk_tags = DAOS_SERVER_TAG,
	.dmk_index = -1,
	.dmk_init = dsm_tls_init,
	.dmk_fini = dsm_tls_fini,
};

struct dss_module pool_module =  {
	.sm_name	= "pool",
	.sm_mod_id	= DAOS_POOL_MODULE,
	.sm_ver		= 1,
	.sm_init	= init,
	.sm_fini	= fini,
	.sm_cl_rpcs	= pool_rpcs,
	.sm_srv_rpcs	= pool_srv_rpcs,
	.sm_handlers	= pool_handlers,
	.sm_key		= &dsm_module_key,
};
