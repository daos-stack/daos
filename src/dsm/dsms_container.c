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
 * dsms: Container Operations
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related container metadata.
 */

#include <daos_srv/daos_m_srv.h>
#include <uuid/uuid.h>
#include <daos/transport.h>
#include "dsm_rpc.h"
#include "dsms_internal.h"
#include "dsms_layout.h"

/*
 * Container service
 *
 * References the mpool descriptor. Identified by a number unique within the
 * pool.
 *
 * TODO: Store and look up this in a hash table.
 */
struct cont_svc {
	uuid_t			cs_pool;
	uint64_t		cs_id;
	struct mpool	       *cs_mpool;
	pthread_rwlock_t	cs_rwlock;
	pthread_mutex_t		cs_lock;
	int			cs_ref;
	daos_handle_t		cs_containers;	/* of container index KVS */
};

static int
cont_svc_init(const uuid_t pool_uuid, int id, struct cont_svc *svc)
{
	int rc;

	uuid_copy(svc->cs_pool, pool_uuid);
	svc->cs_id = id;
	svc->cs_ref = 1;

	rc = dsms_mpool_lookup(pool_uuid, &svc->cs_mpool);
	if (rc != 0)
		D_GOTO(err, rc);

	rc = pthread_rwlock_init(&svc->cs_rwlock, NULL /* attr */);
	if (rc != 0) {
		D_ERROR("failed to initialize cs_rwlock: %d\n", rc);
		D_GOTO(err_mp, rc = -DER_NOMEM);
	}

	rc = pthread_mutex_init(&svc->cs_lock, NULL /* attr */);
	if (rc != 0) {
		D_ERROR("failed to initialize cs_lock: %d\n", rc);
		D_GOTO(err_rwlock, rc = -DER_NOMEM);
	}

	rc = dsms_kvs_nv_open_kvs(svc->cs_mpool->mp_root, CONTAINERS,
				  svc->cs_mpool->mp_pmem, &svc->cs_containers);
	if (rc != 0) {
		D_ERROR("failed to open containers kvs: %d\n", rc);
		D_GOTO(err_lock, rc);
	}

	return 0;

err_lock:
	pthread_mutex_destroy(&svc->cs_lock);
err_rwlock:
	pthread_rwlock_destroy(&svc->cs_rwlock);
err_mp:
	dsms_mpool_put(svc->cs_mpool);
err:
	return rc;
}

static int
cont_svc_lookup(const uuid_t pool_uuid, int id, struct cont_svc **svc)
{
	struct cont_svc	       *p;
	int			rc;

	/* TODO: Hash table. */

	D_ALLOC_PTR(p);
	if (p == NULL) {
		D_ERROR("failed to allocate container service descriptor\n");
		return -DER_NOMEM;
	}

	rc = cont_svc_init(pool_uuid, id, p);
	if (rc != 0) {
		D_FREE_PTR(p);
		return rc;
	}

	*svc = p;
	return 0;
}

static void
cont_svc_put(struct cont_svc *svc)
{
	dbtree_close(svc->cs_containers);
	pthread_mutex_destroy(&svc->cs_lock);
	pthread_rwlock_destroy(&svc->cs_rwlock);
	dsms_mpool_put(svc->cs_mpool);
	D_FREE_PTR(svc);
}

int
dsms_hdlr_cont_create(dtp_rpc_t *rpc)
{
	struct cont_create_in  *in = dtp_req_get(rpc);
	struct cont_create_out *out = dtp_reply_get(rpc);
	struct cont_svc	       *svc;
	volatile daos_handle_t	ch = DAOS_HDL_INVAL;
	int			rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);
	D_ASSERT(in != NULL);
	D_ASSERT(out != NULL);

	D_DEBUG(DF_DSMS, "enter: pool="DF_UUID" pool_hdl="DF_UUID" cont="DF_UUID
		"\n", DP_UUID(in->cci_pool), DP_UUID(in->cci_pool_hdl),
		DP_UUID(in->cci_cont));

	/* TODO: Pool handle verification. */

	/*
	 * TODO: How to map to the correct container service among those
	 * running of this storage node? (Currently, there is only one, with ID
	 * 0, colocated with the pool service.)
	 */
	rc = cont_svc_lookup(in->cci_pool, 0 /* id */, &svc);
	if (rc != 0)
		D_GOTO(out, rc);

	pthread_rwlock_wrlock(&svc->cs_rwlock);

	TX_BEGIN(svc->cs_mpool->mp_pmem) {
		daos_handle_t	h;
		uint64_t	ghce = 0;

		/* Create the container KVS under the container index KVS. */
		rc = dsms_kvs_uv_create_kvs(svc->cs_containers, in->cci_cont,
					    KVS_NV, 0 /* feats */,
					    16 /* order */,
					    svc->cs_mpool->mp_pmem, &h);
		if (rc != 0) {
			D_ERROR("failed to create container kvs: %d\n", rc);
			pmemobj_tx_abort(rc);
		}

		ch = h;

		rc = dsms_kvs_nv_update(ch, CONT_GHCE, &ghce, sizeof(ghce));
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_create_kvs(ch, CONT_HCES, KVS_EC,
					    0 /* feats */, 16 /* order */,
					    svc->cs_mpool->mp_pmem,
					    NULL /* kvsh_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_create_kvs(ch, CONT_LRES, KVS_EC,
					    0 /* feats */, 16 /* order */,
					    svc->cs_mpool->mp_pmem,
					    NULL /* kvsh_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_create_kvs(ch, CONT_LHES, KVS_EC,
					    0 /* feats */, 16 /* order */,
					    svc->cs_mpool->mp_pmem,
					    NULL /* kvsh_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_create_kvs(ch, CONT_SNAPSHOTS, KVS_EC,
					    0 /* feats */, 16 /* order */,
					    svc->cs_mpool->mp_pmem,
					    NULL /* kvsh_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_create_kvs(ch, CONT_HANDLES, KVS_UV,
					    0 /* feats */, 16 /* order */,
					    svc->cs_mpool->mp_pmem,
					    NULL /* kvsh_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_FINALLY {
		if (!daos_handle_is_inval(ch))
			dbtree_close(ch);
	} TX_END

	pthread_rwlock_unlock(&svc->cs_rwlock);
	cont_svc_put(svc);
out:
	D_DEBUG(DF_DSMS, "leave: rc=%d\n", rc);
	out->cco_ret = rc;
	return dtp_reply_send(rpc);
}

int
dsms_hdlr_cont_destroy(dtp_rpc_t *rpc)
{
	struct cont_destroy_in	       *in = dtp_req_get(rpc);
	struct cont_destroy_out	       *out = dtp_reply_get(rpc);
	struct cont_svc		       *svc;
	volatile daos_handle_t		ch;
	daos_handle_t			h;
	int				rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);
	D_ASSERT(in != NULL);
	D_ASSERT(out != NULL);

	D_DEBUG(DF_DSMS, "enter: pool="DF_UUID" pool_hdl="DF_UUID" cont="DF_UUID
		" force=%u\n", DP_UUID(in->cdi_pool), DP_UUID(in->cdi_pool_hdl),
		DP_UUID(in->cdi_cont), in->cdi_force);

	/* TODO: Pool handle verification. */

	rc = cont_svc_lookup(in->cdi_pool, 0 /* id */, &svc);
	if (rc != 0)
		D_GOTO(out, rc);

	pthread_rwlock_wrlock(&svc->cs_rwlock);

	rc = dsms_kvs_uv_open_kvs(svc->cs_containers, in->cdi_cont,
				  svc->cs_mpool->mp_pmem, &h);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			rc = 0;
		D_GOTO(out_rwlock, rc);
	}

	ch = h;

	/* TODO: Send DSM_TGT_CONT_DESTROY to targets. */

	TX_BEGIN(svc->cs_mpool->mp_pmem) {
		rc = dsms_kvs_nv_destroy_kvs(ch, CONT_HANDLES,
					     svc->cs_mpool->mp_pmem);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_destroy_kvs(ch, CONT_SNAPSHOTS,
					     svc->cs_mpool->mp_pmem);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_destroy_kvs(ch, CONT_LHES,
					     svc->cs_mpool->mp_pmem);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_destroy_kvs(ch, CONT_LRES,
					     svc->cs_mpool->mp_pmem);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dsms_kvs_nv_destroy_kvs(ch, CONT_HCES,
					     svc->cs_mpool->mp_pmem);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_destroy(ch);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		ch = DAOS_HDL_INVAL;

		rc = dsms_kvs_uv_delete(svc->cs_containers, in->cdi_cont);
		if (rc != 0) {
			D_ERROR("failed to delete container kvs: %d\n", rc);
			pmemobj_tx_abort(rc);
		}
	} TX_ONABORT {
		if (!daos_handle_is_inval(ch))
			dbtree_close(ch);

		rc = umem_tx_errno(rc);
	} TX_END

out_rwlock:
	pthread_rwlock_unlock(&svc->cs_rwlock);
	cont_svc_put(svc);
out:
	D_DEBUG(DF_DSMS, "leave: rc=%d\n", rc);
	out->cdo_ret = rc;
	return dtp_reply_send(rpc);
}

static int
ec_increment(daos_handle_t kvsh, uint64_t epoch)
{
	uint64_t	c = 0;
	uint64_t	c_new;
	int		rc;

	rc = dsms_kvs_ec_lookup(kvsh, epoch, &c);
	if (rc != 0 && rc != -DER_NONEXIST)
		return rc;

	c_new = c + 1;
	if (c_new < c)
		return -DER_OVERFLOW;

	return dsms_kvs_ec_update(kvsh, epoch, &c_new);
}

static int
ec_decrement(daos_handle_t kvsh, uint64_t epoch)
{
	uint64_t	c = 0;
	uint64_t	c_new;
	int		rc;

	rc = dsms_kvs_ec_lookup(kvsh, epoch, &c);
	if (rc != 0 && rc != -DER_NONEXIST)
		return rc;

	c_new = c - 1;
	if (c_new > c)
		return -DER_OVERFLOW;

	if (c_new == 0)
		rc = dsms_kvs_ec_delete(kvsh, epoch);
	else
		rc = dsms_kvs_ec_update(kvsh, epoch, &c_new);

	return rc;
}

/* Container descriptor */
struct cont {
	uuid_t			c_uuid;
	struct cont_svc	       *c_svc;
	daos_handle_t		c_cont;		/* container KVS */
	daos_handle_t		c_hces;		/* HCE KVS */
	daos_handle_t		c_lres;		/* LRE KVS */
	daos_handle_t		c_lhes;		/* LHE KVS */
	daos_handle_t		c_handles;	/* container handle KVS */
};

static int
cont_lookup(const struct cont_svc *svc, const uuid_t uuid, struct cont **cont)
{
	struct cont    *p;
	int		rc;

	D_ALLOC_PTR(p);
	if (p == NULL) {
		D_ERROR("failed to allocate container descriptor\n");
		D_GOTO(err, rc = -DER_NOMEM);
	}

	uuid_copy(p->c_uuid, uuid);
	p->c_svc = (struct cont_svc *)svc;

	rc = dsms_kvs_uv_open_kvs(svc->cs_containers, uuid,
				  svc->cs_mpool->mp_pmem, &p->c_cont);
	if (rc != 0)
		D_GOTO(err_p, rc);

	rc = dsms_kvs_nv_open_kvs(p->c_cont, CONT_HCES, svc->cs_mpool->mp_pmem,
				  &p->c_hces);
	if (rc != 0)
		D_GOTO(err_cont, rc);

	rc = dsms_kvs_nv_open_kvs(p->c_cont, CONT_LRES, svc->cs_mpool->mp_pmem,
				  &p->c_lres);
	if (rc != 0)
		D_GOTO(err_hces, rc);

	rc = dsms_kvs_nv_open_kvs(p->c_cont, CONT_LHES, svc->cs_mpool->mp_pmem,
				  &p->c_lhes);
	if (rc != 0)
		D_GOTO(err_lres, rc);

	rc = dsms_kvs_nv_open_kvs(p->c_cont, CONT_HANDLES,
				  svc->cs_mpool->mp_pmem, &p->c_handles);
	if (rc != 0)
		D_GOTO(err_lhes, rc);

	*cont = p;
	return 0;

err_lhes:
	dbtree_close(p->c_lhes);
err_lres:
	dbtree_close(p->c_lres);
err_hces:
	dbtree_close(p->c_hces);
err_cont:
	dbtree_close(p->c_cont);
err_p:
	D_FREE_PTR(p);
err:
	return rc;
}

static void
cont_put(struct cont *cont)
{
	dbtree_close(cont->c_handles);
	dbtree_close(cont->c_lhes);
	dbtree_close(cont->c_lres);
	dbtree_close(cont->c_hces);
	dbtree_close(cont->c_cont);
	D_FREE_PTR(cont);
}

int
dsms_hdlr_cont_open(dtp_rpc_t *rpc)
{
	struct cont_open_in    *in = dtp_req_get(rpc);
	struct cont_open_out   *out = dtp_reply_get(rpc);
	struct cont_svc	       *svc;
	struct cont	       *cont;
	struct container_hdl	chdl;
	daos_epoch_t		ghce;
	daos_epoch_t		glre;
	daos_epoch_t		ghpce = DAOS_EPOCH_MAX;
	int			rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);
	D_ASSERT(in != NULL);
	D_ASSERT(out != NULL);

	D_DEBUG(DF_DSMS, "enter: pool="DF_UUID" pool_hdl="DF_UUID" cont="DF_UUID
		" cont_hdl="DF_UUID"\n", DP_UUID(in->coi_pool),
		DP_UUID(in->coi_pool_hdl), DP_UUID(in->coi_cont),
		DP_UUID(in->coi_cont_hdl));

	rc = cont_svc_lookup(in->coi_pool, 0 /* id */, &svc);
	if (rc != 0)
		D_GOTO(out, rc);

	pthread_rwlock_wrlock(&svc->cs_rwlock);

	rc = cont_lookup(svc, in->coi_cont, &cont);
	if (rc != 0)
		D_GOTO(out_rwlock, rc);

	/* See if this container handle already exists. */
	rc = dsms_kvs_uv_lookup(cont->c_handles, in->coi_cont_hdl, &chdl,
				sizeof(chdl));
	if (rc != -DER_NONEXIST) {
		if (rc == 0 && chdl.ch_capas != in->coi_capas) {
			D_ERROR(DF_CONT"found conflicting container handle\n",
				DP_CONT(cont->c_svc->cs_pool, cont->c_uuid));
			rc = -DER_EXIST;
		}
		D_GOTO(out_cont, rc);
	}

	/* Get GHPCE. */
	rc = dsms_kvs_ec_fetch(cont->c_hces, BTR_PROBE_LAST,
			       NULL /* epoch_in */, &ghpce, NULL /* count */);
	if (rc != 0 && rc != -DER_NONEXIST)
		D_GOTO(out_cont, rc);

	/* Get GHCE. */
	rc = dsms_kvs_nv_lookup(cont->c_cont, CONT_GHCE, &ghce, sizeof(ghce));
	if (rc != 0)
		D_GOTO(out_cont, rc);

	/*
	 * Check the coo_epoch_state assignements below if any of these rules
	 * changes.
	 */
	chdl.ch_hce = ghpce == DAOS_EPOCH_MAX ? ghce : ghpce;
	chdl.ch_lre = chdl.ch_hce;
	chdl.ch_lhe = DAOS_EPOCH_MAX;
	chdl.ch_capas = in->coi_capas;

	TX_BEGIN(svc->cs_mpool->mp_pmem) {
		rc = dsms_kvs_uv_update(cont->c_handles, in->coi_cont_hdl,
					&chdl, sizeof(chdl));
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = ec_increment(cont->c_hces, chdl.ch_hce);
		if (rc != 0) {
			D_ERROR(DF_CONT"failed to update hce kvs: %d\n",
				DP_CONT(cont->c_svc->cs_pool, cont->c_uuid),
				rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_increment(cont->c_lres, chdl.ch_lre);
		if (rc != 0) {
			D_ERROR(DF_CONT"failed to update lre kvs: %d\n",
				DP_CONT(cont->c_svc->cs_pool, cont->c_uuid),
				rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_increment(cont->c_lhes, chdl.ch_lhe);
		if (rc != 0) {
			D_ERROR(DF_CONT"failed to update lhe kvs: %d\n",
				DP_CONT(cont->c_svc->cs_pool, cont->c_uuid),
				rc);
			pmemobj_tx_abort(rc);
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_END

	if (rc != 0)
		D_GOTO(out_cont, rc);

	/* Calculate GLRE. */
	rc = dsms_kvs_ec_fetch(cont->c_lres, BTR_PROBE_FIRST,
			       NULL /* epoch_in */, &glre, NULL /* count */);
	if (rc != 0) {
		/* At least there shall be this handle's LRE. */
		D_ASSERT(rc != -DER_NONEXIST);
		D_GOTO(out_cont, rc);
	}

	out->coo_epoch_state.es_hce = chdl.ch_hce;
	out->coo_epoch_state.es_lre = chdl.ch_lre;
	out->coo_epoch_state.es_lhe = chdl.ch_lhe;
	out->coo_epoch_state.es_glb_hce = ghce;
	out->coo_epoch_state.es_glb_lre = glre;
	out->coo_epoch_state.es_glb_hpce = chdl.ch_hce;

out_cont:
	cont_put(cont);
out_rwlock:
	pthread_rwlock_unlock(&svc->cs_rwlock);
	cont_svc_put(svc);
out:
	D_DEBUG(DF_DSMS, "leave: rc=%d\n", rc);
	out->coo_ret = rc;
	return dtp_reply_send(rpc);
}

int
dsms_hdlr_cont_close(dtp_rpc_t *rpc)
{
	struct cont_close_in   *in = dtp_req_get(rpc);
	struct cont_close_out  *out = dtp_reply_get(rpc);
	struct cont_svc	       *svc;
	struct cont	       *cont;
	struct container_hdl	chdl;
	int			rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);
	D_ASSERT(in != NULL);
	D_ASSERT(out != NULL);

	D_DEBUG(DF_DSMS, "enter: pool="DF_UUID" cont="DF_UUID" cont_hdl="DF_UUID
		"\n", DP_UUID(in->cci_pool), DP_UUID(in->cci_cont),
		DP_UUID(in->cci_cont_hdl));

	rc = cont_svc_lookup(in->cci_pool, 0 /* id */, &svc);
	if (rc != 0)
		D_GOTO(out, rc);

	pthread_rwlock_wrlock(&svc->cs_rwlock);

	rc = cont_lookup(svc, in->cci_cont, &cont);
	if (rc != 0)
		D_GOTO(out_rwlock, rc);

	/* See if this container handle is already closed. */
	rc = dsms_kvs_uv_lookup(cont->c_handles, in->cci_cont_hdl, &chdl,
				sizeof(chdl));
	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			D_DEBUG(DF_DSMS, "already closed: "DF_UUID"\n",
				DP_UUID(in->cci_cont_hdl));
			rc = 0;
		}
		D_GOTO(out_cont, rc);
	}

	TX_BEGIN(svc->cs_mpool->mp_pmem) {
		rc = dsms_kvs_uv_delete(cont->c_handles, in->cci_cont_hdl);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = ec_decrement(cont->c_hces, chdl.ch_hce);
		if (rc != 0) {
			D_ERROR(DF_CONT"failed to update hce kvs: %d\n",
				DP_CONT(cont->c_svc->cs_pool, cont->c_uuid),
				rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_decrement(cont->c_lres, chdl.ch_lre);
		if (rc != 0) {
			D_ERROR(DF_CONT"failed to update lre kvs: %d\n",
				DP_CONT(cont->c_svc->cs_pool, cont->c_uuid),
				rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_decrement(cont->c_lhes, chdl.ch_lhe);
		if (rc != 0) {
			D_ERROR(DF_CONT"failed to update lhe kvs: %d\n",
				DP_CONT(cont->c_svc->cs_pool, cont->c_uuid),
				rc);
			pmemobj_tx_abort(rc);
		}

		/* TODO: Update GHCE. */
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_END

	if (rc != 0)
		D_GOTO(out_cont, rc);

out_cont:
	cont_put(cont);
out_rwlock:
	pthread_rwlock_unlock(&svc->cs_rwlock);
	cont_svc_put(svc);
out:
	D_DEBUG(DF_DSMS, "leave: rc=%d\n", rc);
	out->cco_ret = rc;
	return dtp_reply_send(rpc);
}

typedef int (*cont_op_hdlr_t)(struct cont_svc *svc, struct cont *cont,
			      struct container_hdl *hdl, void *input,
			      void *output);

/*
 * TODO: Support more than one container handles. E.g., update GHCE if the
 * client is releasing the previous hold.
 */

static void
epoch_state_set(struct container_hdl *hdl, daos_epoch_state_t *state)
{
	state->es_hce = hdl->ch_hce;
	state->es_lre = hdl->ch_lre;
	state->es_lhe = hdl->ch_lhe;
	state->es_glb_hce = hdl->ch_hce;
	state->es_glb_lre = hdl->ch_lre;
	state->es_glb_hpce = hdl->ch_hce;
}

static int
cont_epoch_query(struct cont_svc *svc, struct cont *cont,
		 struct container_hdl *hdl, void *input, void *output)
{
	struct epoch_op_out    *out = output;

	epoch_state_set(hdl, &out->eoo_epoch_state);
	return 0;
}

static int
cont_epoch_hold(struct cont_svc *svc, struct cont *cont,
		struct container_hdl *hdl, void *input, void *output)
{
	struct epoch_op_in     *in = input;
	struct epoch_op_out    *out = output;
	daos_epoch_t		lhe = hdl->ch_lhe;
	daos_epoch_t		ghpce = hdl->ch_hce;
	int			rc = 0;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);

	if (in->eoi_epoch == hdl->ch_lhe)
		return 0;

	if (in->eoi_epoch <= ghpce)
		hdl->ch_lhe = ghpce + 1;
	else
		hdl->ch_lhe = in->eoi_epoch;

	D_DEBUG(DF_DSMS, "lhe="DF_U64" lhe'="DF_U64"\n", lhe, hdl->ch_lhe);

	TX_BEGIN(svc->cs_mpool->mp_pmem) {
		rc = dsms_kvs_uv_update(cont->c_handles,
					in->eoi_cont_op_in.cpi_cont_hdl, hdl,
					sizeof(*hdl));
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = ec_decrement(cont->c_lhes, lhe);
		if (rc != 0) {
			D_ERROR(DF_CONT"failed to remove original lhe: %d\n",
				DP_CONT(cont->c_svc->cs_pool, cont->c_uuid),
				rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_increment(cont->c_lhes, hdl->ch_lhe);
		if (rc != 0) {
			D_ERROR(DF_CONT"failed to add new lhe: %d\n",
				DP_CONT(cont->c_svc->cs_pool, cont->c_uuid),
				rc);
			pmemobj_tx_abort(rc);
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_END

	if (rc != 0)
		hdl->ch_lhe = lhe;

	epoch_state_set(hdl, &out->eoo_epoch_state);
	return rc;
}

static int
cont_epoch_commit(struct cont_svc *svc, struct cont *cont,
		  struct container_hdl *hdl, void *input, void *output)
{
	struct epoch_op_in     *in = input;
	struct epoch_op_out    *out = output;
	daos_epoch_t		hce = hdl->ch_hce;
	daos_epoch_t		lhe = hdl->ch_lhe;
	int			rc = 0;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);

	if (in->eoi_epoch <= hdl->ch_hce)
		return 0;

	if (in->eoi_epoch < hdl->ch_lhe)
		return -DER_EP_RO;

	hdl->ch_hce = in->eoi_epoch;
	hdl->ch_lhe = hdl->ch_hce + 1;

	D_DEBUG(DF_DSMS, "hce="DF_U64" hce'="DF_U64"\n", hce, hdl->ch_hce);

	TX_BEGIN(svc->cs_mpool->mp_pmem) {
		rc = dsms_kvs_uv_update(cont->c_handles,
					in->eoi_cont_op_in.cpi_cont_hdl, hdl,
					sizeof(*hdl));
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = ec_decrement(cont->c_hces, hce);
		if (rc != 0) {
			D_ERROR(DF_CONT"failed to remove original hce: %d\n",
				DP_CONT(cont->c_svc->cs_pool, cont->c_uuid),
				rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_increment(cont->c_hces, hdl->ch_hce);
		if (rc != 0) {
			D_ERROR(DF_CONT"failed to add new hce: %d\n",
				DP_CONT(cont->c_svc->cs_pool, cont->c_uuid),
				rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_decrement(cont->c_lhes, lhe);
		if (rc != 0) {
			D_ERROR(DF_CONT"failed to remove original lhe: %d\n",
				DP_CONT(cont->c_svc->cs_pool, cont->c_uuid),
				rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_increment(cont->c_lhes, hdl->ch_lhe);
		if (rc != 0) {
			D_ERROR(DF_CONT"failed to add new lhe: %d\n",
				DP_CONT(cont->c_svc->cs_pool, cont->c_uuid),
				rc);
			pmemobj_tx_abort(rc);
		}

		rc = dsms_kvs_nv_update(cont->c_cont, CONT_GHCE, &hdl->ch_hce,
					sizeof(hdl->ch_hce));
		if (rc != 0) {
			D_ERROR(DF_CONT"failed to update ghce: %d\n",
				DP_CONT(cont->c_svc->cs_pool, cont->c_uuid),
				rc);
			pmemobj_tx_abort(rc);
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_END

	if (rc != 0) {
		hdl->ch_lhe = lhe;
		hdl->ch_hce = hce;
	}

	epoch_state_set(hdl, &out->eoo_epoch_state);
	return rc;
}

int
dsms_hdlr_cont_op(dtp_rpc_t *rpc)
{
	struct cont_op_in      *in = dtp_req_get(rpc);
	struct cont_op_out     *out = dtp_reply_get(rpc);
	struct cont_svc	       *svc;
	struct cont	       *cont;
	struct container_hdl	hdl;
	cont_op_hdlr_t		hdlr;
	int			rc;

	D_DEBUG(DF_DSMS, "pool="DF_UUID" cont="DF_UUID" cont_hdl="DF_UUID
		" opc=%u\n", DP_UUID(in->cpi_pool), DP_UUID(in->cpi_cont),
		DP_UUID(in->cpi_cont_hdl), opc_get(rpc->dr_opc));

	rc = cont_svc_lookup(in->cpi_pool, 0 /* id */, &svc);
	if (rc != 0)
		D_GOTO(out, rc);

	pthread_rwlock_wrlock(&svc->cs_rwlock);

	rc = cont_lookup(svc, in->cpi_cont, &cont);
	if (rc != 0)
		D_GOTO(out_rwlock, rc);

	/* Verify the container handle. */
	rc = dsms_kvs_uv_lookup(cont->c_handles, in->cpi_cont_hdl, &hdl,
				sizeof(hdl));
	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			D_ERROR(DF_CONT"rejecting unauthorized operation: "
				DF_UUID"\n",
				DP_CONT(cont->c_svc->cs_pool, cont->c_uuid),
				DP_UUID(in->cpi_cont_hdl));
			rc = -DER_NO_PERM;
		} else {
			D_ERROR(DF_CONT"failed to look up container handle "
				DF_UUID": %d\n",
				DP_CONT(cont->c_svc->cs_pool, cont->c_uuid),
				DP_UUID(in->cpi_cont_hdl), rc);
		}
		D_GOTO(out_cont, rc);
	}

	switch (opc_get(rpc->dr_opc)) {
	case DSM_CONT_EPOCH_QUERY:
		hdlr = cont_epoch_query;
		break;
	case DSM_CONT_EPOCH_HOLD:
		hdlr = cont_epoch_hold;
		break;
	case DSM_CONT_EPOCH_COMMIT:
		hdlr = cont_epoch_commit;
		break;
	default:
		D_ASSERT(0);
	}

	rc = hdlr(svc, cont, &hdl, in, out);

out_cont:
	cont_put(cont);
out_rwlock:
	pthread_rwlock_unlock(&svc->cs_rwlock);
	cont_svc_put(svc);
out:
	D_DEBUG(DF_DSMS, "leave: rc=%d\n", rc);
	out->cpo_ret = rc;
	rc = dtp_reply_send(rpc);
	return rc;
}
