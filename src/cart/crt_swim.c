/*
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It implements the SWIM integration APIs.
 */
#define D_LOGFAC	DD_FAC(swim)
#define CRT_USE_GURT_FAC

#include <ctype.h>
#include "crt_internal.h"
#include "crt_internal_fns.h"

#define CRT_OPC_SWIM_VERSION	2
#define CRT_SWIM_FAIL_BASE	((CRT_OPC_SWIM_BASE >> 16) | \
				 (CRT_OPC_SWIM_VERSION << 4))
#define CRT_SWIM_FAIL_DROP_RPC	(CRT_SWIM_FAIL_BASE | 0x1)	/* id: 65057 */

/**
 * use this macro to determine if a fault should be injected at
 * a specific place
 */
#define CRT_SWIM_SHOULD_FAIL(fa, id)				\
	(crt_swim_should_fail && (crt_swim_fail_id == id) &&	\
	 D_SHOULD_FAIL(fa))

#define crt_proc_swim_id_t	crt_proc_uint64_t

#define CRT_ISEQ_RPC_SWIM	/* input fields */		 \
	((swim_id_t)		     (swim_id)		CRT_VAR) \
	((struct swim_member_update) (upds)		CRT_ARRAY)

/*
 * The excl_grp_ver field belongs to an exclusion detection protocol being
 * piggybacked on SWIM RPCs. This protocol enables a member to detect that it
 * has been excluded (due to inevitable false positive SWIM DEAD events) from
 * the primary group.
 *
 *   - When replying a SWIM RPC, each member sets excl_grp_ver to 0 if the
 *     sender belongs to the local primary group or to the local primary group
 *     version otherwise.
 *
 *   - When processing a SWIM RPC reply, each member compares nonzero
 *     excl_grp_ver values to its local primary group version (see TODO in
 *     crt_swim_cli_cb). If the former is greater than the latter, then this
 *     member has been excluded.
 *
 * (This exclusion detection protocol could be piggybacked on all RPCs, after
 * optimizing away the rank lookup when group versions match and speeding up
 * the rank lookup when group versions differ. The main difficulty is that we
 * would need to expand crt_common_hdr.)
 */
#define CRT_OSEQ_RPC_SWIM	/* output fields */		 \
	((int32_t)		     (rc)		CRT_VAR) \
	((uint32_t)		     (excl_grp_ver)	CRT_VAR) \
	((struct swim_member_update) (upds)		CRT_ARRAY)

static inline int
crt_proc_struct_swim_member_update(crt_proc_t proc, crt_proc_op_t proc_op,
				   struct swim_member_update *data)
{
	return crt_proc_memcpy(proc, proc_op, data, sizeof(*data));
}

CRT_RPC_DECLARE(crt_rpc_swim, CRT_ISEQ_RPC_SWIM, CRT_OSEQ_RPC_SWIM)
CRT_RPC_DEFINE(crt_rpc_swim,  CRT_ISEQ_RPC_SWIM, CRT_OSEQ_RPC_SWIM)

static bool	 crt_swim_should_fail;
static uint64_t	 crt_swim_fail_delay;
static uint64_t	 crt_swim_fail_hlc;
static swim_id_t crt_swim_fail_id;

static struct d_fault_attr_t *d_fa_swim_drop_rpc;

static void
crt_swim_fault_init(const char *args)
{
	char *s, *s_saved, *end, *save_ptr = NULL;

	D_STRNDUP(s_saved, args, strlen(args));
	s = s_saved;
	if (s == NULL)
		return;

	while ((s = strtok_r(s, ",", &save_ptr)) != NULL) {
		while (isspace(*s))
			s++; /* skip space */
		if (!strncasecmp(s, "delay=", 6)) {
			crt_swim_fail_delay = strtoul(s + 6, &end, 0);
			D_EMIT("CRT_SWIM_FAIL_DELAY=%lu\n", crt_swim_fail_delay);
		} else if (!strncasecmp(s, "rank=", 5)) {
			crt_swim_fail_id = strtoul(s + 5, &end, 0);
			D_EMIT("CRT_SWIM_FAIL_ID=%lu\n", crt_swim_fail_id);
		}
		s = NULL;
	}

	D_FREE(s_saved);
}

static void crt_swim_srv_cb(crt_rpc_t *rpc);

static struct crt_proto_rpc_format crt_swim_proto_rpc_fmt[] = {
	{
		.prf_flags	= CRT_RPC_FEAT_QUEUE_FRONT,
		.prf_req_fmt	= &CQF_crt_rpc_swim,
		.prf_hdlr	= crt_swim_srv_cb,
		.prf_co_ops	= NULL,
	}, {
		.prf_flags	= CRT_RPC_FEAT_QUEUE_FRONT,
		.prf_req_fmt	= &CQF_crt_rpc_swim,
		.prf_hdlr	= crt_swim_srv_cb,
		.prf_co_ops	= NULL,
	}
};

static struct crt_proto_format crt_swim_proto_fmt = {
	.cpf_name	= "swim",
	.cpf_ver	= CRT_OPC_SWIM_VERSION,
	.cpf_count	= ARRAY_SIZE(crt_swim_proto_rpc_fmt),
	.cpf_prf	= crt_swim_proto_rpc_fmt,
	.cpf_base	= CRT_OPC_SWIM_BASE,
};

enum swim_rpc_type {
	SWIM_RPC_PING = 0,
	SWIM_RPC_IREQ,
};

static const char *SWIM_RPC_TYPE_STR[] = {
	[SWIM_RPC_PING] = "PING",
	[SWIM_RPC_IREQ] = "IREQ",
};

static uint32_t
crt_swim_update_delays(struct crt_swim_membs *csm, uint64_t hlc,
		       swim_id_t from_id, uint32_t rcv_delay,
		       struct swim_member_update *upds, size_t nupds)
{
	struct crt_swim_target	*cst;
	uint32_t		 snd_delay = 0;
	int			 i;

	/* Update all piggybacked members with remote delays */
	crt_swim_csm_lock(csm);
	for (i = 0; i < nupds; i++) {
		struct swim_member_state *state = &upds[i].smu_state;
		swim_id_t id = upds[i].smu_id;

		D_CIRCLEQ_FOREACH(cst, &csm->csm_head, cst_link) {
			if (cst->cst_id == id) {
				uint32_t l = cst->cst_state.sms_delay;

				if (id == from_id) {
					l = l ? (l + rcv_delay) / 2 : rcv_delay;
					snd_delay = l;
				} else {
					uint32_t r = state->sms_delay;

					l = l ? (l + r) / 2 : r;
				}
				cst->cst_state.sms_delay = l;

				if (crt_swim_fail_delay &&
				    crt_swim_fail_id == id) {
					uint64_t d = crt_swim_fail_delay;

					crt_swim_fail_hlc = hlc -
							    crt_msec2hlc(l) +
							    crt_sec2hlc(d);
					crt_swim_fail_delay = 0;
				}
				break;
			}
		}
	}
	crt_swim_csm_unlock(csm);

	return snd_delay;
}

/*
 * If id belongs to the primary group, this function returns 0; otherwise, this
 * function returns the group version. Note that if the group version is 0,
 * that is, the primary group has not been initialized yet, this function
 * always returns 0.
 */
static uint32_t
crt_swim_lookup_id(swim_id_t id)
{
	struct crt_grp_priv	*grp_priv = crt_gdata.cg_grp->gg_primary_grp;
	d_rank_list_t		*membs;
	uint32_t		 grp_ver;

	D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);
	grp_ver = grp_priv->gp_membs_ver;
	membs = grp_priv_get_membs(grp_priv);
	if (membs) {
		/*
		 * TODO: See if there's a better way. This is okay for now
		 * since we should be performing this linear search only one or
		 * a few times per period.
		 */
		if (d_rank_in_rank_list(membs, id))
			grp_ver = 0;
	}
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
	return grp_ver;
}

static void crt_swim_srv_cb(crt_rpc_t *rpc)
{
	struct crt_rpc_priv	*rpc_priv = container_of(rpc, struct crt_rpc_priv, crp_pub);
	struct crt_grp_priv	*grp_priv = crt_gdata.cg_grp->gg_primary_grp;
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	struct swim_context	*ctx = csm->csm_ctx;
	struct crt_rpc_swim_in	*rpc_in = crt_req_get(rpc);
	struct crt_rpc_swim_out *rpc_out = crt_reply_get(rpc);
	enum swim_rpc_type	 rpc_type;
	swim_id_t		 self_id = swim_self_get(ctx);
	swim_id_t		 from_id;
	swim_id_t		 to_id;
	uint64_t		 max_delay = swim_ping_timeout_get() * 2 / 3;
	uint64_t		 hlc = crt_hlc_get();
	uint32_t		 rcv_delay = 0;
	uint32_t		 snd_delay = 0;
	int			 rc;

	D_ASSERT(crt_is_service());

	from_id = rpc_priv->crp_req_hdr.cch_src_rank;

	/* Initialize empty array in case of error in reply */
	rpc_out->upds.ca_arrays = NULL;
	rpc_out->upds.ca_count  = 0;

	rpc_type = (enum swim_rpc_type)(rpc->cr_opc & CRT_PROTO_COUNT_MASK);
	switch (rpc_type) {
	case SWIM_RPC_PING:
		to_id = rpc->cr_ep.ep_rank;
		break;
	case SWIM_RPC_IREQ:
		to_id = rpc_in->swim_id;
		break;
	default:
		to_id = rpc->cr_ep.ep_rank;
		D_GOTO(out_reply, rc = -DER_INVAL);
		break;
	}

	/*
	 * crt_hg_unpack_header may have failed to synchronize the HLC with
	 * this request.
	 */
	if (hlc > rpc_priv->crp_req_hdr.cch_hlc)
		rcv_delay = crt_hlc2msec(hlc - rpc_priv->crp_req_hdr.cch_hlc);

	RPC_TRACE(DB_NET, rpc_priv,
		  "incoming %s with %zu updates with %u ms delay. %lu: %lu <= %lu\n",
		  SWIM_RPC_TYPE_STR[rpc_type], rpc_in->upds.ca_count, rcv_delay,
		  self_id, to_id, from_id);

	if (self_id == SWIM_ID_INVALID)
		D_GOTO(out_reply, rc = -DER_UNINIT);

	snd_delay = crt_swim_update_delays(csm, hlc, from_id, rcv_delay,
					   rpc_in->upds.ca_arrays,
					   rpc_in->upds.ca_count);

	if (rcv_delay > max_delay || snd_delay > max_delay) {
		csm->csm_nglitches++;
		if (rcv_delay > max_delay)
			swim_net_glitch_update(ctx, self_id, rcv_delay - max_delay);
		if (snd_delay > max_delay)
			swim_net_glitch_update(ctx, from_id, snd_delay - max_delay);
	} else {
		csm->csm_nmessages++;
	}

	if (csm->csm_nmessages > CRT_SWIM_NMESSAGES_TRESHOLD) {
		crt_swim_accommodate();
		csm->csm_nglitches = 0;
		csm->csm_nmessages = 0;
	}

	if (csm->csm_nglitches > CRT_SWIM_NGLITCHES_TRESHOLD) {
		D_ERROR("Too many network glitches are detected, "
			"therefore increase SWIM timeouts by twice.\n");

		swim_suspect_timeout_set(swim_suspect_timeout_get() * 2);
		swim_ping_timeout_set(swim_ping_timeout_get() * 2);
		swim_period_set(swim_period_get() * 2);
		csm->csm_ctx->sc_default_ping_timeout *= 2;
		csm->csm_nglitches = 0;
	}

	if (CRT_SWIM_SHOULD_FAIL(d_fa_swim_drop_rpc, self_id)) {
		rc = d_fa_swim_drop_rpc->fa_err_code;
		D_EMIT("drop %s with %zu updates. %lu: %lu <= %lu "DF_RC"\n",
			SWIM_RPC_TYPE_STR[rpc_type], rpc_in->upds.ca_count,
			self_id, to_id, from_id, DP_RC(rc));
	} else {
		rc = swim_updates_parse(ctx, from_id, from_id, rpc_in->upds.ca_arrays,
					rpc_in->upds.ca_count);
		if (rc == -DER_SHUTDOWN) {
			if (grp_priv->gp_size > 1)
				D_ERROR("SWIM shutdown\n");
			swim_self_set(ctx, SWIM_ID_INVALID);
			D_GOTO(out_reply, rc);
		} else if (rc) {
			RPC_ERROR(rpc_priv,
				  "updates parse. %lu: %lu <= %lu failed: "DF_RC"\n",
				  self_id, to_id, from_id, DP_RC(rc));
		}

		switch (rpc_type) {
		case SWIM_RPC_PING:
			rc = swim_updates_prepare(ctx, from_id, from_id,
						  &rpc_out->upds.ca_arrays,
						  &rpc_out->upds.ca_count);
			break;
		case SWIM_RPC_IREQ:
			rc = swim_ipings_suspend(ctx, from_id, to_id, rpc);
			if (rc == 0 || rc == -DER_ALREADY) {
				RPC_TRACE(DB_NET, rpc_priv,
					  "suspend %s reply. %lu: %lu <= %lu\n",
					  SWIM_RPC_TYPE_STR[rpc_type], self_id, to_id, from_id);
				/* Keep this RPC in ipings queue */
				RPC_ADDREF(rpc_priv);

				if (rc == -DER_ALREADY)
					return; /* don't ping second time */

				rc = swim_updates_send(ctx, to_id, to_id);
				if (rc)
					RPC_ERROR(rpc_priv,
						  "swim_updates_send(): "DF_RC"\n", DP_RC(rc));
				return;
			}
			break;
		default:
			rc = -DER_INVAL;
			break;
		}
	}

out_reply:
	RPC_TRACE(DB_NET, rpc_priv,
		  "reply %s with %zu updates. %lu: %lu <= %lu "DF_RC"\n",
		  SWIM_RPC_TYPE_STR[rpc_type], rpc_out->upds.ca_count,
		  self_id, to_id, from_id, DP_RC(rc));

	rpc_out->rc = rc;
	rpc_out->excl_grp_ver = crt_swim_lookup_id(from_id);
	rc = crt_reply_send(rpc);
	D_FREE(rpc_out->upds.ca_arrays);
	if (rc)
		RPC_ERROR(rpc_priv,
			  "send reply: "DF_RC" failed: "DF_RC"\n",
			  DP_RC(rpc_out->rc), DP_RC(rc));
}

static int crt_swim_get_member_state(struct swim_context *ctx, swim_id_t id,
				     struct swim_member_state *state);
static int crt_swim_set_member_state(struct swim_context *ctx, swim_id_t id,
				     struct swim_member_state *state);

static void crt_swim_cli_cb(const struct crt_cb_info *cb_info)
{
	struct crt_grp_priv	*grp_priv = crt_gdata.cg_grp->gg_primary_grp;
	struct swim_context	*ctx = cb_info->cci_arg;
	crt_rpc_t		*rpc = cb_info->cci_rpc;
	struct crt_rpc_swim_in	*rpc_in  = crt_req_get(rpc);
	struct crt_rpc_swim_out *rpc_out = crt_reply_get(rpc);
	struct crt_rpc_priv	*rpc_priv = container_of(rpc, struct crt_rpc_priv, crp_pub);
	enum swim_rpc_type	 rpc_type;
	swim_id_t		 self_id = swim_self_get(ctx);
	swim_id_t		 from_id;
	swim_id_t		 to_id = rpc->cr_ep.ep_rank;
	uint64_t		 hlc = crt_hlc_get();
	uint32_t		 rcv_delay = 0;
	int			 reply_rc;
	int			 rc;

	D_FREE(rpc_in->upds.ca_arrays);

	rpc_type = (enum swim_rpc_type)(rpc->cr_opc & CRT_PROTO_COUNT_MASK);
	switch (rpc_type) {
	case SWIM_RPC_PING:
		from_id = self_id;
		break;
	case SWIM_RPC_IREQ:
		from_id = rpc_in->swim_id;
		break;
	default:
		D_GOTO(out, rc = -DER_INVAL);
		break;
	}

	if (hlc > rpc_priv->crp_reply_hdr.cch_hlc)
		rcv_delay = crt_hlc2msec(hlc - rpc_priv->crp_reply_hdr.cch_hlc);

	RPC_TRACE(DB_NET, rpc_priv,
		  "complete %s with %zu/%zu updates with %u ms delay. %lu: %lu => %lu "
		  DF_RC" remote: "DF_RC"\n",
		  SWIM_RPC_TYPE_STR[rpc_type], rpc_in->upds.ca_count,
		  rpc_out->upds.ca_count, rcv_delay, self_id, from_id, to_id,
		  DP_RC(cb_info->cci_rc), DP_RC(rpc_out->rc));

	if (self_id == SWIM_ID_INVALID)
		D_GOTO(out, rc = -DER_UNINIT);

	if (cb_info->cci_rc) {
		swim_ctx_lock(ctx);
		if (to_id == ctx->sc_target)
			ctx->sc_deadline = 0;
		swim_ctx_unlock(ctx);
	}

	reply_rc = cb_info->cci_rc ? cb_info->cci_rc : rpc_out->rc;
	if (reply_rc && reply_rc != -DER_TIMEDOUT && reply_rc != -DER_UNREACH) {
		if (reply_rc == -DER_UNINIT || reply_rc == -DER_NONEXIST) {
			struct swim_member_update *upds;

			RPC_TRACE(DB_NET, rpc_priv,
				  "%lu: %lu => %lu answered but not bootstrapped yet.\n",
				  self_id, from_id, to_id);

			/* Simulate ALIVE answer */
			D_FREE(rpc_out->upds.ca_arrays);
			rpc_out->upds.ca_count = 0;
			rc = swim_updates_prepare(ctx, to_id, to_id,
						  &rpc_out->upds.ca_arrays,
						  &rpc_out->upds.ca_count);
			upds = rpc_out->upds.ca_arrays;
			if (!rc && upds != NULL && rpc_out->upds.ca_count > 0)
				upds[0].smu_state.sms_status = SWIM_MEMBER_ALIVE;
			/*
			 * The error from this function should be just ignored
			 * because of it's fine if simulation of valid answer fails.
			 */
		} else {
			RPC_ERROR(rpc_priv,
				  "%lu: %lu => %lu remote failed: "DF_RC"\n",
				  self_id, from_id, to_id, DP_RC(reply_rc));
		}
	}

	rc = swim_updates_parse(ctx, to_id,
				rpc_type == SWIM_RPC_IREQ && !reply_rc ? from_id : to_id,
				rpc_out->upds.ca_arrays, rpc_out->upds.ca_count);
	if (rc == -DER_SHUTDOWN) {
		if (grp_priv->gp_size > 1)
			D_ERROR("SWIM shutdown\n");
		swim_self_set(ctx, SWIM_ID_INVALID);
		D_GOTO(out, rc);
	} else if (rc) {
		RPC_ERROR(rpc_priv,
			  "updates parse. %lu: %lu <= %lu failed: "DF_RC"\n",
			  self_id, from_id, to_id, DP_RC(rc));
	}

	rc = swim_ipings_reply(ctx, to_id, reply_rc);
	if (rc)
		RPC_ERROR(rpc_priv,
			  "send reply: "DF_RC" failed: "DF_RC"\n",
			  DP_RC(rpc_out->rc), DP_RC(rc));

out:
	if (rpc_out->excl_grp_ver > 0) {
		D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);
		if (grp_priv->gp_membs_ver > 0 && rpc_out->excl_grp_ver > grp_priv->gp_membs_ver) {
			struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
			struct crt_swim_target	*cst;
			uint64_t		 incarnation = 0;

			/*
			 * I'm excluded. TODO: Strictly speaking, we should
			 * compare rpc_out->excl_grp_ver with the group version
			 * at which we joined the system.
			 */
			D_WARN("excluded in group version %u (self %u)\n", rpc_out->excl_grp_ver,
			       grp_priv->gp_membs_ver);
			crt_swim_csm_lock(csm);
			D_CIRCLEQ_FOREACH(cst, &csm->csm_head, cst_link) {
				if (cst->cst_id == self_id) {
					incarnation = cst->cst_state.sms_incarnation;
					break;
				}
			}
			crt_swim_csm_unlock(csm);
			crt_trigger_event_cbs(self_id, incarnation, CRT_EVS_GRPMOD, CRT_EVT_DEAD);
		}
		D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
	}

	if (crt_swim_fail_delay && crt_swim_fail_id == self_id) {
		crt_swim_fail_hlc = crt_hlc_get() + crt_sec2hlc(crt_swim_fail_delay);
		crt_swim_fail_delay = 0;
	}
}

static int crt_swim_send_request(struct swim_context *ctx, swim_id_t id,
				 swim_id_t to, struct swim_member_update *upds,
				 size_t nupds)
{
	struct crt_grp_priv	*grp_priv = crt_gdata.cg_grp->gg_primary_grp;
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	struct crt_rpc_swim_in	*rpc_in;
	enum swim_rpc_type	 rpc_type;
	crt_context_t		 crt_ctx;
	crt_rpc_t		*rpc = NULL;
	struct crt_rpc_priv	*rpc_priv;
	crt_endpoint_t		 ep;
	crt_opcode_t		 opc;
	swim_id_t		 self_id = swim_self_get(ctx);
	uint32_t		 timeout_sec;
	int			 ctx_idx = csm->csm_crt_ctx_idx;
	int			 rc;

	if (self_id == SWIM_ID_INVALID)
		D_GOTO(out, rc = -DER_UNINIT);

	crt_ctx = crt_context_lookup(ctx_idx);
	if (crt_ctx == CRT_CONTEXT_NULL) {
		D_ERROR("crt_context_lookup(%d) failed\n", ctx_idx);
		D_GOTO(out, rc = -DER_UNINIT);
	}

	ep.ep_grp  = &grp_priv->gp_pub;
	ep.ep_rank = (d_rank_t)to;
	ep.ep_tag  = ctx_idx;

	rpc_type = (id == to) ? SWIM_RPC_PING : SWIM_RPC_IREQ;
	opc = CRT_PROTO_OPC(CRT_OPC_SWIM_BASE, CRT_OPC_SWIM_VERSION, rpc_type);
	rc = crt_req_create(crt_ctx, &ep, opc, &rpc);
	if (rc) {
		D_ERROR("crt_req_create(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rpc_priv = container_of(rpc, struct crt_rpc_priv, crp_pub);
	rpc_in = crt_req_get(rpc);
	rpc_in->swim_id = id;
	rpc_in->upds.ca_arrays = upds;
	rpc_in->upds.ca_count  = nupds;

	if (CRT_SWIM_SHOULD_FAIL(d_fa_swim_drop_rpc, self_id)) {
		struct crt_rpc_swim_out *rpc_out = crt_reply_get(rpc);
		struct crt_cb_info cbinfo;

		rc = d_fa_swim_drop_rpc->fa_err_code;
		if (rc == 0)
			rpc_out->rc = -DER_TIMEDOUT;

		D_EMIT("drop %s with %zu updates. %lu: %lu => %lu "
			DF_RC" remote: "DF_RC"\n",
			SWIM_RPC_TYPE_STR[rpc_type], nupds,
			self_id, (rpc_type == SWIM_RPC_PING) ? self_id : id, to,
			DP_RC(rc), DP_RC(rpc_out->rc));

		cbinfo.cci_rpc = rpc;
		cbinfo.cci_arg = ctx;
		cbinfo.cci_rc  = rc;
		crt_swim_cli_cb(&cbinfo);

		/* simulate success send */
		crt_req_decref(rpc);
		D_GOTO(out, rc = 0);
	}

	timeout_sec = crt_swim_rpc_timeout();
	if (rpc_type == SWIM_RPC_IREQ)
		timeout_sec *= 2;
	rc = crt_req_set_timeout(rpc, timeout_sec);
	if (rc) {
		RPC_ERROR(rpc_priv,
			  "crt_req_set_timeout(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	RPC_TRACE(DB_NET, rpc_priv,
		  "send %s with %zu updates. %lu: %lu => %lu\n",
		  SWIM_RPC_TYPE_STR[rpc_type], rpc_in->upds.ca_count,
		  self_id, (rpc_type == SWIM_RPC_PING) ? self_id : id, to);

	return crt_req_send(rpc, crt_swim_cli_cb, ctx);

out:
	if (rc && rpc != NULL)
		crt_req_decref(rpc);
	return rc;
}

static int crt_swim_send_reply(struct swim_context *ctx, swim_id_t from,
			       swim_id_t to, int ret_rc, void *args)
{
	crt_rpc_t		*rpc = args;
	struct crt_rpc_priv	*rpc_priv = container_of(rpc, struct crt_rpc_priv, crp_pub);
	struct crt_rpc_swim_out	*rpc_out;
	swim_id_t		 self_id = swim_self_get(ctx);
	int			 rc;

	rpc_out = crt_reply_get(rpc);
	rpc_out->upds.ca_arrays = NULL;
	rpc_out->upds.ca_count  = 0;
	rc = swim_updates_prepare(ctx, from, to,
				  &rpc_out->upds.ca_arrays,
				  &rpc_out->upds.ca_count);
	rpc_out->rc = rc ? rc : ret_rc;
	rpc_out->excl_grp_ver = crt_swim_lookup_id(to);

	RPC_TRACE(DB_NET, rpc_priv,
		  "complete %s with %zu updates. %lu: %lu => %lu "DF_RC"\n",
		  SWIM_RPC_TYPE_STR[SWIM_RPC_IREQ],
		  rpc_out->upds.ca_count, self_id, from, to, DP_RC(rpc_out->rc));

	rc = crt_reply_send(rpc);
	D_FREE(rpc_out->upds.ca_arrays);
	if (rc)
		RPC_ERROR(rpc_priv,
			  "send reply: "DF_RC" failed: "DF_RC"\n",
			  DP_RC(rpc_out->rc), DP_RC(rc));

	/*
	 * This RPC was removed from ipings queue.
	 * So, we need to decrement reference.
	 * Was incremented in crt_swim_srv_cb().
	 */
	RPC_DECREF(rpc_priv);
	return rc;
}

static swim_id_t crt_swim_get_dping_target(struct swim_context *ctx)
{
	struct crt_grp_priv	*grp_priv = crt_gdata.cg_grp->gg_primary_grp;
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	swim_id_t		 self_id = swim_self_get(ctx);
	swim_id_t		 id;
	uint32_t		 count = 0;

	if (self_id == SWIM_ID_INVALID)
		D_GOTO(out, id = SWIM_ID_INVALID);

	D_ASSERT(csm->csm_target != NULL);

	crt_swim_csm_lock(csm);
	do {
		if (count++ > grp_priv->gp_size) /* don't have a candidate */
			D_GOTO(out_unlock, id = SWIM_ID_INVALID);
		/*
		 * Iterate over circled list. So, when a last member is reached
		 * then transparently go to a first and continue.
		 */
		csm->csm_target = D_CIRCLEQ_LOOP_NEXT(&csm->csm_head,
						     csm->csm_target, cst_link);
		id = csm->csm_target->cst_id;
	} while (id == self_id ||
		 csm->csm_target->cst_state.sms_status == SWIM_MEMBER_DEAD);
out_unlock:
	crt_swim_csm_unlock(csm);
out:
	if (id != SWIM_ID_INVALID)
		D_DEBUG(DB_TRACE, "select dping target: %lu => {%lu %c %lu}\n",
			self_id, id, SWIM_STATUS_CHARS[csm->csm_target->cst_state.sms_status],
			csm->csm_target->cst_state.sms_incarnation);
	else
		D_DEBUG(DB_TRACE, "there is no dping target\n");
	return id;
}

static swim_id_t crt_swim_get_iping_target(struct swim_context *ctx)
{
	struct crt_grp_priv	*grp_priv = crt_gdata.cg_grp->gg_primary_grp;
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	swim_id_t		 self_id = swim_self_get(ctx);
	swim_id_t		 id;
	uint32_t		 count = 0;

	if (self_id == SWIM_ID_INVALID)
		D_GOTO(out, id = SWIM_ID_INVALID);

	D_ASSERT(csm->csm_target != NULL);

	crt_swim_csm_lock(csm);
	do {
		if (count++ > grp_priv->gp_size) /* don't have a candidate */
			D_GOTO(out_unlock, id = SWIM_ID_INVALID);
		/*
		 * Iterate over circled list. So, when a last member is reached
		 * then transparently go to a first and continue.
		 */
		csm->csm_target = D_CIRCLEQ_LOOP_NEXT(&csm->csm_head,
						     csm->csm_target, cst_link);
		id = csm->csm_target->cst_id;
	} while (id == self_id ||
		 csm->csm_target->cst_state.sms_status != SWIM_MEMBER_ALIVE);
out_unlock:
	crt_swim_csm_unlock(csm);
out:
	if (id != SWIM_ID_INVALID)
		D_DEBUG(DB_TRACE, "select iping target: %lu => {%lu %c %lu}\n",
			self_id, id, SWIM_STATUS_CHARS[csm->csm_target->cst_state.sms_status],
			csm->csm_target->cst_state.sms_incarnation);
	else
		D_DEBUG(DB_TRACE, "there is no iping target\n");
	return id;
}

static void
crt_swim_notify_rank_state(d_rank_t rank, struct swim_member_state *state)
{
	struct crt_event_cb_priv *cbs_event;
	crt_event_cb		 cb_func;
	void			*cb_args;
	enum crt_event_type	 cb_type;
	size_t			 i, cbs_size;

	D_ASSERT(state != NULL);
	switch (state->sms_status) {
	case SWIM_MEMBER_ALIVE:
		cb_type = CRT_EVT_ALIVE;
		break;
	case SWIM_MEMBER_DEAD:
		cb_type = CRT_EVT_DEAD;
		break;
	default:
		return;
	}

	/* walk the global list to execute the user callbacks */
	cbs_size = crt_plugin_gdata.cpg_event_size;
	cbs_event = crt_plugin_gdata.cpg_event_cbs;

	for (i = 0; i < cbs_size; i++) {
		cb_func = cbs_event[i].cecp_func;
		cb_args = cbs_event[i].cecp_args;
		/* check for and execute event callbacks here */
		if (cb_func != NULL)
			cb_func(rank, state->sms_incarnation, CRT_EVS_SWIM, cb_type, cb_args);
	}
}

static int crt_swim_get_member_state(struct swim_context *ctx,
				     swim_id_t id,
				     struct swim_member_state *state)
{
	struct crt_grp_priv	*grp_priv = crt_gdata.cg_grp->gg_primary_grp;
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	struct crt_swim_target	*cst;
	int			 rc = -DER_NONEXIST;

	D_ASSERT(state != NULL);
	crt_swim_csm_lock(csm);
	D_CIRCLEQ_FOREACH(cst, &csm->csm_head, cst_link) {
		if (cst->cst_id == id) {
			*state = cst->cst_state;
			rc = 0;
			break;
		}
	}
	crt_swim_csm_unlock(csm);

	return rc;
}

static int crt_swim_set_member_state(struct swim_context *ctx,
				     swim_id_t id,
				     struct swim_member_state *state)
{
	struct crt_grp_priv	*grp_priv = crt_gdata.cg_grp->gg_primary_grp;
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	struct crt_swim_target	*cst;
	int			 rc = -DER_NONEXIST;

	D_ASSERT(state != NULL);
	if (state->sms_status == SWIM_MEMBER_SUSPECT)
		state->sms_delay += swim_ping_timeout_get();

	crt_swim_csm_lock(csm);
	D_CIRCLEQ_FOREACH(cst, &csm->csm_head, cst_link) {
		if (cst->cst_id == id) {
			if (cst->cst_state.sms_status != SWIM_MEMBER_ALIVE &&
			    state->sms_status == SWIM_MEMBER_ALIVE)
				csm->csm_alive_count++;
			else if (cst->cst_state.sms_status == SWIM_MEMBER_ALIVE &&
			    state->sms_status != SWIM_MEMBER_ALIVE)
				csm->csm_alive_count--;
			cst->cst_state = *state;
			rc = 0;
			break;
		}
	}
	crt_swim_csm_unlock(csm);

	if (rc == 0)
		crt_swim_notify_rank_state((d_rank_t)id, state);

	return rc;
}

static void crt_swim_new_incarnation(struct swim_context *ctx,
				     swim_id_t id,
				     struct swim_member_state *state)
{
	struct crt_grp_priv	*grp_priv = crt_gdata.cg_grp->gg_primary_grp;
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	uint64_t		 incarnation = crt_hlc_get();

	D_ASSERT(state != NULL);
	D_ASSERTF(id == swim_self_get(ctx), DF_U64" == "DF_U64"\n",
		  id, swim_self_get(ctx));
	crt_swim_csm_lock(csm);
	csm->csm_incarnation = incarnation;
	crt_swim_csm_unlock(csm);
	state->sms_incarnation = incarnation;
}

static void crt_swim_update_last_unpack_hlc(struct crt_swim_membs *csm)
{
	struct crt_context	*ctx = NULL;
	d_list_t		*ctx_list;

	D_RWLOCK_RDLOCK(&crt_gdata.cg_rwlock);

	ctx_list = crt_provider_get_ctx_list(crt_gdata.cg_init_prov);
	d_list_for_each_entry(ctx, ctx_list, cc_link) {
		uint64_t hlc = ctx->cc_last_unpack_hlc;

		if (csm->csm_last_unpack_hlc < hlc)
			csm->csm_last_unpack_hlc = hlc;
	}

	D_RWLOCK_UNLOCK(&crt_gdata.cg_rwlock);
}

static int64_t crt_swim_progress_cb(crt_context_t crt_ctx, int64_t timeout_us, void *arg)
{
	struct crt_grp_priv	*grp_priv = crt_gdata.cg_grp->gg_primary_grp;
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	struct swim_context	*ctx = csm->csm_ctx;
	swim_id_t		 self_id = swim_self_get(ctx);
	int			 rc;

	if (self_id == SWIM_ID_INVALID)
		return timeout_us;

	if (crt_swim_fail_hlc && crt_hlc_get() >= crt_swim_fail_hlc) {
		crt_swim_should_fail = true;
		crt_swim_fail_hlc = 0;
		D_EMIT("SWIM id=%lu should fail\n", crt_swim_fail_id);
	}

	rc = swim_progress(ctx, timeout_us);
	if (rc == -DER_SHUTDOWN) {
		if (grp_priv->gp_size > 1)
			D_ERROR("SWIM shutdown\n");
		swim_self_set(ctx, SWIM_ID_INVALID);
	} else if (rc == -DER_TIMEDOUT || rc == -DER_CANCELED) {
		uint64_t now = swim_now_ms();

		crt_swim_update_last_unpack_hlc(csm);

		/*
		 * Check for network idle in all contexts.
		 * If the time passed from last received RPC till now is more
		 * than 2/3 of suspicion timeout suspends eviction.
		 * The max_delay should be less suspicion timeout to guarantee
		 * the already suspected members will not be expired.
		 */
		if (csm->csm_alive_count > 2) {
			uint64_t hlc1 = csm->csm_last_unpack_hlc;
			uint64_t hlc2 = crt_hlc_get();
			uint64_t delay = crt_hlc2msec(hlc2 - hlc1);
			uint64_t max_delay = swim_suspect_timeout_get() * 2 / 3;

			if (delay > max_delay) {
				D_ERROR("Network outage detected (idle during "
					"%lu.%lu sec > expected %lu.%lu sec).\n",
					delay / 1000, delay % 1000,
					max_delay / 1000, max_delay % 1000);
				swim_net_glitch_update(csm->csm_ctx, self_id, delay);
				csm->csm_last_unpack_hlc = hlc2;
			}
		}

		if (now < ctx->sc_next_event)
			timeout_us = min(timeout_us, (ctx->sc_next_event - now) * 1000);
	} else if (rc) {
		D_ERROR("swim_progress(): "DF_RC"\n", DP_RC(rc));
	}

	return timeout_us;
}

void crt_swim_fini(void)
{
	struct crt_grp_priv	*grp_priv = crt_gdata.cg_grp->gg_primary_grp;
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;

	if (!crt_gdata.cg_swim_inited)
		return;

	crt_swim_rank_del_all(grp_priv);

	if (csm->csm_ctx != NULL) {
		if (csm->csm_crt_ctx_idx != -1)
			crt_unregister_progress_cb(crt_swim_progress_cb,
						   csm->csm_crt_ctx_idx, NULL);
		csm->csm_crt_ctx_idx = -1;
		swim_fini(csm->csm_ctx);
		csm->csm_ctx = NULL;
	}

	crt_gdata.cg_swim_inited = 0;
}

static struct swim_ops crt_swim_ops = {
	.send_request     = &crt_swim_send_request,
	.send_reply       = &crt_swim_send_reply,
	.get_dping_target = &crt_swim_get_dping_target,
	.get_iping_target = &crt_swim_get_iping_target,
	.get_member_state = &crt_swim_get_member_state,
	.set_member_state = &crt_swim_set_member_state,
	.new_incarnation  = &crt_swim_new_incarnation,
};

int crt_swim_init(int crt_ctx_idx)
{
	struct crt_grp_priv	*grp_priv = crt_gdata.cg_grp->gg_primary_grp;
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	d_rank_list_t		*grp_membs;
	d_rank_t		 self = grp_priv->gp_self;
	uint64_t		 hlc = crt_hlc_get();
	int			 i, rc;

	if (crt_gdata.cg_swim_inited) {
		D_ERROR("SWIM already initialized\n");
		D_GOTO(out, rc = -DER_ALREADY);
	}

	grp_membs = grp_priv_get_membs(grp_priv);
	csm->csm_crt_ctx_idx = crt_ctx_idx;
	csm->csm_last_unpack_hlc = hlc;
	csm->csm_alive_count = 0;
	csm->csm_nglitches = 0;
	csm->csm_nmessages = 0;
	/*
	 * Because daos needs to call crt_self_incarnation_get before it calls
	 * crt_rank_self_set, we choose the self incarnation here instead of in
	 * crt_swim_rank_add.
	 */
	csm->csm_incarnation = hlc;
	csm->csm_ctx = swim_init(SWIM_ID_INVALID, &crt_swim_ops, NULL);
	if (csm->csm_ctx == NULL) {
		D_ERROR("swim_init() failed for self=%u, crt_ctx_idx=%d\n",
			self, crt_ctx_idx);
		D_GOTO(out, rc = -DER_NOMEM);
	}

	crt_gdata.cg_swim_inited = 1;
	if (self != CRT_NO_RANK && grp_membs != NULL) {
		if (grp_membs->rl_nr != grp_priv->gp_size) {
			D_ERROR("Mismatch in group size. Expected %d got %d\n",
				grp_membs->rl_nr, grp_priv->gp_size);
			D_GOTO(cleanup, rc = -DER_INVAL);
		}

		for (i = 0; i < grp_priv->gp_size; i++) {
			rc = crt_swim_rank_add(grp_priv,
					       grp_membs->rl_ranks[i]);
			if (rc && rc != -DER_ALREADY) {
				D_ERROR("crt_swim_rank_add(): "DF_RC"\n",
					DP_RC(rc));
				D_GOTO(cleanup, rc);
			}
		}
	}

	rc = crt_proto_register(&crt_swim_proto_fmt);
	if (rc) {
		D_ERROR("crt_proto_register(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(cleanup, rc);
	}

	rc = crt_register_progress_cb(crt_swim_progress_cb, crt_ctx_idx, NULL);
	if (rc) {
		D_ERROR("crt_register_progress_cb(): "DF_RC"\n", DP_RC(rc));
		D_GOTO(cleanup, rc);
	}

	if (!d_fault_inject_is_enabled())
		D_GOTO(out, rc = 0);

	crt_swim_should_fail = false; /* disabled by default */
	crt_swim_fail_hlc = 0;
	crt_swim_fail_delay = 10;
	crt_swim_fail_id = SWIM_ID_INVALID;

	/* Search the attr in inject yml first */
	d_fa_swim_drop_rpc = d_fault_attr_lookup(CRT_SWIM_FAIL_DROP_RPC);
	if (d_fa_swim_drop_rpc != NULL) {
		D_EMIT("fa_swim_drop_rpc: id=%u/0x%x, "
			"interval=%u, max=" DF_U64 ", x=%u, y=%u, args='%s'\n",
			d_fa_swim_drop_rpc->fa_id,
			d_fa_swim_drop_rpc->fa_id,
			d_fa_swim_drop_rpc->fa_interval,
			d_fa_swim_drop_rpc->fa_max_faults,
			d_fa_swim_drop_rpc->fa_probability_x,
			d_fa_swim_drop_rpc->fa_probability_y,
			d_fa_swim_drop_rpc->fa_argument);
		if (d_fa_swim_drop_rpc->fa_argument != NULL)
			crt_swim_fault_init(d_fa_swim_drop_rpc->fa_argument);
	} else {
		D_INFO("fault_id=%lu/0x%lx not found\n",
			CRT_SWIM_FAIL_DROP_RPC, CRT_SWIM_FAIL_DROP_RPC);
	}
	D_GOTO(out, rc = 0);

cleanup:
	if (self != CRT_NO_RANK && grp_membs != NULL) {
		for (i = 0; i < grp_priv->gp_size; i++)
			crt_swim_rank_del(grp_priv, grp_membs->rl_ranks[i]);
	}
	crt_gdata.cg_swim_inited = 0;
	swim_fini(csm->csm_ctx);
	csm->csm_ctx = NULL;
	csm->csm_crt_ctx_idx = -1;
out:
	return rc;
}

int crt_swim_enable(struct crt_grp_priv *grp_priv, int crt_ctx_idx)
{
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	d_rank_t		 self = grp_priv->gp_self;
	swim_id_t		 self_id;
	int			 old_ctx_idx = -1;
	int			 rc = 0;

	if (!crt_gdata.cg_swim_inited)
		D_GOTO(out, rc = 0);

	if (self == CRT_NO_RANK) {
		D_ERROR("Self rank was not set yet\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (crt_ctx_idx < 0) {
		D_ERROR("Invalid context index\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	crt_swim_csm_lock(csm);
	if (csm->csm_crt_ctx_idx != crt_ctx_idx)
		old_ctx_idx = csm->csm_crt_ctx_idx;
	csm->csm_crt_ctx_idx = crt_ctx_idx;
	self_id = swim_self_get(csm->csm_ctx);
	if (self_id != (swim_id_t)self)
		swim_self_set(csm->csm_ctx, (swim_id_t)self);
	crt_swim_csm_unlock(csm);

	if (old_ctx_idx != -1) {
		rc = crt_unregister_progress_cb(crt_swim_progress_cb,
						old_ctx_idx, NULL);
		if (rc == -DER_NONEXIST)
			rc = 0;
		if (rc)
			D_ERROR("crt_unregister_progress_cb(): "DF_RC"\n",
				DP_RC(rc));
	}
	if (old_ctx_idx != crt_ctx_idx) {
		rc = crt_register_progress_cb(crt_swim_progress_cb,
					      crt_ctx_idx, NULL);
		if (rc)
			D_ERROR("crt_register_progress_cb(): "DF_RC"\n",
				DP_RC(rc));
	}

out:
	return rc;
}

int crt_swim_disable(struct crt_grp_priv *grp_priv, int crt_ctx_idx)
{
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	int			 old_ctx_idx = -1;
	int			 rc = -DER_NONEXIST;

	if (!crt_gdata.cg_swim_inited)
		D_GOTO(out, rc = 0);

	if (crt_ctx_idx < 0) {
		swim_self_set(csm->csm_ctx, SWIM_ID_INVALID);
		D_GOTO(out, rc = 0);
	}

	crt_swim_csm_lock(csm);
	if (csm->csm_crt_ctx_idx == crt_ctx_idx) {
		old_ctx_idx = csm->csm_crt_ctx_idx;
		csm->csm_crt_ctx_idx = -1;
		swim_self_set(csm->csm_ctx, SWIM_ID_INVALID);
	}
	crt_swim_csm_unlock(csm);

	if (old_ctx_idx != -1) {
		rc = crt_unregister_progress_cb(crt_swim_progress_cb,
						old_ctx_idx, NULL);
		if (rc == -DER_NONEXIST)
			rc = 0;
		if (rc)
			D_ERROR("crt_unregister_progress_cb(): "DF_RC"\n",
				DP_RC(rc));
	}

out:
	return rc;
}

void crt_swim_disable_all(void)
{
	struct crt_grp_priv	*grp_priv = crt_gdata.cg_grp->gg_primary_grp;
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	int			 old_ctx_idx;

	if (!crt_gdata.cg_swim_inited)
		return;

	crt_swim_csm_lock(csm);
	old_ctx_idx = csm->csm_crt_ctx_idx;
	csm->csm_crt_ctx_idx = -1;
	swim_self_set(csm->csm_ctx, SWIM_ID_INVALID);
	crt_swim_csm_unlock(csm);

	if (old_ctx_idx != -1)
		crt_unregister_progress_cb(crt_swim_progress_cb,
					   old_ctx_idx, NULL);
}

void crt_swim_suspend_all(void)
{
	struct crt_grp_priv	*grp_priv = crt_gdata.cg_grp->gg_primary_grp;
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	struct crt_swim_target	*cst;
	swim_id_t		 self_id;

	if (!crt_gdata.cg_swim_inited)
		return;

	csm->csm_ctx->sc_glitch = 1;
	self_id = swim_self_get(csm->csm_ctx);
	crt_swim_csm_lock(csm);
	D_CIRCLEQ_FOREACH(cst, &csm->csm_head, cst_link) {
		if (cst->cst_id != self_id)
			cst->cst_state.sms_status = SWIM_MEMBER_INACTIVE;
	}
	crt_swim_csm_unlock(csm);
}

/**
 * Calculate average of network delay and set it as expected PING timeout.
 * But limiting this timeout in range from specified by user or default to
 * suspicion timeout divided by 3. It will be automatically increased if
 * network glitches accrues and decreased when network communication is
 * normalized.
 */
void crt_swim_accommodate(void)
{
	struct crt_grp_priv	*grp_priv = crt_gdata.cg_grp->gg_primary_grp;
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	struct crt_swim_target	*cst;
	uint64_t		 average = 0;
	uint64_t		 count = 0;

	if (!crt_gdata.cg_swim_inited)
		return;

	crt_swim_csm_lock(csm);
	D_CIRCLEQ_FOREACH(cst, &csm->csm_head, cst_link) {
		if (cst->cst_state.sms_delay > 0) {
			average += cst->cst_state.sms_delay;
			count++;
		}
	}
	crt_swim_csm_unlock(csm);

	if (count > 0) {
		uint64_t ping_timeout = swim_ping_timeout_get();
		uint64_t max_timeout = swim_suspect_timeout_get() / 3;
		uint64_t min_timeout = csm->csm_ctx->sc_default_ping_timeout;

		average = (2 * average) / count;
		if (average < min_timeout)
			average = min_timeout;
		else if (average > max_timeout)
			average = max_timeout;

		/*
		 * (x >> 5) is just (x / 32) but a way faster.
		 * This should avoid changes for small deltas.
		 */
		if ((average >> 5) != (ping_timeout >> 5)) {
			D_INFO("change PING timeout from %lu ms to %lu ms\n",
			       ping_timeout, average);
			swim_ping_timeout_set(average);
		}
	}
}

int crt_swim_rank_add(struct crt_grp_priv *grp_priv, d_rank_t rank)
{
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	struct crt_swim_target	*cst2, *cst = NULL;
	swim_id_t		 self_id;
	d_rank_t		 self = grp_priv->gp_self;
	bool			 self_in_list = false;
	bool			 rank_in_list = false;
	int			 n, rc = 0;

	if (!crt_gdata.cg_swim_inited)
		return 0;

	if (self == CRT_NO_RANK) {
		D_ERROR("Self rank was not set yet\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_ALLOC_PTR(cst);
	if (cst == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	crt_swim_csm_lock(csm);
	if (D_CIRCLEQ_EMPTY(&csm->csm_head)) {
		cst->cst_id = (swim_id_t)self;
		cst->cst_state.sms_incarnation = csm->csm_incarnation;
		cst->cst_state.sms_status = SWIM_MEMBER_ALIVE;
		D_CIRCLEQ_INSERT_HEAD(&csm->csm_head, cst, cst_link);
		self_in_list = true;

		csm->csm_target = cst;

		D_DEBUG(DB_TRACE, "add self {%lu %c %lu}\n", cst->cst_id,
			SWIM_STATUS_CHARS[cst->cst_state.sms_status],
			cst->cst_state.sms_incarnation);

		cst = NULL;
	} else {
		D_CIRCLEQ_FOREACH(cst2, &csm->csm_head, cst_link) {
			if (cst2->cst_id == (swim_id_t)rank)
				D_GOTO(out_check_self, rc = -DER_ALREADY);
		}
	}

	if (rank != self) {
		if (cst == NULL) {
			D_ALLOC_PTR(cst);
			if (cst == NULL)
				D_GOTO(out_unlock, rc = -DER_NOMEM);
		}
		cst->cst_id = rank;
		cst->cst_state.sms_incarnation = 0;
		cst->cst_state.sms_status = SWIM_MEMBER_ALIVE;
		D_CIRCLEQ_INSERT_AFTER(&csm->csm_head, csm->csm_target, cst,
				       cst_link);
		rank_in_list = true;

		for (n = 1 + rand() % (grp_priv->gp_size + 1); n > 0; n--)
			csm->csm_target = D_CIRCLEQ_LOOP_NEXT(&csm->csm_head,
							      csm->csm_target,
							      cst_link);

		D_DEBUG(DB_TRACE, "add member {%lu %c %lu}\n", cst->cst_id,
			SWIM_STATUS_CHARS[cst->cst_state.sms_status],
			cst->cst_state.sms_incarnation);
		cst = NULL;
	}

out_check_self:
	self_id = swim_self_get(csm->csm_ctx);
	if (self_id != (swim_id_t)self)
		swim_self_set(csm->csm_ctx, (swim_id_t)self);

out_unlock:
	crt_swim_csm_unlock(csm);
	D_FREE(cst);

	if (rc && rc != -DER_ALREADY) {
		if (rank_in_list)
			crt_swim_rank_del(grp_priv, rank);
		if (self_in_list)
			crt_swim_rank_del(grp_priv, self);
	}
out:
	return rc;
}

int crt_swim_rank_del(struct crt_grp_priv *grp_priv, d_rank_t rank)
{
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	struct crt_swim_target	*cst, *next = NULL;
	int			 rc = -DER_NONEXIST;

	if (!crt_gdata.cg_swim_inited)
		return 0;

	crt_swim_csm_lock(csm);
	D_CIRCLEQ_FOREACH(cst, &csm->csm_head, cst_link) {
		if (cst->cst_id == (swim_id_t)rank) {
			D_DEBUG(DB_TRACE, "del member {%lu %c %lu}\n",
				cst->cst_id,
				SWIM_STATUS_CHARS[cst->cst_state.sms_status],
				cst->cst_state.sms_incarnation);

			next = D_CIRCLEQ_LOOP_NEXT(&csm->csm_head,
						   csm->csm_target, cst_link);
			D_CIRCLEQ_REMOVE(&csm->csm_head, cst, cst_link);
			if (D_CIRCLEQ_EMPTY(&csm->csm_head)) {
				swim_self_set(csm->csm_ctx, SWIM_ID_INVALID);
				csm->csm_target = NULL;
			} else if (csm->csm_target == cst) {
				csm->csm_target = next;
			}

			rc = 0;
			break; /* found, free it */
		}
	}
	if (rank == grp_priv->gp_self)
		swim_self_set(csm->csm_ctx, SWIM_ID_INVALID);
	crt_swim_csm_unlock(csm);

	if (rc == 0)
		D_FREE(cst);

	return rc;
}

void crt_swim_rank_del_all(struct crt_grp_priv *grp_priv)
{
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	struct crt_swim_target	*cst;

	if (!crt_gdata.cg_swim_inited)
		return;

	crt_swim_csm_lock(csm);
	swim_self_set(csm->csm_ctx, SWIM_ID_INVALID);
	csm->csm_target = NULL;
	while (!D_CIRCLEQ_EMPTY(&csm->csm_head)) {
		cst = D_CIRCLEQ_FIRST(&csm->csm_head);
		D_DEBUG(DB_TRACE, "del member {%lu %c %lu}\n", cst->cst_id,
			SWIM_STATUS_CHARS[cst->cst_state.sms_status],
			cst->cst_state.sms_incarnation);
		D_CIRCLEQ_REMOVE(&csm->csm_head, cst, cst_link);
		D_FREE(cst);
	}
	crt_swim_csm_unlock(csm);
}

int
crt_rank_state_get(crt_group_t *grp, d_rank_t rank,
		   struct swim_member_state *state)
{
	struct crt_grp_priv	*grp_priv;
	struct crt_swim_membs	*csm;
	int			 rc = 0;

	if (grp == NULL) {
		D_ERROR("Passed group is NULL\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (state == NULL) {
		D_ERROR("Passed state pointer is NULL\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (rank == CRT_NO_RANK) {
		D_ERROR("Rank is invalid\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	grp_priv = crt_grp_pub2priv(grp);
	if (!grp_priv->gp_primary) {
		D_ERROR("Only available for primary groups\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	csm = &grp_priv->gp_membs_swim;
	rc = crt_swim_get_member_state(csm->csm_ctx, (swim_id_t)rank, state);

out:
	return rc;
}

int
crt_self_incarnation_get(uint64_t *incarnation)
{
	struct crt_grp_priv	*grp_priv = crt_grp_pub2priv(NULL);
	struct crt_swim_membs	*csm = &grp_priv->gp_membs_swim;
	int			 rc = 0;

	if (incarnation == NULL) {
		D_ERROR("Passed state pointer is NULL\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (!crt_gdata.cg_swim_inited)
		D_GOTO(out, rc = -DER_UNINIT);

	crt_swim_csm_lock(csm);
	*incarnation = csm->csm_incarnation;
	crt_swim_csm_unlock(csm);
out:
	return rc;
}
