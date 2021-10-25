/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * object client: Module Definitions
 */
#define D_LOGFAC	DD_FAC(object)
#define M_TAG		DM_TAG(OBJ)

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
