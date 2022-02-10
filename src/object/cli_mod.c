/**
 * (C) Copyright 2016-2021 Intel Corporation.
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

struct obj_proto {
	int	version;
	int	rc;
	bool	completed;
};

static void
query_cb(struct crt_proto_query_cb_info *cb_info)
{
	struct obj_proto *oproto = cb_info->pq_arg;

	oproto->rc = cb_info->pq_rc;
	oproto->version = cb_info->pq_ver;
	oproto->completed = true;
}

/**
 * Initialize object interface
 */
int
dc_obj_init(void)
{
	crt_endpoint_t		ep;
	uint32_t		ver_array[2] = {DAOS_OBJ_VERSION, DAOS_OBJ_VERSION + 1};
	struct dc_mgmt_sys	*sys;
	struct obj_proto	oproto = {0};
	crt_context_t		ctx = daos_get_crt_ctx();
	int			rc;

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

	dc_obj_proto_version = 0;

	rc = dc_mgmt_sys_attach(NULL, &sys);
	if (rc != 0) {
		D_ERROR("failed to attach to grp rc "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_class, rc);
	}

        ep.ep_grp = sys->sy_group;
	ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	ep.ep_rank = 0;

	rc = crt_proto_query_with_ctx(&ep, obj_proto_fmt_0.cpf_base, ver_array, 2, query_cb,
				      &oproto, ctx);
	if (rc) {
		D_ERROR("crt_proto_query() failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_grp, rc);
	}

	while (!oproto.completed) {
		rc = crt_progress(ctx, 0);
		if (rc && rc != -DER_TIMEDOUT) {
			D_ERROR("failed to progress CART context: %d\n", rc);
			D_GOTO(out_grp, rc);
		}
	}

	if (oproto.rc != -DER_SUCCESS) {
		rc = oproto.rc;
		D_ERROR("crt_proto_query()failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_grp, rc);
	}

	dc_obj_proto_version = oproto.version;

	if (dc_obj_proto_version != DAOS_OBJ_VERSION &&
	    dc_obj_proto_version != DAOS_OBJ_VERSION + 1) {
		D_ERROR("Invalid object protocol version %d\n", dc_obj_proto_version);
		D_GOTO(out_grp, rc = -DER_PROTO);
	}

	if (dc_obj_proto_version == DAOS_OBJ_VERSION) {
		rc = daos_rpc_register(&obj_proto_fmt_0, OBJ_PROTO_CLI_COUNT - 1, NULL,
				       DAOS_OBJ_MODULE);
		if (rc) {
			D_ERROR("failed to register daos obj RPCs: "DF_RC"\n", DP_RC(rc));
			D_GOTO(out_grp, rc);
		}
	} else if (dc_obj_proto_version == DAOS_OBJ_VERSION + 1) {
		rc = daos_rpc_register(&obj_proto_fmt_1, OBJ_PROTO_CLI_COUNT, NULL,
				       DAOS_OBJ_MODULE);
		if (rc) {
			D_ERROR("failed to register daos obj RPCs: "DF_RC"\n", DP_RC(rc));
			D_GOTO(out_grp, rc);
		}
	}

	rc = obj_ec_codec_init();
	if (rc) {
		D_ERROR("failed to obj_ec_codec_init: "DF_RC"\n", DP_RC(rc));
		if (dc_obj_proto_version == DAOS_OBJ_VERSION)
			daos_rpc_unregister(&obj_proto_fmt_0);
		else
			daos_rpc_unregister(&obj_proto_fmt_1);
		D_GOTO(out_grp, rc);
	}

	dc_mgmt_sys_detach(sys);
	D_GOTO(out, rc = 0);

out_grp:
	dc_mgmt_sys_detach(sys);
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
	if (dc_obj_proto_version == DAOS_OBJ_VERSION)
		daos_rpc_unregister(&obj_proto_fmt_0);
	else
		daos_rpc_unregister(&obj_proto_fmt_1);
	obj_ec_codec_fini();
	obj_class_fini();
	obj_utils_fini();
}
