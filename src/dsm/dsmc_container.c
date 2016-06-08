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
 * dsmc: Container Methods
 */

#include <daos_m.h>

#include "dsm_rpc.h"
#include "dsmc_internal.h"

static int
cont_create_complete(struct daos_op_sp *sp, daos_event_t *ev, int rc)
{
	struct cont_create_out *out;

	if (rc != 0) {
		D_ERROR("RPC error while creating container: %d\n", rc);
		D_GOTO(out, rc);
	}

	out = dtp_reply_get(sp->sp_rpc);

	rc = out->cco_ret;
	if (rc != 0) {
		D_ERROR("failed to create container: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, "completed creating container\n");

out:
	dtp_req_decref(sp->sp_rpc);
	return rc;
}

int
dsm_co_create(daos_handle_t poh, const uuid_t uuid, daos_event_t *ev)
{
	struct cont_create_in  *in;
	struct dsmc_pool       *pool;
	dtp_endpoint_t		ep;
	dtp_rpc_t	       *rpc;
	struct daos_op_sp      *sp;
	int			rc;

	if (uuid_is_null(uuid))
		return -DER_INVAL;

	pool = dsmc_handle2pool(poh);
	if (pool == NULL)
		return -DER_NO_HDL;

	if (!(pool->dp_capas & DAOS_PC_RW) && !(pool->dp_capas & DAOS_PC_EX)) {
		dsmc_pool_put(pool);
		return -DER_NO_PERM;
	}

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0) {
			dsmc_pool_put(pool);
			return rc;
		}
	}

	D_DEBUG(DF_DSMC, DF_UUID"\n", DP_UUID(uuid));

	/* To the only container service. */
	uuid_clear(ep.ep_grp_id);
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	rc = dsm_req_create(daos_ev2ctx(ev), ep, DSM_CONT_CREATE, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		dsmc_pool_put(pool);
		return rc;
	}

	in = dtp_req_get(rpc);
	uuid_copy(in->cci_pool, pool->dp_pool);
	uuid_copy(in->cci_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(in->cci_cont, uuid);

	dsmc_pool_put(pool);

	sp = daos_ev2sp(ev);
	dtp_req_addref(rpc);
	sp->sp_rpc = rpc;

	rc = daos_event_launch(ev, NULL /* abort_cb */, cont_create_complete);
	if (rc != 0) {
		dtp_req_decref(rpc);
		dtp_req_decref(rpc);
		return rc;
	}

	return daos_rpc_send(rpc, ev);
}

static int
cont_destroy_complete(struct daos_op_sp *sp, daos_event_t *ev, int rc)
{
	struct cont_destroy_out *out;

	if (rc != 0) {
		D_ERROR("RPC error while destroying container: %d\n", rc);
		D_GOTO(out, rc);
	}

	out = dtp_reply_get(sp->sp_rpc);

	rc = out->cdo_ret;
	if (rc != 0) {
		D_ERROR("failed to destroy container: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, "completed destroying container\n");

out:
	dtp_req_decref(sp->sp_rpc);
	return rc;
}

int
dsm_co_destroy(daos_handle_t poh, const uuid_t uuid, int force,
	       daos_event_t *ev)
{
	struct cont_destroy_in *in;
	struct dsmc_pool       *pool;
	dtp_endpoint_t		ep;
	dtp_rpc_t	       *rpc;
	struct daos_op_sp      *sp;
	int			rc;

	/* TODO: Implement "force". */
	D_ASSERT(force != 0);

	if (uuid_is_null(uuid))
		return -DER_INVAL;

	pool = dsmc_handle2pool(poh);
	if (pool == NULL)
		return -DER_NO_HDL;

	if (!(pool->dp_capas & DAOS_PC_RW) && !(pool->dp_capas & DAOS_PC_EX)) {
		dsmc_pool_put(pool);
		return -DER_NO_PERM;
	}

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0) {
			dsmc_pool_put(pool);
			return rc;
		}
	}

	D_DEBUG(DF_DSMC, DF_UUID" force=%d\n", DP_UUID(uuid), force);

	/* To the only container service. */
	uuid_clear(ep.ep_grp_id);
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	rc = dsm_req_create(daos_ev2ctx(ev), ep, DSM_CONT_DESTROY, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		dsmc_pool_put(pool);
		return rc;
	}

	in = dtp_req_get(rpc);
	uuid_copy(in->cdi_pool, pool->dp_pool);
	uuid_copy(in->cdi_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(in->cdi_cont, uuid);
	in->cdi_force = force;

	dsmc_pool_put(pool);

	sp = daos_ev2sp(ev);
	dtp_req_addref(rpc);
	sp->sp_rpc = rpc;

	rc = daos_event_launch(ev, NULL /* abort_cb */, cont_destroy_complete);
	if (rc != 0) {
		dtp_req_decref(rpc);
		dtp_req_decref(rpc);
		return rc;
	}

	return daos_rpc_send(rpc, ev);
}

static void
dsmc_container_free(struct daos_hlink *dlink)
{
	struct dsmc_container *dc;

	dc = container_of(dlink, struct dsmc_container, dc_hlink);
	pthread_rwlock_destroy(&dc->dc_obj_list_lock);
	D_ASSERT(daos_list_empty(&dc->dc_po_list));
	D_ASSERT(daos_list_empty(&dc->dc_obj_list));
	D_FREE_PTR(dc);
}


static struct daos_hlink_ops dc_h_ops = {
	.hop_free = dsmc_container_free,
};

static struct dsmc_container *
dsmc_container_alloc(const uuid_t uuid)
{
	struct dsmc_container *dc;

	D_ALLOC_PTR(dc);
	if (dc == NULL)
		return NULL;

	uuid_copy(dc->dc_uuid, uuid);
	DAOS_INIT_LIST_HEAD(&dc->dc_obj_list);
	DAOS_INIT_LIST_HEAD(&dc->dc_po_list);
	pthread_rwlock_init(&dc->dc_obj_list_lock, NULL);

	daos_hhash_hlink_init(&dc->dc_hlink, &dc_h_ops);
	return dc;
}

struct cont_open_arg {
	struct dsmc_pool       *coa_pool;
	struct dsmc_container  *coa_cont;
	daos_co_info_t	       *coa_info;
};

static int
cont_open_complete(struct daos_op_sp *sp, daos_event_t *ev, int rc)
{
	struct cont_open_out   *out;
	struct cont_open_arg   *arg = sp->sp_arg;
	struct dsmc_pool       *pool = arg->coa_pool;
	struct dsmc_container  *cont = arg->coa_cont;

	if (rc != 0) {
		D_ERROR("RPC error while opening container: %d\n", rc);
		D_GOTO(out, rc);
	}

	out = dtp_reply_get(sp->sp_rpc);

	rc = out->coo_ret;
	if (rc != 0) {
		D_ERROR("failed to open container: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, "completed opening container\n");

	pthread_rwlock_wrlock(&pool->dp_co_list_lock);
	if (pool->dp_disconnecting) {
		pthread_rwlock_unlock(&pool->dp_co_list_lock);
		D_ERROR("pool connection being invalidated\n");
		/*
		 * Instead of sending a DSM_CONT_CLOSE RPC, we leave this new
		 * container handle on the server side to the
		 * DSM_POOL_DISCONNECT effort we are racing with.
		 */
		D_GOTO(out, rc = -DER_NO_HDL);
	}
	daos_list_add(&cont->dc_po_list, &pool->dp_co_list);
	cont->dc_pool_hdl = sp->sp_hdl;
	pthread_rwlock_unlock(&pool->dp_co_list_lock);

	dsmc_container_add_cache(cont, sp->sp_hdlp);

	if (arg->coa_info == NULL)
		D_GOTO(out, rc = 0);

	uuid_copy(arg->coa_info->ci_uuid, cont->dc_uuid);
	arg->coa_info->ci_epoch_state = out->coo_epoch_state;
	/* TODO */
	arg->coa_info->ci_nsnapshots = 0;
	arg->coa_info->ci_snapshots = NULL;

out:
	dtp_req_decref(sp->sp_rpc);
	D_FREE_PTR(arg);
	dsmc_container_put(cont);
	dsmc_pool_put(pool);
	return rc;
}

int
dsm_co_open(daos_handle_t poh, const uuid_t uuid, unsigned int flags,
	    daos_rank_list_t *failed, daos_handle_t *coh, daos_co_info_t *info,
	    daos_event_t *ev)
{
	struct cont_open_in    *in;
	struct dsmc_pool       *pool;
	struct dsmc_container  *cont;
	dtp_endpoint_t		ep;
	dtp_rpc_t	       *rpc;
	struct daos_op_sp      *sp;
	struct cont_open_arg   *arg;
	int			rc;

	D_ASSERT(failed == NULL);	/* TODO */

	if (uuid_is_null(uuid) || coh == NULL)
		D_GOTO(err, rc = -DER_INVAL);

	pool = dsmc_handle2pool(poh);
	if (pool == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	if ((flags & DAOS_COO_RW) && (pool->dp_capas & DAOS_PC_RO))
		D_GOTO(err_pool, rc = -DER_NO_PERM);

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			D_GOTO(err_pool, rc);
	}

	D_DEBUG(DF_DSMC, DF_UUID"\n", DP_UUID(uuid));

	cont = dsmc_container_alloc(uuid);
	if (cont == NULL)
		D_GOTO(err_pool, rc = -DER_NOMEM);

	uuid_generate(cont->dc_cont_hdl);
	cont->dc_capas = flags;

	D_ALLOC_PTR(arg);
	if (arg == NULL) {
		D_ERROR("failed to allocate container open arg");
		D_GOTO(err_cont, rc = -DER_NOMEM);
	}

	arg->coa_pool = pool;
	arg->coa_cont = cont;
	arg->coa_info = info;

	/* To the only container service. */
	uuid_clear(ep.ep_grp_id);
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	rc = dsm_req_create(daos_ev2ctx(ev), ep, DSM_CONT_OPEN, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		D_GOTO(err_arg, rc);
	}

	in = dtp_req_get(rpc);
	uuid_copy(in->coi_pool, pool->dp_pool);
	uuid_copy(in->coi_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(in->coi_cont, uuid);
	uuid_copy(in->coi_cont_hdl, cont->dc_cont_hdl);
	in->coi_capas = flags;

	sp = daos_ev2sp(ev);
	dtp_req_addref(rpc);
	sp->sp_rpc = rpc;
	sp->sp_hdl = poh;
	sp->sp_hdlp = coh;
	sp->sp_arg = arg;

	rc = daos_event_launch(ev, NULL /* abort_cb */, cont_open_complete);
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, ev);

err_rpc:
	dtp_req_decref(rpc);
	dtp_req_decref(rpc);
err_arg:
	D_FREE_PTR(arg);
err_cont:
	dsmc_container_put(cont);
err_pool:
	dsmc_pool_put(pool);
err:
	D_DEBUG(DF_DSMC, "failed to open container: %d\n", rc);
	return rc;
}

struct cont_close_arg {
	struct dsmc_pool       *cca_pool;
	struct dsmc_container  *cca_cont;
};

static int
cont_close_complete(struct daos_op_sp *sp, daos_event_t *ev, int rc)
{
	struct cont_close_out  *out;
	struct cont_close_arg  *arg = sp->sp_arg;
	struct dsmc_pool       *pool = arg->cca_pool;
	struct dsmc_container  *cont = arg->cca_cont;

	if (rc != 0) {
		D_ERROR("RPC error while closing container: %d\n", rc);
		D_GOTO(out, rc);
	}

	out = dtp_reply_get(sp->sp_rpc);

	rc = out->cco_ret;
	if (rc != 0) {
		D_ERROR("failed to close container: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, "completed closing container (on master)\n");

	dsmc_container_del_cache(cont);

	/* Remove the container from pool container list */
	pthread_rwlock_wrlock(&pool->dp_co_list_lock);
	daos_list_del_init(&cont->dc_po_list);
	pthread_rwlock_unlock(&pool->dp_co_list_lock);

out:
	dtp_req_decref(sp->sp_rpc);
	dsmc_pool_put(pool);
	dsmc_container_put(cont);
	return rc;
}

int
dsm_co_close(daos_handle_t coh, daos_event_t *ev)
{
	struct cont_close_in   *in;
	struct dsmc_pool       *pool;
	struct dsmc_container  *cont;
	dtp_endpoint_t		ep;
	dtp_rpc_t	       *rpc;
	struct daos_op_sp      *sp;
	struct cont_close_arg  *arg;
	int			rc;

	cont = dsmc_handle2container(coh);
	if (cont == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	/* Check if there are not objects opened for this container */
	pthread_rwlock_rdlock(&cont->dc_obj_list_lock);
	if (!daos_list_empty(&cont->dc_obj_list)) {
		D_ERROR("cannot close container, object not closed.\n");
		pthread_rwlock_unlock(&cont->dc_obj_list_lock);
		D_GOTO(err_cont, rc = -DER_BUSY);
	}
	cont->dc_closing = 1;
	pthread_rwlock_unlock(&cont->dc_obj_list_lock);

	pool = dsmc_handle2pool(cont->dc_pool_hdl);
	D_ASSERT(pool != NULL);

	D_DEBUG(DF_DSMC, DF_UUID"/"DF_UUID": "DF_UUID"\n",
		DP_UUID(pool->dp_pool), DP_UUID(cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

	if (cont->dc_slave) {
		dsmc_container_del_cache(cont);

		/* Remove the container from pool container list */
		pthread_rwlock_wrlock(&pool->dp_co_list_lock);
		daos_list_del_init(&cont->dc_po_list);
		pthread_rwlock_unlock(&pool->dp_co_list_lock);

		dsmc_pool_put(pool);
		dsmc_container_put(cont);

		if (ev != NULL) {
			daos_event_launch(ev, NULL, NULL);
			daos_event_complete(ev, 0);
		}
		D_DEBUG(DF_DSMC, "completed closing container (on slave)\n");
		return 0;
	}

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			D_GOTO(err_pool, rc);
	}

	D_ALLOC_PTR(arg);
	if (arg == NULL) {
		D_ERROR("failed to allocate container close arg");
		D_GOTO(err_pool, rc = -DER_NOMEM);
	}

	arg->cca_pool = pool;
	arg->cca_cont = cont;

	/* To the only container service. */
	uuid_clear(ep.ep_grp_id);
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	rc = dsm_req_create(daos_ev2ctx(ev), ep, DSM_CONT_CLOSE, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		D_GOTO(err_arg, rc);
	}

	in = dtp_req_get(rpc);
	uuid_copy(in->cci_pool, pool->dp_pool);
	uuid_copy(in->cci_cont, cont->dc_uuid);
	uuid_copy(in->cci_cont_hdl, cont->dc_cont_hdl);

	sp = daos_ev2sp(ev);
	dtp_req_addref(rpc);
	sp->sp_rpc = rpc;
	sp->sp_arg = arg;

	rc = daos_event_launch(ev, NULL /* abort_cb */, cont_close_complete);
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, ev);

err_rpc:
	dtp_req_decref(rpc);
	dtp_req_decref(rpc);
err_arg:
	D_FREE_PTR(arg);
err_pool:
	dsmc_pool_put(pool);
err_cont:
	dsmc_container_put(cont);
err:
	D_DEBUG(DF_DSMC, "failed to close container handle "DF_X64": %d\n",
		coh.cookie, rc);
	return rc;
}

static inline void
dsmc_swap_co_glob(struct dsmc_container_glob *cont_glob)
{
	D_ASSERT(cont_glob != NULL);

	D_SWAP32S(&cont_glob->dcg_header.hgh_magic);
	D_SWAP32S(&cont_glob->dcg_header.hgh_type);
	/* skip cont_glob->dcg_pool_hdl (uuid_t) */
	/* skip cont_glob->dcg_uuid (uuid_t) */
	/* skip cont_glob->dcg_cont_hdl (uuid_t) */
	D_SWAP64S(&cont_glob->dcg_capas);
}

int
dsmc_co_l2g(daos_handle_t coh, daos_iov_t *glob)
{
	struct dsmc_pool		*pool;
	struct dsmc_container		*cont;
	struct dsmc_container_glob	*cont_glob;
	daos_size_t			 glob_buf_size;
	int				 rc = 0;

	D_ASSERT(glob != NULL);

	cont = dsmc_handle2container(coh);
	if (cont == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	glob_buf_size = dsmc_container_glob_buf_size();
	if (glob->iov_buf == NULL) {
		glob->iov_buf_len = glob_buf_size;
		D_GOTO(out_cont, rc = 0);
	}
	if (glob->iov_buf_len < glob_buf_size) {
		D_DEBUG(DF_DSMC, "Larger glob buffer needed ("DF_U64" bytes "
			"provided, "DF_U64" required).\n", glob->iov_buf_len,
			glob_buf_size);
		glob->iov_buf_len = glob_buf_size;
		D_GOTO(out_cont, rc = -DER_TRUNC);
	}
	glob->iov_len = glob_buf_size;

	pool = dsmc_handle2pool(cont->dc_pool_hdl);
	if (pool == NULL)
		D_GOTO(out_cont, rc = -DER_NO_HDL);

	/* init global handle */
	cont_glob = (struct dsmc_container_glob *)glob->iov_buf;
	dsmc_hdl_glob_hdr_init(&cont_glob->dcg_header, DSMC_GLOB_CO);
	uuid_copy(cont_glob->dcg_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(cont_glob->dcg_uuid, cont->dc_uuid);
	uuid_copy(cont_glob->dcg_cont_hdl, cont->dc_cont_hdl);
	cont_glob->dcg_capas = cont->dc_capas;

	dsmc_pool_put(pool);

out_cont:
	dsmc_container_put(cont);
out:
	if (rc)
		D_ERROR("dsm_co_l2g failed, rc: %d\n", rc);
	return rc;
}

int
dsm_co_local2global(daos_handle_t coh, daos_iov_t *glob)
{
	int	rc = 0;

	if (glob == NULL) {
		D_ERROR("Invalid parameter, NULL glob pointer.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (glob->iov_buf != NULL && (glob->iov_buf_len == 0 ||
	    glob->iov_len == 0 || glob->iov_buf_len < glob->iov_len)) {
		D_ERROR("Invalid parameter of glob, iov_buf %p, iov_buf_len "
			""DF_U64", iov_len "DF_U64".\n", glob->iov_buf,
			glob->iov_buf_len, glob->iov_len);
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (dsmc_handle_type(coh) != DAOS_HTYPE_CO) {
		D_ERROR("Bad type (%d) of coh handle.\n",
			dsmc_handle_type(coh));
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dsmc_co_l2g(coh, glob);

out:
	return rc;
}

static int
dsmc_co_g2l(daos_handle_t poh, struct dsmc_container_glob *cont_glob,
	    daos_handle_t *coh)
{
	struct dsmc_pool	*pool;
	struct dsmc_container	*cont;
	int			rc = 0;

	D_ASSERT(cont_glob != NULL);
	D_ASSERT(coh != NULL);

	pool = dsmc_handle2pool(poh);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	if (uuid_compare(pool->dp_pool_hdl, cont_glob->dcg_pool_hdl) != 0) {
		D_ERROR("pool_hdl mismatch, in pool: "DF_UUID", in cont_glob: "
			DF_UUID"\n", DP_UUID(pool->dp_pool_hdl),
			DP_UUID(cont_glob->dcg_pool_hdl));
		D_GOTO(out, rc = -DER_INVAL);
	}

	if ((cont_glob->dcg_capas & DAOS_COO_RW) &&
	    (pool->dp_capas & DAOS_PC_RO))
		D_GOTO(out_pool, rc = -DER_NO_PERM);

	cont = dsmc_container_alloc(cont_glob->dcg_uuid);
	if (cont == NULL)
		D_GOTO(out_pool, rc = -DER_NOMEM);

	uuid_copy(cont->dc_cont_hdl, cont_glob->dcg_cont_hdl);
	cont->dc_capas = cont_glob->dcg_capas;
	cont->dc_slave = 1;

	pthread_rwlock_wrlock(&pool->dp_co_list_lock);
	if (pool->dp_disconnecting) {
		pthread_rwlock_unlock(&pool->dp_co_list_lock);
		D_ERROR("pool connection being invalidated\n");
		D_GOTO(out_cont, rc = -DER_NO_HDL);
	}
	daos_list_add(&cont->dc_po_list, &pool->dp_co_list);
	cont->dc_pool_hdl = poh;
	pthread_rwlock_unlock(&pool->dp_co_list_lock);

	dsmc_container_add_cache(cont, coh);

out_cont:
	dsmc_container_put(cont);
out_pool:
	dsmc_pool_put(pool);
out:
	return rc;
}

int
dsm_co_global2local(daos_handle_t poh, daos_iov_t glob, daos_handle_t *coh)
{
	struct dsmc_container_glob	 *cont_glob;
	int				  rc = 0;

	if (dsmc_handle_type(poh) != DAOS_HTYPE_POOL) {
		D_ERROR("Bad type (%d) of poh handle.\n",
			dsmc_handle_type(poh));
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (glob.iov_buf == NULL || glob.iov_buf_len < glob.iov_len ||
	    glob.iov_len != dsmc_container_glob_buf_size()) {
		D_DEBUG(DF_DSMC, "Invalid parameter of glob, iov_buf %p, "
			"iov_buf_len "DF_U64", iov_len "DF_U64".\n",
			glob.iov_buf, glob.iov_buf_len, glob.iov_len);
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (coh == NULL) {
		D_DEBUG(DF_DSMC, "Invalid parameter, NULL coh.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	cont_glob = (struct dsmc_container_glob *)glob.iov_buf;
	if (cont_glob->dcg_header.hgh_magic == D_SWAP32(DSM_GLOB_HDL_MAGIC)) {
		dsmc_swap_co_glob(cont_glob);
		D_ASSERT(cont_glob->dcg_header.hgh_magic == DSM_GLOB_HDL_MAGIC);
	} else if (cont_glob->dcg_header.hgh_magic != DSM_GLOB_HDL_MAGIC) {
		D_ERROR("Bad hgh_magic: 0x%x.\n",
			cont_glob->dcg_header.hgh_magic);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (cont_glob->dcg_header.hgh_type != DSMC_GLOB_CO) {
		D_ERROR("Bad hgh_type: %d.\n", cont_glob->dcg_header.hgh_type);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (uuid_is_null(cont_glob->dcg_pool_hdl) ||
	    uuid_is_null(cont_glob->dcg_uuid) ||
	    uuid_is_null(cont_glob->dcg_cont_hdl)) {
		D_ERROR("Invalid parameter, pool_hdl/uuid/cont_hdl is null.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dsmc_co_g2l(poh, cont_glob, coh);
	if (rc != 0)
		D_ERROR("dsmc_co_g2l failed, rc: %d.\n", rc);

out:
	return rc;
}

struct epoch_op_arg {
	struct dsmc_pool       *eoa_pool;
	struct dsmc_container  *eoa_cont;
	daos_epoch_t	       *eoa_epoch;
	daos_epoch_state_t     *eoa_state;
};

static int
epoch_op_complete(struct daos_op_sp *sp, daos_event_t *ev, int rc)
{
	dtp_rpc_t	       *rpc = sp->sp_rpc;
	dtp_opcode_t		opc = opc_get(rpc->dr_opc);
	struct epoch_op_out    *out = dtp_reply_get(rpc);
	struct epoch_op_arg    *arg = sp->sp_arg;

	if (rc != 0) {
		D_ERROR("RPC error during epoch operation %u: %d\n", opc, rc);
		D_GOTO(out, rc);
	}

	rc = out->eoo_cont_op_out.cpo_ret;
	if (rc != 0) {
		D_ERROR("epoch operation %u failed: %d\n", opc, rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, "completed epoch operation %u\n", opc);

	if (opc == DSM_CONT_EPOCH_HOLD)
		*arg->eoa_epoch = out->eoo_epoch_state.es_lhe;

	if (arg->eoa_state != NULL)
		*arg->eoa_state = out->eoo_epoch_state;

out:
	dtp_req_decref(rpc);
	dsmc_pool_put(arg->eoa_pool);
	dsmc_container_put(arg->eoa_cont);
	D_FREE_PTR(arg);
	return rc;
}

static int
epoch_op(daos_handle_t coh, dtp_opcode_t opc, daos_epoch_t *epoch,
	 daos_epoch_state_t *state, daos_event_t *ev)
{
	struct epoch_op_in     *in;
	struct dsmc_pool       *pool;
	struct dsmc_container  *cont;
	dtp_endpoint_t		ep;
	dtp_rpc_t	       *rpc;
	struct daos_op_sp      *sp;
	struct epoch_op_arg    *arg;
	int			rc;

	/* Check incoming arguments. */
	switch (opc) {
	case DSM_CONT_EPOCH_QUERY:
		D_ASSERT(epoch == NULL);
		break;
	case DSM_CONT_EPOCH_HOLD:
		if (epoch == NULL)
			D_GOTO(err, rc = -DER_INVAL);
		if (*epoch == 0)
			D_GOTO(err, rc = -DER_EP_RO);
		break;
	case DSM_CONT_EPOCH_COMMIT:
		D_ASSERT(epoch != NULL);
		if (*epoch == 0 || *epoch == DAOS_EPOCH_MAX)
			D_GOTO(err, rc = -DER_INVAL);
		break;
	}

	cont = dsmc_handle2container(coh);
	if (cont == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	pool = dsmc_handle2pool(cont->dc_pool_hdl);
	D_ASSERT(pool != NULL);

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			D_GOTO(err_pool, rc);
	}

	D_DEBUG(DF_DSMC, DF_UUID"/"DF_UUID": "DF_UUID" epoch="DF_U64"\n",
		DP_UUID(pool->dp_pool), DP_UUID(cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl), epoch == NULL ? 0 : *epoch);

	D_ALLOC_PTR(arg);
	if (arg == NULL) {
		D_ERROR("failed to allocate epoch op arg");
		D_GOTO(err_pool, rc = -DER_NOMEM);
	}

	arg->eoa_pool = pool;
	arg->eoa_cont = cont;
	arg->eoa_epoch = epoch;
	arg->eoa_state = state;

	/* To the only container service. */
	uuid_clear(ep.ep_grp_id);
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	rc = dsm_req_create(daos_ev2ctx(ev), ep, opc, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		D_GOTO(err_arg, rc);
	}

	in = dtp_req_get(rpc);
	uuid_copy(in->eoi_cont_op_in.cpi_pool, pool->dp_pool);
	uuid_copy(in->eoi_cont_op_in.cpi_cont, cont->dc_uuid);
	uuid_copy(in->eoi_cont_op_in.cpi_cont_hdl, cont->dc_cont_hdl);
	if (opc != DSM_CONT_EPOCH_QUERY)
		in->eoi_epoch = *epoch;

	sp = daos_ev2sp(ev);
	dtp_req_addref(rpc);
	sp->sp_rpc = rpc;
	sp->sp_arg = arg;

	rc = daos_event_launch(ev, NULL /* abort_cb */, epoch_op_complete);
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, ev);

err_rpc:
	dtp_req_decref(rpc);
	dtp_req_decref(rpc);
err_arg:
	D_FREE_PTR(arg);
err_pool:
	dsmc_pool_put(pool);
	dsmc_container_put(cont);
err:
	D_DEBUG(DF_DSMC, "epoch op %u("DF_U64") failed: %d\n", opc,
		epoch == NULL ? 0 : *epoch, rc);
	return rc;
}

int
dsm_epoch_query(daos_handle_t coh, daos_epoch_state_t *state, daos_event_t *ev)
{
	return epoch_op(coh, DSM_CONT_EPOCH_QUERY, NULL /* epoch */, state, ev);
}

int
dsm_epoch_hold(daos_handle_t coh, daos_epoch_t *epoch,
	       daos_epoch_state_t *state, daos_event_t *ev)
{
	return epoch_op(coh, DSM_CONT_EPOCH_HOLD, epoch, state, ev);
}

int
dsm_epoch_commit(daos_handle_t coh, daos_epoch_t epoch,
		 daos_epoch_state_t *state, daos_event_t *ev)
{
	return epoch_op(coh, DSM_CONT_EPOCH_COMMIT, &epoch, state, ev);
}

int
dsm_epoch_flush(daos_handle_t coh, daos_epoch_t epoch,
		daos_epoch_state_t *state, daos_event_t *ev)
{
	return 0;
}
