/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(chk)

#include <daos/rpc.h>
#include <daos/common.h>
#include <daos_srv/pool.h>
#include <daos_srv/daos_chk.h>

#include "chk_internal.h"

#define X(a, b, c, d, e)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
}

static struct crt_proto_rpc_format chk_proto_rpc_fmt[] = {
	CHK_PROTO_SRV_RPC_LIST,
};

#undef X

struct crt_proto_format chk_proto_fmt = {
	.cpf_name  = "chk-proto",
	.cpf_ver   = DAOS_CHK_VERSION,
	.cpf_count = ARRAY_SIZE(chk_proto_rpc_fmt),
	.cpf_prf   = chk_proto_rpc_fmt,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_CHK_MODULE, 0)
};

struct chk_co_rpc_priv {
	chk_co_rpc_cb_t	 cb;
	void		*args;
};

static int
chk_start_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct chk_start_in	*in_source = crt_req_get(source);
	struct chk_start_out	*out_source = crt_reply_get(source);
	struct chk_start_out	*out_result = crt_reply_get(result);
	struct chk_co_rpc_priv	*ccrp = priv;
	int			 rc;

	if (out_source->cso_status < 0) {
		D_ERROR("Failed to check start with gen "DF_X64": "DF_RC"\n",
			in_source->csi_gen, DP_RC(out_source->cso_status));

		if (out_result->cso_child_status == 0)
			out_result->cso_child_status = out_source->cso_status;
	} else {
		rc = ccrp->cb(ccrp->args, out_source->cso_rank, out_source->cso_status,
			      out_source->cso_clues.ca_arrays, out_source->cso_clues.ca_count);
		if (rc != 0 && out_result->cso_child_status == 0)
			out_result->cso_child_status = rc;
	}

	return 0;
}

static int
chk_stop_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct chk_stop_in	*in_source = crt_req_get(source);
	struct chk_stop_out	*out_source = crt_reply_get(source);
	struct chk_stop_out	*out_result = crt_reply_get(result);
	struct chk_co_rpc_priv	*ccrp = priv;
	int			 rc;

	if (out_source->cso_status < 0) {
		D_ERROR("Failed to check stop with gen "DF_X64": "DF_RC"\n",
			in_source->csi_gen, DP_RC(out_source->cso_status));

		if (out_result->cso_child_status == 0)
			out_result->cso_child_status = out_source->cso_status;
	} else if (out_source->cso_status > 0 && ccrp->cb != NULL) {
		rc = ccrp->cb(ccrp->args, out_source->cso_rank, out_source->cso_status, NULL, 0);
		if (rc != 0 && out_result->cso_child_status == 0)
			out_result->cso_child_status = rc;
	}

	return 0;
}

static int
chk_query_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct chk_query_in	*in_source = crt_req_get(source);
	struct chk_query_out	*out_source = crt_reply_get(source);
	struct chk_query_out	*out_result = crt_reply_get(result);
	struct chk_co_rpc_priv	*ccrp = priv;
	int			 rc;

	if (out_source->cqo_status != 0) {
		D_ERROR("Failed to check query with gen "DF_X64": "DF_RC"\n",
			in_source->cqi_gen, DP_RC(out_source->cqo_status));

		if (out_result->cqo_child_status == 0)
			out_result->cqo_child_status = out_source->cqo_status;
	} else {
		rc = ccrp->cb(ccrp->args, 0, out_source->cqo_status,
			      out_source->cqo_shards.ca_arrays, out_source->cqo_shards.ca_count);
		if (rc != 0 && out_result->cqo_child_status == 0)
			out_result->cqo_child_status = rc;
	}

	return 0;
}

static int
chk_mark_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct chk_mark_in	*in_source = crt_req_get(source);
	struct chk_mark_out	*out_source = crt_reply_get(source);
	struct chk_mark_out	*out_result = crt_reply_get(result);

	if (out_source->cmo_status != 0) {
		D_ERROR("Failed to check mark rank dead with gen "DF_X64": "DF_RC"\n",
			in_source->cmi_gen, DP_RC(out_source->cmo_status));

		if (out_result->cmo_status == 0)
			out_result->cmo_status = out_source->cmo_status;
	}

	return 0;
}

static int
chk_act_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct chk_act_in	*in_source = crt_req_get(source);
	struct chk_act_out	*out_source = crt_reply_get(source);
	struct chk_act_out	*out_result = crt_reply_get(result);

	if (out_source->cao_status != 0) {
		D_ERROR("Failed to check act with gen "DF_X64": "DF_RC"\n",
			in_source->cai_gen, DP_RC(out_source->cao_status));

		if (out_result->cao_status == 0)
			out_result->cao_status = out_source->cao_status;
	}

	return 0;
}

static int
chk_cont_list_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct chk_cont_list_in		*in_source = crt_req_get(source);
	struct chk_cont_list_out	*out_source = crt_reply_get(source);
	struct chk_cont_list_out	*out_result = crt_reply_get(result);
	struct chk_co_rpc_priv		*ccrp = priv;
	int				 rc;

	if (out_source->cclo_status < 0) {
		D_ERROR("Failed to check cont list with gen "DF_X64": "DF_RC"\n",
			in_source->ccli_gen, DP_RC(out_source->cclo_status));

		if (out_result->cclo_child_status == 0)
			out_result->cclo_child_status = out_source->cclo_status;
	} else {
		rc = ccrp->cb(ccrp->args, out_source->cclo_rank, 0,
			      out_source->cclo_conts.ca_arrays, out_source->cclo_conts.ca_count);
		if (rc != 0 && out_result->cclo_child_status == 0)
			out_result->cclo_child_status = rc;
	}

	return 0;
}

static int
chk_pool_start_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct chk_pool_start_in	*in_source = crt_req_get(source);
	struct chk_pool_start_out	*out_source = crt_reply_get(source);
	struct chk_pool_start_out	*out_result = crt_reply_get(result);

	if (out_source->cpso_status != 0 && out_source->cpso_status != -DER_NONEXIST) {
		D_ERROR("Failed to pool start with gen "DF_X64" on rank %u: "DF_RC"\n",
			in_source->cpsi_gen, out_source->cpso_rank, DP_RC(out_source->cpso_status));

		if (out_result->cpso_status == 0)
			out_result->cpso_status = out_source->cpso_status;
	}

	return 0;
}

struct crt_corpc_ops chk_start_co_ops = {
	.co_aggregate	= chk_start_aggregator,
	.co_pre_forward	= NULL,
};

struct crt_corpc_ops chk_stop_co_ops = {
	.co_aggregate	= chk_stop_aggregator,
	.co_pre_forward	= NULL,
};

struct crt_corpc_ops chk_query_co_ops = {
	.co_aggregate	= chk_query_aggregator,
	.co_pre_forward	= NULL,
};

struct crt_corpc_ops chk_mark_co_ops = {
	.co_aggregate	= chk_mark_aggregator,
	.co_pre_forward	= NULL,
};

struct crt_corpc_ops chk_act_co_ops = {
	.co_aggregate	= chk_act_aggregator,
	.co_pre_forward	= NULL,
};

struct crt_corpc_ops chk_cont_list_co_ops = {
	.co_aggregate	= chk_cont_list_aggregator,
	.co_pre_forward	= NULL,
};

struct crt_corpc_ops chk_pool_start_co_ops = {
	.co_aggregate	= chk_pool_start_aggregator,
	.co_pre_forward	= NULL,
};

static inline int
chk_co_rpc_prepare(d_rank_list_t *rank_list, crt_opcode_t opc, struct chk_co_rpc_priv *priv,
		   crt_rpc_t **req)
{
	return crt_corpc_req_create(dss_get_module_info()->dmi_ctx, NULL, rank_list,
				    DAOS_RPC_OPCODE(opc, DAOS_CHK_MODULE, DAOS_CHK_VERSION),
				    NULL, priv, CRT_RPC_FLAG_FILTER_INVERT,
				    crt_tree_topo(CRT_TREE_KNOMIAL, 32), req);
}

static inline int
chk_sg_rpc_prepare(d_rank_t rank, crt_opcode_t opc, crt_rpc_t **req)
{
	crt_endpoint_t	tgt_ep;

	tgt_ep.ep_grp = NULL;
	tgt_ep.ep_rank = rank;
	tgt_ep.ep_tag = daos_rpc_tag(DAOS_REQ_CHK, 0);
	opc = DAOS_RPC_OPCODE(opc, DAOS_CHK_MODULE, DAOS_CHK_VERSION);

	return crt_req_create(dss_get_module_info()->dmi_ctx, &tgt_ep, opc, req);
}

int
chk_start_remote(d_rank_list_t *rank_list, uint64_t gen, uint32_t rank_nr, d_rank_t *ranks,
		 uint32_t policy_nr, struct chk_policy *policies, int pool_nr,
		 uuid_t pools[], uint32_t api_flags, int phase, d_rank_t leader, uint32_t flags,
		 chk_co_rpc_cb_t start_cb, void *args)
{
	struct chk_co_rpc_priv	 ccrp;
	crt_rpc_t		*req = NULL;
	struct chk_start_in	*csi;
	struct chk_start_out	*cso;
	int			 rc;
	int			 rc1;

	ccrp.cb = start_cb;
	ccrp.args = args;
	rc = chk_co_rpc_prepare(rank_list, CHK_START, &ccrp, &req);
	if (rc != 0)
		goto out;

	csi = crt_req_get(req);
	csi->csi_gen = gen;
	csi->csi_flags = flags;
	csi->csi_phase = phase;
	csi->csi_leader_rank = leader;
	csi->csi_api_flags = api_flags;
	csi->csi_ranks.ca_count = rank_nr;
	csi->csi_ranks.ca_arrays = ranks;
	csi->csi_policies.ca_count = policy_nr;
	csi->csi_policies.ca_arrays = policies;
	csi->csi_uuids.ca_count = pool_nr;
	csi->csi_uuids.ca_arrays = pools;

	rc = dss_rpc_send(req);
	if (rc != 0)
		goto out;

	cso = crt_reply_get(req);
	if (cso->cso_child_status != 0) {
		rc = cso->cso_child_status;

		/*
		 * Some failure happened on remote check engine or during aggregation.
		 * Then release the clues' buffer for the case of the check engine and
		 * the check leader are on the same rank. See ds_chk_start_hdlr for detail.
		 */
		if (cso->cso_status >= 0)
			chk_fini_clues(cso->cso_clues.ca_arrays, cso->cso_clues.ca_count,
				       cso->cso_rank);
	} else {
		rc = cso->cso_status;

		/*
		 * The aggregator only aggregates the results from other check
		 * engines, does not include the check engine on the same rank
		 * as the check leader resides. Let's aggregate it here.
		 */
		if (rc >= 0)
			rc = start_cb(args, cso->cso_rank, cso->cso_status,
				      cso->cso_clues.ca_arrays, cso->cso_clues.ca_count);
	}

out:
	if (req != NULL) {
		if (rc < 0 && rc != -DER_ALREADY) {
			rc1 = chk_stop_remote(rank_list, gen, pool_nr, pools, NULL, NULL);
			if (rc1 < 0)
				D_ERROR("Failed to cleanup DAOS check with gen "DF_X64": "DF_RC"\n",
					gen, DP_RC(rc1));
		}

		crt_req_decref(req);
	}

	D_CDEBUG(rc < 0, DLOG_ERR, DLOG_INFO,
		 "Rank %u start DAOS check with gen "DF_X64", flags %x, phase %d: "DF_RC"\n",
		 leader, gen, flags, phase, DP_RC(rc));

	return rc;
}

int
chk_stop_remote(d_rank_list_t *rank_list, uint64_t gen, int pool_nr, uuid_t pools[],
		chk_co_rpc_cb_t stop_cb, void *args)
{
	struct chk_co_rpc_priv	 ccrp;
	crt_rpc_t		*req;
	struct chk_stop_in	*csi;
	struct chk_stop_out	*cso;
	int			 rc;

	ccrp.cb = stop_cb;
	ccrp.args = args;
	rc = chk_co_rpc_prepare(rank_list, CHK_STOP, &ccrp, &req);
	if (rc != 0)
		goto out;

	csi = crt_req_get(req);
	csi->csi_gen = gen;
	csi->csi_uuids.ca_count = pool_nr;
	csi->csi_uuids.ca_arrays = pools;

	rc = dss_rpc_send(req);
	if (rc != 0)
		goto out;

	cso = crt_reply_get(req);
	if (cso->cso_child_status != 0) {
		rc = cso->cso_child_status;
	} else {
		rc = cso->cso_status;

		/*
		 * The aggregator only aggregates the results from other check
		 * engines, does not include the check engine on the same rank
		 * as the check leader resides. Let's aggregate it here.
		 */
		if (rc > 0 && stop_cb != NULL)
			rc = stop_cb(args, cso->cso_rank, cso->cso_status, NULL, 0);
	}

out:
	if (req != NULL)
		crt_req_decref(req);

	D_CDEBUG(rc < 0, DLOG_ERR, DLOG_INFO,
		 "Rank %u stop DAOS check with gen "DF_X64", pool_nr %d: "DF_RC"\n",
		 dss_self_rank(), gen, pool_nr, DP_RC(rc));

	return rc;
}

int
chk_query_remote(d_rank_list_t *rank_list, uint64_t gen, int pool_nr, uuid_t pools[],
		 chk_co_rpc_cb_t query_cb, void *args)
{
	struct chk_co_rpc_priv	 ccrp;
	crt_rpc_t		*req;
	struct chk_query_in	*cqi;
	struct chk_query_out	*cqo;
	int			 rc;

	ccrp.cb = query_cb;
	ccrp.args = args;
	rc = chk_co_rpc_prepare(rank_list, CHK_QUERY, &ccrp, &req);
	if (rc != 0)
		goto out;

	cqi = crt_req_get(req);
	cqi->cqi_gen = gen;
	cqi->cqi_uuids.ca_count = pool_nr;
	cqi->cqi_uuids.ca_arrays = pools;

	rc = dss_rpc_send(req);
	if (rc != 0)
		goto out;

	cqo = crt_reply_get(req);
	if (cqo->cqo_child_status != 0) {
		rc = cqo->cqo_child_status;

		/*
		 * Some failure happened on remote check engine or during aggregation.
		 * Then release the shards' buffer for the case of the check engine and
		 * the check leader are on the same rank. See ds_chk_query_hdlr for detail.
		 */
		if (cqo->cqo_status == 0)
			chk_fini_shards(cqo->cqo_shards.ca_arrays, cqo->cqo_shards.ca_count);
	} else {
		rc = cqo->cqo_status;

		/*
		 * The aggregator only aggregates the results from other check
		 * engines, does not include the check engine on the same rank
		 * as the check leader resides. Let's aggregate it here.
		 */
		if (rc == 0)
			rc = query_cb(args, 0, cqo->cqo_status, cqo->cqo_shards.ca_arrays,
				      cqo->cqo_shards.ca_count);
	}

out:
	if (req != NULL)
		crt_req_decref(req);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Rank %u query DAOS check with gen "DF_X64", pool_nr %d: "DF_RC"\n",
		 dss_self_rank(), gen, pool_nr, DP_RC(rc));

	return rc;
}

int
chk_mark_remote(d_rank_list_t *rank_list, uint64_t gen, d_rank_t rank, uint32_t version)
{
	crt_rpc_t		*req;
	struct chk_mark_in	*cmi;
	struct chk_mark_out	*cmo;
	int			 rc;

	rc = chk_co_rpc_prepare(rank_list, CHK_MARK, NULL, &req);
	if (rc != 0)
		goto out;

	cmi = crt_req_get(req);
	cmi->cmi_gen = gen;
	cmi->cmi_rank = rank;
	cmi->cmi_version = version;

	rc = dss_rpc_send(req);
	if (rc != 0)
		goto out;

	cmo = crt_reply_get(req);
	rc = cmo->cmo_status;

out:
	if (req != NULL)
		crt_req_decref(req);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Mark rank %u as dead for DAOS check with gen "DF_X64": "DF_RC"\n",
		 rank, gen, DP_RC(rc));

	return rc;
}

int
chk_act_remote(d_rank_list_t *rank_list, uint64_t gen, uint64_t seq, uint32_t cla,
	       uint32_t act, d_rank_t rank, bool for_all)
{
	crt_rpc_t		*req;
	struct chk_act_in	*cai;
	struct chk_act_out	*cao;
	int			 rc;

	if (for_all)
		rc = chk_co_rpc_prepare(rank_list, CHK_ACT, NULL, &req);
	else
		rc = chk_sg_rpc_prepare(rank, CHK_ACT, &req);

	if (rc != 0)
		goto out;

	cai = crt_req_get(req);
	cai->cai_gen = gen;
	cai->cai_seq = seq;
	cai->cai_cla = cla;
	cai->cai_act = act;
	cai->cai_flags = for_all ? CAF_FOR_ALL : 0;

	rc = dss_rpc_send(req);
	if (rc != 0)
		goto out;

	cao = crt_reply_get(req);
	rc = cao->cao_status;

out:
	if (req != NULL)
		crt_req_decref(req);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Rank %u take action for DAOS check with gen "DF_X64", seq "DF_X64": "DF_RC"\n",
		 rank, gen, seq, DP_RC(rc));

	return rc;
}

int
chk_cont_list_remote(struct ds_pool *pool, uint64_t gen, chk_co_rpc_cb_t list_cb, void *args)
{
	struct chk_co_rpc_priv		 ccrp;
	crt_rpc_t			*req;
	struct chk_cont_list_in		*ccli;
	struct chk_cont_list_out	*cclo;
	int				 rc;

	ccrp.cb = list_cb;
	ccrp.args = args;
	rc = ds_pool_bcast_create(dss_get_module_info()->dmi_ctx, pool, DAOS_CHK_MODULE,
				  CHK_CONT_LIST, DAOS_CHK_VERSION, &req, NULL, NULL, &ccrp);
	if (rc != 0) {
		D_ERROR("Failed to create RPC for check cont list for "DF_UUIDF": "DF_RC"\n",
			DP_UUID(pool->sp_uuid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	ccli = crt_req_get(req);
	ccli->ccli_gen = gen;
	ccli->ccli_rank = dss_self_rank();
	uuid_copy(ccli->ccli_pool, pool->sp_uuid);

	rc = dss_rpc_send(req);
	if (rc != 0)
		goto out;

	cclo = crt_reply_get(req);
	if (cclo->cclo_child_status != 0) {
		rc = cclo->cclo_child_status;

		/*
		 * Some failure happened on remote check engine or during aggregation.
		 * Then release the conts' buffer for the case of the check engine and
		 * PS leader are on the same rank. See ds_chk_cont_list_hdlr for detail.
		 */
		if (cclo->cclo_status >= 0)
			chk_fini_conts(cclo->cclo_conts.ca_arrays, cclo->cclo_rank);
	} else {
		rc = cclo->cclo_status;

		/*
		 * The aggregator only aggregates the results from the pool shards,
		 * does not include the pool shard on the same rank as the PS leader
		 * resides. Let's aggregate it here.
		 */
		if (rc >= 0)
			rc = list_cb(args, cclo->cclo_rank, 0,
				     cclo->cclo_conts.ca_arrays, cclo->cclo_conts.ca_count);
	}

out:
	if (req != NULL)
		crt_req_decref(req);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Rank %u DAOS check cont list for "DF_UUIDF" with gen "DF_X64": "DF_RC"\n",
		 dss_self_rank(), DP_UUID(pool->sp_uuid), gen, DP_RC(rc));

	return rc;
}

int
chk_pool_start_remote(d_rank_list_t *rank_list, uint64_t gen, uuid_t uuid, uint32_t phase)
{
	crt_rpc_t			*req;
	struct chk_pool_start_in	*cpsi;
	struct chk_pool_start_out	*cpso;
	int				 rc;

	rc = chk_co_rpc_prepare(rank_list, CHK_POOL_START, NULL, &req);
	if (rc != 0)
		goto out;

	cpsi = crt_req_get(req);
	cpsi->cpsi_gen = gen;
	uuid_copy(cpsi->cpsi_pool, uuid);
	cpsi->cpsi_phase = phase;

	rc = dss_rpc_send(req);
	if (rc != 0)
		goto out;

	cpso = crt_reply_get(req);
	rc = cpso->cpso_status;

out:
	if (req != NULL)
		crt_req_decref(req);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Start pool ("DF_UUIDF") with gen "DF_X64": "DF_RC"\n",
		 DP_UUID(uuid), gen, DP_RC(rc));

	return rc;
}

int
chk_pool_mbs_remote(d_rank_t rank, uint32_t phase, uint64_t gen, uuid_t uuid, char *label,
		    uint32_t flags, uint32_t mbs_nr, struct chk_pool_mbs *mbs_array,
		    struct rsvc_hint *hint)
{
	crt_rpc_t		*req;
	struct chk_pool_mbs_in	*cpmi;
	struct chk_pool_mbs_out	*cpmo;
	int			 rc;

	rc = chk_sg_rpc_prepare(rank, CHK_POOL_MBS, &req);
	if (rc != 0)
		goto out;

	cpmi = crt_req_get(req);
	cpmi->cpmi_gen = gen;
	uuid_copy(cpmi->cpmi_pool, uuid);
	cpmi->cpmi_flags = flags;
	cpmi->cpmi_phase = phase;
	cpmi->cpmi_label = label;
	cpmi->cpmi_targets.ca_count = mbs_nr;
	cpmi->cpmi_targets.ca_arrays = mbs_array;

	rc = dss_rpc_send(req);
	if (rc != 0)
		goto out;

	cpmo = crt_reply_get(req);
	rc = cpmo->cpmo_status;
	*hint = cpmo->cpmo_hint;

out:
	if (req != NULL)
		crt_req_decref(req);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Sent pool ("DF_UUIDF") members and label %s to rank %u with phase %d gen "
		 DF_X64": "DF_RC"\n",
		 DP_UUID(uuid), label != NULL ? label : "(null)", rank, phase, gen, DP_RC(rc));

	return rc;
}

int chk_report_remote(d_rank_t leader, uint64_t gen, uint32_t cla, uint32_t act, int result,
		      d_rank_t rank, uint32_t target, uuid_t *pool, uuid_t *cont,
		      daos_unit_oid_t *obj, daos_key_t *dkey, daos_key_t *akey, char *msg,
		      uint32_t option_nr, uint32_t *options, uint32_t detail_nr, d_sg_list_t *details,
		      uint64_t *seq)
{
	crt_rpc_t		*req;
	struct chk_report_in	*cri;
	struct chk_report_out	*cro;
	int			 rc;

	rc = chk_sg_rpc_prepare(leader, CHK_REPORT, &req);
	if (rc != 0)
		goto out;

	cri = crt_req_get(req);
	cri->cri_gen = gen;
	cri->cri_ics_class = cla;
	cri->cri_ics_action = act;
	cri->cri_ics_result = result;
	cri->cri_rank = rank;
	cri->cri_target = target;

	if (pool != NULL)
		uuid_copy(cri->cri_pool, *pool);
	else
		memset(cri->cri_pool, 0, sizeof(uuid_t));

	if (cont != NULL)
		uuid_copy(cri->cri_cont, *cont);
	else
		memset(cri->cri_cont, 0, sizeof(uuid_t));

	if (obj != NULL)
		cri->cri_obj = *obj;
	else
		memset(&cri->cri_obj, 0, sizeof(cri->cri_obj));

	if (dkey != NULL)
		cri->cri_dkey = *dkey;
	else
		memset(&cri->cri_dkey, 0, sizeof(cri->cri_dkey));

	if (akey != NULL)
		cri->cri_akey = *akey;
	else
		memset(&cri->cri_akey, 0, sizeof(cri->cri_akey));

	cri->cri_msg = msg;
	cri->cri_options.ca_count = option_nr;
	cri->cri_options.ca_arrays = options;
	cri->cri_details.ca_count = detail_nr;
	cri->cri_details.ca_arrays = details;

	rc = dss_rpc_send(req);
	if (rc != 0)
		goto out;

	cro = crt_reply_get(req);
	rc = cro->cro_status;
	*seq = cro->cro_seq;

out:
	if (req != NULL)
		crt_req_decref(req);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Rank %u report DAOS check to leader %u, gen "DF_X64", class %u, action %u, "
		 "result %d, "DF_UUIDF"/"DF_UUIDF", msg %s, got seq "DF_X64": "DF_RC"\n",
		 rank, leader, gen, cla, act, result, DP_UUID(pool), DP_UUID(cont),
		 msg, *seq, DP_RC(rc));

	return rc;
}

int
chk_rejoin_remote(d_rank_t leader, uint64_t gen, d_rank_t rank, uint32_t *pool_nr, uuid_t **pools)
{
	crt_rpc_t		*req;
	struct chk_rejoin_in	*cri;
	struct chk_rejoin_out	*cro;
	uuid_t			*tmp;
	int			 rc;

	rc = chk_sg_rpc_prepare(leader, CHK_REJOIN, &req);
	if (rc != 0)
		goto out;

	cri = crt_req_get(req);
	cri->cri_gen = gen;
	cri->cri_rank = rank;

	rc = dss_rpc_send(req);
	if (rc != 0)
		goto out;

	cro = crt_reply_get(req);
	rc = cro->cro_status;
	if (rc == 0 && cro->cro_pools.ca_count > 0) {
		D_ALLOC(tmp, cro->cro_pools.ca_count);
		if (tmp == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		memcpy(tmp, cro->cro_pools.ca_arrays, sizeof(*tmp) * cro->cro_pools.ca_count);
		*pool_nr = cro->cro_pools.ca_count;
		*pools = tmp;
	}

out:
	if (req != NULL)
		crt_req_decref(req);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Rank %u rejoin DAOS check with leader %u, gen "DF_X64": "DF_RC"\n",
		 rank, leader, gen, DP_RC(rc));

	return rc;
}

static int
crt_proc_struct_chk_policy(crt_proc_t proc, crt_proc_op_t proc_op, struct chk_policy *policy)
{
	int	rc;

	rc = crt_proc_uint32_t(proc, proc_op, &policy->cp_class);
	if (unlikely(rc != 0))
		return rc;

	return crt_proc_uint32_t(proc, proc_op, &policy->cp_action);
}

static int
crt_proc_struct_chk_time(crt_proc_t proc, crt_proc_op_t proc_op, struct chk_time *time)
{
	int	rc;

	rc = crt_proc_uint64_t(proc, proc_op, &time->ct_start_time);
	if (unlikely(rc != 0))
		return rc;

	return crt_proc_uint64_t(proc, proc_op, &time->ct_start_time);
}

static int
crt_proc_struct_chk_statistics(crt_proc_t proc, crt_proc_op_t proc_op, struct chk_statistics *cs)
{
	int	rc;

	rc = crt_proc_uint64_t(proc, proc_op, &cs->cs_total);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint64_t(proc, proc_op, &cs->cs_repaired);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint64_t(proc, proc_op, &cs->cs_ignored);
	if (unlikely(rc != 0))
		return rc;

	return crt_proc_uint64_t(proc, proc_op, &cs->cs_failed);
}

static int
crt_proc_struct_chk_query_target(crt_proc_t proc, crt_proc_op_t proc_op,
				 struct chk_query_target *target)
{
	int	rc;

	rc = crt_proc_d_rank_t(proc, proc_op, &target->cqt_rank);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &target->cqt_tgt);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &target->cqt_ins_status);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &target->cqt_padding);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_struct_chk_statistics(proc, proc_op, &target->cqt_statistics);
	if (unlikely(rc != 0))
		return rc;

	return crt_proc_struct_chk_time(proc, proc_op, &target->cqt_time);
}

static int
crt_proc_struct_chk_query_pool_shard(crt_proc_t proc, crt_proc_op_t proc_op,
				     struct chk_query_pool_shard *shard)
{
	int	rc;
	int	i;

	rc = crt_proc_uuid_t(proc, proc_op, &shard->cqps_uuid);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &shard->cqps_status);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &shard->cqps_phase);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_struct_chk_statistics(proc, proc_op, &shard->cqps_statistics);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_struct_chk_time(proc, proc_op, &shard->cqps_time);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &shard->cqps_rank);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &shard->cqps_target_nr);
	if (unlikely(rc != 0))
		return rc;

	if (FREEING(proc_op)) {
		D_FREE(shard->cqps_targets);
		return 0;
	}

	if (DECODING(proc_op)) {
		D_ALLOC_ARRAY(shard->cqps_targets, shard->cqps_target_nr);
		if (shard->cqps_targets == NULL)
			return -DER_NOMEM;
	}

	for (i = 0; i < shard->cqps_target_nr; i++) {
		rc = crt_proc_struct_chk_query_target(proc, proc_op, &shard->cqps_targets[i]);
		if (unlikely(rc != 0)) {
			if (DECODING(proc_op))
				D_FREE(shard->cqps_targets);
			return rc;
		}
	}

	return 0;
}

static int
crp_proc_struct_rdb_clue(crt_proc_t proc, crt_proc_op_t proc_op, struct rdb_clue *rdb)
{
	int	rc;

	rc = crt_proc_uint64_t(proc, proc_op, &rdb->bcl_term);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_int32_t(proc, proc_op, &rdb->bcl_vote);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_d_rank_t(proc, proc_op, &rdb->bcl_self);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint64_t(proc, proc_op, &rdb->bcl_last_index);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint64_t(proc, proc_op, &rdb->bcl_last_term);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint64_t(proc, proc_op, &rdb->bcl_base_index);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint64_t(proc, proc_op, &rdb->bcl_base_term);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_d_rank_list_t(proc, proc_op, &rdb->bcl_replicas);
	if (unlikely(rc != 0))
		return rc;

	return crt_proc_uint64_t(proc, proc_op, &rdb->bcl_oid_next);
}

static int
crt_proc_struct_ds_pool_svc_clue(crt_proc_t proc, crt_proc_op_t proc_op,
				 struct ds_pool_svc_clue *psc)
{
	int	rc;

	rc = crp_proc_struct_rdb_clue(proc, proc_op, &psc->psc_db_clue);
	if (unlikely(rc != 0))
		return rc;

	return crt_proc_uint32_t(proc, proc_op, &psc->psc_map_version);
}

static int
crt_proc_struct_ds_pool_clue(crt_proc_t proc, crt_proc_op_t proc_op, struct ds_pool_clue *clue)
{
	int	rc;
	int	i;

	rc = crt_proc_uuid_t(proc, proc_op, &clue->pc_uuid);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_d_rank_t(proc, proc_op, &clue->pc_rank);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &clue->pc_dir);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_int32_t(proc, proc_op, &clue->pc_rc);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &clue->pc_label_len);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &clue->pc_tgt_nr);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &clue->pc_phase);
	if (unlikely(rc != 0))
		return rc;

	if (FREEING(proc_op))
		goto out;

	if (clue->pc_rc > 0) {
		if (DECODING(proc_op)) {
			D_ALLOC_PTR(clue->pc_svc_clue);
			if (clue->pc_svc_clue == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}

		rc = crt_proc_struct_ds_pool_svc_clue(proc, proc_op, clue->pc_svc_clue);
		if (unlikely(rc != 0))
			goto out;
	}

	if (clue->pc_label_len > 0) {
		if (DECODING(proc_op)) {
			D_ALLOC(clue->pc_label, clue->pc_label_len + 1);
			if (clue->pc_label == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}

		rc = crt_proc_memcpy(proc, proc_op, clue->pc_label, clue->pc_label_len);
		if (unlikely(rc != 0))
			goto out;
	}

	if (clue->pc_tgt_nr > 0) {
		if (DECODING(proc_op)) {
			D_ALLOC_ARRAY(clue->pc_tgt_status, clue->pc_tgt_nr);
			if (clue->pc_tgt_status == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}

		for (i = 0; i < clue->pc_tgt_nr; i++) {
			rc = crt_proc_uint32_t(proc, proc_op, &clue->pc_tgt_status[i]);
			if (unlikely(rc != 0))
				goto out;
		}
	}

out:
	if (unlikely(rc != 0 && DECODING(proc_op)) || FREEING(proc_op))
		ds_pool_clue_fini(clue);

	return rc;
}

static int
crt_proc_struct_chk_pool_mbs(crt_proc_t proc, crt_proc_op_t proc_op, struct chk_pool_mbs *mbs)
{
	int	rc;
	int	i;

	rc = crt_proc_d_rank_t(proc, proc_op, &mbs->cpm_rank);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &mbs->cpm_tgt_nr);
	if (unlikely(rc != 0))
		return rc;

	if (FREEING(proc_op))
		goto out;

	if (mbs->cpm_tgt_nr > 0) {
		if (DECODING(proc_op)) {
			D_ALLOC_ARRAY(mbs->cpm_tgt_status, mbs->cpm_tgt_nr);
			if (mbs->cpm_tgt_status == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}

		for (i = 0; i < mbs->cpm_tgt_nr; i++) {
			rc = crt_proc_uint32_t(proc, proc_op, &mbs->cpm_tgt_status[i]);
			if (unlikely(rc != 0))
				goto out;
		}
	}

out:
	if (unlikely(rc != 0 && DECODING(proc_op)) || FREEING(proc_op))
		D_FREE(mbs->cpm_tgt_status);

	return rc;
}

static int
crt_proc_struct_rsvc_hint(crt_proc_t proc, crt_proc_op_t proc_op,
			  struct rsvc_hint *hint)
{
	int rc;

	rc = crt_proc_uint32_t(proc, proc_op, &hint->sh_flags);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, proc_op, &hint->sh_rank);
	if (rc != 0)
		return -DER_HG;

	return crt_proc_uint64_t(proc, proc_op, &hint->sh_term);
}

CRT_RPC_DEFINE(chk_start, DAOS_ISEQ_CHK_START, DAOS_OSEQ_CHK_START);
CRT_RPC_DEFINE(chk_stop, DAOS_ISEQ_CHK_STOP, DAOS_OSEQ_CHK_STOP);
CRT_RPC_DEFINE(chk_query, DAOS_ISEQ_CHK_QUERY, DAOS_OSEQ_CHK_QUERY);
CRT_RPC_DEFINE(chk_mark, DAOS_ISEQ_CHK_MARK, DAOS_OSEQ_CHK_MARK);
CRT_RPC_DEFINE(chk_act, DAOS_ISEQ_CHK_ACT, DAOS_OSEQ_CHK_ACT);
CRT_RPC_DEFINE(chk_cont_list, DAOS_ISEQ_CHK_CONT_LIST, DAOS_OSEQ_CHK_CONT_LIST);
CRT_RPC_DEFINE(chk_pool_start, DAOS_ISEQ_CHK_POOL_START, DAOS_OSEQ_CHK_POOL_START);
CRT_RPC_DEFINE(chk_pool_mbs, DAOS_ISEQ_CHK_POOL_MBS, DAOS_OSEQ_CHK_POOL_MBS);
CRT_RPC_DEFINE(chk_report, DAOS_ISEQ_CHK_REPORT, DAOS_OSEQ_CHK_REPORT);
CRT_RPC_DEFINE(chk_rejoin, DAOS_ISEQ_CHK_REJOIN, DAOS_OSEQ_CHK_REJOIN);
