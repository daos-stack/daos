/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos two-phase commit transaction.
 *
 * vos/vos_dtx_iter.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include "vos_layout.h"
#include "vos_internal.h"

/** Iterator for active-DTX table. */
struct vos_dtx_iter {
	/** embedded VOS common iterator */
	struct vos_iterator	 oit_iter;
	/** Handle of iterator */
	daos_handle_t		 oit_hdl;
	/** Reference to the container */
	struct vos_container	*oit_cont;
};

static struct vos_dtx_iter *
iter2oiter(struct vos_iterator *iter)
{
	return container_of(iter, struct vos_dtx_iter, oit_iter);
}

static int
dtx_iter_fini(struct vos_iterator *iter)
{
	struct vos_dtx_iter	*oiter = iter2oiter(iter);
	int			 rc = 0;

	D_ASSERT(iter->it_type == VOS_ITER_DTX);

	if (daos_handle_is_valid(oiter->oit_hdl)) {
		rc = dbtree_iter_finish(oiter->oit_hdl);
		if (rc != 0)
			D_ERROR("oid_iter_fini failed: rc = "DF_RC"\n",
				DP_RC(rc));
	}

	if (oiter->oit_cont != NULL)
		vos_cont_decref(oiter->oit_cont);

	D_FREE_PTR(oiter);
	return rc;
}

static int
dtx_iter_prep(vos_iter_type_t type, vos_iter_param_t *param,
	      struct vos_iterator **iter_pp, struct vos_ts_set *ts_set)
{
	struct vos_dtx_iter	*oiter;
	struct vos_container	*cont;
	int			 rc;

	if (type != VOS_ITER_DTX) {
		D_ERROR("Expected Type: %d, got %d\n", VOS_ITER_DTX, type);
		return -DER_INVAL;
	}

	cont = vos_hdl2cont(param->ip_hdl);
	if (cont == NULL)
		return -DER_INVAL;

	D_ALLOC_PTR(oiter);
	if (oiter == NULL)
		return -DER_NOMEM;

	oiter->oit_iter.it_type = type;
	oiter->oit_cont = cont;
	vos_cont_addref(cont);

	rc = dbtree_iter_prepare(cont->vc_dtx_active_hdl, 0, &oiter->oit_hdl);
	if (rc != 0) {
		D_ERROR("Failed to prepare DTX iteration: rc = "DF_RC"\n",
			DP_RC(rc));
		dtx_iter_fini(&oiter->oit_iter);
	} else {
		*iter_pp = &oiter->oit_iter;
	}

	return rc;
}

static int
dtx_iter_probe(struct vos_iterator *iter, daos_anchor_t *anchor)
{
	struct vos_dtx_iter	*oiter = iter2oiter(iter);
	dbtree_probe_opc_t	 opc;

	D_ASSERT(iter->it_type == VOS_ITER_DTX);

	opc = anchor == NULL ? BTR_PROBE_FIRST : BTR_PROBE_GE;
	return dbtree_iter_probe(oiter->oit_hdl, opc, vos_iter_intent(iter),
				 NULL, anchor);
}

static int
dtx_iter_next(struct vos_iterator *iter)
{
	struct vos_dtx_iter	*oiter = iter2oiter(iter);
	struct vos_dtx_act_ent	*dae;
	d_iov_t			 rec_iov;
	int			 rc = 0;

	D_ASSERT(iter->it_type == VOS_ITER_DTX);

	while (1) {
		rc = dbtree_iter_next(oiter->oit_hdl);
		if (rc != 0)
			break;

		d_iov_set(&rec_iov, NULL, 0);
		rc = dbtree_iter_fetch(oiter->oit_hdl, NULL, &rec_iov, NULL);
		if (rc != 0)
			break;

		D_ASSERT(rec_iov.iov_len == sizeof(struct vos_dtx_act_ent));
		dae = (struct vos_dtx_act_ent *)rec_iov.iov_buf;

		/* Only return prepared ones. */
		if (!dae->dae_committable && !dae->dae_committed &&
		    !dae->dae_aborted && dae->dae_dbd != NULL)
			break;
	}

	return rc;
}

static int
dtx_iter_fetch(struct vos_iterator *iter, vos_iter_entry_t *it_entry,
	       daos_anchor_t *anchor)
{
	struct vos_dtx_iter	*oiter = iter2oiter(iter);
	struct vos_dtx_act_ent	*dae;
	d_iov_t			 rec_iov;
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_DTX);

	d_iov_set(&rec_iov, NULL, 0);
	rc = dbtree_iter_fetch(oiter->oit_hdl, NULL, &rec_iov, anchor);
	if (rc != 0) {
		D_ERROR("Error while fetching DTX info: rc = "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	D_ASSERT(rec_iov.iov_len == sizeof(struct vos_dtx_act_ent));
	dae = (struct vos_dtx_act_ent *)rec_iov.iov_buf;

	it_entry->ie_epoch = DAE_EPOCH(dae);
	it_entry->ie_dtx_xid = DAE_XID(dae);
	it_entry->ie_dtx_oid = DAE_OID(dae);
	it_entry->ie_dtx_ver = DAE_VER(dae);
	it_entry->ie_dtx_flags = DAE_FLAGS(dae);
	it_entry->ie_dtx_mbs_flags = DAE_MBS_FLAGS(dae);
	it_entry->ie_dtx_tgt_cnt = DAE_TGT_CNT(dae);
	it_entry->ie_dtx_grp_cnt = DAE_GRP_CNT(dae);
	it_entry->ie_dtx_mbs_dsize = DAE_MBS_DSIZE(dae);
	if (DAE_MBS_DSIZE(dae) <= sizeof(DAE_MBS_INLINE(dae)))
		it_entry->ie_dtx_mbs = DAE_MBS_INLINE(dae);
	else
		it_entry->ie_dtx_mbs = umem_off2ptr(
					&oiter->oit_cont->vc_pool->vp_umm,
					DAE_MBS_OFF(dae));

	D_DEBUG(DB_IO, "DTX iterator fetch the one "DF_DTI"\n",
		DP_DTI(&DAE_XID(dae)));

	return 0;
}

static int
dtx_iter_delete(struct vos_iterator *iter, void *args)
{
	struct vos_dtx_iter	*oiter = iter2oiter(iter);
	struct umem_instance	*umm;
	int			 rc;

	D_ASSERT(iter->it_type == VOS_ITER_DTX);

	umm = &oiter->oit_cont->vc_pool->vp_umm;
	rc = umem_tx_begin(umm, NULL);
	if (rc != 0)
		return rc;

	rc = dbtree_iter_delete(oiter->oit_hdl, args);
	if (rc != 0) {
		umem_tx_abort(umm, rc);
		D_ERROR("Failed to delete DTX entry: rc = "DF_RC"\n",
			DP_RC(rc));
	} else {
		umem_tx_commit(umm);
	}

	return rc;
}

struct vos_iter_ops vos_dtx_iter_ops = {
	.iop_prepare =	dtx_iter_prep,
	.iop_finish  =  dtx_iter_fini,
	.iop_probe   =	dtx_iter_probe,
	.iop_next    =  dtx_iter_next,
	.iop_fetch   =  dtx_iter_fetch,
	.iop_delete  =	dtx_iter_delete,
};
