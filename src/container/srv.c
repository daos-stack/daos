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
 * ds_cont: Container Server
 *
 * This is part of daos_server. It exports the container RPC handlers and
 * Container Server API.
 */

#include <daos_srv/daos_server.h>
#include <daos/rpc.h>
#include "rpc.h"
#include "srv_internal.h"

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
static struct daos_rpc_handler dsms_handlers[] = {
	{
		.dr_opc		= DSM_CONT_CREATE,
		.dr_hdlr	= dsms_hdlr_cont_create
	}, {
		.dr_opc		= DSM_CONT_DESTROY,
		.dr_hdlr	= dsms_hdlr_cont_destroy
	}, {
		.dr_opc		= DSM_CONT_OPEN,
		.dr_hdlr	= dsms_hdlr_cont_open
	}, {
		.dr_opc		= DSM_CONT_CLOSE,
		.dr_hdlr	= dsms_hdlr_cont_close
	}, {
		.dr_opc		= DSM_CONT_EPOCH_QUERY,
		.dr_hdlr	= dsms_hdlr_cont_op
	}, {
		.dr_opc		= DSM_CONT_EPOCH_HOLD,
		.dr_hdlr	= dsms_hdlr_cont_op
	}, {
		.dr_opc		= DSM_CONT_EPOCH_COMMIT,
		.dr_hdlr	= dsms_hdlr_cont_op
	}, {
		.dr_opc		= DSM_TGT_CONT_DESTROY,
		.dr_hdlr	= dsms_hdlr_tgt_cont_destroy,
		.dr_corpc_ops	= {
			.co_aggregate  = dsms_hdlr_tgt_cont_destroy_aggregate
		}
	}, {
		.dr_opc		= DSM_TGT_CONT_OPEN,
		.dr_hdlr	= dsms_hdlr_tgt_cont_open,
		.dr_corpc_ops	= {
			.co_aggregate  = dsms_hdlr_tgt_cont_open_aggregate
		}
	}, {
		.dr_opc		= DSM_TGT_CONT_CLOSE,
		.dr_hdlr	= dsms_hdlr_tgt_cont_close,
		.dr_corpc_ops	= {
			.co_aggregate  = dsms_hdlr_tgt_cont_close_aggregate
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
	int		rc;

	D_ALLOC_PTR(tls);
	if (tls == NULL)
		return NULL;

	rc = ds_cont_cache_create(&tls->dt_cont_cache);
	if (rc != 0) {
		D_ERROR("failed to create thread-local container cache: %d\n",
			rc);
		D_FREE_PTR(tls);
		return NULL;
	}

	rc = ds_cont_hdl_hash_create(&tls->dt_cont_hdl_hash);
	if (rc != 0) {
		D_ERROR("failed to create thread-local container handle cache: "
			"%d\n", rc);
		ds_cont_cache_destroy(tls->dt_cont_cache);
		D_FREE_PTR(tls);
		return NULL;
	}

	return tls;
}

static void
dsm_tls_fini(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key, void *data)
{
	struct dsm_tls *tls = data;

	ds_cont_hdl_hash_destroy(&tls->dt_cont_hdl_hash);
	ds_cont_cache_destroy(tls->dt_cont_cache);
	D_FREE_PTR(tls);
}

struct dss_module_key cont_module_key = {
	.dmk_tags = DAOS_SERVER_TAG,
	.dmk_index = -1,
	.dmk_init = dsm_tls_init,
	.dmk_fini = dsm_tls_fini,
};

struct dss_module cont_module =  {
	.sm_name	= "cont",
	.sm_mod_id	= DAOS_CONT_MODULE,
	.sm_ver		= 1,
	.sm_init	= init,
	.sm_fini	= fini,
	.sm_cl_rpcs	= cont_rpcs,
	.sm_srv_rpcs	= cont_srv_rpcs,
	.sm_handlers	= dsms_handlers,
	.sm_key		= &cont_module_key,
};
