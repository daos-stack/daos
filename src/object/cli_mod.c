/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * object client: Module Definitions
 */
#define D_LOGFAC	DD_FAC(object)

#include <pthread.h>
#include <daos/common.h>
#include <daos/rpc.h>
#include <daos/mgmt.h>
#include <daos_types.h>
#include "obj_rpc.h"
#include "obj_internal.h"

unsigned int	srv_io_mode = DIM_DTX_FULL_ENABLED;
int		dc_obj_proto_version;

/**
 * Initialize object interface
 */
int
dc_obj_init(void)
{
	uint32_t		ver_array[2] = {DAOS_OBJ_VERSION - 1, DAOS_OBJ_VERSION};
	int			rc;

	rc = obj_utils_init();
	if (rc)
		return rc;

	rc = obj_class_init();
	if (rc)
		D_GOTO(out_utils, rc);

	dc_obj_proto_version = 0;
	rc = daos_rpc_proto_query(obj_proto_fmt_0.cpf_base, ver_array, 2, &dc_obj_proto_version);
	if (rc)
		D_GOTO(out_class, rc);

	if (dc_obj_proto_version == DAOS_OBJ_VERSION - 1) {
		rc = daos_rpc_register(&obj_proto_fmt_0, OBJ_PROTO_CLI_COUNT, NULL,
				       DAOS_OBJ_MODULE);
	} else if (dc_obj_proto_version == DAOS_OBJ_VERSION) {
		rc = daos_rpc_register(&obj_proto_fmt_1, OBJ_PROTO_CLI_COUNT, NULL,
				       DAOS_OBJ_MODULE);
	} else {
		D_ERROR("%d version object RPC not supported.\n", dc_obj_proto_version);
		rc = -DER_PROTO;
	}

	if (rc) {
		D_ERROR("failed to register daos %d version obj RPCs: "DF_RC"\n",
			dc_obj_proto_version, DP_RC(rc));
		D_GOTO(out_class, rc);
	}

	rc = obj_ec_codec_init();
	if (rc) {
		D_ERROR("failed to obj_ec_codec_init: "DF_RC"\n", DP_RC(rc));
		if (dc_obj_proto_version == DAOS_OBJ_VERSION - 1)
			daos_rpc_unregister(&obj_proto_fmt_0);
		else
			daos_rpc_unregister(&obj_proto_fmt_1);
		D_GOTO(out_class, rc);
	}

	tx_verify_rdg = false;
	d_getenv_bool("DAOS_TX_VERIFY_RDG", &tx_verify_rdg);
	D_INFO("%s TX redundancy group verification\n", tx_verify_rdg ? "Enable" : "Disable");

out_class:
	if (rc)
		obj_class_fini();
out_utils:
	if (rc)
		obj_utils_fini();
	return rc;
}

/**
 * Finalize object interface
 */
void
dc_obj_fini(void)
{
	if (dc_obj_proto_version == DAOS_OBJ_VERSION - 1)
		daos_rpc_unregister(&obj_proto_fmt_0);
	else
		daos_rpc_unregister(&obj_proto_fmt_1);
	obj_ec_codec_fini();
	obj_class_fini();
	obj_utils_fini();
}
