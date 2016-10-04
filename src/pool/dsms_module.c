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
 * dsms: Module Definitions
 *
 * dsms is the DSM server module/library. It exports the DSM RPC handlers and
 * the DSM server API. This file contains the definitions expected by server;
 * the DSM server API methods are exported directly where they are defined as
 * extern functions.
 */

#include <daos_srv/daos_server.h>
#include <daos/rpc.h>
#include "dsm_rpc.h"
#include "dsms_internal.h"

int
dsms_corpc_create(dtp_context_t ctx, dtp_group_t *group, dtp_opcode_t opcode,
		  dtp_rpc_t **rpc)
{
	dtp_opcode_t opc;

	opc = DAOS_RPC_OPCODE(opcode, DAOS_POOL_MODULE, 1);
	return dtp_corpc_req_create(ctx, group, NULL /* excluded_ranks */, opc,
				    NULL /* co_bulk_hdl */, NULL /* priv */,
				    0 /* flags */, 0 /* tree_topo */, rpc);
}

static int
rpc_cb(const struct dtp_cb_info *cb_info)
{
	ABT_eventual *eventual = cb_info->dci_arg;

	ABT_eventual_set(*eventual, (void *)&cb_info->dci_rc,
			 sizeof(cb_info->dci_rc));
	return 0;
}

/* Send the request and wait for the reply. Does not consume rpc references. */
int
dsms_rpc_send(dtp_rpc_t *rpc)
{
	ABT_eventual	eventual;
	int	       *status;
	int		rc;

	rc = ABT_eventual_create(sizeof(*status), &eventual);
	if (rc != ABT_SUCCESS)
		D_GOTO(out, rc = dss_abterr2der(rc));

	dtp_req_addref(rpc);

	rc = dtp_req_send(rpc, rpc_cb, &eventual);
	if (rc != 0)
		D_GOTO(out_eventual, rc);

	rc = ABT_eventual_wait(eventual, (void **)&status);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_eventual, rc = dss_abterr2der(rc));

	rc = *status;

out_eventual:
	ABT_eventual_free(&eventual);
out:
	return rc;
}

static int
init(void)
{
	int rc;

	rc = dsms_storage_init();
	if (rc != 0)
		D_GOTO(err, rc);

	rc = dsms_module_pool_init();
	if (rc != 0)
		D_GOTO(err_storage, rc);

	rc = dsms_module_target_init();
	if (rc != 0)
		D_GOTO(err_pool, rc);

	return 0;

err_pool:
	dsms_module_pool_fini();
err_storage:
	dsms_storage_fini();
err:
	return rc;
}

static int
fini(void)
{
	dsms_module_target_fini();
	dsms_module_pool_fini();
	dsms_storage_fini();
	return 0;
}

/* Note: the rpc input/output parameters is defined in daos_rpc */
static struct daos_rpc_handler dsms_handlers[] = {
	{
		.dr_opc		= DSM_POOL_CONNECT,
		.dr_hdlr	= dsms_hdlr_pool_connect
	}, {
		.dr_opc		= DSM_POOL_DISCONNECT,
		.dr_hdlr	= dsms_hdlr_pool_disconnect
	}, {
		.dr_opc		= DSM_TGT_POOL_CONNECT,
		.dr_hdlr	= dsms_hdlr_tgt_pool_connect,
		.dr_corpc_ops	= {
			.co_aggregate	= dsms_hdlr_tgt_pool_connect_aggregate
		}
	}, {
		.dr_opc		= DSM_TGT_POOL_DISCONNECT,
		.dr_hdlr	= dsms_hdlr_tgt_pool_disconnect,
		.dr_corpc_ops	= {
			.co_aggregate  = dsms_hdlr_tgt_pool_disconnect_aggregate
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
	.sm_handlers	= dsms_handlers,
	.sm_key		= &dsm_module_key,
};
