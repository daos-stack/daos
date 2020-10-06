/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * dc_cont: Container Client
 *
 * This module is part of libdaos. It implements the container methods of DAOS
 * API as well as daos/container.h.
 */
#define D_LOGFAC	DD_FAC(container)

#include <daos/container.h>
#include <daos/cont_props.h>
#include <daos/dedup.h>
#include <daos/event.h>
#include <daos/mgmt.h>
#include <daos/pool.h>
#include <daos/rsvc.h>
#include <daos_types.h>
#include "cli_internal.h"
#include "rpc.h"

/**
 * Initialize container interface
 */
int
dc_cont_init(void)
{
	int rc;

	rc = daos_rpc_register(&cont_proto_fmt, CONT_PROTO_CLI_COUNT,
				NULL, DAOS_CONT_MODULE);
	if (rc != 0)
		D_ERROR("failed to register cont RPCs: "DF_RC"\n", DP_RC(rc));

	return rc;
}

/**
 * Finalize container interface
 */
void
dc_cont_fini(void)
{
	daos_rpc_unregister(&cont_proto_fmt);
}

/*
 * Returns:
 *
 *   < 0			error; end the operation
 *   RSVC_CLIENT_RECHOOSE	task reinited; return 0 from completion cb
 *   RSVC_CLIENT_PROCEED	OK; proceed to process the reply
 */
static int
cont_rsvc_client_complete_rpc(struct dc_pool *pool, const crt_endpoint_t *ep,
			      int rc_crt, struct cont_op_out *out,
			      tse_task_t *task)
{
	int rc;

	D_MUTEX_LOCK(&pool->dp_client_lock);
	rc = rsvc_client_complete_rpc(&pool->dp_client, ep, rc_crt, out->co_rc,
				      &out->co_hint);
	D_MUTEX_UNLOCK(&pool->dp_client_lock);
	if (rc == RSVC_CLIENT_RECHOOSE ||
	    (rc == RSVC_CLIENT_PROCEED && daos_rpc_retryable_rc(out->co_rc))) {
		rc = tse_task_reinit(task);
		if (rc != 0)
			return rc;
		return RSVC_CLIENT_RECHOOSE;
	}
	return RSVC_CLIENT_PROCEED;
}

struct cont_args {
	struct dc_pool		*pool;
	crt_rpc_t		*rpc;
	daos_prop_t		*prop;
};

static int
cont_create_complete(tse_task_t *task, void *data)
{
	struct cont_args       *arg = (struct cont_args *)data;
	struct dc_pool	       *pool = arg->pool;
	struct cont_create_out *out = crt_reply_get(arg->rpc);
	int			rc = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &out->cco_op, task);
	if (rc < 0)
		D_GOTO(out, rc);
	else if (rc == RSVC_CLIENT_RECHOOSE)
		D_GOTO(out, rc = 0);

	if (rc != 0) {
		D_ERROR("RPC error while creating container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->cco_op.co_rc;
	if (rc != 0) {
		D_DEBUG(DF_DSMC, "failed to create container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, "completed creating container\n");

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(pool);
	daos_prop_free(arg->prop);
	return rc;
}

static bool
daos_prop_has_entry(daos_prop_t *prop, uint32_t entry_type)
{
	return (prop != NULL) &&
	       (daos_prop_entry_get(prop, entry_type) != NULL);
}

/*
 * If no owner/group prop was supplied, translates euid/egid to user and group
 * names, and adds them as owners to a new copy of the daos_prop_t passed in.
 * The newly allocated prop is expected to be freed by the cont create callback.
 */
static int
dup_with_default_ownership_props(daos_prop_t **prop_out, daos_prop_t *prop_in)
{
	char	       *owner = NULL;
	char	       *owner_grp = NULL;
	daos_prop_t    *final_prop = NULL;
	uint32_t	idx = 0;
	uint32_t	entries;
	int		rc = 0;
	uid_t		uid = geteuid();
	gid_t		gid = getegid();

	entries = (prop_in == NULL) ? 0 : prop_in->dpp_nr;

	if (!daos_prop_has_entry(prop_in, DAOS_PROP_CO_OWNER)) {
		rc = daos_acl_uid_to_principal(uid, &owner);
		if (rc != 0) {
			D_ERROR("Invalid uid\n");
			D_GOTO(err_out, rc);
		}

		entries++;
	}

	if (!daos_prop_has_entry(prop_in, DAOS_PROP_CO_OWNER_GROUP)) {
		rc = daos_acl_gid_to_principal(gid, &owner_grp);
		if (rc != 0) {
			D_ERROR("Invalid gid\n");
			D_GOTO(err_out, rc);
		}

		entries++;
	}

	/* We always free this prop in the callback - so need to make a copy */
	final_prop = daos_prop_alloc(entries);
	if (final_prop == NULL) {
		D_ERROR("failed to allocate props");
		D_GOTO(err_out, -DER_NOMEM);
	}

	if (prop_in != NULL && prop_in->dpp_nr > 0) {
		rc = daos_prop_copy(final_prop, prop_in);
		if (rc)
			D_GOTO(err_out, rc);
		idx = prop_in->dpp_nr;
	}

	if (prop_in == NULL || entries > prop_in->dpp_nr) {
		if (owner != NULL) {
			final_prop->dpp_entries[idx].dpe_type =
				DAOS_PROP_CO_OWNER;
			final_prop->dpp_entries[idx].dpe_str = owner;
			owner = NULL; /* prop is responsible for it now */
			idx++;
		}

		if (owner_grp != NULL) {
			final_prop->dpp_entries[idx].dpe_type =
				DAOS_PROP_CO_OWNER_GROUP;
			final_prop->dpp_entries[idx].dpe_str = owner_grp;
			owner_grp = NULL; /* prop is responsible for it now */
			idx++;
		}

	}

	*prop_out = final_prop;

	return rc;

err_out:
	daos_prop_free(final_prop);
	D_FREE(owner);
	D_FREE(owner_grp);
	return rc;
}

int
dc_cont_create(tse_task_t *task)
{
	daos_cont_create_t     *args;
	struct cont_create_in  *in;
	struct dc_pool	       *pool;
	crt_endpoint_t		ep;
	crt_rpc_t	       *rpc;
	struct cont_args	arg;
	int			rc;
	daos_prop_t	       *rpc_prop = NULL;

	args = dc_task_get_args(task);
	if (uuid_is_null(args->uuid))
		D_GOTO(err_task, rc = -DER_INVAL);

	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(err_task, rc = -DER_NO_HDL);

	rc = dup_with_default_ownership_props(&rpc_prop, args->prop);
	if (rc != 0)
		D_GOTO(err_pool, rc);

	D_DEBUG(DF_DSMC, DF_UUID": creating "DF_UUIDF"\n",
		DP_UUID(pool->dp_pool), DP_UUID(args->uuid));

	ep.ep_grp = pool->dp_sys->sy_group;
	D_MUTEX_LOCK(&pool->dp_client_lock);
	rc = rsvc_client_choose(&pool->dp_client, &ep);
	D_MUTEX_UNLOCK(&pool->dp_client_lock);
	if (rc != 0) {
		D_ERROR(DF_CONT": cannot find container service: "DF_RC"\n",
			DP_CONT(pool->dp_pool, args->uuid), DP_RC(rc));
		goto err_prop;
	}
	rc = cont_req_create(daos_task2ctx(task), &ep, CONT_CREATE, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_prop, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->cci_op.ci_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(in->cci_op.ci_uuid, args->uuid);
	in->cci_prop = rpc_prop;

	arg.pool = pool;
	arg.rpc = rpc;
	arg.prop = rpc_prop;
	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_create_complete, &arg,
				       sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_prop:
	daos_prop_free(rpc_prop);
err_pool:
	dc_pool_put(pool);
err_task:
	tse_task_complete(task, rc);
	return rc;
}

static int
cont_destroy_complete(tse_task_t *task, void *data)
{
	struct cont_args	*arg = (struct cont_args *)data;
	struct dc_pool		*pool = arg->pool;
	struct cont_destroy_out	*out = crt_reply_get(arg->rpc);
	int			 rc = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &out->cdo_op, task);
	if (rc < 0)
		D_GOTO(out, rc);
	else if (rc == RSVC_CLIENT_RECHOOSE)
		D_GOTO(out, rc = 0);

	if (rc != 0) {
		D_ERROR("RPC error while destroying container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->cdo_op.co_rc;
	if (rc != 0) {
		D_ERROR("failed to destroy container: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, "completed destroying container\n");

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(pool);
	return rc;
}

int
dc_cont_destroy(tse_task_t *task)
{
	daos_cont_destroy_t	*args;
	struct cont_destroy_in	*in;
	struct dc_pool		*pool;
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	struct cont_args	 arg;
	int			 rc;

	args = dc_task_get_args(task);

	if (uuid_is_null(args->uuid))
		D_GOTO(err, rc = -DER_INVAL);

	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	D_DEBUG(DF_DSMC, DF_UUID": destroying "DF_UUID": force=%d\n",
		DP_UUID(pool->dp_pool), DP_UUID(args->uuid), args->force);

	ep.ep_grp = pool->dp_sys->sy_group;
	D_MUTEX_LOCK(&pool->dp_client_lock);
	rc = rsvc_client_choose(&pool->dp_client, &ep);
	D_MUTEX_UNLOCK(&pool->dp_client_lock);
	if (rc != 0) {
		D_ERROR(DF_CONT": cannot find container service: "DF_RC"\n",
			DP_CONT(pool->dp_pool, args->uuid), DP_RC(rc));
		goto err_pool;
	}
	rc = cont_req_create(daos_task2ctx(task), &ep, CONT_DESTROY, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_pool, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->cdi_op.ci_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(in->cdi_op.ci_uuid, args->uuid);
	in->cdi_force = args->force;

	arg.pool = pool;
	arg.rpc = rpc;
	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_destroy_complete, &arg,
				       sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_pool:
	dc_pool_put(pool);
err:
	tse_task_complete(task, rc);
	return rc;
}

static void
dc_cont_hop_free(struct d_hlink *hlink)
{
	struct dc_cont *dc;

	dc = container_of(hlink, struct dc_cont, dc_hlink);
	D_ASSERT(daos_hhash_link_empty(&dc->dc_hlink));
	dc_cont_free(dc);
}

static struct d_hlink_ops cont_h_ops = {
	.hop_free	= dc_cont_hop_free,
};

void
dc_cont_put(struct dc_cont *dc)
{
	daos_hhash_link_putref(&dc->dc_hlink);
}

void
dc_cont_hdl_link(struct dc_cont *dc)
{
	daos_hhash_link_insert(&dc->dc_hlink, DAOS_HTYPE_CO);
}

void
dc_cont_hdl_unlink(struct dc_cont *dc)
{
	daos_hhash_link_delete(&dc->dc_hlink);
}

void
dc_cont_free(struct dc_cont *dc)
{
	D_ASSERT(daos_hhash_link_empty(&dc->dc_hlink));
	D_RWLOCK_DESTROY(&dc->dc_obj_list_lock);
	D_ASSERT(d_list_empty(&dc->dc_po_list));
	D_ASSERT(d_list_empty(&dc->dc_obj_list));
	D_FREE(dc);
}

struct dc_cont *
dc_cont_alloc(const uuid_t uuid)
{
	struct dc_cont *dc;

	D_ALLOC_PTR(dc);
	if (dc == NULL)
		return NULL;

	daos_hhash_hlink_init(&dc->dc_hlink, &cont_h_ops);
	uuid_copy(dc->dc_uuid, uuid);
	D_INIT_LIST_HEAD(&dc->dc_obj_list);
	D_INIT_LIST_HEAD(&dc->dc_po_list);
	if (D_RWLOCK_INIT(&dc->dc_obj_list_lock, NULL) != 0) {
		D_FREE(dc);
		dc = NULL;
	}

	return dc;
}

static void
dc_cont_props_init(struct dc_cont *cont)
{
	uint32_t	csum_type = cont->dc_props.dcp_csum_type;
	uint32_t	compress_type = cont->dc_props.dcp_compress_type;
	uint32_t	encrypt_type = cont->dc_props.dcp_encrypt_type;
	bool		dedup_only = false;

	cont->dc_props.dcp_compress_enabled =
			daos_cont_compress_prop_is_enabled(compress_type);
	cont->dc_props.dcp_encrypt_enabled =
			daos_cont_encrypt_prop_is_enabled(encrypt_type);

	if (csum_type == DAOS_PROP_CO_CSUM_OFF) {
		dedup_only = true;
		csum_type = dedup_get_csum_algo(&cont->dc_props);
	}

	if (!daos_cont_csum_prop_is_enabled(csum_type))
		return;

	daos_csummer_init_with_type(&cont->dc_csummer, csum_type,
				    cont->dc_props.dcp_chunksize, 0);

	if (dedup_only)
		dedup_configure_csummer(cont->dc_csummer, &cont->dc_props);
}

struct cont_open_args {
	struct dc_pool		*coa_pool;
	daos_cont_info_t	*coa_info;
	crt_rpc_t		*rpc;
	daos_handle_t		 hdl;
	daos_handle_t		*hdlp;
};

static int
cont_open_complete(tse_task_t *task, void *data)
{
	struct cont_open_args	*arg = (struct cont_open_args *)data;
	struct cont_open_out	*out = crt_reply_get(arg->rpc);
	struct dc_pool		*pool = arg->coa_pool;
	struct dc_cont		*cont = daos_task_get_priv(task);
	bool			 put_cont = true;
	int			 rc = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &out->coo_op, task);
	if (rc < 0)
		D_GOTO(out, rc);
	else if (rc == RSVC_CLIENT_RECHOOSE) {
		put_cont = false;
		D_GOTO(out, rc = 0);
	}

	if (rc != 0) {
		D_ERROR("RPC error while opening container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->coo_op.co_rc;
	if (rc != 0) {
		D_DEBUG(DF_DSMC, DF_CONT": failed to open container: %d\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), rc);
		D_GOTO(out, rc);
	}

	D_RWLOCK_WRLOCK(&pool->dp_co_list_lock);
	if (pool->dp_disconnecting) {
		D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);
		D_ERROR("pool connection being invalidated\n");
		/*
		 * Instead of sending a CONT_CLOSE RPC, we leave this new
		 * container handle on the server side to the POOL_DISCONNECT
		 * effort we are racing with.
		 */
		D_GOTO(out, rc = -DER_NO_HDL);
	}

	d_list_add(&cont->dc_po_list, &pool->dp_co_list);
	cont->dc_pool_hdl = arg->hdl;

	daos_props_2cont_props(out->coo_prop, &cont->dc_props);
	rc = daos_csummer_init_with_props(&cont->dc_csummer, out->coo_prop);

	D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);

	dc_cont_hdl_link(cont);
	dc_cont2hdl(cont, arg->hdlp);

	D_DEBUG(DF_DSMC, DF_CONT": opened: cookie="DF_X64" hdl="DF_UUID
		" master\n", DP_CONT(pool->dp_pool, cont->dc_uuid),
		arg->hdlp->cookie, DP_UUID(cont->dc_cont_hdl));

	if (arg->coa_info == NULL)
		D_GOTO(out, rc = 0);

	uuid_copy(arg->coa_info->ci_uuid, cont->dc_uuid);

	/* TODO */
	arg->coa_info->ci_nsnapshots = 0;
	arg->coa_info->ci_snapshots = NULL;
	arg->coa_info->ci_lsnapshot = 0;

out:
	crt_req_decref(arg->rpc);
	if (put_cont)
		dc_cont_put(cont);
	dc_pool_put(pool);
	return rc;
}

int
dc_cont_open(tse_task_t *task)
{
	daos_cont_open_t	*args;
	struct cont_open_in	*in;
	struct dc_pool		*pool;
	struct dc_cont		*cont;
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	struct cont_open_args	 arg;
	int			 rc;

	args = dc_task_get_args(task);
	cont = dc_task_get_priv(task);

	if (uuid_is_null(args->uuid) || args->coh == NULL)
		D_GOTO(err, rc = -DER_INVAL);

	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	if (cont == NULL) {
		cont = dc_cont_alloc(args->uuid);
		if (cont == NULL)
			D_GOTO(err_pool, rc = -DER_NOMEM);
		uuid_generate(cont->dc_cont_hdl);
		cont->dc_capas = args->flags;
		dc_task_set_priv(task, cont);
	}

	D_DEBUG(DF_DSMC, DF_CONT": opening: hdl="DF_UUIDF" flags=%x\n",
		DP_CONT(pool->dp_pool, args->uuid), DP_UUID(cont->dc_cont_hdl),
		args->flags);

	ep.ep_grp = pool->dp_sys->sy_group;
	D_MUTEX_LOCK(&pool->dp_client_lock);
	rc = rsvc_client_choose(&pool->dp_client, &ep);
	D_MUTEX_UNLOCK(&pool->dp_client_lock);
	if (rc != 0) {
		D_ERROR(DF_CONT": cannot find container service: "DF_RC"\n",
			DP_CONT(pool->dp_pool, args->uuid), DP_RC(rc));
		goto err_cont;
	}
	rc = cont_req_create(daos_task2ctx(task), &ep, CONT_OPEN, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_cont, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->coi_op.ci_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(in->coi_op.ci_uuid, args->uuid);
	uuid_copy(in->coi_op.ci_hdl, cont->dc_cont_hdl);
	in->coi_flags = args->flags;
	/** Determine which container properties need to be retrieved while
	 * opening the container
	 */
	in->coi_prop_bits	= DAOS_CO_QUERY_PROP_CSUM |
				  DAOS_CO_QUERY_PROP_CSUM_CHUNK |
				  DAOS_CO_QUERY_PROP_DEDUP |
				  DAOS_CO_QUERY_PROP_DEDUP_THRESHOLD;
	arg.coa_pool		= pool;
	arg.coa_info		= args->info;
	arg.rpc			= rpc;
	arg.hdl			= args->poh;
	arg.hdlp		= args->coh;

	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_open_complete, &arg,
				       sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	/** send the request */
	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_cont:
	dc_cont_put(cont);
err_pool:
	dc_pool_put(pool);
err:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "failed to open container: "DF_RC"\n", DP_RC(rc));
	return rc;
}

struct cont_close_args {
	struct dc_pool	*cca_pool;
	struct dc_cont	*cca_cont;
	crt_rpc_t	*rpc;
	daos_handle_t	 hdl;
};

static int
cont_close_complete(tse_task_t *task, void *data)
{
	struct cont_close_args	*arg = (struct cont_close_args *)data;
	struct cont_close_out	*out = crt_reply_get(arg->rpc);
	struct dc_pool		*pool = arg->cca_pool;
	struct dc_cont		*cont = arg->cca_cont;
	int			 rc = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &out->cco_op, task);
	if (rc < 0)
		D_GOTO(out, rc);
	else if (rc == RSVC_CLIENT_RECHOOSE)
		D_GOTO(out, rc = 0);

	if (rc != 0) {
		D_ERROR("RPC error while closing container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->cco_op.co_rc;
	if (rc == -DER_NO_HDL) {
		/* The pool connection cannot be found on the server. */
		D_DEBUG(DF_DSMC, DF_CONT": already disconnected: hdl="DF_UUID
			" pool_hdl="DF_UUID"\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid),
			DP_UUID(cont->dc_cont_hdl), DP_UUID(pool->dp_pool_hdl));
		rc = 0;
	} else if (rc == -DER_NONEXIST) {
		/* The container cannot be found on the server. */
		D_DEBUG(DF_DSMC, DF_CONT": already destroyed: hdl="DF_UUID"\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid),
			DP_UUID(cont->dc_cont_hdl));
		rc = 0;
	} else if (rc != 0) {
		D_ERROR("failed to close container: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, DF_CONT": closed: cookie="DF_X64" hdl="DF_UUID
		" master\n", DP_CONT(pool->dp_pool, cont->dc_uuid),
		arg->hdl.cookie, DP_UUID(cont->dc_cont_hdl));

	dc_cont_hdl_unlink(cont);
	dc_cont_put(cont);

	daos_csummer_destroy(&cont->dc_csummer);

	/* Remove the container from pool container list */
	D_RWLOCK_WRLOCK(&pool->dp_co_list_lock);
	d_list_del_init(&cont->dc_po_list);
	D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(pool);
	dc_cont_put(cont);
	return rc;
}

int
dc_cont_close(tse_task_t *task)
{
	daos_cont_close_t      *args;
	daos_handle_t		coh;
	struct cont_close_in   *in;
	struct dc_pool	       *pool;
	struct dc_cont	       *cont;
	crt_endpoint_t		ep;
	crt_rpc_t	       *rpc;
	struct cont_close_args  arg;
	int			rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");
	coh = args->coh;

	cont = dc_hdl2cont(coh);
	if (cont == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	/* Check if there are not objects opened for this container */
	D_RWLOCK_RDLOCK(&cont->dc_obj_list_lock);
	if (!d_list_empty(&cont->dc_obj_list)) {
		D_ERROR("cannot close container, object not closed.\n");
		D_RWLOCK_UNLOCK(&cont->dc_obj_list_lock);
		D_GOTO(err_cont, rc = -DER_BUSY);
	}
	cont->dc_closing = 1;
	D_RWLOCK_UNLOCK(&cont->dc_obj_list_lock);

	pool = dc_hdl2pool(cont->dc_pool_hdl);
	D_ASSERT(pool != NULL);

	D_DEBUG(DF_DSMC, DF_CONT": closing: cookie="DF_X64" hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid), coh.cookie,
		DP_UUID(cont->dc_cont_hdl));

	if (cont->dc_slave) {
		daos_csummer_destroy(&cont->dc_csummer);
		dc_cont_hdl_unlink(cont);
		dc_cont_put(cont);

		/* Remove the container from pool container list */
		D_RWLOCK_WRLOCK(&pool->dp_co_list_lock);
		d_list_del_init(&cont->dc_po_list);
		D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);

		D_DEBUG(DF_DSMC, DF_CONT": closed: cookie="DF_X64" hdl="DF_UUID
			"\n", DP_CONT(pool->dp_pool, cont->dc_uuid), coh.cookie,
			DP_UUID(cont->dc_cont_hdl));
		dc_pool_put(pool);
		dc_cont_put(cont);
		tse_task_complete(task, 0);
		return 0;
	}

	ep.ep_grp = pool->dp_sys->sy_group;
	D_MUTEX_LOCK(&pool->dp_client_lock);
	rc = rsvc_client_choose(&pool->dp_client, &ep);
	D_MUTEX_UNLOCK(&pool->dp_client_lock);
	if (rc != 0) {
		D_ERROR(DF_CONT": cannot find container service: "DF_RC"\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), DP_RC(rc));
		goto err_pool;
	}
	rc = cont_req_create(daos_task2ctx(task), &ep, CONT_CLOSE, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_pool, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->cci_op.ci_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(in->cci_op.ci_uuid, cont->dc_uuid);
	uuid_copy(in->cci_op.ci_hdl, cont->dc_cont_hdl);

	arg.cca_pool = pool;
	arg.cca_cont = cont;
	arg.rpc = rpc;
	arg.hdl = coh;
	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_close_complete, &arg,
				       sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	/** send the request */
	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_pool:
	dc_pool_put(pool);
err_cont:
	dc_cont_put(cont);
err:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "failed to close container handle "DF_X64": %d\n",
		coh.cookie, rc);
	return rc;
}

struct cont_query_args {
	struct dc_pool		*cqa_pool;
	struct dc_cont		*cqa_cont;
	daos_cont_info_t	*cqa_info;
	daos_prop_t		*cqa_prop;
	crt_rpc_t		*rpc;
	daos_handle_t		hdl;
};

static int
cont_query_complete(tse_task_t *task, void *data)
{
	struct cont_query_args	*arg = (struct cont_query_args *)data;
	struct cont_query_out	*out = crt_reply_get(arg->rpc);
	struct dc_pool		*pool = arg->cqa_pool;
	struct dc_cont		*cont = arg->cqa_cont;
	int			 rc   = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &out->cqo_op, task);
	if (rc < 0)
		D_GOTO(out, rc);
	else if (rc == RSVC_CLIENT_RECHOOSE)
		D_GOTO(out, rc = 0);

	if (rc != 0) {
		D_ERROR("RPC error while querying container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->cqo_op.co_rc;
	if (rc == 0 && arg->cqa_prop != NULL)
		rc = daos_prop_copy(arg->cqa_prop, out->cqo_prop);

	if (rc != 0) {
		D_DEBUG(DF_DSMC, DF_CONT": failed to query container: %d\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, DF_CONT": Queried: using hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

	if (arg->cqa_info == NULL)
		D_GOTO(out, rc = 0);

	uuid_copy(arg->cqa_info->ci_uuid, cont->dc_uuid);

	arg->cqa_info->ci_hae = out->cqo_hae;
	/* TODO */
	arg->cqa_info->ci_nsnapshots = 0;
	arg->cqa_info->ci_snapshots = NULL;
	arg->cqa_info->ci_lsnapshot = 0;

out:
	crt_req_decref(arg->rpc);
	dc_cont_put(cont);
	dc_pool_put(pool);
	return rc;
}

static uint64_t
cont_query_bits(daos_prop_t *prop)
{
	uint64_t		 bits = 0;
	int			 i;

	if (prop == NULL)
		return 0;
	if (prop->dpp_entries == NULL)
		return DAOS_CO_QUERY_PROP_ALL;

	for (i = 0; i < prop->dpp_nr; i++) {
		struct daos_prop_entry	*entry;

		entry = &prop->dpp_entries[i];
		switch (entry->dpe_type) {
		case DAOS_PROP_CO_LABEL:
			bits |= DAOS_CO_QUERY_PROP_LABEL;
			break;
		case DAOS_PROP_CO_LAYOUT_TYPE:
			bits |= DAOS_CO_QUERY_PROP_LAYOUT_TYPE;
			break;
		case DAOS_PROP_CO_LAYOUT_VER:
			bits |= DAOS_CO_QUERY_PROP_LAYOUT_VER;
			break;
		case DAOS_PROP_CO_CSUM:
			bits |= DAOS_CO_QUERY_PROP_CSUM;
			break;
		case DAOS_PROP_CO_CSUM_CHUNK_SIZE:
			bits |= DAOS_CO_QUERY_PROP_CSUM_CHUNK;
			break;
		case DAOS_PROP_CO_CSUM_SERVER_VERIFY:
			bits |= DAOS_CO_QUERY_PROP_CSUM_SERVER;
			break;
		case DAOS_PROP_CO_DEDUP:
			bits |= DAOS_CO_QUERY_PROP_DEDUP;
			break;
		case DAOS_PROP_CO_DEDUP_THRESHOLD:
			bits |= DAOS_CO_QUERY_PROP_DEDUP_THRESHOLD;
		case DAOS_PROP_CO_REDUN_FAC:
			bits |= DAOS_CO_QUERY_PROP_REDUN_FAC;
			break;
		case DAOS_PROP_CO_REDUN_LVL:
			bits |= DAOS_CO_QUERY_PROP_REDUN_LVL;
			break;
		case DAOS_PROP_CO_SNAPSHOT_MAX:
			bits |= DAOS_CO_QUERY_PROP_SNAPSHOT_MAX;
			break;
		case DAOS_PROP_CO_COMPRESS:
			bits |= DAOS_CO_QUERY_PROP_COMPRESS;
			break;
		case DAOS_PROP_CO_ENCRYPT:
			bits |= DAOS_CO_QUERY_PROP_ENCRYPT;
			break;
		case DAOS_PROP_CO_ACL:
			bits |= DAOS_CO_QUERY_PROP_ACL;
			break;
		case DAOS_PROP_CO_OWNER:
			bits |= DAOS_CO_QUERY_PROP_OWNER;
			break;
		case DAOS_PROP_CO_OWNER_GROUP:
			bits |= DAOS_CO_QUERY_PROP_OWNER_GROUP;
			break;
		default:
			D_ERROR("ignore bad dpt_type %d.\n", entry->dpe_type);
			break;
		}
	}
	return bits;
}

int
dc_cont_query(tse_task_t *task)
{
	daos_cont_query_t	*args;
	struct cont_query_in	*in;
	struct dc_pool		*pool;
	struct dc_cont		*cont;
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	struct cont_query_args	 arg;
	int			 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	cont = dc_hdl2cont(args->coh);
	if (cont == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	pool = dc_hdl2pool(cont->dc_pool_hdl);
	D_ASSERT(pool != NULL);

	D_DEBUG(DF_DSMC, DF_CONT": querying: hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool_hdl, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

	ep.ep_grp  = pool->dp_sys->sy_group;
	D_MUTEX_LOCK(&pool->dp_client_lock);
	rc = rsvc_client_choose(&pool->dp_client, &ep);
	D_MUTEX_UNLOCK(&pool->dp_client_lock);
	if (rc != 0) {
		D_ERROR(DF_CONT": cannot find container service: "DF_RC"\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), DP_RC(rc));
		goto err_cont;
	}
	rc = cont_req_create(daos_task2ctx(task), &ep, CONT_QUERY, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_cont, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->cqi_op.ci_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(in->cqi_op.ci_uuid, cont->dc_uuid);
	uuid_copy(in->cqi_op.ci_hdl, cont->dc_cont_hdl);
	in->cqi_bits = cont_query_bits(args->prop);
	if (args->info != NULL)
		in->cqi_bits |= DAOS_CO_QUERY_TGT;

	arg.cqa_pool = pool;
	arg.cqa_cont = cont;
	arg.cqa_info = args->info;
	arg.cqa_prop = args->prop;
	arg.rpc	     = rpc;
	arg.hdl	     = args->coh;
	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_query_complete, &arg,
				       sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_cont:
	dc_cont_put(cont);
	dc_pool_put(pool);
err:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "Failed to query container: "DF_RC"\n", DP_RC(rc));
	return rc;
}

struct cont_set_prop_args {
	struct dc_pool		*cqa_pool;
	struct dc_cont		*cqa_cont;
	crt_rpc_t		*rpc;
	daos_handle_t		hdl;
};

static int
cont_set_prop_complete(tse_task_t *task, void *data)
{
	struct cont_set_prop_args	*arg = (struct cont_set_prop_args *)
						data;
	struct cont_prop_set_out	*out = crt_reply_get(arg->rpc);
	struct dc_pool			*pool = arg->cqa_pool;
	struct dc_cont			*cont = arg->cqa_cont;
	int				 rc   = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &out->cpso_op, task);
	if (rc < 0)
		D_GOTO(out, rc);
	else if (rc == RSVC_CLIENT_RECHOOSE)
		D_GOTO(out, rc = 0);

	if (rc != 0) {
		D_ERROR("RPC error while setting prop on container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->cpso_op.co_rc;

	if (rc != 0) {
		D_DEBUG(DF_DSMC, DF_CONT": failed to set prop on container: "
			"%d\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, DF_CONT": Set prop: using hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

out:
	crt_req_decref(arg->rpc);
	dc_cont_put(cont);
	dc_pool_put(pool);
	return rc;
}

int
dc_cont_set_prop(tse_task_t *task)
{
	daos_cont_set_prop_t		*args;
	struct cont_prop_set_in		*in;
	struct dc_pool			*pool;
	struct dc_cont			*cont;
	crt_endpoint_t			 ep;
	crt_rpc_t			*rpc;
	struct cont_set_prop_args	 arg;
	int				 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	cont = dc_hdl2cont(args->coh);
	if (cont == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	pool = dc_hdl2pool(cont->dc_pool_hdl);
	D_ASSERT(pool != NULL);

	D_DEBUG(DF_DSMC, DF_CONT": setting props: hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

	ep.ep_grp  = pool->dp_sys->sy_group;
	D_MUTEX_LOCK(&pool->dp_client_lock);
	rc = rsvc_client_choose(&pool->dp_client, &ep);
	D_MUTEX_UNLOCK(&pool->dp_client_lock);
	if (rc != 0) {
		D_ERROR(DF_CONT": cannot find container service: "DF_RC"\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), DP_RC(rc));
		goto err_cont;
	}
	rc = cont_req_create(daos_task2ctx(task), &ep, CONT_PROP_SET, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_cont, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->cpsi_op.ci_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(in->cpsi_op.ci_uuid, cont->dc_uuid);
	uuid_copy(in->cpsi_op.ci_hdl, cont->dc_cont_hdl);
	in->cpsi_prop = args->prop;

	arg.cqa_pool = pool;
	arg.cqa_cont = cont;
	arg.rpc	     = rpc;
	arg.hdl	     = args->coh;
	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_set_prop_complete, &arg,
				       sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_cont:
	dc_cont_put(cont);
	dc_pool_put(pool);
err:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "Failed to set prop on container: "DF_RC"\n",
		DP_RC(rc));
	return rc;
}

struct cont_update_acl_args {
	struct dc_pool		*cua_pool;
	struct dc_cont		*cua_cont;
	crt_rpc_t		*rpc;
	daos_handle_t		hdl;
};

static int
cont_update_acl_complete(tse_task_t *task, void *data)
{
	struct cont_update_acl_args	*arg = (struct cont_update_acl_args *)
						data;
	struct cont_acl_update_out	*out = crt_reply_get(arg->rpc);
	struct dc_pool			*pool = arg->cua_pool;
	struct dc_cont			*cont = arg->cua_cont;
	int				 rc   = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &out->cauo_op, task);
	if (rc < 0)
		D_GOTO(out, rc);
	else if (rc == RSVC_CLIENT_RECHOOSE)
		D_GOTO(out, rc = 0);

	if (rc != 0) {
		D_ERROR("RPC error while updating ACL on container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->cauo_op.co_rc;

	if (rc != 0) {
		D_DEBUG(DF_DSMC, DF_CONT": failed to update ACL on container: "
			"%d\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, DF_CONT": Update ACL: using hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

out:
	crt_req_decref(arg->rpc);
	dc_cont_put(cont);
	dc_pool_put(pool);
	return rc;
}

int
dc_cont_update_acl(tse_task_t *task)
{
	daos_cont_update_acl_t		*args;
	struct cont_acl_update_in	*in;
	struct dc_pool			*pool;
	struct dc_cont			*cont;
	crt_endpoint_t			 ep;
	crt_rpc_t			*rpc;
	struct cont_update_acl_args	 arg;
	int				 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	cont = dc_hdl2cont(args->coh);
	if (cont == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	pool = dc_hdl2pool(cont->dc_pool_hdl);
	D_ASSERT(pool != NULL);

	D_DEBUG(DF_DSMC, DF_CONT": updating ACL: hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

	ep.ep_grp  = pool->dp_sys->sy_group;
	D_MUTEX_LOCK(&pool->dp_client_lock);
	rc = rsvc_client_choose(&pool->dp_client, &ep);
	D_MUTEX_UNLOCK(&pool->dp_client_lock);
	if (rc != 0) {
		D_ERROR(DF_CONT": cannot find container service: "DF_RC"\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), DP_RC(rc));
		goto err_cont;
	}
	rc = cont_req_create(daos_task2ctx(task), &ep, CONT_ACL_UPDATE, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_cont, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->caui_op.ci_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(in->caui_op.ci_uuid, cont->dc_uuid);
	uuid_copy(in->caui_op.ci_hdl, cont->dc_cont_hdl);
	in->caui_acl = args->acl;

	arg.cua_pool = pool;
	arg.cua_cont = cont;
	arg.rpc	     = rpc;
	arg.hdl	     = args->coh;
	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_update_acl_complete, &arg,
				       sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_cont:
	dc_cont_put(cont);
	dc_pool_put(pool);
err:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "Failed to update ACL on container: "DF_RC"\n",
		DP_RC(rc));
	return rc;
}

struct cont_delete_acl_args {
	struct dc_pool		*cda_pool;
	struct dc_cont		*cda_cont;
	crt_rpc_t		*rpc;
	daos_handle_t		hdl;
};

static int
cont_delete_acl_complete(tse_task_t *task, void *data)
{
	struct cont_delete_acl_args	*arg = (struct cont_delete_acl_args *)
						data;
	struct cont_acl_delete_out	*out = crt_reply_get(arg->rpc);
	struct dc_pool			*pool = arg->cda_pool;
	struct dc_cont			*cont = arg->cda_cont;
	int				 rc   = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &out->cado_op, task);
	if (rc < 0)
		D_GOTO(out, rc);
	else if (rc == RSVC_CLIENT_RECHOOSE)
		D_GOTO(out, rc = 0);

	if (rc != 0) {
		D_ERROR("RPC error while deleting ACL on container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->cado_op.co_rc;

	if (rc != 0) {
		D_DEBUG(DF_DSMC, DF_CONT": failed to delete ACL on container: "
			"%d\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, DF_CONT": Delete ACL: using hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

out:
	crt_req_decref(arg->rpc);
	dc_cont_put(cont);
	dc_pool_put(pool);
	return rc;
}

int
dc_cont_delete_acl(tse_task_t *task)
{
	daos_cont_delete_acl_t		*args;
	struct cont_acl_delete_in	*in;
	struct dc_pool			*pool;
	struct dc_cont			*cont;
	crt_endpoint_t			 ep;
	crt_rpc_t			*rpc;
	struct cont_delete_acl_args	 arg;
	int				 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	cont = dc_hdl2cont(args->coh);
	if (cont == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	pool = dc_hdl2pool(cont->dc_pool_hdl);
	D_ASSERT(pool != NULL);

	D_DEBUG(DF_DSMC, DF_CONT": deleting ACL: hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

	ep.ep_grp  = pool->dp_sys->sy_group;
	D_MUTEX_LOCK(&pool->dp_client_lock);
	rc = rsvc_client_choose(&pool->dp_client, &ep);
	D_MUTEX_UNLOCK(&pool->dp_client_lock);
	if (rc != 0) {
		D_ERROR(DF_CONT": cannot find container service: "DF_RC"\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), DP_RC(rc));
		goto err_cont;
	}
	rc = cont_req_create(daos_task2ctx(task), &ep, CONT_ACL_DELETE, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_cont, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->cadi_op.ci_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(in->cadi_op.ci_uuid, cont->dc_uuid);
	uuid_copy(in->cadi_op.ci_hdl, cont->dc_cont_hdl);
	in->cadi_principal_type = args->type;
	in->cadi_principal_name = args->name;

	arg.cda_pool = pool;
	arg.cda_cont = cont;
	arg.rpc	     = rpc;
	arg.hdl	     = args->coh;
	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_delete_acl_complete, &arg,
				       sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_cont:
	dc_cont_put(cont);
	dc_pool_put(pool);
err:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "Failed to delete ACL on container: "DF_RC"\n",
		DP_RC(rc));
	return rc;
}

struct cont_oid_alloc_args {
	struct dc_pool		*coaa_pool;
	struct dc_cont		*coaa_cont;
	crt_rpc_t		*rpc;
	daos_handle_t		hdl;
	daos_size_t		num_oids;
	uint64_t		*oid;
};

static int
pool_query_cb(tse_task_t *task, void *data)
{
	daos_pool_query_t	*args;

	args = dc_task_get_args(task);
	D_FREE(args->info);
	return task->dt_result;
}

static int
cont_oid_alloc_complete(tse_task_t *task, void *data)
{
	struct cont_oid_alloc_args *arg = (struct cont_oid_alloc_args *)data;
	struct cont_oid_alloc_out *out = crt_reply_get(arg->rpc);
	struct dc_pool *pool = arg->coaa_pool;
	struct dc_cont *cont = arg->coaa_cont;
	int rc = task->dt_result;

	if (daos_rpc_retryable_rc(rc) || rc == -DER_STALE) {
		tse_sched_t *sched = tse_task2sched(task);
		daos_pool_query_t *pargs;
		tse_task_t *ptask;

		/** pool map update task */
		rc = dc_task_create(dc_pool_query, sched, NULL, &ptask);
		if (rc != 0)
			return rc;

		pargs = dc_task_get_args(ptask);
		pargs->poh = arg->coaa_cont->dc_pool_hdl;
		D_ALLOC_PTR(pargs->info);
		if (pargs->info == NULL) {
			dc_task_decref(ptask);
			D_GOTO(out, rc = -DER_NOMEM);
		}

		rc = dc_task_reg_comp_cb(ptask, pool_query_cb, NULL, 0);
		if (rc != 0) {
			D_FREE(pargs->info);
			dc_task_decref(ptask);
			D_GOTO(out, rc);
		}

		rc = dc_task_resched(task);
		if (rc != 0) {
			D_FREE(pargs->info);
			dc_task_decref(ptask);
			D_GOTO(out, rc);
		}

		rc = dc_task_depend(task, 1, &ptask);
		if (rc != 0) {
			D_FREE(pargs->info);
			dc_task_decref(ptask);
			D_GOTO(out, rc);
		}

		/* ignore returned value, error is reported by comp_cb */
		dc_task_schedule(ptask, true);
		D_GOTO(out, rc = 0);
	} else if (rc != 0) {
		/** error but non retryable RPC */
		D_ERROR("failed to allocate oids: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->coao_op.co_rc;
	if (rc != 0) {
		D_ERROR("failed to allocate oids: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, DF_CONT": OID ALLOC: using hdl="DF_UUID"\n",
		 DP_CONT(pool->dp_pool, cont->dc_uuid),
		 DP_UUID(cont->dc_cont_hdl));

	if (arg->oid)
		*arg->oid = out->oid;

out:
	crt_req_decref(arg->rpc);
	dc_cont_put(cont);
	dc_pool_put(pool);
	return rc;
}

static int
get_tgt_rank(struct dc_pool *pool, unsigned int *rank)
{
	struct pool_target	*tgts = NULL;
	unsigned int		tgt_cnt;
	int			rc;

	rc = pool_map_find_upin_tgts(pool->dp_map, &tgts, &tgt_cnt);
	if (rc)
		return rc;

	if (tgt_cnt == 0 || tgts == NULL)
		return -DER_INVAL;

	*rank = tgts[rand() % tgt_cnt].ta_comp.co_rank;

	D_FREE(tgts);

	return 0;
}

int
dc_cont_alloc_oids(tse_task_t *task)
{
	daos_cont_alloc_oids_t		*args;
	struct cont_oid_alloc_in	*in;
	struct dc_pool			*pool;
	struct dc_cont			*cont;
	crt_endpoint_t			ep;
	crt_rpc_t			*rpc;
	struct cont_oid_alloc_args	arg;
	int				rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (args->num_oids == 0 || args->oid == NULL)
		D_GOTO(err, rc = -DER_INVAL);

	cont = dc_hdl2cont(args->coh);
	if (cont == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	pool = dc_hdl2pool(cont->dc_pool_hdl);
	D_ASSERT(pool != NULL);

	D_DEBUG(DF_DSMC, DF_CONT": oid allocate: hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool_hdl, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

	/** randomly select a rank from the pool map */
	ep.ep_grp = pool->dp_sys->sy_group;
	ep.ep_tag = 0;
	rc = get_tgt_rank(pool, &ep.ep_rank);
	if (rc != 0)
		D_GOTO(err_cont, rc);

	rc = cont_req_create(daos_task2ctx(task), &ep, CONT_OID_ALLOC, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_cont, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->coai_op.ci_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(in->coai_op.ci_uuid, cont->dc_uuid);
	uuid_copy(in->coai_op.ci_hdl, cont->dc_cont_hdl);
	in->num_oids = args->num_oids;

	arg.coaa_pool	= pool;
	arg.coaa_cont	= cont;
	arg.rpc		= rpc;
	arg.hdl		= args->coh;
	arg.num_oids	= args->num_oids;
	arg.oid		= args->oid;
	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, cont_oid_alloc_complete, &arg,
				       sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_cont:
	dc_cont_put(cont);
	dc_pool_put(pool);
err:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "Failed to allocate OIDs: "DF_RC"\n", DP_RC(rc));
	return rc;
}

#define DC_CONT_GLOB_MAGIC	(0x16ca0387)

/* Structure of global buffer for dc_cont */
struct dc_cont_glob {
	/** magic number, DC_CONT_GLOB_MAGIC */
	uint32_t	dcg_magic;
	uint32_t	dcg_padding;
	/** pool connection handle */
	uuid_t		dcg_pool_hdl;
	/** container uuid and capas */
	uuid_t		dcg_uuid;
	uuid_t		dcg_cont_hdl;
	uint64_t	dcg_capas;
	/** specific features */
	uint16_t	dcg_csum_type;
	uint16_t        dcg_encrypt_type;
	uint32_t	dcg_compress_type;
	uint32_t	dcg_csum_chunksize;
	uint32_t        dcg_dedup_th;
	uint32_t	dcg_csum_srv_verify:1,
			dcg_dedup_enabled:1,
			dcg_dedup_verify:1;
};

static inline daos_size_t
dc_cont_glob_buf_size()
{
       return sizeof(struct dc_cont_glob);
}

static inline void
swap_co_glob(struct dc_cont_glob *cont_glob)
{
	D_ASSERT(cont_glob != NULL);

	D_SWAP32S(&cont_glob->dcg_magic);
	/* skip cont_glob->dcg_padding */
	/* skip cont_glob->dcg_pool_hdl (uuid_t) */
	/* skip cont_glob->dcg_uuid (uuid_t) */
	/* skip cont_glob->dcg_cont_hdl (uuid_t) */
	D_SWAP64S(&cont_glob->dcg_capas);
}

static int
dc_cont_l2g(daos_handle_t coh, d_iov_t *glob)
{
	struct dc_pool		*pool;
	struct dc_cont		*cont;
	struct dc_cont_glob	*cont_glob;
	daos_size_t		 glob_buf_size;
	int			 rc = 0;

	D_ASSERT(glob != NULL);

	cont = dc_hdl2cont(coh);
	if (cont == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	glob_buf_size = dc_cont_glob_buf_size();
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

	pool = dc_hdl2pool(cont->dc_pool_hdl);
	if (pool == NULL)
		D_GOTO(out_cont, rc = -DER_NO_HDL);

	/** init global handle */
	cont_glob = (struct dc_cont_glob *)glob->iov_buf;
	cont_glob->dcg_magic = DC_CONT_GLOB_MAGIC;
	uuid_copy(cont_glob->dcg_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(cont_glob->dcg_uuid, cont->dc_uuid);
	uuid_copy(cont_glob->dcg_cont_hdl, cont->dc_cont_hdl);
	cont_glob->dcg_capas = cont->dc_capas;

	/** transfer container properties */
	cont_glob->dcg_csum_type	= cont->dc_props.dcp_csum_type;
	cont_glob->dcg_csum_chunksize	= cont->dc_props.dcp_chunksize;
	cont_glob->dcg_csum_srv_verify	= cont->dc_props.dcp_srv_verify;
	cont_glob->dcg_dedup_enabled	= cont->dc_props.dcp_dedup_enabled;
	cont_glob->dcg_dedup_verify	= cont->dc_props.dcp_dedup_verify;
	cont_glob->dcg_dedup_th		= cont->dc_props.dcp_dedup_size;
	cont_glob->dcg_compress_type	= cont->dc_props.dcp_compress_type;
	cont_glob->dcg_encrypt_type	= cont->dc_props.dcp_encrypt_type;

	dc_pool_put(pool);
out_cont:
	dc_cont_put(cont);
out:
	if (rc)
		D_ERROR("daos_cont_l2g failed, rc: "DF_RC"\n", DP_RC(rc));
	return rc;
}

int
dc_cont_local2global(daos_handle_t coh, d_iov_t *glob)
{
	int	rc = 0;

	if (glob == NULL) {
		D_ERROR("Invalid parameter, NULL glob pointer.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (glob->iov_buf != NULL && (glob->iov_buf_len == 0 ||
	    glob->iov_buf_len < glob->iov_len)) {
		D_ERROR("Invalid parameter of glob, iov_buf %p, iov_buf_len "
			""DF_U64", iov_len "DF_U64".\n", glob->iov_buf,
			glob->iov_buf_len, glob->iov_len);
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dc_cont_l2g(coh, glob);

out:
	return rc;
}

static int
dc_cont_g2l(daos_handle_t poh, struct dc_cont_glob *cont_glob,
	    daos_handle_t *coh)
{
	struct dc_pool *pool;
	struct dc_cont *cont;
	int		rc = 0;

	D_ASSERT(cont_glob != NULL);
	D_ASSERT(coh != NULL);

	pool = dc_hdl2pool(poh);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	if (uuid_compare(pool->dp_pool_hdl, cont_glob->dcg_pool_hdl) != 0) {
		D_ERROR("pool_hdl mismatch, in pool: "DF_UUID", in cont_glob: "
			DF_UUID"\n", DP_UUID(pool->dp_pool_hdl),
			DP_UUID(cont_glob->dcg_pool_hdl));
		D_GOTO(out, rc = -DER_INVAL);
	}

	cont = dc_cont_alloc(cont_glob->dcg_uuid);
	if (cont == NULL)
		D_GOTO(out_pool, rc = -DER_NOMEM);

	uuid_copy(cont->dc_cont_hdl, cont_glob->dcg_cont_hdl);
	cont->dc_capas = cont_glob->dcg_capas;
	cont->dc_slave = 1;

	D_RWLOCK_WRLOCK(&pool->dp_co_list_lock);
	if (pool->dp_disconnecting) {
		D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);
		D_ERROR("pool connection being invalidated\n");
		D_GOTO(out_cont, rc = -DER_NO_HDL);
	}

	d_list_add(&cont->dc_po_list, &pool->dp_co_list);
	cont->dc_pool_hdl = poh;
	D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);

	/** extract container properties */
	cont->dc_props.dcp_dedup_enabled = cont_glob->dcg_dedup_enabled;
	cont->dc_props.dcp_csum_type	 = cont_glob->dcg_csum_type;
	cont->dc_props.dcp_srv_verify	 = cont_glob->dcg_csum_srv_verify;
	cont->dc_props.dcp_chunksize	 = cont_glob->dcg_csum_chunksize;
	cont->dc_props.dcp_dedup_size	 = cont_glob->dcg_dedup_th;
	cont->dc_props.dcp_dedup_verify  = cont_glob->dcg_dedup_verify;
	cont->dc_props.dcp_compress_type = cont_glob->dcg_compress_type;
	cont->dc_props.dcp_encrypt_type	 = cont_glob->dcg_encrypt_type;
	dc_cont_props_init(cont);

	dc_cont_hdl_link(cont);
	dc_cont2hdl(cont, coh);

	D_DEBUG(DF_DSMC, DF_UUID": opened "DF_UUID": cookie="DF_X64" hdl="
		DF_UUID" slave\n", DP_UUID(pool->dp_pool),
		DP_UUID(cont->dc_uuid), coh->cookie,
		DP_UUID(cont->dc_cont_hdl));

out_cont:
	dc_cont_put(cont);
out_pool:
	dc_pool_put(pool);
out:
	return rc;
}

int
dc_cont_global2local(daos_handle_t poh, d_iov_t glob, daos_handle_t *coh)
{
	struct dc_cont_glob	*cont_glob;
	int			 rc = 0;

	if (glob.iov_buf == NULL || glob.iov_buf_len < glob.iov_len ||
	    glob.iov_len != dc_cont_glob_buf_size()) {
		D_DEBUG(DF_DSMC, "Invalid parameter of glob, iov_buf %p, "
			"iov_buf_len "DF_U64", iov_len "DF_U64".\n",
			glob.iov_buf, glob.iov_buf_len, glob.iov_len);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (coh == NULL) {
		D_DEBUG(DF_DSMC, "Invalid parameter, NULL coh.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	cont_glob = (struct dc_cont_glob *)glob.iov_buf;
	if (cont_glob->dcg_magic == D_SWAP32(DC_CONT_GLOB_MAGIC)) {
		swap_co_glob(cont_glob);
		D_ASSERT(cont_glob->dcg_magic == DC_CONT_GLOB_MAGIC);

	} else if (cont_glob->dcg_magic != DC_CONT_GLOB_MAGIC) {
		D_ERROR("Bad hgh_magic: 0x%x.\n", cont_glob->dcg_magic);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (uuid_is_null(cont_glob->dcg_pool_hdl) ||
	    uuid_is_null(cont_glob->dcg_uuid) ||
	    uuid_is_null(cont_glob->dcg_cont_hdl)) {
		D_ERROR("Invalid parameter, pool_hdl/uuid/cont_hdl is null.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dc_cont_g2l(poh, cont_glob, coh);
	if (rc != 0)
		D_ERROR("dc_cont_g2l failed, rc: "DF_RC"\n", DP_RC(rc));

out:
	return rc;
}

struct cont_req_arg {
	struct dc_pool	*cra_pool;
	struct dc_cont	*cra_cont;
	crt_rpc_t	*cra_rpc;
	crt_bulk_t	 cra_bulk;
	tse_task_cb_t	 cra_callback;
};

enum creq_cleanup_stage {
	CLEANUP_ALL,
	CLEANUP_BULK,
	CLEANUP_RPC,
	CLEANUP_POOL,
	CLEANUP_CONT,
};

static void
cont_req_cleanup(enum creq_cleanup_stage stage, struct cont_req_arg *args)
{
	switch (stage) {
	case CLEANUP_ALL:
		crt_req_decref(args->cra_rpc);
	case CLEANUP_BULK:
		if (args->cra_bulk)
			crt_bulk_free(args->cra_bulk);
	case CLEANUP_RPC:
		crt_req_decref(args->cra_rpc);
	case CLEANUP_POOL:
		dc_pool_put(args->cra_pool);
	case CLEANUP_CONT:
		dc_cont_put(args->cra_cont);
	}
}

static int
cont_req_complete(tse_task_t *task, void *data)
{
	struct cont_req_arg	*args = data;
	struct dc_pool		*pool	 = args->cra_pool;
	struct dc_cont		*cont	 = args->cra_cont;
	struct cont_op_out	*op_out	 = crt_reply_get(args->cra_rpc);
	int			 rc	 = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(pool, &args->cra_rpc->cr_ep,
					   rc, op_out, task);
	if (rc < 0)
		D_GOTO(out, rc);
	else if (rc == RSVC_CLIENT_RECHOOSE)
		D_GOTO(out, rc = 0);

	if (rc != 0) {
		D_ERROR("RPC error while querying container: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = op_out->co_rc;
	if (rc != 0) {
		D_DEBUG(DF_DSMC, DF_CONT": failed to access container: %d\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, DF_CONT": Accessed: using hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

	if (args->cra_callback != NULL)
		args->cra_callback(task, data);
out:
	cont_req_cleanup(CLEANUP_BULK, args);
	return rc;
}

static int
cont_req_prepare(daos_handle_t coh, enum cont_operation opcode,
		 crt_context_t *ctx, struct cont_req_arg *args)
{
	struct cont_op_in *in;
	crt_endpoint_t	   ep;
	int		   rc;

	memset(args, 0, sizeof(*args));
	args->cra_cont = dc_hdl2cont(coh);
	if (args->cra_cont == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);
	args->cra_pool = dc_hdl2pool(args->cra_cont->dc_pool_hdl);
	D_ASSERT(args->cra_pool != NULL);

	ep.ep_grp  = args->cra_pool->dp_sys->sy_group;
	D_MUTEX_LOCK(&args->cra_pool->dp_client_lock);
	rc = rsvc_client_choose(&args->cra_pool->dp_client, &ep);
	D_MUTEX_UNLOCK(&args->cra_pool->dp_client_lock);
	if (rc != 0) {
		D_ERROR(DF_CONT": cannot find container service: "DF_RC"\n",
			DP_CONT(args->cra_pool->dp_pool,
				args->cra_cont->dc_uuid), DP_RC(rc));
		cont_req_cleanup(CLEANUP_POOL, args);
		goto out;
	}

	rc = cont_req_create(ctx, &ep, opcode, &args->cra_rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		cont_req_cleanup(CLEANUP_POOL, args);
		D_GOTO(out, rc);
	}

	in = crt_req_get(args->cra_rpc);
	uuid_copy(in->ci_pool_hdl, args->cra_pool->dp_pool_hdl);
	uuid_copy(in->ci_uuid, args->cra_cont->dc_uuid);
	uuid_copy(in->ci_hdl, args->cra_cont->dc_cont_hdl);
out:
	return rc;
}

static int
attr_list_req_complete(tse_task_t *task, void *data)
{
	struct cont_req_arg	  *args = data;
	daos_cont_list_attr_t	  *task_args = dc_task_get_args(task);
	struct cont_attr_list_out *out = crt_reply_get(args->cra_rpc);

	*task_args->size = out->calo_size;
	return 0;
}

int
dc_cont_list_attr(tse_task_t *task)
{
	daos_cont_list_attr_t		*args;
	struct cont_attr_list_in	*in;
	struct cont_req_arg		 cb_args;
	int				 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (args->size == NULL ||
	    (*args->size > 0 && args->buf == NULL)) {
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = cont_req_prepare(args->coh, CONT_ATTR_LIST,
			     daos_task2ctx(task), &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DF_DSMC, DF_CONT": listing attributes: hdl="
			 DF_UUID "; size=%lu\n",
		DP_CONT(cb_args.cra_pool->dp_pool_hdl,
			cb_args.cra_cont->dc_uuid),
		DP_UUID(cb_args.cra_cont->dc_cont_hdl), *args->size);

	in = crt_req_get(cb_args.cra_rpc);
	if (*args->size > 0) {
		d_iov_t iov = {
			.iov_buf     = args->buf,
			.iov_buf_len = *args->size,
			.iov_len     = 0
		};
		d_sg_list_t sgl = {
			.sg_nr_out = 0,
			.sg_nr	   = 1,
			.sg_iovs   = &iov
		};
		rc = crt_bulk_create(daos_task2ctx(task), &sgl,
				     CRT_BULK_RW, &in->cali_bulk);
		if (rc != 0) {
			cont_req_cleanup(CLEANUP_RPC, &cb_args);
			D_GOTO(out, rc);
		}
	}

	cb_args.cra_bulk = in->cali_bulk;
	cb_args.cra_callback = attr_list_req_complete;
	rc = tse_task_register_comp_cb(task, cont_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_BULK, &cb_args);
		D_GOTO(out, rc);
	}

	crt_req_addref(cb_args.cra_rpc);
	rc = daos_rpc_send(cb_args.cra_rpc, task);
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_ALL, &cb_args);
		D_GOTO(out, rc);
	}

	return rc;
out:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "Failed to list container attributes: "DF_RC"\n",
		DP_RC(rc));
	return rc;
}

static int
attr_bulk_create(int n, char *names[], void *values[], size_t sizes[],
		 crt_context_t crt_ctx, crt_bulk_perm_t perm, crt_bulk_t *bulk)
{
	int		rc;
	int		i;
	int		j;
	d_sg_list_t	sgl;

	/* Buffers = 'n' names */
	sgl.sg_nr_out	= 0;
	sgl.sg_nr	= n;

	/* + 1 sizes */
	if (sizes != NULL)
		++sgl.sg_nr;

	/* + non-null values */
	if (sizes != NULL && values != NULL) {
		for (j = 0; j < n; j++)
			if (sizes[j] > 0)
				++sgl.sg_nr;
	}

	D_ALLOC_ARRAY(sgl.sg_iovs, sgl.sg_nr);
	if (sgl.sg_iovs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* names */
	for (j = 0, i = 0; j < n; ++j)
		d_iov_set(&sgl.sg_iovs[i++], (void *)(names[j]),
			     strlen(names[j]) + 1 /* trailing '\0' */);

	/* TODO: Add packing/unpacking of non-byte-arrays to rpc.[hc] ? */

	/* sizes */
	if (sizes != NULL)
		d_iov_set(&sgl.sg_iovs[i++], (void *)sizes, n * sizeof(*sizes));

	/* values */
	if (sizes != NULL && values != NULL) {
		for (j = 0; j < n; ++j)
			if (sizes[j] > 0)
				d_iov_set(&sgl.sg_iovs[i++],
					  values[j], sizes[j]);
	}

	rc = crt_bulk_create(crt_ctx, &sgl, perm, bulk);
	D_FREE(sgl.sg_iovs);
out:
	return rc;
}

/*
 * Check for valid inputs. If readonly is true, normalizes
 * by setting corresponding size to zero for NULL values.
 * Otherwise, values may not be NULL.
 */
static int
attr_check_input(int n, char const *const names[], void const *const values[],
		 size_t sizes[], bool readonly)
{
	int i;

	if (n <= 0 || names == NULL || ((sizes == NULL
	    || values == NULL) && !readonly)) {
		D_ERROR("Invalid Arguments: n = %d, names = %p, values = %p"
			", sizes = %p", n, names, values, sizes);
		return -DER_INVAL;
	}

	for (i = 0; i < n; i++) {
		if (names[i] == NULL || *(names[i]) == '\0') {
			D_ERROR("Invalid Arguments: names[%d] = %s",
				i, names[i] == NULL ? "NULL" : "\'\\0\'");

			return -DER_INVAL;
		}
		if (sizes != NULL) {
			if (values == NULL)
				sizes[i] = 0;
			else if (values[i] == NULL || sizes[i] == 0) {
				if (!readonly) {
					D_ERROR("Invalid Arguments: values[%d] = %p, sizes[%d] = %lu",
						i, values[i], i, sizes[i]);
					return -DER_INVAL;
				}
				sizes[i] = 0;
			}
		}
	}
	return 0;
}

int
dc_cont_get_attr(tse_task_t *task)
{
	daos_cont_get_attr_t	*args;
	struct cont_attr_get_in	*in;
	struct cont_req_arg	 cb_args;
	int			 rc;
	int			 i;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	rc = attr_check_input(args->n, args->names,
			      (const void *const*) args->values,
			      (size_t *)args->sizes, true);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = cont_req_prepare(args->coh, CONT_ATTR_GET,
			     daos_task2ctx(task), &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DF_DSMC, DF_CONT": getting attributes: hdl="DF_UUID"\n",
		DP_CONT(cb_args.cra_pool->dp_pool_hdl,
			cb_args.cra_cont->dc_uuid),
		DP_UUID(cb_args.cra_cont->dc_cont_hdl));

	in = crt_req_get(cb_args.cra_rpc);
	in->cagi_count = args->n;
	for (i = 0, in->cagi_key_length = 0; i < args->n; i++)
		in->cagi_key_length += strlen(args->names[i]) + 1;

	rc = attr_bulk_create(args->n, (char **)args->names,
			      (void **)args->values, (size_t *)args->sizes,
			      daos_task2ctx(task), CRT_BULK_RW, &in->cagi_bulk);
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_RPC, &cb_args);
		D_GOTO(out, rc);
	}

	cb_args.cra_bulk = in->cagi_bulk;
	rc = tse_task_register_comp_cb(task, cont_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_BULK, &cb_args);
		D_GOTO(out, rc);
	}

	crt_req_addref(cb_args.cra_rpc);
	rc = daos_rpc_send(cb_args.cra_rpc, task);
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_ALL, &cb_args);
		D_GOTO(out, rc);
	}

	return rc;
out:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "Failed to get container attributes: "DF_RC"\n",
		DP_RC(rc));
	return rc;
}

int
dc_cont_set_attr(tse_task_t *task)
{
	daos_cont_set_attr_t	*args;
	struct cont_attr_set_in	*in;
	struct cont_req_arg	 cb_args;
	int			 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	rc = attr_check_input(args->n, args->names, args->values,
			      (size_t *)args->sizes, false);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = cont_req_prepare(args->coh, CONT_ATTR_SET,
			     daos_task2ctx(task), &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DF_DSMC, DF_CONT": setting attributes: hdl="DF_UUID"\n",
		DP_CONT(cb_args.cra_pool->dp_pool_hdl,
			cb_args.cra_cont->dc_uuid),
		DP_UUID(cb_args.cra_cont->dc_cont_hdl));

	in = crt_req_get(cb_args.cra_rpc);
	in->casi_count = args->n;
	rc = attr_bulk_create(args->n, (char **)args->names,
			      (void **)args->values, (size_t *)args->sizes,
			      daos_task2ctx(task), CRT_BULK_RO, &in->casi_bulk);
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_RPC, &cb_args);
		D_GOTO(out, rc);
	}

	cb_args.cra_bulk = in->casi_bulk;
	rc = tse_task_register_comp_cb(task, cont_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_BULK, &cb_args);
		D_GOTO(out, rc);
	}

	crt_req_addref(cb_args.cra_rpc);
	rc = daos_rpc_send(cb_args.cra_rpc, task);
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_ALL, &cb_args);
		D_GOTO(out, rc);
	}

	return rc;
out:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "Failed to set container attributes: "DF_RC"\n",
		DP_RC(rc));
	return rc;
}

int
dc_cont_del_attr(tse_task_t *task)
{
	daos_cont_set_attr_t	*args;
	struct cont_attr_del_in	*in;
	struct cont_req_arg	 cb_args;
	int			 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	rc = attr_check_input(args->n, args->names, NULL, NULL, true);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = cont_req_prepare(args->coh, CONT_ATTR_DEL,
			      daos_task2ctx(task), &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DF_DSMC, DF_CONT": deleting attributes: hdl="DF_UUID"\n",
		DP_CONT(cb_args.cra_pool->dp_pool_hdl,
			cb_args.cra_cont->dc_uuid),
		DP_UUID(cb_args.cra_cont->dc_cont_hdl));

	in = crt_req_get(cb_args.cra_rpc);
	in->cadi_count = args->n;
	rc = attr_bulk_create(args->n, (char **)args->names, NULL, NULL,
			      daos_task2ctx(task), CRT_BULK_RO, &in->cadi_bulk);
	if (rc != 0)
		D_GOTO(cleanup, rc);

	cb_args.cra_bulk = in->cadi_bulk;
	rc = tse_task_register_comp_cb(task, cont_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0)
		D_GOTO(cleanup, rc);

	crt_req_addref(cb_args.cra_rpc);
	rc = daos_rpc_send(cb_args.cra_rpc, task);
	if (rc != 0)
		D_GOTO(cleanup, rc);

	return rc;
cleanup:
	cont_req_cleanup(CLEANUP_BULK, &cb_args);
out:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "Failed to del container attributes: "DF_RC"\n",
		DP_RC(rc));
	return rc;
}

struct epoch_op_arg {
	/* eoa_req must always be the first member of epoch_op_arg */
	struct cont_req_arg	 eoa_req;
	daos_epoch_t		*eoa_epoch;
};

static int
cont_epoch_op_req_complete(tse_task_t *task, void *data)
{
	struct epoch_op_arg *arg = data;
	struct cont_epoch_op_out *op_out;
	int rc;

	rc = cont_req_complete(task, &arg->eoa_req);
	if (rc)
		return rc;

	op_out = crt_reply_get(arg->eoa_req.cra_rpc);

	*arg->eoa_epoch = op_out->ceo_epoch;

	return 0;
}

int
dc_epoch_op(daos_handle_t coh, crt_opcode_t opc, daos_epoch_t *epoch,
	    tse_task_t *task)
{
	struct cont_epoch_op_in	*in;
	struct epoch_op_arg	 arg;
	int			 rc;

	/* Check incoming arguments. */
	D_ASSERT(epoch != NULL);
	if (*epoch >= DAOS_EPOCH_MAX)
		D_GOTO(out, rc = -DER_OVERFLOW);

	rc = cont_req_prepare(coh, opc, daos_task2ctx(task), &arg.eoa_req);
	if (rc != 0)
		goto out;

	D_DEBUG(DF_DSMC, DF_CONT": op=%u; hdl="DF_UUID"; epoch="DF_U64"\n",
		DP_CONT(arg.eoa_req.cra_pool->dp_pool_hdl,
			arg.eoa_req.cra_cont->dc_uuid), opc,
		DP_UUID(arg.eoa_req.cra_cont->dc_cont_hdl), *epoch);

	in = crt_req_get(arg.eoa_req.cra_rpc);
	in->cei_epoch = *epoch;

	arg.eoa_epoch = epoch;

	rc = tse_task_register_comp_cb(task, cont_epoch_op_req_complete,
				       &arg, sizeof(arg));
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_ALL, &arg.eoa_req);
		goto out;
	}

	crt_req_addref(arg.eoa_req.cra_rpc);
	rc = daos_rpc_send(arg.eoa_req.cra_rpc, task);
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_ALL, &arg.eoa_req);
		goto out;
	}
	return rc;
out:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "epoch op %u("DF_U64") failed: %d\n", opc, *epoch, rc);
	return rc;
}

int
dc_cont_aggregate(tse_task_t *task)
{
	daos_cont_aggregate_t	*args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (args->epoch == DAOS_EPOCH_MAX) {
		D_ERROR("Invalid epoch: "DF_U64"\n", args->epoch);
		tse_task_complete(task, -DER_INVAL);
		return -DER_INVAL;
	}

	return dc_epoch_op(args->coh, CONT_EPOCH_AGGREGATE, &args->epoch, task);
}

int
dc_cont_rollback(tse_task_t *task)
{
	D_ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_cont_subscribe(tse_task_t *task)
{
	D_ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_cont_create_snap(tse_task_t *task)
{
	daos_cont_create_snap_t *args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (args->name != NULL) {
		D_ERROR("Named Snapshots not yet supported\n");
		tse_task_complete(task, -DER_NOSYS);
		return -DER_NOSYS;
	}

	if (args->epoch == NULL) {
		tse_task_complete(task, -DER_INVAL);
		return -DER_INVAL;
	}

	*args->epoch = crt_hlc_get();
	return dc_epoch_op(args->coh, CONT_SNAP_CREATE, args->epoch, task);
}

int
dc_cont_destroy_snap(tse_task_t *task)
{
	daos_cont_destroy_snap_t	*args;
	int				rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (args->epr.epr_lo > args->epr.epr_hi) {
		D_ERROR("Invalid epoch range.\n");
		D_GOTO(err, rc = -DER_INVAL);
	}

	/** TODO - add support for valid epoch ranges. */
	if (args->epr.epr_lo != args->epr.epr_hi || args->epr.epr_lo == 0) {
		D_ERROR("Unsupported epoch range.\n");
		D_GOTO(err, rc = -DER_INVAL);
	}

	return dc_epoch_op(args->coh, CONT_SNAP_DESTROY, &args->epr.epr_lo,
			   task);

err:
	tse_task_complete(task, -DER_INVAL);
	return rc;
}

static int
snap_list_req_complete(tse_task_t *task, void *data)
{
	struct cont_req_arg	  *args = data;
	daos_cont_list_snap_t	  *task_args = dc_task_get_args(task);
	struct cont_snap_list_out *out = crt_reply_get(args->cra_rpc);

	*task_args->nr = out->slo_count;
	task_args->anchor->da_type = DAOS_ANCHOR_TYPE_EOF;

	return 0;
}

int
dc_cont_list_snap(tse_task_t *task)
{
	daos_cont_list_snap_t		*args;
	struct cont_snap_list_in	*in;
	struct cont_req_arg		 cb_args;
	int				 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (args->nr == NULL)
		D_GOTO(out, rc = -DER_INVAL);
	if (args->epochs == NULL || *args->nr < 0)
		*args->nr = 0;

	rc = cont_req_prepare(args->coh, CONT_SNAP_LIST,
			     daos_task2ctx(task), &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DF_DSMC, DF_CONT": listing snapshots: hdl="
			 DF_UUID "; size=%d\n",
		DP_CONT(cb_args.cra_pool->dp_pool_hdl,
			cb_args.cra_cont->dc_uuid),
		DP_UUID(cb_args.cra_cont->dc_cont_hdl), *args->nr);

	in = crt_req_get(cb_args.cra_rpc);
	if (*args->nr > 0) {
		d_iov_t iov = {
			.iov_buf     = args->epochs,
			.iov_buf_len = *args->nr * sizeof(*args->epochs),
			.iov_len     = 0
		};
		d_sg_list_t sgl = {
			.sg_nr_out = 0,
			.sg_nr	   = 1,
			.sg_iovs   = &iov
		};
		rc = crt_bulk_create(daos_task2ctx(task), &sgl,
				     CRT_BULK_RW, &in->sli_bulk);
		if (rc != 0) {
			cont_req_cleanup(CLEANUP_RPC, &cb_args);
			D_GOTO(out, rc);
		}
	}

	cb_args.cra_bulk = in->sli_bulk;
	cb_args.cra_callback = snap_list_req_complete;
	rc = tse_task_register_comp_cb(task, cont_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_BULK, &cb_args);
		D_GOTO(out, rc);
	}

	crt_req_addref(cb_args.cra_rpc);
	rc = daos_rpc_send(cb_args.cra_rpc, task);
	if (rc != 0) {
		cont_req_cleanup(CLEANUP_ALL, &cb_args);
		D_GOTO(out, rc);
	}

	return rc;
out:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "Failed to list container snapshots: "DF_RC"\n",
		DP_RC(rc));
	return rc;
}

/**
 * Get pool_target by container handle and target index.
 *
 * \param coh [IN]	container handle.
 * \param tgt_idx [IN]	target index.
 * \param tgt [OUT]	pool target pointer.
 *
 * \return		0 if get the pool_target.
 * \return		errno if it does not get the pool_target.
 */
int
dc_cont_tgt_idx2ptr(daos_handle_t coh, uint32_t tgt_idx,
		    struct pool_target **tgt)
{
	struct dc_cont	*dc;
	struct dc_pool	*pool;
	int		 n;

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return -DER_NO_HDL;

	/* Get map_tgt so that we can have the rank of the target. */
	pool = dc_hdl2pool(dc->dc_pool_hdl);
	D_ASSERT(pool != NULL);
	D_RWLOCK_RDLOCK(&pool->dp_map_lock);
	n = pool_map_find_target(pool->dp_map, tgt_idx, tgt);
	D_RWLOCK_UNLOCK(&pool->dp_map_lock);
	dc_pool_put(pool);
	dc_cont_put(dc);
	if (n != 1) {
		D_ERROR("failed to find target %u\n", tgt_idx);
		return -DER_INVAL;
	}
	return 0;
}

/**
 * Get pool_domain by container handle and node id.
 *
 * \param coh [IN]	container handle.
 * \param node_id [IN]	node id.
 * \param dom [OUT]	pool domain pointer.
 *
 * \return		0 if get the pool_target.
 * \return		errno if it does not get the pool_target.
 */
int
dc_cont_node_id2ptr(daos_handle_t coh, uint32_t node_id,
		    struct pool_domain **dom)
{
	struct dc_cont	*dc;
	struct dc_pool	*pool;
	int		 n;

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return -DER_NO_HDL;

	/* Get node so that we can have the rank of the target. */
	pool = dc_hdl2pool(dc->dc_pool_hdl);
	D_ASSERT(pool != NULL);
	D_RWLOCK_RDLOCK(&pool->dp_map_lock);
	n = pool_map_find_nodes(pool->dp_map, node_id, dom);
	D_RWLOCK_UNLOCK(&pool->dp_map_lock);
	dc_pool_put(pool);
	dc_cont_put(dc);
	if (n != 1) {
		D_ERROR("failed to find target %u\n", node_id);
		return -DER_INVAL;
	}
	return 0;
}

int
dc_cont_hdl2uuid(daos_handle_t coh, uuid_t *hdl_uuid, uuid_t *uuid)
{
	struct dc_cont *dc;

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return -DER_NO_HDL;

	if (hdl_uuid != NULL)
		uuid_copy(*hdl_uuid, dc->dc_cont_hdl);
	if (uuid != NULL)
		uuid_copy(*uuid, dc->dc_uuid);
	dc_cont_put(dc);
	return 0;
}

daos_handle_t
dc_cont_hdl2pool_hdl(daos_handle_t coh)
{
	struct dc_cont	*dc;
	daos_handle_t	 ph;

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return DAOS_HDL_INVAL;

	ph = dc->dc_pool_hdl;
	dc_cont_put(dc);
	return ph;
}
struct daos_csummer *
dc_cont_hdl2csummer(daos_handle_t coh)
{
	struct dc_cont	*dc;
	struct daos_csummer *csum;

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return NULL;

	csum = dc->dc_csummer;
	dc_cont_put(dc);

	return csum;

}

struct cont_props
dc_cont_hdl2props(daos_handle_t coh)
{
	struct dc_cont	*dc = NULL;
	struct cont_props result = {0};

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return result;

	result = dc->dc_props;
	dc_cont_put(dc);

	return result;

}
