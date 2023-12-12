/**
 * (C) Copyright 2023 Intel Corporation.
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
 * 1. The DTX leader (on IO XS) knows on which VOS target the non-leader can find out the DTX,
 *    so DTX leader can send related information (IO XS index) to the non-leader.
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
	struct dtx_coll_in		*dci = dcpa->dcpa_params;
	struct dtx_memberships		*mbs = NULL;
	struct ds_cont_child		*cont = NULL;
	int				 rc = 0;

	dcpa->dcpa_result = ds_cont_child_lookup(dci->dci_po_uuid, dci->dci_co_uuid, &cont);
	if (dcpa->dcpa_result != 0) {
		D_ERROR("Failed to locate pool="DF_UUID" cont="DF_UUID" for DTX "
			DF_DTI" with opc %u: "DF_RC"\n",
			DP_UUID(dci->dci_po_uuid), DP_UUID(dci->dci_co_uuid),
			DP_DTI(&dci->dci_xid), dcpa->dcpa_opc, DP_RC(dcpa->dcpa_result));
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
	    dcpa->dcpa_opc == DTX_COLL_CHECK) {
		rc = start_dtx_reindex_ult(cont);
		if (rc != 0)
			D_ERROR(DF_UUID": Failed to trigger DTX reindex: "DF_RC"\n",
				DP_UUID(cont->sc_uuid), DP_RC(rc));
	}

	ds_cont_child_put(cont);
	if (dcpa->dcpa_result != 0)
		goto out;

	dcpa->dcpa_result = dtx_coll_prep(dci->dci_po_uuid, dcpa->dcpa_oid, &dci->dci_xid, mbs, -1,
					  dci->dci_version, false, &dcpa->dcpa_dce);
	if (dcpa->dcpa_result != 0)
		D_ERROR("Failed to prepare the bitmap (and hints) for collective DTX "
			DF_DTI" opc %u: "DF_RC"\n", DP_DTI(&dci->dci_xid),
			dcpa->dcpa_opc, DP_RC(dcpa->dcpa_result));

out:
	rc = ABT_future_set(dcpa->dcpa_future, NULL);
	D_ASSERT(rc == ABT_SUCCESS);
}

static int
dtx_coll_dtg_cmp(const void *m1, const void *m2)
{
	const struct dtx_target_group	*dtg1 = m1;
	const struct dtx_target_group	*dtg2 = m2;

	if (dtg1->dtg_rank > dtg2->dtg_rank)
		return 1;

	if (dtg1->dtg_rank < dtg2->dtg_rank)
		return -1;

	return 0;
}

int
dtx_coll_prep(uuid_t po_uuid, daos_unit_oid_t oid, struct dtx_id *xid, struct dtx_memberships *mbs,
	      uint32_t my_tgtid, uint32_t version, bool need_hint, struct dtx_coll_entry **p_dce)
{
	struct pl_map		*map = NULL;
	struct pool_target	*target;
	struct dtx_daos_target	*ddt;
	struct dtx_coll_entry	*dce = NULL;
	struct dtx_target_group	*base;
	struct dtx_target_group	*dtg = NULL;
	struct dtx_target_group	 key = { 0 };
	uint32_t		 node_nr;
	d_rank_t		 my_rank = dss_self_rank();
	d_rank_t		 max_rank;
	int			 count;
	int			 rc = 0;
	int			 i;
	int			 j;
	int			 k;

	D_ASSERT(mbs->dm_flags & DMF_CONTAIN_TARGET_GRP);

	D_ALLOC_PTR(dce);
	if (dce == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dce->dce_xid = *xid;
	dce->dce_ver = version;
	dce->dce_refs = 1;

	ddt = &mbs->dm_tgts[0];
	base = (struct dtx_target_group *)(ddt + mbs->dm_tgt_cnt);
	count = (mbs->dm_data_size - sizeof(*ddt) * mbs->dm_tgt_cnt) / sizeof(*dtg);

	/*
	 * The first dtg is for the original leader group. The others groups are sorted against
	 * ranks ID.
	 */

	if (base->dtg_rank == my_rank) {
		dtg = base;
	} else {
		key.dtg_rank = my_rank;
		dtg = bsearch(&key, base + 1, count - 1, sizeof(*dtg), dtx_coll_dtg_cmp);
		if (dtg == NULL) {
			D_ERROR("Cannot locate rank %u in the mbs\n", my_rank);
			D_GOTO(out, rc = -DER_IO);
		}
	}

	dce->dce_bitmap_sz = ((dss_tgt_nr - 1) >> 3) + 1;
	D_ALLOC_ARRAY(dce->dce_bitmap, dce->dce_bitmap_sz);
	if (dce->dce_bitmap == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	map = pl_map_find(po_uuid, oid.id_pub);
	if (map == NULL) {
		D_ERROR("Failed to find valid placement map for "DF_OID"\n", DP_OID(oid.id_pub));
		D_GOTO(out, rc = -DER_INVAL);
	}

	for (i = dtg->dtg_start_idx, j = 0; i < dtg->dtg_start_idx + dtg->dtg_tgt_nr; i++) {
		rc = pool_map_find_target(map->pl_poolmap, ddt[i].ddt_id, &target);
		D_ASSERT(rc == 1);

		/* Skip the targets that reside on other engines. */
		if (unlikely(my_rank != target->ta_comp.co_rank))
			continue;

		/* Skip the target that (re-)joined the system after the DTX. */
		if (target->ta_comp.co_ver > version)
			continue;

		/* Skip non-healthy one. */
		if (target->ta_comp.co_status != PO_COMP_ST_UP &&
		    target->ta_comp.co_status != PO_COMP_ST_UPIN &&
		    target->ta_comp.co_status != PO_COMP_ST_NEW &&
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

	if (!need_hint || unlikely(count == 1))
		goto out;

	dce->dce_ranks = d_rank_list_alloc(count - 1);
	if (dce->dce_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	node_nr = pool_map_node_nr(map->pl_poolmap);
	D_ALLOC_ARRAY(dce->dce_hints, node_nr);
	if (dce->dce_hints == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0, j = 0, max_rank = 0, dtg = base; i < count; i++, dtg++) {
		/* Skip current leader rank. */
		if (my_rank == dtg->dtg_rank)
			continue;

		for (k = dtg->dtg_start_idx; k < dtg->dtg_start_idx + dtg->dtg_tgt_nr; k++) {
			rc = pool_map_find_target(map->pl_poolmap, ddt[k].ddt_id, &target);
			D_ASSERT(rc == 1);

			if ((target->ta_comp.co_ver <= version) &&
			    (target->ta_comp.co_status == PO_COMP_ST_UP ||
			     target->ta_comp.co_status == PO_COMP_ST_UPIN ||
			     target->ta_comp.co_status == PO_COMP_ST_NEW ||
			     target->ta_comp.co_status == PO_COMP_ST_DRAIN)) {
				dce->dce_ranks->rl_ranks[j++] = dtg->dtg_rank;
				dce->dce_hints[dtg->dtg_rank] = target->ta_comp.co_index;
				if (max_rank < dtg->dtg_rank)
					max_rank = dtg->dtg_rank;
				break;
			}
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
	} else {
		dce->dce_ranks->rl_nr = j;
		dce->dce_hint_sz = max_rank + 1;
	}

out:
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
		rc = vos_dtx_check(cont->sc_hdl, &dcla->dcla_xid, NULL, NULL, NULL, NULL, false);
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

	rc = dss_thread_collective_reduce(&coll_ops, &coll_args, 0);
	D_CDEBUG(rc < 0, DLOG_ERR, DB_TRACE,
		 "Locally exec collective DTX PRC %u for "DF_DTI": "DF_RC"\n",
		 opc, DP_DTI(xid), DP_RC(rc));

out:
	*p_results = dcla.dcla_results;
	return rc < 0 ? rc : dss_tgt_nr;
}
