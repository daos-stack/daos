/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * src/object/srv_coll.c
 *
 * For client side collecitve operation.
 */
#define D_LOGFAC	DD_FAC(object)

#include <uuid/uuid.h>

#include <daos_srv/pool.h>
#include <daos_srv/container.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/dtx_srv.h>
#include "obj_rpc.h"
#include "srv_internal.h"

struct obj_coll_tgt_args {
	crt_rpc_t				*octa_rpc;
	struct daos_coll_shard			*octa_shards;
	uint32_t				*octa_versions;
	uint32_t				 octa_sponsor_tgt;
	struct obj_io_context			*octa_sponsor_ioc;
	struct dtx_handle			*octa_sponsor_dth;
	union {
		void				*octa_misc;
		/* Different collective operations may need different parameters. */
		struct dtx_memberships		*octa_mbs;
		struct obj_tgt_query_args	*octa_otqas;
	};
};

int
obj_coll_local(crt_rpc_t *rpc, struct daos_coll_shard *shards, struct dtx_coll_entry *dce,
	       uint32_t *version, struct obj_io_context *ioc, struct dtx_handle *dth, void *args,
	       obj_coll_func_t func)
{
	struct obj_coll_tgt_args	octa = { 0 };
	struct dss_coll_ops		coll_ops = { 0 };
	struct dss_coll_args		coll_args = { 0 };
	uint32_t			size = dce->dce_bitmap_sz << 3;
	int				rc = 0;
	int				i;

	D_ASSERT(dce->dce_bitmap != NULL);
	D_ASSERT(ioc != NULL);

	if (version != NULL) {
		if (size > dss_tgt_nr)
			size = dss_tgt_nr;
		D_ALLOC_ARRAY(octa.octa_versions, size);
		if (octa.octa_versions == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	octa.octa_rpc = rpc;
	octa.octa_shards = shards;
	octa.octa_misc = args;
	octa.octa_sponsor_ioc = ioc;
	octa.octa_sponsor_dth = dth;
	octa.octa_sponsor_tgt = dss_get_module_info()->dmi_tgt_id;

	coll_ops.co_func = func;
	coll_args.ca_func_args = &octa;
	coll_args.ca_tgt_bitmap = dce->dce_bitmap;
	coll_args.ca_tgt_bitmap_sz = dce->dce_bitmap_sz;

	rc = dss_thread_collective_reduce(&coll_ops, &coll_args, DSS_USE_CURRENT_ULT);

out:
	if (octa.octa_versions != NULL) {
		for (i = 0, *version = 0; i < size; i++) {
			if (isset(dce->dce_bitmap, i) && *version < octa.octa_versions[i])
				*version = octa.octa_versions[i];
		}
		D_FREE(octa.octa_versions);
	}

	return rc;
}

int
obj_coll_tgt_punch(void *args)
{
	struct obj_coll_tgt_args	*octa = args;
	crt_rpc_t			*rpc = octa->octa_rpc;
	struct obj_coll_punch_in	*ocpi = crt_req_get(rpc);
	struct obj_punch_in		*opi = NULL;
	struct obj_tgt_punch_args	 otpa = { 0 };
	uint32_t			 tgt_id = dss_get_module_info()->dmi_tgt_id;
	int				 rc;

	D_ALLOC_PTR(opi);
	if (opi == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	opi->opi_dti = ocpi->ocpi_xid;
	uuid_copy(opi->opi_pool_uuid, ocpi->ocpi_po_uuid);
	uuid_copy(opi->opi_co_hdl, ocpi->ocpi_co_hdl);
	uuid_copy(opi->opi_co_uuid, ocpi->ocpi_co_uuid);
	opi->opi_oid = ocpi->ocpi_oid;
	opi->opi_oid.id_shard = octa->octa_shards[tgt_id].dcs_buf[0];
	opi->opi_epoch = ocpi->ocpi_epoch;
	opi->opi_api_flags = ocpi->ocpi_api_flags;
	opi->opi_map_ver = ocpi->ocpi_map_ver;
	opi->opi_flags = ocpi->ocpi_flags & ~ORF_LEADER;

	otpa.opi = opi;
	otpa.opc = opc_get(rpc->cr_opc);
	if (tgt_id == octa->octa_sponsor_tgt) {
		otpa.sponsor_ioc = octa->octa_sponsor_ioc;
		otpa.sponsor_dth = octa->octa_sponsor_dth;
	}
	otpa.mbs = octa->octa_mbs;
	if (octa->octa_versions != NULL)
		otpa.ver = &octa->octa_versions[tgt_id];
	otpa.data = rpc;

	rc = obj_tgt_punch(&otpa, octa->octa_shards[tgt_id].dcs_buf,
			   octa->octa_shards[tgt_id].dcs_nr);
	D_FREE(opi);

out:
	DL_CDEBUG(rc == 0 || rc == -DER_INPROGRESS || rc == -DER_TX_RESTART, DB_IO, DLOG_ERR, rc,
		  "Collective punch obj shard "DF_UOID" with "DF_DTI" on tgt %u",
		  DP_OID(ocpi->ocpi_oid.id_pub), octa->octa_shards[tgt_id].dcs_buf[0],
		  ocpi->ocpi_oid.id_layout_ver, DP_DTI(&ocpi->ocpi_xid), tgt_id);

	return rc;
}

int
obj_coll_punch_disp(struct dtx_leader_handle *dlh, void *arg, int idx, dtx_sub_comp_cb_t comp_cb)
{
	struct ds_obj_exec_arg		*exec_arg = arg;
	crt_rpc_t			*rpc = exec_arg->rpc;
	struct obj_coll_punch_in	*ocpi = crt_req_get(rpc);
	int				 rc;

	if (idx != -1)
		return ds_obj_coll_punch_remote(dlh, arg, idx, comp_cb);

	/* Local punch on current rank, including the leader target. */
	rc = obj_coll_local(rpc, exec_arg->coll_shards, dlh->dlh_coll_entry, NULL, exec_arg->ioc,
			    &dlh->dlh_handle, dlh->dlh_handle.dth_mbs, obj_coll_tgt_punch);

	DL_CDEBUG(rc == 0 || rc == -DER_INPROGRESS || rc == -DER_TX_RESTART, DB_IO, DLOG_ERR, rc,
		  "Collective punch obj "DF_UOID" with "DF_DTI" on rank %u",
		  DP_UOID(ocpi->ocpi_oid), DP_DTI(&ocpi->ocpi_xid), dss_self_rank());

	if (comp_cb != NULL)
		comp_cb(dlh, idx, rc);

	return rc;
}

int
obj_coll_punch_bulk(crt_rpc_t *rpc, d_iov_t *iov, crt_proc_t *p_proc,
		    struct daos_coll_target **p_dcts, uint32_t *dct_nr)
{
	struct obj_coll_punch_in	*ocpi = crt_req_get(rpc);
	struct daos_coll_target		*dcts = NULL;
	crt_proc_t			 proc = NULL;
	d_sg_list_t			 sgl;
	d_sg_list_t			*sgls = &sgl;
	int				 rc = 0;
	int				 i;
	int				 j;

	D_ALLOC(iov->iov_buf, ocpi->ocpi_bulk_tgt_sz);
	if (iov->iov_buf == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	iov->iov_buf_len = ocpi->ocpi_bulk_tgt_sz;
	iov->iov_len = ocpi->ocpi_bulk_tgt_sz;

	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = iov;

	rc = obj_bulk_transfer(rpc, CRT_BULK_GET, false, &ocpi->ocpi_tgt_bulk, NULL, NULL,
			       DAOS_HDL_INVAL, &sgls, 1, NULL, NULL);
	if (rc != 0)
		goto out;

	rc = crt_proc_create(dss_get_module_info()->dmi_ctx, iov->iov_buf, iov->iov_len,
			     CRT_PROC_DECODE, &proc);
	if (rc != 0)
		goto out;

	D_ALLOC_ARRAY(dcts, ocpi->ocpi_bulk_tgt_nr);
	if (dcts == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < ocpi->ocpi_bulk_tgt_nr; i++) {
		rc = crt_proc_struct_daos_coll_target(proc, CRT_PROC_DECODE, &dcts[i]);
		if (rc != 0) {
			crt_proc_reset(proc, iov->iov_buf, iov->iov_len, CRT_PROC_FREE);
			for (j = 0; j < i; j++)
				crt_proc_struct_daos_coll_target(proc, CRT_PROC_FREE, &dcts[j]);
			goto out;
		}
	}

out:
	if (rc != 0) {
		D_FREE(dcts);
		if (proc != NULL)
			crt_proc_destroy(proc);
		daos_iov_free(iov);
	} else {
		*p_proc = proc;
		*p_dcts = dcts;
		*dct_nr = ocpi->ocpi_bulk_tgt_nr;
	}

	return rc;
}

int
obj_coll_punch_prep(struct obj_coll_punch_in *ocpi, struct daos_coll_target *dcts, uint32_t dct_nr,
		    struct dtx_coll_entry **p_dce)
{
	struct pl_map		*map = NULL;
	struct dtx_memberships	*mbs = ocpi->ocpi_mbs;
	struct dtx_daos_target	*ddt = mbs->dm_tgts;
	struct dtx_coll_entry	*dce = NULL;
	struct dtx_coll_target	*target;
	d_rank_t		 max_rank = 0;
	uint32_t		 size;
	int			 rc = 0;
	int			 i;
	int			 j;

	/* dcts[0] is for current engine. */
	if (dcts[0].dct_bitmap == NULL || dcts[0].dct_bitmap_sz == 0 ||
	    dcts[0].dct_shards == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	/* Already allocated enough space in MBS when decode to hold the targets and bitmap. */
	target = (struct dtx_coll_target *)(ddt + mbs->dm_tgt_cnt);

	size = sizeof(*ddt) * mbs->dm_tgt_cnt + sizeof(*target) +
	       sizeof(dcts[0].dct_tgt_ids[0]) * dcts[0].dct_tgt_nr + dcts[0].dct_bitmap_sz;
	if (unlikely(ocpi->ocpi_odm.odm_mbs_max_sz < sizeof(*mbs) + size)) {
		D_ERROR("Pre-allocated MBS buffer is too small: %u vs %ld + %u\n",
			ocpi->ocpi_odm.odm_mbs_max_sz, sizeof(*mbs), size);
		D_GOTO(out, rc = -DER_INVAL);
	}

	target->dct_tgt_nr = dcts[0].dct_tgt_nr;
	memcpy(target->dct_tgts, dcts[0].dct_tgt_ids,
	       sizeof(dcts[0].dct_tgt_ids[0]) * dcts[0].dct_tgt_nr);
	target->dct_bitmap_sz = dcts[0].dct_bitmap_sz;
	memcpy(target->dct_tgts + target->dct_tgt_nr, dcts[0].dct_bitmap, dcts[0].dct_bitmap_sz);
	mbs->dm_data_size = size;

	D_ALLOC_PTR(dce);
	if (dce == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dce->dce_xid = ocpi->ocpi_xid;
	dce->dce_ver = ocpi->ocpi_map_ver;
	dce->dce_refs = 1;

	D_ALLOC(dce->dce_bitmap, dcts[0].dct_bitmap_sz);
	if (dce->dce_bitmap == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dce->dce_bitmap_sz = dcts[0].dct_bitmap_sz;
	memcpy(dce->dce_bitmap, dcts[0].dct_bitmap, dcts[0].dct_bitmap_sz);

	if (!(ocpi->ocpi_flags & ORF_LEADER) || unlikely(dct_nr <= 1))
		D_GOTO(out, rc = 0);

	map = pl_map_find(ocpi->ocpi_po_uuid, ocpi->ocpi_oid.id_pub);
	if (map == NULL) {
		D_ERROR("Failed to find valid placement map in pool "DF_UUID"\n",
			DP_UUID(ocpi->ocpi_po_uuid));
		D_GOTO(out, rc = -DER_INVAL);
	}

	size = pool_map_node_nr(map->pl_poolmap);
	D_ALLOC_ARRAY(dce->dce_hints, size);
	if (dce->dce_hints == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dce->dce_ranks = d_rank_list_alloc(dct_nr - 1);
	if (dce->dce_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* Set i = 1 to skip leader_rank. */
	for (i = 1; i < dct_nr; i++) {
		dce->dce_ranks->rl_ranks[i - 1] = dcts[i].dct_rank;
		if (max_rank < dcts[i].dct_rank)
			max_rank = dcts[i].dct_rank;

		size = dcts[i].dct_bitmap_sz << 3;
		if (size > dss_tgt_nr)
			size = dss_tgt_nr;

		for (j = 0; j < size; j++) {
			if (isset(dcts[i].dct_bitmap, j)) {
				dce->dce_hints[dcts[i].dct_rank] = j;
				break;
			}
		}
	}

	dce->dce_hint_sz = max_rank + 1;

out:
	if (map != NULL)
		pl_map_decref(map);

	if (rc != 0 && dce != NULL)
		dtx_coll_entry_put(dce);
	else
		*p_dce = dce;

	return rc;
}

int
obj_coll_tgt_query(void *args)
{
	struct obj_coll_tgt_args	*octa = args;
	struct obj_tgt_query_args	*otqa;
	crt_rpc_t			*rpc = octa->octa_rpc;
	struct obj_coll_query_in	*ocqi = crt_req_get(rpc);
	struct obj_coll_query_out	*ocqo = crt_reply_get(rpc);
	struct daos_coll_target		*dct = ocqi->ocqi_tgts.ca_arrays;
	uint32_t			 tgt_id = dss_get_module_info()->dmi_tgt_id;
	uint32_t			 version = ocqi->ocqi_map_ver;
	int				 rc;

	otqa = &octa->octa_otqas[tgt_id];
	otqa->in_dkey = &ocqi->ocqi_dkey;
	otqa->in_akey = &ocqi->ocqi_akey;
	otqa->out_dkey = &ocqo->ocqo_dkey;
	otqa->out_akey = &ocqo->ocqo_akey;
	if (tgt_id == octa->octa_sponsor_tgt) {
		otqa->ioc = octa->octa_sponsor_ioc;
		otqa->dth = octa->octa_sponsor_dth;
	}

	if (ocqi->ocqi_tgts.ca_count > 1 || dct->dct_tgt_nr > 1)
		otqa->need_copy = 1;

	rc = obj_tgt_query(otqa, ocqi->ocqi_po_uuid, ocqi->ocqi_co_hdl, ocqi->ocqi_co_uuid,
			   ocqi->ocqi_oid, ocqi->ocqi_epoch, ocqi->ocqi_epoch_first,
			   ocqi->ocqi_api_flags, ocqi->ocqi_flags, &version, rpc,
			   octa->octa_shards[tgt_id].dcs_nr, octa->octa_shards[tgt_id].dcs_buf,
			   &ocqi->ocqi_xid);

	DL_CDEBUG(rc == 0 || rc == -DER_NONEXIST || rc == -DER_INPROGRESS || rc == -DER_TX_RESTART,
		  DB_IO, DLOG_ERR, rc, "Collective query obj shard "DF_OID".%u.%u with "
		  DF_DTI" on tgt %u", DP_OID(ocqi->ocqi_oid.id_pub),
		  octa->octa_shards[tgt_id].dcs_buf[0], ocqi->ocqi_oid.id_layout_ver,
		  DP_DTI(&ocqi->ocqi_xid), tgt_id);

	if (octa->octa_versions != NULL)
		octa->octa_versions[tgt_id] = version;

	return rc;
}

int
obj_coll_query_merge_tgts(struct obj_coll_query_in *ocqi, struct daos_oclass_attr *oca,
			  struct obj_tgt_query_args *otqas, uint8_t *bitmap, uint32_t bitmap_sz,
			  uint32_t tgt_id, int allow_failure)
{
	struct obj_query_merge_args	 oqma = { 0 };
	struct obj_tgt_query_args	*otqa = &otqas[tgt_id];
	struct obj_tgt_query_args	*tmp;
	int				 size = bitmap_sz << 3;
	int				 allow_failure_cnt;
	int				 succeeds;
	int				 rc = 0;
	int				 i;

	D_ASSERT(otqa->need_copy);
	D_ASSERT(otqa->keys_copied);

	oqma.oca = oca;
	oqma.oid = ocqi->ocqi_oid;
	oqma.in_dkey = &ocqi->ocqi_dkey;
	oqma.tgt_dkey = &otqa->dkey_copy;
	oqma.tgt_akey = &otqa->akey_copy;
	oqma.tgt_recx = &otqa->recx;
	oqma.tgt_epoch = &otqa->max_epoch;
	oqma.tgt_map_ver = &otqa->version;
	oqma.shard = &otqa->shard;
	oqma.flags = ocqi->ocqi_api_flags;
	oqma.opc = DAOS_OBJ_RPC_COLL_QUERY;

	if (size > dss_tgt_nr)
		size = dss_tgt_nr;

	for (i = 0, allow_failure_cnt = 0, succeeds = 0; i < size; i++) {
		if (isclr(bitmap, i))
			continue;

		tmp = &otqas[i];
		if (!tmp->completed)
			continue;

		if (tmp->result == allow_failure) {
			if (otqa->max_epoch < tmp->max_epoch)
				otqa->max_epoch = tmp->max_epoch;
			allow_failure_cnt++;
			continue;
		}

		/* Stop subsequent merge when hit one unallowed failure. */
		if (tmp->result != 0)
			D_GOTO(out, rc = tmp->result);

		succeeds++;

		if (i == tgt_id)
			continue;

		oqma.oid.id_shard = tmp->shard;
		oqma.src_epoch = tmp->max_epoch;
		oqma.src_dkey = &tmp->dkey_copy;
		oqma.src_akey = &tmp->akey_copy;
		oqma.src_recx = &tmp->recx;
		oqma.src_map_ver = tmp->version;
		/*
		 * Merge (L2) the results from other VOS targets on the same engine
		 * into current otqa that stands for the results for current engine.
		 */
		rc = daos_obj_merge_query_merge(&oqma);
		if (rc != 0)
			goto out;
	}

	D_DEBUG(DB_IO, " sub_requests %d/%d, allow_failure %d, result %d\n",
		allow_failure_cnt, succeeds, allow_failure, rc);

	if (allow_failure_cnt > 0 && rc == 0 && succeeds == 0)
		rc = allow_failure;

out:
	return rc;
}

int
obj_coll_query_disp(struct dtx_leader_handle *dlh, void *arg, int idx, dtx_sub_comp_cb_t comp_cb)
{
	struct ds_obj_exec_arg		*exec_arg = arg;
	crt_rpc_t			*rpc = exec_arg->rpc;
	struct obj_coll_query_in	*ocqi = crt_req_get(rpc);
	struct obj_tgt_query_args	*otqa;
	uint32_t			 tgt_id = dss_get_module_info()->dmi_tgt_id;
	int				 rc = 0;

	if (idx != -1)
		return ds_obj_coll_query_remote(dlh, arg, idx, comp_cb);

	rc = obj_coll_local(rpc, exec_arg->coll_shards, dlh->dlh_coll_entry, NULL, exec_arg->ioc,
			    &dlh->dlh_handle, exec_arg->args, obj_coll_tgt_query);

	DL_CDEBUG(rc == 0 || rc == -DER_INPROGRESS || rc == -DER_TX_RESTART, DB_IO, DLOG_ERR, rc,
		  "Collective query obj "DF_OID".%u.%u with "DF_DTI" on rank %u",
		  DP_OID(ocqi->ocqi_oid.id_pub), exec_arg->coll_shards[tgt_id].dcs_buf[0],
		  ocqi->ocqi_oid.id_layout_ver, DP_DTI(&ocqi->ocqi_xid), dss_self_rank());

	otqa = (struct obj_tgt_query_args *)exec_arg->args + tgt_id;
	if (otqa->completed && otqa->keys_copied && (rc == 0 || rc == dlh->dlh_allow_failure))
		rc = obj_coll_query_merge_tgts(ocqi, &exec_arg->ioc->ioc_oca, exec_arg->args,
					       dlh->dlh_coll_entry->dce_bitmap,
					       dlh->dlh_coll_entry->dce_bitmap_sz,
					       tgt_id, dlh->dlh_allow_failure);

	if (comp_cb != NULL)
		comp_cb(dlh, idx, rc);

	return rc;
}

int
obj_coll_query_agg_cb(struct dtx_leader_handle *dlh, void *arg)
{
	struct obj_query_merge_args	 oqma = { 0 };
	struct ds_obj_exec_arg		*exec_arg = arg;
	struct obj_tgt_query_args	*otqa;
	struct dtx_sub_status		*sub;
	crt_rpc_t			*rpc;
	struct obj_coll_query_in	*ocqi;
	struct obj_coll_query_out	*ocqo;
	int				 allow_failure = dlh->dlh_allow_failure;
	int				 allow_failure_cnt;
	int				 succeeds;
	int				 rc = 0;
	int				 i;
	bool				 cleanup = false;

	D_ASSERTF(allow_failure == -DER_NONEXIST, "Unexpected allow failure %d\n", allow_failure);

	otqa = (struct obj_tgt_query_args *)exec_arg->args + dss_get_module_info()->dmi_tgt_id;
	D_ASSERT(otqa->need_copy);

	/*
	 * If keys_copied is not set on current engine, then the query for current engine is either
	 * not triggered because of some earlier failure or the query on current engine hit trouble
	 * and cannot copy the keys. Under such cases, cleanup RPCs instead of merge query resutls.
	 */
	if (unlikely(!otqa->keys_copied)) {
		cleanup = true;
		/* otqa->result may be not initialized under such case. */
	} else {
		oqma.oca = &exec_arg->ioc->ioc_oca;
		oqma.tgt_dkey = &otqa->dkey_copy;
		oqma.tgt_akey = &otqa->akey_copy;
		oqma.tgt_recx = &otqa->recx;
		oqma.tgt_epoch = &otqa->max_epoch;
		oqma.tgt_map_ver = &otqa->version;
		oqma.shard = &otqa->shard;
		oqma.opc = DAOS_OBJ_RPC_COLL_QUERY;
	}

	for (i = 0, allow_failure_cnt = 0, succeeds = 0; i < dlh->dlh_normal_sub_cnt; i++) {
		sub = &dlh->dlh_subs[i];
		if (unlikely(!sub->dss_comp)) {
			D_ASSERT(sub->dss_data == NULL);
			continue;
		}

		if (dlh->dlh_rmt_ver < sub->dss_version)
			dlh->dlh_rmt_ver = sub->dss_version;

		rpc = sub->dss_data;

		if (sub->dss_result == allow_failure) {
			D_ASSERT(rpc != NULL);

			ocqo = crt_reply_get(rpc);
			if (otqa->max_epoch < ocqo->ocqo_max_epoch)
				otqa->max_epoch = ocqo->ocqo_max_epoch;
			allow_failure_cnt++;
			goto next;
		}

		if (sub->dss_result != 0) {
			/* Ignore INPROGRESS if there is other failure. */
			if (rc == -DER_INPROGRESS || rc == 0)
				rc = sub->dss_result;
			cleanup = true;
		} else {
			succeeds++;
		}

		/* Skip subsequent merge when hit one unallowed failure. */
		if (cleanup)
			goto next;

		D_ASSERT(rpc != NULL);

		ocqi = crt_req_get(rpc);
		ocqo = crt_reply_get(rpc);

		/*
		 * The RPC reply may be aggregated results from multiple VOS targets, as to related
		 * max/min dkey/recx are not from the direct target. The ocqo->ocqo_shard indicates
		 * the right one.
		 */
		oqma.oid = ocqi->ocqi_oid;
		oqma.oid.id_shard = ocqo->ocqo_shard;
		oqma.src_epoch = ocqo->ocqo_max_epoch;
		oqma.in_dkey = &ocqi->ocqi_dkey;
		oqma.src_dkey = &ocqo->ocqo_dkey;
		oqma.src_akey = &ocqo->ocqo_akey;
		oqma.src_recx = &ocqo->ocqo_recx;
		oqma.flags = ocqi->ocqi_api_flags;
		oqma.src_map_ver = obj_reply_map_version_get(rpc);
		/*
		 * Merge (L3) the results from other engines into current otqa that stands for the
		 * results for related engines' group, including current engine.
		 */
		rc = daos_obj_merge_query_merge(&oqma);

next:
		if (rpc != NULL)
			crt_req_decref(rpc);
		sub->dss_data = NULL;
	}

	D_DEBUG(DB_IO, DF_DTI" sub_requests %d/%d, allow_failure %d, result %d\n",
		DP_DTI(&dlh->dlh_handle.dth_xid),
		allow_failure_cnt, succeeds, allow_failure, rc);

	/*
	 * The agg_cb return value only stands for execution on remote engines.
	 * It is unnecessary to consider local failure on current engine, that
	 * will be returned via obj_coll_query_disp().
	 */
	if (allow_failure_cnt > 0 && rc == 0 && succeeds == 0)
		rc = allow_failure;

	return rc;
}
