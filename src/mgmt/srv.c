/**
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of the DAOS server. It implements the DAOS storage
 * management interface that covers:
 * - storage detection;
 * - storage allocation;
 * - storage health query
 * - DAOS pool initialization.
 *
 * The management server is a first-class server module (like object/pool
 * server-side library) and can be unloaded/reloaded.
 */
#define D_LOGFAC	DD_FAC(mgmt)

#include <signal.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/rsvc.h>
#include <daos_srv/pool.h>
#include <daos_srv/security.h>
#include <daos/drpc_modules.h>
#include <daos_mgmt.h>

#include "srv_internal.h"
#include "drpc_internal.h"

const int max_svc_nreplicas = 13;

static struct crt_corpc_ops ds_mgmt_hdlr_tgt_create_co_ops = {
	.co_aggregate	= ds_mgmt_tgt_create_aggregator,
	.co_pre_forward	= NULL,
	.co_post_reply = ds_mgmt_tgt_create_post_reply,
};

static struct crt_corpc_ops ds_mgmt_hdlr_tgt_destroy_co_ops = {
	.co_aggregate	= ds_mgmt_tgt_destroy_aggregator
};

static struct crt_corpc_ops ds_mgmt_hdlr_tgt_map_update_co_ops = {
	.co_aggregate	= ds_mgmt_tgt_map_update_aggregator,
	.co_pre_forward	= ds_mgmt_tgt_map_update_pre_forward,
};

/* Define for cont_rpcs[] array population below.
 * See MGMT_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)                                                                           \
	{                                                                                          \
	    .dr_opc       = a,                                                                     \
	    .dr_hdlr      = d,                                                                     \
	    .dr_corpc_ops = e,                                                                     \
	},

static struct daos_rpc_handler mgmt_handlers_v2[] = {MGMT_PROTO_CLI_RPC_LIST MGMT_PROTO_SRV_RPC_LIST_V2};
static struct daos_rpc_handler mgmt_handlers_v3[] = {MGMT_PROTO_CLI_RPC_LIST MGMT_PROTO_SRV_RPC_LIST};

#undef X

static void
process_drpc_request(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	/**
	 * Process drpc request and populate daos response,
	 * command errors should be indicated inside daos response.
	 */
	switch (drpc_req->method) {
	case DRPC_METHOD_MGMT_PREP_SHUTDOWN:
		ds_mgmt_drpc_prep_shutdown(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_PING_RANK:
		ds_mgmt_drpc_ping_rank(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_SET_UP:
		ds_mgmt_drpc_set_up(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_SET_LOG_MASKS:
		ds_mgmt_drpc_set_log_masks(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_SET_RANK:
		ds_mgmt_drpc_set_rank(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_CREATE:
		ds_mgmt_drpc_pool_create(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_DESTROY:
		ds_mgmt_drpc_pool_destroy(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_UPGRADE:
		ds_mgmt_drpc_pool_upgrade(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_EVICT:
		ds_mgmt_drpc_pool_evict(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_EXCLUDE:
		ds_mgmt_drpc_pool_exclude(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_DRAIN:
		ds_mgmt_drpc_pool_drain(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_REINT:
		ds_mgmt_drpc_pool_reintegrate(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_EXTEND:
		ds_mgmt_drpc_pool_extend(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_BIO_HEALTH_QUERY:
		ds_mgmt_drpc_bio_health_query(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_SMD_LIST_DEVS:
		ds_mgmt_drpc_smd_list_devs(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_SMD_LIST_POOLS:
		ds_mgmt_drpc_smd_list_pools(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_DEV_SET_FAULTY:
		ds_mgmt_drpc_dev_set_faulty(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_DEV_REPLACE:
		ds_mgmt_drpc_dev_replace(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_GET_ACL:
		ds_mgmt_drpc_pool_get_acl(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_OVERWRITE_ACL:
		ds_mgmt_drpc_pool_overwrite_acl(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_UPDATE_ACL:
		ds_mgmt_drpc_pool_update_acl(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_DELETE_ACL:
		ds_mgmt_drpc_pool_delete_acl(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_LIST_CONTAINERS:
		ds_mgmt_drpc_pool_list_cont(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_SET_PROP:
		ds_mgmt_drpc_pool_set_prop(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_GET_PROP:
		ds_mgmt_drpc_pool_get_prop(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_QUERY:
		ds_mgmt_drpc_pool_query(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_QUERY_TARGETS:
		ds_mgmt_drpc_pool_query_targets(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_CONT_SET_OWNER:
		ds_mgmt_drpc_cont_set_owner(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_GROUP_UPDATE:
		ds_mgmt_drpc_group_update(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_LED_MANAGE:
		ds_mgmt_drpc_dev_manage_led(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_CHK_START:
		ds_mgmt_drpc_check_start(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_CHK_STOP:
		ds_mgmt_drpc_check_stop(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_CHK_QUERY:
		ds_mgmt_drpc_check_query(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_CHK_PROP:
		ds_mgmt_drpc_check_prop(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_CHK_ACT:
		ds_mgmt_drpc_check_act(drpc_req, drpc_resp);
		break;
	default:
		drpc_resp->status = DRPC__STATUS__UNKNOWN_METHOD;
		D_ERROR("Unknown method\n");
	}
}

static struct dss_drpc_handler mgmt_drpc_handlers[] = {
	{
		.module_id = DRPC_MODULE_MGMT,
		.handler = process_drpc_request
	},
	{
		.module_id = 0,
		.handler = NULL
	}
};

/**
 * Set parameter on all of server targets, for testing or other
 * purpose.
 */
void
ds_mgmt_params_set_hdlr(crt_rpc_t *rpc)
{
	struct mgmt_params_set_in	*ps_in;
	crt_opcode_t			opc;
	int				topo;
	crt_rpc_t			*tc_req;
	struct mgmt_tgt_params_set_in	*tc_in;
	struct mgmt_params_set_out	*out;
	int				rc;

	ps_in = crt_req_get(rpc);
	D_ASSERT(ps_in != NULL);
	D_DEBUG(DB_MGMT, "ps_rank=%u, key_id=0x%x, value=0x%"PRIx64", extra=0x%"PRIx64"\n",
		ps_in->ps_rank, ps_in->ps_key_id, ps_in->ps_value, ps_in->ps_value_extra);

	if (ps_in->ps_rank != -1) {
		/* Only set local parameter */
		rc = dss_parameters_set(ps_in->ps_key_id, ps_in->ps_value);
		if (rc == 0 && ps_in->ps_key_id == DMG_KEY_FAIL_LOC)
			rc = dss_parameters_set(DMG_KEY_FAIL_VALUE,
						ps_in->ps_value_extra);
		if (rc)
			D_ERROR("Set parameter failed key_id %d: rc %d\n",
				ps_in->ps_key_id, rc);
		D_GOTO(out, rc);
	}

	topo = crt_tree_topo(CRT_TREE_KNOMIAL, 32);
	opc = DAOS_RPC_OPCODE(MGMT_TGT_PARAMS_SET, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_corpc_req_create(dss_get_module_info()->dmi_ctx, NULL, NULL,
				  opc, NULL, NULL, 0, topo, &tc_req);
	if (rc)
		D_GOTO(out, rc);

	tc_in = crt_req_get(tc_req);
	D_ASSERT(tc_in != NULL);

	tc_in->tps_key_id = ps_in->ps_key_id;
	tc_in->tps_value = ps_in->ps_value;
	tc_in->tps_value_extra = ps_in->ps_value_extra;

	rc = dss_rpc_send(tc_req);

	crt_req_decref(tc_req);
out:
	out = crt_reply_get(rpc);
	out->srv_rc = rc;
	crt_reply_send(rpc);
}

/**
 * Set parameter on all of server targets, for testing or other
 * purpose.
 */
void
ds_mgmt_profile_hdlr(crt_rpc_t *rpc)
{
	struct mgmt_profile_in	*in;
	crt_opcode_t		opc;
	int			topo;
	crt_rpc_t		*tc_req;
	struct mgmt_profile_in	*tc_in;
	struct mgmt_profile_out	*out;
	int			rc;

	in = crt_req_get(rpc);
	D_ASSERT(in != NULL);

	topo = crt_tree_topo(CRT_TREE_KNOMIAL, 32);
	opc = DAOS_RPC_OPCODE(MGMT_TGT_PROFILE, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_corpc_req_create(dss_get_module_info()->dmi_ctx, NULL, NULL,
				  opc, NULL, NULL, 0, topo, &tc_req);
	if (rc)
		D_GOTO(out, rc);

	tc_in = crt_req_get(tc_req);
	D_ASSERT(tc_in != NULL);

	tc_in->p_path = in->p_path;
	tc_in->p_op = in->p_op;
	tc_in->p_avg = in->p_avg;
	rc = dss_rpc_send(tc_req);

	crt_req_decref(tc_req);
out:
	D_DEBUG(DB_MGMT, "profile hdlr: rc "DF_RC"\n", DP_RC(rc));
	out = crt_reply_get(rpc);
	out->p_rc = rc;
	crt_reply_send(rpc);
}

/**
 * Set mark on all of server targets
 */
void
ds_mgmt_mark_hdlr(crt_rpc_t *rpc)
{
	struct mgmt_mark_in	*in;
	crt_opcode_t		opc;
	int			topo;
	crt_rpc_t		*tc_req;
	struct mgmt_mark_in	*tc_in;
	struct mgmt_mark_out	*out;
	int			rc;

	in = crt_req_get(rpc);
	D_ASSERT(in != NULL);

	topo = crt_tree_topo(CRT_TREE_KNOMIAL, 32);
	opc = DAOS_RPC_OPCODE(MGMT_TGT_MARK, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_corpc_req_create(dss_get_module_info()->dmi_ctx, NULL, NULL,
				  opc, NULL, NULL, 0, topo, &tc_req);
	if (rc)
		D_GOTO(out, rc);

	tc_in = crt_req_get(tc_req);
	D_ASSERT(tc_in != NULL);

	tc_in->m_mark = in->m_mark;
	rc = dss_rpc_send(tc_req);

	crt_req_decref(tc_req);
out:
	D_DEBUG(DB_MGMT, "mark hdlr: rc "DF_RC"\n", DP_RC(rc));
	out = crt_reply_get(rpc);
	out->m_rc = rc;
	crt_reply_send(rpc);
}

void
ds_mgmt_hdlr_svc_rip(crt_rpc_t *rpc)
{
	struct mgmt_svc_rip_in	*murderer;
	int			 sig;
	bool			 force;
	d_rank_t		 rank = -1;

	murderer = crt_req_get(rpc);
	if (murderer == NULL)
		return;

	force = (murderer->rip_flags != 0);

	/*
	 * the yield below is to workaround an ofi err msg at client-side -
	 * fi_cq_readerr got err: 5(Input/output error) ..
	 */
	int i;

	for (i = 0; i < 200; i++) {
		ABT_thread_yield();
		usleep(10);
	}

	/** ... adieu */
	if (force)
		sig = SIGKILL;
	else
		sig = SIGTERM;
	crt_group_rank(NULL, &rank);
	D_PRINT("Service rank %d is being killed by signal %d... farewell\n",
		rank, sig);
	kill(getpid(), sig);
}

void ds_mgmt_pool_get_svcranks_hdlr(crt_rpc_t *rpc)
{
	struct mgmt_pool_get_svcranks_in	*in;
	struct mgmt_pool_get_svcranks_out	*out;
	int					 rc;

	in = crt_req_get(rpc);
	D_ASSERT(in != NULL);

	D_DEBUG(DB_MGMT, "get svcranks for pool "DF_UUIDF"\n",
		DP_UUID(in->gsr_puuid));

	out = crt_reply_get(rpc);

	rc =  ds_get_pool_svc_ranks(in->gsr_puuid, &out->gsr_ranks);
	if (rc == -DER_NONEXIST) /* not an error */
		D_DEBUG(DB_MGMT, DF_UUID": get_pool_svc_ranks() upcall failed, "
			DF_RC"\n", DP_UUID(in->gsr_puuid), DP_RC(rc));
	else if (rc != 0)
		D_ERROR(DF_UUID": get_pool_svc_ranks() upcall failed, "
			DF_RC"\n", DP_UUID(in->gsr_puuid), DP_RC(rc));
	out->gsr_rc = rc;

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR(DF_UUID": crt_reply_send() failed, "DF_RC"\n",
			DP_UUID(in->gsr_puuid), DP_RC(rc));

	d_rank_list_free(out->gsr_ranks);
}

void ds_mgmt_pool_find_hdlr(crt_rpc_t *rpc)
{
	struct mgmt_pool_find_in	*in;
	struct mgmt_pool_find_out	*out;
	int					 rc;

	in = crt_req_get(rpc);
	D_ASSERT(in != NULL);

	D_DEBUG(DB_MGMT, "find pool uuid:"DF_UUID", lbl %s\n",
		DP_UUID(in->pfi_puuid), in->pfi_label);

	out = crt_reply_get(rpc);

	if (in->pfi_bylabel) {
		rc = ds_pool_find_bylabel(in->pfi_label, out->pfo_puuid,
					  &out->pfo_ranks);
	} else {
		rc = ds_get_pool_svc_ranks(in->pfi_puuid, &out->pfo_ranks);
	}
	if (rc == -DER_NONEXIST) /* not an error */
		D_DEBUG(DB_MGMT, DF_UUID": %s: ds_pool_find() not found, "
			DF_RC"\n", DP_UUID(in->pfi_puuid), in->pfi_label,
			DP_RC(rc));
	else if (rc != 0)
		D_ERROR(DF_UUID": %s: ds_pool_find_bylabel() upcall failed, "
			DF_RC"\n", DP_UUID(in->pfi_puuid), in->pfi_label,
			DP_RC(rc));
	out->pfo_rc = rc;

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR(DF_UUID": %s: crt_reply_send() failed, "DF_RC"\n",
			DP_UUID(in->pfi_puuid), in->pfi_label, DP_RC(rc));

	d_rank_list_free(out->pfo_ranks);
}

static int
check_cred_pool_access(uuid_t pool_uuid, d_rank_list_t *svc_ranks, int flags, d_iov_t *cred)
{
	daos_prop_t            *props           = NULL;
	struct daos_prop_entry *acl_entry       = NULL;
	struct daos_prop_entry *owner_entry     = NULL;
	struct daos_prop_entry *owner_grp_entry = NULL;
	struct d_ownership      owner;
	uint64_t                sec_capas;
	int                     rc;

	rc = ds_mgmt_pool_get_acl(pool_uuid, svc_ranks, &props);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to retrieve ACL props", DP_UUID(pool_uuid));
		return rc;
	}

	acl_entry = daos_prop_entry_get(props, DAOS_PROP_PO_ACL);
	D_ASSERT(acl_entry != NULL);
	D_ASSERT(acl_entry->dpe_val_ptr != NULL);

	owner_entry = daos_prop_entry_get(props, DAOS_PROP_PO_OWNER);
	D_ASSERT(owner_entry != NULL);
	D_ASSERT(owner_entry->dpe_str != NULL);

	owner_grp_entry = daos_prop_entry_get(props, DAOS_PROP_PO_OWNER_GROUP);
	D_ASSERT(owner_grp_entry != NULL);
	D_ASSERT(owner_grp_entry->dpe_str != NULL);

	owner.user  = owner_entry->dpe_str;
	owner.group = owner_grp_entry->dpe_str;

	rc = ds_sec_pool_get_capabilities(flags, cred, &owner, acl_entry->dpe_val_ptr, &sec_capas);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to read sec capabilities", DP_UUID(pool_uuid));
		D_GOTO(out_props, rc);
	}

	if (!ds_sec_pool_can_connect(sec_capas)) {
		D_GOTO(out_props, rc = -DER_NO_PERM);
	}

out_props:
	daos_prop_free(props);
	return rc;
}

void
ds_mgmt_pool_list_hdlr(crt_rpc_t *rpc)
{
	struct mgmt_pool_list_in   *in;
	struct mgmt_pool_list_out  *out;
	size_t                      n_mgmt     = 0;
	size_t                      n_rpc      = 0;
	daos_mgmt_pool_info_t      *mgmt_pools = NULL;
	struct mgmt_pool_list_pool *rpc_pools  = NULL;
	int                         i;
	int                         rc;
	int                         chk_rc;

	in = crt_req_get(rpc);
	D_ASSERT(in != NULL);

	out = crt_reply_get(rpc);
	D_ASSERT(out != NULL);

	if (in->pli_npools > 0) {
		D_ALLOC_ARRAY(mgmt_pools, in->pli_npools);
		if (mgmt_pools == NULL) {
			D_ERROR("failed to alloc mgmt pools\n");
			D_GOTO(send_resp, rc = -DER_NOMEM);
		}
	}

	n_mgmt = in->pli_npools;
	rc     = ds_get_pool_list(&n_mgmt, mgmt_pools);
	if (rc != 0) {
		DL_ERROR(rc, "ds_get_pool_list() failed");
		D_GOTO(send_resp, rc);
	}

	/* caller just needs the number of pools */
	if (in->pli_npools == 0) {
		out->plo_npools = n_mgmt;
		D_GOTO(send_resp, rc);
	}

	D_ALLOC_ARRAY(rpc_pools, n_mgmt);
	if (rpc_pools == NULL) {
		D_ERROR("failed to alloc response pools\n");
		D_GOTO(send_resp, rc = -DER_NOMEM);
	}

	for (i = 0; i < n_mgmt; i++) {
		daos_mgmt_pool_info_t      *mgmt_pool = &mgmt_pools[i];
		struct mgmt_pool_list_pool *rpc_pool  = &rpc_pools[n_rpc];

		chk_rc = check_cred_pool_access(mgmt_pool->mgpi_uuid, mgmt_pool->mgpi_svc,
						DAOS_PC_RO, &in->pli_cred);
		if (chk_rc != 0) {
			if (chk_rc != -DER_NO_PERM)
				DL_ERROR(chk_rc, DF_UUID ": failed to check pool access",
					 DP_UUID(mgmt_pool->mgpi_uuid));
			continue;
		}
		uuid_copy(rpc_pool->plp_uuid, mgmt_pool->mgpi_uuid);
		rpc_pool->plp_label    = mgmt_pool->mgpi_label;
		rpc_pool->plp_svc_list = mgmt_pool->mgpi_svc;
		n_rpc++;
	}
	out->plo_pools.ca_arrays = rpc_pools;
	out->plo_pools.ca_count  = n_rpc;
	out->plo_npools          = n_rpc;

send_resp:
	out->plo_op.mo_rc = rc;

	if (rc == 0) {
		if (n_rpc > 0)
			D_DEBUG(DB_MGMT, "returning %zu/%zu pools\n", n_rpc, n_mgmt);
		else
			D_DEBUG(DB_MGMT, "returning %zu pools\n", n_mgmt);
	} else {
		DL_ERROR(rc, "failed to list pools");
	}

	rc = crt_reply_send(rpc);
	if (rc != 0)
		DL_ERROR(rc, "crt_reply_send() failed");

	if (n_mgmt > 0 && mgmt_pools != NULL) {
		for (i = 0; i < n_mgmt; i++) {
			daos_mgmt_pool_info_t *mgmt_pool = &mgmt_pools[i];
			if (mgmt_pool->mgpi_label)
				D_FREE(mgmt_pool->mgpi_label);
			if (mgmt_pool->mgpi_svc)
				d_rank_list_free(mgmt_pool->mgpi_svc);
		}
		D_FREE(mgmt_pools);
	}
	if (rpc_pools)
		D_FREE(rpc_pools);
}

static int
ds_mgmt_init()
{
	int rc;

	rc = ds_mgmt_system_module_init();
	if (rc != 0)
		return rc;

	D_DEBUG(DB_MGMT, "successful init call\n");
	return 0;
}

static int
ds_mgmt_fini()
{
	ds_mgmt_system_module_fini();

	D_DEBUG(DB_MGMT, "successful fini call\n");
	return 0;
}

static int
ds_mgmt_setup()
{
	return ds_mgmt_tgt_setup();
}

static int
ds_mgmt_cleanup()
{
	ds_mgmt_tgt_cleanup();
	return ds_mgmt_svc_stop();
}

struct dss_module mgmt_module = {
	.sm_name          = "mgmt",
	.sm_mod_id        = DAOS_MGMT_MODULE,
	.sm_ver           = DAOS_MGMT_VERSION,
	.sm_proto_count   = 2,
	.sm_init          = ds_mgmt_init,
	.sm_fini          = ds_mgmt_fini,
	.sm_setup         = ds_mgmt_setup,
	.sm_cleanup       = ds_mgmt_cleanup,
	.sm_proto_fmt     = {&mgmt_proto_fmt_v2, &mgmt_proto_fmt_v3},
	.sm_cli_count     = {MGMT_PROTO_CLI_COUNT, MGMT_PROTO_CLI_COUNT},
	.sm_handlers      = {mgmt_handlers_v2, mgmt_handlers_v3},
	.sm_drpc_handlers = mgmt_drpc_handlers,
};
