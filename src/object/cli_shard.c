/*
 *  (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * object shard operations.
 */
#define D_LOGFAC	DD_FAC(object)

#include <daos/container.h>
#include <daos/mgmt.h>
#include <daos/pool.h>
#include <daos/pool_map.h>
#include <daos/rpc.h>
#include <daos/checksum.h>
#include "cli_csum.h"
#include "obj_rpc.h"
#include "obj_internal.h"

static inline struct dc_obj_layout *
obj_shard2layout(struct dc_obj_shard *shard)
{
	return container_of(shard, struct dc_obj_layout,
			    do_shards[shard->do_shard_idx]);
}

void
obj_shard_decref(struct dc_obj_shard *shard)
{
	struct dc_obj_layout	*layout;
	struct dc_object	*obj;
	bool			 release = false;

	D_ASSERT(shard != NULL);
	D_ASSERT(shard->do_obj != NULL);

	obj = shard->do_obj;
	layout = obj_shard2layout(shard);

	D_SPIN_LOCK(&obj->cob_spin);
	D_ASSERT(shard->do_ref > 0);
	if (--(shard->do_ref) == 0) {
		layout->do_open_count--;
		if (layout->do_open_count == 0 && layout != obj->cob_shards)
			release = true;
		shard->do_obj = NULL;
	}
	D_SPIN_UNLOCK(&obj->cob_spin);

	if (release)
		D_FREE(layout);
}

void
obj_shard_addref(struct dc_obj_shard *shard)
{
	D_ASSERT(shard->do_obj != NULL);
	D_SPIN_LOCK(&shard->do_obj->cob_spin);
	shard->do_ref++;
	D_SPIN_UNLOCK(&shard->do_obj->cob_spin);
}

int
dc_obj_shard_open(struct dc_object *obj, daos_unit_oid_t oid,
		  unsigned int mode, struct dc_obj_shard *shard)
{
	struct pool_target	*map_tgt;
	int			rc;

	D_ASSERT(obj != NULL && shard != NULL);
	D_ASSERT(shard->do_obj == NULL);

	rc = dc_pool_tgt_idx2ptr(obj->cob_pool, shard->do_target_id,
				 &map_tgt);
	if (rc)
		return rc;

	shard->do_id = oid;
	shard->do_target_rank = map_tgt->ta_comp.co_rank;
	shard->do_target_idx = map_tgt->ta_comp.co_index;
	shard->do_obj = obj;
	shard->do_co = obj->cob_co;
	obj_shard_addref(shard); /* release this until obj_layout_free */

	D_SPIN_LOCK(&obj->cob_spin);
	obj->cob_shards->do_open_count++;
	D_SPIN_UNLOCK(&obj->cob_spin);

	return 0;
}

void
dc_obj_shard_close(struct dc_obj_shard *shard)
{
	obj_shard_decref(shard);
}

struct rw_cb_args {
	crt_rpc_t		*rpc;
	daos_handle_t		*hdlp;
	d_sg_list_t		*rwaa_sgls;
	struct dc_cont		*co;
	unsigned int		*map_ver;
	daos_iom_t		*maps;
	crt_endpoint_t		tgt_ep;
	struct shard_rw_args	*shard_args;
};

static d_iov_t *
rw_args2csum_iov(const struct shard_rw_args *shard_args)
{
	daos_obj_rw_t	*api_args;

	D_ASSERT(shard_args != NULL);
	D_ASSERT(shard_args->auxi.obj_auxi != NULL);
	D_ASSERT(shard_args->auxi.obj_auxi->obj_task != NULL);

	api_args = dc_task_get_args(shard_args->auxi.obj_auxi->obj_task);
	return api_args->csum_iov;
}

static int
rw_cb_csum_verify(const struct rw_cb_args *rw_args)
{
	struct obj_rw_in	*orw = crt_req_get(rw_args->rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rw_args->rpc);
	int			 rc;
	bool			 is_ec_obj;
	struct dc_object	*obj = rw_args->shard_args->auxi.obj_auxi->obj;
	struct dc_csum_veriry_args csum_verify_args = {
	    .csummer    = rw_args->co->dc_csummer,
	    .sgls       = rw_args->rwaa_sgls,
	    .iods       = orw->orw_iod_array.oia_iods,
	    .iods_csums = orwo->orw_iod_csums.ca_arrays,
	    .maps       = orwo->orw_maps.ca_arrays,
	    .dkey       = &orw->orw_dkey,
	    .sizes      = orwo->orw_iod_sizes.ca_arrays,
	    .oid        = orw->orw_oid,
	    .iod_nr     = orw->orw_iod_array.oia_iod_nr,
	    .maps_nr    = orwo->orw_maps.ca_count,
	    .oiods      = rw_args->shard_args->oiods,
	    .reasb_req  = rw_args->shard_args->reasb_req,
	    .obj        = obj,
	    .dkey_hash  = rw_args->shard_args->auxi.obj_auxi->dkey_hash,
	    .shard_offs = rw_args->shard_args->offs,
	    .oc_attr    = &obj->cob_oca,
	    .iov_csum   = rw_args2csum_iov(rw_args->shard_args),
	    .shard      = rw_args->shard_args->auxi.shard,
	};

	if (obj_is_ec(obj))
		csum_verify_args.shard_idx =
		    obj_ec_shard_off(obj,
				     rw_args->shard_args->auxi.obj_auxi->dkey_hash,
				     orw->orw_oid.id_shard);
	else
		csum_verify_args.shard_idx = orw->orw_oid.id_shard %
					     daos_oclass_grp_size(&obj->cob_oca);


	rc = dc_rw_cb_csum_verify(&csum_verify_args);

	is_ec_obj = (rw_args->shard_args->reasb_req != NULL) &&
		    daos_oclass_is_ec(rw_args->shard_args->reasb_req->orr_oca);

	if (rc == -DER_CSUM && is_ec_obj) {
		uint32_t tgt_idx;

		tgt_idx = rw_args->shard_args->auxi.shard % obj_get_grp_size(obj);
		rc = obj_ec_fail_info_insert(rw_args->shard_args->reasb_req, tgt_idx);
		if (rc) {
			D_ERROR(DF_OID" fail info insert"
				DF_RC"\n",
				DP_OID(orw->orw_oid.id_pub),
				DP_RC(rc));
		}
		rc = -DER_CSUM;
	}

	return rc;
}

static int
iom_recx_merge(daos_iom_t *dst, daos_recx_t *recx, bool iom_realloc)
{
	daos_recx_t	*tmpr = NULL;
	uint32_t	 iom_nr, i;

	for (i = 0; i < dst->iom_nr_out; i++) {
		tmpr = &dst->iom_recxs[i];
		if (DAOS_RECX_PTR_OVERLAP(tmpr, recx) ||
		    DAOS_RECX_PTR_ADJACENT(tmpr, recx)) {
			daos_recx_merge(recx, tmpr);
			return 0;
		}
	}

	D_ASSERTF(dst->iom_nr_out <= dst->iom_nr,
		 "iom_nr_out %d, iom_nr %d\n", dst->iom_nr_out, dst->iom_nr);
	if (iom_realloc && dst->iom_nr_out == dst->iom_nr) {
		iom_nr = dst->iom_nr + 32;
		D_REALLOC_ARRAY(tmpr, dst->iom_recxs, dst->iom_nr, iom_nr);
		if (tmpr == NULL)
			return -DER_NOMEM;
		dst->iom_recxs = tmpr;
		dst->iom_nr = iom_nr;
	}

	if (dst->iom_nr_out < dst->iom_nr) {
		dst->iom_recxs[dst->iom_nr_out] = *recx;
		dst->iom_nr_out++;
		return 0;
	}

	return -DER_REC2BIG;
}

static int
obj_ec_iom_merge(struct dc_object *obj, struct obj_reasb_req *reasb_req, uint64_t dkey_hash,
		 uint32_t shard, uint32_t tgt_idx, const daos_iom_t *src, daos_iom_t *dst,
		 struct daos_recx_ep_list *recov_list)
{
	struct daos_oclass_attr	*oca = reasb_req->orr_oca;
	uint64_t		 stripe_rec_nr = obj_ec_stripe_rec_nr(oca);
	uint64_t		 cell_rec_nr = obj_ec_cell_rec_nr(oca);
	uint64_t		 end, rec_nr;
	daos_recx_t		 hi, lo, recx, tmpr;
	daos_recx_t		 recov_hi = { 0 };
	daos_recx_t		 recov_lo = { 0 };
	uint32_t		 tgt_off;
	uint32_t		 iom_nr, i;
	bool			 done;
	int			 rc = 0;

	tgt_off = obj_ec_shard_off(obj, dkey_hash, tgt_idx);
	D_ASSERTF(tgt_off < obj_ec_data_tgt_nr(oca), "tgt_off %d, tgt_nr %d\n",
		  tgt_off, obj_ec_data_tgt_nr(oca));

	if (recov_list != NULL)
		daos_recx_ep_list_hilo(recov_list, &recov_hi, &recov_lo);

	D_MUTEX_LOCK(&reasb_req->orr_mutex);

	/* merge iom_recx_hi */
	hi = src->iom_recx_hi;
	end = DAOS_RECX_END(hi);
	if (end > 0) {
		hi.rx_idx = max(hi.rx_idx, rounddown(end - 1, cell_rec_nr));
		hi.rx_nr = end - hi.rx_idx;
		hi.rx_idx = obj_ec_idx_vos2daos(hi.rx_idx, stripe_rec_nr,
						cell_rec_nr, tgt_off);
	}
	if (recov_list != NULL &&
	    DAOS_RECX_END(recov_hi) > DAOS_RECX_END(hi))
		hi = recov_hi;
	if (reasb_req->orr_iom_tgt_nr == 0)
		dst->iom_recx_hi = hi;
	else if (DAOS_RECX_OVERLAP(dst->iom_recx_hi, hi) ||
		 DAOS_RECX_ADJACENT(dst->iom_recx_hi, hi))
		daos_recx_merge(&hi, &dst->iom_recx_hi);
	else if (hi.rx_idx > dst->iom_recx_hi.rx_idx)
		dst->iom_recx_hi = hi;

	/* merge iom_recx_lo */
	lo = src->iom_recx_lo;
	end = DAOS_RECX_END(lo);
	if (end > 0) {
		lo.rx_nr = min(end, roundup(lo.rx_idx + 1, cell_rec_nr)) -
			   lo.rx_idx;
		lo.rx_idx = obj_ec_idx_vos2daos(lo.rx_idx, stripe_rec_nr,
						cell_rec_nr, tgt_off);
	}
	if (recov_list != NULL && (end == 0 ||
	    DAOS_RECX_END(recov_lo) < DAOS_RECX_END(lo)))
		lo = recov_lo;
	if (reasb_req->orr_iom_tgt_nr == 0)
		dst->iom_recx_lo = lo;
	else if (DAOS_RECX_OVERLAP(dst->iom_recx_lo, lo) ||
		 DAOS_RECX_ADJACENT(dst->iom_recx_lo, lo))
		daos_recx_merge(&lo, &dst->iom_recx_lo);
	else if (lo.rx_idx < dst->iom_recx_lo.rx_idx)
		dst->iom_recx_lo = lo;

	if ((dst->iom_flags & DAOS_IOMF_DETAIL) == 0) {
		dst->iom_nr_out = 0;
		D_MUTEX_UNLOCK(&reasb_req->orr_mutex);
		return 0;
	}

	/* If user provides NULL iom_recxs an requires DAOS_IOMF_DETAIL,
	 * DAOS internally allocates the buffer and user should free it.
	 */
	if (dst->iom_recxs == NULL) {
		iom_nr = src->iom_nr * reasb_req->orr_tgt_nr;
		iom_nr = roundup(iom_nr, 8);
		D_ALLOC_ARRAY(dst->iom_recxs, iom_nr);
		if (dst->iom_recxs == NULL) {
			D_MUTEX_UNLOCK(&reasb_req->orr_mutex);
			return -DER_NOMEM;
		}
		dst->iom_nr = iom_nr;
		reasb_req->orr_iom_realloc = 1;
	}

	/* merge iom_recxs */
	reasb_req->orr_iom_tgt_nr++;
	D_ASSERTF(reasb_req->orr_iom_tgt_nr <= reasb_req->orr_tgt_nr,
		  "orr_iom_tgt_nr %d, orr_tgt_nr %d.\n",
		  reasb_req->orr_iom_tgt_nr, reasb_req->orr_tgt_nr);
	done = (reasb_req->orr_iom_tgt_nr == reasb_req->orr_tgt_nr);
	reasb_req->orr_iom_nr += src->iom_nr;
	for (i = 0; i < src->iom_nr; i++) {
		recx = src->iom_recxs[i];
		D_ASSERT(recx.rx_nr > 0);
		end = DAOS_RECX_END(recx);
		rec_nr = 0;
		while (rec_nr < recx.rx_nr) {
			tmpr.rx_idx = recx.rx_idx + rec_nr;
			tmpr.rx_nr = min(roundup(tmpr.rx_idx + 1, cell_rec_nr),
					 end) - tmpr.rx_idx;
			rec_nr += tmpr.rx_nr;
			tmpr.rx_idx = obj_ec_idx_vos2daos(tmpr.rx_idx,
							  stripe_rec_nr,
							  cell_rec_nr,
							  tgt_off);
			rc = iom_recx_merge(dst, &tmpr,
					    reasb_req->orr_iom_realloc);
			if (rc == -DER_NOMEM)
				break;
			if (rc == -DER_REC2BIG) {
				if (done)
					dst->iom_nr_out = reasb_req->orr_iom_nr
						+ reasb_req->orr_tgt_nr;
				rc = 0;
			}
		}
		if (rc)
			break;
	}

	/* merge recov list */
	if (recov_list != NULL && rc == 0) {
		for (i = 0; i < recov_list->re_nr; i++) {
			rc = iom_recx_merge(dst,
					    &recov_list->re_items[i].re_recx,
					    reasb_req->orr_iom_realloc);
			if (rc == -DER_NOMEM)
				break;
			if (rc == -DER_REC2BIG) {
				if (done)
					dst->iom_nr_out += recov_list->re_nr;
				rc = 0;
			}
		}
	}

	if (rc == 0 && done) {
		daos_recx_t	*r1, *r2;
		daos_size_t	 move_len;

		daos_iom_sort(dst);
		if (dst->iom_nr_out > dst->iom_nr)
			goto out;
		for (i = 1; i < dst->iom_nr_out; i++) {
			r1 = &dst->iom_recxs[i - 1];
			r2 = &dst->iom_recxs[i];
			if (DAOS_RECX_PTR_OVERLAP(r1, r2) ||
			    DAOS_RECX_PTR_ADJACENT(r1, r2)) {
				daos_recx_merge(r2, r1);
				if (i < dst->iom_nr_out - 1) {
					move_len = (dst->iom_nr_out - i - 1) *
						   sizeof(*r1);
					memmove(r2, r2 + 1, move_len);
				}
				dst->iom_nr_out--;
				i--;
			}
		}
	}

out:
	D_MUTEX_UNLOCK(&reasb_req->orr_mutex);
	return rc;
}

static int
daos_iom_copy(const daos_iom_t *src, daos_iom_t *dst)
{
	uint32_t	i;
	uint32_t	to_copy;

	dst->iom_type = src->iom_type;
	dst->iom_size = src->iom_size;
	dst->iom_recx_hi = src->iom_recx_hi;
	dst->iom_recx_lo = src->iom_recx_lo;

	if ((dst->iom_flags & DAOS_IOMF_DETAIL) == 0 ||
	    src->iom_nr_out == 0) {
		dst->iom_nr_out = 0;
		return 0;
	}

	dst->iom_nr_out = src->iom_nr_out;
	if (dst->iom_recxs == NULL) {
		dst->iom_recxs = daos_recx_alloc(dst->iom_nr_out);
		if (dst->iom_recxs == NULL)
			return -DER_NOMEM;
		dst->iom_nr = dst->iom_nr_out;
	}

	to_copy = min(dst->iom_nr, dst->iom_nr_out);
	for (i = 0; i < to_copy ; i++)
		dst->iom_recxs[i] = src->iom_recxs[i];
	return 0;
}

static void
csum_report_cb(const struct crt_cb_info *cb_info)
{
	crt_rpc_t	*rpc = cb_info->cci_arg;
	int		 rc = cb_info->cci_rc;

	D_DEBUG(DB_IO, "rpc %p, csum report "DF_RC"\n", rpc, DP_RC(rc));
	crt_req_decref(rpc);
	crt_req_decref(cb_info->cci_rpc);
}

/* send CSUM_REPORT to original target */
static int
dc_shard_csum_report(tse_task_t *task, crt_endpoint_t *tgt_ep, crt_rpc_t *rpc)
{
	crt_rpc_t		*csum_rpc;
	struct obj_rw_in	*orw, *csum_orw;
	int			 opc;
	int			 rc;

	opc = opc_get(rpc->cr_opc);
	D_ASSERTF(opc == DAOS_OBJ_RPC_FETCH, "bad opc 0x%x\n", opc);
	rc = obj_req_create(daos_task2ctx(task), tgt_ep, opc, &csum_rpc);
	if (rc) {
		D_ERROR("Failed to create csum report request, task %p.\n",
			task);
		return rc;
	}

	orw = crt_req_get(rpc);
	csum_orw = crt_req_get(csum_rpc);
	memcpy(csum_orw, orw, rpc->cr_input_size);
	csum_orw->orw_flags |= ORF_CSUM_REPORT;
	csum_orw->orw_iod_array.oia_iod_csums = NULL;
	csum_orw->orw_sgls.ca_count = 0;
	csum_orw->orw_sgls.ca_arrays = NULL;
	csum_orw->orw_bulks.ca_count = 0;
	csum_orw->orw_bulks.ca_arrays = NULL;
	csum_orw->orw_dkey_csum = NULL;
	csum_orw->orw_iod_array.oia_iod_nr = 0;
	csum_orw->orw_nr = 0;
	crt_req_addref(csum_rpc);
	crt_req_addref(rpc);
	return crt_req_send(csum_rpc, csum_report_cb, rpc);
}

static bool
dc_shard_singv_size_conflict(struct daos_oclass_attr *oca, daos_size_t old_size,
			     daos_size_t new_size)
{
	struct obj_ec_singv_local	old_loc = { 0 };

	if (new_size >= old_size)
		return false;

	if (obj_ec_singv_one_tgt(old_size, NULL, oca))
		return false;

	if (!obj_ec_singv_one_tgt(new_size, NULL, oca))
		return false;

	obj_ec_singv_local_sz(old_size, oca, 1, &old_loc, false);
	if (old_loc.esl_off < new_size) {
		D_ERROR("old_size "DF_U64", tgt idx 1 off "DF_U64", new_size "DF_U64", conflict.\n",
			old_size, old_loc.esl_off, new_size);
		return true;
	}

	return false;
}

static int
dc_shard_update_size(struct rw_cb_args *rw_args, int fetch_rc)
{
	struct obj_rw_in	*orw;
	struct obj_rw_out	*orwo;
	daos_iod_t		*iods;
	uint64_t		*sizes;
	struct obj_reasb_req	*reasb_req;
	bool			is_ec_obj;
	bool			fetch_again = false;
	bool			rec2big = false;
	int			i;
	int			rc = 0;

	orw = crt_req_get(rw_args->rpc);
	orwo = crt_reply_get(rw_args->rpc);
	D_ASSERT(orw != NULL && orwo != NULL);
	D_ASSERTF(fetch_rc == 0 || fetch_rc == -DER_REC2BIG, "bad fetch_rc %d\n", fetch_rc);

	iods = orw->orw_iod_array.oia_iods;
	sizes = orwo->orw_iod_sizes.ca_arrays;

	reasb_req = rw_args->shard_args->reasb_req;
	is_ec_obj = (reasb_req != NULL) && daos_oclass_is_ec(reasb_req->orr_oca);
	/* update the sizes in iods */
	for (i = 0; i < orw->orw_nr; i++) {
		daos_iod_t		*iod;
		daos_iod_t		*uiod;
		uint32_t		shard;
		struct daos_oclass_attr	*oca;
		struct shard_fetch_stat	*fetch_stat;
		bool			conflict = false;

		D_DEBUG(DB_IO, DF_UOID" size "DF_U64" eph "DF_U64"\n", DP_UOID(orw->orw_oid),
			sizes[i], orw->orw_epoch);

		if (!is_ec_obj) {
			iods[i].iod_size = sizes[i];
			rc = fetch_rc;
			continue;
		}

		D_ASSERT(reasb_req != NULL);
		iod = &reasb_req->orr_iods[i];
		uiod = &reasb_req->orr_uiods[i];
		oca = reasb_req->orr_oca;
		fetch_stat = &reasb_req->orr_fetch_stat[i];
		D_ASSERT(oca != NULL);

		D_MUTEX_LOCK(&reasb_req->orr_mutex);
		if (iod->iod_type == DAOS_IOD_ARRAY) {
			if (fetch_stat->sfs_size == 0 || iod->iod_size == 0) {
				fetch_stat->sfs_size = sizes[i];
				iod->iod_size = sizes[i];
			}
			goto unlock;
		}

		/* single-value, trust the size replied from first shard or parity shard,
		 * because if overwrite those shards must be updated.
		 */
		shard = orw->orw_oid.id_shard % daos_oclass_grp_size(oca);
		if (shard == obj_ec_singv_small_idx(rw_args->shard_args->auxi.obj_auxi->obj,
						    orw->orw_dkey_hash, iod) ||
		    is_ec_parity_shard(rw_args->shard_args->auxi.obj_auxi->obj, orw->orw_dkey_hash,
				       orw->orw_oid.id_shard)) {
			if (uiod->iod_size != 0 && uiod->iod_size < sizes[i]) {
				rec2big = true;
				rc = -DER_REC2BIG;
				D_ERROR(DF_UOID" original iod_size "DF_U64", real size "DF_U64
					", "DF_RC"\n", DP_UOID(orw->orw_oid),
					iod->iod_size, sizes[i], DP_RC(rc));
				iod->iod_size = sizes[i];
				uiod->iod_size = sizes[i];
				goto unlock;
			}

			iod->iod_size = sizes[i];
			uiod->iod_size = sizes[i];
			if (fetch_stat->sfs_size == 0) {
				fetch_stat->sfs_size = sizes[i];
			} else if (fetch_stat->sfs_size != sizes[i]) {
				rc = -DER_IO;
				D_ERROR(DF_UOID" size mismatch "DF_U64" != "DF_U64", "DF_RC"\n",
					DP_UOID(orw->orw_oid), fetch_stat->sfs_size, sizes[i],
					DP_RC(rc));
				goto unlock;
			}

			if (fetch_stat->sfs_size_other != 0 && fetch_rc == 0 &&
			    fetch_stat->sfs_rc_other == 0) {
				conflict = dc_shard_singv_size_conflict(oca,
						fetch_stat->sfs_size_other, fetch_stat->sfs_size);
			}
			if (rc == 0)
				rc = fetch_rc;
			/* one case needs to ignore the DER_REC2BIG failure - long singv
			 * overwritten by short singv and the short singv only store on one
			 * data target (and parity targets), other cases should return
			 * DER_REC2BIG.
			 */
			if (rc == 0 && fetch_stat->sfs_rc_other == -DER_REC2BIG &&
			    !obj_ec_singv_one_tgt(fetch_stat->sfs_size, NULL, oca)) {
				rec2big = true;
				D_ERROR("other shard got -DER_REC2BIG, sfs_size "DF_U64
					" on all shards\n", fetch_stat->sfs_size);
			}
		} else if (sizes[i] != 0) {
			if (iod->iod_size == 0)
				iod->iod_size = sizes[i];
			if (fetch_stat->sfs_rc_other == 0)
				fetch_stat->sfs_rc_other = fetch_rc;
			if (fetch_stat->sfs_size_other == 0) {
				fetch_stat->sfs_size_other = sizes[i];
			} else {
				if (fetch_stat->sfs_size_other != sizes[i]) {
					rc = -DER_IO;
					D_ERROR(DF_UOID" size mismatch "DF_U64" != "DF_U64", "
						DF_RC"\n", DP_UOID(orw->orw_oid),
						fetch_stat->sfs_size, sizes[i], DP_RC(rc));
					goto unlock;
				}
			}
			if (fetch_rc == -DER_REC2BIG && fetch_stat->sfs_size != 0 &&
			    !obj_ec_singv_one_tgt(fetch_stat->sfs_size, NULL, oca)) {
				rec2big = true;
				D_ERROR("this non-parity shard got -DER_REC2BIG, sfs_size "DF_U64
					" on all shards\n", fetch_stat->sfs_size);
			}

			if (fetch_rc == 0 && fetch_stat->sfs_size != 0)
				conflict = dc_shard_singv_size_conflict(oca,
					fetch_stat->sfs_size_other, fetch_stat->sfs_size);
		}

		if (conflict && !reasb_req->orr_size_fetch && rc == 0)
			fetch_again = true;

unlock:
		D_MUTEX_UNLOCK(&reasb_req->orr_mutex);
		if (rc == -DER_IO)
			break;
	}

	if (rc == 0) {
		if (rec2big)
			rc = -DER_REC2BIG;
		else if (fetch_again)
			rc = -DER_FETCH_AGAIN;
	}

	return rc;
}

static int
dc_rw_cb(tse_task_t *task, void *arg)
{
	struct rw_cb_args	*rw_args = arg;
	struct obj_rw_in	*orw;
	struct obj_rw_out	*orwo;
	daos_handle_t		th;
	struct obj_reasb_req	*reasb_req;
	daos_obj_rw_t		*api_args;
	bool			 is_ec_obj;
	int			 opc;
	int			 ret = task->dt_result;
	int			 i;
	int			 rc = 0;

	opc = opc_get(rw_args->rpc->cr_opc);
	D_DEBUG(DB_IO, "rpc %p opc:%d completed, task %p dt_result %d.\n",
		rw_args->rpc, opc, task, ret);
	if (opc == DAOS_OBJ_RPC_FETCH &&
	    DAOS_FAIL_CHECK(DAOS_SHARD_OBJ_FETCH_TIMEOUT)) {
		D_ERROR("Inducing -DER_TIMEDOUT error on shard I/O fetch\n");
		D_GOTO(out, rc = -DER_TIMEDOUT);
	}
	if (opc == DAOS_OBJ_RPC_UPDATE &&
	    DAOS_FAIL_CHECK(DAOS_SHARD_OBJ_UPDATE_TIMEOUT)) {
		D_ERROR("Inducing -DER_TIMEDOUT error on shard I/O update\n");
		D_GOTO(out, rc = -DER_TIMEDOUT);
	}
	if (opc == DAOS_OBJ_RPC_UPDATE &&
	    DAOS_FAIL_CHECK(DAOS_OBJ_UPDATE_NOSPACE)) {
		D_ERROR("Inducing -DER_NOSPACE error on shard I/O update\n");
		D_GOTO(out, rc = -DER_NOSPACE);
	}

	if (DAOS_FAIL_CHECK(DAOS_SHARD_OBJ_RW_DROP_REPLY)) {
		D_ERROR("Drop RPC for shard I/O update\n");
		D_GOTO(out, rc = -DER_HG);
	}

	reasb_req = rw_args->shard_args->reasb_req;
	is_ec_obj = reasb_req != NULL && daos_oclass_is_ec(reasb_req->orr_oca);

	orw = crt_req_get(rw_args->rpc);
	orwo = crt_reply_get(rw_args->rpc);
	D_ASSERT(orw != NULL && orwo != NULL);
	if (ret != 0) {
		/*
		 * If any failure happens inside Cart, let's reset failure to
		 * TIMEDOUT, so the upper layer can retry.
		 */
		D_ERROR(DF_UOID" (%s) RPC %d to %d/%d, flags %lx/%x, task %p failed, %s: "DF_RC"\n",
			DP_UOID(orw->orw_oid), is_ec_obj ? "EC" : "non-EC", opc,
			rw_args->rpc->cr_ep.ep_rank, rw_args->rpc->cr_ep.ep_tag,
			(unsigned long)orw->orw_api_flags, orw->orw_flags, task,
			orw->orw_bulks.ca_arrays != NULL ||
			orw->orw_bulks.ca_count != 0 ? "DMA" : "non-DMA", DP_RC(ret));

		D_GOTO(out, ret);
	}

	rc = obj_reply_get_status(rw_args->rpc);
	/*
	 * orwo->orw_epoch may be set even when the status is nonzero (e.g.,
	 * -DER_TX_RESTART and -DER_INPROGRESS).
	 */
	api_args = dc_task_get_args(rw_args->shard_args->auxi.obj_auxi->obj_task);
	th = api_args->th;
	if (daos_handle_is_valid(th)) {
		int rc_tmp;

		rc_tmp = dc_tx_op_end(task, th,
				      &rw_args->shard_args->auxi.epoch, rc,
				      orwo->orw_epoch);
		if (rc_tmp != 0) {
			D_ERROR("failed to end transaction operation (rc=%d "
				"epoch="DF_U64": "DF_RC"\n", rc,
				orwo->orw_epoch, DP_RC(rc_tmp));
			goto out;
		}
	}

	if (rc != 0) {
		if (rc == -DER_INPROGRESS || rc == -DER_TX_BUSY) {
			D_DEBUG(DB_IO, "rpc %p opc %d to rank %d tag %d may "
				"need retry: "DF_RC"\n", rw_args->rpc, opc,
				rw_args->rpc->cr_ep.ep_rank,
				rw_args->rpc->cr_ep.ep_tag, DP_RC(rc));
			D_GOTO(out, rc);
		} else if (rc == -DER_STALE) {
			D_INFO("rpc %p got DER_STALE, pool map update needed\n",
			       rw_args->rpc);
			D_GOTO(out, rc);
		}

		/*
		 * don't log errors in-case of possible conditionals or
		 * rec2big errors which can be expected.
		 */
		if (rc == -DER_REC2BIG || rc == -DER_NONEXIST || rc == -DER_NO_PERM ||
		    rc == -DER_EXIST || rc == -DER_RF)
			D_DEBUG(DB_IO, DF_UOID" rpc %p opc %d to rank %d tag %d: "DF_RC"\n",
				DP_UOID(orw->orw_oid), rw_args->rpc, opc,
				rw_args->rpc->cr_ep.ep_rank, rw_args->rpc->cr_ep.ep_tag, DP_RC(rc));
		else
			D_ERROR(DF_CONT DF_UOID" rpc %p opc %d to rank %d tag %d: "DF_RC"\n",
				DP_CONT(orw->orw_pool_uuid, orw->orw_co_uuid),
				DP_UOID(orw->orw_oid), rw_args->rpc, opc,
				rw_args->rpc->cr_ep.ep_rank, rw_args->rpc->cr_ep.ep_tag, DP_RC(rc));

		if (opc == DAOS_OBJ_RPC_FETCH) {
			/* For EC obj fetch, set orr_epoch as highest server
			 * epoch, so if need to recovery data (-DER_CSUM etc)
			 * can use that epoch (see obj_ec_recov_task_init).
			 */
			if (is_ec_obj &&
			    (reasb_req->orr_epoch.oe_value == DAOS_EPOCH_MAX ||
			     reasb_req->orr_epoch.oe_value < orwo->orw_epoch))
				reasb_req->orr_epoch.oe_value = orwo->orw_epoch;

			if (rc == -DER_CSUM && is_ec_obj) {
				struct shard_auxi_args	*sa;
				uint32_t		 tgt_idx;

				sa = &rw_args->shard_args->auxi;
				tgt_idx = sa->shard %
					  obj_get_grp_size(rw_args->shard_args->auxi.obj_auxi->obj);
				rc = obj_ec_fail_info_insert(reasb_req, tgt_idx);
				if (rc)
					D_ERROR(DF_OID" fail info insert: " DF_RC"\n",
						DP_OID(orw->orw_oid.id_pub),
						DP_RC(rc));
				rc = -DER_CSUM;
			} else if (rc == -DER_REC2BIG) {
				rc = dc_shard_update_size(rw_args, rc);
			}
		}
		D_GOTO(out, rc);
	}
	*rw_args->map_ver = obj_reply_map_version_get(rw_args->rpc);

	if (opc == DAOS_OBJ_RPC_FETCH) {
		if (rw_args->shard_args->auxi.flags & ORF_CHECK_EXISTENCE)
			goto out;

		if (is_ec_obj &&
		    (reasb_req->orr_epoch.oe_value == DAOS_EPOCH_MAX ||
		     reasb_req->orr_epoch.oe_value < orwo->orw_epoch))
			reasb_req->orr_epoch.oe_value = orwo->orw_epoch;

		if (orwo->orw_iod_sizes.ca_count != orw->orw_nr) {
			D_ERROR("out:%u != in:%u for "DF_UOID" with eph "
				DF_U64".\n",
				(unsigned)orwo->orw_iod_sizes.ca_count,
				orw->orw_nr, DP_UOID(orw->orw_oid),
				orw->orw_epoch);
			D_GOTO(out, rc = -DER_PROTO);
		}

		if (is_ec_obj && !reasb_req->orr_recov) {
			rc = obj_ec_recov_add(reasb_req,
					      orwo->orw_rels.ca_arrays,
					      orwo->orw_rels.ca_count);
			if (rc) {
				D_ERROR(DF_UOID " obj_ec_recov_add failed, " DF_RC "\n",
					DP_UOID(orw->orw_oid), DP_RC(rc));
				goto out;
			}
		} else if (is_ec_obj && reasb_req->orr_recov &&
			   orwo->orw_rels.ca_arrays != NULL) {
			rc = obj_ec_parity_check(reasb_req,
						 orwo->orw_rels.ca_arrays,
						 orwo->orw_rels.ca_count);
			if (rc) {
				D_ERROR(DF_UOID " obj_ec_parity_check failed, " DF_RC "\n",
					DP_UOID(orw->orw_oid), DP_RC(rc));
				goto out;
			}
		}

		rc = dc_shard_update_size(rw_args, 0);
		if (rc) {
			D_ERROR(DF_UOID " dc_shard_update_size failed, " DF_RC "\n",
				DP_UOID(orw->orw_oid), DP_RC(rc));
			goto out;
		}

		if (is_ec_obj && reasb_req->orr_size_fetch)
			goto out;

		if (orwo->orw_sgls.ca_count > 0) {
			/* inline transfer */
			rc = daos_sgls_copy_data_out(rw_args->rwaa_sgls,
						     orw->orw_nr,
						     orwo->orw_sgls.ca_arrays,
						     orwo->orw_sgls.ca_count);
		} else if (rw_args->rwaa_sgls != NULL) {
			/* for bulk transfer it needs to update sg_nr_out */
			d_sg_list_t	*sgls = rw_args->rwaa_sgls;
			uint32_t	*nrs;
			uint32_t	 nrs_count;
			daos_size_t	*replied_sizes;
			daos_size_t	*size_array = NULL;
			daos_size_t	 data_size, size_in_iod;

			nrs = orwo->orw_nrs.ca_arrays;
			nrs_count = orwo->orw_nrs.ca_count;
			replied_sizes = orwo->orw_data_sizes.ca_arrays;
			if (nrs_count != orw->orw_nr) {
				D_ERROR("Invalid nrs %u != %u\n", nrs_count,
					orw->orw_nr);
				D_GOTO(out, rc = -DER_PROTO);
			}

			/*  For EC obj, record the daos_sizes from shards and
			 *  obj layer will handle it (obj_ec_fetch_set_sgl).
			 */
			if (is_ec_obj) {
				D_ASSERTF(orw->orw_tgt_idx <
					  obj_ec_tgt_nr(reasb_req->orr_oca),
					  "orw_tgt_idx %d, obj_ec_tgt_nr %d\n",
					  orw->orw_tgt_idx,
					  obj_ec_tgt_nr(reasb_req->orr_oca));
				size_array = reasb_req->orr_data_sizes +
					     orw->orw_tgt_idx * orw->orw_nr;
			}

			for (i = 0; i < orw->orw_nr; i++) {
				daos_iod_t *iod;

				/* server returned bs_nr_out is only to check
				 * if it is empty record in that case just set
				 * sg_nr_out as zero, or will set sg_nr_out and
				 * iov_len by checking with iods as server
				 * filled the buffer from beginning.
				 */
				if (!is_ec_obj && nrs[i] == 0) {
					sgls[i].sg_nr_out = 0;
					continue;
				}

				iod = &orw->orw_iod_array.oia_iods[i];
				size_in_iod = daos_iods_len(iod, 1);
				if (size_in_iod == -1) {
					/* only for echo mode */
					sgls[i].sg_nr_out = sgls[i].sg_nr;
					continue;
				}
				if (is_ec_obj) {
					size_array[i] = replied_sizes[i];
					continue;
				}
				data_size = replied_sizes[i];
				D_ASSERTF(data_size <= size_in_iod,
					  "data_size "DF_U64
					  ", size_in_iod "DF_U64"\n",
					  data_size, size_in_iod);
				dc_sgl_out_set(&sgls[i], data_size);
			}
		}
		if (rc != 0)
			goto out;

		rc = rw_cb_csum_verify(rw_args);
		if (rc != 0)
			goto out;

		if (rw_args->maps != NULL && orwo->orw_maps.ca_count > 0) {
			daos_iom_t			*reply_maps;
			struct daos_recx_ep_list	*recov_list;

			D_ASSERT(reasb_req == NULL || !reasb_req->orr_recov);
			/** Should have 1 map per iod */
			D_ASSERTF(orwo->orw_maps.ca_count == orw->orw_nr,
				  "ca_count "DF_U64", orw_nr %d\n",
				  orwo->orw_maps.ca_count, orw->orw_nr);
			for (i = 0; i < orw->orw_nr; i++) {
				reply_maps = &orwo->orw_maps.ca_arrays[i];
				recov_list = orwo->orw_rels.ca_arrays;
				if (recov_list != NULL) {
					recov_list += i;
					if (recov_list->re_nr == 0)
						recov_list = NULL;
				}
				if (is_ec_obj &&
				    reply_maps->iom_type == DAOS_IOD_ARRAY) {
					struct obj_auxi_args	*obj_auxi;

					obj_auxi = rw_args->shard_args->auxi.obj_auxi;
					rc = obj_ec_iom_merge(obj_auxi->obj, reasb_req,
							      obj_auxi->dkey_hash,
							      orw->orw_oid.id_shard,
							      orw->orw_tgt_idx, reply_maps,
							      &rw_args->maps[i], recov_list);
				} else {
					rc = daos_iom_copy(reply_maps, &rw_args->maps[i]);
				}
				if (rc)
					goto out;
			}
		}
	}

out:
	if (rc == -DER_CSUM && opc == DAOS_OBJ_RPC_FETCH)
		dc_shard_csum_report(task, &rw_args->tgt_ep, rw_args->rpc);
	crt_req_decref(rw_args->rpc);

	if (ret == 0 || obj_retry_error(rc))
		ret = rc;
	return ret;
}

static struct dc_pool *
obj_shard_ptr2pool(struct dc_obj_shard *shard)
{
	return shard->do_obj->cob_pool;
}

int
dc_obj_shard_rw(struct dc_obj_shard *shard, enum obj_rpc_opc opc,
		void *shard_args, struct daos_shard_tgt *fw_shard_tgts,
		uint32_t fw_cnt, tse_task_t *task)
{
	struct shard_rw_args	*args = shard_args;
	struct shard_auxi_args	*auxi = &args->auxi;
	daos_obj_rw_t		*api_args = dc_task_get_args(auxi->obj_auxi->obj_task);
	struct dc_pool		*pool;
	daos_key_t		*dkey = api_args->dkey;
	unsigned int		 nr = api_args->nr;
	d_sg_list_t		*sgls = api_args->sgls;
	crt_rpc_t		*req = NULL;
	struct obj_rw_in	*orw;
	struct rw_cb_args	 rw_args;
	crt_endpoint_t		 tgt_ep;
	uuid_t			 cont_hdl_uuid;
	uuid_t			 cont_uuid;
	uint32_t		 flags = 0;
	int			 rc;

	if (DAOS_FAIL_CHECK(DAOS_SHARD_OBJ_UPDATE_TIMEOUT_SINGLE)) {
		if (auxi->shard == daos_fail_value_get()) {
			D_INFO("Set Shard %d update to return -DER_TIMEDOUT\n",
			       auxi->shard);
			daos_fail_loc_set(DAOS_SHARD_OBJ_UPDATE_TIMEOUT |
					  DAOS_FAIL_ONCE);
		}
	}
	if (DAOS_FAIL_CHECK(DAOS_OBJ_TGT_IDX_CHANGE))
		flags = ORF_DTX_SYNC;

	if (auxi->epoch.oe_flags & DTX_EPOCH_UNCERTAIN)
		flags |= ORF_EPOCH_UNCERTAIN;

	rc = dc_cont2uuid(shard->do_co, &cont_hdl_uuid, &cont_uuid);
	if (rc != 0)
		D_GOTO(out, rc);

	pool = obj_shard_ptr2pool(shard);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	tgt_ep.ep_grp = pool->dp_sys->sy_group;
	tgt_ep.ep_tag = shard->do_target_idx;
	tgt_ep.ep_rank = shard->do_target_rank;
	rw_args.tgt_ep = tgt_ep;
	if ((int)tgt_ep.ep_rank < 0)
		D_GOTO(out, rc = (int)tgt_ep.ep_rank);

	rc = obj_req_create(daos_task2ctx(task), &tgt_ep, opc, &req);
	D_DEBUG(DB_TRACE, "rpc %p opc:%d "DF_UOID" "DF_KEY" rank:%d tag:%d eph "
		DF_U64"\n", req, opc, DP_UOID(shard->do_id), DP_KEY(dkey),
		tgt_ep.ep_rank, tgt_ep.ep_tag, auxi->epoch.oe_value);
	if (rc != 0)
		D_GOTO(out, rc);

	if (DAOS_FAIL_CHECK(DAOS_SHARD_OBJ_FAIL))
		D_GOTO(out_req, rc = -DER_INVAL);

	orw = crt_req_get(req);
	D_ASSERT(orw != NULL);

	if (fw_shard_tgts != NULL) {
		D_ASSERT(fw_cnt >= 1);
		orw->orw_shard_tgts.ca_count = fw_cnt;
		orw->orw_shard_tgts.ca_arrays = fw_shard_tgts;
	} else {
		orw->orw_shard_tgts.ca_count = 0;
		orw->orw_shard_tgts.ca_arrays = NULL;
	}
	orw->orw_map_ver = auxi->map_ver;
	orw->orw_start_shard = auxi->start_shard;
	orw->orw_oid = shard->do_id;
	uuid_copy(orw->orw_pool_uuid, pool->dp_pool);
	uuid_copy(orw->orw_co_hdl, cont_hdl_uuid);
	uuid_copy(orw->orw_co_uuid, cont_uuid);
	daos_dti_copy(&orw->orw_dti, &args->dti);
	orw->orw_flags = auxi->flags | flags;
	if (auxi->obj_auxi->reintegrating)
		orw->orw_flags |= ORF_REINTEGRATING_IO;
	if (auxi->obj_auxi->rebuilding)
		orw->orw_flags |= ORF_REBUILDING_IO;
	orw->orw_tgt_idx = auxi->ec_tgt_idx;
	if (args->reasb_req && args->reasb_req->orr_oca)
		orw->orw_tgt_max = obj_ec_tgt_nr(args->reasb_req->orr_oca) - 1;
	if (auxi->obj_auxi->ec_degrade_fetch) {
		struct obj_tgt_oiod *toiod;

		D_ASSERT(args->reasb_req != NULL);
		D_ASSERT(args->reasb_req->tgt_oiods != NULL);
		D_ASSERT(!auxi->obj_auxi->spec_shard);
		toiod = obj_ec_tgt_oiod_get(args->reasb_req->tgt_oiods,
					    args->reasb_req->orr_tgt_nr,
					    auxi->ec_tgt_idx);
		D_ASSERTF(toiod != NULL, "tgt idx %u\n", auxi->ec_tgt_idx);
		if (toiod->oto_orig_tgt_idx != toiod->oto_tgt_idx) {
			orw->orw_flags |= ORF_EC_DEGRADED;
			orw->orw_tgt_idx = toiod->oto_orig_tgt_idx;
		}
	}

	orw->orw_dti_cos.ca_count = 0;
	orw->orw_dti_cos.ca_arrays = NULL;

	orw->orw_api_flags = api_args->flags;
	orw->orw_epoch = auxi->epoch.oe_value;
	orw->orw_epoch_first = auxi->epoch.oe_first;
	orw->orw_dkey_hash = auxi->obj_auxi->dkey_hash;
	orw->orw_nr = nr;
	orw->orw_dkey = *dkey;
	orw->orw_dkey_csum = args->dkey_csum;
	orw->orw_iod_array.oia_iod_nr = nr;
	orw->orw_iod_array.oia_iods = api_args->iods;
	orw->orw_iod_array.oia_iod_csums = args->iod_csums;
	orw->orw_iod_array.oia_oiods = args->oiods;
	orw->orw_iod_array.oia_oiod_nr = (args->oiods == NULL) ?
					 0 : nr;
	orw->orw_iod_array.oia_offs = args->offs;

	D_DEBUG(DB_IO, "rpc %p opc %d "DF_UOID" "DF_KEY" rank %d tag %d eph "
		DF_U64", DTI = "DF_DTI" start shard %u ver %u\n", req, opc,
		DP_UOID(shard->do_id), DP_KEY(dkey), tgt_ep.ep_rank,
		tgt_ep.ep_tag, auxi->epoch.oe_value, DP_DTI(&orw->orw_dti),
		orw->orw_start_shard, orw->orw_map_ver);

	if (args->bulks != NULL) {
		orw->orw_sgls.ca_count = 0;
		orw->orw_sgls.ca_arrays = NULL;
		orw->orw_bulks.ca_count = nr;
		orw->orw_bulks.ca_arrays = args->bulks;
		if (fw_shard_tgts != NULL)
			orw->orw_flags |= ORF_BULK_BIND;
	} else {
		if ((args->reasb_req && args->reasb_req->orr_size_fetch) ||
		    auxi->flags & ORF_CHECK_EXISTENCE) {
			/* NULL bulk/sgl for size_fetch or check existence */
			orw->orw_sgls.ca_count = 0;
			orw->orw_sgls.ca_arrays = NULL;
		} else {
			/* Transfer data inline */
			if (sgls != NULL)
				orw->orw_sgls.ca_count = nr;
			else
				orw->orw_sgls.ca_count = 0;
			orw->orw_sgls.ca_arrays = sgls;
		}
		orw->orw_bulks.ca_count = 0;
		orw->orw_bulks.ca_arrays = NULL;
	}

	crt_req_addref(req);
	rw_args.rpc = req;
	rw_args.hdlp = (daos_handle_t *)pool;
	rw_args.map_ver = &auxi->map_ver;
	rw_args.co = shard->do_co;
	rw_args.shard_args = args;
	/* remember the sgl to copyout the data inline for fetch */
	rw_args.rwaa_sgls = (opc == DAOS_OBJ_RPC_FETCH) ? sgls : NULL;
	if (args->reasb_req && args->reasb_req->orr_recov) {
		rw_args.maps = NULL;
		orw->orw_flags |= ORF_EC_RECOV;
		if (args->reasb_req->orr_recov_snap)
			orw->orw_flags |= ORF_EC_RECOV_SNAP;
	} else {
		if (api_args->extra_flags & DIOF_EC_RECOV_FROM_PARITY)
			orw->orw_flags |= ORF_EC_RECOV_FROM_PARITY;
		rw_args.maps = api_args->ioms;
	}
	if (opc == DAOS_OBJ_RPC_FETCH) {
		if (args->iod_csums != NULL) {
			orw->orw_flags |= (ORF_CREATE_MAP |
					   ORF_CREATE_MAP_DETAIL);
		} else if (rw_args.maps != NULL) {
			orw->orw_flags |= ORF_CREATE_MAP;
			if (rw_args.maps->iom_flags & DAOS_IOMF_DETAIL)
				orw->orw_flags |= ORF_CREATE_MAP_DETAIL;
		}
	}

	if (DAOS_FAIL_CHECK(DAOS_SHARD_OBJ_RW_CRT_ERROR))
		D_GOTO(out_args, rc = -DER_HG);

	rc = tse_task_register_comp_cb(task, dc_rw_cb, &rw_args,
				       sizeof(rw_args));
	if (rc != 0)
		D_GOTO(out_args, rc);

	if (daos_io_bypass & IOBP_CLI_RPC) {
		rc = daos_rpc_complete(req, task);
	} else {
		if (opc == DAOS_OBJ_RPC_UPDATE && args->bulks != NULL &&
		    !(orw->orw_flags & ORF_RESEND) &&
		    DAOS_FAIL_CHECK(DAOS_DTX_RESEND_DELAY1)) {
			/* RPC (from client to server) timeout is 3 seconds. */
			rc = crt_req_set_timeout(req, 3);
			if (rc != 0)
				D_ERROR("crt_req_set_timeout error: %d\n", rc);
		    }

		rc = daos_rpc_send(req, task);
	}

	return rc;

out_args:
	crt_req_decref(req);
out_req:
	crt_req_decref(req);
out:
	tse_task_complete(task, rc);
	return rc;
}

struct obj_punch_cb_args {
	crt_rpc_t	*rpc;
	unsigned int	*map_ver;
};

static int
obj_shard_punch_cb(tse_task_t *task, void *data)
{
	struct obj_punch_cb_args	*cb_args;
	crt_rpc_t			*rpc;

	cb_args = (struct obj_punch_cb_args *)data;
	rpc = cb_args->rpc;
	if (task->dt_result == 0) {
		task->dt_result = obj_reply_get_status(rpc);
		*cb_args->map_ver = obj_reply_map_version_get(rpc);
	}

	crt_req_decref(rpc);
	return task->dt_result;
}

int
dc_obj_shard_punch(struct dc_obj_shard *shard, enum obj_rpc_opc opc,
		   void *shard_args, struct daos_shard_tgt *fw_shard_tgts,
		   uint32_t fw_cnt, tse_task_t *task)
{
	struct shard_punch_args		*args = shard_args;
	daos_obj_punch_t		*obj_args;
	daos_key_t			*dkey;
	struct dc_pool			*pool;
	struct obj_punch_in		*opi;
	crt_rpc_t			*req;
	struct obj_punch_cb_args	 cb_args;
	daos_unit_oid_t			 oid;
	crt_endpoint_t			 tgt_ep;
	int				 rc;

	obj_args = dc_task_get_args(args->pa_auxi.obj_auxi->obj_task);
	dkey = obj_args->dkey;
	pool = obj_shard_ptr2pool(shard);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	oid = shard->do_id;
	tgt_ep.ep_grp	= pool->dp_sys->sy_group;
	tgt_ep.ep_tag	= shard->do_target_idx;
	tgt_ep.ep_rank = shard->do_target_rank;
	if ((int)tgt_ep.ep_rank < 0)
		D_GOTO(out, rc = (int)tgt_ep.ep_rank);

	D_DEBUG(DB_IO, "opc=%d, rank=%d tag=%d flags "DF_X64" epoch "DF_U64".\n",
		opc, tgt_ep.ep_rank, tgt_ep.ep_tag, obj_args->flags,
		args->pa_auxi.epoch.oe_value);

	rc = obj_req_create(daos_task2ctx(task), &tgt_ep, opc, &req);
	if (rc != 0)
		D_GOTO(out, rc);

	crt_req_addref(req);
	cb_args.rpc = req;
	cb_args.map_ver = &args->pa_auxi.map_ver;
	rc = tse_task_register_comp_cb(task, obj_shard_punch_cb, &cb_args,
				       sizeof(cb_args));
	if (rc != 0)
		D_GOTO(out_req, rc);

	opi = crt_req_get(req);
	D_ASSERT(opi != NULL);

	opi->opi_map_ver	 = args->pa_auxi.map_ver;
	opi->opi_api_flags	 = obj_args->flags;
	opi->opi_epoch		 = args->pa_auxi.epoch.oe_value;
	opi->opi_dkey_hash	 = args->pa_auxi.obj_auxi->dkey_hash;
	opi->opi_oid		 = oid;
	opi->opi_dkeys.ca_count  = (dkey == NULL) ? 0 : 1;
	opi->opi_dkeys.ca_arrays = dkey;
	opi->opi_akeys.ca_count	 = obj_args->akey_nr;
	opi->opi_akeys.ca_arrays = obj_args->akeys;
	if (fw_shard_tgts != NULL) {
		D_ASSERT(fw_cnt >= 1);
		opi->opi_shard_tgts.ca_count = fw_cnt;
		opi->opi_shard_tgts.ca_arrays = fw_shard_tgts;
	} else {
		opi->opi_shard_tgts.ca_count = 0;
		opi->opi_shard_tgts.ca_arrays = NULL;
	}
	uuid_copy(opi->opi_pool_uuid, pool->dp_pool);
	uuid_copy(opi->opi_co_hdl, args->pa_coh_uuid);
	uuid_copy(opi->opi_co_uuid, args->pa_cont_uuid);
	daos_dti_copy(&opi->opi_dti, &args->pa_dti);
	opi->opi_flags = args->pa_auxi.flags;
	opi->opi_dti_cos.ca_count = 0;
	opi->opi_dti_cos.ca_arrays = NULL;

	rc = daos_rpc_send(req, task);
	return rc;

out_req:
	crt_req_decref(req);
	crt_req_decref(req);
out:
	tse_task_complete(task, rc);
	return rc;
}

struct obj_enum_args {
	crt_rpc_t		*rpc;
	daos_handle_t		*hdlp;
	uint32_t		*eaa_nr;
	daos_key_desc_t		*eaa_kds;
	daos_anchor_t		*eaa_anchor;
	daos_anchor_t		*eaa_dkey_anchor;
	daos_anchor_t		*eaa_akey_anchor;
	struct dc_obj_shard	*eaa_obj;
	d_sg_list_t		*eaa_sgl;
	daos_recx_t		*eaa_recxs;
	daos_size_t		*eaa_size;
	unsigned int		*eaa_map_ver;
	d_iov_t			*csum;
	struct dtx_epoch	*epoch;
	daos_handle_t		*th;
};

/**
 * use iod/iod_csum as vehicle to verify data
 */
static int
csum_enum_verify_recx(struct daos_csummer *csummer, struct obj_enum_rec *rec,
		      d_iov_t *enum_type_val, struct dcs_csum_info *csum_info)
{
	daos_iod_t		 tmp_iod = {0};
	d_sg_list_t		 tmp_sgl = {0};
	struct dcs_iod_csums	 tmp_iod_csum = {0};
	int			 rc;

	tmp_iod.iod_size = rec->rec_size;
	tmp_iod.iod_type = DAOS_IOD_ARRAY;
	tmp_iod.iod_recxs = &rec->rec_recx;
	tmp_iod.iod_nr = 1;

	tmp_sgl.sg_nr = tmp_sgl.sg_nr_out = 1;
	tmp_sgl.sg_iovs = enum_type_val;

	tmp_iod_csum.ic_nr = 1;
	tmp_iod_csum.ic_data = csum_info;

	rc = daos_csummer_verify_iod(csummer, &tmp_iod, &tmp_sgl,
				     &tmp_iod_csum, NULL, 0, NULL);

	return rc;
}

/**
 * use iod/iod_csum as vehicle to verify data
 */
static int
csum_enum_verify_sv(struct daos_csummer *csummer, struct obj_enum_rec *rec,
		    d_iov_t *enum_type_val, struct dcs_csum_info *csum_info)
{
	daos_iod_t		 tmp_iod = {0};
	d_sg_list_t		 tmp_sgl = {0};
	struct dcs_iod_csums	 tmp_iod_csum = {0};
	int			 rc;

	tmp_iod.iod_size = rec->rec_size;
	tmp_iod.iod_type = DAOS_IOD_SINGLE;
	tmp_iod.iod_nr = 1;

	tmp_sgl.sg_nr = tmp_sgl.sg_nr_out = 1;
	tmp_sgl.sg_iovs = enum_type_val;

	tmp_iod_csum.ic_nr = 1;
	tmp_iod_csum.ic_data = csum_info;
	rc = daos_csummer_verify_iod(csummer, &tmp_iod, &tmp_sgl,
				     &tmp_iod_csum, NULL, 0, NULL);

	return rc;
}

struct csum_enum_args {
	d_iov_t			*csum_iov;
	struct daos_csummer	*csummer;
};

static int
verify_csum_cb(daos_key_desc_t *kd, void *buf, unsigned int size, void *arg)
{
	struct dcs_csum_info	 *ci_to_compare = NULL;
	struct csum_enum_args	*args = arg;
	d_iov_t			 enum_type_val;
	int rc;

	switch (kd->kd_val_type) {
	case OBJ_ITER_SINGLE:
	case OBJ_ITER_RECX: {
		struct obj_enum_rec	*rec;
		uint64_t		 rec_data_len;

		rec = buf;
		buf += sizeof(*rec);

		/**
		 * Only inlined data has csums serialized
		 * to csum_iov
		 */
		if (!(rec->rec_flags & RECX_INLINE))
			return 0;

		ci_cast(&ci_to_compare, args->csum_iov);
		ci_move_next_iov(ci_to_compare, args->csum_iov);
		rec_data_len = rec->rec_size *
			       rec->rec_recx.rx_nr;

		d_iov_set(&enum_type_val, buf, rec_data_len);

		if (kd->kd_val_type == OBJ_ITER_RECX)
			rc = csum_enum_verify_recx(args->csummer, rec,
						   &enum_type_val,
						   ci_to_compare);
		else
			rc = csum_enum_verify_sv(args->csummer, rec,
						 &enum_type_val,
						 ci_to_compare);
		if (rc != 0)
			return rc;
		break;
	}
	case OBJ_ITER_AKEY:
	case OBJ_ITER_DKEY:
		d_iov_set(&enum_type_val, buf, kd->kd_key_len);
		/**
		  * fault injection - corrupt keys before verifying -
		  * simulates corruption over network
		  */
		if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_FETCH_AKEY) ||
		    DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_FETCH_DKEY))
			((uint8_t *)buf)[0] += 2;

		ci_cast(&ci_to_compare, args->csum_iov);
		ci_move_next_iov(ci_to_compare, args->csum_iov);

		rc = daos_csummer_verify_key(args->csummer,
					     &enum_type_val, ci_to_compare);

		if (rc != 0) {
			D_ERROR("daos_csummer_verify_key error for %s: %d\n",
				kd->kd_val_type == OBJ_ITER_AKEY ? "AKEY" : "DKEY", rc);
			return rc;
		}
		break;
	default:

		break;
	}

	return 0;
}

/* See obj_enum.c:fill_rec() for how the recx and csum_iov is serialized */
static int
csum_enum_verify(const struct obj_enum_args *enum_args,
		 const struct obj_key_enum_out *oeo)
{
	struct daos_csummer	*csummer;
	int			 rc = 0;
	d_sg_list_t		 sgl = oeo->oeo_sgl;
	d_iov_t			 csum_iov = oeo->oeo_csum_iov;

	if (enum_args->eaa_nr == NULL ||
	    *enum_args->eaa_nr == 0 ||
	    sgl.sg_nr_out == 0)
		return 0; /** no keys to verify */

	csummer = enum_args->eaa_obj->do_co->dc_csummer;
	if (!daos_csummer_initialized(csummer) || csummer->dcs_skip_key_verify)
		return 0; /** csums not enabled */

	struct csum_enum_args csum_args = {0};

	csum_args.csummer = daos_csummer_copy(csummer);
	if (csum_args.csummer == NULL)
		return -DER_NOMEM;
	csum_args.csum_iov = &csum_iov;

	rc = obj_enum_iterate(enum_args->eaa_kds, &sgl, *enum_args->eaa_nr,
			      -1, verify_csum_cb, &csum_args);

	daos_csummer_destroy(&csum_args.csummer);

	return rc;
}

/**
 * If requested (dst iov is set) and there is csum info to copy, copy the
 * serialized csum. If not all of it will fit into the provided buffer, copy
 * what can and set the destination iov len to needed len and let caller
 * decide what to do.
 */
static int
dc_enumerate_copy_csum(d_iov_t *dst, const d_iov_t *src)
{
	if (dst != NULL && src->iov_len > 0) {
		memcpy(dst->iov_buf, src->iov_buf,
		       min(dst->iov_buf_len,
			   src->iov_len));
		dst->iov_len = src->iov_len;
		if (dst->iov_len > dst->iov_buf_len) {
			D_DEBUG(DB_CSUM, "Checksum buffer truncated %d > %d\n",
				(int)dst->iov_len, (int)dst->iov_buf_len);
			return -DER_TRUNC;
		}
	}
	return 0;
}

static int
dc_enumerate_cb(tse_task_t *task, void *arg)
{
	struct obj_enum_args	*enum_args = (struct obj_enum_args *)arg;
	struct obj_key_enum_in	*oei;
	struct obj_key_enum_out	*oeo;
	int			 opc = opc_get(enum_args->rpc->cr_opc);
	int			 ret = task->dt_result;
	int			 rc = 0;

	oei = crt_req_get(enum_args->rpc);
	D_ASSERT(oei != NULL);

	if (ret != 0) {
		/* If any failure happens inside Cart, let's reset
		 * failure to TIMEDOUT, so the upper layer can retry
		 **/
		D_ERROR("RPC %d failed: "DF_RC"\n", opc, DP_RC(ret));
		D_GOTO(out, ret);
	}

	oeo = crt_reply_get(enum_args->rpc);

	rc = obj_reply_get_status(enum_args->rpc);

	/* See the similar dc_rw_cb. */
	if (daos_handle_is_valid(*enum_args->th)) {
		int rc_tmp;

		rc_tmp = dc_tx_op_end(task, *enum_args->th, enum_args->epoch,
				      rc, oeo->oeo_epoch);
		if (rc_tmp != 0) {
			D_ERROR("failed to end transaction operation (rc=%d "
				"epoch="DF_U64": "DF_RC"\n", rc,
				oeo->oeo_epoch, DP_RC(rc_tmp));
			goto out;
		}
	}

	if (rc != 0) {
		if (rc == -DER_KEY2BIG) {
			D_DEBUG(DB_IO, "key size "DF_U64" %p too big.\n",
				oeo->oeo_size, enum_args->eaa_kds);
			if (enum_args->eaa_kds)
				enum_args->eaa_kds[0].kd_key_len = oeo->oeo_size;
		} else if (rc == -DER_INPROGRESS || rc == -DER_TX_BUSY) {
			D_DEBUG(DB_TRACE, "rpc %p RPC %d may need retry: "DF_RC"\n",
				enum_args->rpc, opc, DP_RC(rc));
		} else if (rc == -DER_TX_RESTART) {
			D_DEBUG(DB_TRACE, "rpc %p RPC %d may need restart: "DF_RC"\n",
				enum_args->rpc, opc, DP_RC(rc));
		} else {
			D_ERROR("rpc %p RPC %d failed: "DF_RC"\n",
				enum_args->rpc, opc, DP_RC(rc));
		}
		D_GOTO(out, rc);
	}

	rc = dc_enumerate_copy_csum(enum_args->csum, &oeo->oeo_csum_iov);
	if (rc != 0)
		D_GOTO(out, rc);

	*enum_args->eaa_map_ver = obj_reply_map_version_get(enum_args->rpc);

	if (enum_args->eaa_size)
		*enum_args->eaa_size = oeo->oeo_size;

	if (*enum_args->eaa_nr < oeo->oeo_num) {
		D_ERROR("key enumerate get %d > %d more kds, %d\n",
			oeo->oeo_num, *enum_args->eaa_nr, -DER_PROTO);
		D_GOTO(out, rc = -DER_PROTO);
	}

	*enum_args->eaa_nr = oeo->oeo_num;

	if (enum_args->eaa_kds && oeo->oeo_kds.ca_count > 0)
		memcpy(enum_args->eaa_kds, oeo->oeo_kds.ca_arrays,
		       sizeof(*enum_args->eaa_kds) *
		       oeo->oeo_kds.ca_count);

	if (enum_args->eaa_recxs && oeo->oeo_recxs.ca_count > 0) {
		D_ASSERTF(*enum_args->eaa_nr >= oeo->oeo_recxs.ca_count,
			  "eaa_nr %d, ca_count "DF_U64"\n",
			  *enum_args->eaa_nr, oeo->oeo_recxs.ca_count);
		memcpy(enum_args->eaa_recxs, oeo->oeo_recxs.ca_arrays,
		       sizeof(*enum_args->eaa_recxs) *
		       oeo->oeo_recxs.ca_count);
	}

	if (enum_args->eaa_sgl) {
		if (oeo->oeo_sgl.sg_nr > 0) {
			rc = daos_sgl_copy_data_out(enum_args->eaa_sgl, &oeo->oeo_sgl);
			if (rc)
				D_GOTO(out, rc);
		} else {
			dc_sgl_out_set(enum_args->eaa_sgl, oeo->oeo_size);
		}
	}

	/* Update dkey hash and tag */
	if (enum_args->eaa_dkey_anchor)
		enum_anchor_copy(enum_args->eaa_dkey_anchor,
				 &oeo->oeo_dkey_anchor);

	if (enum_args->eaa_akey_anchor)
		enum_anchor_copy(enum_args->eaa_akey_anchor,
				 &oeo->oeo_akey_anchor);

	if (enum_args->eaa_anchor)
		enum_anchor_copy(enum_args->eaa_anchor,
				 &oeo->oeo_anchor);
	rc = csum_enum_verify(enum_args, oeo);
	if (rc != 0)
		D_GOTO(out, rc);

out:
	if (enum_args->eaa_obj != NULL)
		obj_shard_decref(enum_args->eaa_obj);

	if (oei->oei_bulk != NULL)
		crt_bulk_free(oei->oei_bulk);
	if (oei->oei_kds_bulk != NULL)
		crt_bulk_free(oei->oei_kds_bulk);
	crt_req_decref(enum_args->rpc);

	if (ret == 0 || obj_retry_error(rc))
		ret = rc;
	return ret;
}

#define KDS_BULK_LIMIT	128

int
dc_obj_shard_list(struct dc_obj_shard *obj_shard, enum obj_rpc_opc opc,
		  void *shard_args, struct daos_shard_tgt *fw_shard_tgts,
		  uint32_t fw_cnt, tse_task_t *task)
{
	struct shard_list_args *args = shard_args;
	daos_obj_list_t		*obj_args = dc_task_get_args(args->la_auxi.obj_auxi->obj_task);
	daos_key_desc_t	       *kds = args->la_kds;
	d_sg_list_t	       *sgl = args->la_sgl;
	crt_endpoint_t		tgt_ep;
	struct dc_pool	       *pool = NULL;
	crt_rpc_t	       *req;
	uuid_t			cont_hdl_uuid;
	uuid_t			cont_uuid;
	struct obj_key_enum_in	*oei;
	struct obj_enum_args	enum_args;
	daos_size_t		sgl_size = 0;
	int			rc;

	D_ASSERT(obj_shard != NULL);
	obj_shard_addref(obj_shard);

	pool = obj_shard_ptr2pool(obj_shard);
	if (pool == NULL)
		D_GOTO(out_put, rc = -DER_NO_HDL);

	rc = dc_cont2uuid(obj_shard->do_co, &cont_hdl_uuid, &cont_uuid);
	if (rc != 0)
		D_GOTO(out_put, rc);

	tgt_ep.ep_grp = pool->dp_sys->sy_group;
	tgt_ep.ep_tag = obj_shard->do_target_idx;
	tgt_ep.ep_rank = obj_shard->do_target_rank;
	if ((int)tgt_ep.ep_rank < 0)
		D_GOTO(out_put, rc = (int)tgt_ep.ep_rank);

	D_DEBUG(DB_IO, "opc %d "DF_UOID" rank %d tag %d\n",
		opc, DP_UOID(obj_shard->do_id), tgt_ep.ep_rank, tgt_ep.ep_tag);

	rc = obj_req_create(daos_task2ctx(task), &tgt_ep, opc, &req);
	if (rc != 0)
		D_GOTO(out_put, rc);

	oei = crt_req_get(req);
	D_ASSERT(oei != NULL);

	if (obj_args->dkey != NULL)
		oei->oei_dkey = *obj_args->dkey;
	if (obj_args->akey != NULL)
		oei->oei_akey = *obj_args->akey;
	oei->oei_oid		= obj_shard->do_id;
	oei->oei_map_ver	= args->la_auxi.map_ver;
	if (args->la_auxi.epoch.oe_flags & DTX_EPOCH_UNCERTAIN)
		oei->oei_flags |= ORF_EPOCH_UNCERTAIN;
	if (obj_args->eprs != NULL && opc == DAOS_OBJ_RPC_ENUMERATE) {
		oei->oei_epr = *obj_args->eprs;
		/*
		 * If an epoch range is specified, we shall not assume any
		 * epoch uncertainty.
		 */
		oei->oei_flags &= ~ORF_EPOCH_UNCERTAIN;
	} else {
		/*
		 * Note that we reuse oei_epr as "epoch_first" and "epoch" to
		 * save space.
		 */
		oei->oei_epr.epr_lo = args->la_auxi.epoch.oe_first;
		oei->oei_epr.epr_hi = args->la_auxi.epoch.oe_value;
		oei->oei_flags |= ORF_ENUM_WITHOUT_EPR;
	}
	if ((!obj_args->incr_order) && (opc == DAOS_OBJ_RECX_RPC_ENUMERATE))
		oei->oei_flags |= ORF_DESCENDING_ORDER;

	oei->oei_nr		= args->la_nr;
	oei->oei_rec_type	= obj_args->type;
	uuid_copy(oei->oei_pool_uuid, pool->dp_pool);
	uuid_copy(oei->oei_co_hdl, cont_hdl_uuid);
	uuid_copy(oei->oei_co_uuid, cont_uuid);
	daos_dti_copy(&oei->oei_dti, &args->la_dti);

	if (args->la_anchor != NULL)
		enum_anchor_copy(&oei->oei_anchor, args->la_anchor);
	if (args->la_dkey_anchor != NULL) {
		enum_anchor_copy(&oei->oei_dkey_anchor, args->la_dkey_anchor);

		if (daos_anchor_get_flags(args->la_dkey_anchor) &
		    DIOF_FOR_MIGRATION)
			oei->oei_flags |= ORF_FOR_MIGRATION;
		if (daos_anchor_get_flags(args->la_dkey_anchor) & DIOF_RECX_REVERSE)
			oei->oei_flags |= ORF_DESCENDING_ORDER;
	}
	if (args->la_akey_anchor != NULL)
		enum_anchor_copy(&oei->oei_akey_anchor, args->la_akey_anchor);

	if (sgl != NULL) {
		oei->oei_sgl = *sgl;
		sgl_size = daos_sgls_packed_size(sgl, 1, NULL);
		if (sgl_size >= DAOS_BULK_LIMIT) {
			/* Create bulk */
			rc = crt_bulk_create(daos_task2ctx(task),
					     sgl, CRT_BULK_RW,
					     &oei->oei_bulk);
			if (rc < 0)
				D_GOTO(out_req, rc);
		}
	}

	if (args->la_nr > KDS_BULK_LIMIT) {
		d_sg_list_t	tmp_sgl = { 0 };
		d_iov_t		tmp_iov = { 0 };

		tmp_iov.iov_buf_len = sizeof(*kds) * args->la_nr;
		tmp_iov.iov_buf = kds;
		tmp_sgl.sg_nr_out = 1;
		tmp_sgl.sg_nr = 1;
		tmp_sgl.sg_iovs = &tmp_iov;

		rc = crt_bulk_create(daos_task2ctx(task),
				     &tmp_sgl, CRT_BULK_RW,
				     &oei->oei_kds_bulk);
		if (rc < 0)
			D_GOTO(out_req, rc);
	}

	crt_req_addref(req);
	enum_args.rpc = req;
	enum_args.hdlp = (daos_handle_t *)pool;
	enum_args.eaa_nr = &args->la_nr;
	enum_args.eaa_kds = kds;
	enum_args.eaa_anchor = args->la_anchor;
	enum_args.eaa_dkey_anchor = args->la_dkey_anchor;
	enum_args.eaa_akey_anchor = args->la_akey_anchor;
	enum_args.eaa_obj = obj_shard;
	enum_args.eaa_size = obj_args->size;
	enum_args.eaa_sgl = sgl;
	enum_args.csum = obj_args->csum;
	enum_args.eaa_map_ver = &args->la_auxi.map_ver;
	enum_args.eaa_recxs = args->la_recxs;
	enum_args.epoch = &args->la_auxi.epoch;
	enum_args.th = &obj_args->th;
	rc = tse_task_register_comp_cb(task, dc_enumerate_cb, &enum_args,
				       sizeof(enum_args));
	if (rc != 0)
		D_GOTO(out_eaa, rc);

	return daos_rpc_send(req, task);

out_eaa:
	crt_req_decref(req);
	if (sgl != NULL && sgl_size >= DAOS_BULK_LIMIT)
		crt_bulk_free(oei->oei_bulk);
out_req:
	crt_req_decref(req);
out_put:
	obj_shard_decref(obj_shard);
	tse_task_complete(task, rc);
	return rc;
}

struct obj_query_key_cb_args {
	crt_rpc_t		*rpc;
	unsigned int		*map_ver;
	daos_unit_oid_t		oid;
	daos_key_t		*dkey;
	daos_key_t		*akey;
	daos_recx_t		*recx;
	daos_epoch_t		*max_epoch;
	struct dc_object	*obj;
	struct dc_obj_shard	*shard;
	struct dtx_epoch	epoch;
	daos_handle_t		th;
};

static void
obj_shard_query_recx_post(struct obj_query_key_cb_args *cb_args, uint32_t shard, daos_key_t *dkey,
			  daos_recx_t *reply_recx, bool get_max, bool changed)
{
	daos_recx_t		*result_recx = cb_args->recx;
	daos_recx_t		 tmp_recx = {0};
	uint64_t		 tmp_end;
	uint32_t		 tgt_off;
	bool			 from_data_tgt;
	struct daos_oclass_attr	*oca;
	uint64_t		dkey_hash;
	uint64_t		 stripe_rec_nr, cell_rec_nr;

	oca = obj_get_oca(cb_args->obj);
	if (oca == NULL || !daos_oclass_is_ec(oca)) {
		*result_recx = *reply_recx;
		return;
	}

	dkey_hash = obj_dkey2hash(cb_args->obj->cob_md.omd_id, dkey);
	tgt_off = obj_ec_shard_off(cb_args->obj, dkey_hash, shard);
	from_data_tgt = is_ec_data_shard_by_tgt_off(tgt_off, oca);
	stripe_rec_nr = obj_ec_stripe_rec_nr(oca);
	cell_rec_nr = obj_ec_cell_rec_nr(oca);
	D_ASSERT(!(reply_recx->rx_idx & PARITY_INDICATOR));
	/* data ext from data shard needs to convert to daos ext,
	 * replica ext from parity shard needs not to convert.
	 */
	tmp_recx = *reply_recx;
	tmp_end = DAOS_RECX_END(tmp_recx);
	D_DEBUG(DB_IO, "shard %d/%u get recx "DF_U64" "DF_U64"\n",
		shard, tgt_off, tmp_recx.rx_idx, tmp_recx.rx_nr);
	if (tmp_end > 0 && from_data_tgt) {
		if (get_max) {
			tmp_recx.rx_idx = max(tmp_recx.rx_idx, rounddown(tmp_end - 1, cell_rec_nr));
			tmp_recx.rx_nr = tmp_end - tmp_recx.rx_idx;
		} else {
			tmp_recx.rx_nr = min(tmp_end, roundup(tmp_recx.rx_idx + 1, cell_rec_nr)) -
					 tmp_recx.rx_idx;
		}

		tmp_recx.rx_idx = obj_ec_idx_vos2daos(tmp_recx.rx_idx, stripe_rec_nr,
							      cell_rec_nr, tgt_off);
		tmp_end = DAOS_RECX_END(tmp_recx);
	}

	if (get_max) {
		if (DAOS_RECX_END(*result_recx) < tmp_end || changed)
			*result_recx = tmp_recx;
	} else {
		if (DAOS_RECX_END(*result_recx) > tmp_end || changed)
			*result_recx = tmp_recx;
	}
}

static int
obj_shard_query_key_cb(tse_task_t *task, void *data)
{
	struct obj_query_key_cb_args	*cb_args;
	struct obj_query_key_in		*okqi;
	struct obj_query_key_out	*okqo;
	uint32_t			flags;
	int				opc;
	int				ret = task->dt_result;
	int				rc = 0;
	crt_rpc_t			*rpc;

	cb_args = (struct obj_query_key_cb_args *)data;
	rpc = cb_args->rpc;

	okqi = crt_req_get(cb_args->rpc);
	D_ASSERT(okqi != NULL);

	flags = okqi->okqi_api_flags;
	opc = opc_get(cb_args->rpc->cr_opc);

	if (ret != 0) {
		D_ERROR("RPC %d failed, "DF_RC"\n", opc, DP_RC(ret));
		D_GOTO(out, ret);
	}

	okqo = crt_reply_get(cb_args->rpc);
	rc = obj_reply_get_status(rpc);

	/* See the similar dc_rw_cb. */
	if (daos_handle_is_valid(cb_args->th)) {
		int rc_tmp;

		rc_tmp = dc_tx_op_end(task, cb_args->th, &cb_args->epoch, rc,
				      okqo->okqo_epoch);
		if (rc_tmp != 0) {
			D_ERROR("failed to end transaction operation (rc=%d "
				"epoch="DF_U64": "DF_RC"\n", rc,
				okqo->okqo_epoch, DP_RC(rc_tmp));
			goto out;
		}
	}

	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			D_SPIN_LOCK(&cb_args->obj->cob_spin);
			D_GOTO(set_max_epoch, rc = 0);
		}

		if (rc == -DER_INPROGRESS || rc == -DER_TX_BUSY)
			D_DEBUG(DB_TRACE, "rpc %p RPC %d may need retry: %d\n",
				cb_args->rpc, opc, rc);
		else
			D_ERROR("rpc %p RPC %d failed: %d\n",
				cb_args->rpc, opc, rc);
		D_GOTO(out, rc);
	}

	D_SPIN_LOCK(&cb_args->obj->cob_spin);
	*cb_args->map_ver = obj_reply_map_version_get(rpc);

	if (flags == 0)
		goto set_max_epoch;

	bool check = true;
	bool changed = false;
	bool first = (cb_args->dkey->iov_len == 0);
	bool is_ec_obj = obj_is_ec(cb_args->obj);

	if (flags & DAOS_GET_DKEY) {
		uint64_t *val = (uint64_t *)okqo->okqo_dkey.iov_buf;
		uint64_t *cur = (uint64_t *)cb_args->dkey->iov_buf;

		if (okqo->okqo_dkey.iov_len != sizeof(uint64_t)) {
			D_ERROR("Invalid Dkey obtained\n");
			D_SPIN_UNLOCK(&cb_args->obj->cob_spin);
			D_GOTO(out, rc = -DER_IO);
		}

		/** for first cb, just set the dkey */
		if (first) {
			*cur = *val;
			cb_args->dkey->iov_len = okqo->okqo_dkey.iov_len;
			changed = true;
		} else if (flags & DAOS_GET_MAX) {
			if (*val > *cur) {
				D_DEBUG(DB_IO, "dkey update "DF_U64"->"
					DF_U64"\n", *cur, *val);
				*cur = *val;
				/** set to change akey and recx */
				changed = true;
			} else {
				/** no change, don't check akey and recx for
				 * replica obj, for EC obj need to check again
				 * as it possibly from different data shards.
				 */
				if (!is_ec_obj || *val < *cur)
					check = false;
			}
		} else if (flags & DAOS_GET_MIN) {
			if (*val < *cur) {
				*cur = *val;
				/** set to change akey and recx */
				changed = true;
			} else {
				if (!is_ec_obj)
					check = false;
			}
		} else {
			D_ASSERT(0);
		}
	}

	if (check && flags & DAOS_GET_AKEY) {
		uint64_t *val = (uint64_t *)okqo->okqo_akey.iov_buf;
		uint64_t *cur = (uint64_t *)cb_args->akey->iov_buf;

		/** if first cb, or dkey changed, set akey */
		if (first || changed)
			*cur = *val;
	}

	if (check && flags & DAOS_GET_RECX) {
		bool		 get_max = (okqi->okqi_api_flags & DAOS_GET_MAX);
		daos_key_t	*dkey;

		if (okqi->okqi_api_flags & DAOS_GET_DKEY)
			dkey = &okqo->okqo_dkey;
		else
			dkey = &okqi->okqi_dkey;
		obj_shard_query_recx_post(cb_args, okqi->okqi_oid.id_shard,
					  dkey, &okqo->okqo_recx, get_max, changed);
	}

set_max_epoch:
	if (cb_args->max_epoch && *cb_args->max_epoch < okqo->okqo_max_epoch)
		*cb_args->max_epoch = okqo->okqo_max_epoch;
	D_SPIN_UNLOCK(&cb_args->obj->cob_spin);

out:
	crt_req_decref(rpc);
	if (ret == 0 || obj_retry_error(rc))
		ret = rc;
	return ret;
}

int
dc_obj_shard_query_key(struct dc_obj_shard *shard, struct dtx_epoch *epoch, uint32_t flags,
		       uint32_t req_map_ver, struct dc_object *obj,
		       daos_key_t *dkey, daos_key_t *akey, daos_recx_t *recx,
		       daos_epoch_t *max_epoch, const uuid_t coh_uuid, const uuid_t cont_uuid,
		       struct dtx_id *dti, uint32_t *map_ver, daos_handle_t th, tse_task_t *task)
{
	struct dc_pool			*pool = NULL;
	struct obj_query_key_in		*okqi;
	crt_rpc_t			*req;
	struct obj_query_key_cb_args	 cb_args;
	daos_unit_oid_t			 oid;
	crt_endpoint_t			 tgt_ep;
	int				 rc;

	pool = obj_shard_ptr2pool(shard);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	oid = shard->do_id;
	tgt_ep.ep_grp	= pool->dp_sys->sy_group;
	tgt_ep.ep_tag	= shard->do_target_idx;
	tgt_ep.ep_rank = shard->do_target_rank;
	if ((int)tgt_ep.ep_rank < 0)
		D_GOTO(out, rc = (int)tgt_ep.ep_rank);

	D_DEBUG(DB_IO, "OBJ_QUERY_KEY_RPC, rank=%d tag=%d.\n", tgt_ep.ep_rank, tgt_ep.ep_tag);

	rc = obj_req_create(daos_task2ctx(task), &tgt_ep, DAOS_OBJ_RPC_QUERY_KEY, &req);
	if (rc != 0)
		D_GOTO(out, rc);

	crt_req_addref(req);
	cb_args.rpc		= req;
	cb_args.map_ver		= map_ver;
	cb_args.oid		= shard->do_id;
	cb_args.dkey		= dkey;
	cb_args.akey		= akey;
	cb_args.recx		= recx;
	cb_args.obj		= obj;
	cb_args.shard		= shard;
	cb_args.epoch		= *epoch;
	cb_args.th		= th;
	cb_args.max_epoch	= max_epoch;

	rc = tse_task_register_comp_cb(task, obj_shard_query_key_cb, &cb_args, sizeof(cb_args));
	if (rc != 0)
		D_GOTO(out_req, rc);

	okqi = crt_req_get(req);
	D_ASSERT(okqi != NULL);

	okqi->okqi_map_ver		= req_map_ver;
	okqi->okqi_epoch		= epoch->oe_value;
	okqi->okqi_epoch_first		= epoch->oe_first;
	okqi->okqi_api_flags		= flags;
	okqi->okqi_oid			= oid;
	d_iov_set(&okqi->okqi_dkey, NULL, 0);
	d_iov_set(&okqi->okqi_akey, NULL, 0);
	if (dkey != NULL && !(flags & DAOS_GET_DKEY))
		okqi->okqi_dkey		= *dkey;
	if (akey != NULL && !(flags & DAOS_GET_AKEY))
		okqi->okqi_akey		= *akey;
	if (epoch->oe_flags & DTX_EPOCH_UNCERTAIN)
		okqi->okqi_flags	= ORF_EPOCH_UNCERTAIN;
	if (obj_is_ec(obj))
		okqi->okqi_flags	|= ORF_EC;
	uuid_copy(okqi->okqi_pool_uuid, pool->dp_pool);
	uuid_copy(okqi->okqi_co_hdl, coh_uuid);
	uuid_copy(okqi->okqi_co_uuid, cont_uuid);
	daos_dti_copy(&okqi->okqi_dti, dti);

	rc = daos_rpc_send(req, task);
	return rc;

out_req:
	crt_req_decref(req);
	crt_req_decref(req);
out:
	tse_task_complete(task, rc);
	return rc;
}

struct obj_shard_sync_cb_args {
	crt_rpc_t	*rpc;
	daos_epoch_t	*epoch;
	uint32_t	*map_ver;
};

static int
obj_shard_sync_cb(tse_task_t *task, void *data)
{
	struct obj_shard_sync_cb_args	*cb_args;
	struct obj_sync_out		*oso;
	int				 ret = task->dt_result;
	int				 rc = 0;
	crt_rpc_t			*rpc;

	cb_args = (struct obj_shard_sync_cb_args *)data;
	rpc = cb_args->rpc;

	if (ret != 0) {
		D_ERROR("OBJ_SYNC RPC failed: rc = %d\n", ret);
		D_GOTO(out, rc = ret);
	}

	oso = crt_reply_get(rpc);
	rc = oso->oso_ret;
	if (rc == -DER_NONEXIST)
		D_GOTO(out, rc = 0);

	if (rc == -DER_INPROGRESS || rc == -DER_TX_BUSY) {
		D_DEBUG(DB_TRACE,
			"rpc %p OBJ_SYNC_RPC may need retry: rc = "DF_RC"\n",
			rpc, DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (rc != 0) {
		D_ERROR("rpc %p OBJ_SYNC_RPC failed: rc = "DF_RC"\n", rpc,
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	*cb_args->epoch = oso->oso_epoch;
	*cb_args->map_ver = oso->oso_map_version;

	D_DEBUG(DB_IO, "OBJ_SYNC_RPC reply: eph "DF_U64", version %u.\n",
		oso->oso_epoch, oso->oso_map_version);

out:
	crt_req_decref(rpc);
	return rc;
}

int
dc_obj_shard_sync(struct dc_obj_shard *shard, enum obj_rpc_opc opc,
		  void *shard_args, struct daos_shard_tgt *fw_shard_tgts,
		  uint32_t fw_cnt, tse_task_t *task)
{
	struct shard_sync_args		*args = shard_args;
	struct dc_pool			*pool = NULL;
	uuid_t				 cont_hdl_uuid;
	uuid_t				 cont_uuid;
	struct obj_sync_in		*osi;
	crt_rpc_t			*req;
	struct obj_shard_sync_cb_args	 cb_args;
	crt_endpoint_t			 tgt_ep;
	int				 rc;

	pool = obj_shard_ptr2pool(shard);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	rc = dc_cont2uuid(shard->do_co, &cont_hdl_uuid, &cont_uuid);
	if (rc != 0)
		D_GOTO(out, rc);

	tgt_ep.ep_grp	= pool->dp_sys->sy_group;
	tgt_ep.ep_tag	= shard->do_target_idx;
	tgt_ep.ep_rank	= shard->do_target_rank;
	if ((int)tgt_ep.ep_rank < 0)
		D_GOTO(out, rc = (int)tgt_ep.ep_rank);

	D_DEBUG(DB_IO, "OBJ_SYNC_RPC, rank=%d tag=%d.\n",
		tgt_ep.ep_rank, tgt_ep.ep_tag);

	rc = obj_req_create(daos_task2ctx(task), &tgt_ep, opc, &req);
	if (rc != 0)
		D_GOTO(out, rc);

	crt_req_addref(req);
	cb_args.rpc	= req;
	cb_args.epoch	= args->sa_epoch;
	cb_args.map_ver = &args->sa_auxi.map_ver;

	rc = tse_task_register_comp_cb(task, obj_shard_sync_cb, &cb_args,
				       sizeof(cb_args));
	if (rc != 0)
		D_GOTO(out_req, rc);

	osi = crt_req_get(req);
	D_ASSERT(osi != NULL);

	uuid_copy(osi->osi_co_hdl, cont_hdl_uuid);
	uuid_copy(osi->osi_pool_uuid, pool->dp_pool);
	uuid_copy(osi->osi_co_uuid, cont_uuid);
	osi->osi_oid		= shard->do_id;
	osi->osi_epoch		= args->sa_auxi.epoch.oe_value;
	osi->osi_map_ver	= args->sa_auxi.map_ver;

	return daos_rpc_send(req, task);

out_req:
	crt_req_decref(req);
	crt_req_decref(req);

out:
	tse_task_complete(task, rc);
	return rc;
}

struct obj_k2a_args {
	crt_rpc_t		*rpc;
	daos_handle_t		*hdlp;
	struct dc_obj_shard	*eaa_obj;
	unsigned int		*eaa_map_ver;
	struct dtx_epoch	*epoch;
	daos_handle_t		*th;
	daos_anchor_t		*anchor;
	uint32_t		shard;
};

static int
dc_k2a_cb(tse_task_t *task, void *arg)
{
	struct obj_k2a_args		*k2a_args = (struct obj_k2a_args *)arg;
	struct obj_key2anchor_in	*oki;
	struct obj_key2anchor_out	*oko;
	int				ret = task->dt_result;
	int				rc = 0;

	oki = crt_req_get(k2a_args->rpc);
	D_ASSERT(oki != NULL);
	if (ret != 0) {
		D_ERROR("RPC %d failed: "DF_RC"\n", DAOS_OBJ_RPC_KEY2ANCHOR, DP_RC(ret));
		D_GOTO(out, ret);
	}

	oko = crt_reply_get(k2a_args->rpc);

	rc = obj_reply_get_status(k2a_args->rpc);

	/* See the similar dc_rw_cb. */
	if (daos_handle_is_valid(*k2a_args->th)) {
		int rc_tmp;

		rc_tmp = dc_tx_op_end(task, *k2a_args->th, k2a_args->epoch,
				      rc, oko->oko_epoch);
		if (rc_tmp != 0) {
			D_ERROR("failed to end transaction operation (rc=%d "
				"epoch="DF_U64": "DF_RC"\n", rc,
				oko->oko_epoch, DP_RC(rc_tmp));
			goto out;
		}
	}

	if (rc != 0) {
		if (rc == -DER_INPROGRESS || rc == -DER_TX_BUSY) {
			D_DEBUG(DB_TRACE, "rpc %p RPC %d may need retry: "DF_RC"\n",
				k2a_args->rpc, DAOS_OBJ_RPC_KEY2ANCHOR, DP_RC(rc));
		} else if (rc == -DER_TX_RESTART) {
			D_DEBUG(DB_TRACE, "rpc %p RPC %d may need restart: "DF_RC"\n",
				k2a_args->rpc, DAOS_OBJ_RPC_KEY2ANCHOR, DP_RC(rc));
		} else {
			D_ERROR("rpc %p RPC %d failed: "DF_RC"\n", k2a_args->rpc,
				DAOS_OBJ_RPC_KEY2ANCHOR, DP_RC(rc));
		}
		D_GOTO(out, rc);
	}

	*k2a_args->eaa_map_ver = obj_reply_map_version_get(k2a_args->rpc);
	enum_anchor_copy(k2a_args->anchor, &oko->oko_anchor);
	dc_obj_shard2anchor(k2a_args->anchor, k2a_args->shard);
out:
	if (k2a_args->eaa_obj != NULL)
		obj_shard_decref(k2a_args->eaa_obj);
	crt_req_decref(k2a_args->rpc);
	if (ret == 0 || obj_retry_error(rc))
		ret = rc;
	return ret;
}

int
dc_obj_shard_key2anchor(struct dc_obj_shard *obj_shard, enum obj_rpc_opc opc,
			void *shard_args, struct daos_shard_tgt *fw_shard_tgts,
			uint32_t fw_cnt, tse_task_t *task)
{
	struct shard_k2a_args		*args = shard_args;
	daos_obj_key2anchor_t		*obj_args =
		dc_task_get_args(args->ka_auxi.obj_auxi->obj_task);
	struct dc_pool			*pool = NULL;
	crt_endpoint_t			tgt_ep;
	crt_rpc_t			*req;
	uuid_t				cont_hdl_uuid;
	uuid_t				cont_uuid;
	struct obj_key2anchor_in	*oki;
	struct obj_k2a_args		cb_args;
	int				rc;

	D_ASSERT(obj_shard != NULL);
	obj_shard_addref(obj_shard);

	pool = obj_shard_ptr2pool(obj_shard);
	if (pool == NULL)
		D_GOTO(out_put, rc = -DER_NO_HDL);

	rc = dc_cont2uuid(obj_shard->do_co, &cont_hdl_uuid, &cont_uuid);
	if (rc != 0)
		D_GOTO(out_put, rc);

	tgt_ep.ep_grp = pool->dp_sys->sy_group;
	tgt_ep.ep_tag = obj_shard->do_target_idx;
	tgt_ep.ep_rank = obj_shard->do_target_rank;
	if ((int)tgt_ep.ep_rank < 0)
		D_GOTO(out_put, rc = (int)tgt_ep.ep_rank);

	D_DEBUG(DB_IO, "opc %d "DF_UOID" rank %d tag %d\n",
		opc, DP_UOID(obj_shard->do_id), tgt_ep.ep_rank, tgt_ep.ep_tag);

	rc = obj_req_create(daos_task2ctx(task), &tgt_ep, opc, &req);
	if (rc != 0)
		D_GOTO(out_put, rc);

	oki = crt_req_get(req);
	D_ASSERT(oki != NULL);

	oki->oki_dkey = *obj_args->dkey;
	if (obj_args->akey != NULL)
		oki->oki_akey = *obj_args->akey;
	oki->oki_oid		= obj_shard->do_id;
	oki->oki_map_ver	= args->ka_auxi.map_ver;
	uuid_copy(oki->oki_pool_uuid, pool->dp_pool);
	uuid_copy(oki->oki_co_hdl, cont_hdl_uuid);
	uuid_copy(oki->oki_co_uuid, cont_uuid);
	daos_dti_copy(&oki->oki_dti, &args->ka_dti);
	if (args->ka_auxi.epoch.oe_flags & DTX_EPOCH_UNCERTAIN)
		oki->oki_flags |= ORF_EPOCH_UNCERTAIN;

	crt_req_addref(req);
	cb_args.rpc = req;
	cb_args.hdlp = (daos_handle_t *)pool;
	cb_args.eaa_obj = obj_shard;
	cb_args.eaa_map_ver = &args->ka_auxi.map_ver;
	cb_args.epoch = &args->ka_auxi.epoch;
	cb_args.th = &obj_args->th;
	cb_args.anchor = args->ka_anchor;
	cb_args.shard = obj_shard->do_shard_idx;
	rc = tse_task_register_comp_cb(task, dc_k2a_cb, &cb_args, sizeof(cb_args));
	if (rc != 0)
		D_GOTO(out_eaa, rc);

	return daos_rpc_send(req, task);

out_eaa:
	crt_req_decref(req);
	crt_req_decref(req);
out_put:
	obj_shard_decref(obj_shard);
	tse_task_complete(task, rc);
	return rc;
}
