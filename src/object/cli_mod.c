/**
 * (C) Copyright 2016-2021 Intel Corporation.
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
 * object client: Module Definitions
 */
#define D_LOGFAC	DD_FAC(object)

#include <pthread.h>
#include <daos/common.h>
#include <daos/rpc.h>
#include <daos_types.h>
#include "obj_rpc.h"
#include "obj_internal.h"

unsigned int	srv_io_mode = DIM_DTX_FULL_ENABLED;

/**
 * Initialize object interface
 */
int
dc_obj_init(void)
{
	int rc;

	d_getenv_int("DAOS_IO_MODE", &srv_io_mode);
	if (srv_io_mode == DIM_CLIENT_DISPATCH) {
		D_DEBUG(DB_IO, "Client dispatch.\n");
	} else if (srv_io_mode == DIM_SERVER_DISPATCH) {
		D_DEBUG(DB_IO, "Server dispatch but without dtx.\n");
	} else {
		srv_io_mode = DIM_DTX_FULL_ENABLED;
		D_DEBUG(DB_IO, "Full dtx mode by default\n");
	}

	rc = obj_utils_init();
	if (rc)
		D_GOTO(out, rc);

	rc = obj_class_init();
	if (rc)
		D_GOTO(out_utils, rc);

	rc = daos_rpc_register(&obj_proto_fmt, OBJ_PROTO_CLI_COUNT,
				NULL, DAOS_OBJ_MODULE);
	if (rc) {
		D_ERROR("failed to register daos obj RPCs: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out_class, rc);
	}

	rc = obj_ec_codec_init();
	if (rc) {
		D_ERROR("failed to obj_ec_codec_init: "DF_RC"\n", DP_RC(rc));
		daos_rpc_unregister(&obj_proto_fmt);
		D_GOTO(out_class, rc);
	}

	D_GOTO(out, rc = 0);

out_class:
	obj_class_fini();
out_utils:
	obj_utils_fini();
out:
	return rc;
}

/**
 * Finalize object interface
 */
void
dc_obj_fini(void)
{
	daos_rpc_unregister(&obj_proto_fmt);
	obj_ec_codec_fini();
	obj_class_fini();
	obj_utils_fini();
}
