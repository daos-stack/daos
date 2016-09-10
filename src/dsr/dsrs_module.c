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
 * dsrs: Module Definitions
 */

#include <daos_srv/daos_server.h>
#include <daos/rpc.h>
#include "dsr_rpc.h"
#include "dsr_internal.h"

static int
dsr_mod_init(void)
{
	return 0;
}

static int
dsr_mod_fini(void)
{
	return 0;
}

/* Note: the rpc input/output parameters is defined in daos_rpc */
static struct daos_rpc_handler dsrs_handlers[] = {
	{
		.dr_opc		= DSR_TGT_OBJ_UPDATE,
		.dr_hdlr	= dsrs_hdlr_object_rw,
	}, {
		.dr_opc		= DSR_TGT_OBJ_FETCH,
		.dr_hdlr	= dsrs_hdlr_object_rw,
	}, {
		.dr_opc		= DSR_TGT_OBJ_ENUMERATE,
		.dr_hdlr	= dsrs_hdlr_object_enumerate,
	}, {
		.dr_opc		= 0
	}
};

struct dss_module daos_sr_srv_module =  {
	.sm_name	= "daos_sr_srv",
	.sm_mod_id	= DAOS_DSR_MODULE,
	.sm_ver		= 1,
	.sm_init	= dsr_mod_init,
	.sm_fini	= dsr_mod_fini,
	.sm_cl_rpcs	= dsr_rpcs, /** TBD */
	.sm_handlers	= dsrs_handlers,
};
