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

struct rebuild_status {
	int scanning;
	int status;
	int rec_count;
	int obj_count;
};

int
dss_rebuild_check_scanning(void *arg)
{
	struct rebuild_tls	*tls = rebuild_tls_get();
	struct rebuild_status	*status = arg;

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
	struct rebuild_status		status;
	int				i;
	bool				rebuilding = false;
	int				rc;

	memset(&status, 0, sizeof(status));
	rtqo = crt_reply_get(rpc);
	rtqo->rtqo_rebuilding = 0;
	rtqo->rtqo_rec_count = 0;
	rtqo->rtqo_obj_count = 0;

	/* First let's checking building */
	for (i = 0; i < tls->rebuild_building_nr; i++) {
		if (tls->rebuild_building[i] > 0) {
			D_DEBUG(DB_TRACE, "thread %d still rebuilding\n", i);
			rebuilding = true;
		}
	}

	/* let's check status on every thread*/
	rc = dss_collective(dss_rebuild_check_scanning, &status);
	if (rc)
		D_GOTO(out, rc);

	if (!rebuilding && status.scanning > 0)
		rebuilding = true;

	if (rebuilding)
		rtqo->rtqo_rebuilding = 1;

	D_DEBUG(DB_TRACE, "pool "DF_UUID" scanning %d/%d rebuilding %s"
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

/* query the rebuild status */
int
ds_rebuild_query_handler(crt_rpc_t *rpc)
{
	struct rebuild_query_in		*rqi;
	struct rebuild_query_out	*rqo;
	struct rebuild_tgt_query_in	*rtqi;
	struct rebuild_tgt_query_out	*rtqo;
	struct ds_pool		*pool;
	crt_rpc_t		*tgt_rpc;
	int			rc;

	rqi = crt_req_get(rpc);
	rqo = crt_reply_get(rpc);

	rqo->rqo_done = 0;
	pool = ds_pool_lookup(rqi->rqi_pool_uuid);
	if (pool == NULL) {
		D_ERROR("can not find "DF_UUID" rc %d\n",
			DP_UUID(rqi->rqi_pool_uuid), -DER_NO_HDL);
		D_GOTO(out, rc = -DER_NO_HDL);
	}

	/* Then send rebuild RPC to all targets of the pool */
	rc = ds_pool_bcast_create(dss_get_module_info()->dmi_ctx,
				  pool, DAOS_REBUILD_MODULE,
				  REBUILD_TGT_QUERY, &tgt_rpc, NULL,
				  rqi->rqi_tgts_failed);
	if (rc != 0)
		D_GOTO(out_put, rc);

	rtqi = crt_req_get(tgt_rpc);
	uuid_copy(rtqi->rtqi_uuid, rqi->rqi_pool_uuid);
	rc = dss_rpc_send(tgt_rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	rtqo = crt_reply_get(tgt_rpc);
	D_DEBUG(DB_TRACE, "%p query rebuild status %d obj count %d"
		" rec count %d\n", rtqo, rtqo->rtqo_rebuilding,
		rtqo->rtqo_obj_count, rtqo->rtqo_rec_count);
	if (rtqo->rtqo_rebuilding == 0)
		rqo->rqo_done = 1;

	if (rtqo->rtqo_status)
		rqo->rqo_status = rtqo->rtqo_status;
	rqo->rqo_rec_count = rtqo->rtqo_rec_count;
	rqo->rqo_obj_count = rtqo->rtqo_obj_count;
out_rpc:
	crt_req_decref(tgt_rpc);
out_put:
	ds_pool_put(pool);
out:
	if (rqo->rqo_status == 0)
		rqo->rqo_status = rc;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: rc %d\n", rc);
	return rc;
}

/**
 * Initiate the rebuild process, i.e. sending rebuild
 * requests to every target to find out the impacted
 * objects.
 */
int
ds_rebuild(const uuid_t uuid, daos_rank_list_t *tgts_failed)
{
	struct ds_pool		*pool;
	crt_rpc_t		*rpc;
	struct rebuild_scan_in	*rsi;
	struct rebuild_out	*ro;
	int			rc;

	D_DEBUG(DB_TRACE, "rebuild "DF_UUID"\n", DP_UUID(uuid));

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
	rsi->rsi_pool_map_ver = pool_map_get_version(pool->sp_map);
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

static int
ds_rebuild_fini_one(void *arg)
{
	struct rebuild_tls *tls = rebuild_tls_get();

	D_DEBUG(DB_TRACE, "close container/pool "DF_UUID"/"DF_UUID"\n",
		DP_UUID(tls->rebuild_cont_hdl_uuid),
		DP_UUID(tls->rebuild_pool_hdl_uuid));
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
	struct rebuild_out	*ro = crt_reply_get(rpc);
	struct rebuild_tls	*tls = rebuild_tls_get();
	int			rc;

	if (uuid_compare(rfi->rfti_pool_uuid, tls->rebuild_pool_uuid) != 0)
		D_GOTO(out, rc = -DER_NO_HDL);

	D_DEBUG(DB_TRACE, "close container/pool "DF_UUID"/"DF_UUID"\n",
		DP_UUID(tls->rebuild_cont_hdl_uuid),
		DP_UUID(tls->rebuild_pool_hdl_uuid));
	/* close the rebuild pool/container */
	rc = dss_collective(ds_rebuild_fini_one, NULL);
out:
	ro->ro_status = rc;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed %d\n", rc);

	return rc;
}

/**
 * Finish the rebuild pool, i.e. disconnect pool, close rebuild container,
 * and Mark the failure target to be DOWNOUT.
 */
int
ds_rebuild_fini(const uuid_t uuid, daos_rank_list_t *tgts_failed)
{
	struct rebuild_tls	*tls = rebuild_tls_get();
	struct ds_pool		*pool;
	struct rebuild_fini_tgt_in *rfi;
	struct rebuild_out	*ro;
	crt_rpc_t		*rpc;
	int			rc;

	D_DEBUG(DB_TRACE, "pool rebuild "DF_UUID" finish.\n", DP_UUID(uuid));

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
	rfi->rfti_pool_map_ver = pool_map_get_version(pool->sp_map);

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

int
ds_rebuild_handler(crt_rpc_t *rpc)
{
	struct rebuild_tgt_in	*rti;
	struct rebuild_out	*ro;
	int			rc;

	rti = crt_req_get(rpc);
	ro = crt_reply_get(rpc);

	if (opc_get(rpc->cr_opc) == REBUILD_TGT)
		rc = ds_rebuild(rti->rti_pool_uuid, rti->rti_failed_tgts);
	else if (opc_get(rpc->cr_opc) == REBUILD_FINI)
		rc = ds_rebuild_fini(rti->rti_pool_uuid, rti->rti_failed_tgts);
	else
		D_ASSERT(0);

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
