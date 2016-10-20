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
#include <daos/transport.h>
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
bcast_create(dtp_context_t ctx, struct cont_svc *svc, dtp_opcode_t opcode,
	     dtp_rpc_t **rpc)
{
	return ds_pool_bcast_create(ctx, svc->cs_pool, DAOS_CONT_MODULE, opcode,
				    rpc);
}

int
dsms_hdlr_cont_create(dtp_rpc_t *rpc)
{
	struct cont_create_in  *in = dtp_req_get(rpc);
	struct cont_create_out *out = dtp_reply_get(rpc);
	struct cont_svc	       *svc;
	struct ds_pool_hdl     *pool_hdl;
	volatile daos_handle_t	ch = DAOS_HDL_INVAL;
	int			rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);
	D_ASSERT(in != NULL);
	D_ASSERT(out != NULL);

	D_DEBUG(DF_DSMS, DF_CONT": handling rpc %p: pool_hdl="DF_UUID"\n",
		DP_CONT(in->cci_pool, in->cci_cont), rpc,
		DP_UUID(in->cci_pool_hdl));

	/* Verify the pool handle. */
	pool_hdl = ds_pool_hdl_lookup(in->cci_pool_hdl);
	if (pool_hdl == NULL)
		D_GOTO(out, rc = -DER_NO_PERM);
	else if (!(pool_hdl->sph_capas & DAOS_PC_RW) &&
		 !(pool_hdl->sph_capas & DAOS_PC_EX))
		D_GOTO(out_pool_hdl, rc = -DER_NO_PERM);

	/*
	 * TODO: How to map to the correct container service among those
	 * running of this storage node? (Currently, there is only one, with ID
	 * 0, colocated with the pool service.)
	 */
	rc = cont_svc_lookup(in->cci_pool, 0 /* id */, &svc);
	if (rc != 0)
		D_GOTO(out_pool_hdl, rc);

	pthread_rwlock_wrlock(&svc->cs_rwlock);

	/*
	 * Target-side creations (i.e., vos_co_create() calls) are deferred to
	 * the time when the container is first successfully opened.
	 */

	TX_BEGIN(svc->cs_mpool->mp_pmem) {
		daos_handle_t	h;
		uint64_t	ghce = 0;

		/* Create the container tree under the container index tree. */
		rc = dbtree_uv_create_tree(svc->cs_containers, in->cci_cont,
					   DBTREE_CLASS_NV, 0 /* feats */,
					   16 /* order */,
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

	pthread_rwlock_unlock(&svc->cs_rwlock);
	cont_svc_put(svc);
out_pool_hdl:
	ds_pool_hdl_put(pool_hdl);
out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(in->cci_pool, in->cci_cont), rpc, rc);
	out->cco_ret = rc;
	return dtp_reply_send(rpc);
}

static int
cont_destroy_bcast(dtp_context_t ctx, struct cont_svc *svc,
		   const uuid_t cont_uuid)
{
	struct tgt_cont_destroy_in     *in;
	struct tgt_cont_destroy_out    *out;
	dtp_rpc_t		       *rpc;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": bcasting\n",
		DP_CONT(svc->cs_pool_uuid, cont_uuid));

	rc = bcast_create(ctx, svc, DSM_TGT_CONT_DESTROY, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);

	in = dtp_req_get(rpc);
	uuid_copy(in->tcdi_pool, svc->cs_pool_uuid);
	uuid_copy(in->tcdi_cont, cont_uuid);

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = dtp_reply_get(rpc);
	rc = out->tcdo_ret;
	if (rc != 0)
		D_ERROR(DF_CONT": failed to destroy some targets: %d\n",
			DP_CONT(svc->cs_pool_uuid, cont_uuid), rc);

out_rpc:
	dtp_req_decref(rpc);
out:
	D_DEBUG(DF_DSMS, DF_CONT": bcasted: %d\n",
		DP_CONT(svc->cs_pool_uuid, cont_uuid), rc);
	return rc;
}

int
dsms_hdlr_cont_destroy(dtp_rpc_t *rpc)
{
	struct cont_destroy_in	       *in = dtp_req_get(rpc);
	struct cont_destroy_out	       *out = dtp_reply_get(rpc);
	struct cont_svc		       *svc;
	struct ds_pool_hdl	       *pool_hdl;
	volatile daos_handle_t		ch;
	daos_handle_t			h;
	int				rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);
	D_ASSERT(in != NULL);
	D_ASSERT(out != NULL);

	D_DEBUG(DF_DSMS, DF_CONT": handling rpc %p: pool_hdl="DF_UUID
		" force=%u\n", DP_CONT(in->cdi_pool, in->cdi_cont), rpc,
		DP_UUID(in->cdi_pool_hdl), in->cdi_force);

	/* Verify the pool handle. */
	pool_hdl = ds_pool_hdl_lookup(in->cdi_pool_hdl);
	if (pool_hdl == NULL)
		D_GOTO(out, rc = -DER_NO_PERM);
	else if (!(pool_hdl->sph_capas & DAOS_PC_RW) &&
		 !(pool_hdl->sph_capas & DAOS_PC_EX))
		D_GOTO(out_pool_hdl, rc = -DER_NO_PERM);

	rc = cont_svc_lookup(in->cdi_pool, 0 /* id */, &svc);
	if (rc != 0)
		D_GOTO(out_pool_hdl, rc);

	pthread_rwlock_wrlock(&svc->cs_rwlock);

	rc = dbtree_uv_open_tree(svc->cs_containers, in->cdi_cont,
				 svc->cs_mpool->mp_pmem, &h);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			rc = 0;
		D_GOTO(out_rwlock, rc);
	}
	ch = h;

	rc = cont_destroy_bcast(rpc->dr_ctx, svc, in->cdi_cont);
	if (rc != 0)
		D_GOTO(out_rwlock, rc);


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

		rc = dbtree_uv_delete(svc->cs_containers, in->cdi_cont);
		if (rc != 0) {
			D_ERROR("failed to delete container tree: %d\n", rc);
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
out_pool_hdl:
	ds_pool_hdl_put(pool_hdl);
out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(in->cdi_pool, in->cdi_cont), rpc, rc);
	out->cdo_ret = rc;
	return dtp_reply_send(rpc);
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
cont_open_bcast(dtp_context_t ctx, struct cont *cont, const uuid_t pool_hdl,
		const uuid_t cont_hdl, uint64_t capas)
{
	struct tgt_cont_open_in	       *in;
	struct tgt_cont_open_out       *out;
	dtp_rpc_t		       *rpc;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": bcasting: pool_hdl="DF_UUID" cont_hdl="
		DF_UUID" capas="DF_X64"\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
		DP_UUID(pool_hdl), DP_UUID(cont_hdl), capas);

	rc = bcast_create(ctx, cont->c_svc, DSM_TGT_CONT_OPEN, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);

	in = dtp_req_get(rpc);
	uuid_copy(in->tcoi_pool, cont->c_svc->cs_pool_uuid);
	uuid_copy(in->tcoi_pool_hdl, pool_hdl);
	uuid_copy(in->tcoi_cont, cont->c_uuid);
	uuid_copy(in->tcoi_cont_hdl, cont_hdl);
	in->tcoi_capas = capas;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = dtp_reply_get(rpc);
	rc = out->tcoo_ret;
	if (rc != 0)
		D_ERROR(DF_CONT": failed to open some targets: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);

out_rpc:
	dtp_req_decref(rpc);
out:
	D_DEBUG(DF_DSMS, DF_CONT": bcasted: pool_hdl="DF_UUID" cont_hdl="DF_UUID
		" capas="DF_X64": %d\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
		DP_UUID(pool_hdl), DP_UUID(cont_hdl), capas, rc);
	return rc;
}

int
dsms_hdlr_cont_open(dtp_rpc_t *rpc)
{
	struct cont_open_in	*in = dtp_req_get(rpc);
	struct cont_open_out	*out = dtp_reply_get(rpc);
	struct cont_svc		*svc;
	struct ds_pool_hdl	*pool_hdl;
	struct cont		*cont;
	struct container_hdl	chdl;
	daos_epoch_t		ghce;
	daos_epoch_t		glre;
	daos_epoch_t		ghpce = DAOS_EPOCH_MAX;
	int			rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);
	D_ASSERT(in != NULL);
	D_ASSERT(out != NULL);

	D_DEBUG(DF_DSMS, DF_CONT": handling rpc %p: pool_hdl="DF_UUID
		" cont_hdl="DF_UUID" capas="DF_X64"\n",
		DP_CONT(in->coi_pool, in->coi_cont), rpc,
		DP_UUID(in->coi_pool_hdl), DP_UUID(in->coi_cont_hdl),
		in->coi_capas);

	/* Verify the pool handle. */
	pool_hdl = ds_pool_hdl_lookup(in->coi_pool_hdl);
	if (pool_hdl == NULL)
		D_GOTO(out, rc = -DER_NO_PERM);
	else if ((!(pool_hdl->sph_capas & DAOS_PC_RO) &&
		  !(pool_hdl->sph_capas & DAOS_PC_RW) &&
		  !(pool_hdl->sph_capas & DAOS_PC_EX)) ||
		 (!(pool_hdl->sph_capas & DAOS_PC_RW) &&
		  !(pool_hdl->sph_capas & DAOS_PC_EX) &&
		  (in->coi_capas & DAOS_COO_RW)))
		D_GOTO(out_pool_hdl, rc = -DER_NO_PERM);

	rc = cont_svc_lookup(in->coi_pool, 0 /* id */, &svc);
	if (rc != 0)
		D_GOTO(out_pool_hdl, rc);

	pthread_rwlock_wrlock(&svc->cs_rwlock);

	rc = cont_lookup(svc, in->coi_cont, &cont);
	if (rc != 0)
		D_GOTO(out_rwlock, rc);

	/* See if this container handle already exists. */
	rc = dbtree_uv_lookup(cont->c_handles, in->coi_cont_hdl, &chdl,
			      sizeof(chdl));
	if (rc != -DER_NONEXIST) {
		if (rc == 0 && chdl.ch_capas != in->coi_capas) {
			D_ERROR(DF_CONT": found conflicting container handle\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid));
			rc = -DER_EXIST;
		}
		D_GOTO(out_cont, rc);
	}

	rc = cont_open_bcast(rpc->dr_ctx, cont, in->coi_pool_hdl,
			     in->coi_cont_hdl, in->coi_capas);
	if (rc != 0)
		D_GOTO(out_cont, rc);

	/* TODO: Rollback cont_open_bcast() on errors from now on. */

	/* Get GHPCE. */
	rc = dbtree_ec_fetch(cont->c_hces, BTR_PROBE_LAST, NULL /* epoch_in */,
			     &ghpce, NULL /* count */);
	if (rc != 0 && rc != -DER_NONEXIST)
		D_GOTO(out_cont, rc);

	/* Get GHCE. */
	rc = dbtree_nv_lookup(cont->c_cont, CONT_GHCE, &ghce, sizeof(ghce));
	if (rc != 0)
		D_GOTO(out_cont, rc);

	/*
	 * Check the coo_epoch_state assignments below if any of these rules
	 * changes.
	 */
	chdl.ch_hce = ghpce == DAOS_EPOCH_MAX ? ghce : ghpce;
	chdl.ch_lre = chdl.ch_hce;
	chdl.ch_lhe = DAOS_EPOCH_MAX;
	chdl.ch_capas = in->coi_capas;

	TX_BEGIN(svc->cs_mpool->mp_pmem) {
		rc = dbtree_uv_update(cont->c_handles, in->coi_cont_hdl, &chdl,
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
		D_GOTO(out_cont, rc);

	/* Calculate GLRE. */
	rc = dbtree_ec_fetch(cont->c_lres, BTR_PROBE_FIRST, NULL /* epoch_in */,
			     &glre, NULL /* count */);
	if (rc != 0) {
		/* At least there shall be this handle's LRE. */
		D_ASSERT(rc != -DER_NONEXIST);
		D_GOTO(out_cont, rc);
	}

	out->coo_epoch_state.es_hce = chdl.ch_hce;
	out->coo_epoch_state.es_lre = chdl.ch_lre;
	out->coo_epoch_state.es_lhe = chdl.ch_lhe;
	out->coo_epoch_state.es_ghce = ghce;
	out->coo_epoch_state.es_glre = glre;
	out->coo_epoch_state.es_ghpce = chdl.ch_hce;

out_cont:
	cont_put(cont);
out_rwlock:
	pthread_rwlock_unlock(&svc->cs_rwlock);
	cont_svc_put(svc);
out_pool_hdl:
	ds_pool_hdl_put(pool_hdl);
out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(in->coi_pool, in->coi_cont), rpc, rc);
	out->coo_ret = rc;
	return dtp_reply_send(rpc);
}

static int
cont_close_bcast(dtp_context_t ctx, struct cont *cont, const uuid_t cont_hdl)
{
	struct tgt_cont_close_in       *in;
	struct tgt_cont_close_out      *out;
	dtp_rpc_t		       *rpc;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": bcasting: cont_hdl="DF_UUID"\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
		DP_UUID(cont_hdl));

	rc = bcast_create(ctx, cont->c_svc, DSM_TGT_CONT_CLOSE, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);

	in = dtp_req_get(rpc);
	uuid_copy(in->tcci_cont_hdl, cont_hdl);

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = dtp_reply_get(rpc);
	rc = out->tcco_ret;
	if (rc != 0)
		D_ERROR(DF_CONT": failed to close some targets: %d\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);

out_rpc:
	dtp_req_decref(rpc);
out:
	D_DEBUG(DF_DSMS, DF_CONT": bcasted: cont_hdl="DF_UUID": %d\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
		DP_UUID(cont_hdl), rc);
	return rc;
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

	D_DEBUG(DF_DSMS, DF_CONT": handling rpc %p: hdl="DF_UUID"\n",
		DP_CONT(in->cci_pool, in->cci_cont), rpc,
		DP_UUID(in->cci_cont_hdl));

	rc = cont_svc_lookup(in->cci_pool, 0 /* id */, &svc);
	if (rc != 0)
		D_GOTO(out, rc);

	pthread_rwlock_wrlock(&svc->cs_rwlock);

	rc = cont_lookup(svc, in->cci_cont, &cont);
	if (rc != 0)
		D_GOTO(out_rwlock, rc);

	/* See if this container handle is already closed. */
	rc = dbtree_uv_lookup(cont->c_handles, in->cci_cont_hdl, &chdl,
			      sizeof(chdl));
	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			D_DEBUG(DF_DSMS, DF_CONT": already closed: "DF_UUID"\n",
				DP_CONT(svc->cs_pool->sp_uuid, cont->c_uuid),
				DP_UUID(in->cci_cont_hdl));
			rc = 0;
		}
		D_GOTO(out_cont, rc);
	}

	rc = cont_close_bcast(rpc->dr_ctx, cont, in->cci_cont_hdl);
	if (rc != 0)
		D_GOTO(out_cont, rc);

	TX_BEGIN(svc->cs_mpool->mp_pmem) {
		rc = dbtree_uv_delete(cont->c_handles, in->cci_cont_hdl);
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

	if (rc != 0)
		D_GOTO(out_cont, rc);

out_cont:
	cont_put(cont);
out_rwlock:
	pthread_rwlock_unlock(&svc->cs_rwlock);
	cont_svc_put(svc);
out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(in->cci_pool, in->cci_cont), rpc, rc);
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
	state->es_ghce = hdl->ch_hce;
	state->es_glre = hdl->ch_lre;
	state->es_ghpce = hdl->ch_hce;
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
		rc = dbtree_uv_update(cont->c_handles,
				      in->eoi_cont_op_in.cpi_cont_hdl, hdl,
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
		rc = dbtree_uv_update(cont->c_handles,
				      in->eoi_cont_op_in.cpi_cont_hdl, hdl,
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
	rc = dbtree_uv_lookup(cont->c_handles, in->cpi_cont_hdl, &hdl,
			      sizeof(hdl));
	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			D_ERROR(DF_CONT": rejecting unauthorized operation: "
				DF_UUID"\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid),
				DP_UUID(in->cpi_cont_hdl));
			rc = -DER_NO_PERM;
		} else {
			D_ERROR(DF_CONT": failed to look up container handle "
				DF_UUID": %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid),
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
