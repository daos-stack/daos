/**
 * (C) Copyright 2023-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dtx: DTX collective RPC logic
 */
#define D_LOGFAC	DD_FAC(dtx)

#include <stdlib.h>
#include <daos/placement.h>
#include <daos/pool_map.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/container.h>
#include <daos_srv/vos.h>
#include <daos_srv/dtx_srv.h>
#include "dtx_internal.h"

/*
 * For collective DTX, when commit/abort/check the DTX on system XS (on non-leader), we cannot
 * directly locate the DTX entry since no VOS target is attached to system XS. Under such case,
 * we have two options:
 *
 * 1. The DTX leader (on IO XS) knows on which VOS target the non-leader can find out the DTX
 *    entry. So DTX leader can send related information (IO XS index) to the non-leader.
 *
 * 2. The non-leader can start ULT on every local XS collectively to find the DTX by force in
 *    spite of whether related DTX entry really exists on the VOS target or not.
 *
 * Usually, the 2nd option may cause more overhead, should be avoid. Then the 1st is relative
 * better choice. On the other hand, if there are a lot of VOS targets in the system, then it
 * maybe inefficient to send all VOS targets information to all related non-leaders via bcast.
 * Instead, we will only send one VOS target information for each non-leader, then non-leader
 * can load mbs (dtx_memberships) from the DTX entry and then calculate the other VOS targets
 * information by itself.
 */

struct dtx_coll_local_args {
	uuid_t			 dcla_po_uuid;
	uuid_t			 dcla_co_uuid;
	struct dtx_id		 dcla_xid;
	daos_epoch_t		 dcla_epoch;
	uint32_t		 dcla_opc;
	int			*dcla_results;
};

void
dtx_coll_prep_ult(void *arg)
{
	struct dtx_coll_prep_args	*dcpa = arg;
	struct dtx_coll_in		*dci = crt_req_get(dcpa->dcpa_rpc);
	struct dtx_memberships		*mbs = NULL;
	struct ds_cont_child		*cont = NULL;
	uint32_t			 opc = opc_get(dcpa->dcpa_rpc->cr_opc);
	int				 rc = 0;

	dcpa->dcpa_result = ds_cont_child_lookup(dci->dci_po_uuid, dci->dci_co_uuid, &cont);
	if (dcpa->dcpa_result != 0) {
		D_ERROR("Failed to locate pool="DF_UUID" cont="DF_UUID" for DTX "
			DF_DTI" with opc %u: "DF_RC"\n",
			DP_UUID(dci->dci_po_uuid), DP_UUID(dci->dci_co_uuid),
			DP_DTI(&dci->dci_xid), opc, DP_RC(dcpa->dcpa_result));
		/*
		 * Convert the case of container non-exist as -DER_IO to distinguish
		 * the case of DTX entry does not exist. The latter one is normal.
		 */
		if (dcpa->dcpa_result == -DER_NONEXIST)
			dcpa->dcpa_result = -DER_IO;

		goto out;
	}

	dcpa->dcpa_result = vos_dtx_load_mbs(cont->sc_hdl, &dci->dci_xid, &dcpa->dcpa_oid, &mbs);
	if (dcpa->dcpa_result == -DER_INPROGRESS && !dtx_cont_opened(cont) &&
	    opc == DTX_COLL_CHECK) {
		rc = start_dtx_reindex_ult(cont);
		if (rc != 0)
			D_ERROR(DF_UUID": Failed to trigger DTX reindex: "DF_RC"\n",
				DP_UUID(cont->sc_uuid), DP_RC(rc));
	}

	if (dcpa->dcpa_result != 0)
		goto out;

	dcpa->dcpa_result = dtx_coll_prep(dci->dci_po_uuid, dcpa->dcpa_oid, &dci->dci_xid, mbs, -1,
					  dci->dci_version, cont->sc_pool->spc_map_version,
					  opc == DTX_COLL_CHECK, false, &dcpa->dcpa_dce);
	if (dcpa->dcpa_result != 0)
		D_ERROR("Failed to prepare the bitmap (and hints) for collective DTX "
			DF_DTI" opc %u: "DF_RC"\n", DP_DTI(&dci->dci_xid), opc,
			DP_RC(dcpa->dcpa_result));

out:
	if (cont != NULL)
		ds_cont_child_put(cont);

	rc = ABT_future_set(dcpa->dcpa_future, NULL);
	D_ASSERT(rc == ABT_SUCCESS);
}

int
dtx_coll_prep(uuid_t po_uuid, daos_unit_oid_t oid, struct dtx_id *xid, struct dtx_memberships *mbs,
	      uint32_t my_tgtid, uint32_t dtx_ver, uint32_t pm_ver, bool for_check, bool need_hint,
	      struct dtx_coll_entry **p_dce)
{
	struct pl_map		*map = NULL;
	struct pl_obj_layout	*layout = NULL;
	struct pool_target	*target;
	struct dtx_daos_target	*ddt;
	struct dtx_coll_target	*dct;
	struct dtx_coll_entry	*dce = NULL;
	struct daos_obj_md	 md = { 0 };
	uint32_t		 node_nr;
	d_rank_t		 my_rank = dss_self_rank();
	d_rank_t		 max_rank = 0;
	int			 rc = 0;
	int			 i;
	int			 j;

	D_ASSERT(mbs->dm_flags & DMF_COLL_TARGET);

	D_ALLOC_PTR(dce);
	if (dce == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dce->dce_xid = *xid;
	dce->dce_ver = dtx_ver;
	dce->dce_refs = 1;

	ddt = &mbs->dm_tgts[0];
	dct = (struct dtx_coll_target *)(ddt + mbs->dm_tgt_cnt);
	D_ALLOC(dce->dce_bitmap, dct->dct_bitmap_sz);
	if (dce->dce_bitmap == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dce->dce_bitmap_sz = dct->dct_bitmap_sz;

	if (!for_check) {
		memcpy(dce->dce_bitmap, dct->dct_tgts + dct->dct_tgt_nr, dct->dct_bitmap_sz);
	} else {
		map = pl_map_find(po_uuid, oid.id_pub);
		if (map == NULL) {
			D_ERROR("Failed to find valid placement map in pool "DF_UUID"\n",
				DP_UUID(po_uuid));
			D_GOTO(out, rc = -DER_INVAL);
		}

		for (i = 0, j = 0; i < dct->dct_tgt_nr; i++) {
			rc = pool_map_find_target(map->pl_poolmap, dct->dct_tgts[i], &target);
			D_ASSERT(rc == 1);

			/* Skip the targets that reside on other engines. */
			if (unlikely(target->ta_comp.co_rank != my_rank))
				continue;

			/* Skip the target that (re-)joined the system after the DTX. */
			if (target->ta_comp.co_ver > dtx_ver)
				continue;

			/* Skip non-healthy one. */
			if (target->ta_comp.co_status != PO_COMP_ST_UP &&
			    target->ta_comp.co_status != PO_COMP_ST_UPIN &&
			    target->ta_comp.co_status != PO_COMP_ST_DRAIN)
				continue;

			/* Skip current (new) leader target. */
			if (my_tgtid != target->ta_comp.co_index) {
				setbit(dce->dce_bitmap, target->ta_comp.co_index);
				j++;
			}
		}

		rc = 0;

		if (unlikely(j == 0)) {
			D_FREE(dce->dce_bitmap);
			dce->dce_bitmap_sz = 0;
		}
	}

	if (!need_hint)
		goto out;

	if (map == NULL) {
		map = pl_map_find(po_uuid, oid.id_pub);
		if (map == NULL) {
			D_ERROR("Failed to find valid placement map in pool "DF_UUID"\n",
				DP_UUID(po_uuid));
			D_GOTO(out, rc = -DER_INVAL);
		}
	}

	node_nr = pool_map_node_nr(map->pl_poolmap);
	if (unlikely(node_nr == 1))
		D_GOTO(out, rc = 0);

	dce->dce_ranks = d_rank_list_alloc(node_nr - 1);
	if (dce->dce_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC_ARRAY(dce->dce_hints, node_nr);
	if (dce->dce_hints == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < node_nr; i++)
		dce->dce_hints[i] = (uint8_t)(-1);

	md.omd_id = oid.id_pub;
	md.omd_ver = pm_ver;
	md.omd_fdom_lvl = dct->dct_fdom_lvl;
	md.omd_pda = dct->dct_pda;
	md.omd_pdom_lvl = dct->dct_pdom_lvl;

	rc = pl_obj_place(map, oid.id_layout_ver, &md, DAOS_OO_RW, NULL, &layout);
	if (rc != 0) {
		D_ERROR("Failed to load object layout for "DF_OID" in pool "DF_UUID"\n",
			DP_OID(oid.id_pub), DP_UUID(po_uuid));
		goto out;
	}

	for (i = 0, j = 0; i < layout->ol_nr && j < node_nr - 1; i++) {
		if (layout->ol_shards[i].po_target == -1 || layout->ol_shards[i].po_shard == -1)
			continue;

		rc = pool_map_find_target(map->pl_poolmap, layout->ol_shards[i].po_target, &target);
		D_ASSERT(rc == 1);

		/* Skip current leader rank. */
		if (target->ta_comp.co_rank == my_rank)
			continue;

		/* Skip the target that (re-)joined the system after the DTX. */
		if (target->ta_comp.co_ver > dtx_ver)
			continue;

		/* Skip non-healthy one. */
		if (target->ta_comp.co_status != PO_COMP_ST_UP &&
		    target->ta_comp.co_status != PO_COMP_ST_UPIN &&
		    target->ta_comp.co_status != PO_COMP_ST_DRAIN)
			continue;

		if (dce->dce_hints[target->ta_comp.co_rank] == (uint8_t)(-1)) {
			dce->dce_hints[target->ta_comp.co_rank] = target->ta_comp.co_index;
			dce->dce_ranks->rl_ranks[j++] = target->ta_comp.co_rank;
			if (max_rank < target->ta_comp.co_rank)
				max_rank = target->ta_comp.co_rank;
		}
	}

	rc = 0;

	/*
	 * It is no matter that the real size of rl_ranks array is larger than rl_nr.
	 * Then reduce rl_nr to skip those non-defined ranks at the tail in rl_ranks.
	 */
	if (unlikely(j == 0)) {
		d_rank_list_free(dce->dce_ranks);
		dce->dce_ranks = NULL;
		D_FREE(dce->dce_hints);
		dce->dce_hint_sz = 0;
	} else {
		dce->dce_ranks->rl_nr = j;
		dce->dce_hint_sz = max_rank + 1;
	}

out:
	if (layout != NULL)
		pl_obj_layout_free(layout);

	if (map != NULL)
		pl_map_decref(map);

	if (rc != 0)
		dtx_coll_entry_put(dce);
	else
		*p_dce = dce;

	return rc;
}

static int
dtx_coll_local_one(void *args)
{
	struct dss_module_info		*dmi = dss_get_module_info();
	struct dtx_coll_local_args	*dcla = args;
	struct ds_cont_child		*cont = NULL;
	uint32_t			 opc = dcla->dcla_opc;
	int				 rc;
	int				 rc1;

	rc = ds_cont_child_lookup(dcla->dcla_po_uuid, dcla->dcla_co_uuid, &cont);
	if (rc != 0) {
		D_ERROR("Failed to locate "DF_UUID"/"DF_UUID" for collective DTX "
			DF_DTI" rpc %u: "DF_RC"\n", DP_UUID(dcla->dcla_po_uuid),
			DP_UUID(dcla->dcla_co_uuid), DP_DTI(&dcla->dcla_xid), opc, DP_RC(rc));
		goto out;
	}

	switch (opc) {
	case DTX_COLL_COMMIT:
		rc = vos_dtx_commit(cont->sc_hdl, &dcla->dcla_xid, 1, NULL);
		break;
	case DTX_COLL_ABORT:
		rc = vos_dtx_abort(cont->sc_hdl, &dcla->dcla_xid, dcla->dcla_epoch);
		break;
	case DTX_COLL_CHECK:
		rc = vos_dtx_check(cont->sc_hdl, &dcla->dcla_xid, NULL, NULL, NULL, false);
		if (rc == DTX_ST_INITED) {
			/*
			 * For DTX_CHECK, non-ready one is equal to non-exist. Do not directly
			 * return 'DTX_ST_INITED' to avoid interoperability trouble if related
			 * request is from old server.
			 */
			rc = -DER_NONEXIST;
		} else if (rc == -DER_INPROGRESS && !dtx_cont_opened(cont)) {
			/* Trigger DTX re-index for subsequent (retry) DTX_CHECK. */
			rc1 = start_dtx_reindex_ult(cont);
			if (rc1 != 0)
				D_ERROR("Failed to trigger DTX reindex for "DF_UUID"/"DF_UUID
					" on target %u/%u: "DF_RC"\n",
					DP_UUID(dcla->dcla_po_uuid), DP_UUID(dcla->dcla_co_uuid),
					dss_self_rank(), dmi->dmi_tgt_id, DP_RC(rc1));
		}
		break;
	default:
		D_ASSERTF(0, "Unknown collective DTX opc %u\n", opc);
		D_GOTO(out, rc = -DER_NOTSUPPORTED);
	}

out:
	dcla->dcla_results[dmi->dmi_tgt_id] = rc;
	if (cont != NULL)
		ds_cont_child_put(cont);

	return 0;
}

int
dtx_coll_local_exec(uuid_t po_uuid, uuid_t co_uuid, struct dtx_id *xid, daos_epoch_t epoch,
		    uint32_t opc, uint32_t bitmap_sz, uint8_t *bitmap, int **p_results)
{
	struct dtx_coll_local_args	 dcla = { 0 };
	struct dss_coll_ops		 coll_ops = { 0 };
	struct dss_coll_args		 coll_args = { 0 };
	int				 rc;

	D_ALLOC_ARRAY(dcla.dcla_results, dss_tgt_nr);
	if (dcla.dcla_results == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	uuid_copy(dcla.dcla_po_uuid, po_uuid);
	uuid_copy(dcla.dcla_co_uuid, co_uuid);
	dcla.dcla_xid = *xid;
	dcla.dcla_epoch = epoch;
	dcla.dcla_opc = opc;

	coll_ops.co_func = dtx_coll_local_one;
	coll_args.ca_func_args = &dcla;
	coll_args.ca_tgt_bitmap_sz = bitmap_sz;
	coll_args.ca_tgt_bitmap = bitmap;

	rc = dss_thread_collective_reduce(&coll_ops, &coll_args, DSS_USE_CURRENT_ULT);
	D_CDEBUG(rc < 0, DLOG_ERR, DB_TRACE,
		 "Locally exec collective DTX PRC %u for "DF_DTI": "DF_RC"\n",
		 opc, DP_DTI(xid), DP_RC(rc));

out:
	*p_results = dcla.dcla_results;
	return rc < 0 ? rc : dss_tgt_nr;
}
