/**
 * (C) Copyright 2021-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(pipeline)

#include <daos/common.h>
#include <daos_pipeline.h>
#include <daos/task.h>
#include <daos_task.h>
#include <daos/pool.h>
#include <daos/container.h>
#include <daos/object.h>
#include <daos_types.h>
#include <daos/dtx.h>
#include <daos/placement.h>
#include <daos/event.h>
#include <daos/mgmt.h>
#include <math.h>
#include "pipeline_internal.h"
#include "pipeline_rpc.h"

struct pipeline_auxi_args {
	int           opc;         /** Only one OPC for now (pipeline_run) */
	uint32_t      map_ver_req; /** For I/O retry */
	daos_obj_id_t omd_id;      /** I AM SETTING THIS BUT NOT USING IT */
	tse_task_t   *api_task;    /** For I/O retry */
	d_list_t      shard_task_head;
};

struct shard_pipeline_run_args {
	uint32_t                   pra_map_ver; /** I AM SETTING THIS BUT NOT USING IT */
	uint32_t                   pra_shard;
	uint32_t                   pra_target;

	daos_pipeline_run_t       *pra_api_args;
	daos_unit_oid_t            pra_oid;
	uuid_t                     pra_coh_uuid;
	uuid_t                     pra_cont_uuid;
	/*uint64_t			pra_dkey_hash; ??*/

	struct pipeline_auxi_args *pipeline_auxi;
};

/** shard completion call back arguments */
struct pipeline_run_cb_args {
	uint32_t             shard;
	crt_rpc_t           *rpc;
	unsigned int        *map_ver; /**
				       * I AM SETTING THIS BUT NOT USING IT.
				       * TODO: Do a pool map refersh to update this.
				       */
	daos_pipeline_run_t *api_args;
	uint32_t             nr_iods;
	uint32_t             nr_kds;
};

/** final complete call back arguments */
struct pipeline_comp_cb_args {
	daos_pipeline_run_t     *api_args;
	struct daos_oclass_attr *oca;
	uint32_t                 total_shards;
	uint32_t                 total_replicas;
};

int
dc_pipeline_check(daos_pipeline_t *pipeline)
{
	return d_pipeline_check(pipeline);
}

static void
anchor_check_eof(daos_anchor_t *anchor, struct daos_oclass_attr *oca, uint32_t total_shards,
		 uint32_t total_replicas)
{
	uint32_t shard;

	if (!daos_anchor_is_eof(anchor) || daos_anchor_get_flags(anchor) & DIOF_TO_SPEC_SHARD) {
		/** anchor is not EOF or the user used split anchors (no need to advance shard) */
		return;
	}

	shard = dc_obj_anchor2shard(anchor);
	if (shard + total_replicas < total_shards) {
		/** there are shards left; advancing to the next one */
		shard += total_replicas;
		daos_anchor_set_zero(anchor);
		dc_obj_shard2anchor(anchor, shard);
	}
}

static bool
first_ever_cb(daos_anchor_t *anchor, uint32_t shard)
{
	return daos_anchor_is_zero(anchor) &&
		(shard == 0 || (daos_anchor_get_flags(anchor) & DIOF_TO_SPEC_SHARD));
}

static int
pipeline_comp_cb(tse_task_t *task, void *data)
{
	struct pipeline_comp_cb_args  *cb_args;
	daos_pipeline_run_t           *api_args;

	cb_args  = (struct pipeline_comp_cb_args *)data;
	api_args = cb_args->api_args;
	if (task->dt_result != 0)
		D_DEBUG(DB_IO, "pipeline_comp_db task=%p result=%d\n", task, task->dt_result);

	anchor_check_eof(api_args->anchor, cb_args->oca, cb_args->total_shards,
			 cb_args->total_replicas);
	return 0;
}

static int
pipeline_shard_run_cb(tse_task_t *task, void *data)
{
	struct pipeline_run_cb_args  *cb_args;
	daos_pipeline_run_t          *api_args;
	struct pipeline_run_out      *pro; /** received data from srv */
	int                           opc;
	int                           ret = task->dt_result;
	int                           rc  = 0;
	crt_rpc_t                    *rpc;
	uint32_t                      nr_iods;
	uint32_t                      nr_kds;
	uint32_t                      nr_agg;
	uint32_t                      i;

	cb_args  = (struct pipeline_run_cb_args *)data;
	api_args = cb_args->api_args;
	rpc      = cb_args->rpc;
	opc      = opc_get(rpc->cr_opc);

	if (ret != 0) {
		D_ERROR("RPC %d failed, " DF_RC "\n", opc, DP_RC(ret));
		D_GOTO(out, ret);
	}

	pro          = (struct pipeline_run_out *)crt_reply_get(rpc);
	rc           = pro->pro_ret; /** get status */

	nr_iods      = cb_args->nr_iods;
	nr_kds       = cb_args->nr_kds;
	nr_agg       = api_args->pipeline->num_aggr_filters;

	D_ASSERT(pro->pro_nr_kds             <= nr_kds);
	D_ASSERT(pro->pro_kds.ca_count       <= pro->pro_nr_kds);
	D_ASSERT(pro->pro_sgl_keys.sg_nr_out <= pro->pro_nr_kds);
	D_ASSERT(pro->pro_nr_iods            <= nr_iods);
	D_ASSERT(pro->pro_recx_size.ca_count <= pro->pro_nr_iods * pro->pro_nr_kds);
	D_ASSERT(pro->pro_sgl_recx.sg_nr_out <= pro->pro_nr_iods * pro->pro_nr_kds);
	D_ASSERT(pro->pro_sgl_agg.sg_nr_out  == nr_agg);

	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_GOTO(out, rc = 0);
		if (rc == -DER_INPROGRESS || rc == -DER_TX_BUSY)
			D_DEBUG(DB_TRACE, "rpc %p RPC %d may need retry: %d\n", rpc, opc, rc);
		else
			D_ERROR("rpc %p RPC %d failed: %d\n", rpc, opc, rc);
		D_GOTO(out, rc);
	}

	if (pro->pro_kds.ca_count > 0) {
		/** copying key descriptors */
		memcpy((void *)api_args->kds, (void *)pro->pro_kds.ca_arrays,
		       sizeof(*api_args->kds) * pro->pro_kds.ca_count);
	}
	if (pro->pro_sgl_keys.sg_nr_out > 0) {
		/** copying keys */
		rc = daos_sgls_copy_data_out(api_args->sgl_keys, 1, &pro->pro_sgl_keys, 1);
		if (rc != 0)
			D_GOTO(out, rc);
	}
	if (api_args->recx_size != NULL && pro->pro_recx_size.ca_count > 0) {
		/** copying records' size */
		memcpy((void *)api_args->recx_size, (void *)pro->pro_recx_size.ca_arrays,
		       sizeof(*api_args->recx_size) * pro->pro_recx_size.ca_count);
	}
	if (pro->pro_sgl_recx.sg_nr_out > 0) {
		/** copying record data (akeys' values) */
		rc = daos_sgls_copy_data_out(api_args->sgl_recx, 1, &pro->pro_sgl_recx, 1);
		if (rc != 0)
			D_GOTO(out, rc);
	}
	for (i = 0; i < nr_agg; i++) {
		/** copying aggregation buffers */
		double             *src, *dst;
		daos_filter_part_t *part;
		char               *part_type;
		size_t              length_part_type;

		dst = (double *)api_args->sgl_agg->sg_iovs[i].iov_buf;
		src = (double *)pro->pro_sgl_agg.sg_iovs[i].iov_buf;

		if (first_ever_cb(api_args->anchor, cb_args->shard)) {
			/**
			 * This is the first time ever that this callback is executed for
			 * this particular pipeline run.
			 */
			*dst = *src;
			continue;
		}

		part             = api_args->pipeline->aggr_filters[i]->parts[0];
		part_type        = (char *)part->part_type.iov_buf;

		length_part_type = strlen("DAOS_FILTER_FUNC_XXX");
					/**
					* XXX: we can do this because all function names
					* are the same length. Change this in the future
					* if needed.
					*/

		if (!strncmp(part_type, "DAOS_FILTER_FUNC_SUM", length_part_type) ||
		    !strncmp(part_type, "DAOS_FILTER_FUNC_AVG", length_part_type)) {
			*dst += *src;
		} else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MIN", length_part_type)) {
			if (*src < *dst)
				*dst = *src;
		} else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MAX", length_part_type)) {
			if (*src > *dst)
				*dst = *src;
		}
		api_args->sgl_agg->sg_iovs[i].iov_len = sizeof(double);
	}
	if (nr_agg > 0)
		api_args->sgl_agg->sg_nr_out = nr_agg;

	*api_args->nr_kds  = pro->pro_nr_kds;
	*api_args->nr_iods = pro->pro_nr_iods;

	if (api_args->stats != NULL) {
		/** user wants stats */
		if (first_ever_cb(api_args->anchor, cb_args->shard)) {
			*api_args->stats = pro->stats;
		} else {
			api_args->stats->nr_objs += pro->stats.nr_objs;
			api_args->stats->nr_dkeys += pro->stats.nr_dkeys;
			api_args->stats->nr_akeys += pro->stats.nr_akeys;
		}
	}

	/** anchor should always be updated at the end */
	*api_args->anchor = pro->pro_anchor;

out:
	crt_req_decref(rpc);
	tse_task_list_del(task);
	tse_task_decref(task);

	if (ret == 0) /** XXX: see obj_retry_error(int err) when retry I/O is implemented */
		ret = rc;
	return ret;
}

static void
shard_pipeline_set_buffers_to_zero(d_sg_list_t *sgl)
{
	uint32_t i;

	if (sgl != NULL && sgl->sg_iovs != NULL) {
		for (i = 0; i < sgl->sg_nr; i++)
			sgl->sg_iovs[i].iov_len = 0;
		sgl->sg_nr_out = 0;
	}
}

#define KDS_BULK_LIMIT	128

static int
shard_pipeline_run_task(tse_task_t *task)
{
	struct shard_pipeline_run_args  *args;
	crt_rpc_t                       *req;
	crt_context_t                    crt_ctx;
	crt_opcode_t                     opcode;
	crt_endpoint_t                   tgt_ep;
	daos_handle_t                    coh;
	daos_handle_t                    poh;
	struct dc_pool                  *pool = NULL;
	struct pool_target              *map_tgt;
	struct pipeline_run_cb_args      cb_args;
	struct pipeline_run_in          *pri;
	uint32_t                         nr_kds;
	uint32_t                         nr_iods;
	daos_size_t                      size;
	bool                             no_aggregation;
	int                              rc;

	args    = tse_task_buf_embedded(task, sizeof(*args));
	crt_ctx = daos_task2ctx(task);
	opcode =
	    DAOS_RPC_OPCODE(args->pipeline_auxi->opc, DAOS_PIPELINE_MODULE, DAOS_PIPELINE_VERSION);
	coh  = dc_obj_hdl2cont_hdl(args->pra_api_args->oh);
	poh  = dc_cont_hdl2pool_hdl(coh);
	pool = dc_hdl2pool(poh);
	if (pool == NULL) {
		D_WARN("Cannot find valid pool\n");
		D_GOTO(out, rc = -DER_NO_HDL);
	}
	rc = dc_cont_tgt_idx2ptr(coh, args->pra_target, &map_tgt);
	if (rc != 0)
		D_GOTO(out, rc);

	tgt_ep.ep_grp  = pool->dp_sys->sy_group;
	/* tgt_ep.ep_tag	= map_tgt->ta_comp.co_index; */
	tgt_ep.ep_tag  = daos_rpc_tag(DAOS_REQ_IO, map_tgt->ta_comp.co_index);
	tgt_ep.ep_rank = map_tgt->ta_comp.co_rank;

	rc             = crt_req_create(crt_ctx, &tgt_ep, opcode, &req);
	if (rc != 0)
		D_GOTO(out, rc);

	/** -- nr_iods, nr_kds for this shard */

	nr_iods        = *args->pra_api_args->nr_iods;
	nr_kds         = *args->pra_api_args->nr_kds;

	/** -- call back function arguments */

	crt_req_addref(req);
	cb_args.shard        = args->pra_shard;
	cb_args.rpc          = req;
	cb_args.map_ver      = &args->pra_map_ver;
	cb_args.api_args     = args->pra_api_args;
	cb_args.nr_iods      = nr_iods;
	cb_args.nr_kds       = nr_kds;

	/**
	 * -- Forcing iov buffers to be empty. Pipeline API is read only for now, so we don't need
	 *  to be careful here. This operation is necessary to avoid sending empty buffers over with
	 *  the RPC. We only need to send the structure of the sgl, not its contents.
	 *  An exception happens with sgl_aggr, which can have valid contents from previous runs.
	 *  However, the content is not needed on the server, since it is merged on the client when
	 *  the call back is executed.
	 */
	shard_pipeline_set_buffers_to_zero(args->pra_api_args->sgl_keys);
	shard_pipeline_set_buffers_to_zero(args->pra_api_args->sgl_recx);
	if (args->pra_api_args->pipeline->num_aggr_filters != 0)
		shard_pipeline_set_buffers_to_zero(args->pra_api_args->sgl_agg);

	/** -- sending the RPC */

	pri = crt_req_get(req);
	D_ASSERT(pri != NULL);
	pri->pri_pipe = *args->pra_api_args->pipeline;
	pri->pri_oid  = args->pra_oid;
	/**
	 * No EPR for now. Using pri_epr to pass epoch values
	 * -> lo for oe_first, and hi for oe_value
	 */
	pri->pri_epr  = (daos_epoch_range_t){.epr_lo = 0, .epr_hi = DAOS_EPOCH_MAX};
	if (args->pra_api_args->dkey != NULL)
		pri->pri_dkey = *args->pra_api_args->dkey;
	else
		pri->pri_dkey = (daos_key_t){.iov_buf = NULL, .iov_buf_len = 0, .iov_len = 0};

	pri->pri_iods.nr      = nr_iods;
	pri->pri_iods.iods    = args->pra_api_args->iods;
	pri->pri_sgl_keys     = *args->pra_api_args->sgl_keys;
	pri->pri_sgl_recx     = *args->pra_api_args->sgl_recx;

	if (!args->pra_api_args->pipeline->num_aggr_filters) {
		pri->pri_sgl_agg = (d_sg_list_t){.sg_nr = 0, .sg_nr_out = 0, .sg_iovs = NULL};
	} else {
		pri->pri_sgl_agg = *args->pra_api_args->sgl_agg;
	}
	D_ASSERT(pri->pri_sgl_agg.sg_nr == args->pra_api_args->pipeline->num_aggr_filters);

	pri->pri_anchor       = *args->pra_api_args->anchor;
	pri->pri_flags        = args->pra_api_args->flags;
	pri->pri_nr_kds       = nr_kds;
	uuid_copy(pri->pri_pool_uuid, pool->dp_pool);
	uuid_copy(pri->pri_co_hdl, args->pra_coh_uuid);
	uuid_copy(pri->pri_co_uuid, args->pra_cont_uuid);

	/** Transfer in bulk set up */

	no_aggregation = !args->pra_api_args->pipeline->num_aggr_filters;
	/**
	 * No need to transfer in bulk kds and sgl_keys if aggregation is to be performed, since
	 * only one dkey is actually returned.
	 */
	if (no_aggregation && nr_kds > KDS_BULK_LIMIT) {
		d_sg_list_t	tmp_sgl = {0};
		d_iov_t		tmp_iov = {0};

		tmp_iov.iov_buf_len	= sizeof(*args->pra_api_args->kds) * nr_kds;
		tmp_iov.iov_buf		= args->pra_api_args->kds;
		tmp_sgl.sg_nr_out	= 1;
		tmp_sgl.sg_nr		= 1;
		tmp_sgl.sg_iovs		= &tmp_iov;

		rc = crt_bulk_create(crt_ctx, &tmp_sgl, CRT_BULK_RW, &pri->pri_kds_bulk);
		if (rc < 0)
			D_GOTO(out_req, rc);
	}
	/** everything else is based on packed size */
	size = 0;
	if (args->pra_api_args->recx_size != NULL && nr_iods > 0) {
		size += nr_iods * nr_kds * sizeof(daos_size_t);
		if (size >= DAOS_BULK_LIMIT) {
			d_sg_list_t	tmp_sgl = {0};
			d_iov_t		tmp_iov = {0};

			tmp_iov.iov_buf_len	= nr_iods * nr_kds * sizeof(daos_size_t);
			tmp_iov.iov_buf		= args->pra_api_args->recx_size;
			tmp_sgl.sg_nr_out	= 1;
			tmp_sgl.sg_nr		= 1;
			tmp_sgl.sg_iovs		= &tmp_iov;

			rc = crt_bulk_create(crt_ctx, &tmp_sgl, CRT_BULK_RW, &pri->pri_iods_bulk);
			if (rc < 0)
				D_GOTO(out_req, rc);
		}
	}
	if (nr_kds > 0) {
		if (no_aggregation && args->pra_api_args->sgl_keys != NULL) {
			size += daos_sgls_packed_size(args->pra_api_args->sgl_keys, 1, NULL);
			if (size >= DAOS_BULK_LIMIT) {
				rc = crt_bulk_create(crt_ctx, args->pra_api_args->sgl_keys,
						     CRT_BULK_RW, &pri->pri_sgl_keys_bulk);
				if (rc < 0)
					D_GOTO(out_req, rc);
			}
		}
		if (args->pra_api_args->sgl_recx != NULL) {
			size += daos_sgls_packed_size(args->pra_api_args->sgl_recx, 1, NULL);
			if (size >= DAOS_BULK_LIMIT) {
				rc = crt_bulk_create(crt_ctx, args->pra_api_args->sgl_recx,
						     CRT_BULK_RW, &pri->pri_sgl_recx_bulk);
				if (rc < 0)
					D_GOTO(out_req, rc);
			}
		}
	}

	/** -- register call back function for this particular shard task */

	rc = tse_task_register_comp_cb(task, pipeline_shard_run_cb, &cb_args, sizeof(cb_args));
	if (rc != 0)
		D_GOTO(out_req, rc);

	/** -- finally sending rpc... */

	rc = daos_rpc_send(req, task);

	/** -- exit */

	dc_pool_put(pool);
	return rc;
out_req:
	crt_req_decref(req);
	crt_req_decref(req);
out:
	if (pool)
		dc_pool_put(pool);
	tse_task_complete(task, rc);
	return rc;
}

static int
shard_pipeline_task_abort(tse_task_t *task, void *arg)
{
	int rc = *((int *)arg);

	tse_task_list_del(task);
	tse_task_decref(task);
	tse_task_complete(task, rc);

	return 0;
}

static int
queue_shard_pipeline_run_task(tse_task_t *api_task, struct pl_obj_layout *layout,
			      struct pipeline_auxi_args *pipeline_auxi, int shard,
			      unsigned int map_ver, daos_unit_oid_t oid, uuid_t coh_uuid,
			      uuid_t cont_uuid)
{
	daos_pipeline_run_t             *api_args;
	tse_sched_t                     *sched;
	tse_task_t                      *task;
	struct shard_pipeline_run_args  *args;
	int                              rc;

	api_args = dc_task_get_args(api_task);
	sched    = tse_task2sched(api_task);
	rc       = tse_task_create(shard_pipeline_run_task, sched, NULL, &task);
	if (rc != 0)
		D_GOTO(out_task, rc);

	args                = tse_task_buf_embedded(task, sizeof(*args));
	args->pra_api_args  = api_args;
	args->pra_map_ver   = map_ver;
	args->pra_shard     = shard;
	args->pra_oid       = oid;
	args->pipeline_auxi = pipeline_auxi;
	args->pra_target    = layout->ol_shards[shard].po_target;
	uuid_copy(args->pra_coh_uuid, coh_uuid);
	uuid_copy(args->pra_cont_uuid, cont_uuid);
	rc = tse_task_register_deps(api_task, 1, &task);
	if (rc != 0)
		D_GOTO(out_task, rc);

	tse_task_addref(task);
	tse_task_list_add(task, &pipeline_auxi->shard_task_head);

out_task:
	if (rc)
		tse_task_complete(task, rc);
	return rc;
}

struct shard_task_sched_args {
	bool tsa_scheded;
};

static int
shard_task_sched(tse_task_t *task, void *arg)
{
	struct shard_task_sched_args *sched_arg = arg;
	int                           rc        = 0;

	/** TODO: Retry I/O */

	tse_task_schedule(task, true);
	sched_arg->tsa_scheded = true;

	return rc;
}

static int
pipeline_create_layout(daos_handle_t coh, struct dc_pool *pool, struct daos_obj_md *obj_md,
		       struct pl_obj_layout **layout)
{
	int            rc = 0;
	struct pl_map *map;

	map = pl_map_find(pool->dp_pool, obj_md->omd_id);
	if (map == NULL) {
		D_DEBUG(DB_PL, "Cannot find valid placement map\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = pl_obj_place(map, 0, obj_md, 0, NULL, layout);
	pl_map_decref(map);
	if (rc != 0) {
		D_DEBUG(DB_PL, "Failed to generate object layout\n");
		D_GOTO(out, rc);
	}
	D_DEBUG(DB_PL, "Place object on %d targets ver %d\n", (*layout)->ol_nr, (*layout)->ol_ver);
	D_ASSERT((*layout)->ol_nr == (*layout)->ol_grp_size * (*layout)->ol_grp_nr);

out:
	return rc;
}

static void
pipeline_create_auxi(tse_task_t *api_task, uint32_t map_ver, struct daos_obj_md *obj_md,
		     struct pipeline_auxi_args **pipeline_auxi)
{
	struct pipeline_auxi_args *p_auxi;
	d_list_t		  *head = NULL;

	p_auxi                          = tse_task_stack_push(api_task, sizeof(*p_auxi));
	p_auxi->opc                     = DAOS_PIPELINE_RPC_RUN;
	p_auxi->map_ver_req             = map_ver;
	p_auxi->omd_id                  = obj_md->omd_id;
	p_auxi->api_task                = api_task;
	head                            = &p_auxi->shard_task_head;
	D_INIT_LIST_HEAD(head);

	*pipeline_auxi = p_auxi;
}

int
dc_pipeline_run(tse_task_t *api_task)
{
	daos_pipeline_run_t          *api_args = dc_task_get_args(api_task);
	struct pl_obj_layout         *layout   = NULL;
	daos_handle_t                 coh;
	struct daos_obj_md            obj_md;
	daos_handle_t                 poh;
	struct dc_pool               *pool;
	struct daos_oclass_attr      *oca;
	int                           rc;
	d_list_t                      *shard_task_head = NULL;
	daos_unit_oid_t               oid;
	uint32_t                      map_ver = 0;
	uuid_t                        coh_uuid;
	uuid_t                        cont_uuid;
	struct pipeline_auxi_args    *pipeline_auxi;
	uint32_t                      nr_grps;
	struct shard_task_sched_args  sched_arg;
	int                           total_shards;
	int                           total_replicas;
	int                           shard;
	struct pipeline_comp_cb_args  comp_cb_args;

	coh = dc_obj_hdl2cont_hdl(api_args->oh);
	rc  = dc_obj_hdl2obj_md(api_args->oh, &obj_md);
	if (rc != 0)
		D_GOTO(out, rc);

	poh  = dc_cont_hdl2pool_hdl(coh);
	pool = dc_hdl2pool(poh);
	if (pool == NULL) {
		D_WARN("Cannot find valid pool\n");
		D_GOTO(out, rc = -DER_NO_HDL);
	}
	obj_md.omd_ver = dc_pool_get_version(pool);
	obj_md.omd_pdom_lvl = dc_obj_hdl2pdom(api_args->oh);
	obj_md.omd_fdom_lvl = dc_obj_hdl2redun_lvl(api_args->oh);
	obj_md.omd_pda = dc_obj_hdl2pda(api_args->oh);


	/** get layout */

	rc             = pipeline_create_layout(coh, pool, &obj_md, &layout);
	dc_pool_put(pool);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = dc_cont_hdl2uuid(coh, &coh_uuid, &cont_uuid);
	if (rc != 0)
		D_GOTO(out, rc);

	/** object class */

	oca = daos_oclass_attr_find(obj_md.omd_id, &nr_grps);
	if (oca == NULL) {
		D_DEBUG(DB_PL, "Failed to find oclass attr\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (daos_oclass_is_ec(oca)) {
		D_DEBUG(DB_PL, "EC objects not supported for pipelines\n");
		D_GOTO(out, rc = -DER_NOTSUPPORTED);
	}

	/** Not supporting EC objects yet, so #groups x #replicas = #shards */

	total_replicas = layout->ol_grp_size;
	total_shards   = layout->ol_grp_nr * total_replicas;

	/**
	 * Pipelines are read only for now, no transactions needed.
	 * Ignoring api_args->th for now...
	 */

	if (map_ver == 0)
		map_ver = layout->ol_ver;

	/** -- Register completion call back function for full operation */

	comp_cb_args.api_args       = api_args;
	comp_cb_args.oca            = oca;
	comp_cb_args.total_shards   = total_shards;
	comp_cb_args.total_replicas = total_replicas;

	pipeline_create_auxi(api_task, map_ver, &obj_md, &pipeline_auxi);

	rc = tse_task_register_comp_cb(api_task, pipeline_comp_cb, &comp_cb_args,
				       sizeof(comp_cb_args));
	if (rc != 0) {
		D_ERROR("task %p, register_comp_cb " DF_RC "\n", api_task, DP_RC(rc));
		tse_task_stack_pop(api_task, sizeof(struct pipeline_auxi_args));
		D_GOTO(out, rc);
	}

	/** current shard */

	shard           = dc_obj_anchor2shard(api_args->anchor);

	/** object id */
	oid.id_pub		= obj_md.omd_id;
	oid.id_shard		= shard;
	oid.id_layout_ver	= dc_obj_hdl2layout_ver(api_args->oh);
	oid.id_padding		= 0;

	/** queue shard task */

	shard_task_head = &pipeline_auxi->shard_task_head;
	D_ASSERT(d_list_empty(shard_task_head));

	rc = queue_shard_pipeline_run_task(api_task, layout, pipeline_auxi, shard, map_ver, oid,
					   coh_uuid, cont_uuid);
	if (rc)
		D_GOTO(out, rc);

	/* -- schedule queued shard task */

	D_ASSERT(!d_list_empty(shard_task_head));
	sched_arg.tsa_scheded = false;
	tse_task_list_traverse(shard_task_head, shard_task_sched, &sched_arg);
	if (!sched_arg.tsa_scheded)
		tse_task_complete(api_task, 0);

	pl_obj_layout_free(layout);
	return rc;
out:
	if (shard_task_head != NULL && !d_list_empty(shard_task_head))
		tse_task_list_traverse(shard_task_head, shard_pipeline_task_abort, &rc);
	if (layout != NULL)
		pl_obj_layout_free(layout);
	tse_task_complete(api_task, rc);

	return rc;
}
