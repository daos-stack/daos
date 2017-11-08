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
/*
 * object server: module definitions
 */
#define DDSUBSYS	DDFAC(object)

#include <daos_srv/daos_server.h>
#include <daos/rpc.h>
#include "obj_rpc.h"
#include "obj_internal.h"

bool srv_bypass_bulk;

static int
obj_mod_init(void)
{
	char	*env;

	env = getenv(IO_BYPASS_ENV);
	if (env && !strcasecmp(env, "srv_bulk")) {
		D__DEBUG(DB_IO, "All bulk data will be dropped\n");
		srv_bypass_bulk = true;
	}

	dss_abt_pool_choose_cb_register(DAOS_OBJ_MODULE,
					ds_obj_abt_pool_choose_cb);
	return 0;
}

static int
obj_mod_fini(void)
{
	return 0;
}

/* Note: the rpc input/output parameters is defined in daos_rpc */
static struct daos_rpc_handler obj_handlers[] = {
	{
		.dr_opc		= DAOS_OBJ_RPC_UPDATE,
		.dr_hdlr	= ds_obj_rw_handler,
	},
	{
		.dr_opc		= DAOS_OBJ_RPC_FETCH,
		.dr_hdlr	= ds_obj_rw_handler,
	},
	{
		.dr_opc		= DAOS_OBJ_DKEY_RPC_ENUMERATE,
		.dr_hdlr	= ds_obj_enum_handler,
	},
	{
		.dr_opc         = DAOS_OBJ_AKEY_RPC_ENUMERATE,
		.dr_hdlr        = ds_obj_enum_handler,
	},
	{
		.dr_opc         = DAOS_OBJ_RECX_RPC_ENUMERATE,
		.dr_hdlr        = ds_obj_enum_handler,
	},
	{
		.dr_opc		= DAOS_OBJ_RPC_PUNCH,
		.dr_hdlr	= ds_obj_punch_handler,
	},
	{
		.dr_opc		= DAOS_OBJ_RPC_PUNCH_DKEYS,
		.dr_hdlr	= ds_obj_punch_handler,
	},
	{
		.dr_opc		= DAOS_OBJ_RPC_PUNCH_AKEYS,
		.dr_hdlr	= ds_obj_punch_handler,
	},
	{
		.dr_opc		= 0
	}
};

static void *
obj_tls_init(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key)
{
	struct obj_tls *tls;

	D__ALLOC_PTR(tls);
	return tls;
}

static void
obj_tls_fini(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key, void *data)
{
	struct obj_tls *tls = data;

	if (tls->ot_echo_sgl.sg_iovs != NULL)
		daos_sgl_fini(&tls->ot_echo_sgl, true);

	D__FREE_PTR(tls);
}

struct dss_module_key obj_module_key = {
	.dmk_tags = DAOS_SERVER_TAG,
	.dmk_index = -1,
	.dmk_init = obj_tls_init,
	.dmk_fini = obj_tls_fini,
};

struct dss_module obj_module =  {
	.sm_name	= "obj",
	.sm_mod_id	= DAOS_OBJ_MODULE,
	.sm_ver		= 1,
	.sm_init	= obj_mod_init,
	.sm_fini	= obj_mod_fini,
	.sm_cl_rpcs	= daos_obj_rpcs,
	.sm_handlers	= obj_handlers,
	.sm_key		= &obj_module_key,
};
