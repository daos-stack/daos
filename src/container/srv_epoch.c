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

#define DDSUBSYS	DDFAC(container)

#include <daos_srv/pool.h>
#include <daos_srv/rdb.h>
#include "rpc.h"
#include "srv_internal.h"
#include "srv_layout.h"

/*
 * Epoch counter (i.e., LRE/LHE KVSs) utilities
 */

enum ec_type {
	EC_LRE,
	EC_LHE
};

static rdb_path_t *
ec_type2kvs(struct cont *cont, enum ec_type type)
{
	switch (type) {
	case EC_LRE:
		return &cont->c_lres;
	case EC_LHE:
		return &cont->c_lhes;
	default:
		D__ASSERT(0);
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
		D__ASSERT(0);
	}
}

static int
ec_increment(struct rdb_tx *tx, struct cont *cont, enum ec_type type,
	     uint64_t epoch)
{
	rdb_path_t     *kvs = ec_type2kvs(cont, type);
	daos_iov_t	key;
	daos_iov_t	value;
	uint64_t	c = 0;
	uint64_t	c_new;
	int		rc;

	daos_iov_set(&key, &epoch, sizeof(epoch));
	daos_iov_set(&value, &c, sizeof(c));
	rc = rdb_tx_lookup(tx, kvs, &key, &value);
	if (rc != 0 && rc != -DER_NONEXIST)
		D__GOTO(out, rc);

	c_new = c + 1;
	if (c_new < c)
		D__GOTO(out, rc = -DER_OVERFLOW);

	daos_iov_set(&value, &c_new, sizeof(c_new));
	rc = rdb_tx_update(tx, kvs, &key, &value);

out:
	if (rc != 0)
		D__ERROR(DF_CONT": failed to increment %s epoch counter: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			ec_type2name(type), rc);
	return rc;
}

static int
ec_decrement(struct rdb_tx *tx, struct cont *cont, enum ec_type type,
	     uint64_t epoch)
{
	rdb_path_t     *kvs = ec_type2kvs(cont, type);
	daos_iov_t	key;
	daos_iov_t	value;
	uint64_t	c = 0;
	uint64_t	c_new;
	int		rc;

	daos_iov_set(&key, &epoch, sizeof(epoch));
	daos_iov_set(&value, &c, sizeof(c));
	rc = rdb_tx_lookup(tx, kvs, &key, &value);
	if (rc != 0 && rc != -DER_NONEXIST)
		D__GOTO(out, rc);

	c_new = c - 1;
	if (c_new > c)
		D__GOTO(out, rc = -DER_OVERFLOW);

	if (c_new == 0) {
		rc = rdb_tx_delete(tx, kvs, &key);
	} else {
		daos_iov_set(&value, &c_new, sizeof(c_new));
		rc = rdb_tx_update(tx, kvs, &key, &value);
	}

out:
	if (rc != 0)
		D__ERROR(DF_CONT": failed to decrement %s epoch counter: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			ec_type2name(type), rc);
	return rc;
}

static int
ec_find_lowest(struct rdb_tx *tx, struct cont *cont, enum ec_type type,
	       daos_epoch_t *epoch)
{
	rdb_path_t     *kvs = ec_type2kvs(cont, type);
	daos_iov_t	key;
	int		rc;

	daos_iov_set(&key, epoch, sizeof(*epoch));
	rc = rdb_tx_fetch(tx, kvs, RDB_PROBE_FIRST, NULL /* key_in */, &key,
			  NULL /* value */);
	if (rc == -DER_NONEXIST)
		D_DEBUG(DF_DSMS, DF_CONT": %s KVS empty: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			ec_type2name(type), rc);
	else if (rc != 0)
		D__ERROR(DF_CONT": failed to find lowest %s: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			ec_type2name(type), rc);
	return rc;
}

struct ec_decrement_iter_cb_arg {
	daos_epoch_t	eda_decremented;
	bool		eda_found;
	daos_epoch_t	eda_lowest;
};

static int
ec_decrement_iter_cb(daos_handle_t ih, daos_iov_t *key, daos_iov_t *val,
		     void *varg)
{
	struct ec_decrement_iter_cb_arg	       *arg = varg;
	uint64_t			       *epoch = key->iov_buf;
	uint64_t			       *counter = val->iov_buf;

	D__ASSERTF(key->iov_len == sizeof(epoch), DF_U64"\n", key->iov_len);
	D__ASSERTF(val->iov_len == sizeof(counter), DF_U64"\n", val->iov_len);
	/*
	 * If this epoch will be deleted from the KVS, then use the second
	 * lowest one.
	 */
	if (*epoch == arg->eda_decremented && *counter == 1)
		return 0;
	arg->eda_found = true;
	arg->eda_lowest = *epoch;
	return 1;
}

/*
 * Decrement epoch \a dec, if not NULL, increment epoch \a inc, if not NULL,
 * and then find the lowest epoch, in the \a type KVS. If after the update(s)
 * the KVS will become empty, then \a emptyp returns true; otherwise, \a lowestp
 * returns the lowest epoch. This is a little bit tricky because rdb does not
 * support querying a TX's own uncommitted updates.
 */
static int
ec_update_and_find_lowest(struct rdb_tx *tx, struct cont *cont,
			  enum ec_type type, const daos_epoch_t *dec,
			  const daos_epoch_t *inc, bool *emptyp,
			  daos_epoch_t *lowestp)
{
	rdb_path_t     *kvs = ec_type2kvs(cont, type);
	daos_epoch_t	lowest;
	bool		empty = false;
	int		rc;

	/* Decrement and set lowest and empty. */
	if (dec != NULL) {
		struct ec_decrement_iter_cb_arg arg;

		rc = ec_decrement(tx, cont, type, *dec);
		if (rc != 0)
			return rc;

		arg.eda_decremented = *dec;
		arg.eda_found = false;
		arg.eda_lowest = 0;
		rc = rdb_tx_iterate(tx, kvs, false /* !backward */,
				    ec_decrement_iter_cb, &arg);
		if (rc != 0) {
			D__ERROR(DF_CONT": failed to iterate %s KVS: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid),
				ec_type2name(type), rc);
			return rc;
		}
		if (arg.eda_found)
			lowest = arg.eda_lowest;
		else
			empty = true;
	} else {
		rc = ec_find_lowest(tx, cont, type, &lowest);
		if (rc == -DER_NONEXIST)
			empty = true;
		else if (rc != 0)
			return rc;
	}

	/* Increment and set lowest and empty. */
	if (inc != NULL) {
		rc = ec_increment(tx, cont, type, *inc);
		if (rc != 0)
			return rc;

		if (empty || lowest > *inc)
			lowest = *inc;
		empty = false;
	}

	if (emptyp != NULL)
		*emptyp = empty;
	if (lowestp != NULL && !empty)
		*lowestp = lowest;
	return 0;
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
epoch_aggregate_bcast(crt_context_t ctx, struct cont *cont,
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
		D__GOTO(out, rc);

	in = crt_req_get(rpc);
	in->tai_start_epoch = epr->epr_lo;
	in->tai_end_epoch   = epr->epr_hi;
	uuid_copy(in->tai_pool_uuid, cont->c_svc->cs_pool_uuid);
	uuid_copy(in->tai_cont_uuid, cont->c_uuid);

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D__GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->tao_rc;

	if (rc != 0) {
		D__ERROR(DF_CONT",agg-bcast,e:"DF_U64"->"DF_U64":%d(targets)\n",
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

static int
read_epoch_attr(struct rdb_tx *tx, struct cont *cont,
			struct epoch_attr *attr)
{
	daos_iov_t	value;
	daos_epoch_t	ghce;
	daos_epoch_t	ghpce;
	daos_epoch_t	glre;
	daos_epoch_t	glhe;
	int		rc;

	/* GHCE */
	daos_iov_set(&value, &ghce, sizeof(ghce));
	rc = rdb_tx_lookup(tx, &cont->c_attrs, &ds_cont_attr_ghce, &value);
	if (rc != 0) {
		D__ERROR(DF_CONT": failed to lookup GHCE: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);
		return rc;
	}

	/* GHPCE */
	daos_iov_set(&value, &ghpce, sizeof(ghpce));
	rc = rdb_tx_lookup(tx, &cont->c_attrs, &ds_cont_attr_ghpce, &value);
	if (rc != 0) {
		D__ERROR(DF_CONT": failed to lookup GHPCE: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);
		return rc;
	}

	/* GLRE */
	rc = ec_find_lowest(tx, cont, EC_LRE, &glre);
	if (rc == -DER_NONEXIST)
		glre = DAOS_EPOCH_MAX;
	else if (rc != 0)
		return rc;

	/* GLHE */
	rc = ec_find_lowest(tx, cont, EC_LHE, &glhe);
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

static int
trigger_aggregation(daos_epoch_t glre, daos_epoch_t glre_curr,
		    daos_epoch_t ghce, struct cont *cont, crt_context_t ctx)
{
	/** Trigger aggregation bcast here */
	/** Broadcast only if GLRE changed */
	if (glre_curr > glre) {
		daos_epoch_range_t	range;
		int			rc;

		/** Always trigger aggregation until glre - 1 */
		/** XXX this will change while introducing snapshots **/
		range.epr_lo	= 0;
		range.epr_hi	= MIN(glre_curr, ghce);
		rc = epoch_aggregate_bcast(ctx, cont, &range);
		return rc;
	}
	return 0;
}



/* Return 0 if the check passes, 1 otherwise. */
static inline int
check_global_epoch_invariant(struct cont *cont, struct epoch_attr *attr)
{
	if (!(attr->ea_ghce <= attr->ea_ghpce)) {
		D__ERROR(DF_CONT": GHCE "DF_U64" > GHPCE "DF_U64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			attr->ea_ghce, attr->ea_ghpce);
		return 1;
	}

	if (!(attr->ea_glhe == DAOS_EPOCH_MAX ||
	      attr->ea_glhe > attr->ea_ghce)) {
		D__ERROR(DF_CONT": GLHE "DF_U64" <= GHCE "DF_U64"\n",
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
		D__ERROR(DF_CONT": HCE "DF_U64" >= LHE "DF_U64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			hdl->ch_hce, hdl->ch_lhe);
		return 1;
	}

	if (!(attr->ea_glre <= hdl->ch_lre)) {
		D__ERROR(DF_CONT": GLRE "DF_U64" > LRE "DF_U64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			attr->ea_glre, hdl->ch_lre);
		return 1;
	}

	if (!(attr->ea_ghce < hdl->ch_lhe)) {
		D__ERROR(DF_CONT": GHCE "DF_U64" >= LHE "DF_U64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			attr->ea_ghce, hdl->ch_lhe);
		return 1;
	}

	if (!(attr->ea_ghpce >= hdl->ch_hce)) {
		D__ERROR(DF_CONT": GHPCE "DF_U64" < HCE "DF_U64"\n",
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
update_ghce(struct rdb_tx *tx, struct cont *cont, struct epoch_attr *attr)
{
	daos_epoch_t	ghce;
	daos_iov_t	value;
	int		rc;

	/*
	 * GHCE shall be the highest epoch e satisfying both of these:
	 *   - e <= GHPCE (committed by some handle)
	 *   - e < GLHE (not held by any handle)
	 */
	ghce = min(attr->ea_ghpce, attr->ea_glhe - 1);

	if (ghce < attr->ea_ghce) {
		D__ERROR(DF_CONT": GHCE would decrease: "DF_EPOCH_ATTR"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			DP_EPOCH_ATTR(attr));
		return -DER_IO;
	} else if (ghce == attr->ea_ghce) {
		return 0;
	}

	daos_iov_set(&value, &ghce, sizeof(ghce));
	rc = rdb_tx_update(tx, &cont->c_attrs, &ds_cont_attr_ghce, &value);
	if (rc != 0)
		D__ERROR(DF_CONT": failed to update ghce: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid,
				cont->c_uuid), rc);

	attr->ea_ghce = ghce;
	return rc;
}

int
ds_cont_epoch_init_hdl(struct rdb_tx *tx, struct cont *cont,
		       struct container_hdl *hdl, daos_epoch_state_t *state)
{
	struct epoch_attr	attr;
	int			rc;

	rc = read_epoch_attr(tx, cont, &attr);
	if (rc != 0)
		return rc;

	if (check_global_epoch_invariant(cont, &attr) != 0)
		return -DER_IO;

	hdl->ch_hce = attr.ea_ghce;
	hdl->ch_lre = attr.ea_ghce;
	hdl->ch_lhe = DAOS_EPOCH_MAX;

	/* Determine the new GLRE and update the LRE KVS. */
	rc = ec_update_and_find_lowest(tx, cont, EC_LRE, NULL /* dec */,
				       &hdl->ch_lre /* inc */,
				       NULL /* emptyp */, &attr.ea_glre);
	if (rc != 0)
		return rc;

	/* Determine the new GLHE and update the LHE KVS. */
	rc = ec_update_and_find_lowest(tx, cont, EC_LHE, NULL /* dec */,
				       &hdl->ch_lhe /* inc */,
				       NULL /* emptyp */, &attr.ea_glhe);
	if (rc != 0)
		return rc;

	if (check_epoch_invariant(cont, &attr, hdl) != 0)
		return -DER_IO;

	set_epoch_state(&attr, hdl, state);
	return 0;
}

int
ds_cont_epoch_fini_hdl(struct rdb_tx *tx, struct cont *cont,
		       crt_context_t ctx, struct container_hdl *hdl)
{
	struct epoch_attr	attr;
	daos_epoch_t		glre;
	bool			empty;
	char			*islip;
	bool			slip_flag;
	int			rc;

	rc = read_epoch_attr(tx, cont, &attr);
	if (rc != 0)
		return rc;

	slip_flag = false;
	islip = getenv("DAOS_IMPLICIT_PURGE");
	if (islip != NULL)
		slip_flag = true;
	glre = attr.ea_glre;

	if (check_epoch_invariant(cont, &attr, hdl) != 0)
		return -DER_IO;

	/* Determine the new GLRE and update the LRE KVS. */
	rc = ec_update_and_find_lowest(tx, cont, EC_LRE, &hdl->ch_lre /* dec */,
				       NULL /* inc */, &empty, &attr.ea_glre);
	if (rc != 0)
		return rc;
	if (empty)
		attr.ea_glre = DAOS_EPOCH_MAX;

	/* Determine the new GLHE and update the LHE KVS. */
	rc = ec_update_and_find_lowest(tx, cont, EC_LHE, &hdl->ch_lhe /* dec */,
				       NULL /* inc */, &empty, &attr.ea_glhe);
	if (rc != 0)
		return rc;
	if (empty)
		attr.ea_glhe = DAOS_EPOCH_MAX;

	rc = update_ghce(tx, cont, &attr);
	if (rc != 0)
		return rc;

	if (check_global_epoch_invariant(cont, &attr) != 0)
		return -DER_IO;

	/** once we have an aggregation daemon, mask this error message */
	if (slip_flag)
		rc = trigger_aggregation(glre, attr.ea_glre, attr.ea_ghce,
					 cont, ctx);

	return rc;
}

int
ds_cont_epoch_read_state(struct rdb_tx *tx, struct cont *cont,
			 struct container_hdl *hdl, daos_epoch_state_t *state)
{
	struct epoch_attr	attr;
	int			rc;

	rc = read_epoch_attr(tx, cont, &attr);
	if (rc != 0)
		return rc;

	set_epoch_state(&attr, hdl, state);
	return 0;
}

int
ds_cont_epoch_query(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		    struct cont *cont, struct container_hdl *hdl,
		    crt_rpc_t *rpc)
{
	struct cont_epoch_op_out       *out = crt_reply_get(rpc);
	struct epoch_attr		attr;
	int				rc;

	rc = read_epoch_attr(tx, cont, &attr);
	if (rc != 0)
		return rc;

	if (check_epoch_invariant(cont, &attr, hdl) != 0)
		return -DER_IO;

	set_epoch_state(&attr, hdl, &out->ceo_epoch_state);
	return 0;
}

int
ds_cont_epoch_hold(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		   struct cont *cont, struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_epoch_op_in	       *in = crt_req_get(rpc);
	struct cont_epoch_op_out       *out = crt_reply_get(rpc);
	struct epoch_attr		attr;
	daos_epoch_t			lhe = hdl->ch_lhe;
	daos_iov_t			key;
	daos_iov_t			value;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: epoch="DF_U64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		in->cei_epoch);

	/* Verify the container handle capabilities. */
	if (!(hdl->ch_capas & DAOS_COO_RW))
		D__GOTO(out, rc = -DER_NO_PERM);

	if (in->cei_epoch > DAOS_EPOCH_MAX)
		D__GOTO(out, rc = -DER_OVERFLOW);

	rc = read_epoch_attr(tx, cont, &attr);
	if (rc != 0)
		D__GOTO(out, rc);

	if (check_epoch_invariant(cont, &attr, hdl) != 0)
		D__GOTO(out, rc = -DER_IO);

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
		D__GOTO(out_state, rc = 0);

	D_DEBUG(DF_DSMS, "lhe="DF_U64" lhe'="DF_U64"\n", lhe, hdl->ch_lhe);

	daos_iov_set(&key, in->cei_op.ci_hdl, sizeof(uuid_t));
	daos_iov_set(&value, hdl, sizeof(*hdl));
	rc = rdb_tx_update(tx, &cont->c_svc->cs_hdls, &key, &value);
	if (rc != 0)
		D__GOTO(out_hdl, rc);

	rc = ec_update_and_find_lowest(tx, cont, EC_LHE, &lhe /* dec */,
				       &hdl->ch_lhe /* inc */,
				       NULL /* emptyp */, &attr.ea_glhe);
	if (rc != 0)
		D__GOTO(out_hdl, rc);

	/* If we are releasing held epochs, then update GHCE. */
	if (hdl->ch_lhe > lhe) {
		rc = update_ghce(tx, cont, &attr);
		if (rc != 0)
			D__GOTO(out_hdl, rc);
	}

	if (check_epoch_invariant(cont, &attr, hdl) != 0)
		D__GOTO(out_hdl, rc = -DER_IO);

out_state:
	set_epoch_state(&attr, hdl, &out->ceo_epoch_state);
out_hdl:
	if (rc != 0)
		hdl->ch_lhe = lhe;
out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		rc);
	return rc;
}

int
ds_cont_epoch_slip(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		   struct cont *cont, struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_epoch_op_in	       *in = crt_req_get(rpc);
	struct cont_epoch_op_out       *out = crt_reply_get(rpc);
	struct epoch_attr		attr;
	daos_epoch_t			lre = hdl->ch_lre;
	daos_epoch_t			glre;
	daos_iov_t			key;
	daos_iov_t			value;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: epoch="DF_U64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		in->cei_epoch);

	if (in->cei_epoch >= DAOS_EPOCH_MAX)
		D__GOTO(out, rc = -DER_OVERFLOW);

	rc = read_epoch_attr(tx, cont, &attr);
	if (rc != 0)
		D__GOTO(out, rc);
	glre = attr.ea_glre;

	if (check_epoch_invariant(cont, &attr, hdl) != 0)
		D__GOTO(out, rc = -DER_IO);

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
		D__GOTO(out_state, rc = 0);

	D_DEBUG(DF_DSMS, "lre="DF_U64" lre'="DF_U64"\n", lre, hdl->ch_lre);

	daos_iov_set(&key, in->cei_op.ci_hdl, sizeof(uuid_t));
	daos_iov_set(&value, hdl, sizeof(*hdl));
	rc = rdb_tx_update(tx, &cont->c_svc->cs_hdls, &key, &value);
	if (rc != 0)
		D__GOTO(out_hdl, rc);

	rc = ec_update_and_find_lowest(tx, cont, EC_LRE, &lre /* dec */,
				       &hdl->ch_lre /* inc */,
				       NULL /* emptyp */, &attr.ea_glre);
	if (rc != 0)
		D__GOTO(out_hdl, rc);

	if (check_epoch_invariant(cont, &attr, hdl) != 0)
		D__GOTO(out_hdl, rc = -DER_IO);

	/** XXX
	 * Once we have an aggregation daemon,
	 * we need to mask the return value, we need not
	 * fail if aggregation bcast fails
	 */
	rc = trigger_aggregation(glre, attr.ea_glre, attr.ea_ghce,
				 cont, rpc->cr_ctx);
out_state:
	set_epoch_state(&attr, hdl, &out->ceo_epoch_state);
out_hdl:
	if (rc != 0)
		hdl->ch_lre = lre;
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
		D__GOTO(out, rc);

	in = crt_req_get(rpc);
	uuid_copy(in->tii_hdl, hdl_uuid);
	in->tii_epoch = epoch;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D__GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->tio_rc;
	if (rc != 0) {
		D__ERROR(DF_CONT": failed to discard epoch "DF_U64" for handle "
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
ds_cont_epoch_discard(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		      struct cont *cont, struct container_hdl *hdl,
		      crt_rpc_t *rpc)
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
		D__GOTO(out, rc = -DER_NO_PERM);

	if (in->cei_epoch >= DAOS_EPOCH_MAX)
		D__GOTO(out, rc = -DER_OVERFLOW);
	else if (in->cei_epoch < hdl->ch_lhe)
		/* Discarding an unheld epoch is not allowed. */
		D__GOTO(out, rc = -DER_EP_RO);

	rc = read_epoch_attr(tx, cont, &attr);
	if (rc != 0)
		D__GOTO(out, rc);

	if (check_epoch_invariant(cont, &attr, hdl) != 0)
		D__GOTO(out, rc = -DER_IO);

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
ds_cont_epoch_commit(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		     struct cont *cont, struct container_hdl *hdl,
		     crt_rpc_t *rpc)
{
	struct cont_epoch_op_in	       *in = crt_req_get(rpc);
	struct cont_epoch_op_out       *out = crt_reply_get(rpc);
	struct epoch_attr		attr;
	daos_epoch_t			hce = hdl->ch_hce;
	daos_epoch_t			lhe = hdl->ch_lhe;
	daos_epoch_t			lre = hdl->ch_lre;
	daos_epoch_t			glre = 0;
	daos_iov_t			key;
	daos_iov_t			value;
	char				*islip;
	bool				slip_flag;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: epoch="DF_U64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		in->cei_epoch);

	slip_flag = false;
	islip	  = getenv("DAOS_IMPLICIT_PURGE");
	if (islip != NULL)
		slip_flag = true;

	/* Verify the container handle capabilities. */
	if (!(hdl->ch_capas & DAOS_COO_RW))
		D__GOTO(out, rc = -DER_NO_PERM);

	if (in->cei_epoch >= DAOS_EPOCH_MAX)
		D__GOTO(out, rc = -DER_OVERFLOW);

	rc = read_epoch_attr(tx, cont, &attr);
	if (rc != 0)
		D__GOTO(out, rc);

	if (slip_flag)
		glre = attr.ea_glre;

	if (check_epoch_invariant(cont, &attr, hdl) != 0)
		D__GOTO(out, rc = -DER_IO);

	if (in->cei_epoch <= hdl->ch_hce)
		/* Committing an already committed epoch is okay and a no-op. */
		D__GOTO(out_state, rc = 0);
	else if (in->cei_epoch < hdl->ch_lhe)
		/* Committing an unheld epoch is not allowed. */
		D__GOTO(out, rc = -DER_EP_RO);

	hdl->ch_hce = in->cei_epoch;
	hdl->ch_lhe = hdl->ch_hce + 1;
	if (!(hdl->ch_capas & DAOS_COO_NOSLIP))
		hdl->ch_lre = hdl->ch_hce;

	D_DEBUG(DF_DSMS, "hce="DF_U64" hce'="DF_U64"\n", hce, hdl->ch_hce);
	D_DEBUG(DF_DSMS, "lhe="DF_U64" lhe'="DF_U64"\n", lhe, hdl->ch_lhe);
	D_DEBUG(DF_DSMS, "lre="DF_U64" lre'="DF_U64"\n", lre, hdl->ch_lre);

	daos_iov_set(&key, in->cei_op.ci_hdl, sizeof(uuid_t));
	daos_iov_set(&value, hdl, sizeof(*hdl));
	rc = rdb_tx_update(tx, &cont->c_svc->cs_hdls, &key, &value);
	if (rc != 0)
		D__GOTO(out_hdl, rc);

	rc = ec_update_and_find_lowest(tx, cont, EC_LHE, &lhe /* dec */,
				       &hdl->ch_lhe /* inc */,
				       NULL /* emptyp */, &attr.ea_glhe);
	if (rc != 0)
		D__GOTO(out_hdl, rc);

	if (!(hdl->ch_capas & DAOS_COO_NOSLIP)) {
		rc = ec_update_and_find_lowest(tx, cont, EC_LRE, &lre /* dec */,
					       &hdl->ch_lre /* inc */,
					       NULL /* emptyp */,
					       &attr.ea_glre);
		if (rc != 0)
			D__GOTO(out_hdl, rc);
	}

	if (hdl->ch_hce > attr.ea_ghpce) {
		attr.ea_ghpce = hdl->ch_hce;
		daos_iov_set(&value, &attr.ea_ghpce, sizeof(attr.ea_ghpce));
		rc = rdb_tx_update(tx, &cont->c_attrs, &ds_cont_attr_ghpce,
				   &value);
		if (rc != 0) {
			D__ERROR(DF_CONT": failed to update ghpce: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			D__GOTO(out_hdl, rc);
		}
	}

	rc = update_ghce(tx, cont, &attr);
	if (rc != 0)
		D__GOTO(out_hdl, rc);

	if (check_epoch_invariant(cont, &attr, hdl) != 0)
		D__GOTO(out_hdl, rc = -DER_IO);

	if (slip_flag) {
		rc = trigger_aggregation(glre, attr.ea_glre, attr.ea_ghce,
					 cont, rpc->cr_ctx);
		if (rc != 0) {
			D__ERROR("Trigger aggregation from commit failed %d\n",
				rc);
			D__GOTO(out_hdl, rc);
		}
	}

out_state:
	set_epoch_state(&attr, hdl, &out->ceo_epoch_state);
out_hdl:
	if (rc != 0) {
		hdl->ch_lre = lre;
		hdl->ch_lhe = lhe;
		hdl->ch_hce = hce;
	}
out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		rc);
	return rc;
}
