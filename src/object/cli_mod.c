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

struct obj_proto {
	struct rsvc_client	cli;
	crt_endpoint_t		ep;
	int			version;
	int			rc;
	bool			completed;
};

static void
query_cb(struct crt_proto_query_cb_info *cb_info)
{
	struct obj_proto *oproto = (struct obj_proto *)cb_info->pq_arg;

	if (daos_rpc_retryable_rc(cb_info->pq_rc)) {
		uint32_t	ver_array[2] = {DAOS_OBJ_VERSION - 1, DAOS_OBJ_VERSION};
		int		rc;

		rc = rsvc_client_choose(&oproto->cli, &oproto->ep);
		if (rc) {
			D_ERROR("rsvc_client_choose() failed: "DF_RC"\n", DP_RC(rc));
			oproto->rc = rc;
			oproto->completed = true;
		}

		rc = crt_proto_query_with_ctx(&oproto->ep, obj_proto_fmt_0.cpf_base, ver_array, 2,
					      query_cb, oproto, daos_get_crt_ctx());
		if (rc) {
			D_ERROR("crt_proto_query_with_ctx() failed: "DF_RC"\n", DP_RC(rc));
			oproto->rc = rc;
			oproto->completed = true;
		}
	} else {
		oproto->rc = cb_info->pq_rc;
		oproto->version = cb_info->pq_ver;
		oproto->completed = true;
	}
}

/**
 * Initialize object interface
 */
int
dc_obj_init(void)
{
	uint32_t		ver_array[2] = {DAOS_OBJ_VERSION - 1, DAOS_OBJ_VERSION};
	struct dc_mgmt_sys	*sys;
	struct obj_proto	*oproto = NULL;
	crt_context_t		ctx = daos_get_crt_ctx();
	int			num_ranks;
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

	D_ALLOC_PTR(oproto);
	if (oproto == NULL)
		D_GOTO(out_grp, rc = -DER_NOMEM);

	rc = rsvc_client_init(&oproto->cli, sys->sy_info.ms_ranks);
	if (rc) {
		D_ERROR("rsvc_client_init() failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_grp, rc);
	}

	oproto->ep.ep_grp = sys->sy_group;
	oproto->ep.ep_tag = 0;

	num_ranks = dc_mgmt_net_get_num_srv_ranks();
	oproto->ep.ep_rank = rand() % num_ranks;


	rc = crt_proto_query_with_ctx(&oproto->ep, obj_proto_fmt_0.cpf_base, ver_array, 2, query_cb,
				      oproto, ctx);
	if (rc) {
		D_ERROR("crt_proto_query_with_ctx() failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_rsvc, rc);
	}

	while (!oproto->completed) {
		rc = crt_progress(ctx, 0);
		if (rc && rc != -DER_TIMEDOUT) {
			D_ERROR("failed to progress CART context: %d\n", rc);
			D_GOTO(out_rsvc, rc);
		}
	}

	if (oproto->rc != -DER_SUCCESS) {
		rc = oproto->rc;
		D_ERROR("crt_proto_query()failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_rsvc, rc);
	}

	dc_obj_proto_version = oproto->version;
	if (dc_obj_proto_version != DAOS_OBJ_VERSION &&
	    dc_obj_proto_version != DAOS_OBJ_VERSION - 1) {
		D_ERROR("Invalid object protocol version %d\n", dc_obj_proto_version);
		D_GOTO(out_rsvc, rc = -DER_PROTO);
	}

	if (dc_obj_proto_version == DAOS_OBJ_VERSION - 1) {
		rc = daos_rpc_register(&obj_proto_fmt_0, OBJ_PROTO_CLI_COUNT, NULL,
				       DAOS_OBJ_MODULE);
		if (rc) {
			D_ERROR("failed to register daos obj RPCs: "DF_RC"\n", DP_RC(rc));
			D_GOTO(out_rsvc, rc);
		}
	} else if (dc_obj_proto_version == DAOS_OBJ_VERSION) {
		rc = daos_rpc_register(&obj_proto_fmt_1, OBJ_PROTO_CLI_COUNT, NULL,
				       DAOS_OBJ_MODULE);
		if (rc) {
			D_ERROR("failed to register daos obj RPCs: "DF_RC"\n", DP_RC(rc));
			D_GOTO(out_rsvc, rc);
		}
	}

	rc = obj_ec_codec_init();
	if (rc) {
		D_ERROR("failed to obj_ec_codec_init: "DF_RC"\n", DP_RC(rc));
		if (dc_obj_proto_version == DAOS_OBJ_VERSION - 1)
			daos_rpc_unregister(&obj_proto_fmt_0);
		else
			daos_rpc_unregister(&obj_proto_fmt_1);
		D_GOTO(out_rsvc, rc);
	}

out_rsvc:
	rsvc_client_fini(&oproto->cli);
out_grp:
	dc_mgmt_sys_detach(sys);
out_class:
	if (rc)
		obj_class_fini();
out_utils:
	if (rc)
		obj_utils_fini();
out:
	D_FREE(oproto);
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
