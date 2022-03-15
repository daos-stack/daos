/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(pipeline)

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
	int			opc;
	uint32_t		map_ver_req;         // FOR IO RETRY
	daos_obj_id_t		omd_id;              // I AM SETTING BUT NOT REALLY USING THIS YET
	tse_task_t		*api_task;           // FOR IO RETRY
	pthread_rwlock_t	*cb_rwlock;
	bool			*cb_first;
	uint32_t		*total_aggr_shards;
	d_list_t		shard_task_head;
};

struct shard_pipeline_run_args {
	uint32_t			pra_map_ver;// I AM SETTING BUT NOT REALLY USING THIS YET
	uint32_t			pra_shard;
	uint32_t			pra_shards;
	uint32_t			pra_target;

	daos_pipeline_run_t		*pra_api_args;
	daos_unit_oid_t			pra_oid;
	uuid_t				pra_coh_uuid;
	uuid_t				pra_cont_uuid;
	//uint64_t			pra_dkey_hash; // ??
	
	struct pipeline_auxi_args	*pipeline_auxi;
};

struct pipeline_run_cb_args {
	uint32_t		shard;
	crt_rpc_t		*rpc;
	unsigned int		*map_ver; // I AM SETTING BUT NOT REALLY USING THIS YET
	daos_pipeline_run_t	*api_args;
	uint32_t		nr_iods;
	uint32_t		nr_kds;
	uint32_t		shard_nr_kds;
	bool			*first;
	pthread_rwlock_t	*rwlock;
	uint32_t		*total_aggr_shards;
};

struct pipeline_comp_cb_args {
	bool				*shard_cb_first;
	pthread_rwlock_t		*rwlock;
	daos_pipeline_run_t		*api_args;
	struct daos_oclass_attr		*oca;
	uint32_t			total_shards;
	uint32_t			*total_aggr_shards;
};

int dc_pipeline_check(daos_pipeline_t *pipeline)
{
	return d_pipeline_check(pipeline);
}

static void
anchor_check_eof(daos_anchor_t *anchor, struct daos_oclass_attr *oca,
		 uint32_t total_shards)
{
	int i;

	if (anchor->da_sub_anchors == 0)
	{
		return;
	}

	for (i = 0; i < total_shards; i++)
	{
		daos_anchor_t *sub_anchor =
			&((daos_anchor_t *) anchor->da_sub_anchors)[i];
		if (!daos_anchor_is_eof(sub_anchor))
		{
			break;
		}
	}

	if (i == total_shards) /** all sub_anchors are eof */
	{
		void *ptr = (void *) anchor->da_sub_anchors;

		daos_anchor_set_eof(anchor);
		D_FREE(ptr);
		anchor->da_sub_anchors = 0;
	}
}

static int
pipeline_comp_cb(tse_task_t *task, void *data)
{
	struct pipeline_comp_cb_args	*cb_args;
	daos_pipeline_run_t		*api_args;

	cb_args  = (struct pipeline_comp_cb_args *) data;
	api_args = cb_args->api_args;
	if (task->dt_result != 0)
	{
		D_DEBUG(DB_IO, "pipeline_comp_db task=%p result=%d\n",
			task, task->dt_result);
	}

	anchor_check_eof(api_args->anchor, cb_args->oca, cb_args->total_shards);

	if (*cb_args->total_aggr_shards > 0)
	{
		pipeline_aggregations_fixavgs(
					   &api_args->pipeline,
					   (double) *cb_args->total_aggr_shards,
					   api_args->sgl_agg);
	}

	D_RWLOCK_DESTROY(cb_args->rwlock);
	D_FREE(cb_args->rwlock);
	D_FREE(cb_args->shard_cb_first);
	D_FREE(cb_args->total_aggr_shards);

	return 0;
}

static int
pipeline_shard_run_cb(tse_task_t *task, void *data)
{
	struct pipeline_run_cb_args	*cb_args;
	daos_pipeline_run_t		*api_args;
	struct pipeline_run_in		*pri;
	struct pipeline_run_out		*pro;
	int				opc;
	int				ret = task->dt_result;
	int				rc = 0;
	crt_rpc_t			*rpc;
	uint32_t			nr_iods;
	uint32_t			shard_nr_kds;
	uint32_t			shard_nr_recx;
	uint32_t			nr_agg;
	uint32_t			i;
	daos_key_desc_t			*kds_ptr;
	d_sg_list_t			*sgl_keys_ptr;
	d_sg_list_t			*sgl_recx_ptr;
	uint32_t			out_nr_recx;
	daos_anchor_t			*sub_anchors;


	cb_args		= (struct pipeline_run_cb_args *) data;
	api_args	= cb_args->api_args;
	rpc		= cb_args->rpc;
	pri		= crt_req_get(rpc);
	D_ASSERT(pri != NULL);
	opc = opc_get(rpc->cr_opc);

	if (ret != 0)
	{
		D_ERROR("RPC %d failed, "DF_RC"\n", opc, DP_RC(ret));
		D_GOTO(out, ret);
	}

	pro		= (struct pipeline_run_out *) crt_reply_get(rpc);
	rc		= pro->pro_ret; // get status

	nr_iods 	= cb_args->nr_iods;
	shard_nr_kds	= cb_args->shard_nr_kds;
	shard_nr_recx	= nr_iods*shard_nr_kds;
	nr_agg		= api_args->pipeline.num_aggr_filters;

	D_ASSERT(pro->pro_kds.ca_count <= shard_nr_kds);
	D_ASSERT(pro->pro_sgl_recx.ca_count <= shard_nr_recx);
	D_ASSERT(pro->pro_sgl_agg.ca_count == nr_agg);

	if (rc != 0)
	{
		if (rc == -DER_NONEXIST)
		{
			D_GOTO(out, rc = 0);
		}
		if (rc == -DER_INPROGRESS || rc == -DER_TX_BUSY)
		{
			D_DEBUG(DB_TRACE, "rpc %p RPC %d may need retry: %d\n",
				rpc, opc, rc);
		}
		else
		{
			D_ERROR("rpc %p RPC %d failed: %d\n", rpc, opc, rc);
		}
		D_GOTO(out, rc);
	}

	D_RWLOCK_WRLOCK(cb_args->rwlock);

	if (*cb_args->first == true)
	{
		*api_args->nr_kds = 0;
	}

	if (pro->pro_kds.ca_count > 0)
	{
		if (nr_agg == 0)
		{
			kds_ptr		= &api_args->kds[*api_args->nr_kds];
			sgl_keys_ptr	= &api_args->sgl_keys[*api_args->nr_kds];
		}
		else
		{
			kds_ptr		= api_args->kds;
			sgl_keys_ptr	= api_args->sgl_keys; /** only one record */
		}

		memcpy((void *) kds_ptr, (void *) pro->pro_kds.ca_arrays,
		       sizeof(*kds_ptr) * (pro->pro_kds.ca_count));

		rc = daos_sgls_copy_data_out(sgl_keys_ptr,
					     shard_nr_kds,
					     pro->pro_sgl_keys.ca_arrays,
					     pro->pro_kds.ca_count);
		if (rc != 0)
		{
			D_GOTO(unlock, rc);
		}
	}

	if (pro->pro_sgl_recx.ca_count > 0)
	{
		if (nr_agg == 0)
		{
			out_nr_recx	= (*api_args->nr_kds) * nr_iods;
			sgl_recx_ptr	= &api_args->sgl_recx[out_nr_recx];
		}
		else
		{
			sgl_recx_ptr	= api_args->sgl_recx; /** only one record */
		}

		rc = daos_sgls_copy_data_out(sgl_recx_ptr,
					     shard_nr_recx,
					     pro->pro_sgl_recx.ca_arrays,
					     pro->pro_sgl_recx.ca_count);
		if (rc != 0)
		{
			D_GOTO(unlock, rc);
		}
	}

	for (i = 0; i < nr_agg; i++)
	{
		daos_filter_t *aggr_filter = api_args->pipeline.aggr_filters[i];
		daos_filter_part_t *part   = aggr_filter->parts[0];
		double *src, *dst;
		size_t lenght_part_type;

		dst = (double *) api_args->sgl_agg[i].sg_iovs->iov_buf;
		src = (double *) pro->pro_sgl_agg.ca_arrays[i].sg_iovs->iov_buf;

		if (*cb_args->first == true)
		{
			*dst = *src;
			continue;
		}

		// TODO: right now, all functions' names are same lenght.
		//       change this in the future if the lenght changes
		lenght_part_type = 20;

		if (!strncmp(part->part_type.iov_buf, "DAOS_FILTER_FUNC_SUM",
			     lenght_part_type))
		{
			*dst += *src;
		}
		else if (!strncmp(part->part_type.iov_buf, "DAOS_FILTER_FUNC_MIN",
				  lenght_part_type))
		{
			if (*src < *dst)
			{
				*dst = *src;
			}
		}
		else if (!strncmp(part->part_type.iov_buf, "DAOS_FILTER_FUNC_MAX",
				  lenght_part_type))
		{
			if (*src > *dst)
			{
				*dst = *src;
			}
		}
		else if (!strncmp(part->part_type.iov_buf, "DAOS_FILTER_FUNC_AVG",
				  lenght_part_type))
		{
			*dst += *src;
		}
	}
	if (nr_agg == 0)
	{
		*api_args->nr_kds += pro->pro_kds.ca_count;
	}
	else if (pro->pro_kds.ca_count == 1)
	{
		*api_args->nr_kds = 1;
		*cb_args->total_aggr_shards += 1;
	}

	/**
	 * TODO: nr_iods and iods are left as they are for now. Once pipeline
	 *       is able to filter/aggregate akeys by a provided dkey, then
	 *       outputing iods and nr_iods will make sense. For now, this is
	 *       IN only.
	 *
	 *       api_args->nr_iods =
	 *       api_args->iods =
	 */

	sub_anchors = (daos_anchor_t *) api_args->anchor->da_sub_anchors;
	sub_anchors[cb_args->shard] = pro->pro_anchor;

unlock:
	*cb_args->first = false;
	D_RWLOCK_UNLOCK(cb_args->rwlock);
out:
	crt_req_decref(rpc);
	if (ret == 0) // see -->> obj_retry_error(int err)
	{
		ret = rc;
	}
	return ret;
}

static int
shard_prepare_anchors(daos_pipeline_run_t *api_args, int nr)
{

	daos_anchor_t	*sub_anchors;
	int		i;

	D_ASSERT(api_args->anchor->da_sub_anchors == 0);
	D_ALLOC_ARRAY(sub_anchors, nr);
	if (sub_anchors == NULL)
	{
		return -DER_NOMEM;
	}

	for (i = 0; i < nr; i++)
	{
		sub_anchors[i] = *api_args->anchor;
	}

	api_args->anchor->da_sub_anchors = (uint64_t) sub_anchors;
	return 0;
}

static int
shard_pipeline_run_task(tse_task_t *task)
{
	struct shard_pipeline_run_args	*args;
	crt_rpc_t			*req;
	crt_context_t			crt_ctx;
	crt_opcode_t			opcode;
	crt_endpoint_t			tgt_ep;
	daos_handle_t			coh;
	daos_handle_t			poh;
	struct dc_pool			*pool = NULL;
	struct pool_target		*map_tgt;
	struct pipeline_run_cb_args	cb_args;
	struct pipeline_run_in		*pri;
	uint32_t			nr_kds;
	uint32_t			nr_iods;
	uint32_t			shard_nr_kds;
	int				rc;
	daos_anchor_t			*da_sub_anchors;


	args = tse_task_buf_embedded(task, sizeof(*args));
	crt_ctx	= daos_task2ctx(task);
	opcode	= DAOS_RPC_OPCODE(args->pipeline_auxi->opc,
				  DAOS_PIPELINE_MODULE,
				  DAOS_PIPELINE_VERSION);

	coh	= dc_obj_hdl2cont_hdl(args->pra_api_args->oh);
	poh	= dc_cont_hdl2pool_hdl(coh);
	pool	= dc_hdl2pool(poh);
	if (pool == NULL)
	{
		D_WARN("Cannot find valid pool\n");
		D_GOTO(out, rc = -DER_NO_HDL);
	}
	rc = dc_cont_tgt_idx2ptr(coh, args->pra_target, &map_tgt);
	if (rc != 0)
	{
		D_GOTO(out, rc);
	}

	tgt_ep.ep_grp	= pool->dp_sys->sy_group;
	tgt_ep.ep_tag	= daos_rpc_tag(DAOS_REQ_IO, map_tgt->ta_comp.co_index);
	tgt_ep.ep_rank	= map_tgt->ta_comp.co_rank;

	rc = crt_req_create(crt_ctx, &tgt_ep, opcode, &req);
	if (rc != 0)
	{
		D_GOTO(out, rc);
	}

	/** -- calculating nr_iods, nr_kds for this shard */

	D_ASSERT(args->pra_shards > 0 && args->pra_shard >= 0);
	D_ASSERT(args->pra_shard < args->pra_shards);

	nr_iods		= *(args->pra_api_args->nr_iods);
	nr_kds		= *(args->pra_api_args->nr_kds);

	shard_nr_kds	= nr_kds / args->pra_shards;
	if (shard_nr_kds * args->pra_shards < nr_kds)
	{
		shard_nr_kds++;
		if (args->pra_shard == args->pra_shards - 1)
		{
			shard_nr_kds = nr_kds % shard_nr_kds;
		}
	}

	/** -- register call back function for this particular shard task */

	crt_req_addref(req);
	cb_args.shard             = args->pra_shard;
	cb_args.rpc               = req;
	cb_args.map_ver           = &args->pra_map_ver;
	cb_args.api_args          = args->pra_api_args;
	cb_args.nr_iods           = nr_iods;
	cb_args.nr_kds            = nr_kds;
	cb_args.shard_nr_kds      = shard_nr_kds;
	cb_args.rwlock            = args->pipeline_auxi->cb_rwlock;
	cb_args.first             = args->pipeline_auxi->cb_first;
	cb_args.total_aggr_shards = args->pipeline_auxi->total_aggr_shards;

	rc = tse_task_register_comp_cb(task, pipeline_shard_run_cb, &cb_args,
				       sizeof(cb_args));
	if (rc != 0)
	{
		D_GOTO(out_req, rc);
	}

	/** -- sending the RPC */

	pri = crt_req_get(req);
	D_ASSERT(pri != NULL);
	pri->pri_pipe		= args->pra_api_args->pipeline;
	pri->pri_oid		= args->pra_oid;
	/**
	  * No EPR for now. Using pri_epr to pass epoch values
	  * -> lo for oe_first, and hi for oe_value
	  */
	pri->pri_epr		= (daos_epoch_range_t)
				 { .epr_lo	= 0,
				   .epr_hi	= DAOS_EPOCH_MAX };
	if (args->pra_api_args->dkey != NULL)
	{
		pri->pri_dkey	= *(args->pra_api_args->dkey);
	}
	else
	{
		pri->pri_dkey	= (daos_key_t)
				 { .iov_buf		= NULL,
				   .iov_buf_len		= 0,
				   .iov_len		= 0 };
	}
	pri->pri_iods.nr	= nr_iods;
	pri->pri_iods.iods	= args->pra_api_args->iods;
	pri->pri_nr_kds		= shard_nr_kds;
	da_sub_anchors		= (daos_anchor_t *)
				     args->pra_api_args->anchor->da_sub_anchors;
	pri->pri_anchor		= da_sub_anchors[args->pra_shard];
	pri->pri_flags		= args->pra_api_args->flags;
	uuid_copy(pri->pri_pool_uuid, pool->dp_pool);
	uuid_copy(pri->pri_co_hdl, args->pra_coh_uuid);
	uuid_copy(pri->pri_co_uuid, args->pra_cont_uuid);

	rc = daos_rpc_send(req, task);

	/** -- exit */

	dc_pool_put(pool);
	return rc;
out_req:
	crt_req_decref(req);
	crt_req_decref(req);
out:
	if (pool)
	{
		dc_pool_put(pool);
	}
	tse_task_complete(task, rc);
	return rc;
}

static int
shard_pipeline_task_abort(tse_task_t *task, void *arg)
{
	int	rc = *((int *)arg);

	tse_task_list_del(task);
	tse_task_decref(task);
	tse_task_complete(task, rc);

	return 0;
}

static int
queue_shard_pipeline_run_task(tse_task_t *api_task, struct pl_obj_layout *layout,
			      struct pipeline_auxi_args *pipeline_auxi,
			      int shard, int shards, unsigned int map_ver,
			      daos_unit_oid_t oid, uuid_t coh_uuid,
			      uuid_t cont_uuid)
{
	daos_pipeline_run_t		*api_args;
	tse_sched_t			*sched;
	tse_task_t			*task;
	struct shard_pipeline_run_args	*args;
	int				rc;

	api_args	= dc_task_get_args(api_task);
	sched		= tse_task2sched(api_task);
	rc		= tse_task_create(shard_pipeline_run_task,
					  sched, NULL, &task);
	if (rc != 0)
	{
		D_GOTO(out_task, rc);
	}

	args = tse_task_buf_embedded(task, sizeof(*args));
	args->pra_api_args	= api_args;
	args->pra_map_ver	= map_ver;
	args->pra_shard		= shard;
	args->pra_shards	= shards;
	args->pra_oid		= oid;
	args->pipeline_auxi	= pipeline_auxi;
	args->pra_target	= layout->ol_shards[shard].po_target;
	uuid_copy(args->pra_coh_uuid, coh_uuid);
	uuid_copy(args->pra_cont_uuid, cont_uuid);
	rc = tse_task_register_deps(api_task, 1, &task);
	if (rc != 0)
	{
		D_GOTO(out_task, rc);
	}
	tse_task_addref(task);
	tse_task_list_add(task, &pipeline_auxi->shard_task_head);

out_task:
	if (rc)
	{
		tse_task_complete(task, rc);
	}
	return rc;
}

struct shard_task_sched_args {
	bool			tsa_scheded;
};

static int
shard_task_sched(tse_task_t *task, void *arg)
{
	struct shard_task_sched_args		*sched_arg = arg;
	int					rc = 0;
	//struct shard_pipeline_run_args		*shard_args;
	//struct pipeline_auxi_args		*pipeline_auxi;
	//tse_task_t				*api_task;
	//uint32_t				target;
	//uint32_t				map_ver;

	//shard_args    = tse_task_buf_embedded(task, sizeof(*shard_args));
	//pipeline_auxi = shard_args->pipeline_auxi;
	//map_ver       = pipeline_auxi->map_ver_req;
	//api_task      = pipeline_auxi->api_task;

	/** TODO: Retry I/O */
	/**/

	tse_task_schedule(task, true);
	sched_arg->tsa_scheded = true;

	return rc;
}

static int
pipeline_create_layout(daos_handle_t coh, struct dc_pool *pool,
		      struct daos_obj_md *obj_md, struct pl_obj_layout **layout)
{
	int		rc = 0;
	struct pl_map	*map;

	map = pl_map_find(pool->dp_pool, obj_md->omd_id);
	if (map == NULL)
	{
		D_DEBUG(DB_PL, "Cannot find valid placement map\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = pl_obj_place(map, obj_md, 0, NULL, layout);
	pl_map_decref(map);
	if (rc != 0)
	{
		D_DEBUG(DB_PL, "Failed to generate object layout\n");
		D_GOTO(out, rc);
	}
	D_DEBUG(DB_PL, "Place object on %d targets ver %d\n", (*layout)->ol_nr,
		(*layout)->ol_ver);
	D_ASSERT((*layout)->ol_nr == (*layout)->ol_grp_size * (*layout)->ol_grp_nr);

out:
	return rc;
}

static void
pipeline_create_auxi(tse_task_t *api_task, uint32_t map_ver,
		     struct daos_obj_md *obj_md, pthread_rwlock_t *cb_rwlock,
		     bool *shard_cb_first, uint32_t *total_aggr_shards,
		     struct pipeline_auxi_args **pipeline_auxi)
{
	struct pipeline_auxi_args	*p_auxi;
	d_list_t			*head = NULL;

	p_auxi = tse_task_stack_push(api_task, sizeof(*p_auxi));
	p_auxi->opc			= DAOS_PIPELINE_RPC_RUN;
	p_auxi->map_ver_req		= map_ver;
	p_auxi->omd_id			= obj_md->omd_id;
	p_auxi->api_task		= api_task;
	p_auxi->cb_rwlock		= cb_rwlock;
	p_auxi->cb_first		= shard_cb_first;
	p_auxi->total_aggr_shards	= total_aggr_shards;
	head = &p_auxi->shard_task_head;
	D_INIT_LIST_HEAD(head);

	*pipeline_auxi = p_auxi;
}

int
dc_pipeline_run(tse_task_t *api_task)
{
	daos_pipeline_run_t		*api_args = dc_task_get_args(api_task);
	struct pl_obj_layout		*layout = NULL;
	daos_handle_t			coh;
	struct daos_obj_md		obj_md;
	daos_handle_t			poh;
	struct dc_pool			*pool;
	struct daos_oclass_attr		*oca;
	int				rc;
	uint32_t			i;
	d_list_t			*shard_task_head = NULL;
	daos_unit_oid_t			oid;
	uint32_t			map_ver = 0;
	uuid_t				coh_uuid;
	uuid_t				cont_uuid;
	struct pipeline_auxi_args	*pipeline_auxi;
	uint32_t			nr_grps;
	struct shard_task_sched_args	sched_arg;
	int				total_shards;
	struct pipeline_comp_cb_args	comp_cb_args;


	coh	= dc_obj_hdl2cont_hdl(api_args->oh);
	rc	= dc_obj_hdl2obj_md(api_args->oh, &obj_md);
	if (rc != 0)
	{
		D_GOTO(out, rc);
	}
	poh	= dc_cont_hdl2pool_hdl(coh);
	pool	= dc_hdl2pool(poh);
	if (pool == NULL)
	{
		D_WARN("Cannot find valid pool\n");
		D_GOTO(out, rc = -DER_NO_HDL);
	}
	obj_md.omd_ver = dc_pool_get_version(pool);

	/** get layout */

	rc = pipeline_create_layout(coh, pool, &obj_md, &layout);
	dc_pool_put(pool);
	if (rc != 0)
	{
		D_GOTO(out, rc);
	}

	rc = dc_cont_hdl2uuid(coh, &coh_uuid, &cont_uuid);
	if (rc != 0)
	{
		D_GOTO(out, rc);
	}

	/** object class and number of replicas */

	oca = daos_oclass_attr_find(obj_md.omd_id, &nr_grps);
	if (oca == NULL)
	{
		D_DEBUG(DB_PL, "Failed to find oclass attr\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (!daos_oclass_is_ec(oca) ||
			likely(!DAOS_FAIL_CHECK(DAOS_OBJ_SKIP_PARITY)))
	{
		total_shards = layout->ol_grp_nr; /* one replica per group */
	}
	else
	{
		/* groups x data_cells_in_each_group */
		total_shards = layout->ol_grp_nr * oca->u.ec.e_k;
	}

	/** Pipelines are read only for now, no transactions needed.
	  * Ignoring api_args->th for now... */

	if (map_ver == 0)
	{
		map_ver = layout->ol_ver;
	}

	D_ALLOC(comp_cb_args.rwlock, sizeof(*comp_cb_args.rwlock));
	D_ALLOC(comp_cb_args.shard_cb_first,
		sizeof(*comp_cb_args.shard_cb_first));
	D_ALLOC(comp_cb_args.total_aggr_shards,
		sizeof(*comp_cb_args.total_aggr_shards));
	if (comp_cb_args.rwlock == NULL || comp_cb_args.shard_cb_first == NULL
		|| comp_cb_args.total_aggr_shards == NULL)
	{
		D_GOTO(out, rc = -DER_NOMEM);
	}
	rc = D_RWLOCK_INIT(comp_cb_args.rwlock, NULL);
	if (rc != 0)
	{
		D_GOTO(out, rc);
	}
	*comp_cb_args.shard_cb_first	= true;
	comp_cb_args.api_args		= api_args;
	comp_cb_args.oca		= oca;
	comp_cb_args.total_shards	= total_shards;
	*comp_cb_args.total_aggr_shards = 0;

	pipeline_create_auxi(api_task, map_ver, &obj_md, comp_cb_args.rwlock,
			     comp_cb_args.shard_cb_first,
			     comp_cb_args.total_aggr_shards, &pipeline_auxi);

	/** -- Register completion call back function for full operation */

	rc = tse_task_register_comp_cb(api_task, pipeline_comp_cb,
				       &comp_cb_args,
				       sizeof(comp_cb_args));
	if (rc != 0)
	{
		D_ERROR("task %p, register_comp_cb "DF_RC"\n", api_task, DP_RC(rc));
		tse_task_stack_pop(api_task, sizeof(struct pipeline_auxi_args));
		D_GOTO(out, rc);
	}

	/** -- Allocate sub anchors if needed */


	if (api_args->anchor->da_sub_anchors == 0)
	{
		rc = shard_prepare_anchors(api_args, total_shards);
		if (rc != 0)
		{
			D_ERROR("task %p failed to allocate sub_anchors "DF_RC"\n",
								api_task, DP_RC(rc));
			tse_task_stack_pop(api_task, sizeof(struct pipeline_auxi_args));
			D_GOTO(out, rc);
		}
	}

	/** -- Iterate over shards */

	shard_task_head = &pipeline_auxi->shard_task_head;
	D_ASSERT(d_list_empty(shard_task_head));

	for (i = 0; i < layout->ol_grp_nr; i++)
	{
		int start_shard;
		int j;

		/** Try leader for current group */
		start_shard	= i * layout->ol_grp_size;

		if (!daos_oclass_is_ec(oca) ||
				likely(!DAOS_FAIL_CHECK(DAOS_OBJ_SKIP_PARITY)))
		{
			int leader;

			leader	= pl_select_leader(obj_md.omd_id, i,
						   layout->ol_grp_size, NIL_BITMAP,
						   NULL, NULL, pl_obj_get_shard,
						   layout);

			if (leader >= 0)
			{
				oid.id_pub	= obj_md.omd_id;
				oid.id_shard	= leader;
				oid.id_pad_32	= 0;

				rc = queue_shard_pipeline_run_task(
								api_task, layout,
								pipeline_auxi,
								leader,
								total_shards,
								map_ver, oid,
								coh_uuid, cont_uuid
								);
				if (rc)
				{
					D_GOTO(out, rc);
				}
				continue;
			}
			if (!daos_oclass_is_ec(oca))
			{
				/* There has to be a leader for non-EC object */
				D_ERROR(DF_OID" no valid shard, rc " DF_RC"\n",
					DP_OID(obj_md.omd_id), DP_RC(leader));
				D_GOTO(out, rc = leader);
			}
		}

		/** Then try non-leader shards */
		D_DEBUG(DB_IO, DF_OID" try non-leader shards for group %d.\n",
			DP_OID(obj_md.omd_id), i);
		for (j = start_shard; j < start_shard + oca->u.ec.e_k; j++) {
			rc = queue_shard_pipeline_run_task(api_task, layout, pipeline_auxi,
							   j, total_shards,
							   map_ver, oid, coh_uuid,
							   cont_uuid);
			if (rc)
			{
				D_GOTO(out, rc);
			}
		}
	}

	/* -- schedule all the queued shard tasks */

	D_ASSERT(!d_list_empty(shard_task_head));
	sched_arg.tsa_scheded	= false;
	tse_task_list_traverse(shard_task_head, shard_task_sched, &sched_arg);
	if (sched_arg.tsa_scheded == false)
	{
		tse_task_complete(api_task, 0);
	}

	return rc;
out:
	if (shard_task_head != NULL && !d_list_empty(shard_task_head))
	{
		tse_task_list_traverse(shard_task_head, shard_pipeline_task_abort, &rc);
	}
	
	tse_task_complete(api_task, rc);

	return rc;
}
