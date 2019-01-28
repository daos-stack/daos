/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(container)

#include <daos_srv/pool.h>
#include <daos_srv/rdb.h>
#include "rpc.h"
#include "srv_internal.h"
#include "srv_layout.h"

static inline bool
auto_slip_enabled()
{
	bool auto_slip = false;

	d_getenv_bool("DAOS_IMPLICIT_PURGE", &auto_slip);
	return auto_slip;
}

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
		D_ASSERT(0);
	}
	return NULL;
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
	return NULL;
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
		D_GOTO(out, rc);

	c_new = c + 1;
	if (c_new < c)
		D_GOTO(out, rc = -DER_OVERFLOW);

	daos_iov_set(&value, &c_new, sizeof(c_new));
	rc = rdb_tx_update(tx, kvs, &key, &value);

out:
	if (rc != 0)
		D_ERROR(DF_CONT": failed to increment %s epoch counter: %d\n",
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
		D_GOTO(out, rc);

	c_new = c - 1;
	if (c_new > c)
		D_GOTO(out, rc = -DER_OVERFLOW);

	if (c_new == 0) {
		rc = rdb_tx_delete(tx, kvs, &key);
	} else {
		daos_iov_set(&value, &c_new, sizeof(c_new));
		rc = rdb_tx_update(tx, kvs, &key, &value);
	}

out:
	if (rc != 0)
		D_ERROR(DF_CONT": failed to decrement %s epoch counter: %d\n",
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
		D_ERROR(DF_CONT": failed to find lowest %s: %d\n",
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

	D_ASSERTF(key->iov_len == sizeof(epoch), DF_U64"\n", key->iov_len);
	D_ASSERTF(val->iov_len == sizeof(counter), DF_U64"\n", val->iov_len);
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
			D_ERROR(DF_CONT": failed to iterate %s KVS: %d\n",
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

/* Buffer for epoch-related container properties (global epoch state) */
struct epoch_prop {
	daos_epoch_t	ep_ghce;
	daos_epoch_t	ep_ghpce;
	daos_epoch_t	ep_glre;	/* DAOS_EPOCH_MAX if no refs */
	daos_epoch_t	ep_glhe;	/* DAOS_EPOCH_MAX if no holds */
};

static int
epoch_aggregate_bcast(crt_context_t ctx, struct cont *cont,
		      daos_epoch_range_t *epr, size_t count)
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
	in->tai_epr_list.ca_arrays = epr;
	in->tai_epr_list.ca_count = count;
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

static int
read_epoch_prop(struct rdb_tx *tx, struct cont *cont,
			struct epoch_prop *prop)
{
	daos_iov_t	value;
	daos_epoch_t	ghce;
	daos_epoch_t	ghpce;
	daos_epoch_t	glre;
	daos_epoch_t	glhe;
	int		rc;

	/* GHCE */
	daos_iov_set(&value, &ghce, sizeof(ghce));
	rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_ghce, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to lookup GHCE: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);
		return rc;
	}

	/* GHPCE */
	daos_iov_set(&value, &ghpce, sizeof(ghpce));
	rc = rdb_tx_lookup(tx, &cont->c_prop, &ds_cont_prop_ghpce, &value);
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to lookup GHPCE: %d\n",
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

	prop->ep_ghce = ghce;
	prop->ep_ghpce = ghpce;
	prop->ep_glre = glre;
	prop->ep_glhe = glhe;
	return 0;
}

struct snap_list_iter_args {
	int		 sla_index;
	int		 sla_count;
	int		 sla_max_count;
	daos_epoch_t	*sla_buf;
};

static int
snap_list_iter_cb(daos_handle_t ih, daos_iov_t *key, daos_iov_t *val,
		  void *arg)
{
	struct snap_list_iter_args *i_args = arg;

	D_ASSERTF(key->iov_len == sizeof(daos_epoch_t),
		  DF_U64"\n", key->iov_len);

	if (i_args->sla_count > 0) {
		/* Check if we've reached capacity */
		if (i_args->sla_index == i_args->sla_count)  {
			/* Increase capacity exponentially */
			i_args->sla_count *= 2;
			/* If max_count < 0, there is no upper limit */
			if (i_args->sla_max_count > 0 &&
			    i_args->sla_max_count < i_args->sla_count)
				i_args->sla_count = i_args->sla_max_count;

			/* Re-allocate only if count actually increased */
			if (i_args->sla_index < i_args->sla_count) {
				void *ptr;

				D_REALLOC(ptr, i_args->sla_buf,
					  i_args->sla_count *
					  sizeof(daos_epoch_t));
				if (ptr == NULL)
					return -DER_NOMEM;
				i_args->sla_buf = ptr;
			}
		}

		if (i_args->sla_index < i_args->sla_count)
			memcpy(&i_args->sla_buf[i_args->sla_index],
			       key->iov_buf, sizeof(daos_epoch_t));
	}
	++i_args->sla_index;
	return 0;
}

static int
read_snap_list(struct rdb_tx *tx, struct cont *cont,
	       daos_epoch_t **buf, int *count)
{
	struct snap_list_iter_args iter_args;
	int rc;

	iter_args.sla_index = 0;
	iter_args.sla_max_count = *count;
	if (*count != 0) {
		/* start with initial size then grow the buffer */
		iter_args.sla_count = *count > 0 && *count < 64 ? *count : 64;
		D_ALLOC_ARRAY(iter_args.sla_buf, iter_args.sla_count);
		if (iter_args.sla_buf == NULL)
			return -DER_NOMEM;
	} else {
		iter_args.sla_count = 0;
		iter_args.sla_buf = NULL;
	}
	rc = rdb_tx_iterate(tx, &cont->c_snaps, false /* !backward */,
			    snap_list_iter_cb, &iter_args);
	if (rc != 0) {
		D_FREE(iter_args.sla_buf);
		return rc;
	}
	*count = iter_args.sla_index;
	*buf   = iter_args.sla_buf;
	return 0;
}

static int
trigger_aggregation(struct rdb_tx *tx,
		    daos_epoch_t glre, daos_epoch_t glre_curr,
		    daos_epoch_t ghce, struct cont *cont, crt_context_t ctx)
{
	daos_epoch_t		*snapshots;
	int			 snap_count;
	daos_epoch_range_t	 target_range;
	daos_epoch_range_t	*ranges;
	int			 range_count;
	int			 start;
	int			 i;
	int			 rc = 0;

	/* glre and glre_curr can take the value DAOS_EPOCH_MAX */
	target_range.epr_lo = glre < DAOS_EPOCH_MAX ? glre : 0;
	target_range.epr_hi = MIN(ghce, glre_curr);
	if (target_range.epr_hi <= target_range.epr_lo)
		goto out;
	D_DEBUG(DF_DSMS, DF_CONT": Move GLRE { "DF_U64" => "DF_U64" }\n",
			 DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			 target_range.epr_lo, target_range.epr_hi);

	/* Aggregate between all snapshots less than GLRE */
	snap_count = -1; /* No upper limit */
	rc = read_snap_list(tx, cont, &snapshots, &snap_count);
	if (rc != 0)
		goto out;
	/* Start from highest snapshot less than old GLRE */
	for (i = 0; i < snap_count &&
	     snapshots[i] < target_range.epr_lo; ++i)
		;
	start = i - 1;

	/* range_count will be at least `1` and at most `snap_count + 1` */
	for (range_count = 1; i < snap_count &&
	     snapshots[i] < target_range.epr_hi; ++range_count, ++i)
	     ;

	D_DEBUG(DF_DSMS, DF_CONT": snap_count=%d range_count=%d start=%d\n",
			 DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			 snap_count, range_count, start);

	D_ALLOC_ARRAY(ranges, range_count);
	if (ranges == NULL) {
		rc = -DER_NOMEM;
		goto out_snap;
	}

	/* If start == -1, old GLRE is less than lowest snapshot */
	ranges[0].epr_lo = start < 0 ? 0UL : snapshots[start];
	for (i = 1; i < range_count; ++i) {
		ranges[i - 1].epr_hi = snapshots[start + i];
		ranges[i].epr_lo = snapshots[start + i] + 1;
	}
	ranges[range_count - 1].epr_hi = target_range.epr_hi;
	rc = epoch_aggregate_bcast(ctx, cont, ranges, range_count);
	D_FREE(ranges);
out_snap:
	D_FREE(snapshots);
out:
	return rc;
}

/* Return 0 if the check passes, 1 otherwise. */
static inline int
check_global_epoch_invariant(struct cont *cont, struct epoch_prop *prop)
{
	if (!(prop->ep_ghce <= prop->ep_ghpce)) {
		D_ERROR(DF_CONT": GHCE "DF_U64" > GHPCE "DF_U64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			prop->ep_ghce, prop->ep_ghpce);
		return 1;
	}

	if (!(prop->ep_glhe == DAOS_EPOCH_MAX ||
	      prop->ep_glhe > prop->ep_ghce)) {
		D_ERROR(DF_CONT": GLHE "DF_U64" <= GHCE "DF_U64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			prop->ep_glhe, prop->ep_ghce);
		return 1;
	}

	return 0;
}

/* Return 0 if the check passes, 1 otherwise. */
static inline int
check_epoch_invariant(struct cont *cont, struct epoch_prop *prop,
		      struct container_hdl *hdl)
{
	if (check_global_epoch_invariant(cont, prop) != 0)
		return 1;

	if (!(hdl->ch_hce < hdl->ch_lhe)) {
		D_ERROR(DF_CONT": HCE "DF_U64" >= LHE "DF_U64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			hdl->ch_hce, hdl->ch_lhe);
		return 1;
	}

	if (!(prop->ep_glre <= hdl->ch_lre)) {
		D_ERROR(DF_CONT": GLRE "DF_U64" > LRE "DF_U64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			prop->ep_glre, hdl->ch_lre);
		return 1;
	}

	if (!(prop->ep_ghce < hdl->ch_lhe)) {
		D_ERROR(DF_CONT": GHCE "DF_U64" >= LHE "DF_U64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			prop->ep_ghce, hdl->ch_lhe);
		return 1;
	}

	if (!(prop->ep_ghpce >= hdl->ch_hce)) {
		D_ERROR(DF_CONT": GHPCE "DF_U64" < HCE "DF_U64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			prop->ep_ghpce, hdl->ch_hce);
		return 1;
	}

	return 0;
}

/*
 * Calculate GHCE afresh using prop->ep_ghpce and prop->ep_glhe, and if the
 * result is higher than prop->ep_ghce, update prop->ep_ghce and CONT_GHCE.
 *
 * This function must be called by operations change GHPCE or GLHE. It may be
 * called unnecessarily by other operations without compromising safety.
 */
static int
update_ghce(struct rdb_tx *tx, struct cont *cont, struct epoch_prop *prop)
{
	daos_epoch_t	ghce;
	daos_iov_t	value;
	int		rc;

	/*
	 * GHCE shall be the highest epoch e satisfying both of these:
	 *   - e <= GHPCE (committed by some handle)
	 *   - e < GLHE (not held by any handle)
	 */
	ghce = min(prop->ep_ghpce, prop->ep_glhe - 1);

	if (ghce < prop->ep_ghce) {
		D_ERROR(DF_CONT": GHCE would decrease: GLRE="DF_U64" GHCE="
			DF_U64" GHPCE="DF_U64 " GLHE="DF_U64"\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
			prop->ep_glre, prop->ep_ghce, prop->ep_ghpce,
			prop->ep_glhe);
		return -DER_IO;
	} else if (ghce == prop->ep_ghce) {
		return 0;
	}

	daos_iov_set(&value, &ghce, sizeof(ghce));
	rc = rdb_tx_update(tx, &cont->c_prop, &ds_cont_prop_ghce, &value);
	if (rc != 0)
		D_ERROR(DF_CONT": failed to update ghce: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid,
				cont->c_uuid), rc);

	prop->ep_ghce = ghce;
	return rc;
}

int
ds_cont_epoch_init_hdl(struct rdb_tx *tx, struct cont *cont, uuid_t c_hdl,
		       struct container_hdl *hdl)
{
	struct epoch_prop	prop;
	int			rc;

	rc = read_epoch_prop(tx, cont, &prop);
	if (rc != 0)
		return rc;

	if (check_global_epoch_invariant(cont, &prop) != 0)
		return -DER_IO;

	hdl->ch_hce = prop.ep_ghce;
	hdl->ch_lre = prop.ep_ghce;
	hdl->ch_lhe = DAOS_EPOCH_MAX;

	/* Determine the new GLRE and update the LRE KVS. */
	rc = ec_update_and_find_lowest(tx, cont, EC_LRE, NULL /* dec */,
				       &hdl->ch_lre /* inc */,
				       NULL /* emptyp */, &prop.ep_glre);
	if (rc != 0)
		return rc;

	/*
	 * TODO - For now, do an epoch hold on container open since
	 * daos_epoch_hold() is removed from API. Revisit when new epoch model
	 * implemented.
	 */
	if (hdl->ch_capas & DAOS_COO_RW) {
		daos_iov_t	key;
		daos_iov_t	value;

		hdl->ch_lhe = prop.ep_ghpce + 1;

		daos_iov_set(&key, c_hdl, sizeof(uuid_t));
		daos_iov_set(&value, hdl, sizeof(*hdl));
		rc = rdb_tx_update(tx, &cont->c_svc->cs_hdls, &key, &value);
		if (rc != 0)
			return rc;
	}

	/* Determine the new GLHE and update the LHE KVS. */
	rc = ec_update_and_find_lowest(tx, cont, EC_LHE, NULL /* dec */,
				       &hdl->ch_lhe /* inc */,
				       NULL /* emptyp */, &prop.ep_glhe);
	if (rc != 0)
		return rc;

	if (check_epoch_invariant(cont, &prop, hdl) != 0)
		return -DER_IO;

	return 0;
}

int
ds_cont_epoch_fini_hdl(struct rdb_tx *tx, struct cont *cont,
		       crt_context_t ctx, struct container_hdl *hdl)
{
	struct epoch_prop	prop;
	daos_epoch_t		glre;
	bool			empty;
	bool			slip_flag;
	int			rc;

	rc = read_epoch_prop(tx, cont, &prop);
	if (rc != 0)
		return rc;

	slip_flag = auto_slip_enabled();
	glre = prop.ep_glre;

	if (check_epoch_invariant(cont, &prop, hdl) != 0)
		return -DER_IO;

	/* Determine the new GLRE and update the LRE KVS. */
	rc = ec_update_and_find_lowest(tx, cont, EC_LRE, &hdl->ch_lre /* dec */,
				       NULL /* inc */, &empty, &prop.ep_glre);
	if (rc != 0)
		return rc;
	if (empty)
		prop.ep_glre = DAOS_EPOCH_MAX;

	/* Determine the new GLHE and update the LHE KVS. */
	rc = ec_update_and_find_lowest(tx, cont, EC_LHE, &hdl->ch_lhe /* dec */,
				       NULL /* inc */, &empty, &prop.ep_glhe);
	if (rc != 0)
		return rc;
	if (empty)
		prop.ep_glhe = DAOS_EPOCH_MAX;

	rc = update_ghce(tx, cont, &prop);
	if (rc != 0)
		return rc;

	if (check_global_epoch_invariant(cont, &prop) != 0)
		return -DER_IO;

	/** once we have an aggregation daemon, mask this error message */
	if (slip_flag)
		rc = trigger_aggregation(tx, glre, prop.ep_glre, prop.ep_ghce,
					 cont, ctx);

	return rc;
}

int
ds_cont_epoch_hold(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		   struct cont *cont, struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_epoch_op_in	       *in = crt_req_get(rpc);
	struct epoch_prop		prop;
	daos_epoch_t			lhe = hdl->ch_lhe;
	daos_iov_t			key;
	daos_iov_t			value;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: epoch="DF_U64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		in->cei_epoch);

	/* Verify the container handle capabilities. */
	if (!(hdl->ch_capas & DAOS_COO_RW))
		D_GOTO(out, rc = -DER_NO_PERM);

	if (in->cei_epoch > DAOS_EPOCH_MAX)
		D_GOTO(out, rc = -DER_OVERFLOW);

	rc = read_epoch_prop(tx, cont, &prop);
	if (rc != 0)
		D_GOTO(out, rc);

	if (check_epoch_invariant(cont, &prop, hdl) != 0)
		D_GOTO(out, rc = -DER_IO);

	if (in->cei_epoch == 0)
		/*
		 * No specific requirement, as defined by daos_epoch_hold().
		 * Give back an epoch that can be used to read and overwrite
		 * all (partially) committed epochs.
		 */
		hdl->ch_lhe = prop.ep_ghpce + 1;
	else if (in->cei_epoch <= prop.ep_ghce)
		/* Immutable epochs cannot be held. */
		hdl->ch_lhe = prop.ep_ghce + 1;
	else
		hdl->ch_lhe = in->cei_epoch;

	if (hdl->ch_lhe == lhe)
		D_GOTO(out_hdl, rc = 0);

	D_DEBUG(DF_DSMS, "lhe="DF_U64" lhe'="DF_U64"\n", lhe, hdl->ch_lhe);

	daos_iov_set(&key, in->cei_op.ci_hdl, sizeof(uuid_t));
	daos_iov_set(&value, hdl, sizeof(*hdl));
	rc = rdb_tx_update(tx, &cont->c_svc->cs_hdls, &key, &value);
	if (rc != 0)
		D_GOTO(out_hdl, rc);

	rc = ec_update_and_find_lowest(tx, cont, EC_LHE, &lhe /* dec */,
				       &hdl->ch_lhe /* inc */,
				       NULL /* emptyp */, &prop.ep_glhe);
	if (rc != 0)
		D_GOTO(out_hdl, rc);

	/* If we are releasing held epochs, then update GHCE. */
	if (hdl->ch_lhe > lhe) {
		rc = update_ghce(tx, cont, &prop);
		if (rc != 0)
			D_GOTO(out_hdl, rc);
	}

	if (check_epoch_invariant(cont, &prop, hdl) != 0)
		D_GOTO(out_hdl, rc = -DER_IO);

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
	struct epoch_prop		prop;
	daos_epoch_t			lre = hdl->ch_lre;
	daos_epoch_t			glre;
	daos_iov_t			key;
	daos_iov_t			value;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: epoch="DF_U64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		in->cei_epoch);

	if (in->cei_epoch >= DAOS_EPOCH_MAX)
		D_GOTO(out, rc = -DER_OVERFLOW);

	rc = read_epoch_prop(tx, cont, &prop);
	if (rc != 0)
		D_GOTO(out, rc);
	glre = prop.ep_glre;

	if (check_epoch_invariant(cont, &prop, hdl) != 0)
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
		D_GOTO(out_hdl, rc = 0);

	D_DEBUG(DF_DSMS, "lre="DF_U64" lre'="DF_U64"\n", lre, hdl->ch_lre);

	daos_iov_set(&key, in->cei_op.ci_hdl, sizeof(uuid_t));
	daos_iov_set(&value, hdl, sizeof(*hdl));
	rc = rdb_tx_update(tx, &cont->c_svc->cs_hdls, &key, &value);
	if (rc != 0)
		D_GOTO(out_hdl, rc);

	rc = ec_update_and_find_lowest(tx, cont, EC_LRE, &lre /* dec */,
				       &hdl->ch_lre /* inc */,
				       NULL /* emptyp */, &prop.ep_glre);
	if (rc != 0)
		D_GOTO(out_hdl, rc);

	if (check_epoch_invariant(cont, &prop, hdl) != 0)
		D_GOTO(out_hdl, rc = -DER_IO);

	/** XXX
	 * Once we have an aggregation daemon,
	 * we need to mask the return value, we need not
	 * fail if aggregation bcast fails
	 */
	rc = trigger_aggregation(tx, glre, prop.ep_glre, prop.ep_ghce,
				 cont, rpc->cr_ctx);
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
ds_cont_epoch_discard(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		      struct cont *cont, struct container_hdl *hdl,
		      crt_rpc_t *rpc)
{
	struct cont_epoch_op_in	       *in = crt_req_get(rpc);
	struct epoch_prop		prop;
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

	rc = read_epoch_prop(tx, cont, &prop);
	if (rc != 0)
		D_GOTO(out, rc);

	if (check_epoch_invariant(cont, &prop, hdl) != 0)
		D_GOTO(out, rc = -DER_IO);

	rc = cont_epoch_discard_bcast(rpc->cr_ctx, cont, in->cei_op.ci_hdl,
				      in->cei_epoch);

out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		rc);
	return rc;
}

int
ds_cont_epoch_commit(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		     struct cont *cont, struct container_hdl *hdl,
		     crt_rpc_t *rpc, bool snapshot)
{
	struct cont_epoch_op_in	       *in = crt_req_get(rpc);
	struct epoch_prop		prop;
	daos_epoch_t			hce = hdl->ch_hce;
	daos_epoch_t			lhe = hdl->ch_lhe;
	daos_epoch_t			lre = hdl->ch_lre;
	daos_epoch_t			glre = 0;
	daos_iov_t			key;
	daos_iov_t			value;
	bool				slip_flag;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: epoch="DF_U64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		in->cei_epoch);

	/*
	 * TODO - slip_flag should be set to true, but aggregation is not
	 * working properly, so disable for now.
	 */
	slip_flag = false;

	/* Verify the container handle capabilities. */
	if (!(hdl->ch_capas & DAOS_COO_RW))
		D_GOTO(out, rc = -DER_NO_PERM);

	if (in->cei_epoch >= DAOS_EPOCH_MAX)
		D_GOTO(out, rc = -DER_OVERFLOW);

	rc = read_epoch_prop(tx, cont, &prop);
	if (rc != 0)
		D_GOTO(out, rc);

	if (slip_flag)
		glre = prop.ep_glre;

	if (check_epoch_invariant(cont, &prop, hdl) != 0)
		D_GOTO(out, rc = -DER_IO);

	if (in->cei_epoch <= hdl->ch_hce)
		/* Committing an already committed epoch is okay and a no-op. */
		D_GOTO(out_hdl, rc = 0);
	else if (in->cei_epoch < hdl->ch_lhe)
		/* Committing an unheld epoch is not allowed. */
		D_GOTO(out, rc = -DER_EP_RO);

	hdl->ch_hce = in->cei_epoch;
	hdl->ch_lhe = hdl->ch_hce + 1;
	if (!(hdl->ch_capas & DAOS_COO_NOSLIP))
		hdl->ch_lre = hdl->ch_hce;

	daos_iov_set(&key, in->cei_op.ci_hdl, sizeof(uuid_t));
	daos_iov_set(&value, hdl, sizeof(*hdl));
	rc = rdb_tx_update(tx, &cont->c_svc->cs_hdls, &key, &value);
	if (rc != 0)
		D_GOTO(out_hdl, rc);

	rc = ec_update_and_find_lowest(tx, cont, EC_LHE, &lhe /* dec */,
				       &hdl->ch_lhe /* inc */,
				       NULL /* emptyp */, &prop.ep_glhe);
	if (rc != 0)
		D_GOTO(out_hdl, rc);

	if (!(hdl->ch_capas & DAOS_COO_NOSLIP)) {
		rc = ec_update_and_find_lowest(tx, cont, EC_LRE, &lre /* dec */,
					       &hdl->ch_lre /* inc */,
					       NULL /* emptyp */,
					       &prop.ep_glre);
		if (rc != 0)
			D_GOTO(out_hdl, rc);
	}

	if (hdl->ch_hce > prop.ep_ghpce) {
		prop.ep_ghpce = hdl->ch_hce;
		daos_iov_set(&value, &prop.ep_ghpce, sizeof(prop.ep_ghpce));
		rc = rdb_tx_update(tx, &cont->c_prop, &ds_cont_prop_ghpce,
				   &value);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to update ghpce: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			D_GOTO(out_hdl, rc);
		}
	}

	rc = update_ghce(tx, cont, &prop);
	if (rc != 0)
		D_GOTO(out_hdl, rc);

	if (check_epoch_invariant(cont, &prop, hdl) != 0)
		D_GOTO(out_hdl, rc = -DER_IO);

	if (snapshot) {
		char		zero = 0;

		daos_iov_set(&key, &in->cei_epoch, sizeof(in->cei_epoch));
		daos_iov_set(&value, &zero, sizeof(zero));
		rc = rdb_tx_update(tx, &cont->c_snaps, &key, &value);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to create snapshot: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			goto out;
		}
	}

	if (slip_flag) {
		rc = trigger_aggregation(tx, glre, prop.ep_glre, prop.ep_ghce,
					 cont, rpc->cr_ctx);
		if (rc != 0) {
			D_ERROR("Trigger aggregation from commit failed %d\n",
				rc);
			D_GOTO(out_hdl, rc);
		}
	}

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

struct snap_destroy_iter_args {
	daos_epoch_t		sda_find;
	daos_epoch_t		sda_prev;
	daos_epoch_range_t	sda_range;
};

static int
snap_destroy_iter_cb(daos_handle_t ih, daos_iov_t *key, daos_iov_t *val,
		     void *arg)
{
	struct snap_destroy_iter_args *i_args = arg;
	daos_epoch_t *curr = key->iov_buf;

	D_ASSERTF(key->iov_len == sizeof(daos_epoch_t),
		  DF_U64"\n", key->iov_len);
	if (*curr == i_args->sda_find)
		i_args->sda_range.epr_lo = i_args->sda_prev;
	else if (i_args->sda_find == i_args->sda_prev) {
		if (i_args->sda_range.epr_hi > *curr)
			i_args->sda_range.epr_hi = *curr;
		return 1;
	}
	i_args->sda_prev = *curr;
	return 0;
}

int
ds_cont_snap_destroy(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		    struct cont *cont, struct container_hdl *hdl,
		    crt_rpc_t *rpc)
{
	struct cont_epoch_op_in		*in = crt_req_get(rpc);
	struct snap_destroy_iter_args	 iter_args;
	daos_iov_t			 key;
	struct epoch_prop		 prop;
	bool				 slip_flag;
	int				 rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: epoch="DF_U64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid),
		rpc, in->cei_epoch);

	daos_iov_set(&key, &in->cei_epoch, sizeof(daos_epoch_t));
	rc = rdb_tx_delete(tx, &cont->c_snaps, &key);
	if (rc != 0) {
		rc = 0;
		goto out;
	}
	slip_flag = auto_slip_enabled();
	rc = read_epoch_prop(tx, cont, &prop);
	if (rc != 0)
		goto out;
	if (check_global_epoch_invariant(cont, &prop) != 0)
		return -DER_IO;


	/* No aggregation necessary */
	if (!slip_flag || prop.ep_glre <= in->cei_epoch)
		goto out;

	iter_args.sda_find = in->cei_epoch;
	iter_args.sda_prev = 0UL;
	iter_args.sda_range.epr_lo = 0UL;
	iter_args.sda_range.epr_hi = MIN(prop.ep_glre, prop.ep_ghce);
	rc = rdb_tx_iterate(tx, &cont->c_snaps, false /* !backward */,
			    snap_destroy_iter_cb, &iter_args);
	if (rc != 0)
		goto out;

	D_DEBUG(DF_DSMS, "deleted="DF_U64" prev="DF_U64" next="DF_U64"\n",
		in->cei_epoch, iter_args.sda_range.epr_lo,
		iter_args.sda_range.epr_hi);
	rc = epoch_aggregate_bcast(rpc->cr_ctx, cont, &iter_args.sda_range, 1);
out:
	return rc;
}

static int
bulk_cb(const struct crt_bulk_cb_info *cb_info)
{
	ABT_eventual *eventual = cb_info->bci_arg;

	ABT_eventual_set(*eventual, (void *)&cb_info->bci_rc,
			 sizeof(cb_info->bci_rc));
	return 0;
}

int
ds_cont_snap_list(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		  struct cont *cont, struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_snap_list_in	*in		= crt_req_get(rpc);
	struct cont_snap_list_out	*out		= crt_reply_get(rpc);
	daos_size_t			 bulk_size;
	daos_epoch_t			*snapshots;
	int				 snap_count;
	int				 xfer_size;
	int				 rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->sli_op.ci_uuid),
		rpc, DP_UUID(in->sli_op.ci_hdl));
	/*
	 * If remote bulk handle does not exist, only aggregate size is sent.
	 */
	if (in->sli_bulk) {
		rc = crt_bulk_get_len(in->sli_bulk, &bulk_size);
		if (rc != 0)
			goto out;
		D_DEBUG(DF_DSMS, DF_CONT": bulk_size=%lu\n",
			DP_CONT(pool_hdl->sph_pool->sp_uuid,
				in->sli_op.ci_uuid), bulk_size);
		snap_count = (int)(bulk_size / sizeof(daos_epoch_t));
	} else {
		bulk_size = 0;
		snap_count = 0;
	}
	rc = read_snap_list(tx, cont, &snapshots, &snap_count);
	if (rc != 0)
		goto out;
	out->slo_count = snap_count;
	xfer_size = snap_count * sizeof(daos_epoch_t);
	xfer_size = MIN(xfer_size, bulk_size);

	if (xfer_size > 0) {
		ABT_eventual	 eventual;
		int		*status;
		daos_iov_t	 iov = {
			.iov_buf	= snapshots,
			.iov_len	= xfer_size,
			.iov_buf_len	= xfer_size
		};
		daos_sg_list_t	 sgl = {
			.sg_nr_out = 1,
			.sg_nr	   = 1,
			.sg_iovs   = &iov
		};
		struct crt_bulk_desc bulk_desc = {
			.bd_rpc		= rpc,
			.bd_bulk_op	= CRT_BULK_PUT,
			.bd_local_off	= 0,
			.bd_remote_hdl	= in->sli_bulk,
			.bd_remote_off	= 0,
			.bd_len		= xfer_size
		};

		rc = ABT_eventual_create(sizeof(*status), &eventual);
		if (rc != ABT_SUCCESS) {
			rc = dss_abterr2der(rc);
			goto out_mem;
		}

		rc = crt_bulk_create(rpc->cr_ctx, daos2crt_sg(&sgl),
				     CRT_BULK_RW, &bulk_desc.bd_local_hdl);
		if (rc != 0)
			goto out_eventual;
		rc = crt_bulk_transfer(&bulk_desc, bulk_cb, &eventual, NULL);
		if (rc != 0)
			goto out_bulk;
		rc = ABT_eventual_wait(eventual, (void **)&status);
		if (rc != ABT_SUCCESS)
			rc = dss_abterr2der(rc);
		else
			rc = *status;

out_bulk:
		crt_bulk_free(bulk_desc.bd_local_hdl);
out_eventual:
		ABT_eventual_free(&eventual);
	}

out_mem:
	D_FREE(snapshots);
out:
	return rc;
}
