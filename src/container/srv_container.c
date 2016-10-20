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
 * TODO: After moving to LRU, we are still not evicting cont_svc objects based
 * on their numbers of container handles yet.
 */
struct cont_svc {
	struct daos_llink	cs_entry;
	uuid_t			cs_pool_uuid;
	uint64_t		cs_id;
	struct ds_pool_mpool   *cs_mpool;
	struct ds_pool	       *cs_pool;
	ABT_rwlock		cs_lock;
	daos_handle_t		cs_root;	/* root tree */
	daos_handle_t		cs_containers;	/* container tree */
	daos_handle_t		cs_hdls;	/* container handle tree */
};

static struct daos_lru_cache *cont_svc_cache;

struct cont_svc_key {
	uuid_t		csk_pool_uuid;
	uint64_t	csk_id;
};

static inline struct cont_svc *
cont_svc_obj(struct daos_llink *llink)
{
	return container_of(llink, struct cont_svc, cs_entry);
}

static int
cont_svc_init(const uuid_t pool_uuid, int id, struct cont_svc *svc)
{
	struct umem_attr	uma;
	int			rc;

	svc->cs_pool = ds_pool_lookup(pool_uuid);
	if (svc->cs_pool == NULL)
		/* Therefore not a single pool handle exists. */
		D_GOTO(err, rc = -DER_NO_HDL);

	uuid_copy(svc->cs_pool_uuid, pool_uuid);
	svc->cs_id = id;

	rc = ds_pool_mpool_lookup(pool_uuid, &svc->cs_mpool);
	if (rc != 0)
		D_GOTO(err_pool, rc);

	rc = ABT_rwlock_create(&svc->cs_lock);
	if (rc != ABT_SUCCESS) {
		D_ERROR("failed to create cs_lock: %d\n", rc);
		D_GOTO(err_mp, rc = dss_abterr2der(rc));
	}

	uma.uma_id = UMEM_CLASS_PMEM;
	uma.uma_u.pmem_pool = svc->cs_mpool->mp_pmem;
	rc = dbtree_open_inplace(&svc->cs_mpool->mp_sb->s_cont_root, &uma,
				 &svc->cs_root);
	if (rc != 0) {
		D_ERROR("failed to open container root tree: %d\n", rc);
		D_GOTO(err_lock, rc);
	}

	rc = dbtree_nv_open_tree(svc->cs_root, CONTAINERS, &svc->cs_containers);
	if (rc != 0) {
		D_ERROR("failed to open container tree: %d\n", rc);
		D_GOTO(err_root, rc);
	}

	rc = dbtree_nv_open_tree(svc->cs_root, CONTAINER_HDLS, &svc->cs_hdls);
	if (rc != 0) {
		D_ERROR("failed to open container handle tree: %d\n", rc);
		D_GOTO(err_containers, rc);
	}

	return 0;

err_containers:
	dbtree_close(svc->cs_containers);
err_root:
	dbtree_close(svc->cs_root);
err_lock:
	ABT_rwlock_free(&svc->cs_lock);
err_mp:
	ds_pool_mpool_put(svc->cs_mpool);
err_pool:
	ds_pool_put(svc->cs_pool);
err:
	return rc;
}

static int
cont_svc_alloc_ref(void *vkey, unsigned int ksize, void *varg,
		   struct daos_llink **link)
{
	struct cont_svc_key    *key = vkey;
	struct cont_svc	       *svc;
	int			rc;

	D_ASSERTF(ksize == sizeof(*key), "%u\n", ksize);

	D_DEBUG(DF_DSMS, DF_UUID"["DF_U64"]: creating\n",
		DP_UUID(key->csk_pool_uuid), key->csk_id);

	D_ALLOC_PTR(svc);
	if (svc == NULL) {
		D_ERROR("failed to allocate container service descriptor\n");
		return -DER_NOMEM;
	}

	rc = cont_svc_init(key->csk_pool_uuid, key->csk_id, svc);
	if (rc != 0) {
		D_FREE_PTR(svc);
		return rc;
	}

	*link = &svc->cs_entry;
	return 0;
}

static void
cont_svc_free_ref(struct daos_llink *llink)
{
	struct cont_svc	       *svc = cont_svc_obj(llink);

	D_DEBUG(DF_DSMS, DF_UUID"["DF_U64"]: freeing\n",
		DP_UUID(svc->cs_pool_uuid), svc->cs_id);
	dbtree_close(svc->cs_hdls);
	dbtree_close(svc->cs_containers);
	ABT_rwlock_free(&svc->cs_lock);
	ds_pool_mpool_put(svc->cs_mpool);
	ds_pool_put(svc->cs_pool);
	D_FREE_PTR(svc);
}

static bool
cont_svc_cmp_keys(const void *vkey, unsigned int ksize,
		  struct daos_llink *llink)
{
	const struct cont_svc_key      *key = vkey;
	struct cont_svc		       *svc = cont_svc_obj(llink);

	return uuid_compare(key->csk_pool_uuid, svc->cs_pool_uuid) == 0 &&
	       key->csk_id == svc->cs_id;
}

static struct daos_llink_ops cont_svc_cache_ops = {
	.lop_alloc_ref	= cont_svc_alloc_ref,
	.lop_free_ref	= cont_svc_free_ref,
	.lop_cmp_keys	= cont_svc_cmp_keys
};

int
ds_cont_svc_cache_init(void)
{
	return daos_lru_cache_create(-1 /* bits */, 0 /* feats */,
				     &cont_svc_cache_ops, &cont_svc_cache);
}

void
ds_cont_svc_cache_fini(void)
{
	daos_lru_cache_destroy(cont_svc_cache);
}

static int
cont_svc_lookup(const uuid_t pool_uuid, uint64_t id, struct cont_svc **svc)
{
	struct cont_svc_key	key;
	struct daos_llink      *llink;
	int			rc;

	uuid_copy(key.csk_pool_uuid, pool_uuid);
	key.csk_id = id;

	rc = daos_lru_ref_hold(cont_svc_cache, &key, sizeof(key),
			       NULL /* args */, &llink);
	if (rc != 0) {
		D_ERROR(DF_UUID"["DF_U64"]: failed to look up container "
			"service: %d\n", DP_UUID(pool_uuid), id, rc);
		return rc;
	}

	*svc = cont_svc_obj(llink);
	return 0;
}

static void
cont_svc_put(struct cont_svc *svc)
{
	daos_lru_ref_release(cont_svc_cache, &svc->cs_entry);
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

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid), rpc);

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

		/*
		 * Create the container attribute tree under the container tree.
		 */
		rc = dbtree_uv_create_tree(svc->cs_containers,
					   in->cci_op.ci_uuid, DBTREE_CLASS_NV,
					   0 /* feats */, 16 /* order */, &h);
		if (rc != 0) {
			D_ERROR("failed to create container attribute tree: "
				"%d\n", rc);
			pmemobj_tx_abort(rc);
		}

		ch = h;

		rc = dbtree_nv_update(ch, CONT_GHCE, &ghce, sizeof(ghce));
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_nv_create_tree(ch, CONT_HCES, DBTREE_CLASS_EC,
					   0 /* feats */, 16 /* order */,
					   NULL /* tree_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_nv_create_tree(ch, CONT_LRES, DBTREE_CLASS_EC,
					   0 /* feats */, 16 /* order */,
					   NULL /* tree_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_nv_create_tree(ch, CONT_LHES, DBTREE_CLASS_EC,
					   0 /* feats */, 16 /* order */,
					   NULL /* tree_new */);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_nv_create_tree(ch, CONT_SNAPSHOTS, DBTREE_CLASS_EC,
					   0 /* feats */, 16 /* order */,
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
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to destroy %d targets\n",
			DP_CONT(svc->cs_pool_uuid, cont_uuid), rc);
		rc = -DER_IO;
	}

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

	/* Open the container attribute tree. */
	rc = dbtree_uv_open_tree(svc->cs_containers, in->cdi_op.ci_uuid, &h);
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
		rc = dbtree_nv_destroy_tree(ch, CONT_SNAPSHOTS);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_nv_destroy_tree(ch, CONT_LHES);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_nv_destroy_tree(ch, CONT_LRES);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_nv_destroy_tree(ch, CONT_HCES);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = dbtree_destroy(ch);
		if (rc != 0)
			pmemobj_tx_abort(rc);

		ch = DAOS_HDL_INVAL;

		rc = dbtree_uv_delete(svc->cs_containers, in->cdi_op.ci_uuid);
		if (rc != 0) {
			D_ERROR("failed to delete container attribute tree: "
				"%d\n", rc);
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
	daos_handle_t		c_cont;		/* container attribute tree */
	daos_handle_t		c_hces;		/* HCE tree */
	daos_handle_t		c_lres;		/* LRE tree */
	daos_handle_t		c_lhes;		/* LHE tree */
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

	rc = dbtree_uv_open_tree(svc->cs_containers, uuid, &p->c_cont);
	if (rc != 0)
		D_GOTO(err_p, rc);

	rc = dbtree_nv_open_tree(p->c_cont, CONT_HCES, &p->c_hces);
	if (rc != 0)
		D_GOTO(err_cont, rc);

	rc = dbtree_nv_open_tree(p->c_cont, CONT_LRES, &p->c_lres);
	if (rc != 0)
		D_GOTO(err_hces, rc);

	rc = dbtree_nv_open_tree(p->c_cont, CONT_LHES, &p->c_lhes);
	if (rc != 0)
		D_GOTO(err_lres, rc);

	*cont = p;
	return 0;

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
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to open %d targets\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);
		rc = -DER_IO;
	}

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
	rc = dbtree_uv_lookup(cont->c_svc->cs_hdls, in->coi_op.ci_hdl, &chdl,
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
	uuid_copy(chdl.ch_pool_hdl, pool_hdl->sph_uuid);
	uuid_copy(chdl.ch_cont, cont->c_uuid);
	chdl.ch_hce = ghpce == DAOS_EPOCH_MAX ? ghce : ghpce;
	chdl.ch_lre = chdl.ch_hce;
	chdl.ch_lhe = DAOS_EPOCH_MAX;
	chdl.ch_capas = in->coi_capas;

	TX_BEGIN(cont->c_svc->cs_mpool->mp_pmem) {
		rc = dbtree_uv_update(cont->c_svc->cs_hdls, in->coi_op.ci_hdl,
				      &chdl, sizeof(chdl));
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

/* TODO: Use bulk bcast to support large recs[]. */
static int
cont_close_bcast(crt_context_t ctx, struct cont_svc *svc,
		 struct cont_tgt_close_rec recs[], int nrecs)
{
	struct cont_tgt_close_in       *in;
	struct cont_tgt_close_out      *out;
	crt_rpc_t		       *rpc;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": bcasting: recs[0].hdl="DF_UUID
		" recs[0].hce="DF_U64" nrecs=%d\n",
		DP_CONT(svc->cs_pool_uuid, NULL), DP_UUID(recs[0].tcr_hdl),
		recs[0].tcr_hce, nrecs);

	rc = bcast_create(ctx, svc, CONT_TGT_CLOSE, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);

	in = crt_req_get(rpc);
	in->tci_recs.da_arrays = recs;
	in->tci_recs.da_count = nrecs;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc = out->tco_rc;
	if (rc != 0) {
		D_ERROR(DF_CONT": failed to close %d targets\n",
			DP_CONT(svc->cs_pool_uuid, NULL), rc);
		rc = -DER_IO;
	}

out_rpc:
	crt_req_decref(rpc);
out:
	D_DEBUG(DF_DSMS, DF_CONT": bcasted: hdls[0]="DF_UUID" nhdls=%d: %d\n",
		DP_CONT(svc->cs_pool_uuid, NULL), DP_UUID(recs[0].tcr_hdl),
		nrecs, rc);
	return rc;
}

/* Close an array of handles, possibly belonging to different containers. */
static int
cont_close_hdls(struct cont_svc *svc, struct cont_tgt_close_rec *recs,
		int nrecs, crt_context_t ctx)
{
	struct container_hdl	chdl;
	struct cont * volatile	cont = NULL;
	int			i;
	volatile int		rc;

	D_ASSERTF(nrecs > 0, "%d\n", nrecs);
	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);

	D_DEBUG(DF_DSMS, DF_CONT": closing %d recs: recs[0].hdl="DF_UUID
		" recs[0].hce="DF_U64"\n", DP_CONT(svc->cs_pool_uuid, NULL),
		nrecs, DP_UUID(recs[0].tcr_hdl), recs[0].tcr_hce);

	rc = cont_close_bcast(ctx, svc, recs, nrecs);
	if (rc != 0)
		D_GOTO(out, rc);

	TX_BEGIN(svc->cs_mpool->mp_pmem) {
		for (i = 0; i < nrecs; i++) {
			struct cont *cont_tmp;

			/* Look up the handle. */
			rc = dbtree_uv_lookup(svc->cs_hdls, recs[i].tcr_hdl,
					      &chdl, sizeof(chdl));
			if (rc != 0)
				pmemobj_tx_abort(rc);

			/* Look up the container. */
			rc = cont_lookup(svc, chdl.ch_cont, &cont_tmp);
			if (rc != 0)
				pmemobj_tx_abort(rc);
			cont = cont_tmp;

			/* Delete this handle. */
			rc = dbtree_uv_delete(svc->cs_hdls, recs[i].tcr_hdl);
			if (rc != 0)
				pmemobj_tx_abort(rc);

			rc = ec_decrement(cont->c_hces, chdl.ch_hce);
			if (rc != 0) {
				D_ERROR(DF_CONT": failed to update hce tree: "
					"%d\n",
					DP_CONT(svc->cs_pool_uuid,
						cont->c_uuid), rc);
				pmemobj_tx_abort(rc);
			}

			rc = ec_decrement(cont->c_lres, chdl.ch_lre);
			if (rc != 0) {
				D_ERROR(DF_CONT": failed to update lre tree: "
					"%d\n",
					DP_CONT(svc->cs_pool_uuid,
						cont->c_uuid), rc);
				pmemobj_tx_abort(rc);
			}

			rc = ec_decrement(cont->c_lhes, chdl.ch_lhe);
			if (rc != 0) {
				D_ERROR(DF_CONT": failed to update lhe tree: "
					"%d\n",
					DP_CONT(svc->cs_pool_uuid,
						cont->c_uuid), rc);
				pmemobj_tx_abort(rc);
			}

			/* TODO: Update GHCE. */
			cont_put(cont);
			cont = NULL;
		}
	} TX_ONABORT {
		if (cont != NULL)
			cont_put(cont);

		rc = umem_tx_errno(rc);
	} TX_END

out:
	D_DEBUG(DF_DSMS, DF_CONT": leaving: %d\n",
		DP_CONT(svc->cs_pool_uuid, NULL), rc);
	return rc;
}

static int
cont_close(struct ds_pool_hdl *pool_hdl, struct cont *cont, crt_rpc_t *rpc)
{
	struct cont_close_in	       *in = crt_req_get(rpc);
	struct container_hdl		chdl;
	struct cont_tgt_close_rec	rec;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid), rpc,
		DP_UUID(in->cci_op.ci_hdl));

	/* See if this container handle is already closed. */
	rc = dbtree_uv_lookup(cont->c_svc->cs_hdls, in->cci_op.ci_hdl, &chdl,
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

	uuid_copy(rec.tcr_hdl, in->cci_op.ci_hdl);
	rec.tcr_hce = chdl.ch_hce;

	rc = cont_close_hdls(cont->c_svc, &rec, 1 /* nrecs */, rpc->cr_ctx);

out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid), rpc,
		rc);
	return rc;
}

struct close_iter_arg {
	struct cont_tgt_close_rec      *cia_recs;
	size_t				cia_recs_size;
	int				cia_nrecs;
	uuid_t			       *cia_pool_hdls;
	int				cia_n_pool_hdls;
};

static int
shall_close(const uuid_t pool_hdl, uuid_t *pool_hdls, int n_pool_hdls)
{
	int i;

	for (i = 0; i < n_pool_hdls; i++) {
		if (uuid_compare(pool_hdls[i], pool_hdl) == 0)
			return 1;
	}
	return 0;
}

static int
close_iter_cb(daos_iov_t *key, daos_iov_t *val, void *varg)
{
	struct close_iter_arg  *arg = varg;
	struct container_hdl   *hdl;

	D_ASSERT(arg->cia_recs != NULL);
	D_ASSERT(arg->cia_recs_size > sizeof(*arg->cia_recs));

	if (key->iov_len != sizeof(uuid_t) ||
	    val->iov_len != sizeof(*hdl)) {
		D_ERROR("invalid key/value size: key="DF_U64" value="DF_U64"\n",
			key->iov_len, val->iov_len);
		return -DER_IO;
	}

	hdl = val->iov_buf;

	if (!shall_close(hdl->ch_pool_hdl, arg->cia_pool_hdls,
			 arg->cia_n_pool_hdls))
		return 0;

	/* Make sure arg->cia_recs[] have enough space for this handle. */
	if (sizeof(*arg->cia_recs) * (arg->cia_nrecs + 1) >
	    arg->cia_recs_size) {
		struct cont_tgt_close_rec      *recs_tmp;
		size_t				recs_size_tmp;

		recs_size_tmp = arg->cia_recs_size * 2;
		D_ALLOC(recs_tmp, recs_size_tmp);
		if (recs_tmp == NULL)
			return -DER_NOMEM;
		memcpy(recs_tmp, arg->cia_recs,
		       arg->cia_recs_size);
		D_FREE(arg->cia_recs, arg->cia_recs_size);
		arg->cia_recs = recs_tmp;
		arg->cia_recs_size = recs_size_tmp;
	}

	uuid_copy(arg->cia_recs[arg->cia_nrecs].tcr_hdl, key->iov_buf);
	arg->cia_recs[arg->cia_nrecs].tcr_hce = hdl->ch_hce;
	arg->cia_nrecs++;
	return 0;
}

/* Callers are responsible for freeing *recs if this function returns zero. */
static int
find_hdls_to_close(struct cont_svc *svc, uuid_t *pool_hdls, int n_pool_hdls,
		   struct cont_tgt_close_rec **recs, size_t *recs_size,
		   int *nrecs)
{
	struct close_iter_arg	arg;
	int			rc;

	arg.cia_recs_size = 4096;
	D_ALLOC(arg.cia_recs, arg.cia_recs_size);
	if (arg.cia_recs == NULL)
		return -DER_NOMEM;
	arg.cia_nrecs = 0;
	arg.cia_pool_hdls = pool_hdls;
	arg.cia_n_pool_hdls = n_pool_hdls;

	rc = dbtree_iterate(svc->cs_hdls, BTR_PROBE_FIRST, close_iter_cb, &arg);
	if (rc != 0) {
		D_FREE(arg.cia_recs, arg.cia_recs_size);
		return rc;
	}

	*recs = arg.cia_recs;
	*recs_size = arg.cia_recs_size;
	*nrecs = arg.cia_nrecs;
	return 0;
}

/*
 * Close container handles that are associated with "pool_hdls[n_pool_hdls]"
 * and managed by local container services.
 */
int
ds_cont_close_by_pool_hdls(const uuid_t pool_uuid, uuid_t *pool_hdls,
			   int n_pool_hdls, crt_context_t ctx)
{
	struct cont_svc		       *svc;
	struct cont_tgt_close_rec      *recs;
	size_t				recs_size;
	int				nrecs;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": closing by %d pool hdls: pool_hdls[0]="
		DF_UUID"\n", DP_CONT(pool_uuid, NULL), n_pool_hdls,
		DP_UUID(pool_hdls[0]));

	/* TODO: Do the following for all local container services. */
	rc = cont_svc_lookup(pool_uuid, 0 /* id */, &svc);
	if (rc != 0)
		return rc;

	ABT_rwlock_wrlock(svc->cs_lock);

	rc = find_hdls_to_close(svc, pool_hdls, n_pool_hdls, &recs, &recs_size,
				&nrecs);
	if (rc != 0)
		D_GOTO(out_lock, rc);

	if (nrecs > 0)
		rc = cont_close_hdls(svc, recs, nrecs, ctx);

	D_FREE(recs, recs_size);
out_lock:
	ABT_rwlock_unlock(svc->cs_lock);
	cont_svc_put(svc);
	return rc;
}

/*
 * TODO: Support more than one container handles. E.g., update GHCE if the
 * client is releasing the previous hold.
 */

static void
epoch_state_set(struct container_hdl *hdl, daos_epoch_state_t *state)
{
	D_ASSERTF(hdl->ch_lre <= hdl->ch_hce && hdl->ch_hce < hdl->ch_lhe,
		  "lre="DF_U64" hce="DF_U64" lhe="DF_U64"\n", hdl->ch_lre,
		  hdl->ch_hce, hdl->ch_lhe);
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

	if (in->cei_epoch > DAOS_EPOCH_MAX)
		D_GOTO(out, rc = -DER_OVERFLOW);

	if (in->cei_epoch <= ghpce)
		hdl->ch_lhe = ghpce + 1;
	else
		hdl->ch_lhe = in->cei_epoch;

	if (hdl->ch_lhe == lhe)
		D_GOTO(out, rc = 0);

	D_DEBUG(DF_DSMS, "lhe="DF_U64" lhe'="DF_U64"\n", lhe, hdl->ch_lhe);

	TX_BEGIN(cont->c_svc->cs_mpool->mp_pmem) {
		rc = dbtree_uv_update(cont->c_svc->cs_hdls, in->cei_op.ci_hdl,
				      hdl, sizeof(*hdl));
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
cont_epoch_slip(struct ds_pool_hdl *pool_hdl, struct cont *cont,
		struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_epoch_op_in	       *in = crt_req_get(rpc);
	struct cont_epoch_op_out       *out = crt_reply_get(rpc);
	daos_epoch_t			lre = hdl->ch_lre;
	volatile int			rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: epoch="DF_U64"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cei_op.ci_uuid), rpc,
		in->cei_epoch);

	if (in->cei_epoch >= DAOS_EPOCH_MAX)
		D_GOTO(out, rc = -DER_OVERFLOW);

	if (in->cei_epoch < hdl->ch_lre)
		/*
		 * Since we don't allow LRE to decrease, let the new LRE be the
		 * old one.  (This is actually unnecessary; we only have to
		 * guarantee that the new LRE has not been aggregated away.)
		 */
		hdl->ch_lre = hdl->ch_lre;
	else if (in->cei_epoch <= hdl->ch_hce)
		hdl->ch_lre = in->cei_epoch;
	else
		hdl->ch_lre = hdl->ch_hce;

	if (hdl->ch_lre == lre)
		D_GOTO(out, rc = 0);

	D_DEBUG(DF_DSMS, "lre="DF_U64" lre'="DF_U64"\n", lre, hdl->ch_lre);

	TX_BEGIN(cont->c_svc->cs_mpool->mp_pmem) {
		rc = dbtree_uv_update(cont->c_svc->cs_hdls, in->cei_op.ci_hdl,
				      hdl, sizeof(*hdl));
		if (rc != 0)
			pmemobj_tx_abort(rc);

		rc = ec_decrement(cont->c_lres, lre);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to remove original lre: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_increment(cont->c_lres, hdl->ch_lre);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to add new lre: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			pmemobj_tx_abort(rc);
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
	} TX_END

	if (rc != 0)
		hdl->ch_lre = lre;

out:
	epoch_state_set(hdl, &out->ceo_epoch_state);
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

	rc = bcast_create(ctx, cont->c_svc, CONT_TGT_EPOCH_DISCARD, &rpc);
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

static int
cont_epoch_discard(struct ds_pool_hdl *pool_hdl, struct cont *cont,
		   struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_epoch_op_in	       *in = crt_req_get(rpc);
	struct cont_epoch_op_out       *out = crt_reply_get(rpc);
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
		D_GOTO(out, rc = -DER_EP_RO);

	rc = cont_epoch_discard_bcast(rpc->cr_ctx, cont, in->cei_op.ci_hdl,
				      in->cei_epoch);

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

	if (in->cei_epoch <= hdl->ch_hce)
		D_GOTO(out, rc = 0);
	else if (in->cei_epoch < hdl->ch_lhe)
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

		rc = ec_decrement(cont->c_lres, lre);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to remove original lre: %d\n",
				DP_CONT(cont->c_svc->cs_pool_uuid,
					cont->c_uuid), rc);
			pmemobj_tx_abort(rc);
		}

		rc = ec_increment(cont->c_lres, hdl->ch_lre);
		if (rc != 0) {
			D_ERROR(DF_CONT": failed to add new lre: %d\n",
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
	case CONT_EPOCH_SLIP:
		return cont_epoch_slip(pool_hdl, cont, hdl, rpc);
	case CONT_EPOCH_DISCARD:
		return cont_epoch_discard(pool_hdl, cont, hdl, rpc);
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
		rc = dbtree_uv_lookup(cont->c_svc->cs_hdls, in->ci_hdl, &hdl,
				      sizeof(hdl));
		if (rc != 0) {
			if (rc == -DER_NONEXIST) {
				D_ERROR(DF_CONT": rejecting unauthorized "
					"operation: "DF_UUID"\n",
					DP_CONT(cont->c_svc->cs_pool_uuid,
						cont->c_uuid),
					DP_UUID(in->ci_hdl));
				rc = -DER_NO_HDL;
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
	if (opc == CONT_EPOCH_QUERY || opc == CONT_EPOCH_DISCARD)
		ABT_rwlock_rdlock(svc->cs_lock);
	else
		ABT_rwlock_wrlock(svc->cs_lock);

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
	ABT_rwlock_unlock(svc->cs_lock);
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
		D_GOTO(out, rc = -DER_NO_HDL);

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
