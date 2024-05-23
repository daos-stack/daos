/**
 * (C) Copyright 2022-2024 Intel Corporation.
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

static int
chk_start_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct chk_start_in	*in_source = crt_req_get(source);
	struct chk_start_out	*out_source = crt_reply_get(source);
	struct chk_start_out	*out_result = crt_reply_get(result);
	struct ds_pool_clue	*clues;
	d_rank_t		*ranks;
	uint32_t		 cap;
	uint32_t		 nr;
	int			 i;

	if (out_source->cso_status < 0) {
		D_ERROR("Failed to check start with gen "DF_X64": %d\n",
			in_source->csi_gen, out_source->cso_status);

		if (out_result->cso_status == 0)
			out_result->cso_status = out_source->cso_status;

		return 0;
	}

	if (out_source->cso_clues.ca_count == 0)
		goto cmp_ranks;

	nr = out_source->cso_clues.ca_count + out_result->cso_clues.ca_count;
	if (nr > out_result->cso_clue_cap) {
		cap = out_result->cso_clue_cap > 0 ? out_result->cso_clue_cap : 1;
		while (cap < nr)
			cap <<= 1;

clue_again:
		D_REALLOC_ARRAY(clues, out_result->cso_clues.ca_arrays,
				out_result->cso_clue_cap, cap);
		if (clues == NULL) {
			if (cap > nr) {
				cap = nr;
				goto clue_again;
			}

			if (out_result->cso_status == 0)
				out_result->cso_status = -DER_NOMEM;

			return -DER_NOMEM;
		}

		out_result->cso_clues.ca_arrays = clues;
		out_result->cso_clue_cap = cap;
	}

	memcpy((struct ds_pool_clue *)out_result->cso_clues.ca_arrays +
	       out_result->cso_clues.ca_count, out_source->cso_clues.ca_arrays,
	       sizeof(*clues) * out_source->cso_clues.ca_count);
	out_result->cso_clues.ca_count = nr;

	/*
	 * pc_svc_clue/pc_label/pc_tgt_status are shared between out_source and out_result.
	 * Let's reset them in out_source to avoid being released when cleanup out_source.
	 */
	for (i = 0, clues = out_source->cso_clues.ca_arrays;
	     i < out_source->cso_clues.ca_count; i++, clues++) {
		clues->pc_label_len = 0;
		clues->pc_tgt_nr = 0;
		clues->pc_svc_clue = NULL;
		clues->pc_label = NULL;
		clues->pc_tgt_status = NULL;
	}

cmp_ranks:
	if (out_source->cso_cmp_ranks.ca_count == 0)
		return 0;

	nr = out_source->cso_cmp_ranks.ca_count + out_result->cso_cmp_ranks.ca_count;
	if (nr > out_result->cso_rank_cap) {
		cap = out_result->cso_rank_cap > 0 ? out_result->cso_rank_cap : 1;
		while (cap < nr)
			cap <<= 1;

rank_again:
		D_REALLOC_ARRAY(ranks, out_result->cso_cmp_ranks.ca_arrays,
				out_result->cso_rank_cap, cap);
		if (ranks == NULL) {
			if (cap > nr) {
				cap = nr;
				goto rank_again;
			}

			if (out_result->cso_status == 0)
				out_result->cso_status = -DER_NOMEM;

			return -DER_NOMEM;
		}

		out_result->cso_cmp_ranks.ca_arrays = ranks;
		out_result->cso_rank_cap = cap;
	}

	memcpy((d_rank_t *)out_result->cso_cmp_ranks.ca_arrays + out_result->cso_cmp_ranks.ca_count,
	       out_source->cso_cmp_ranks.ca_arrays,
	       sizeof(*ranks) * out_source->cso_cmp_ranks.ca_count);
	out_result->cso_cmp_ranks.ca_count = nr;

	return 0;
}

static int
chk_start_post_reply(crt_rpc_t *rpc, void *arg)
{
	struct chk_start_out	*cso = crt_reply_get(rpc);
	struct ds_pool_clues	 clues = { 0 };

	if (cso != NULL) {
		D_FREE(cso->cso_cmp_ranks.ca_arrays);

		clues.pcs_len = cso->cso_clues.ca_count;
		clues.pcs_array = cso->cso_clues.ca_arrays;
		ds_pool_clues_fini(&clues);
	}

	return 0;
}

static int
chk_stop_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct chk_stop_in	*in_source = crt_req_get(source);
	struct chk_stop_out	*out_source = crt_reply_get(source);
	struct chk_stop_out	*out_result = crt_reply_get(result);
	d_rank_t		*ranks;
	uint32_t		 cap;
	uint32_t		 nr;

	if (out_source->cso_status < 0) {
		D_ERROR("Failed to check stop with gen "DF_X64": %d\n",
			in_source->csi_gen, out_source->cso_status);

		if (out_result->cso_status == 0)
			out_result->cso_status = out_source->cso_status;

		return 0;
	}

	out_result->cso_flags |= out_source->cso_flags;

	if (out_source->cso_ranks.ca_count == 0)
		return 0;

	nr = out_source->cso_ranks.ca_count + out_result->cso_ranks.ca_count;
	if (nr > out_result->cso_cap) {
		cap = out_result->cso_cap > 0 ? out_result->cso_cap : 1;
		while (cap < nr)
			cap <<= 1;

again:
		D_REALLOC_ARRAY(ranks, out_result->cso_ranks.ca_arrays, out_result->cso_cap, cap);
		if (ranks == NULL) {
			if (cap > nr) {
				cap = nr;
				goto again;
			}

			if (out_result->cso_status == 0)
				out_result->cso_status = -DER_NOMEM;

			return -DER_NOMEM;
		}

		out_result->cso_ranks.ca_arrays = ranks;
		out_result->cso_cap = cap;
	}

	memcpy((d_rank_t *)out_result->cso_ranks.ca_arrays + out_result->cso_ranks.ca_count,
	       out_source->cso_ranks.ca_arrays, sizeof(*ranks) * out_source->cso_ranks.ca_count);
	out_result->cso_ranks.ca_count = nr;

	return 0;
}

static int
chk_stop_post_reply(crt_rpc_t *rpc, void *arg)
{
	struct chk_stop_out	*cso = crt_reply_get(rpc);

	if (cso != NULL)
		D_FREE(cso->cso_ranks.ca_arrays);

	return 0;
}

static int
chk_query_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct chk_query_in		*in_source = crt_req_get(source);
	struct chk_query_out		*out_source = crt_reply_get(source);
	struct chk_query_out		*out_result = crt_reply_get(result);
	struct chk_query_pool_shard	*shards;
	uint32_t			 cap;
	uint32_t			 nr;
	int				 i;

	if (out_source->cqo_status != 0) {
		D_ERROR("Failed to check query with gen "DF_X64": %d\n",
			in_source->cqi_gen, out_source->cqo_status);

		if (out_result->cqo_status == 0)
			out_result->cqo_status = out_source->cqo_status;

		return 0;
	}

	if (out_source->cqo_shards.ca_count == 0)
		return 0;

	nr = out_source->cqo_shards.ca_count + out_result->cqo_shards.ca_count;
	if (nr >out_result->cqo_cap) {
		cap = out_result->cqo_cap > 0 ? out_result->cqo_cap : 1;
		while (cap < nr)
			cap <<= 1;

again:
		D_REALLOC_ARRAY(shards, out_result->cqo_shards.ca_arrays, out_result->cqo_cap, cap);
		if (shards == NULL) {
			if (cap > nr) {
				cap = nr;
				goto again;
			}

			if (out_result->cqo_status == 0)
				out_result->cqo_status = -DER_NOMEM;

			return -DER_NOMEM;
		}

		out_result->cqo_shards.ca_arrays = shards;
		out_result->cqo_cap = cap;
	}

	memcpy((struct chk_query_pool_shard *)out_result->cqo_shards.ca_arrays +
	       out_result->cqo_shards.ca_count, out_source->cqo_shards.ca_arrays,
	       sizeof(*shards) * out_source->cqo_shards.ca_count);
	out_result->cqo_shards.ca_count = nr;

	chk_ins_merge_info(&out_result->cqo_ins_status, out_source->cqo_ins_status,
			   &out_result->cqo_ins_phase, out_source->cqo_ins_phase,
			   &out_result->cqo_gen, out_source->cqo_gen);

	/*
	 * cqps_target_nr and cqps_targets are shared between out_source and out_result.
	 * Let's reset them in out_source to avoid being released when cleanup out_source.
	 */
	for (i = 0, shards = out_source->cqo_shards.ca_arrays;
	     i < out_source->cqo_shards.ca_count; i++, shards++) {
		shards->cqps_target_nr = 0;
		shards->cqps_targets = NULL;
	}

	return 0;
}

static int
chk_query_post_reply(crt_rpc_t *rpc, void *arg)
{
	struct chk_query_out	*cqo = crt_reply_get(rpc);

	if (cqo != NULL)
		chk_query_free(cqo->cqo_shards.ca_arrays, cqo->cqo_shards.ca_count);

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
	uuid_t				*uuids;
	uint32_t			 cap;
	uint32_t			 nr;

	if (out_source->cclo_status < 0) {
		D_ERROR("Failed to check cont list with gen "DF_X64": %d\n",
			in_source->ccli_gen, out_source->cclo_status);

		if (out_result->cclo_status == 0)
			out_result->cclo_status = out_source->cclo_status;

		return 0;
	}

	if (out_source->cclo_conts.ca_count == 0)
		return 0;

	nr = out_source->cclo_conts.ca_count + out_result->cclo_conts.ca_count;
	if (nr > out_result->cclo_cap) {
		cap = out_result->cclo_cap > 0 ? out_result->cclo_cap : 1;
		while (cap < nr)
			cap <<= 1;

again:
		D_REALLOC_ARRAY(uuids, out_result->cclo_conts.ca_arrays, out_result->cclo_cap, cap);
		if (uuids == NULL) {
			if (cap > nr) {
				cap = nr;
				goto again;
			}

			if (out_result->cclo_status == 0)
				out_result->cclo_status = -DER_NOMEM;

			return -DER_NOMEM;
		}

		out_result->cclo_conts.ca_arrays = uuids;
		out_result->cclo_cap = cap;
	}

	memcpy((uuid_t *)out_result->cclo_conts.ca_arrays + out_result->cclo_conts.ca_count,
	       out_source->cclo_conts.ca_arrays, sizeof(uuid_t) * out_source->cclo_conts.ca_count);
	out_result->cclo_conts.ca_count = nr;

	return 0;
}

static int
chk_cont_list_post_reply(crt_rpc_t *rpc, void *arg)
{
	struct chk_cont_list_out	*cclo = crt_reply_get(rpc);

	if (cclo != NULL)
		D_FREE(cclo->cclo_conts.ca_arrays);

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
	.co_post_reply = chk_start_post_reply,
};

struct crt_corpc_ops chk_stop_co_ops = {
	.co_aggregate	= chk_stop_aggregator,
	.co_pre_forward	= NULL,
	.co_post_reply = chk_stop_post_reply,
};

struct crt_corpc_ops chk_query_co_ops = {
	.co_aggregate	= chk_query_aggregator,
	.co_pre_forward	= NULL,
	.co_post_reply = chk_query_post_reply,
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
	.co_post_reply = chk_cont_list_post_reply,
};

struct crt_corpc_ops chk_pool_start_co_ops = {
	.co_aggregate	= chk_pool_start_aggregator,
	.co_pre_forward	= NULL,
};

static inline int
chk_co_rpc_prepare(d_rank_list_t *rank_list, crt_opcode_t opc, crt_rpc_t **req, bool failout)
{
	uint32_t	flags = CRT_RPC_FLAG_FILTER_INVERT;

	if (failout)
		flags |= CRT_RPC_FLAG_CO_FAILOUT;

	return crt_corpc_req_create(dss_get_module_info()->dmi_ctx, NULL, rank_list,
				    DAOS_RPC_OPCODE(opc, DAOS_CHK_MODULE, DAOS_CHK_VERSION),
				    NULL, NULL, flags, crt_tree_topo(CRT_TREE_KNOMIAL, 32), req);
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
		 uuid_t iv_uuid, chk_co_rpc_cb_t start_cb, void *args)
{
	struct chk_co_rpc_cb_args	cb_args = { 0 };
	crt_rpc_t			*req = NULL;
	struct chk_start_in		*csi = NULL;
	struct chk_start_out		*cso = NULL;
	d_rank_t			*cmp_rank;
	int				 rc;
	int				 rc1;
	int				 i;

	rc = chk_co_rpc_prepare(rank_list, CHK_START, &req, true);
	if (rc != 0)
		goto out;

	csi = crt_req_get(req);
	csi->csi_gen = gen;
	csi->csi_flags = flags;
	csi->csi_phase = phase;
	csi->csi_leader_rank = leader;
	csi->csi_api_flags = api_flags;
	uuid_copy(csi->csi_iv_uuid, iv_uuid);
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
	if (cso->cso_status < 0)
		D_GOTO(out, rc = cso->cso_status);

	cb_args.cb_priv = args;
	cb_args.cb_data = cso->cso_clues.ca_arrays;
	cb_args.cb_nr = cso->cso_clues.ca_count;
	rc = start_cb(&cb_args);
	if (rc != 0)
		goto out;

	if (cso->cso_cmp_ranks.ca_arrays == NULL)
		return 0;

	cb_args.cb_data = NULL; /* unused data */
	cb_args.cb_nr = 0; /* unused nr */
	cb_args.cb_result = 1;
	for (i = 0, cmp_rank = cso->cso_cmp_ranks.ca_arrays; i < cso->cso_cmp_ranks.ca_count;
	     i++, cmp_rank++) {
		cb_args.cb_rank = *cmp_rank;
		rc = start_cb(&cb_args);
		if (rc != 0)
			goto out;
	}

out:
	if (req != NULL) {
		/*
		 * co_post_reply will not be automatically called on the root node of the corpc.
		 * Let's trigger it explicitly to release related buffer.
		 */
		chk_start_post_reply(req, NULL);

		if (rc < 0) {
			rc1 = chk_stop_remote(rank_list, gen, pool_nr, pools, NULL, NULL);
			if (rc1 < 0 && rc1 != -DER_NOTAPPLICABLE)
				D_ERROR("Failed to cleanup DAOS check with gen "DF_X64": "DF_RC"\n",
					gen, DP_RC(rc1));
		}

		crt_req_decref(req);
	}

	D_CDEBUG(rc < 0, DLOG_ERR, DLOG_INFO,
		 "Rank %u start checker, gen "DF_X64", flags %x, phase %d, iv "DF_UUIDF":"DF_RC"\n",
		 leader, gen, flags, phase, DP_UUID(iv_uuid), DP_RC(rc));

	return rc;
}

int
chk_stop_remote(d_rank_list_t *rank_list, uint64_t gen, int pool_nr, uuid_t pools[],
		chk_co_rpc_cb_t stop_cb, void *args)
{
	struct chk_co_rpc_cb_args	 cb_args = { 0 };
	crt_rpc_t			*req = NULL;
	struct chk_stop_in		*csi = NULL;
	struct chk_stop_out		*cso = NULL;
	d_rank_t			*rank;
	int				 rc;
	int				 i;

	rc = chk_co_rpc_prepare(rank_list, CHK_STOP, &req, false);
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
	if (cso->cso_status < 0)
		D_GOTO(out, rc = cso->cso_status);

	if (stop_cb == NULL)
		D_GOTO(out, rc = 0);

	if (cso->cso_ranks.ca_arrays == NULL)
		D_GOTO(out, rc = 0);

	cb_args.cb_priv = args;
	cb_args.cb_result = 1;
	cb_args.cb_flags = cso->cso_flags;
	for (i = 0, rank = cso->cso_ranks.ca_arrays; i < cso->cso_ranks.ca_count; i++, rank++) {
		cb_args.cb_rank = *rank;
		rc = stop_cb(&cb_args);
		if (rc != 0)
			goto out;
	}

out:
	if (req != NULL) {
		/*
		 * co_post_reply will not be automatically called on the root node of the corpc.
		 * Let's trigger it explicitly to release related buffer.
		 */
		chk_stop_post_reply(req, NULL);
		crt_req_decref(req);
	}

	D_CDEBUG(rc < 0, DLOG_ERR, DLOG_INFO,
		 "Rank %u stop DAOS check with gen "DF_X64", pool_nr %d: "DF_RC"\n",
		 dss_self_rank(), gen, pool_nr, DP_RC(rc));

	return rc;
}

int
chk_query_remote(d_rank_list_t *rank_list, uint64_t gen, int pool_nr, uuid_t pools[],
		 chk_co_rpc_cb_t query_cb, void *args)
{
	struct chk_co_rpc_cb_args	 cb_args = { 0 };
	crt_rpc_t			*req = NULL;
	struct chk_query_in		*cqi = NULL;
	struct chk_query_out		*cqo = NULL;
	int				 rc;

	rc = chk_co_rpc_prepare(rank_list, CHK_QUERY, &req, true);
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
	if (cqo->cqo_status < 0)
		D_GOTO(out, rc = cqo->cqo_status);

	cb_args.cb_priv = args;
	cb_args.cb_gen = gen;
	cb_args.cb_result = cqo->cqo_status;
	cb_args.cb_ins_status = cqo->cqo_ins_status;
	cb_args.cb_ins_phase = cqo->cqo_ins_phase;
	cb_args.cb_data = cqo->cqo_shards.ca_arrays;
	cb_args.cb_nr = cqo->cqo_shards.ca_count;
	rc = query_cb(&cb_args);

out:
	if (req != NULL) {
		/*
		 * co_post_reply will not be automatically called on the root node of the corpc.
		 * Let's trigger it explicitly to release related buffer.
		 */
		chk_query_post_reply(req, NULL);
		crt_req_decref(req);
	}

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

	rc = chk_co_rpc_prepare(rank_list, CHK_MARK, &req, false);
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
		rc = chk_co_rpc_prepare(rank_list, CHK_ACT, &req, false);
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
	struct chk_co_rpc_cb_args	 cb_args = { 0 };
	crt_rpc_t			*req = NULL;
	struct chk_cont_list_in		*ccli = NULL;
	struct chk_cont_list_out	*cclo = NULL;
	int				 rc;

	rc = ds_pool_bcast_create(dss_get_module_info()->dmi_ctx, pool, DAOS_CHK_MODULE,
				  CHK_CONT_LIST, DAOS_CHK_VERSION, &req, NULL, NULL, NULL);
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
	if (cclo->cclo_status < 0)
		D_GOTO(out, rc = cclo->cclo_status);

	cb_args.cb_priv = args;
	cb_args.cb_result = cclo->cclo_status;
	cb_args.cb_data = cclo->cclo_conts.ca_arrays;
	cb_args.cb_nr = cclo->cclo_conts.ca_count;
	rc = list_cb(&cb_args);

out:
	if (req != NULL) {
		/*
		 * co_post_reply will not be automatically called on the root node of the corpc.
		 * Let's trigger it explicitly to release related buffer.
		 */
		chk_cont_list_post_reply(req, NULL);
		crt_req_decref(req);
	}

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Rank %u DAOS check cont list for "DF_UUIDF" with gen "DF_X64": "DF_RC"\n",
		 dss_self_rank(), DP_UUID(pool->sp_uuid), gen, DP_RC(rc));

	return rc;
}

int
chk_pool_start_remote(d_rank_list_t *rank_list, uint64_t gen, uuid_t uuid, uint32_t phase,
		      uint32_t flags)
{
	crt_rpc_t			*req;
	struct chk_pool_start_in	*cpsi;
	struct chk_pool_start_out	*cpso;
	int				 rc;

	rc = chk_co_rpc_prepare(rank_list, CHK_POOL_START, &req, true);
	if (rc != 0)
		goto out;

	cpsi = crt_req_get(req);
	cpsi->cpsi_gen = gen;
	uuid_copy(cpsi->cpsi_pool, uuid);
	cpsi->cpsi_phase = phase;
	cpsi->cpsi_flags = flags;

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
		    uint64_t seq, uint32_t flags, uint32_t mbs_nr, struct chk_pool_mbs *mbs_array,
		    int *svc_rc, struct rsvc_hint *svc_hint)
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
	cpmi->cpmi_label_seq = seq;
	cpmi->cpmi_targets.ca_count = mbs_nr;
	cpmi->cpmi_targets.ca_arrays = mbs_array;

	rc = dss_rpc_send(req);
	if (rc != 0)
		goto out;

	cpmo = crt_reply_get(req);
	*svc_rc = cpmo->cpmo_status;
	*svc_hint = cpmo->cpmo_hint;

out:
	if (req != NULL)
		crt_req_decref(req);

	D_CDEBUG(rc != 0 || *svc_rc != 0, DLOG_ERR, DLOG_INFO,
		 "Sent pool ("DF_UUIDF") members and label %s ("
		 DF_X64") to rank %u with phase %d gen "DF_X64": %d/%d\n", DP_UUID(uuid),
		 label != NULL ? label : "(null)", seq, rank, phase, gen, rc, *svc_rc);

	return rc;
}

int chk_report_remote(d_rank_t leader, uint64_t gen, uint32_t cla, uint32_t act, int result,
		      d_rank_t rank, uint32_t target, uuid_t *pool, char *pool_label, uuid_t *cont,
		      char *cont_label, daos_unit_oid_t *obj, daos_key_t *dkey, daos_key_t *akey,
		      char *msg, uint32_t option_nr, uint32_t *options, uint32_t detail_nr,
		      d_sg_list_t *details, uint64_t seq)
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
	cri->cri_seq = seq;

	if (pool != NULL)
		uuid_copy(cri->cri_pool, *pool);
	else
		memset(cri->cri_pool, 0, sizeof(uuid_t));

	cri->cri_pool_label = pool_label;

	if (cont != NULL)
		uuid_copy(cri->cri_cont, *cont);
	else
		memset(cri->cri_cont, 0, sizeof(uuid_t));

	cri->cri_cont_label = cont_label;

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

out:
	if (req != NULL)
		crt_req_decref(req);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Rank %u report DAOS check to leader %u, gen "DF_X64", class %u, action %u, "
		 "result %d, "DF_UUIDF"/"DF_UUIDF", seq "DF_X64": "DF_RC"\n", rank, leader,
		 gen, cla, act, result, DP_UUID(pool), DP_UUID(cont), seq, DP_RC(rc));

	return rc;
}

int
chk_rejoin_remote(d_rank_t leader, uint64_t gen, d_rank_t rank, uuid_t iv_uuid, uint32_t *flags,
		  uint32_t *pool_nr, uuid_t **pools)
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
	uuid_copy(cri->cri_iv_uuid, iv_uuid);

	rc = dss_rpc_send(req);
	if (rc != 0)
		goto out;

	cro = crt_reply_get(req);
	rc = cro->cro_status;
	if (rc == 0 && cro->cro_pools.ca_count > 0) {
		*flags = cro->cro_flags;
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
		 "Rank %u rejoin DAOS check with leader %u, gen "DF_X64", iv "DF_UUIDF": "DF_RC"\n",
		 rank, leader, gen, DP_UUID(iv_uuid), DP_RC(rc));

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

	if (shard->cqps_target_nr == 0)
		return 0;

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

	rc = crt_proc_int32_t(proc, proc_op, &clue->pc_tgt_nr);
	if (unlikely(rc != 0))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &clue->pc_label_len);
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
