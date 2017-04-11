/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * ds_cont: Epoch Operations
 */

#define DD_SUBSYS	DD_FAC(container)

#include <daos/btree_class.h>
#include <daos_srv/pool.h>
#include "rpc.h"
#include "srv_internal.h"
#include "srv_layout.h"

enum ec_type {
	EC_LRE,
	EC_LHE
};

static daos_handle_t
ec_type2tree(struct cont *cont, enum ec_type type)
{
	switch (type) {
	case EC_LRE:
		return cont->c_lres;
	case EC_LHE:
		return cont->c_lhes;
	default:
		D_ASSERT(0);
	}
}

static const char *
ec_type2name(enum ec_type type)
{
	switch (type) {
	case EC_LRE:
		return "LRE";
	case EC_LHE:
		return "LHE";
	default:
		D_ASSERT(0);
	}
}

static int
ec_increment(struct cont *cont, enum ec_type type, uint64_t epoch)
{
	daos_handle_t	tree = ec_type2tree(cont, type);
	uint64_t	c = 0;
	uint64_t	c_new;
	int		rc;

	rc = dbtree_ec_lookup(tree, epoch, &c);
	if (rc != 0 && rc != -DER_NONEXIST)
		D_GOTO(out, rc);

	c_new = c + 1;
	if (c_new < c)
		D_GOTO(out, rc = -DER_OVERFLOW);

	rc = dbtree_ec_update(tree, epoch, &c_new);

out:
	if (rc != 0)
		D_ERROR(DF_CONT": failed to increment %s epoch counter: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			ec_type2name(type), rc);
	return rc;
}

static int
ec_decrement(struct cont *cont, enum ec_type type, uint64_t epoch)
{
	daos_handle_t	tree = ec_type2tree(cont, type);
	uint64_t	c = 0;
	uint64_t	c_new;
	int		rc;

	rc = dbtree_ec_lookup(tree, epoch, &c);
	if (rc != 0 && rc != -DER_NONEXIST)
		D_GOTO(out, rc);

	c_new = c - 1;
	if (c_new > c)
		D_GOTO(out, rc = -DER_OVERFLOW);

	if (c_new == 0)
		rc = dbtree_ec_delete(tree, epoch);
	else
		rc = dbtree_ec_update(tree, epoch, &c_new);

out:
	if (rc != 0)
		D_ERROR(DF_CONT": failed to decrement %s epoch counter: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			ec_type2name(type), rc);
	return rc;
}

static int
ec_find_lowest(struct cont *cont, enum ec_type type, daos_epoch_t *epoch)
{
	daos_handle_t	tree = ec_type2tree(cont, type);
	int		rc;

	rc = dbtree_ec_fetch(tree, BTR_PROBE_FIRST, NULL /* epoch_in */, epoch,
			     NULL /* count */);
	if (rc == -DER_NONEXIST)
		D_DEBUG(DF_DSMS, DF_CONT": %s tree empty: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			ec_type2name(type), rc);
	else if (rc != 0)
		D_ERROR(DF_CONT": failed to find lowest %s: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			ec_type2name(type), rc);
	return rc;
}

/* Buffer for epoch-related container attributes (global epoch state) */
struct epoch_attr {
	daos_epoch_t	ea_ghce;
	daos_epoch_t	ea_ghpce;
	daos_epoch_t	ea_glre;	/* DAOS_EPOCH_MAX if no refs */
	daos_epoch_t	ea_glhe;	/* DAOS_EPOCH_MAX if no holds */
};

#define DF_EPOCH_ATTR		"GLRE="DF_U64" GHCE="DF_U64" GHPCE="DF_U64 \
				" GLHE="DF_U64
#define DP_EPOCH_ATTR(attr)	attr->ea_glre, attr->ea_ghce, attr->ea_ghpce, \
				attr->ea_glhe

static int
read_epoch_attr(struct cont *cont, struct epoch_attr *attr)
{
	daos_epoch_t	ghce;
	daos_epoch_t	ghpce;
	daos_epoch_t	glre;
	daos_epoch_t	glhe;
	int		rc;

	rc = dbtree_nv_lookup(cont->c_cont, CONT_GHCE, strlen(CONT_GHCE),
			      &ghce, sizeof(ghce));
	if (rc != 0)
		return rc;

	rc = dbtree_nv_lookup(cont->c_cont, CONT_GHPCE, strlen(CONT_GHPCE),
			      &ghpce, sizeof(ghpce));
	if (rc != 0)
		return rc;

	rc = ec_find_lowest(cont, EC_LRE, &glre);
	if (rc == -DER_NONEXIST)
		glre = DAOS_EPOCH_MAX;
	else if (rc != 0)
		return rc;

	rc = ec_find_lowest(cont, EC_LHE, &glhe);
	if (rc == -DER_NONEXIST)
		glhe = DAOS_EPOCH_MAX;
	else if (rc != 0)
		return rc;

	attr->ea_ghce = ghce;
	attr->ea_ghpce = ghpce;
	attr->ea_glre = glre;
	attr->ea_glhe = glhe;
	return 0;
}

/* Return 0 if the check passes, 1 otherwise. */
static inline int
check_global_epoch_invariant(struct cont *cont, struct epoch_attr *attr)
{
	if (!(attr->ea_ghce <= attr->ea_ghpce)) {
		D_ERROR(DF_CONT": GHCE "DF_U64" > GHPCE "DF_U64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			attr->ea_ghce, attr->ea_ghpce);
		return 1;
	}

	if (!(attr->ea_glhe == DAOS_EPOCH_MAX ||
	      attr->ea_glhe > attr->ea_ghce)) {
		D_ERROR(DF_CONT": GLHE "DF_U64" <= GHCE "DF_U64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			attr->ea_glhe, attr->ea_ghce);
		return 1;
	}

	return 0;
}

/* Return 0 if the check passes, 1 otherwise. */
static inline int
check_epoch_invariant(struct cont *cont, struct epoch_attr *attr,
		      struct container_hdl *hdl)
{
	if (check_global_epoch_invariant(cont, attr) != 0)
		return 1;

	if (!(hdl->ch_hce < hdl->ch_lhe)) {
		D_ERROR(DF_CONT": HCE "DF_U64" >= LHE "DF_U64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			hdl->ch_hce, hdl->ch_lhe);
		return 1;
	}

	if (!(attr->ea_glre <= hdl->ch_lre)) {
		D_ERROR(DF_CONT": GLRE "DF_U64" > LRE "DF_U64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			attr->ea_glre, hdl->ch_lre);
		return 1;
	}

	if (!(attr->ea_ghce < hdl->ch_lhe)) {
		D_ERROR(DF_CONT": GHCE "DF_U64" >= LHE "DF_U64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			attr->ea_ghce, hdl->ch_lhe);
		return 1;
	}

	if (!(attr->ea_ghpce >= hdl->ch_hce)) {
		D_ERROR(DF_CONT": GHPCE "DF_U64" < HCE "DF_U64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			attr->ea_ghpce, hdl->ch_hce);
		return 1;
	}

	return 0;
}

static void
set_epoch_state(struct epoch_attr *attr, struct container_hdl *hdl,
		daos_epoch_state_t *state)
{
	state->es_hce = hdl->ch_hce;
	state->es_lre = hdl->ch_lre;
	state->es_lhe = hdl->ch_lhe;
	state->es_ghce = attr->ea_ghce;
	state->es_glre = attr->ea_glre;
	state->es_ghpce = attr->ea_ghpce;
}

/*
 * Calculate GHCE afresh using attr->ea_ghpce and attr->ea_glhe, and if the
 * result is higher than attr->ea_ghce, update attr->ea_ghce and CONT_GHCE.
 *
 * This function must be called by operations change GHPCE or GLHE. It may be
 * called unnecessarily by other operations without compromising safety.
 */
static int
update_ghce(struct cont *cont, struct epoch_attr *attr)
{
	daos_epoch_t	ghce;
	int		rc;

	/*
	 * GHCE shall be the highest epoch e satisfying both of these:
	 *   - e <= GHPCE (committed by some handle)
	 *   - e < GLHE (not held by any handle)
	 */
	ghce = min(attr->ea_ghpce, attr->ea_glhe - 1);

	if (ghce < attr->ea_ghce) {
		D_ERROR(DF_CONT": GHCE would decrease: "DF_EPOCH_ATTR"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			DP_EPOCH_ATTR(attr));
		return -DER_IO;
	} else if (ghce == attr->ea_ghce) {
		return 0;
	}

	rc = dbtree_nv_update(cont->c_cont, CONT_GHCE, strlen(CONT_GHCE),
			      &ghce, sizeof(ghce));
	if (rc != 0)
		D_ERROR(DF_CONT": failed to update ghce: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid,
				cont->c_uuid), rc);

	attr->ea_ghce = ghce;
	return rc;
}

/* Callers must abort the TX if this function returns an error. */
int
ds_cont_epoch_init_hdl(struct cont *cont, struct container_hdl *hdl,
		       daos_epoch_state_t *state)
{
	struct epoch_attr	attr;
	int			rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_WORK);

	rc = read_epoch_attr(cont, &attr);
	if (rc != 0)
		return rc;

	if (check_global_epoch_invariant(cont, &attr) != 0)
		return -DER_IO;

	hdl->ch_hce = attr.ea_ghce;
	hdl->ch_lre = attr.ea_ghce;
	hdl->ch_lhe = DAOS_EPOCH_MAX;

	rc = ec_increment(cont, EC_LRE, hdl->ch_lre);
	if (rc != 0)
		return rc;
	rc = ec_find_lowest(cont, EC_LRE, &attr.ea_glre);
	if (rc != 0)
		return rc;

	rc = ec_increment(cont, EC_LHE, hdl->ch_lhe);
	if (rc != 0)
		return rc;
	rc = ec_find_lowest(cont, EC_LHE, &attr.ea_glhe);
	if (rc != 0)
		return rc;

	if (check_epoch_invariant(cont, &attr, hdl) != 0)
		return -DER_IO;

	set_epoch_state(&attr, hdl, state);
	return 0;
}

/* Callers must abort the TX if this function returns an error. */
int
ds_cont_epoch_fini_hdl(struct cont *cont, struct container_hdl *hdl)
{
	struct epoch_attr	attr;
	int			rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_WORK);

	rc = read_epoch_attr(cont, &attr);
	if (rc != 0)
		return rc;

	if (check_epoch_invariant(cont, &attr, hdl) != 0)
		return -DER_IO;

	rc = ec_decrement(cont, EC_LRE, hdl->ch_lre);
	if (rc != 0)
		return rc;
	rc = ec_find_lowest(cont, EC_LRE, &attr.ea_glre);
	if (rc == -DER_NONEXIST)
		attr.ea_glre = DAOS_EPOCH_MAX;
	else if (rc != 0)
		return rc;

	rc = ec_decrement(cont, EC_LHE, hdl->ch_lhe);
	if (rc != 0)
		return rc;
	rc = ec_find_lowest(cont, EC_LHE, &attr.ea_glhe);
	if (rc == -DER_NONEXIST)
		attr.ea_glhe = DAOS_EPOCH_MAX;
	else if (rc != 0)
		return rc;

	rc = update_ghce(cont, &attr);
	if (rc != 0)
		return rc;

	if (check_global_epoch_invariant(cont, &attr) != 0)
		return -DER_IO;

	return rc;
}

int
ds_cont_epoch_read_state(struct cont *cont, struct container_hdl *hdl,
			 daos_epoch_state_t *state)
{
	struct epoch_attr		attr;
	int				rc;

	rc = read_epoch_attr(cont, &attr);
	if (rc != 0)
		return rc;

	set_epoch_state(&attr, hdl, state);
	return rc;
}

int
ds_cont_epoch_query(struct ds_pool_hdl *pool_hdl, struct cont *cont,
		    struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_epoch_op_out       *out = crt_reply_get(rpc);
	struct epoch_attr		attr;
	int				rc;

	rc = read_epoch_attr(cont, &attr);
	if (rc != 0)
		return rc;

	if (check_epoch_invariant(cont, &attr, hdl) != 0)
		return -DER_IO;

	set_epoch_state(&attr, hdl, &out->ceo_epoch_state);
	return 0;
}

int
ds_cont_epoch_hold(struct ds_pool_hdl *pool_hdl, struct cont *cont,
		   struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_epoch_op_in	       *in = crt_req_get(rpc);
	struct cont_epoch_op_out       *out = crt_reply_get(rpc);
	struct epoch_attr		attr;
	daos_epoch_t			lhe = hdl->ch_lhe;
	volatile int			rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: epoch="DF_U64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		in->cei_epoch);

	/* Verify the container handle capabilities. */
	if (!(hdl->ch_capas & DAOS_COO_RW))
		D_GOTO(out, rc = -DER_NO_PERM);

	if (in->cei_epoch > DAOS_EPOCH_MAX)
		D_GOTO(out, rc = -DER_OVERFLOW);

	rc = read_epoch_attr(cont, &attr);
	if (rc != 0)
		D_GOTO(out, rc);

	if (check_epoch_invariant(cont, &attr, hdl) != 0)
		D_GOTO(out, rc = -DER_IO);

	if (in->cei_epoch == 0)
		/*
		 * No specific requirement, as defined by daos_epoch_hold().
		 * Give back an epoch that can be used to read and overwrite
		 * all (partially) committed epochs.
		 */
		hdl->ch_lhe = attr.ea_ghpce + 1;
	else if (in->cei_epoch <= attr.ea_ghce)
		/* Immutable epochs cannot be held. */
		hdl->ch_lhe = attr.ea_ghce + 1;
	else
		hdl->ch_lhe = in->cei_epoch;

	if (hdl->ch_lhe == lhe)
		D_GOTO(out_state, rc = 0);

	D_DEBUG(DF_DSMS, "lhe="DF_U64" lhe'="DF_U64"\n", lhe, hdl->ch_lhe);

	TX_BEGIN(cont->c_svc->cs_mpool->mp_pmem) {
		rc = dbtree_uv_update(cont->c_svc->cs_hdls, in->cei_op.ci_hdl,
				      hdl, sizeof(*hdl));
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = ec_decrement(cont, EC_LHE, lhe);
		if (rc != 0)
			pmemobj_tx_abort(rc);
		rc = ec_increment(cont, EC_LHE, hdl->ch_lhe);
		if (rc != 0)
			pmemobj_tx_abort(rc);
		rc = ec_find_lowest(cont, EC_LHE, &attr.ea_glhe);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		/* If we are releasing held epochs, then update GHCE. */
		if (hdl->ch_lhe > lhe) {
			rc = update_ghce(cont, &attr);
			if (rc != 0)
				pmemobj_tx_abort(rc);
		}

		if (check_epoch_invariant(cont, &attr, hdl) != 0) {
			rc = -DER_IO;
			pmemobj_tx_abort(rc);
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_END

	if (rc != 0) {
		hdl->ch_lhe = lhe;
		D_GOTO(out, rc);
	}

out_state:
	set_epoch_state(&attr, hdl, &out->ceo_epoch_state);
out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		rc);
	return rc;
}

static int
cont_epoch_aggregate_bcast(crt_context_t ctx, struct cont *cont,
			   daos_epoch_range_t *epr)
{
	struct cont_tgt_epoch_aggregate_in	*in;
	struct cont_tgt_epoch_aggregate_out	*out;
	crt_rpc_t				*rpc;
	int					rc;

	D_DEBUG(DF_DSMS, DF_CONT" bcast epr: "DF_U64"->"DF_U64 "\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
		epr->epr_lo, epr->epr_hi);

	rc = ds_cont_bcast_create(ctx, cont->c_svc, CONT_TGT_EPOCH_AGGREGATE,
				  &rpc);
	if (rc != 0)
		D_GOTO(out, rc);

	in = crt_req_get(rpc);
	in->tai_start_epoch = epr->epr_lo;
	in->tai_end_epoch   = epr->epr_hi;
	uuid_copy(in->tai_pool_uuid, cont->c_svc->cs_pool_uuid);
	uuid_copy(in->tai_cont_uuid, cont->c_uuid);

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->tao_rc;

	if (rc != 0) {
		D_ERROR(DF_CONT",agg-bcast,e:"DF_U64"->"DF_U64":%d(targets)\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			epr->epr_lo, epr->epr_hi, rc);
		rc = -DER_IO;
	}

out_rpc:
	crt_req_decref(rpc);
out:
	D_DEBUG(DF_DSMS, DF_CONT": bcasted: %d\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);
	return rc;
}

int
ds_cont_epoch_slip(struct ds_pool_hdl *pool_hdl, struct cont *cont,
		   struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_epoch_op_in	       *in = crt_req_get(rpc);
	struct cont_epoch_op_out       *out = crt_reply_get(rpc);
	struct epoch_attr		attr;
	daos_epoch_t			lre = hdl->ch_lre;
	daos_epoch_t			glre_tmp;
	volatile int			rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: epoch="DF_U64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		in->cei_epoch);

	if (in->cei_epoch >= DAOS_EPOCH_MAX)
		D_GOTO(out, rc = -DER_OVERFLOW);

	rc = read_epoch_attr(cont, &attr);
	if (rc != 0)
		D_GOTO(out, rc);

	if (check_epoch_invariant(cont, &attr, hdl) != 0)
		D_GOTO(out, rc = -DER_IO);

	if (in->cei_epoch < hdl->ch_lre)
		/*
		 * Since we don't allow LRE to decrease, let the new LRE be the
		 * old one.  (This is actually unnecessary; we only have to
		 * guarantee that the new LRE has not been aggregated away.)
		 */
		;
	else
		hdl->ch_lre = in->cei_epoch;

	if (hdl->ch_lre == lre)
		D_GOTO(out_state, rc = 0);

	D_DEBUG(DF_DSMS, "lre="DF_U64" lre'="DF_U64"\n", lre, hdl->ch_lre);

	glre_tmp = attr.ea_glre;
	TX_BEGIN(cont->c_svc->cs_mpool->mp_pmem) {
		rc = dbtree_uv_update(cont->c_svc->cs_hdls, in->cei_op.ci_hdl,
				      hdl, sizeof(*hdl));
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = ec_decrement(cont, EC_LRE, lre);
		if (rc != 0)
			pmemobj_tx_abort(rc);
		rc = ec_increment(cont, EC_LRE, hdl->ch_lre);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = ec_find_lowest(cont, EC_LRE, &attr.ea_glre);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		if (check_epoch_invariant(cont, &attr, hdl) != 0) {
			rc = -DER_IO;
			pmemobj_tx_abort(rc);
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_END

	if (rc != 0) {
		hdl->ch_lre = lre;
		D_GOTO(out, rc);
	}

	/* Broadcast only if GLRE changed */
	if (attr.ea_glre > glre_tmp) {
		daos_epoch_range_t	range;

		range.epr_lo = 0;
		range.epr_hi = in->cei_epoch;
		/* trigger aggregation bcast here */
		rc = cont_epoch_aggregate_bcast(rpc->cr_ctx, cont, &range);
	}

out_state:
	set_epoch_state(&attr, hdl, &out->ceo_epoch_state);
out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		rc);
	return rc;
}

static int
cont_epoch_discard_bcast(crt_context_t ctx, struct cont *cont,
			 const uuid_t hdl_uuid, daos_epoch_t epoch)
{
	struct cont_tgt_epoch_discard_in       *in;
	struct cont_tgt_epoch_discard_out      *out;
	crt_rpc_t			       *rpc;
	int					rc;

	D_DEBUG(DF_DSMS, DF_CONT": bcasting\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid));

	rc = ds_cont_bcast_create(ctx, cont->c_svc, CONT_TGT_EPOCH_DISCARD,
				  &rpc);
	if (rc != 0)
		D_GOTO(out, rc);

	in = crt_req_get(rpc);
	uuid_copy(in->tii_hdl, hdl_uuid);
	in->tii_epoch = epoch;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->tio_rc;
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to discard epoch "DF_U64" for handle "
			DF_UUID" on %d targets\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), epoch,
			DP_UUID(hdl_uuid), rc);
		rc = -DER_IO;
	}

out_rpc:
	crt_req_decref(rpc);
out:
	D_DEBUG(DF_DSMS, DF_CONT": bcasted: %d\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);
	return rc;
}

int
ds_cont_epoch_discard(struct ds_pool_hdl *pool_hdl, struct cont *cont,
		      struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_epoch_op_in	       *in = crt_req_get(rpc);
	struct cont_epoch_op_out       *out = crt_reply_get(rpc);
	struct epoch_attr		attr;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID" epoch="
		DF_U64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		DP_UUID(in->cei_op.ci_hdl), in->cei_epoch);

	/* Verify the container handle capabilities. */
	if (!(hdl->ch_capas & DAOS_COO_RW))
		D_GOTO(out, rc = -DER_NO_PERM);

	if (in->cei_epoch >= DAOS_EPOCH_MAX)
		D_GOTO(out, rc = -DER_OVERFLOW);
	else if (in->cei_epoch < hdl->ch_lhe)
		/* Discarding an unheld epoch is not allowed. */
		D_GOTO(out, rc = -DER_EP_RO);

	rc = read_epoch_attr(cont, &attr);
	if (rc != 0)
		D_GOTO(out, rc);

	if (check_epoch_invariant(cont, &attr, hdl) != 0)
		D_GOTO(out, rc = -DER_IO);

	rc = cont_epoch_discard_bcast(rpc->cr_ctx, cont, in->cei_op.ci_hdl,
				      in->cei_epoch);

	set_epoch_state(&attr, hdl, &out->ceo_epoch_state);

out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		rc);
	return rc;
}

int
ds_cont_epoch_commit(struct ds_pool_hdl *pool_hdl, struct cont *cont,
		     struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_epoch_op_in	       *in = crt_req_get(rpc);
	struct cont_epoch_op_out       *out = crt_reply_get(rpc);
	struct epoch_attr		attr;
	daos_epoch_t			hce = hdl->ch_hce;
	daos_epoch_t			lhe = hdl->ch_lhe;
	daos_epoch_t			lre = hdl->ch_lre;
	volatile int			rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: epoch="DF_U64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		in->cei_epoch);

	/* Verify the container handle capabilities. */
	if (!(hdl->ch_capas & DAOS_COO_RW))
		D_GOTO(out, rc = -DER_NO_PERM);

	if (in->cei_epoch >= DAOS_EPOCH_MAX)
		D_GOTO(out, rc = -DER_OVERFLOW);

	rc = read_epoch_attr(cont, &attr);
	if (rc != 0)
		D_GOTO(out, rc);

	if (check_epoch_invariant(cont, &attr, hdl) != 0)
		D_GOTO(out, rc = -DER_IO);

	if (in->cei_epoch <= hdl->ch_hce)
		/* Committing an already committed epoch is okay and a no-op. */
		D_GOTO(out_state, rc = 0);
	else if (in->cei_epoch < hdl->ch_lhe)
		/* Committing an unheld epoch is not allowed. */
		D_GOTO(out, rc = -DER_EP_RO);

	hdl->ch_hce = in->cei_epoch;
	hdl->ch_lhe = hdl->ch_hce + 1;
	if (!(hdl->ch_capas & DAOS_COO_NOSLIP))
		hdl->ch_lre = hdl->ch_hce;

	D_DEBUG(DF_DSMS, "hce="DF_U64" hce'="DF_U64"\n", hce, hdl->ch_hce);
	D_DEBUG(DF_DSMS, "lhe="DF_U64" lhe'="DF_U64"\n", lhe, hdl->ch_lhe);
	D_DEBUG(DF_DSMS, "lre="DF_U64" lre'="DF_U64"\n", lre, hdl->ch_lre);

	TX_BEGIN(cont->c_svc->cs_mpool->mp_pmem) {
		rc = dbtree_uv_update(cont->c_svc->cs_hdls, in->cei_op.ci_hdl,
				      hdl, sizeof(*hdl));
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = ec_decrement(cont, EC_LHE, lhe);
		if (rc != 0)
			pmemobj_tx_abort(rc);
		rc = ec_increment(cont, EC_LHE, hdl->ch_lhe);
		if (rc != 0)
			pmemobj_tx_abort(rc);
		rc = ec_find_lowest(cont, EC_LHE, &attr.ea_glhe);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		if (!(hdl->ch_capas & DAOS_COO_NOSLIP)) {
			rc = ec_decrement(cont, EC_LRE, lre);
			if (rc != 0)
				pmemobj_tx_abort(rc);
			rc = ec_increment(cont, EC_LRE, hdl->ch_lre);
			if (rc != 0)
				pmemobj_tx_abort(rc);
			rc = ec_find_lowest(cont, EC_LRE, &attr.ea_glre);
			if (rc != 0)
				pmemobj_tx_abort(rc);
		}

		if (hdl->ch_hce > attr.ea_ghpce) {
			attr.ea_ghpce = hdl->ch_hce;
			rc = dbtree_nv_update(cont->c_cont, CONT_GHPCE,
					      strlen(CONT_GHPCE),
					      &attr.ea_ghpce,
					      sizeof(attr.ea_ghpce));
			if (rc != 0) {
				D_ERROR(DF_CONT": failed to update ghpce: %d\n",
					DP_CONT(cont->c_svc->cs_pool_uuid,
						cont->c_uuid), rc);
				pmemobj_tx_abort(rc);
			}
		}

		rc = update_ghce(cont, &attr);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		if (check_epoch_invariant(cont, &attr, hdl) != 0) {
			rc = -DER_IO;
			pmemobj_tx_abort(rc);
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_END

	if (rc != 0) {
		hdl->ch_lhe = lhe;
		hdl->ch_hce = hce;
		D_GOTO(out, rc);
	}

out_state:
	set_epoch_state(&attr, hdl, &out->ceo_epoch_state);
out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		rc);
	return rc;
}
