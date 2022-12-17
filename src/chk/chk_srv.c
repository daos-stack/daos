/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(chk)

#include <daos/rpc.h>
#include <daos/btree.h>
#include <daos/btree_class.h>
#include <daos_srv/daos_chk.h>
#include <daos_srv/daos_engine.h>

#include "chk_internal.h"

static void
ds_chk_start_hdlr(crt_rpc_t *rpc)
{
	struct chk_start_in	*csi = crt_req_get(rpc);
	struct chk_start_out	*cso = crt_reply_get(rpc);
	struct ds_pool_clues	 clues = { 0 };
	int			 rc;

	rc = chk_engine_start(csi->csi_gen, csi->csi_ranks.ca_count, csi->csi_ranks.ca_arrays,
			      csi->csi_policies.ca_count, csi->csi_policies.ca_arrays,
			      csi->csi_uuids.ca_count, csi->csi_uuids.ca_arrays, csi->csi_api_flags,
			      csi->csi_phase, csi->csi_leader_rank, csi->csi_flags, &clues);

	cso->cso_status = rc;
	cso->cso_rank = dss_self_rank();
	cso->cso_clues.ca_count = clues.pcs_len;
	cso->cso_clues.ca_arrays = clues.pcs_array;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("Failed to reply check start: "DF_RC"\n", DP_RC(rc));

	/*
	 * If the check engine and the leader are on the same rank, we will not go through
	 * CRT proc function that will copy the clues into related RPC reply buffer. Then
	 * has to keep the clues for a while until the check leader completed aggregating
	 * the result for this local engine. And then the check leader will release it.
	 */
	if (cso->cso_status < 0 || !chk_is_on_leader(csi->csi_gen, csi->csi_leader_rank, true))
		ds_pool_clues_fini(&clues);
}

static void
ds_chk_stop_hdlr(crt_rpc_t *rpc)
{
	struct chk_stop_in	*csi = crt_req_get(rpc);
	struct chk_stop_out	*cso = crt_reply_get(rpc);
	int			 rc;

	rc = chk_engine_stop(csi->csi_gen, csi->csi_uuids.ca_count, csi->csi_uuids.ca_arrays);

	cso->cso_status = rc;
	cso->cso_rank = dss_self_rank();
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("Failed to reply check stop: "DF_RC"\n", DP_RC(rc));
}

static void
ds_chk_query_hdlr(crt_rpc_t *rpc)
{
	struct chk_query_in		*cqi = crt_req_get(rpc);
	struct chk_query_out		*cqo = crt_reply_get(rpc);
	struct chk_query_pool_shard	*shards = NULL;
	uint32_t			 shard_nr = 0;
	int				 rc;

	rc = chk_engine_query(cqi->cqi_gen, cqi->cqi_uuids.ca_count, cqi->cqi_uuids.ca_arrays,
			      &shard_nr, &shards);
	if (rc != 0) {
		cqo->cqo_status = rc;
		cqo->cqo_shards.ca_count = 0;
		cqo->cqo_shards.ca_arrays = NULL;
	} else {
		cqo->cqo_status = 0;
		cqo->cqo_shards.ca_count = shard_nr;
		cqo->cqo_shards.ca_arrays = shards;
	}

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("Failed to reply check query: "DF_RC"\n", DP_RC(rc));

	/*
	 * If the check engine and the leader are on the same rank, we will not go through
	 * CRT proc function that will copy the shards into related RPC reply buffer. Then
	 * has to keep the shards for a while until the check leader completed aggregating
	 * the result for this local engine. And then the check leader will release it.
	 */
	if (cqo->cqo_status < 0 || !chk_is_on_leader(cqi->cqi_gen, -1, false))
		chk_query_free(shards, shard_nr);
}

static void
ds_chk_mark_hdlr(crt_rpc_t *rpc)
{
	struct chk_mark_in	*cmi = crt_req_get(rpc);
	struct chk_mark_out	*cmo = crt_reply_get(rpc);
	int			 rc;

	rc = chk_engine_mark_rank_dead(cmi->cmi_gen, cmi->cmi_rank, cmi->cmi_version);

	cmo->cmo_status = rc;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("Failed to reply check mark rank dead: "DF_RC"\n", DP_RC(rc));
}

static void
ds_chk_act_hdlr(crt_rpc_t *rpc)
{
	struct chk_act_in	*cai = crt_req_get(rpc);
	struct chk_act_out	*cao = crt_reply_get(rpc);
	int			 rc;

	rc = chk_engine_act(cai->cai_gen, cai->cai_seq, cai->cai_cla, cai->cai_act, cai->cai_flags);

	cao->cao_status = rc;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("Failed to reply check act: "DF_RC"\n", DP_RC(rc));
}

static void
ds_chk_cont_list_hdlr(crt_rpc_t *rpc)
{
	struct chk_cont_list_in		*ccli = crt_req_get(rpc);
	struct chk_cont_list_out	*cclo = crt_reply_get(rpc);
	uuid_t				*conts = NULL;
	d_rank_t			 myrank = dss_self_rank();
	uint32_t			 count = 0;
	int				 rc = 0;
	bool				 remote;

	if (ccli->ccli_rank != myrank)
		remote = true;
	else
		remote = false;

	rc = chk_engine_cont_list(ccli->ccli_gen, ccli->ccli_pool, &conts, &count);

	cclo->cclo_status = rc;
	cclo->cclo_rank = myrank;
	cclo->cclo_conts.ca_arrays = conts;
	cclo->cclo_conts.ca_count = count;

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("Failed to reply check cont list: "DF_RC"\n", DP_RC(rc));

	/*
	 * If the check engine and the PS leader are on the same rank, we will not go
	 * through CRT proc function that will copy the containers' uuids into related
	 * RPC reply buffer. Then has to keep the containers' uuids for a while until
	 * the PS leader completed aggregating the result for the local shard. And then
	 * the PS leader will release the buffer.
	 */
	if (cclo->cclo_status < 0 || remote)
		D_FREE(conts);
}

static void
ds_chk_pool_start_hdlr(crt_rpc_t *rpc)
{
	struct chk_pool_start_in	*cpsi = crt_req_get(rpc);
	struct chk_pool_start_out	*cpso = crt_reply_get(rpc);
	int				 rc;

	rc = chk_engine_pool_start(cpsi->cpsi_gen, cpsi->cpsi_pool, cpsi->cpsi_phase);

	cpso->cpso_status = rc;
	cpso->cpso_rank = dss_self_rank();
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("Failed to reply check pool start: "DF_RC"\n", DP_RC(rc));
}

static void
ds_chk_pool_mbs_hdlr(crt_rpc_t *rpc)
{
	struct chk_pool_mbs_in	*cpmi = crt_req_get(rpc);
	struct chk_pool_mbs_out	*cpmo = crt_reply_get(rpc);
	int			 rc;

	rc = chk_engine_pool_mbs(cpmi->cpmi_gen, cpmi->cpmi_pool, cpmi->cpmi_phase,
				 cpmi->cpmi_label, cpmi->cpmi_label_seq, cpmi->cpmi_flags,
				 cpmi->cpmi_targets.ca_count, cpmi->cpmi_targets.ca_arrays,
				 &cpmo->cpmo_hint);

	cpmo->cpmo_status = rc;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("Failed to reply check pool mbs: "DF_RC"\n", DP_RC(rc));
}

static void
ds_chk_report_hdlr(crt_rpc_t *rpc)
{
	struct chk_report_in	*cri = crt_req_get(rpc);
	struct chk_report_out	*cro = crt_reply_get(rpc);
	struct chk_report_unit	 cru;
	int			 rc;

	cru.cru_gen = cri->cri_gen;
	cru.cru_cla = cri->cri_ics_class;
	cru.cru_act = cri->cri_ics_action;
	cru.cru_target = cri->cri_target;
	cru.cru_rank = cri->cri_rank;
	cru.cru_option_nr = cri->cri_options.ca_count;
	cru.cru_detail_nr = cri->cri_details.ca_count;
	cru.cru_pool = &cri->cri_pool;
	cru.cru_pool_label = cri->cri_pool_label;
	cru.cru_cont = &cri->cri_cont;
	cru.cru_cont_label = cri->cri_cont_label;
	cru.cru_obj = &cri->cri_obj;
	cru.cru_dkey = &cri->cri_dkey;
	cru.cru_akey = &cri->cri_akey;
	cru.cru_msg = cri->cri_msg;
	cru.cru_options = cri->cri_options.ca_arrays;
	cru.cru_details = cri->cri_details.ca_arrays;
	cru.cru_result = cri->cri_ics_result;
	cro->cro_seq = cri->cri_seq;

	rc = chk_leader_report(&cru, &cro->cro_seq, NULL);

	cro->cro_status = rc;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("Failed to reply check report: "DF_RC"\n", DP_RC(rc));
}

static void
ds_chk_rejoin_hdlr(crt_rpc_t *rpc)
{
	struct chk_rejoin_in	*cri = crt_req_get(rpc);
	struct chk_rejoin_out	*cro = crt_reply_get(rpc);
	uuid_t			*pools = NULL;
	int			 pool_nr = 0;
	int			 rc;

	rc = chk_leader_rejoin(cri->cri_gen, cri->cri_rank, &pool_nr, &pools);

	cro->cro_status = rc;
	if (rc == 0) {
		cro->cro_pools.ca_count = pool_nr;
		cro->cro_pools.ca_arrays = pools;
	}

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("Failed to reply check rejoin: "DF_RC"\n", DP_RC(rc));

	D_FREE(pools);
}

static int
ds_chk_init(void)
{
	int	rc;

	rc = dbtree_class_register(DBTREE_CLASS_CHK_POOL, 0, &chk_pool_ops);
	if (rc != 0)
		goto out;

	rc = dbtree_class_register(DBTREE_CLASS_CHK_RANK, 0, &chk_rank_ops);
	if (rc != 0)
		goto out;

	rc = dbtree_class_register(DBTREE_CLASS_CHK_PA, 0, &chk_pending_ops);
	if (rc != 0)
		goto out;

	rc = dbtree_class_register(DBTREE_CLASS_CHK_CONT, 0, &chk_cont_ops);
	if (rc != 0)
		goto out;

	rc = chk_iv_init();

out:
	return rc;
}

static int
ds_chk_fini(void)
{
	return chk_iv_fini();
}

static int
ds_chk_setup(void)
{
	int	rc;

	/* Do NOT move chk_vos_init into ds_chk_init, because sys_db is not ready at that time. */
	chk_vos_init();

	rc = chk_leader_init();
	if (rc != 0)
		goto out_vos;

	rc = chk_engine_init();
	if (rc != 0)
		goto out_leader;

	/*
	 * Currently, we do NOT support leader to rejoin the former check instance. Because we do
	 * not support leader switch, during current leader down time, the reported inconsistency
	 * and related repair result are lost. Under such case, the admin has to stop and restart
	 * the check explicitly.
	 */

	rc = dss_ult_create(chk_engine_rejoin, NULL, DSS_XS_SYS, 0, 0, NULL);
	D_ASSERT(rc == 0);

	goto out_done;

out_leader:
	chk_leader_fini();
out_vos:
	chk_vos_fini();
out_done:
	return rc;
}

static int
ds_chk_cleanup(void)
{
	chk_engine_pause();
	chk_leader_pause();
	chk_engine_fini();
	chk_leader_fini();
	chk_vos_fini();

	return 0;
}

#define X(a, b, c, d, e)	\
{				\
	.dr_opc       = a,	\
	.dr_hdlr      = d,	\
	.dr_corpc_ops = e,	\
}

static struct daos_rpc_handler chk_handlers[] = {
	CHK_PROTO_SRV_RPC_LIST,
};

#undef X

struct dss_module chk_module = {
	.sm_name		= "chk",
	.sm_mod_id		= DAOS_CHK_MODULE,
	.sm_ver			= DAOS_CHK_VERSION,
	.sm_init		= ds_chk_init,
	.sm_fini		= ds_chk_fini,
	.sm_setup		= ds_chk_setup,
	.sm_cleanup		= ds_chk_cleanup,
	.sm_proto_count		= 1,
	.sm_proto_fmt		= &chk_proto_fmt,
	.sm_cli_count		= 0,
	.sm_handlers		= chk_handlers,
};
