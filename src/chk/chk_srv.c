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

static int
chk_engine_start(uint64_t gen, uint32_t rank_nr, d_rank_t *ranks,
		 uint32_t policy_nr, struct chk_policy **policies, uint32_t pool_nr,
		 uuid_t pools[], uint32_t flags, int32_t exp_phase, d_rank_t leader,
		 uint32_t *cur_phase, struct ds_pool_clues *clues)
{
	/* XXX: to be implemented in subsequent patch. */
	return 0;
}

static int
chk_engine_stop(uint64_t gen, uint32_t pool_nr, uuid_t pools[])
{
	/* XXX: to be implemented in subsequent patch. */
	return 0;
}

static int
chk_engine_query(uint64_t gen, uint32_t pool_nr, uuid_t pools[],
		 uint32_t *shard_nr, struct chk_query_pool_shard **shards)
{
	/* XXX: to be implemented in subsequent patch. */
	return 0;
}

static int
chk_engine_mark_rank_dead(uint64_t gen, d_rank_t rank, uint32_t version)
{
	/* XXX: to be implemented in subsequent patch. */
	return 0;
}

static int
chk_engine_act(uint64_t gen, uint64_t seq, uint32_t cla, uint32_t act, uint32_t flags)
{
	/* XXX: to be implemented in subsequent patch. */
	return 0;
}

static void
ds_chk_start_hdlr(crt_rpc_t *rpc)
{
	struct chk_start_in	*csi = crt_req_get(rpc);
	struct chk_start_out	*cso = crt_reply_get(rpc);
	struct ds_pool_clues	 clues = { 0 };
	uint32_t		 phase = 0;
	int			 rc;

	rc = chk_engine_start(csi->csi_gen, csi->csi_ranks.ca_count, csi->csi_ranks.ca_arrays,
			      csi->csi_policies.ca_count,
			      (struct chk_policy **)csi->csi_policies.ca_arrays,
			      csi->csi_uuids.ca_count, csi->csi_uuids.ca_arrays,
			      csi->csi_flags, csi->csi_phase, csi->csi_leader_rank, &phase, &clues);

	cso->cso_status = rc;
	cso->cso_rank = dss_self_rank();
	cso->cso_phase = phase;
	cso->cso_clues.ca_count = clues.pcs_len;
	cso->cso_clues.ca_arrays = clues.pcs_array;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("Failed to reply check start: "DF_RC"\n", DP_RC(rc));

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
ds_chk_report_hdlr(crt_rpc_t *rpc)
{
}

static void
ds_chk_rejoin_hdlr(crt_rpc_t *rpc)
{
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

out_vos:
	chk_vos_fini();

	return rc;
}

static int
ds_chk_cleanup(void)
{
	chk_leader_pause();
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
