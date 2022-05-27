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

static daos_unit_oid_t		chk_dummy_obj = { 0 };
static daos_key_t		chk_dummy_key = { 0 };

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

		if (out_result->cso_status == 0)
			out_result->cso_status = out_source->cso_status;
	} else {
		rc = ccrp->cb(ccrp->args, out_source->cso_rank, out_source->cso_phase,
			      out_source->cso_status, out_result->cso_clues.ca_arrays,
			      out_result->cso_clues.ca_count);
		if (rc != 0 && out_result->cso_status == 0)
			out_result->cso_status = rc;
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

		if (out_result->cso_status == 0)
			out_result->cso_status = out_source->cso_status;
	} else if (ccrp->cb != NULL && out_source->cso_status > 0) {
		rc = ccrp->cb(ccrp->args, out_source->cso_rank, 0, out_source->cso_status, NULL, 0);
		if (rc != 0 && out_result->cso_status == 0)
			out_result->cso_status = rc;
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
		D_ERROR("Failed to check query rank dead with gen "DF_X64": "DF_RC"\n",
			in_source->cqi_gen, DP_RC(out_source->cqo_status));

		if (out_result->cqo_status == 0)
			out_result->cqo_status = out_source->cqo_status;
	} else {
		rc = ccrp->cb(ccrp->args, 0, 0, out_source->cqo_status,
			      out_result->cqo_shards.ca_arrays, out_result->cqo_shards.ca_count);
		if (rc != 0 && out_result->cqo_status == 0)
			out_result->cqo_status = rc;
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

static inline int
chk_co_rpc_prepare(d_rank_list_t *rank_list, crt_opcode_t opc, struct chk_co_rpc_priv *priv,
		   crt_rpc_t **req)
{
	int	topo;

	topo = crt_tree_topo(CRT_TREE_KNOMIAL, 32);
	opc = DAOS_RPC_OPCODE(opc, DAOS_CHK_MODULE, DAOS_CHK_VERSION);

	return crt_corpc_req_create(dss_get_module_info()->dmi_ctx, NULL, rank_list, opc,
				    NULL, priv, CRT_RPC_FLAG_FILTER_INVERT, topo, req);
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
		 uint32_t policy_nr, struct chk_policy **policies, uint32_t pool_nr,
		 uuid_t pools[], uint32_t flags, int32_t phase, d_rank_t leader,
		 chk_co_rpc_cb_t start_cb, void *args)
{
	struct chk_co_rpc_priv	 ccrp;
	crt_rpc_t		*req = NULL;
	struct chk_start_in	*csi;
	struct chk_start_out	*cso;
	int			 rc;

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
	csi->csi_ranks.ca_count = rank_nr;
	csi->csi_ranks.ca_arrays = ranks;
	csi->csi_policies.ca_count = policy_nr;
	csi->csi_policies.ca_arrays = (void *)policies;
	csi->csi_uuids.ca_count = pool_nr;
	csi->csi_uuids.ca_arrays = pools;

	rc = dss_rpc_send(req);
	if (rc != 0)
		goto out;

	cso = crt_reply_get(req);
	rc = cso->cso_status;

out:
	if (req != NULL) {
		if (rc < 0)
			chk_stop_remote(rank_list, gen, pool_nr, pools, NULL, NULL);

		crt_req_decref(req);
	}

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Rank %u start DAOS check with gen "DF_X64", flags %x, phase %u: "DF_RC"\n",
		 leader, gen, flags, phase, DP_RC(rc));

	return rc;
}

int
chk_stop_remote(d_rank_list_t *rank_list, uint64_t gen, uint32_t pool_nr, uuid_t pools[],
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
	rc = cso->cso_status;

out:
	if (req != NULL)
		crt_req_decref(req);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Rank %u stop DAOS check with gen "DF_X64", pool_nr %u: "DF_RC"\n",
		 dss_self_rank(), gen, pool_nr, DP_RC(rc));

	return rc;
}

int
chk_query_remote(d_rank_list_t *rank_list, uint64_t gen, uint32_t pool_nr, uuid_t pools[],
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
	rc = cqo->cqo_status;

out:
	if (req != NULL)
		crt_req_decref(req);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Rank %u query DAOS check with gen "DF_X64", pool_nr %u: "DF_RC"\n",
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
		 "Rank %u as dead for DAOS check with gen "DF_X64": "DF_RC"\n",
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

int chk_report_remote(d_rank_t leader, uint64_t gen, uint32_t cla, uint32_t act, int32_t result,
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
		cri->cri_obj = chk_dummy_obj;

	if (dkey != NULL)
		cri->cri_dkey = *dkey;
	else
		cri->cri_dkey = chk_dummy_key;

	if (akey != NULL)
		cri->cri_akey = *akey;
	else
		cri->cri_akey = chk_dummy_key;

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
		 "result %d, obj "DF_UOID", dkey "DF_KEY", akey "DF_KEY", msg %s, got seq "
		 DF_X64": "DF_RC"\n", rank, leader, gen, cla, act, result,
		 DP_UOID(obj != NULL ? *obj : chk_dummy_obj),
		 DP_KEY(dkey != NULL ? dkey : &chk_dummy_key),
		 DP_KEY(akey != NULL ? akey : &chk_dummy_key), msg, *seq, DP_RC(rc));

	return rc;
}

int
chk_rejoin_remote(d_rank_t leader, uint64_t gen, d_rank_t rank, uint32_t phase)
{
	crt_rpc_t		*req;
	struct chk_rejoin_in	*cri;
	struct chk_rejoin_out	*cro;
	int			 rc;

	rc = chk_sg_rpc_prepare(leader, CHK_REJOIN, &req);
	if (rc != 0)
		goto out;

	cri = crt_req_get(req);
	cri->cri_gen = gen;
	cri->cri_rank = rank;
	cri->cri_phase = phase;

	rc = dss_rpc_send(req);
	if (rc != 0)
		goto out;

	cro = crt_reply_get(req);
	rc = cro->cro_status;

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

	rc = crt_proc_uint64_t(proc, proc_op, &rdb->bcl_oid_next);
	if (unlikely(rc != 0))
		return rc;

	return 0;
}

static int
crt_proc_struct_ds_pool_svc_clue(crt_proc_t proc, crt_proc_op_t proc_op,
				 struct ds_pool_svc_clue *psc)
{
	int	rc;

	rc = crp_proc_struct_rdb_clue(proc, proc_op, &psc->psc_db_clue);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &psc->psc_map_version);
	if (unlikely(rc != 0))
		return rc;

	return 0;
}

static int
crt_proc_struct_ds_pool_clue(crt_proc_t proc, crt_proc_op_t proc_op, struct ds_pool_clue *clue)
{
	int	rc;

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

	if (clue->pc_rc > 0) {
		if (FREEING(proc_op))
			goto out;

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
		if (FREEING(proc_op))
			goto out;

		if (DECODING(proc_op)) {
			D_ALLOC(clue->pc_label, clue->pc_label_len + 1);
			if (clue->pc_label == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}

		rc = crt_proc_memcpy(proc, proc_op, clue->pc_label, clue->pc_label_len);
		if (unlikely(rc != 0))
			goto out;
	}

out:
	if (unlikely(rc != 0 && DECODING(proc_op)) || FREEING(proc_op))
		ds_pool_clue_fini(clue);

	return rc;
}

CRT_RPC_DEFINE(chk_start, DAOS_ISEQ_CHK_START, DAOS_OSEQ_CHK_START);
CRT_RPC_DEFINE(chk_stop, DAOS_ISEQ_CHK_STOP, DAOS_OSEQ_CHK_STOP);
CRT_RPC_DEFINE(chk_query, DAOS_ISEQ_CHK_QUERY, DAOS_OSEQ_CHK_QUERY);
CRT_RPC_DEFINE(chk_mark, DAOS_ISEQ_CHK_MARK, DAOS_OSEQ_CHK_MARK);
CRT_RPC_DEFINE(chk_act, DAOS_ISEQ_CHK_ACT, DAOS_OSEQ_CHK_ACT);
CRT_RPC_DEFINE(chk_report, DAOS_ISEQ_CHK_REPORT, DAOS_OSEQ_CHK_REPORT);
CRT_RPC_DEFINE(chk_rejoin, DAOS_ISEQ_CHK_REJOIN, DAOS_OSEQ_CHK_REJOIN);
