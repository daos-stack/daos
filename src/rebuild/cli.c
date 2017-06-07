/**
 * (C) Copyright 2017 Intel Corporation.
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
 * rebuild: rebuild client
 *
 * rebuild client side API.
 *
 */
#define DD_SUBSYS	DD_FAC(rebuild)

#include <daos/rpc.h>
#include <daos/event.h>
#include <daos/pool.h>
#include <daos_task.h>
#include "rpc.h"

/**
 * Initialize rebuild interface
 */
int
dc_rebuild_init(void)
{
	int rc;

	rc = daos_rpc_register(rebuild_cli_rpcs, NULL, DAOS_REBUILD_MODULE);
	if (rc != 0)
		D_ERROR("failed to register rebuild RPCs: %d\n", rc);

	return rc;
}

/**
 * Finalize rebuild interface
 */
void
dc_rebuild_fini(void)
{
	daos_rpc_unregister(rebuild_cli_rpcs);
}

static int
dc_rebuild_tgt_cp(struct daos_task *task, void *data)
{
	crt_rpc_t		*rpc = *(crt_rpc_t **)data;
	struct rebuild_tgt_in   *in = crt_req_get(rpc);
	struct rebuild_out	*out = crt_reply_get(rpc);
	int			rc;

	rc = out->ro_status;
	if (rc != 0) {
		D_ERROR(DF_UUID"failed to rebuild target: %d\n",
			DP_UUID(in->rti_pool_uuid), rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_TRACE, DF_UUID": rebuild\n", DP_UUID(in->rti_pool_uuid));

out:
	daos_group_detach(rpc->cr_ep.ep_grp);
	crt_req_decref(rpc);
	return rc;
}

static int
dc_rebuild_tgt_internal(uuid_t pool_uuid, daos_rank_list_t *failed_list,
			struct daos_task *task, unsigned int opc)
{
	struct rebuild_tgt_in	*rti;
	crt_endpoint_t		ep;
	crt_rpc_t		*rpc;
	int			rc;

	rc = daos_group_attach(NULL, &ep.ep_grp);
	if (rc != 0)
		return rc;

	/* Currently, rank 0 runs the pool and the (only) container service. */
	ep.ep_rank = 0;
	ep.ep_tag = 0;
	rc = rebuild_req_create(daos_task2ctx(task), ep, opc, &rpc);
	if (rc != 0)
		D_GOTO(err_group, rc);

	rti = crt_req_get(rpc);
	D_ASSERT(rti != NULL);

	uuid_copy(rti->rti_pool_uuid, pool_uuid);
	rti->rti_failed_tgts = failed_list;

	crt_req_addref(rpc);

	rc = daos_task_register_comp_cb(task, dc_rebuild_tgt_cp, sizeof(rpc),
					&rpc);
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	D_DEBUG(DB_TRACE, "rebuild tgt for "DF_UUID"\n", DP_UUID(pool_uuid));
	rc = daos_rpc_send(rpc, task);
	if (rc != 0)
		D_ERROR("Send rebuild rpc failed: %d\n", rc);
	return rc;
err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_group:
	daos_group_detach(ep.ep_grp);
	return rc;
}

int
dc_rebuild_tgt(uuid_t pool_uuid, daos_rank_list_t *failed_list,
	       struct daos_task *task)
{
	return dc_rebuild_tgt_internal(pool_uuid, failed_list, task,
				       REBUILD_TGT);
}

int
dc_rebuild_tgt_fini(uuid_t pool_uuid, daos_rank_list_t *failed_list,
		    struct daos_task *task)
{
	return dc_rebuild_tgt_internal(pool_uuid, failed_list, task,
				       REBUILD_FINI);
}

struct dc_query_cb_arg {
	crt_rpc_t	*rpc;
	int		*done;
	int		*status;
	unsigned int	*rec_count;
	unsigned int	*obj_count;
};

static int
dc_rebuild_query_cp(struct daos_task *task, void *data)
{
	struct dc_query_cb_arg	*arg = data;
	crt_rpc_t		*rpc = arg->rpc;
	struct rebuild_query_out *out = crt_reply_get(rpc);
	int			rc;

	rc = out->rqo_status;
	*arg->status = rc;
	if (rc != 0) {
		D_ERROR("failed to rebuild target: %d\n", rc);
		D_GOTO(out, rc);
	}

	*arg->done = out->rqo_done;
	*arg->rec_count = out->rqo_rec_count;
	*arg->obj_count = out->rqo_obj_count;
out:
	daos_group_detach(rpc->cr_ep.ep_grp);
	crt_req_decref(rpc);
	return rc;
}

int
dc_rebuild_query(daos_handle_t poh, daos_rank_list_t *failed_list,
		 int *done, int *status, unsigned int *rec_count,
		 unsigned int *obj_count, struct daos_task *task)
{
	struct rebuild_query_in	*rqi;
	crt_endpoint_t		ep;
	struct dc_pool		*pool;
	struct dc_query_cb_arg	arg;
	crt_rpc_t		*rpc;
	int			rc;

	pool = dc_hdl2pool(poh);
	if (pool == NULL)
		D_GOTO(err, rc = -DER_NO_HDL);

	rc = daos_group_attach(NULL, &ep.ep_grp);
	if (rc != 0)
		D_GOTO(err_put, rc);

	/* Currently, rank 0 runs the pool and the (only) container service. */
	pthread_mutex_lock(&pool->dp_client_lock);
	rsvc_client_choose(&pool->dp_client, &ep);
	pthread_mutex_unlock(&pool->dp_client_lock);

	D_DEBUG(DB_TRACE, "send rebuild query to rank %d\n", ep.ep_rank);
	rc = rebuild_req_create(daos_task2ctx(task), ep, REBUILD_QUERY,
				&rpc);
	if (rc != 0)
		D_GOTO(err_group, rc);

	rqi = crt_req_get(rpc);
	D_ASSERT(rqi != NULL);

	uuid_copy(rqi->rqi_pool_uuid, pool->dp_pool);
	rqi->rqi_tgts_failed = failed_list;

	crt_req_addref(rpc);

	arg.rpc = rpc;
	arg.done = done;
	arg.status = status;
	arg.rec_count = rec_count;
	arg.obj_count = obj_count;
	rc = daos_task_register_comp_cb(task, dc_rebuild_query_cp,
					sizeof(arg), &arg);
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	rc = daos_rpc_send(rpc, task);
	if (rc != 0)
		D_ERROR("Send rebuild rpc failed: %d\n", rc);
	dc_pool_put(pool);
	return rc;
err_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
err_group:
	daos_group_detach(ep.ep_grp);
err_put:
	dc_pool_put(pool);
err:
	daos_task_complete(task, rc);
	return rc;
}
