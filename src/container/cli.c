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
 * dc_cont: Container Client
 *
 * This module is part of libdaos. It implements the container methods of DAOS
 * API as well as daos/container.h.
 */
#define DD_SUBSYS	DD_FAC(container)

#include <daos_task.h>
#include <daos_types.h>
#include <daos/container.h>
#include <daos/event.h>
#include <daos/pool.h>
#include <daos/rsvc.h>
#include "cli_internal.h"
#include "rpc.h"

/**
 * Initialize container interface
 */
int
dc_cont_init(void)
{
	return daos_rpc_register(cont_rpcs, NULL, DAOS_CONT_MODULE);
}

/**
 * Finalize container interface
 */
void
dc_cont_fini(void)
{
	daos_rpc_unregister(cont_rpcs);
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
			      struct daos_task *task)
{
	int rc;

	pthread_mutex_lock(&pool->dp_client_lock);
	rc = rsvc_client_complete_rpc(&pool->dp_client, ep, rc_crt, out->co_rc,
				      &out->co_hint);
	pthread_mutex_unlock(&pool->dp_client_lock);
	if (rc == RSVC_CLIENT_RECHOOSE ||
	    (rc == RSVC_CLIENT_PROCEED && daos_rpc_retryable_rc(out->co_rc))) {
		task->dt_result = 0;
		rc = daos_task_reinit(task);
		if (rc != 0)
			return rc;
		return RSVC_CLIENT_RECHOOSE;
	}
	return RSVC_CLIENT_PROCEED;
}

struct cont_args {
	struct dc_pool		*pool;
	crt_rpc_t		*rpc;
};

static int
cont_create_complete(struct daos_task *task, void *data)
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
		D_ERROR("RPC error while creating container: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = out->cco_op.co_rc;
	if (rc != 0) {
		D_DEBUG(DF_DSMC, "failed to create container: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, "completed creating container\n");

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(pool);
	return rc;
}

int
dc_cont_create(struct daos_task *task)
{
	daos_cont_create_t     *args;
	struct cont_create_in  *in;
	struct dc_pool	       *pool;
	crt_endpoint_t		ep;
	crt_rpc_t	       *rpc;
	struct cont_args	arg;
	int			rc;

	args = daos_task_get_args(DAOS_OPC_CONT_CREATE, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (uuid_is_null(args->uuid))
		D_GOTO(err_task, rc = -DER_INVAL);

	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(err_task, rc = -DER_NO_HDL);

	if (!(pool->dp_capas & DAOS_PC_RW) && !(pool->dp_capas & DAOS_PC_EX))
		D_GOTO(err_pool, rc = -DER_NO_PERM);

	D_DEBUG(DF_DSMC, DF_UUID": creating "DF_UUIDF"\n",
		DP_UUID(pool->dp_pool), DP_UUID(args->uuid));

	ep.ep_grp = pool->dp_group;
	pthread_mutex_lock(&pool->dp_client_lock);
	rsvc_client_choose(&pool->dp_client, &ep);
	pthread_mutex_unlock(&pool->dp_client_lock);
	rc = cont_req_create(daos_task2ctx(task), ep, CONT_CREATE, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		D_GOTO(err_pool, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->cci_op.ci_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(in->cci_op.ci_uuid, args->uuid);

	arg.pool = pool;
	arg.rpc = rpc;
	crt_req_addref(rpc);

	rc = daos_task_register_comp_cb(task, cont_create_complete, &arg,
					sizeof(arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_pool:
	dc_pool_put(pool);
err_task:
	daos_task_complete(task, rc);
	return rc;
}

static int
cont_destroy_complete(struct daos_task *task, void *data)
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
		D_ERROR("RPC error while destroying container: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = out->cdo_op.co_rc;
	if (rc != 0) {
		D_DEBUG(DF_DSMC, "failed to destroy container: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, "completed destroying container\n");

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(pool);
	return rc;
}

int
dc_cont_destroy(struct daos_task *task)
{
	daos_cont_destroy_t	*args;
	struct cont_destroy_in	*in;
	struct dc_pool		*pool;
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	struct cont_args	 arg;
	int			 rc;

	args = daos_task_get_args(DAOS_OPC_CONT_DESTROY, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	/* TODO: Implement "force". */
	D_ASSERT(args->force != 0);

	if (uuid_is_null(args->uuid))
		D_GOTO(err, rc = -DER_INVAL);

	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	if (!(pool->dp_capas & DAOS_PC_RW) && !(pool->dp_capas & DAOS_PC_EX))
		D_GOTO(err_pool, rc = -DER_NO_PERM);

	D_DEBUG(DF_DSMC, DF_UUID": destroying "DF_UUID": force=%d\n",
		DP_UUID(pool->dp_pool), DP_UUID(args->uuid), args->force);

	ep.ep_grp = pool->dp_group;
	pthread_mutex_lock(&pool->dp_client_lock);
	rsvc_client_choose(&pool->dp_client, &ep);
	pthread_mutex_unlock(&pool->dp_client_lock);
	rc = cont_req_create(daos_task2ctx(task), ep, CONT_DESTROY, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		D_GOTO(err_pool, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->cdi_op.ci_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(in->cdi_op.ci_uuid, args->uuid);
	in->cdi_force = args->force;

	arg.pool = pool;
	arg.rpc = rpc;
	crt_req_addref(rpc);

	rc = daos_task_register_comp_cb(task, cont_destroy_complete, &arg,
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
	daos_task_complete(task, rc);
	return rc;
}

static void
dc_cont_free(struct dc_cont *dc)
{
	pthread_rwlock_destroy(&dc->dc_obj_list_lock);
	D_ASSERT(daos_list_empty(&dc->dc_po_list));
	D_ASSERT(daos_list_empty(&dc->dc_obj_list));
	D_FREE_PTR(dc);
}

void
dc_cont_put(struct dc_cont *dc)
{
	D_ASSERT(dc->dc_ref > 0);
	if (--dc->dc_ref == 0)
		dc_cont_free(dc);
}

static struct dc_cont *
dc_cont_alloc(const uuid_t uuid)
{
	struct dc_cont *dc;

	D_ALLOC_PTR(dc);
	if (dc == NULL)
		return NULL;

	uuid_copy(dc->dc_uuid, uuid);
	DAOS_INIT_LIST_HEAD(&dc->dc_obj_list);
	DAOS_INIT_LIST_HEAD(&dc->dc_po_list);
	pthread_rwlock_init(&dc->dc_obj_list_lock, NULL);
	dc->dc_ref = 1;

	return dc;
}

struct cont_open_args {
	struct dc_pool		*coa_pool;
	daos_cont_info_t	*coa_info;
	crt_rpc_t		*rpc;
	daos_handle_t		 hdl;
	daos_handle_t		*hdlp;
};

static int
cont_open_complete(struct daos_task *task, void *data)
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
		D_ERROR("RPC error while opening container: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = out->coo_op.co_rc;
	if (rc != 0) {
		D_DEBUG(DF_DSMC, DF_CONT": failed to open container: %d\n",
			DP_CONT(pool->dp_pool, cont->dc_uuid), rc);
		D_GOTO(out, rc);
	}

	pthread_rwlock_wrlock(&pool->dp_co_list_lock);
	if (pool->dp_disconnecting) {
		pthread_rwlock_unlock(&pool->dp_co_list_lock);
		D_ERROR("pool connection being invalidated\n");
		/*
		 * Instead of sending a CONT_CLOSE RPC, we leave this new
		 * container handle on the server side to the POOL_DISCONNECT
		 * effort we are racing with.
		 */
		D_GOTO(out, rc = -DER_NO_HDL);
	}

	daos_list_add(&cont->dc_po_list, &pool->dp_co_list);
	cont->dc_pool_hdl = arg->hdl;
	pthread_rwlock_unlock(&pool->dp_co_list_lock);

	dc_cont2hdl(cont, arg->hdlp);

	D_DEBUG(DF_DSMC, DF_CONT": opened: cookie="DF_X64" hdl="DF_UUID
		" master\n", DP_CONT(pool->dp_pool, cont->dc_uuid),
		arg->hdlp->cookie, DP_UUID(cont->dc_cont_hdl));

	if (arg->coa_info == NULL)
		D_GOTO(out, rc = 0);

	uuid_copy(arg->coa_info->ci_uuid, cont->dc_uuid);
	arg->coa_info->ci_epoch_state = out->coo_epoch_state;
	/* TODO */
	arg->coa_info->ci_nsnapshots = 0;
	arg->coa_info->ci_snapshots = NULL;

out:
	crt_req_decref(arg->rpc);
	if (put_cont)
		dc_cont_put(cont);
	dc_pool_put(pool);
	return rc;
}

int
dc_cont_local_close(daos_handle_t ph, daos_handle_t coh)
{
	struct dc_cont *cont = NULL;
	struct dc_pool *pool = NULL;
	int		rc = 0;

	cont = dc_hdl2cont(coh);
	if (cont == NULL)
		return 0;

	pool = dc_hdl2pool(ph);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	dc_cont_put(cont);

	/* Remove the container from pool container list */
	pthread_rwlock_wrlock(&pool->dp_co_list_lock);
	daos_list_del_init(&cont->dc_po_list);
	pthread_rwlock_unlock(&pool->dp_co_list_lock);

out:
	if (cont != NULL)
		dc_cont_put(cont);
	if (pool != NULL)
		dc_pool_put(pool);

	return rc;
}

int
dc_cont_local_open(uuid_t cont_uuid, uuid_t cont_hdl_uuid,
		   unsigned int flags, daos_handle_t ph,
		   daos_handle_t *coh)
{
	struct dc_cont	*cont = NULL;
	struct dc_pool	*pool = NULL;
	int		rc = 0;

	if (!daos_handle_is_inval(*coh)) {
		cont = dc_hdl2cont(*coh);
		if (cont != NULL)
			D_GOTO(out, rc);
	}

	cont = dc_cont_alloc(cont_uuid);
	if (cont == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ASSERT(!daos_handle_is_inval(ph));
	pool = dc_hdl2pool(ph);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	uuid_copy(cont->dc_cont_hdl, cont_hdl_uuid);
	cont->dc_capas = flags;

	pthread_rwlock_wrlock(&pool->dp_co_list_lock);
	daos_list_add(&cont->dc_po_list, &pool->dp_co_list);
	cont->dc_pool_hdl = ph;
	pthread_rwlock_unlock(&pool->dp_co_list_lock);

	dc_cont2hdl(cont, coh);
out:
	if (cont != NULL)
		dc_cont_put(cont);
	if (pool != NULL)
		dc_pool_put(pool);

	return rc;
}

int
dc_cont_open(struct daos_task *task)
{
	daos_cont_open_t	*args;
	struct cont_open_in	*in;
	struct dc_pool		*pool;
	struct dc_cont		*cont;
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	struct cont_open_args	 arg;
	int			 rc;

	args = daos_task_get_args(DAOS_OPC_CONT_OPEN, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");
	cont = daos_task_get_priv(task);

	if (uuid_is_null(args->uuid) || args->coh == NULL)
		D_GOTO(err, rc = -DER_INVAL);

	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	if ((args->flags & DAOS_COO_RW) && (pool->dp_capas & DAOS_PC_RO))
		D_GOTO(err_pool, rc = -DER_NO_PERM);

	if (cont == NULL) {
		cont = dc_cont_alloc(args->uuid);
		if (cont == NULL)
			D_GOTO(err_pool, rc = -DER_NOMEM);
		uuid_generate(cont->dc_cont_hdl);
		cont->dc_capas = args->flags;
		daos_task_set_priv(task, cont);
	}

	D_DEBUG(DF_DSMC, DF_CONT": opening: hdl="DF_UUIDF" flags=%x\n",
		DP_CONT(pool->dp_pool, args->uuid), DP_UUID(cont->dc_cont_hdl),
		args->flags);

	ep.ep_grp = pool->dp_group;
	pthread_mutex_lock(&pool->dp_client_lock);
	rsvc_client_choose(&pool->dp_client, &ep);
	pthread_mutex_unlock(&pool->dp_client_lock);
	rc = cont_req_create(daos_task2ctx(task), ep, CONT_OPEN, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		D_GOTO(err_cont, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->coi_op.ci_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(in->coi_op.ci_uuid, args->uuid);
	uuid_copy(in->coi_op.ci_hdl, cont->dc_cont_hdl);
	in->coi_capas = args->flags;

	arg.coa_pool = pool;
	arg.coa_info = args->info;
	arg.rpc = rpc;
	arg.hdl = args->poh;
	arg.hdlp = args->coh;

	crt_req_addref(rpc);

	rc = daos_task_register_comp_cb(task, cont_open_complete, &arg,
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
	daos_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "failed to open container: %d\n", rc);
	return rc;
}

struct cont_close_args {
	struct dc_pool	*cca_pool;
	struct dc_cont	*cca_cont;
	crt_rpc_t	*rpc;
	daos_handle_t	 hdl;
};

static int
cont_close_complete(struct daos_task *task, void *data)
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
		D_ERROR("RPC error while closing container: %d\n", rc);
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
	} else if (rc != 0) {
		D_ERROR("failed to close container: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, DF_CONT": closed: cookie="DF_X64" hdl="DF_UUID
		" master\n", DP_CONT(pool->dp_pool, cont->dc_uuid),
		arg->hdl.cookie, DP_UUID(cont->dc_cont_hdl));

	dc_cont_put(cont);

	/* Remove the container from pool container list */
	pthread_rwlock_wrlock(&pool->dp_co_list_lock);
	daos_list_del_init(&cont->dc_po_list);
	pthread_rwlock_unlock(&pool->dp_co_list_lock);

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(pool);
	dc_cont_put(cont);
	return rc;
}

int
dc_cont_close(struct daos_task *task)
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

	args = daos_task_get_args(DAOS_OPC_CONT_CLOSE, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");
	coh = args->coh;

	cont = dc_hdl2cont(coh);
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

	pool = dc_hdl2pool(cont->dc_pool_hdl);
	D_ASSERT(pool != NULL);

	D_DEBUG(DF_DSMC, DF_CONT": closing: cookie="DF_X64" hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid), coh.cookie,
		DP_UUID(cont->dc_cont_hdl));

	if (cont->dc_slave) {
		dc_cont_put(cont);

		/* Remove the container from pool container list */
		pthread_rwlock_wrlock(&pool->dp_co_list_lock);
		daos_list_del_init(&cont->dc_po_list);
		pthread_rwlock_unlock(&pool->dp_co_list_lock);

		D_DEBUG(DF_DSMC, DF_CONT": closed: cookie="DF_X64" hdl="DF_UUID
			"\n", DP_CONT(pool->dp_pool, cont->dc_uuid), coh.cookie,
			DP_UUID(cont->dc_cont_hdl));
		dc_pool_put(pool);
		dc_cont_put(cont);
		daos_task_complete(task, 0);
		return 0;
	}

	ep.ep_grp = pool->dp_group;
	pthread_mutex_lock(&pool->dp_client_lock);
	rsvc_client_choose(&pool->dp_client, &ep);
	pthread_mutex_unlock(&pool->dp_client_lock);
	rc = cont_req_create(daos_task2ctx(task), ep, CONT_CLOSE, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
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

	rc = daos_task_register_comp_cb(task, cont_close_complete, &arg,
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
	daos_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "failed to close container handle "DF_X64": %d\n",
		coh.cookie, rc);
	return rc;
}

struct cont_query_args {
	struct dc_pool		*cqa_pool;
	struct dc_cont		*cqa_cont;
	daos_cont_info_t	*cqa_info;
	crt_rpc_t		*rpc;
	daos_handle_t		hdl;
};

static int
cont_query_complete(struct daos_task *task, void *data)
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
		D_ERROR("RPC error while querying container: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = out->cqo_op.co_rc;
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
	arg->cqa_info->ci_epoch_state = out->cqo_epoch_state;
	arg->cqa_info->ci_min_slipped_epoch = out->cqo_min_slipped_epoch;

	/* TODO */
	arg->cqa_info->ci_nsnapshots = 0;
	arg->cqa_info->ci_snapshots = NULL;

out:
	crt_req_decref(arg->rpc);
	dc_cont_put(cont);
	dc_pool_put(pool);
	return rc;
}

int
dc_cont_query(struct daos_task *task)
{
	daos_cont_query_t	*args;
	struct cont_query_in	*in;
	struct dc_pool		*pool;
	struct dc_cont		*cont;
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	struct cont_query_args	 arg;
	int			 rc;

	args = daos_task_get_args(DAOS_OPC_CONT_QUERY, task);
	D_ASSERTF(args != NULL, "Task Argumetn OPC does not match DC OPC\n");

	if (args->info == NULL)
		D_GOTO(err, rc = -DER_INVAL);

	cont = dc_hdl2cont(args->coh);
	if (cont == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	pool = dc_hdl2pool(cont->dc_pool_hdl);
	D_ASSERT(pool != NULL);

	D_DEBUG(DF_DSMC, DF_CONT": querying: hdl="DF_UUID"\n",
		DP_CONT(pool->dp_pool_hdl, cont->dc_uuid),
		DP_UUID(cont->dc_cont_hdl));

	ep.ep_grp  = pool->dp_group;
	pthread_mutex_lock(&pool->dp_client_lock);
	rsvc_client_choose(&pool->dp_client, &ep);
	pthread_mutex_unlock(&pool->dp_client_lock);
	rc = cont_req_create(daos_task2ctx(task), ep, CONT_QUERY, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		D_GOTO(err_cont, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->cqi_op.ci_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(in->cqi_op.ci_uuid, cont->dc_uuid);
	uuid_copy(in->cqi_op.ci_hdl, cont->dc_cont_hdl);

	arg.cqa_pool = pool;
	arg.cqa_cont = cont;
	arg.cqa_info = args->info;
	arg.rpc	     = rpc;
	arg.hdl	     = args->coh;
	crt_req_addref(rpc);

	rc = daos_task_register_comp_cb(task, cont_query_complete, &arg,
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
	daos_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "Failed to open container: %d\n", rc);
	return rc;
}

#define DC_CONT_GLOB_MAGIC	(0x16ca0387)

/* Structure of global buffer for dc_cont */
struct dc_cont_glob {
	/* magic number, DC_CONT_GLOB_MAGIC */
	uint32_t	dcg_magic;
	uint32_t	dcg_padding;
	/* pool connection handle */
	uuid_t		dcg_pool_hdl;
	/* container uuid and capas */
	uuid_t		dcg_uuid;
	uuid_t		dcg_cont_hdl;
	uint64_t	dcg_capas;
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
	/* skip cont_glob->dcg_padding) */
	/* skip cont_glob->dcg_pool_hdl (uuid_t) */
	/* skip cont_glob->dcg_uuid (uuid_t) */
	/* skip cont_glob->dcg_cont_hdl (uuid_t) */
	D_SWAP64S(&cont_glob->dcg_capas);
}

static int
dc_cont_l2g(daos_handle_t coh, daos_iov_t *glob)
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

	/* init global handle */
	cont_glob = (struct dc_cont_glob *)glob->iov_buf;
	cont_glob->dcg_magic = DC_CONT_GLOB_MAGIC;
	uuid_copy(cont_glob->dcg_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(cont_glob->dcg_uuid, cont->dc_uuid);
	uuid_copy(cont_glob->dcg_cont_hdl, cont->dc_cont_hdl);
	cont_glob->dcg_capas = cont->dc_capas;

	dc_pool_put(pool);
out_cont:
	dc_cont_put(cont);
out:
	if (rc)
		D_ERROR("daos_cont_l2g failed, rc: %d\n", rc);
	return rc;
}

int
dc_cont_local2global(daos_handle_t coh, daos_iov_t *glob)
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

	if ((cont_glob->dcg_capas & DAOS_COO_RW) &&
	    (pool->dp_capas & DAOS_PC_RO))
		D_GOTO(out_pool, rc = -DER_NO_PERM);

	cont = dc_cont_alloc(cont_glob->dcg_uuid);
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
dc_cont_global2local(daos_handle_t poh, daos_iov_t glob, daos_handle_t *coh)
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
		D_ERROR("dc_cont_g2l failed, rc: %d.\n", rc);

out:
	return rc;
}

int
dc_cont_attr_get(struct daos_task *task)
{
	return -DER_NOSYS;
}

int
dc_cont_attr_list(struct daos_task *task)
{
	return -DER_NOSYS;
}

int
dc_cont_attr_set(struct daos_task *task)
{
	return -DER_NOSYS;
}

int
dc_snap_create(struct daos_task *task)
{
	return -DER_NOSYS;
}

int
dc_snap_list(struct daos_task *task)
{
	return -DER_NOSYS;
}

int
dc_snap_destroy(struct daos_task *task)
{
	return -DER_NOSYS;
}

struct epoch_op_arg {
	struct dc_pool		*eoa_pool;
	struct dc_cont		*eoa_cont;
	daos_epoch_t		*eoa_epoch;
	daos_epoch_state_t	*eoa_state;
	crt_rpc_t		*rpc;
};

static int
epoch_op_complete(struct daos_task *task, void *data)
{
	struct epoch_op_arg	       *arg = (struct epoch_op_arg *)data;
	crt_rpc_t		       *rpc = arg->rpc;
	crt_opcode_t			opc = opc_get(rpc->cr_opc);
	struct cont_epoch_op_out       *out = crt_reply_get(rpc);
	int				rc = task->dt_result;

	rc = cont_rsvc_client_complete_rpc(arg->eoa_pool, &arg->rpc->cr_ep, rc,
					   &out->ceo_op, task);
	if (rc < 0)
		D_GOTO(out, rc);
	else if (rc == RSVC_CLIENT_RECHOOSE)
		D_GOTO(out, rc = 0);

	if (rc != 0) {
		D_ERROR("RPC error during epoch operation %u: %d\n", opc, rc);
		D_GOTO(out, rc);
	}

	rc = out->ceo_op.co_rc;
	if (rc != 0) {
		D_ERROR("epoch operation %u failed: %d\n", opc, rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, "completed epoch operation %u\n", opc);

	if (opc == CONT_EPOCH_HOLD)
		*arg->eoa_epoch = out->ceo_epoch_state.es_lhe;

	if (arg->eoa_state != NULL)
		*arg->eoa_state = out->ceo_epoch_state;

out:
	crt_req_decref(rpc);
	dc_pool_put(arg->eoa_pool);
	dc_cont_put(arg->eoa_cont);
	return rc;
}

static int
epoch_op(daos_handle_t coh, crt_opcode_t opc, daos_epoch_t *epoch,
	 daos_epoch_state_t *state, struct daos_task *task)
{
	struct cont_epoch_op_in	       *in;
	struct dc_pool		       *pool;
	struct dc_cont		       *cont;
	crt_endpoint_t			ep;
	crt_rpc_t		       *rpc;
	struct epoch_op_arg	        arg;
	int				rc;

	/* Check incoming arguments. */
	switch (opc) {
	case CONT_EPOCH_QUERY:
		D_ASSERT(epoch == NULL);
		break;
	case CONT_EPOCH_HOLD:
		if (epoch == NULL)
			D_GOTO(err, rc = -DER_INVAL);
		break;
	case CONT_EPOCH_SLIP:
	case CONT_EPOCH_DISCARD:
	case CONT_EPOCH_COMMIT:
		D_ASSERT(epoch != NULL);
		if (*epoch >= DAOS_EPOCH_MAX)
			D_GOTO(err, rc = -DER_OVERFLOW);
		break;
	}

	cont = dc_hdl2cont(coh);
	if (cont == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	pool = dc_hdl2pool(cont->dc_pool_hdl);
	D_ASSERT(pool != NULL);

	D_DEBUG(DF_DSMC, DF_CONT": op=%u hdl="DF_UUID" epoch="DF_U64"\n",
		DP_CONT(pool->dp_pool, cont->dc_uuid), opc,
		DP_UUID(cont->dc_cont_hdl), epoch == NULL ? 0 : *epoch);

	ep.ep_grp = pool->dp_group;
	pthread_mutex_lock(&pool->dp_client_lock);
	rsvc_client_choose(&pool->dp_client, &ep);
	pthread_mutex_unlock(&pool->dp_client_lock);
	rc = cont_req_create(daos_task2ctx(task), ep, opc, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		D_GOTO(err_pool, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->cei_op.ci_pool_hdl, pool->dp_pool_hdl);
	uuid_copy(in->cei_op.ci_uuid, cont->dc_uuid);
	uuid_copy(in->cei_op.ci_hdl, cont->dc_cont_hdl);
	if (opc != CONT_EPOCH_QUERY)
		in->cei_epoch = *epoch;

	arg.eoa_pool = pool;
	arg.eoa_cont = cont;
	arg.eoa_epoch = epoch;
	arg.eoa_state = state;
	crt_req_addref(rpc);
	arg.rpc = rpc;

	rc = daos_task_register_comp_cb(task, epoch_op_complete, &arg,
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
	dc_cont_put(cont);
err:
	daos_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "epoch op %u("DF_U64") failed: %d\n", opc,
		epoch == NULL ? 0 : *epoch, rc);
	return rc;
}

int
dc_epoch_query(struct daos_task *task)
{
	daos_epoch_query_t *args;

	args = daos_task_get_args(DAOS_OPC_EPOCH_QUERY, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return epoch_op(args->coh, CONT_EPOCH_QUERY, NULL, args->state, task);
}

int
dc_epoch_hold(struct daos_task *task)
{
	daos_epoch_hold_t *args;

	args = daos_task_get_args(DAOS_OPC_EPOCH_HOLD, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return epoch_op(args->coh, CONT_EPOCH_HOLD, args->epoch, args->state,
			task);
}

int
dc_epoch_slip(struct daos_task *task)
{
	daos_epoch_slip_t *args;

	args = daos_task_get_args(DAOS_OPC_EPOCH_SLIP, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return epoch_op(args->coh, CONT_EPOCH_SLIP, &args->epoch, args->state,
			task);
}

int
dc_epoch_discard(struct daos_task *task)
{
	daos_epoch_discard_t *args;

	args = daos_task_get_args(DAOS_OPC_EPOCH_DISCARD, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return epoch_op(args->coh, CONT_EPOCH_DISCARD, &args->epoch,
			args->state, task);
}

int
dc_epoch_commit(struct daos_task *task)
{
	daos_epoch_commit_t *args;

	args = daos_task_get_args(DAOS_OPC_EPOCH_COMMIT, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return epoch_op(args->coh, CONT_EPOCH_COMMIT, &args->epoch, args->state,
			task);
}

int
dc_epoch_flush(struct daos_task *task)
{
	return -DER_NOSYS;
}

int
dc_epoch_wait(struct daos_task *task)
{
	return -DER_NOSYS;
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
	pthread_rwlock_rdlock(&pool->dp_map_lock);
	n = pool_map_find_target(pool->dp_map, tgt_idx, tgt);
	pthread_rwlock_unlock(&pool->dp_map_lock);
	dc_pool_put(pool);
	dc_cont_put(dc);
	if (n != 1) {
		D_ERROR("failed to find target %u\n", tgt_idx);
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
