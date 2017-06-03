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
 * rebuild: rebuild service
 *
 * Rebuild service module api.
 */
#define DD_SUBSYS	DD_FAC(rebuild)

#include <daos/rpc.h>
#include <daos/pool.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/pool.h>
#include <daos_srv/container.h>
#include "rpc.h"
#include "rebuild_internal.h"

static int
init(void)
{
	return 0;
}

static int
fini(void)
{
	return 0;
}

static void *
rebuild_tls_init(const struct dss_thread_local_storage *dtls,
		 struct dss_module_key *key)
{
	struct rebuild_tls *tls;

	D_ALLOC_PTR(tls);
	DAOS_INIT_LIST_HEAD(&tls->rebuild_task_list);
	return tls;
}

bool
is_rebuild_container(uuid_t cont_hdl_uuid)
{
	struct rebuild_tls *tls = rebuild_tls_get();

	return !uuid_compare(tls->rebuild_cont_hdl_uuid, cont_hdl_uuid);
}

bool
is_rebuild_pool(uuid_t pool_hdl)
{
	struct rebuild_tls *tls = rebuild_tls_get();

	return !uuid_compare(tls->rebuild_pool_hdl_uuid, pool_hdl);
}

static void
rebuild_tls_fini(const struct dss_thread_local_storage *dtls,
		 struct dss_module_key *key, void *data)
{
	struct rebuild_tls *tls = data;

	D_ASSERT(tls->rebuild_local_root_init == 0);
	D_FREE_PTR(tls);
}

struct rebuild_tgt_query_info {
	int scanning;
	int status;
	int rec_count;
	int obj_count;
};

int
dss_rebuild_check_scanning(void *arg)
{
	struct rebuild_tls	*tls = rebuild_tls_get();
	struct rebuild_tgt_query_info	*status = arg;

	if (tls->rebuild_scanning)
		status->scanning++;
	if (tls->rebuild_status != 0 && status->status == 0)
		status->status = tls->rebuild_status;
	status->rec_count += tls->rebuild_rec_count;
	status->obj_count += tls->rebuild_obj_count;

	return 0;
}

int
ds_rebuild_tgt_query_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				void *priv)
{
	struct rebuild_tgt_query_out    *out_source = crt_reply_get(source);
	struct rebuild_tgt_query_out    *out_result = crt_reply_get(result);

	out_result->rtqo_rebuilding += out_source->rtqo_rebuilding;
	if (out_result->rtqo_status == 0 && out_source->rtqo_status != 0)
		out_result->rtqo_status = out_source->rtqo_status;

	out_result->rtqo_rec_count += out_source->rtqo_rec_count;
	out_result->rtqo_obj_count += out_source->rtqo_obj_count;

	return 0;
}

int
ds_rebuild_tgt_query_handler(crt_rpc_t *rpc)
{
	struct rebuild_tls		*tls = rebuild_tls_get();
	struct rebuild_tgt_query_out	*rtqo;
	struct rebuild_tgt_query_info	status;
	int				i;
	bool				rebuilding = false;
	int				rc;

	memset(&status, 0, sizeof(status));
	rtqo = crt_reply_get(rpc);
	rtqo->rtqo_rebuilding = 0;
	rtqo->rtqo_rec_count = 0;
	rtqo->rtqo_obj_count = 0;

	/* let's check status on every thread*/
	rc = dss_collective(dss_rebuild_check_scanning, &status);
	if (rc)
		D_GOTO(out, rc);

	if (status.scanning == 0) {
		/* then check building status*/
		for (i = 0; i < tls->rebuild_building_nr; i++) {
			if (tls->rebuild_building[i] > 0) {
				D_DEBUG(DB_TRACE,
					"thread %d still rebuilding\n", i);
				rebuilding = true;
				break;
			}
		}
	} else {
		rebuilding = true;
	}

	if (rebuilding)
		rtqo->rtqo_rebuilding = 1;

	D_DEBUG(DB_TRACE, "pool "DF_UUID" scanning %d/%d rebuilding %s "
		"obj_count %d rec_count %d\n",
		DP_UUID(tls->rebuild_pool_uuid), status.scanning,
		status.status, rebuilding ? "yes" : "no", status.obj_count,
		status.rec_count);
	rtqo->rtqo_rec_count = status.rec_count;
	rtqo->rtqo_obj_count = status.obj_count;

	if (status.status != 0)
		rtqo->rtqo_status = status.status;
out:
	if (rtqo->rtqo_status == 0)
		rtqo->rtqo_status = rc;

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed %d\n", rc);

	return rc;
}

struct rebuild_status {
	unsigned int	result;	/* rebuild result */
	unsigned int	obj_cnt; /* how many objects being rebuilt */
	unsigned int	rec_cnt; /* how many records being rebuilt */
	unsigned int	done:1;	 /* If the rebuild is done */
};

static int
ds_rebuild_query_internal(uuid_t pool_uuid, daos_rank_list_t *failed_tgts,
			  struct rebuild_status *status)
{
	struct ds_pool	*pool;
	crt_rpc_t	*tgt_rpc;
	struct rebuild_tgt_query_in	*rtqi;
	struct rebuild_tgt_query_out	*rtqo;
	int		rc;

	status->done = 0;
	pool = ds_pool_lookup(pool_uuid);
	if (pool == NULL) {
		D_ERROR("can not find "DF_UUID" rc %d\n",
			DP_UUID(pool_uuid), -DER_NO_HDL);
		return -DER_NO_HDL;
	}

	/* Then send rebuild RPC to all targets of the pool */
	rc = ds_pool_bcast_create(dss_get_module_info()->dmi_ctx,
				  pool, DAOS_REBUILD_MODULE,
				  REBUILD_TGT_QUERY, &tgt_rpc, NULL,
				  failed_tgts);
	if (rc != 0)
		D_GOTO(out_put, rc);

	rtqi = crt_req_get(tgt_rpc);
	uuid_copy(rtqi->rtqi_uuid, pool_uuid);
	rc = dss_rpc_send(tgt_rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	rtqo = crt_reply_get(tgt_rpc);
	D_DEBUG(DB_TRACE, "%p query rebuild status %d obj count %d"
		" rec count %d\n", rtqo, rtqo->rtqo_rebuilding,
		rtqo->rtqo_obj_count, rtqo->rtqo_rec_count);
	if (rtqo->rtqo_rebuilding == 0)
		status->done = 1;

	if (status->result == 0)
		status->result = rtqo->rtqo_status;

	status->rec_cnt = rtqo->rtqo_rec_count;
	status->obj_cnt = rtqo->rtqo_obj_count;

out_rpc:
	crt_req_decref(tgt_rpc);
out_put:
	ds_pool_put(pool);
	return rc;
}

/* query the rebuild status */
int
ds_rebuild_query_handler(crt_rpc_t *rpc)
{
	struct rebuild_query_in		*rqi;
	struct rebuild_query_out	*rqo;
	struct rebuild_status		status;
	int			rc;

	rqi = crt_req_get(rpc);
	rqo = crt_reply_get(rpc);

	memset(&status, 0, sizeof(status));

	rc = ds_rebuild_query_internal(rqi->rqi_pool_uuid,
				       rqi->rqi_tgts_failed, &status);
	if (rc)
		D_GOTO(out, rc);

	rqo->rqo_done = status.done;
	rqo->rqo_status = status.result;
	rqo->rqo_rec_count = status.rec_cnt;
	rqo->rqo_obj_count = status.obj_cnt;
out:
	if (rqo->rqo_status == 0)
		rqo->rqo_status = rc;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: rc %d\n", rc);
	return rc;
}

/**
 * Finish the rebuild pool, i.e. disconnect pool, close rebuild container,
 * and Mark the failure target to be DOWNOUT.
 */
int
ds_rebuild_fini(const uuid_t uuid, uint32_t map_ver,
		daos_rank_list_t *tgts_failed)
{
	struct rebuild_tls	*tls = rebuild_tls_get();
	struct ds_pool		*pool;
	struct rebuild_fini_tgt_in *rfi;
	struct rebuild_out	*ro;
	crt_rpc_t		*rpc;
	int			rc;

	D_DEBUG(DB_TRACE, "pool rebuild "DF_UUID" (map_ver=%u) finish.\n",
		DP_UUID(uuid), map_ver);

	if (uuid_compare(uuid, tls->rebuild_pool_uuid) != 0)
		return 0;

	/* Mark the target to be DOWNOUT */
	rc = ds_pool_tgt_exclude_out(tls->rebuild_pool_hdl_uuid,
				     tls->rebuild_pool_uuid, tgts_failed, NULL);
	if (rc)
		return rc;

	pool = ds_pool_lookup(uuid);
	if (pool == NULL)
		return -DER_NO_HDL;

	/* Then send rebuild RPC to all targets of the pool */
	rc = ds_pool_bcast_create(dss_get_module_info()->dmi_ctx,
				  pool, DAOS_REBUILD_MODULE,
				  REBUILD_TGT_FINI, &rpc, NULL, tgts_failed);
	if (rc != 0) {
		D_ERROR("pool map broad cast failed: rc %d\n", rc);
		D_GOTO(out, rc);
	}

	rfi = crt_req_get(rpc);
	uuid_copy(rfi->rfti_pool_uuid, uuid);
	rfi->rfti_pool_map_ver = map_ver;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	ro = crt_reply_get(rpc);
	rc = ro->ro_status;
	if (rc != 0)
		D_ERROR(DF_UUID": failed to fini pool rebuild: %d\n",
			DP_UUID(uuid), rc);
out_rpc:
	crt_req_decref(rpc);
out:
	ds_pool_put(pool);
	if (rc == 0)
		uuid_clear(tls->rebuild_pool_uuid);

	return rc;
}


static int
rank_list_copy(daos_rank_list_t *dst, daos_rank_list_t *src)
{
	int i;

	D_ALLOC(dst->rl_ranks, src->rl_nr.num * sizeof(*src->rl_ranks));
	if (dst->rl_ranks == NULL)
		return -DER_NOMEM;

	dst->rl_nr.num = src->rl_nr.num;
	dst->rl_nr.num_out = src->rl_nr.num_out;

	for (i = 0; i < src->rl_nr.num; i++)
		dst->rl_ranks[i] = src->rl_ranks[i];

	return 0;
}

#define RBLD_QUERY_INTV	2	/* # seocnds to query rebuild status */

void
ds_rebuild_check(uuid_t pool_uuid, uint32_t map_ver,
		 daos_rank_list_t *tgts_failed)
{
	double	then = 0;
	double	now;
	int	rc;

	while (1) {
		struct rebuild_status	status;

		now = ABT_get_wtime();
		if (now - then < RBLD_QUERY_INTV) {
			/* Yield to other ULT */
			ABT_thread_yield();
			continue;
		}

		memset(&status, 0, sizeof(status));
		rc = ds_rebuild_query_internal(pool_uuid, tgts_failed,
					       &status);
		D_DEBUG(DB_TRACE, DF_UUID "done/result/obj/rec %d/%d/%d/%d"
			" rc %d\n", DP_UUID(pool_uuid), status.done,
			status.result, status.obj_cnt, status.rec_cnt, rc);
		if (rc || status.done)
			break;

		then = now;
	};
}

/**
 * Initiate the rebuild process, i.e. sending rebuild requests to every target
 * to find out the impacted objects.
 */
static int
ds_rebuild(const uuid_t uuid, uint32_t map_ver, daos_rank_list_t *tgts_failed)
{
	struct ds_pool		*pool;
	crt_rpc_t		*rpc;
	struct rebuild_scan_in	*rsi;
	struct rebuild_out	*ro;
	int			rc;

	D_DEBUG(DB_TRACE, "rebuild "DF_UUID", map version=%u\n",
		DP_UUID(uuid), map_ver);

	/* Broadcast the pool map first */
	rc = ds_pool_pmap_broadcast(uuid, tgts_failed);
	if (rc)
		D_ERROR("pool map broad cast failed: rc %d\n", rc);

	pool = ds_pool_lookup(uuid);
	if (pool == NULL)
		return -DER_NO_HDL;

	/* Then send rebuild RPC to all targets of the pool */
	rc = ds_pool_bcast_create(dss_get_module_info()->dmi_ctx,
				  pool, DAOS_REBUILD_MODULE,
				  REBUILD_OBJECTS_SCAN, &rpc, NULL,
				  tgts_failed);
	if (rc != 0) {
		D_ERROR("pool map broad cast failed: rc %d\n", rc);
		D_GOTO(out, rc = 0); /* ignore the failure */
	}

	rsi = crt_req_get(rpc);
	uuid_generate(rsi->rsi_rebuild_cont_hdl_uuid);
	uuid_generate(rsi->rsi_rebuild_pool_hdl_uuid);
	uuid_copy(rsi->rsi_pool_uuid, uuid);
	D_DEBUG(DB_TRACE, "rebuild "DF_UUID"/"DF_UUID"\n",
		DP_UUID(rsi->rsi_pool_uuid),
		DP_UUID(rsi->rsi_rebuild_cont_hdl_uuid));
	rsi->rsi_pool_map_ver = map_ver;
	rsi->rsi_tgts_failed = tgts_failed;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	ro = crt_reply_get(rpc);
	rc = ro->ro_status;
	if (rc != 0)
		D_ERROR(DF_UUID": failed to start pool rebuild: %d\n",
			DP_UUID(uuid), rc);
out_rpc:
	crt_req_decref(rpc);
out:
	ds_pool_put(pool);
	return rc;
}

struct ds_rebuild_task {
	daos_list_t	dst_list;
	uuid_t		dst_pool_uuid;
	uint32_t	dst_map_ver;
	daos_rank_list_t dst_tgts_failed;
};

static int
ds_rebuild_one(uuid_t pool_uuid, uint32_t map_ver,
	       daos_rank_list_t *tgts_failed)
{
	int rc;
	int rc1;

	rc = ds_rebuild(pool_uuid, map_ver, tgts_failed);
	if (rc != 0) {
		D_ERROR(""DF_UUID" (ver=%u) rebuild failed: rc %d\n",
			DP_UUID(pool_uuid), map_ver, rc);
		D_GOTO(out, rc);
	}

	/* Wait until rebuild finished */
	ds_rebuild_check(pool_uuid, map_ver, tgts_failed);
	D_EXIT;
out:
	rc1 = ds_rebuild_fini(pool_uuid, map_ver, tgts_failed);
	if (rc == 0)
		rc = rc1;

	return rc;
}

static void
ds_rebuild_ult(void *arg)
{
	struct ds_rebuild_task	*task;
	struct ds_rebuild_task	*tmp;
	struct rebuild_tls	*tls = rebuild_tls_get();
	int rc;

	/* rebuild all failures one by one */
	daos_list_for_each_entry_safe(task, tmp, &tls->rebuild_task_list,
				      dst_list) {
		daos_list_del(&task->dst_list);
		rc = ds_rebuild_one(task->dst_pool_uuid, task->dst_map_ver,
				    &task->dst_tgts_failed);
		if (rc != 0)
			D_ERROR(""DF_UUID" rebuild failed: rc %d\n",
				DP_UUID(task->dst_pool_uuid), rc);
		if (task->dst_tgts_failed.rl_nr.num > 0)
			D_FREE(task->dst_tgts_failed.rl_ranks,
			       task->dst_tgts_failed.rl_nr.num *
			       sizeof(*task->dst_tgts_failed.rl_ranks));

		D_FREE_PTR(task);
		ABT_thread_yield();
	}

	tls->rebuild_ult = 0;
}

/**
 * Add rebuild task to the rebuild list and another ULT will rebuild the
 * pool.
 */
int
ds_rebuild_schedule(const uuid_t uuid, uint32_t map_ver,
		    daos_rank_list_t *tgts_failed)
{
	struct ds_rebuild_task	*task;
	struct rebuild_tls	*tls = rebuild_tls_get();
	int			rc;

	D_ALLOC_PTR(task);
	if (task == NULL)
		return -DER_NOMEM;

	task->dst_map_ver = map_ver;
	uuid_copy(task->dst_pool_uuid, uuid);
	DAOS_INIT_LIST_HEAD(&task->dst_list);

	rc = rank_list_copy(&task->dst_tgts_failed, tgts_failed);
	if (rc) {
		D_FREE_PTR(task);
		return rc;
	}

	daos_list_add_tail(&tls->rebuild_task_list, &task->dst_list);

	if (!tls->rebuild_ult) {
		dss_ult_create(ds_rebuild_ult, NULL, -1, NULL);
		tls->rebuild_ult = 1;
	}

	return 0;
}

static int
ds_rebuild_fini_one(void *arg)
{
	struct rebuild_tls *tls = rebuild_tls_get();

	D_DEBUG(DB_TRACE, "close container/pool "DF_UUID"/"DF_UUID"\n",
		DP_UUID(tls->rebuild_cont_hdl_uuid),
		DP_UUID(tls->rebuild_pool_hdl_uuid));

	if (!daos_handle_is_inval(tls->rebuild_pool_hdl)) {
		dc_pool_local_close(tls->rebuild_pool_hdl);
		tls->rebuild_pool_hdl = DAOS_HDL_INVAL;
	}

	ds_cont_local_close(tls->rebuild_cont_hdl_uuid);
	uuid_clear(tls->rebuild_cont_hdl_uuid);
	ds_pool_local_close(tls->rebuild_pool_hdl_uuid);
	uuid_clear(tls->rebuild_pool_hdl_uuid);

	return 0;
}

int
ds_rebuild_tgt_fini_handler(crt_rpc_t *rpc)
{
	struct rebuild_fini_tgt_in	*rfi = crt_req_get(rpc);
	struct rebuild_out		*ro = crt_reply_get(rpc);
	struct rebuild_tls		*tls = rebuild_tls_get();
	int				 rc;

	if (uuid_compare(rfi->rfti_pool_uuid, tls->rebuild_pool_uuid) != 0)
		D_GOTO(out, rc = -DER_NO_HDL);

	D_DEBUG(DB_TRACE, "Finalize rebuild for "DF_UUID", map_ver=%u\n",
		rfi->rfti_pool_uuid, rfi->rfti_pool_map_ver);

	/* close the rebuild pool/container */
	rc = dss_collective(ds_rebuild_fini_one, NULL);
out:
	ro->ro_status = rc;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed %d\n", rc);

	return rc;
}

int
ds_rebuild_handler(crt_rpc_t *rpc)
{
	struct rebuild_tgt_in	*rti;
	struct rebuild_out	*ro;
	int			rc;

	rti = crt_req_get(rpc);
	ro = crt_reply_get(rpc);

	if (opc_get(rpc->cr_opc) == REBUILD_TGT) {
		rc = ds_rebuild_schedule(rti->rti_pool_uuid, rti->rti_map_ver,
					 rti->rti_failed_tgts);

	} else if (opc_get(rpc->cr_opc) == REBUILD_FINI) {
		rc = ds_rebuild_fini(rti->rti_pool_uuid, rti->rti_map_ver,
				     rti->rti_failed_tgts);
	} else {
		D_ASSERT(0);
	}

	ro->ro_status = rc;

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: %d\n", rc);

	return rc;
}

/* Note: the rpc input/output parameters is defined in daos_rpc */
static struct daos_rpc_handler rebuild_handlers[] = {
	{
		.dr_opc		= REBUILD_OBJECTS_SCAN,
		.dr_hdlr	= ds_rebuild_scan_handler
	}, {
		.dr_opc		= REBUILD_OBJECTS,
		.dr_hdlr	= ds_rebuild_obj_handler
	}, {
		.dr_opc		= REBUILD_TGT,
		.dr_hdlr	= ds_rebuild_handler
	}, {
		.dr_opc		= REBUILD_FINI,
		.dr_hdlr	= ds_rebuild_handler
	}, {
		.dr_opc		= REBUILD_TGT_FINI,
		.dr_hdlr	= ds_rebuild_tgt_fini_handler
	}, {
		.dr_opc		= REBUILD_QUERY,
		.dr_hdlr	= ds_rebuild_query_handler
	}, {
		.dr_opc		= REBUILD_TGT_QUERY,
		.dr_hdlr	= ds_rebuild_tgt_query_handler,
		.dr_corpc_ops	= {
			.co_aggregate	= ds_rebuild_tgt_query_aggregator,
		}
	}, {
		.dr_opc		= 0
	}
};

struct dss_module_key rebuild_module_key = {
	.dmk_tags = DAOS_SERVER_TAG,
	.dmk_index = -1,
	.dmk_init = rebuild_tls_init,
	.dmk_fini = rebuild_tls_fini,
};

struct dss_module rebuild_module =  {
	.sm_name	= "rebuild",
	.sm_mod_id	= DAOS_REBUILD_MODULE,
	.sm_ver		= 1,
	.sm_init	= init,
	.sm_fini	= fini,
	.sm_cl_rpcs	= rebuild_cli_rpcs,
	.sm_srv_rpcs	= rebuild_rpcs,
	.sm_handlers	= rebuild_handlers,
	.sm_key		= &rebuild_module_key,
};
