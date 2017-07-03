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

#define DD_SUBSYS	DD_FAC(container)

#include <daos_srv/container.h>

#include <daos/rpc.h>
#include <daos_srv/pool.h>
#include <daos_srv/rdb.h>
#include "rpc.h"
#include "srv_internal.h"
#include "srv_layout.h"

static int
cont_svc_init(struct cont_svc **svcp, const uuid_t pool_uuid, uint64_t id,
	      struct rdb *db)
{
	struct cont_svc	       *svc = *svcp;
	int			rc;

	uuid_copy(svc->cs_pool_uuid, pool_uuid);
	svc->cs_id = id;
	svc->cs_in_pool_svc = svcp;
	svc->cs_db = db;

	rc = ABT_rwlock_create(&svc->cs_lock);
	if (rc != ABT_SUCCESS) {
		D_ERROR("failed to create cs_lock: %d\n", rc);
		D_GOTO(err, rc = dss_abterr2der(rc));
	}

	/* cs_root */
	rc = rdb_path_init(&svc->cs_root);
	if (rc != 0)
		D_GOTO(err_lock, rc);
	rc = rdb_path_push(&svc->cs_root, &rdb_path_root_key);
	if (rc != 0)
		D_GOTO(err_root, rc);

	/* cs_conts */
	rc = rdb_path_clone(&svc->cs_root, &svc->cs_conts);
	if (rc != 0)
		D_GOTO(err_root, rc);
	rc = rdb_path_push(&svc->cs_conts, &ds_cont_attr_conts);
	if (rc != 0)
		D_GOTO(err_conts, rc);

	/* cs_hdls */
	rc = rdb_path_clone(&svc->cs_root, &svc->cs_hdls);
	if (rc != 0)
		D_GOTO(err_conts, rc);
	rc = rdb_path_push(&svc->cs_hdls, &ds_cont_attr_cont_handles);
	if (rc != 0)
		D_GOTO(err_hdls, rc);

	return 0;

err_hdls:
	rdb_path_fini(&svc->cs_hdls);
err_conts:
	rdb_path_fini(&svc->cs_conts);
err_root:
	rdb_path_fini(&svc->cs_root);
err_lock:
	ABT_rwlock_free(&svc->cs_lock);
err:
	return rc;
}

static void
cont_svc_fini(struct cont_svc *svc)
{
	rdb_path_fini(&svc->cs_hdls);
	rdb_path_fini(&svc->cs_conts);
	rdb_path_fini(&svc->cs_root);
	ABT_rwlock_free(&svc->cs_lock);
}

static void
cont_svc_step_up(struct cont_svc *svc)
{
	D_ASSERT(svc->cs_pool == NULL);
	svc->cs_pool = ds_pool_lookup(svc->cs_pool_uuid);
	D_ASSERT(svc->cs_pool != NULL);
}

static void
cont_svc_step_down(struct cont_svc *svc)
{
	D_ASSERT(svc->cs_pool != NULL);
	ds_pool_put(svc->cs_pool);
	svc->cs_pool = NULL;
}

int
ds_cont_svc_init(struct cont_svc **svcp, const uuid_t pool_uuid, uint64_t id,
		 struct rdb *db)
{
	int rc;

	D_ALLOC_PTR(*svcp);
	if (*svcp == NULL)
		return -DER_NOMEM;
	rc = cont_svc_init(svcp, pool_uuid, id, db);
	if (rc != 0) {
		D_FREE_PTR(*svcp);
		*svcp = NULL;
		return rc;
	}
	return 0;
}

void
ds_cont_svc_fini(struct cont_svc **svcp)
{
	cont_svc_fini(*svcp);
	D_FREE_PTR(*svcp);
	*svcp = NULL;
}

void
ds_cont_svc_step_up(struct cont_svc *svc)
{
	cont_svc_step_up(svc);
}

void
ds_cont_svc_step_down(struct cont_svc *svc)
{
	cont_svc_step_down(svc);
}

static int
cont_svc_lookup(const uuid_t pool_uuid, uint64_t id, struct cont_svc **svcp)
{
	struct cont_svc	      **p;
	int			rc;

	D_ASSERTF(id == 0, DF_U64"\n", id);
	rc = ds_pool_lookup_cont_svc(pool_uuid, &p);
	if (rc != 0)
		return rc;
	D_ASSERT(p != NULL); /* p == &pool_svc->ps_cont_svc */
	if (*p == NULL) {
		D_ERROR(DF_UUID"["DF_U64"]: container service uninitialized\n",
			DP_UUID(pool_uuid), id);
		return -DER_NONEXIST;
	}
	*svcp = *p;
	return 0;
}

static void
cont_svc_put(struct cont_svc *svc)
{
	ds_pool_put_cont_svc(svc->cs_in_pool_svc);
}

int
ds_cont_bcast_create(crt_context_t ctx, struct cont_svc *svc,
		     crt_opcode_t opcode, crt_rpc_t **rpc)
{
	return ds_pool_bcast_create(ctx, svc->cs_pool, DAOS_CONT_MODULE, opcode,
				    rpc, NULL, NULL);
}

void
ds_cont_wrlock_metadata(struct cont_svc *svc)
{
	ABT_rwlock_wrlock(svc->cs_lock);
}

void
ds_cont_rdlock_metadata(struct cont_svc *svc)
{
	ABT_rwlock_rdlock(svc->cs_lock);
}

void
ds_cont_unlock_metadata(struct cont_svc *svc)
{
	ABT_rwlock_unlock(svc->cs_lock);
}

/**
 * Initialize the container metadata in the combined pool/container service.
 *
 * \param[in]	tx		transaction
 * \param[in]	kvs		root KVS for container metadata
 * \param[in]	pool_uuid	pool UUID
 */
int
ds_cont_init_metadata(struct rdb_tx *tx, const rdb_path_t *kvs,
		      const uuid_t pool_uuid)
{
	struct rdb_kvs_attr	attr;
	int			rc;

	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, kvs, &ds_cont_attr_conts, &attr);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create container KVS: %d\n",
			DP_UUID(pool_uuid), rc);
		return rc;
	}

	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, kvs, &ds_cont_attr_cont_handles, &attr);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create container handle KVS: %d\n",
			DP_UUID(pool_uuid), rc);
		return rc;
	}

	return rc;
}

static int
cont_create(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
	    struct cont_svc *svc, crt_rpc_t *rpc)
{
	struct cont_create_in  *in = crt_req_get(rpc);
	daos_iov_t		key;
	daos_iov_t		value;
	struct rdb_kvs_attr	attr;
	rdb_path_t		kvs;
	uint64_t		ghce = 0;
	uint64_t		ghpce = 0;
	int			rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid), rpc);

	/* Verify the pool handle capabilities. */
	if (!(pool_hdl->sph_capas & DAOS_PC_RW) &&
	    !(pool_hdl->sph_capas & DAOS_PC_EX))
		D_GOTO(out, rc = -DER_NO_PERM);

	/* Check if a container with this UUID already exists. */
	daos_iov_set(&key, in->cci_op.ci_uuid, sizeof(uuid_t));
	daos_iov_set(&value, NULL /* buf */, 0 /* size */);
	rc = rdb_tx_lookup(tx, &svc->cs_conts, &key, &value);
	if (rc != -DER_NONEXIST) {
		if (rc == 0)
			D_DEBUG(DF_DSMS, DF_CONT": container already exists\n",
				DP_CONT(pool_hdl->sph_pool->sp_uuid,
					in->cci_op.ci_uuid));
		D_GOTO(out, rc);
	}

	/*
	 * Target-side creations (i.e., vos_cont_create() calls) are
	 * deferred to the time when the container is first successfully
	 * opened.
	 */

	/* Create the container attribute KVS under the container KVS. */
	daos_iov_set(&key, in->cci_op.ci_uuid, sizeof(uuid_t));
	attr.dsa_class = RDB_KVS_GENERIC;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, &svc->cs_conts, &key, &attr);
	if (rc != 0) {
		D_ERROR("failed to create container attribute KVS: "
			"%d\n", rc);
		D_GOTO(out, rc);
	}

	/* Create a path to the container attribute KVS. */
	rc = rdb_path_clone(&svc->cs_conts, &kvs);
	if (rc != 0)
		D_GOTO(out, rc);
	rc = rdb_path_push(&kvs, &key);
	if (rc != 0)
		D_GOTO(out_kvs, rc);

	/* Create the GHCE and GHPCE attributes. */
	daos_iov_set(&value, &ghce, sizeof(ghce));
	rc = rdb_tx_update(tx, &kvs, &ds_cont_attr_ghce, &value);
	if (rc != 0)
		D_GOTO(out_kvs, rc);
	daos_iov_set(&value, &ghpce, sizeof(ghpce));
	rc = rdb_tx_update(tx, &kvs, &ds_cont_attr_ghpce, &value);
	if (rc != 0)
		D_GOTO(out_kvs, rc);

	/* Create the LRE and LHE KVSs. */
	attr.dsa_class = RDB_KVS_INTEGER;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, &kvs, &ds_cont_attr_lres, &attr);
	if (rc != 0)
		D_GOTO(out_kvs, rc);
	rc = rdb_tx_create_kvs(tx, &kvs, &ds_cont_attr_lhes, &attr);
	if (rc != 0)
		D_GOTO(out_kvs, rc);

	/* Create the snapshot KVS. */
	attr.dsa_class = RDB_KVS_INTEGER;
	attr.dsa_order = 16;
	rc = rdb_tx_create_kvs(tx, &kvs, &ds_cont_attr_snapshots, &attr);
	D_EXIT;
out_kvs:
	rdb_path_fini(&kvs);
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

	rc = ds_cont_bcast_create(ctx, svc, CONT_TGT_DESTROY, &rpc);
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
cont_destroy(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
	     struct cont_svc *svc, crt_rpc_t *rpc)
{
	struct cont_destroy_in *in = crt_req_get(rpc);
	daos_iov_t		key;
	daos_iov_t		value;
	rdb_path_t		kvs;
	int			rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: force=%u\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cdi_op.ci_uuid), rpc,
		in->cdi_force);

	/* Verify the pool handle capabilities. */
	if (!(pool_hdl->sph_capas & DAOS_PC_RW) &&
	    !(pool_hdl->sph_capas & DAOS_PC_EX))
		D_GOTO(out, rc = -DER_NO_PERM);

	/* Check if the container attribute KVS exists. */
	daos_iov_set(&key, in->cdi_op.ci_uuid, sizeof(uuid_t));
	daos_iov_set(&value, NULL /* buf */, 0 /* size */);
	rc = rdb_tx_lookup(tx, &svc->cs_conts, &key, &value);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			rc = 0;
		D_GOTO(out, rc);
	}

	/* Create a path to the container attribute KVS. */
	rc = rdb_path_clone(&svc->cs_conts, &kvs);
	if (rc != 0)
		D_GOTO(out, rc);
	rc = rdb_path_push(&kvs, &key);
	if (rc != 0)
		D_GOTO(out_kvs, rc);

	rc = cont_destroy_bcast(rpc->cr_ctx, svc, in->cdi_op.ci_uuid);
	if (rc != 0)
		D_GOTO(out_kvs, rc);

	/* Destroy the snapshot KVS. */
	rc = rdb_tx_destroy_kvs(tx, &kvs, &ds_cont_attr_snapshots);
	if (rc != 0)
		D_GOTO(out_kvs, rc);

	/* Destroy the LHE and LRE KVSs. */
	rc = rdb_tx_destroy_kvs(tx, &kvs, &ds_cont_attr_lhes);
	if (rc != 0)
		D_GOTO(out_kvs, rc);
	rc = rdb_tx_destroy_kvs(tx, &kvs, &ds_cont_attr_lres);
	if (rc != 0)
		D_GOTO(out_kvs, rc);

	/* Destroy the container attribute KVS. */
	rc = rdb_tx_destroy_kvs(tx, &svc->cs_conts, &key);

	D_EXIT;
out_kvs:
	rdb_path_fini(&kvs);
out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cdi_op.ci_uuid), rpc,
		rc);
	return rc;
}

static int
cont_lookup(struct rdb_tx *tx, const struct cont_svc *svc, const uuid_t uuid,
	    struct cont **cont)
{
	struct cont    *p;
	daos_iov_t	key;
	daos_iov_t	tmp;
	int		rc;

	daos_iov_set(&key, (void *)uuid, sizeof(uuid_t));
	daos_iov_set(&tmp, NULL, 0);
	/* check if the container exists or not */
	rc = rdb_tx_lookup(tx, &svc->cs_conts, &key, &tmp);
	if (rc != 0)
		D_GOTO(err, rc);

	D_ALLOC_PTR(p);
	if (p == NULL) {
		D_ERROR("Failed to allocate container descriptor\n");
		D_GOTO(err, rc = -DER_NOMEM);
	}

	uuid_copy(p->c_uuid, uuid);
	p->c_svc = (struct cont_svc *)svc;

	/* c_attrs */
	rc = rdb_path_clone(&svc->cs_conts, &p->c_attrs);
	if (rc != 0)
		D_GOTO(err_p, rc);

	rc = rdb_path_push(&p->c_attrs, &key);
	if (rc != 0)
		D_GOTO(err_attrs, rc);

	/* c_lres */
	rc = rdb_path_clone(&p->c_attrs, &p->c_lres);
	if (rc != 0)
		D_GOTO(err_attrs, rc);
	rc = rdb_path_push(&p->c_lres, &ds_cont_attr_lres);
	if (rc != 0)
		D_GOTO(err_lres, rc);

	/* c_lhes */
	rc = rdb_path_clone(&p->c_attrs, &p->c_lhes);
	if (rc != 0)
		D_GOTO(err_lres, rc);
	rc = rdb_path_push(&p->c_lhes, &ds_cont_attr_lhes);
	if (rc != 0)
		D_GOTO(err_lhes, rc);

	*cont = p;
	D_EXIT;
	return 0;

err_lhes:
	rdb_path_fini(&p->c_lhes);
err_lres:
	rdb_path_fini(&p->c_lres);
err_attrs:
	rdb_path_fini(&p->c_attrs);
err_p:
	D_FREE_PTR(p);
err:
	return rc;
}

static void
cont_put(struct cont *cont)
{
	rdb_path_fini(&cont->c_lhes);
	rdb_path_fini(&cont->c_lres);
	rdb_path_fini(&cont->c_attrs);
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

	rc = ds_cont_bcast_create(ctx, cont->c_svc, CONT_TGT_OPEN, &rpc);
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
cont_open(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl, struct cont *cont,
	  crt_rpc_t *rpc)
{
	struct cont_open_in    *in = crt_req_get(rpc);
	struct cont_open_out   *out = crt_reply_get(rpc);
	daos_iov_t		key;
	daos_iov_t		value;
	struct container_hdl	chdl;
	int			rc;

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
	daos_iov_set(&key, in->coi_op.ci_hdl, sizeof(uuid_t));
	daos_iov_set(&value, &chdl, sizeof(chdl));
	rc = rdb_tx_lookup(tx, &cont->c_svc->cs_hdls, &key, &value);
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

	uuid_copy(chdl.ch_pool_hdl, pool_hdl->sph_uuid);
	uuid_copy(chdl.ch_cont, cont->c_uuid);
	chdl.ch_capas = in->coi_capas;

	rc = ds_cont_epoch_init_hdl(tx, cont, &chdl, &out->coo_epoch_state);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = rdb_tx_update(tx, &cont->c_svc->cs_hdls, &key, &value);
	if (rc != 0)
		memset(&out->coo_epoch_state, 0, sizeof(out->coo_epoch_state));

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

	rc = ds_cont_bcast_create(ctx, svc, CONT_TGT_CLOSE, &rpc);
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

static int
cont_close_one_hdl(struct rdb_tx *tx, struct cont_svc *svc, const uuid_t uuid)
{
	daos_iov_t		key;
	daos_iov_t		value;
	struct container_hdl	chdl;
	struct cont	       *cont;
	int			rc;

	/* Look up the handle. */
	daos_iov_set(&key, (void *)uuid, sizeof(uuid_t));
	daos_iov_set(&value, &chdl, sizeof(chdl));
	rc = rdb_tx_lookup(tx, &svc->cs_hdls, &key, &value);
	if (rc != 0)
		return rc;

	rc = cont_lookup(tx, svc, chdl.ch_cont, &cont);
	if (rc != 0)
		return rc;

	rc = ds_cont_epoch_fini_hdl(tx, cont, &chdl);
	cont_put(cont);
	cont = NULL;
	if (rc != 0)
		return rc;

	/* Delete this handle. */
	return rdb_tx_delete(tx, &svc->cs_hdls, &key);
}

/* Close an array of handles, possibly belonging to different containers. */
static int
cont_close_hdls(struct cont_svc *svc, struct cont_tgt_close_rec *recs,
		int nrecs, crt_context_t ctx)
{
	int	i;
	int	rc;

	D_ASSERTF(nrecs > 0, "%d\n", nrecs);
	D_DEBUG(DF_DSMS, DF_CONT": closing %d recs: recs[0].hdl="DF_UUID
		" recs[0].hce="DF_U64"\n", DP_CONT(svc->cs_pool_uuid, NULL),
		nrecs, DP_UUID(recs[0].tcr_hdl), recs[0].tcr_hce);

	rc = cont_close_bcast(ctx, svc, recs, nrecs);
	if (rc != 0)
		D_GOTO(out, rc);

	/*
	 * Use one TX per handle to avoid calling ds_cont_epoch_fini_hdl() more
	 * than once in a TX, in which case we would be attempting to query
	 * uncommitted updates. This could be optimized by adding container
	 * UUIDs into recs[i] and sorting recs[] by container UUIDs. Then we
	 * could maintain a list of deleted LREs and a list of deleted LHEs for
	 * each container while looping, and use the lists to update the GHCE
	 * once for each container. This approach enables us to commit only
	 * once (or when a TX becomes too big).
	 */
	for (i = 0; i < nrecs; i++) {
		struct rdb_tx tx;

		rc = rdb_tx_begin(svc->cs_db, &tx);
		if (rc != 0)
			break;
		rc = cont_close_one_hdl(&tx, svc, recs[i].tcr_hdl);
		if (rc != 0) {
			rdb_tx_end(&tx);
			break;
		}
		rc = rdb_tx_commit(&tx);
		rdb_tx_end(&tx);
		if (rc != 0)
			break;
	}

out:
	D_DEBUG(DF_DSMS, DF_CONT": leaving: %d\n",
		DP_CONT(svc->cs_pool_uuid, NULL), rc);
	return rc;
}

static int
cont_close(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl, struct cont *cont,
	   crt_rpc_t *rpc)
{
	struct cont_close_in	       *in = crt_req_get(rpc);
	daos_iov_t			key;
	daos_iov_t			value;
	struct container_hdl		chdl;
	struct cont_tgt_close_rec	rec;
	int				rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid), rpc,
		DP_UUID(in->cci_op.ci_hdl));

	/* See if this container handle is already closed. */
	daos_iov_set(&key, in->cci_op.ci_hdl, sizeof(uuid_t));
	daos_iov_set(&value, &chdl, sizeof(chdl));
	rc = rdb_tx_lookup(tx, &cont->c_svc->cs_hdls, &key, &value);
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

	D_DEBUG(DF_DSMS, DF_CONT": closing: hdl="DF_UUID" hce="DF_U64"\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, in->cci_op.ci_uuid),
		DP_UUID(rec.tcr_hdl), rec.tcr_hce);

	rc = cont_close_bcast(rpc->cr_ctx, cont->c_svc, &rec, 1 /* nrecs */);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = cont_close_one_hdl(tx, cont->c_svc, rec.tcr_hdl);

out:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: %d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cci_op.ci_uuid), rpc,
		rc);
	return rc;
}

static int
cont_query_bcast(crt_context_t ctx, struct cont *cont, const uuid_t pool_hdl,
		 const uuid_t cont_hdl, struct cont_query_out *query_out)
{
	struct	cont_tgt_query_in	*in;
	struct  cont_tgt_query_out	*out;
	crt_rpc_t			*rpc;
	int				 rc;

	D_DEBUG(DF_DSMS,
		DF_CONT"bcasting pool_hld="DF_UUID" cont_hdl ="DF_UUID"\n",
		DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid),
		DP_UUID(pool_hdl), DP_UUID(cont_hdl));

	rc = ds_cont_bcast_create(ctx, cont->c_svc, CONT_TGT_QUERY, &rpc);
	if (rc != 0)
		D_GOTO(out, rc);

	in = crt_req_get(rpc);
	uuid_copy(in->tqi_pool_uuid, pool_hdl);
	uuid_copy(in->tqi_cont_uuid, cont->c_uuid);
	out = crt_reply_get(rpc);
	out->tqo_min_purged_epoch = DAOS_EPOCH_MAX;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	out = crt_reply_get(rpc);
	rc  = out->tqo_rc;
	if (rc != 0) {
		D_DEBUG(DF_DSMS, DF_CONT": failed to query %d targets\n",
			DP_CONT(cont->c_svc->cs_pool_uuid, cont->c_uuid), rc);
		D_GOTO(out_rpc, rc = -DER_IO);
	}

	query_out->cqo_min_slipped_epoch = out->tqo_min_purged_epoch;
	D_EXIT;
out_rpc:
	crt_req_decref(rpc);
out:
	return rc;
}

static int
cont_query(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl, struct cont *cont,
	   struct container_hdl *hdl, crt_rpc_t *rpc)
{
	struct cont_query_in   *in  = crt_req_get(rpc);
	struct cont_query_out  *out = crt_reply_get(rpc);
	int			rc;

	D_DEBUG(DF_DSMS, DF_CONT": processing rpc %p: hdl="DF_UUID"\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->cqi_op.ci_uuid), rpc,
		DP_UUID(in->cqi_op.ci_hdl));

	rc = cont_query_bcast(rpc->cr_ctx, cont, in->cqi_op.ci_pool_hdl,
			      in->cqi_op.ci_hdl, out);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = ds_cont_epoch_read_state(tx, cont, hdl, &out->cqo_epoch_state);
	if (rc != 0)
		D_GOTO(out, rc);
	D_EXIT;
out:
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
close_iter_cb(daos_handle_t ih, daos_iov_t *key, daos_iov_t *val, void *varg)
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
find_hdls_to_close(struct rdb_tx *tx, struct cont_svc *svc, uuid_t *pool_hdls,
		   int n_pool_hdls, struct cont_tgt_close_rec **recs,
		   size_t *recs_size, int *nrecs)
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

	rc = rdb_tx_iterate(tx, &svc->cs_hdls, false /* !backward */,
			    close_iter_cb, &arg);
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
	struct rdb_tx			tx;
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
	if (!ds_pool_is_cont_svc_up(svc->cs_in_pool_svc))
		D_GOTO(out_svc, rc = -DER_NOTLEADER);

	rc = rdb_tx_begin(svc->cs_db, &tx);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	ABT_rwlock_wrlock(svc->cs_lock);

	rc = find_hdls_to_close(&tx, svc, pool_hdls, n_pool_hdls, &recs,
				&recs_size, &nrecs);
	if (rc != 0)
		D_GOTO(out_lock, rc);

	if (nrecs > 0)
		rc = cont_close_hdls(svc, recs, nrecs, ctx);

	D_FREE(recs, recs_size);
out_lock:
	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);
out_svc:
	cont_svc_put(svc);
	return rc;
}

static int
cont_op_with_hdl(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		 struct cont *cont, struct container_hdl *hdl, crt_rpc_t *rpc)
{
	switch (opc_get(rpc->cr_opc)) {
	case CONT_QUERY:
		return cont_query(tx, pool_hdl, cont, hdl, rpc);
	case CONT_EPOCH_QUERY:
		return ds_cont_epoch_query(tx, pool_hdl, cont, hdl, rpc);
	case CONT_EPOCH_HOLD:
		return ds_cont_epoch_hold(tx, pool_hdl, cont, hdl, rpc);
	case CONT_EPOCH_SLIP:
		return ds_cont_epoch_slip(tx, pool_hdl, cont, hdl, rpc);
	case CONT_EPOCH_DISCARD:
		return ds_cont_epoch_discard(tx, pool_hdl, cont, hdl, rpc);
	case CONT_EPOCH_COMMIT:
		return ds_cont_epoch_commit(tx, pool_hdl, cont, hdl, rpc);
	default:
		D_ASSERT(0);
	}
}

/*
 * Look up the container handle, or if the RPC does not need this, call the
 * final handler.
 */
static int
cont_op_with_cont(struct rdb_tx *tx, struct ds_pool_hdl *pool_hdl,
		  struct cont *cont, crt_rpc_t *rpc)
{
	struct cont_op_in      *in = crt_req_get(rpc);
	daos_iov_t		key;
	daos_iov_t		value;
	struct container_hdl	hdl;
	int			rc;

	switch (opc_get(rpc->cr_opc)) {
	case CONT_OPEN:
		rc = cont_open(tx, pool_hdl, cont, rpc);
		break;
	case CONT_CLOSE:
		rc = cont_close(tx, pool_hdl, cont, rpc);
		break;
	default:
		/* Look up the container handle. */
		daos_iov_set(&key, in->ci_hdl, sizeof(uuid_t));
		daos_iov_set(&value, &hdl, sizeof(hdl));
		rc = rdb_tx_lookup(tx, &cont->c_svc->cs_hdls, &key, &value);
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
		rc = cont_op_with_hdl(tx, pool_hdl, cont, &hdl, rpc);
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
	struct rdb_tx		tx;
	crt_opcode_t		opc = opc_get(rpc->cr_opc);
	struct cont	       *cont = NULL;
	int			rc;

	rc = rdb_tx_begin(svc->cs_db, &tx);
	if (rc != 0)
		D_GOTO(out, rc);

	/* TODO: Implement per-container locking. */
	if (opc == CONT_EPOCH_QUERY || opc == CONT_QUERY ||
	    opc == CONT_EPOCH_DISCARD)
		ABT_rwlock_rdlock(svc->cs_lock);
	else
		ABT_rwlock_wrlock(svc->cs_lock);

	switch (opc) {
	case CONT_CREATE:
		rc = cont_create(&tx, pool_hdl, svc, rpc);
		break;
	case CONT_DESTROY:
		rc = cont_destroy(&tx, pool_hdl, svc, rpc);
		break;
	default:
		rc = cont_lookup(&tx, svc, in->ci_uuid, &cont);
		if (rc != 0)
			D_GOTO(out_lock, rc);
		rc = cont_op_with_cont(&tx, pool_hdl, cont, rpc);
		cont_put(cont);
	}
	if (rc != 0)
		D_GOTO(out_lock, rc);

	rc = rdb_tx_commit(&tx);
	D_EXIT;
out_lock:
	ABT_rwlock_unlock(svc->cs_lock);
	rdb_tx_end(&tx);
out:
	return rc;
}

static void
set_rsvc_hint(struct rdb *db, struct cont_op_out *out)
{
	int rc;

	rc = rdb_get_leader(db, &out->co_hint.sh_term, &out->co_hint.sh_rank);
	if (rc != 0)
		return;
	out->co_hint.sh_flags |= RSVC_HINT_VALID;
}

/* Look up the pool handle and the matching container service. */
void
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
	if (!ds_pool_is_cont_svc_up(svc->cs_in_pool_svc))
		D_GOTO(out_svc, rc = -DER_NOTLEADER);

	rc = cont_op_with_svc(pool_hdl, svc, rpc);

out_svc:
	set_rsvc_hint(svc->cs_db, out);
	cont_svc_put(svc);
out_pool_hdl:
	D_DEBUG(DF_DSMS, DF_CONT": replying rpc %p: hdl="DF_UUID
		" opc=%u rc=%d\n",
		DP_CONT(pool_hdl->sph_pool->sp_uuid, in->ci_uuid), rpc,
		DP_UUID(in->ci_hdl), opc, rc);
	ds_pool_hdl_put(pool_hdl);
out:
	out->co_rc = rc;
	crt_reply_send(rpc);

	return;
}
