/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(container)

#include <daos_srv/daos_server.h>
#include <daos/rpc.h>
#include "rpc.h"
#include "srv_internal.h"

static int
init(void)
{
	int rc;

	rc = ds_oid_iv_init();
	if (rc)
		D_GOTO(err, rc);

	rc = ds_cont_iv_init();
	if (rc)
		D_GOTO(err, rc);

	rc = ds_cont_prop_default_init();
	if (rc)
		D_GOTO(err, rc);

	return 0;
err:
	ds_oid_iv_fini();
	return rc;
}

static int
fini(void)
{
	ds_cont_iv_fini();
	ds_oid_iv_fini();
	ds_cont_prop_default_fini();

	return 0;
}

static struct crt_corpc_ops ds_cont_tgt_destroy_co_ops = {
	.co_aggregate   = ds_cont_tgt_destroy_aggregator,
	.co_pre_forward = NULL,
};

static struct crt_corpc_ops ds_cont_tgt_query_co_ops = {
	.co_aggregate   = ds_cont_tgt_query_aggregator,
	.co_pre_forward = NULL,
};

static struct crt_corpc_ops ds_cont_tgt_epoch_aggregate_co_ops = {
	.co_aggregate   = ds_cont_tgt_epoch_aggregate_aggregator,
	.co_pre_forward = NULL,
};

static struct crt_corpc_ops ds_cont_tgt_snapshot_notify_co_ops = {
	.co_aggregate   = ds_cont_tgt_snapshot_notify_aggregator,
	.co_pre_forward = NULL,
};

/* Define for cont_rpcs[] array population below.
 * See CONT_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.dr_opc       = a,	\
	.dr_hdlr      = d,	\
	.dr_corpc_ops = e,	\
}

static struct daos_rpc_handler cont_handlers[] = {
	CONT_PROTO_CLI_RPC_LIST,
	CONT_PROTO_SRV_RPC_LIST,
};

#undef X

static void *
dsm_tls_init(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key)
{
	struct dsm_tls *tls;
	int		rc;

	D_ALLOC_PTR(tls);
	if (tls == NULL)
		return NULL;

	rc = ds_cont_child_cache_create(&tls->dt_cont_cache);
	if (rc != 0) {
		D_ERROR("failed to create thread-local container cache: %d\n",
			rc);
		D_FREE(tls);
		return NULL;
	}

	rc = ds_cont_hdl_hash_create(&tls->dt_cont_hdl_hash);
	if (rc != 0) {
		D_ERROR("failed to create thread-local container handle cache:"
			" "DF_RC"\n", DP_RC(rc));
		ds_cont_child_cache_destroy(tls->dt_cont_cache);
		D_FREE(tls);
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
	ds_cont_child_cache_destroy(tls->dt_cont_cache);
	D_FREE(tls);
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
	.sm_ver		= DAOS_CONT_VERSION,
	.sm_init	= init,
	.sm_fini	= fini,
	.sm_proto_fmt	= &cont_proto_fmt,
	.sm_cli_count	= CONT_PROTO_CLI_COUNT,
	.sm_handlers	= cont_handlers,
	.sm_key		= &cont_module_key,
};
