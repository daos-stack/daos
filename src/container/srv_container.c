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
 * ds_cont: Container Operations
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related container metadata.
 */

#include <daos/btree_class.h>
#include <daos/rpc.h>
#include <daos_srv/pool.h>
#include "rpc.h"
#include "srv_internal.h"
#include "srv_layout.h"

/*
 * Container service
 *
 * References the ds_pool_mpool descriptor. Identified by a number unique
 * within the pool.
 *
 * TODO: Store and look up this in a hash table.
 */
struct cont_svc {
	uuid_t			cs_pool_uuid;
	uint64_t		cs_id;
	struct ds_pool_mpool   *cs_mpool;
	struct ds_pool	       *cs_pool;
	pthread_rwlock_t	cs_rwlock;
	pthread_mutex_t		cs_lock;
	int			cs_ref;
	daos_handle_t		cs_root;	/* root tree */
	daos_handle_t		cs_containers;	/* container index tree */
};

static int
cont_svc_init(const uuid_t pool_uuid, int id, struct cont_svc *svc)
{
	struct umem_attr	uma;
	int			rc;

	svc->cs_pool = ds_pool_lookup(pool_uuid);
	if (svc->cs_pool == NULL)
		/* Therefore not a single pool handle exists. */
		D_GOTO(err, rc = -DER_NO_PERM);

	uuid_copy(svc->cs_pool_uuid, pool_uuid);
	svc->cs_id = id;
	svc->cs_ref = 1;

	rc = ds_pool_mpool_lookup(pool_uuid, &svc->cs_mpool);
	if (rc != 0)
		D_GOTO(err_pool, rc);

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

	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_u.pmem_pool = svc->cs_mpool->mp_pmem;
	rc = dbtree_open_inplace(&svc->cs_mpool->mp_sb->s_cont_root, &uma,
				 &svc->cs_root);
	if (rc != 0) {
		D_ERROR("failed to open container root tree: %d\n", rc);
		D_GOTO(err_lock, rc);
	}

	rc = dbtree_nv_open_tree(svc->cs_root, CONTAINERS,
				 svc->cs_mpool->mp_pmem, &svc->cs_containers);
	if (rc != 0) {
		D_ERROR("failed to open containers tree: %d\n", rc);
		D_GOTO(err_root, rc);
	}

	return 0;

err_root:
	dbtree_close(svc->cs_root);
err_lock:
	pthread_mutex_destroy(&svc->cs_lock);
err_rwlock:
	pthread_rwlock_destroy(&svc->cs_rwlock);
err_mp:
	ds_pool_mpool_put(svc->cs_mpool);
err_pool:
	ds_pool_put(svc->cs_pool);
err:
	return rc;
}

static int
cont_svc_lookup(const uuid_t pool_uuid, uint64_t id, struct cont_svc **svc)
{
	struct cont_svc	       *p;
	int			rc;

	/* TODO: Hash table. */

	D_DEBUG(DF_DSMS, DF_UUID"["DF_U64"]: allocating\n", DP_UUID(pool_uuid),
		id);

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
	D_DEBUG(DF_DSMS, DF_UUID"["DF_U64"]: freeing\n",
		DP_UUID(svc->cs_pool_uuid), svc->cs_id);
	dbtree_close(svc->cs_containers);
	pthread_mutex_destroy(&svc->cs_lock);
	pthread_rwlock_destroy(&svc->cs_rwlock);
	ds_pool_mpool_put(svc->cs_mpool);
	D_FREE_PTR(svc);
}

static int
bcast_create(crt_context_t ctx, struct cont_svc *svc, crt_opcode_t opcode,
	     crt_rpc_t **rpc)
{
	return ds_pool_bcast_create(ctx, svc->cs_pool, DAOS_CONT_MODULE, opcode,
				    rpc);
}

static int
cont_create(struct ds_pool_hdl *pool_hdl, struct cont_svc *svc, crt_rpc_t *rpc)
{
	struct cont_create_in  *in = crt_req_get(rpc);
	volatile daos_handle_t	ch = DAOS_HDL_INVAL;
	volatile int		rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);

	/* Verify the pool handle capabilities. */
	if (!(pool_hdl->sph_capas & DAOS_PC_RW) &&
	    !(pool_hdl->sph_capas & DAOS_PC_EX))
		D_GOTO(out, rc = -DER_NO_PERM);

	/*
	 * Target-side creations (i.e., vos_co_create() calls) are deferred to
	 * the time when the container is first successfully opened.
	 */

	TX_BEGIN(svc->cs_mpool->mp_pmem) {
		daos_handle_t	h;
		uint64_t	ghce = 0;

		/* Create the container tree under the container index tree. */
		rc = dbtree_uv_create_tree(svc->cs_containers,
					   in->cci_op.ci_uuid, DBTREE_CLASS_NV,
					   0 /* feats */, 16 /* order */,
					   svc->cs_mpool->mp_pmem, &h);
		if (rc != 0) {
			D_ERROR("failed to create container tree: %d\n", rc);
			pmemobj_tx_abort(rc);
		}

		ch = h;

		rc = dbtree_nv_update(ch, CONT_GHCE, &ghce, sizeof(ghce));
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_nv_create_tree(ch, CONT_HCES, DBTREE_CLASS_EC,
					   0 /* feats */, 16 /* order */,
					   svc->cs_mpool->mp_pmem,
					   NULL /* tree_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_nv_create_tree(ch, CONT_LRES, DBTREE_CLASS_EC,
					   0 /* feats */, 16 /* order */,
					   svc->cs_mpool->mp_pmem,
					   NULL /* tree_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_nv_create_tree(ch, CONT_LHES, DBTREE_CLASS_EC,
					   0 /* feats */, 16 /* order */,
					   svc->cs_mpool->mp_pmem,
					   NULL /* tree_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_nv_create_tree(ch, CONT_SNAPSHOTS, DBTREE_CLASS_EC,
					   0 /* feats */, 16 /* order */,
					   svc->cs_mpool->mp_pmem,
					   NULL /* tree_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_nv_create_tree(ch, CONT_HANDLES, DBTREE_CLASS_UV,
					   0 /* feats */, 16 /* order */,
					   svc->cs_mpool->mp_pmem,
					   NULL /* tree_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_FINALLY {
		if (!daos_handle_is_inval(ch))
			dbtree_close(ch);
	} TX_END

out:
	return rc;
}

static int
cont_destroy_bcast(crt_context_t ctx, struct cont_svc *svc,
		   const uuid_t cont_uuid)
{
	struct cont_tgt_destroy_in     *in;
	struct cont_tgt_destroy_out    *out;
	crt_rpc_t		       *rpc;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": bcasting\n",
		DP_CONT(svc->cs_pool_uuid, cont_uuid));

	rc = bcast_create(ctx, svc, CONT_TGT_DESTROY, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);

	in = crt_req_get(rpc);
	uuid_copy(in->tdi_pool_uuid, svc->cs_pool_uuid);
	uuid_copy(in->tdi_uuid, cont_uuid);

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->tdo_rc;
	if (rc != 0)
		D_ERROR(DF_CONT": failed to destroy some targets: %d\n",
			DP_CONT(svc->cs_pool_uuid, cont_uuid), rc);

out_rpc:
	crt_req_decref(rpc);
out:
	D_DEBUG(DF_DSMS, DF_CONT": bcasted: %d\n",
		DP_CONT(svc->cs_pool_uuid, cont_uuid), rc);
	return rc;
}

static int
cont_destroy(struct ds_pool_hdl *pool_hdl, struct cont_svc *svc, crt_rpc_t *rpc)
{
	struct cont_destroy_in	       *in = crt_req_get(rpc);
	volatile daos_handle_t		ch;
	daos_handle_t			h;
	volatile int			rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: force=%u\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cdi_op.ci_uuid), rpc,
		in->cdi_force);

	/* Verify the pool handle capabilities. */
	if (!(pool_hdl->sph_capas & DAOS_PC_RW) &&
	    !(pool_hdl->sph_capas & DAOS_PC_EX))
		D_GOTO(out, rc = -DER_NO_PERM);

	/* Open the container tree. */
	rc = dbtree_uv_open_tree(svc->cs_containers, in->cdi_op.ci_uuid,
				 svc->cs_mpool->mp_pmem, &h);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			rc = 0;
		D_GOTO(out, rc);
	}
	ch = h;

	rc = cont_destroy_bcast(rpc->cr_ctx, svc, in->cdi_op.ci_uuid);
	if (rc != 0)
		D_GOTO(out_ch, rc);

	TX_BEGIN(svc->cs_mpool->mp_pmem) {
		rc = dbtree_nv_destroy_tree(ch, CONT_HANDLES,
					    svc->cs_mpool->mp_pmem);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_nv_destroy_tree(ch, CONT_SNAPSHOTS,
					    svc->cs_mpool->mp_pmem);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_nv_destroy_tree(ch, CONT_LHES,
					    svc->cs_mpool->mp_pmem);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_nv_destroy_tree(ch, CONT_LRES,
					    svc->cs_mpool->mp_pmem);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_nv_destroy_tree(ch, CONT_HCES,
					    svc->cs_mpool->mp_pmem);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_destroy(ch);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		ch = DAOS_HDL_INVAL;

		rc = dbtree_uv_delete(svc->cs_containers, in->cdi_op.ci_uuid);
		if (rc != 0) {
			D_ERROR("failed to delete container tree: %d\n", rc);
			pmemobj_tx_abort(rc);
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_END

out_ch:
	if (!daos_handle_is_inval(ch))
		dbtree_close(ch);
out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cdi_op.ci_uuid), rpc,
		rc);
	return rc;
}

static int
ec_increment(daos_handle_t tree, uint64_t epoch)
{
	uint64_t	c = 0;
	uint64_t	c_new;
	int		rc;

	rc = dbtree_ec_lookup(tree, epoch, &c);
	if (rc != 0 && rc != -DER_NONEXIST)
		return rc;

	c_new = c + 1;
	if (c_new < c)
		return -DER_OVERFLOW;

	return dbtree_ec_update(tree, epoch, &c_new);
}

static int
ec_decrement(daos_handle_t tree, uint64_t epoch)
{
	uint64_t	c = 0;
	uint64_t	c_new;
	int		rc;

	rc = dbtree_ec_lookup(tree, epoch, &c);
	if (rc != 0 && rc != -DER_NONEXIST)
		return rc;

	c_new = c - 1;
	if (c_new > c)
		return -DER_OVERFLOW;

	if (c_new == 0)
		rc = dbtree_ec_delete(tree, epoch);
	else
		rc = dbtree_ec_update(tree, epoch, &c_new);

	return rc;
}

/* Container descriptor */
struct cont {
	uuid_t			c_uuid;
	struct cont_svc	       *c_svc;
	daos_handle_t		c_cont;		/* container tree */
	daos_handle_t		c_hces;		/* HCE tree */
	daos_handle_t		c_lres;		/* LRE tree */
	daos_handle_t		c_lhes;		/* LHE tree */
	daos_handle_t		c_handles;	/* container handle tree */
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

	rc = dbtree_uv_open_tree(svc->cs_containers, uuid,
				 svc->cs_mpool->mp_pmem, &p->c_cont);
	if (rc != 0)
		D_GOTO(err_p, rc);

	rc = dbtree_nv_open_tree(p->c_cont, CONT_HCES, svc->cs_mpool->mp_pmem,
				 &p->c_hces);
	if (rc != 0)
		D_GOTO(err_cont, rc);

	rc = dbtree_nv_open_tree(p->c_cont, CONT_LRES, svc->cs_mpool->mp_pmem,
				 &p->c_lres);
	if (rc != 0)
		D_GOTO(err_hces, rc);

	rc = dbtree_nv_open_tree(p->c_cont, CONT_LHES, svc->cs_mpool->mp_pmem,
				 &p->c_lhes);
	if (rc != 0)
		D_GOTO(err_lres, rc);

	rc = dbtree_nv_open_tree(p->c_cont, CONT_HANDLES,
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

static int
cont_open_bcast(crt_context_t ctx, struct cont *cont, const uuid_t pool_hdl,
		const uuid_t cont_hdl, uint64_t capas)
{
	struct cont_tgt_open_in	       *in;
	struct cont_tgt_open_out       *out;
	crt_rpc_t		       *rpc;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": bcasting: pool_hdl="DF_UUID" cont_hdl="
		DF_UUID" capas="DF_X64"\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
		DP_UUID(pool_hdl), DP_UUID(cont_hdl), capas);

	rc = bcast_create(ctx, cont->c_svc, CONT_TGT_OPEN, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);

	in = crt_req_get(rpc);
	uuid_copy(in->toi_pool_uuid, cont->c_svc->cs_pool_uuid);
	uuid_copy(in->toi_pool_hdl, pool_hdl);
	uuid_copy(in->toi_uuid, cont->c_uuid);
	uuid_copy(in->toi_hdl, cont_hdl);
	in->toi_capas = capas;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->too_rc;
	if (rc != 0)
		D_ERROR(DF_CONT": failed to open some targets: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);

out_rpc:
	crt_req_decref(rpc);
out:
	D_DEBUG(DF_DSMS, DF_CONT": bcasted: pool_hdl="DF_UUID" cont_hdl="DF_UUID
		" capas="DF_X64": %d\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
		DP_UUID(pool_hdl), DP_UUID(cont_hdl), capas, rc);
	return rc;
}

static int
cont_open(struct ds_pool_hdl *pool_hdl, struct cont *cont, crt_rpc_t *rpc)
{
	struct cont_open_in    *in = crt_req_get(rpc);
	struct cont_open_out   *out = crt_reply_get(rpc);
	struct container_hdl	chdl;
	daos_epoch_t		ghce;
	daos_epoch_t		glre;
	daos_epoch_t		ghpce = DAOS_EPOCH_MAX;
	volatile int		rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID" capas="
		DF_X64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->coi_op.ci_uuid), rpc,
		DP_UUID(in->coi_op.ci_hdl), in->coi_capas);

	/* Verify the pool handle capabilities. */
	if ((in->coi_capas & DAOS_COO_RW) &&
	    !(pool_hdl->sph_capas & DAOS_PC_RW) &&
	    !(pool_hdl->sph_capas & DAOS_PC_EX))
		D_GOTO(out, rc = -DER_NO_PERM);

	/* See if this container handle already exists. */
	rc = dbtree_uv_lookup(cont->c_handles, in->coi_op.ci_hdl, &chdl,
			      sizeof(chdl));
	if (rc != -DER_NONEXIST) {
		if (rc == 0 && chdl.ch_capas != in->coi_capas) {
			D_ERROR(DF_CONT": found conflicting container handle\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid));
			rc = -DER_EXIST;
		}
		D_GOTO(out, rc);
	}

	rc = cont_open_bcast(rpc->cr_ctx, cont, in->coi_op.ci_pool_hdl,
			     in->coi_op.ci_hdl, in->coi_capas);
	if (rc != 0)
		D_GOTO(out, rc);

	/* TODO: Rollback cont_open_bcast() on errors from now on. */

	/* Get GHPCE. */
	rc = dbtree_ec_fetch(cont->c_hces, BTR_PROBE_LAST, NULL /* epoch_in */,
			     &ghpce, NULL /* count */);
	if (rc != 0 && rc != -DER_NONEXIST)
		D_GOTO(out, rc);

	/* Get GHCE. */
	rc = dbtree_nv_lookup(cont->c_cont, CONT_GHCE, &ghce, sizeof(ghce));
	if (rc != 0)
		D_GOTO(out, rc);

	/*
	 * Check the coo_epoch_state assignments below if any of these rules
	 * changes.
	 */
	chdl.ch_hce = ghpce == DAOS_EPOCH_MAX ? ghce : ghpce;
	chdl.ch_lre = chdl.ch_hce;
	chdl.ch_lhe = DAOS_EPOCH_MAX;
	chdl.ch_capas = in->coi_capas;

	TX_BEGIN(cont->c_svc->cs_mpool->mp_pmem) {
		rc = dbtree_uv_update(cont->c_handles, in->coi_op.ci_hdl, &chdl,
				      sizeof(chdl));
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = ec_increment(cont->c_hces, chdl.ch_hce);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to update hce tree: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_increment(cont->c_lres, chdl.ch_lre);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to update lre tree: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_increment(cont->c_lhes, chdl.ch_lhe);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to update lhe tree: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			pmemobj_tx_abort(rc);
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_END

	if (rc != 0)
		D_GOTO(out, rc);

	/* Calculate GLRE. */
	rc = dbtree_ec_fetch(cont->c_lres, BTR_PROBE_FIRST, NULL /* epoch_in */,
			     &glre, NULL /* count */);
	if (rc != 0) {
		/* At least there shall be this handle's LRE. */
		D_ASSERT(rc != -DER_NONEXIST);
		D_GOTO(out, rc);
	}

	out->coo_epoch_state.es_hce = chdl.ch_hce;
	out->coo_epoch_state.es_lre = chdl.ch_lre;
	out->coo_epoch_state.es_lhe = chdl.ch_lhe;
	out->coo_epoch_state.es_ghce = ghce;
	out->coo_epoch_state.es_glre = glre;
	out->coo_epoch_state.es_ghpce = chdl.ch_hce;

out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->coi_op.ci_uuid), rpc,
		rc);
	return rc;
}

static int
cont_close_bcast(crt_context_t ctx, struct cont *cont, const uuid_t cont_hdl)
{
	struct cont_tgt_close_in       *in;
	struct cont_tgt_close_out      *out;
	crt_rpc_t		       *rpc;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": bcasting: cont_hdl="DF_UUID"\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
		DP_UUID(cont_hdl));

	rc = bcast_create(ctx, cont->c_svc, CONT_TGT_CLOSE, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);

	in = crt_req_get(rpc);
	uuid_copy(in->tci_hdl, cont_hdl);

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->tco_rc;
	if (rc != 0)
		D_ERROR(DF_CONT": failed to close some targets: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);

out_rpc:
	crt_req_decref(rpc);
out:
	D_DEBUG(DF_DSMS, DF_CONT": bcasted: cont_hdl="DF_UUID": %d\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
		DP_UUID(cont_hdl), rc);
	return rc;
}

static int
cont_close(struct ds_pool_hdl *pool_hdl, struct cont *cont, crt_rpc_t *rpc)
{
	struct cont_close_in   *in = crt_req_get(rpc);
	struct container_hdl	chdl;
	volatile int		rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid), rpc,
		DP_UUID(in->cci_op.ci_hdl));

	/* See if this container handle is already closed. */
	rc = dbtree_uv_lookup(cont->c_handles, in->cci_op.ci_hdl, &chdl,
			      sizeof(chdl));
	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			D_DEBUG(DF_DSMS, DF_CONT": already closed: "DF_UUID"\n",
				DP_CONT(cont->c_svc->cs_pool->sp_uuid,
					cont->c_uuid),
				DP_UUID(in->cci_op.ci_hdl));
			rc = 0;
		}
		D_GOTO(out, rc);
	}

	rc = cont_close_bcast(rpc->cr_ctx, cont, in->cci_op.ci_hdl);
	if (rc != 0)
		D_GOTO(out, rc);

	TX_BEGIN(cont->c_svc->cs_mpool->mp_pmem) {
		rc = dbtree_uv_delete(cont->c_handles, in->cci_op.ci_hdl);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = ec_decrement(cont->c_hces, chdl.ch_hce);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to update hce tree: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_decrement(cont->c_lres, chdl.ch_lre);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to update lre tree: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_decrement(cont->c_lhes, chdl.ch_lhe);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to update lhe tree: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			pmemobj_tx_abort(rc);
		}

		/* TODO: Update GHCE. */
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_END

out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid), rpc,
		rc);
	return rc;
}

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
	state->es_ghce = hdl->ch_hce;
	state->es_glre = hdl->ch_lre;
	state->es_ghpce = hdl->ch_hce;
}

static int
cont_epoch_query(struct ds_pool_hdl *pool_hdl, struct cont *cont,
		 struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_epoch_op_out *out = crt_reply_get(rpc);

	epoch_state_set(hdl, &out->ceo_epoch_state);
	return 0;
}

static int
cont_epoch_hold(struct ds_pool_hdl *pool_hdl, struct cont *cont,
		struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_epoch_op_in	       *in = crt_req_get(rpc);
	struct cont_epoch_op_out       *out = crt_reply_get(rpc);
	daos_epoch_t			lhe = hdl->ch_lhe;
	daos_epoch_t			ghpce = hdl->ch_hce;
	volatile int			rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: epoch="DF_U64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		in->cei_epoch);

	/* Verify the container handle capabilities. */
	if (!(hdl->ch_capas & DAOS_COO_RW))
		D_GOTO(out, rc = -DER_NO_PERM);

	if (in->cei_epoch == hdl->ch_lhe)
		D_GOTO(out, rc = 0);

	if (in->cei_epoch <= ghpce)
		hdl->ch_lhe = ghpce + 1;
	else
		hdl->ch_lhe = in->cei_epoch;

	D_DEBUG(DF_DSMS, "lhe="DF_U64" lhe'="DF_U64"\n", lhe, hdl->ch_lhe);

	TX_BEGIN(cont->c_svc->cs_mpool->mp_pmem) {
		rc = dbtree_uv_update(cont->c_handles, in->cei_op.ci_hdl, hdl,
				      sizeof(*hdl));
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = ec_decrement(cont->c_lhes, lhe);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to remove original lhe: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_increment(cont->c_lhes, hdl->ch_lhe);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to add new lhe: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			pmemobj_tx_abort(rc);
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_END

	if (rc != 0)
		hdl->ch_lhe = lhe;

out:
	epoch_state_set(hdl, &out->ceo_epoch_state);
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		rc);
	return rc;
}

static int
cont_epoch_commit(struct ds_pool_hdl *pool_hdl, struct cont *cont,
		  struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_epoch_op_in	       *in = crt_req_get(rpc);
	struct cont_epoch_op_out       *out = crt_reply_get(rpc);
	daos_epoch_t			hce = hdl->ch_hce;
	daos_epoch_t			lhe = hdl->ch_lhe;
	volatile int			rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: epoch="DF_U64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		in->cei_epoch);

	/* Verify the container handle capabilities. */
	if (!(hdl->ch_capas & DAOS_COO_RW))
		D_GOTO(out, rc = -DER_NO_PERM);

	if (in->cei_epoch <= hdl->ch_hce)
		D_GOTO(out, rc = 0);

	if (in->cei_epoch < hdl->ch_lhe)
		D_GOTO(out, rc = -DER_EP_RO);

	hdl->ch_hce = in->cei_epoch;
	hdl->ch_lhe = hdl->ch_hce + 1;

	D_DEBUG(DF_DSMS, "hce="DF_U64" hce'="DF_U64"\n", hce, hdl->ch_hce);

	TX_BEGIN(cont->c_svc->cs_mpool->mp_pmem) {
		rc = dbtree_uv_update(cont->c_handles, in->cei_op.ci_hdl, hdl,
				      sizeof(*hdl));
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = ec_decrement(cont->c_hces, hce);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to remove original hce: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_increment(cont->c_hces, hdl->ch_hce);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to add new hce: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_decrement(cont->c_lhes, lhe);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to remove original lhe: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_increment(cont->c_lhes, hdl->ch_lhe);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to add new lhe: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			pmemobj_tx_abort(rc);
		}

		rc = dbtree_nv_update(cont->c_cont, CONT_GHCE, &hdl->ch_hce,
				      sizeof(hdl->ch_hce));
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to update ghce: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			pmemobj_tx_abort(rc);
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_END

	if (rc != 0) {
		hdl->ch_lhe = lhe;
		hdl->ch_hce = hce;
	}

out:
	epoch_state_set(hdl, &out->ceo_epoch_state);
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		rc);
	return rc;
}

static int
cont_op_with_hdl(struct ds_pool_hdl *pool_hdl, struct cont *cont,
		 struct container_hdl *hdl, crt_rpc_t *rpc)
{
	switch (opc_get(rpc->cr_opc)) {
	case CONT_EPOCH_QUERY:
		return cont_epoch_query(pool_hdl, cont, hdl, rpc);
	case CONT_EPOCH_HOLD:
		return cont_epoch_hold(pool_hdl, cont, hdl, rpc);
	case CONT_EPOCH_COMMIT:
		return cont_epoch_commit(pool_hdl, cont, hdl, rpc);
	default:
		D_ASSERT(0);
	}
}

/*
 * Look up the container handle, or if the RPC does not need this, call the
 * final handler.
 */
static int
cont_op_with_cont(struct ds_pool_hdl *pool_hdl, struct cont *cont,
		  crt_rpc_t *rpc)
{
	struct cont_op_in      *in = crt_req_get(rpc);
	struct container_hdl	hdl;
	int			rc;

	switch (opc_get(rpc->cr_opc)) {
	case CONT_OPEN:
		rc = cont_open(pool_hdl, cont, rpc);
		break;
	case CONT_CLOSE:
		rc = cont_close(pool_hdl, cont, rpc);
		break;
	default:
		/* Look up the container handle. */
		rc = dbtree_uv_lookup(cont->c_handles, in->ci_hdl, &hdl,
				      sizeof(hdl));
		if (rc != 0) {
			if (rc == -DER_NONEXIST) {
				D_ERROR(DF_CONT": rejecting unauthorized "
					"operation: "DF_UUID"\n",
					DP_CONT(cont->c_svc->cs_pool_uuid,
						cont->c_uuid),
					DP_UUID(in->ci_hdl));
				rc = -DER_NO_PERM;
			} else {
				D_ERROR(DF_CONT": failed to look up container"
					"handle "DF_UUID": %d\n",
					DP_CONT(cont->c_svc->cs_pool_uuid,
						cont->c_uuid),
					DP_UUID(in->ci_hdl), rc);
			}
			D_GOTO(out, rc);
		}
		rc = cont_op_with_hdl(pool_hdl, cont, &hdl, rpc);
	}

out:
	return rc;
}

/*
 * Look up the container, or if the RPC does not need this, call the final
 * handler.
 */
static int
cont_op_with_svc(struct ds_pool_hdl *pool_hdl, struct cont_svc *svc,
		 crt_rpc_t *rpc)
{
	struct cont_op_in      *in = crt_req_get(rpc);
	crt_opcode_t		opc = opc_get(rpc->cr_opc);
	struct cont	       *cont = NULL;
	int			rc;

	/* TODO: Implement per-container locking. */
	if (opc == CONT_EPOCH_QUERY)
		pthread_rwlock_rdlock(&svc->cs_rwlock);
	else
		pthread_rwlock_wrlock(&svc->cs_rwlock);

	switch (opc) {
	case CONT_CREATE:
		rc = cont_create(pool_hdl, svc, rpc);
		break;
	case CONT_DESTROY:
		rc = cont_destroy(pool_hdl, svc, rpc);
		break;
	default:
		rc = cont_lookup(svc, in->ci_uuid, &cont);
		if (rc != 0)
			D_GOTO(out_lock, rc);
		rc = cont_op_with_cont(pool_hdl, cont, rpc);
		cont_put(cont);
	}

out_lock:
	pthread_rwlock_unlock(&svc->cs_rwlock);
	return rc;
}

/* Look up the pool handle and the matching container service. */
int
ds_cont_op_handler(crt_rpc_t *rpc)
{
	struct cont_op_in      *in = crt_req_get(rpc);
	struct cont_op_out     *out = crt_reply_get(rpc);
	struct ds_pool_hdl     *pool_hdl;
	crt_opcode_t		opc = opc_get(rpc->cr_opc);
	struct cont_svc	       *svc;
	int			rc;

	pool_hdl = ds_pool_hdl_lookup(in->ci_pool_hdl);
	if (pool_hdl == NULL)
		D_GOTO(out, rc = -DER_NO_PERM);

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID" opc=%u\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->ci_uuid), rpc,
		DP_UUID(in->ci_hdl), opc);

	/*
	 * TODO: How to map to the correct container service among those
	 * running of this storage node? (Currently, there is only one, with ID
	 * 0, colocated with the pool service.)
	 */
	rc = cont_svc_lookup(pool_hdl->sph_pool->sp_uuid, 0 /* id */, &svc);
	if (rc != 0)
		D_GOTO(out_pool_hdl, rc);

	rc = cont_op_with_svc(pool_hdl, svc, rpc);

	cont_svc_put(svc);
out_pool_hdl:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: hdl="DF_UUID
		" opc=%u rc=%d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->ci_uuid), rpc,
		DP_UUID(in->ci_hdl), opc, rc);
	ds_pool_hdl_put(pool_hdl);
out:
	out->co_rc = rc;
	return crt_reply_send(rpc);
}
