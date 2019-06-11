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
/*
 * object server: module definitions
 */
#define D_LOGFAC	DD_FAC(object)

#include <daos_srv/daos_server.h>
#include <daos_srv/vos.h>
#include <daos_srv/pool.h>
#include <daos/rpc.h>
#include "obj_rpc.h"
#include "obj_internal.h"

/**
 * Swtich of enable DTX or not, enabled by default.
 */
bool srv_enable_dtx = true;

static int
obj_mod_init(void)
{
	uint32_t	mode = DIM_DTX_FULL_ENABLED;
	int		rc;

	d_getenv_int("DAOS_IO_MODE", &mode);
	if (mode != DIM_DTX_FULL_ENABLED) {
		srv_enable_dtx = false;
		D_DEBUG(DB_IO, "DTX is disabled.\n");
	} else {
		D_DEBUG(DB_IO, "DTX is enabled.\n");
	}

	rc = obj_ec_codec_init();
	if (rc != 0)
		D_ERROR("failed to obj_ec_codec_init: %d\n", rc);

	return rc;
}

static int
obj_mod_fini(void)
{
	obj_ec_codec_fini();
	return 0;
}

/* Define for cont_rpcs[] array population below.
 * See OBJ_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.dr_opc       = a,	\
	.dr_hdlr      = d,	\
	.dr_corpc_ops = e,	\
}

static struct daos_rpc_handler obj_handlers[] = {
	OBJ_PROTO_CLI_RPC_LIST,
};

#undef X

static void *
obj_tls_init(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key)
{
	struct obj_tls *tls;

	D_ALLOC_PTR(tls);
	return tls;
}

static void
obj_tls_fini(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key, void *data)
{
	struct obj_tls *tls = data;

	if (tls->ot_echo_sgl.sg_iovs != NULL)
		daos_sgl_fini(&tls->ot_echo_sgl, true);

	if (tls->ot_sp)
		srv_profile_destroy(tls->ot_sp);

	D_FREE(tls);
}

char *profile_op_names[] = {
	[OBJ_PF_UPDATE_PREP] = "update_prep",
	[OBJ_PF_UPDATE_DISPATCH] = "update_dispatch",
	[OBJ_PF_UPDATE_LOCAL] = "update_local",
	[OBJ_PF_UPDATE_END] = "update_end",
	[OBJ_PF_UPDATE_WAIT] = "update_end",
	[OBJ_PF_UPDATE_REPLY] = "update_repl",
	[OBJ_PF_UPDATE] = "update",
};

static int
ds_obj_profile_start(char *path)
{
	struct obj_tls *tls = obj_tls_get();
	int rc;

	if (tls->ot_sp)
		return 0;

	rc = srv_profile_start(&tls->ot_sp, path, profile_op_names);

	D_DEBUG(DB_MGMT, "object profile start: %d\n", rc);
	return rc;
}

static int
ds_obj_profile_stop(void)
{
	struct obj_tls *tls = obj_tls_get();
	int	rc;

	if (tls->ot_sp == NULL)
		return 0;

	rc = srv_profile_stop(tls->ot_sp);

	D_DEBUG(DB_MGMT, "object profile stop: %d\n", rc);
	tls->ot_sp = NULL;
	return rc;
}

struct dss_module_key obj_module_key = {
	.dmk_tags = DAOS_SERVER_TAG,
	.dmk_index = -1,
	.dmk_init = obj_tls_init,
	.dmk_fini = obj_tls_fini,
};

static struct dss_module_ops ds_obj_mod_ops = {
	.dms_abt_pool_choose_cb = ds_obj_abt_pool_choose_cb,
	.dms_profile_start = ds_obj_profile_start,
	.dms_profile_stop = ds_obj_profile_stop,
};

struct dss_module obj_module =  {
	.sm_name	= "obj",
	.sm_mod_id	= DAOS_OBJ_MODULE,
	.sm_ver		= DAOS_OBJ_VERSION,
	.sm_init	= obj_mod_init,
	.sm_fini	= obj_mod_fini,
	.sm_proto_fmt	= &obj_proto_fmt,
	.sm_cli_count	= OBJ_PROTO_CLI_COUNT,
	.sm_handlers	= obj_handlers,
	.sm_key		= &obj_module_key,
	.sm_mod_ops	= &ds_obj_mod_ops,
};
