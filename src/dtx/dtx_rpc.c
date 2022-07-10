/**
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dtx: DTX RPC
 */
#define D_LOGFAC	DD_FAC(dtx)

#include <abt.h>
#include <daos/rpc.h>
#include <daos/btree.h>
#include <daos/pool_map.h>
#include <daos/btree_class.h>
#include <daos_srv/vos.h>
#include <daos_srv/dtx_srv.h>
#include <daos_srv/container.h>
#include <daos_srv/daos_engine.h>
#include "dtx_internal.h"

CRT_RPC_DEFINE(dtx, DAOS_ISEQ_DTX, DAOS_OSEQ_DTX);

#define X(a, b, c, d, e, f)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
},

static struct crt_proto_rpc_format dtx_proto_rpc_fmt[] = {
	DTX_PROTO_SRV_RPC_LIST
};

#undef X

struct crt_proto_format dtx_proto_fmt = {
	.cpf_name  = "dtx-proto",
	.cpf_ver   = DAOS_DTX_VERSION,
	.cpf_count = ARRAY_SIZE(dtx_proto_rpc_fmt),
	.cpf_prf   = dtx_proto_rpc_fmt,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_DTX_MODULE, 0)
};

/* Top level DTX RPC args */
struct dtx_req_args {
	ABT_future			 dra_future;
	/* The RPC code */
	crt_opcode_t			 dra_opc;
	/* pool UUID */
	uuid_t				 dra_po_uuid;
	/* container UUID */
	uuid_t				 dra_co_uuid;
	/* The count of sub requests. */
	int				 dra_length;
	/* The collective RPC result. */
	int				 dra_result;
	/* Pointer to the container, used for DTX_REFRESH case. */
	struct ds_cont_child		*dra_cont;
	/* Pointer to the committed DTX list, used for DTX_REFRESH case. */
	d_list_t			*dra_cmt_list;
	/* Pointer to the aborted DTX list, used for DTX_REFRESH case. */
	d_list_t			*dra_abt_list;
	/* Pointer to the active DTX list, used for DTX_REFRESH case. */
	d_list_t			*dra_act_list;
	/* The committed DTX entries on all related participants, for DTX_COMMIT. */
	int				*dra_committed;
};

/* The record for the DTX classify-tree in DRAM.
 * Each dtx_req_rec contains one RPC (to related rank/tag) args.
 */
struct dtx_req_rec {
	/* All the records are linked into one global list,
	 * used for travelling the classify-tree efficiently.
	 */
	d_list_t			 drr_link;
	struct dtx_req_args		*drr_parent; /* The top level args */
	d_rank_t			 drr_rank; /* The server ID */
	uint32_t			 drr_tag; /* The VOS ID */
	int				 drr_count; /* DTX count */
	int				 drr_result; /* The RPC result */
	uint32_t			 drr_comp:1;
	struct dtx_id			*drr_dti; /* The DTX array */
	struct dtx_share_peer		**drr_cb_args; /* Used by dtx_req_cb. */
};

struct dtx_cf_rec_bundle {
	union {
		struct {
			d_rank_t	 dcrb_rank;
			uint32_t	 dcrb_tag;
		};
		uint64_t		 dcrb_key;
	};
	/* Pointer to the global list head for all the dtx_req_rec. */
	d_list_t			*dcrb_head;
	/* Pointer to the length of above global list. */
	int				*dcrb_length;
	/* Current DTX to be classified. */
	struct dtx_id			*dcrb_dti;
	/* The number of DTXs to be classified that will be used as
	 * the dtx_req_rec::drr_dti array size when allocating it.
	 */
	int				 dcrb_count;
};

/* Make sure that the "dcrb_key" is consisted of "dcrb_rank" + "dcrb_tag". */
D_CASSERT(sizeof(((struct dtx_cf_rec_bundle *)0)->dcrb_rank) +
	  sizeof(((struct dtx_cf_rec_bundle *)0)->dcrb_tag) ==
	  sizeof(((struct dtx_cf_rec_bundle *)0)->dcrb_key));

uint32_t dtx_rpc_helper_thd;

static void
dtx_req_cb(const struct crt_cb_info *cb_info)
{
	crt_rpc_t		*req = cb_info->cci_rpc;
	struct dtx_req_rec	*drr = cb_info->cci_arg;
	struct dtx_req_args	*dra = drr->drr_parent;
	struct dtx_in		*din = crt_req_get(req);
	struct dtx_out		*dout;
	int			 rc = cb_info->cci_rc;
	int			 i;

	D_ASSERT(drr->drr_comp == 0);

	if (rc != 0)
		goto out;

	dout = crt_reply_get(req);
	if (dra->dra_opc == DTX_COMMIT) {
		(*dra->dra_committed) += dout->do_misc;
		D_GOTO(out, rc = dout->do_status);
	}

	if (dout->do_status != 0 || dra->dra_opc != DTX_REFRESH)
		D_GOTO(out, rc = dout->do_status);

	if (din->di_dtx_array.ca_count != dout->do_sub_rets.ca_count)
		D_GOTO(out, rc = -DER_PROTO);

	for (i = 0; i < dout->do_sub_rets.ca_count; i++) {
		struct dtx_share_peer	*dsp;
		int			*ret;
		int			 rc1;

		dsp = drr->drr_cb_args[i];
		if (dsp == NULL)
			continue;

		drr->drr_cb_args[i] = NULL;
		ret = (int *)dout->do_sub_rets.ca_arrays + i;

		switch (*ret) {
		case DTX_ST_PREPARED:
			/* Not committable yet. */
			if (dra->dra_act_list != NULL)
				d_list_add_tail(&dsp->dsp_link,
						dra->dra_act_list);
			else
				D_FREE(dsp);
			break;
		case DTX_ST_COMMITTABLE:
			/* Committable, will be committed soon. */
			if (dra->dra_cmt_list != NULL)
				d_list_add_tail(&dsp->dsp_link,
						dra->dra_cmt_list);
			else
				D_FREE(dsp);
			break;
		case DTX_ST_COMMITTED:
			/* Has been committed on leader, we may miss related
			 * commit request, so let's commit it locally.
			 */
			rc1 = vos_dtx_commit(dra->dra_cont->sc_hdl,
					     &dsp->dsp_xid, 1, NULL);
			if (rc1 < 0 && rc1 != -DER_NONEXIST &&
			    dra->dra_cmt_list != NULL)
				d_list_add_tail(&dsp->dsp_link,
						dra->dra_cmt_list);
			else
				D_FREE(dsp);
			break;
		case DTX_ST_CORRUPTED:
			/* The DTX entry is corrupted. */
			D_FREE(dsp);
			D_GOTO(out, rc = -DER_DATA_LOSS);
		case -DER_TX_UNCERTAIN:
			/* Related DTX entry on leader does not exist. We do not know whether it has
			 * been aborted or committed (then removed by DTX aggregation). Then mark it
			 * as 'orphan' that will be handled via some special DAOS tools in future.
			 */
			rc1 = vos_dtx_set_flags(dra->dra_cont->sc_hdl, &dsp->dsp_xid, DTE_ORPHAN);
			D_ERROR("Hit uncertain leaked DTX "DF_DTI", mark it as orphan: "DF_RC"\n",
				DP_DTI(&dsp->dsp_xid), DP_RC(rc1));
			D_FREE(dsp);

			if (rc1 == -DER_NONEXIST)
				break;

			D_GOTO(out, rc = -DER_TX_UNCERTAIN);
		case -DER_NONEXIST:
			/* The leader does not have related DTX info, we may miss related DTX abort
			 * request, let's abort it locally.
			 */
			rc1 = vos_dtx_abort(dra->dra_cont->sc_hdl, &dsp->dsp_xid, dsp->dsp_epoch);
			if (rc1 < 0 && rc1 != -DER_NONEXIST && dra->dra_abt_list != NULL)
				d_list_add_tail(&dsp->dsp_link, dra->dra_abt_list);
			else
				D_FREE(dsp);
			break;
		default:
			D_FREE(dsp);
			D_GOTO(out, rc = *ret);
		}
	}

out:
	drr->drr_comp = 1;
	drr->drr_result = rc;
	rc = ABT_future_set(dra->dra_future, drr);
	D_ASSERTF(rc == ABT_SUCCESS,
		  "ABT_future_set failed for opc %x to %d/%d: rc = %d.\n",
		  dra->dra_opc, drr->drr_rank, drr->drr_tag, rc);

	D_DEBUG(DB_TRACE,
		"DTX req for opc %x (req %p future %p) got reply from %d/%d: "
		"epoch :"DF_X64", rc %d.\n", dra->dra_opc, req,
		dra->dra_future, drr->drr_rank, drr->drr_tag,
		din != NULL ? din->di_epoch : 0, drr->drr_result);
}

static int
dtx_req_send(struct dtx_req_rec *drr, daos_epoch_t epoch)
{
	struct dtx_req_args	*dra = drr->drr_parent;
	crt_rpc_t		*req;
	crt_endpoint_t		 tgt_ep;
	crt_opcode_t		 opc;
	struct dtx_in		*din = NULL;
	int			 rc;

	tgt_ep.ep_grp = NULL;
	tgt_ep.ep_rank = drr->drr_rank;
	tgt_ep.ep_tag = daos_rpc_tag(DAOS_REQ_TGT, drr->drr_tag);
	opc = DAOS_RPC_OPCODE(dra->dra_opc, DAOS_DTX_MODULE, DAOS_DTX_VERSION);

	rc = crt_req_create(dss_get_module_info()->dmi_ctx, &tgt_ep, opc, &req);
	if (rc == 0) {
		din = crt_req_get(req);
		uuid_copy(din->di_po_uuid, dra->dra_po_uuid);
		uuid_copy(din->di_co_uuid, dra->dra_co_uuid);
		din->di_epoch = epoch;
		din->di_dtx_array.ca_count = drr->drr_count;
		din->di_dtx_array.ca_arrays = drr->drr_dti;

		if (dra->dra_opc == DTX_REFRESH && DAOS_FAIL_CHECK(DAOS_DTX_RESYNC_DELAY))
			crt_req_set_timeout(req, 3);

		rc = crt_req_send(req, dtx_req_cb, drr);
	}

	D_DEBUG(DB_TRACE, "DTX req for opc %x to %d/%d (req %p future %p) sent "
		"epoch "DF_X64" : rc %d.\n", dra->dra_opc, drr->drr_rank,
		drr->drr_tag, req, dra->dra_future,
		din != NULL ? din->di_epoch : 0, rc);

	if (rc != 0 && drr->drr_comp == 0) {
		drr->drr_comp = 1;
		drr->drr_result = rc;
		ABT_future_set(dra->dra_future, drr);
	}

	return rc;
}

static void
dtx_req_list_cb(void **args)
{
	struct dtx_req_rec	*drr = args[0];
	struct dtx_req_args	*dra = drr->drr_parent;
	int			 i;

	if (dra->dra_opc == DTX_CHECK) {
		for (i = 0; i < dra->dra_length; i++) {
			drr = args[i];
			switch (drr->drr_result) {
			case DTX_ST_COMMITTED:
			case DTX_ST_COMMITTABLE:
				dra->dra_result = DTX_ST_COMMITTED;
				/* As long as one target has committed the DTX,
				 * then the DTX is committable on all targets.
				 */
				D_DEBUG(DB_TRACE,
					"The DTX "DF_DTI" has been committed "
					"on %d/%d.\n", DP_DTI(drr->drr_dti),
					drr->drr_rank, drr->drr_tag);
				return;
			case -DER_EXCLUDED:
				/* If non-leader is excluded, handle it
				 * as 'prepared'. If other non-leaders
				 * also 'prepared' then related DTX is
				 * committable. Fall through.
				 */
			case DTX_ST_PREPARED:
				if (dra->dra_result == 0 ||
				    dra->dra_result == DTX_ST_CORRUPTED)
					dra->dra_result = drr->drr_result;
				break;
			case DTX_ST_CORRUPTED:
				if (dra->dra_result == 0)
					dra->dra_result = drr->drr_result;
				break;
			default:
				dra->dra_result = drr->drr_result >= 0 ?
					-DER_IO : drr->drr_result;
				break;
			}

			D_DEBUG(DB_TRACE, "The DTX "DF_DTI" RPC req result %d, "
				"status is %d.\n", DP_DTI(drr->drr_dti),
				drr->drr_result, dra->dra_result);
		}
	} else {
		for (i = 0; i < dra->dra_length; i++) {
			drr = args[i];
			if (dra->dra_result == 0 || dra->dra_result == -DER_NONEXIST)
				dra->dra_result = drr->drr_result;
		}

		drr = args[0];
		D_CDEBUG(dra->dra_result < 0 &&
			 dra->dra_result != -DER_NONEXIST, DLOG_ERR, DB_TRACE,
			 "DTX req for opc %x ("DF_DTI") %s, count %d: %d.\n",
			 dra->dra_opc, DP_DTI(drr->drr_dti),
			 dra->dra_result < 0 ? "failed" : "succeed",
			 dra->dra_length, dra->dra_result);
	}
}

static int
dtx_req_wait(struct dtx_req_args *dra)
{
	int	rc;

	rc = ABT_future_wait(dra->dra_future);
	D_ASSERTF(rc == ABT_SUCCESS,
		  "ABT_future_wait failed for opc %x, length = %d: rc = %d.\n",
		  dra->dra_opc, dra->dra_length, rc);

	D_DEBUG(DB_TRACE, "DTX req for opc %x, future %p done, rc = %d\n",
		dra->dra_opc, dra->dra_future, rc);

	ABT_future_free(&dra->dra_future);
	return dra->dra_result;
}

static int
dtx_req_list_send(struct dtx_req_args *dra, crt_opcode_t opc, int *committed, d_list_t *head,
		  int len, uuid_t po_uuid, uuid_t co_uuid, daos_epoch_t epoch,
		  struct ds_cont_child *cont, d_list_t *cmt_list,
		  d_list_t *abt_list, d_list_t *act_list)
{
	ABT_future		 future;
	struct dtx_req_rec	*drr;
	int			 rc;
	int			 i = 0;

	dra->dra_opc = opc;
	uuid_copy(dra->dra_po_uuid, po_uuid);
	uuid_copy(dra->dra_co_uuid, co_uuid);
	dra->dra_length = len;
	dra->dra_result = 0;
	dra->dra_cont = cont;
	dra->dra_cmt_list = cmt_list;
	dra->dra_abt_list = abt_list;
	dra->dra_act_list = act_list;
	dra->dra_committed = committed;

	rc = ABT_future_create(len, dtx_req_list_cb, &future);
	if (rc != ABT_SUCCESS) {
		D_ERROR("ABT_future_create failed for opc %x, len = %d: "
			"rc = %d.\n", opc, len, rc);
		return dss_abterr2der(rc);
	}

	D_DEBUG(DB_TRACE, "DTX req for opc %x, future %p start.\n",
		opc, future);
	dra->dra_future = future;
	d_list_for_each_entry(drr, head, drr_link) {
		drr->drr_parent = dra;
		drr->drr_result = 0;
		rc = dtx_req_send(drr, epoch);
		if (rc != 0) {
			/* If the first sub-RPC failed, then break, otherwise
			 * other remote replicas may have already received the
			 * RPC and executed it, so have to go ahead.
			 */
			if (i == 0) {
				ABT_future_free(&dra->dra_future);
				dra->dra_future = ABT_FUTURE_NULL;
				return rc;
			}
		}

		/* Yield to avoid holding CPU for too long time. */
		if (++i >= DTX_RPC_YIELD_THD)
			ABT_thread_yield();
	}

	return 0;
}

static int
dtx_cf_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov,
		 d_iov_t *val_iov, struct btr_record *rec, d_iov_t *val_out)
{
	struct dtx_req_rec		*drr;
	struct dtx_cf_rec_bundle	*dcrb;

	D_ASSERT(tins->ti_umm.umm_id == UMEM_CLASS_VMEM);

	D_ALLOC_PTR(drr);
	if (drr == NULL)
		return -DER_NOMEM;

	dcrb = val_iov->iov_buf;
	D_ALLOC_ARRAY(drr->drr_dti, dcrb->dcrb_count);
	if (drr->drr_dti == NULL) {
		D_FREE(drr);
		return -DER_NOMEM;
	}

	drr->drr_rank = dcrb->dcrb_rank;
	drr->drr_tag = dcrb->dcrb_tag;
	drr->drr_count = 1;
	drr->drr_comp = 0;
	drr->drr_dti[0] = *dcrb->dcrb_dti;
	d_list_add_tail(&drr->drr_link, dcrb->dcrb_head);
	++(*dcrb->dcrb_length);

	rec->rec_off = umem_ptr2off(&tins->ti_umm, drr);
	return 0;
}

static int
dtx_cf_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct dtx_req_rec	*drr;

	D_ASSERT(tins->ti_umm.umm_id == UMEM_CLASS_VMEM);

	drr = (struct dtx_req_rec *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
	d_list_del(&drr->drr_link);
	D_FREE(drr->drr_cb_args);
	D_FREE(drr->drr_dti);
	D_FREE(drr);

	return 0;
}

static int
dtx_cf_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
		 d_iov_t *key_iov, d_iov_t *val_iov)
{
	D_ASSERTF(0, "We should not come here.\n");
	return 0;
}

static int
dtx_cf_rec_update(struct btr_instance *tins, struct btr_record *rec,
		  d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	struct dtx_req_rec		*drr;
	struct dtx_cf_rec_bundle	*dcrb;

	drr = (struct dtx_req_rec *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
	dcrb = (struct dtx_cf_rec_bundle *)val->iov_buf;
	D_ASSERT(drr->drr_count >= 1);

	if (!daos_dti_equal(&drr->drr_dti[drr->drr_count - 1],
			    dcrb->dcrb_dti)) {
		D_ASSERT(drr->drr_count < dcrb->dcrb_count);

		drr->drr_dti[drr->drr_count++] = *dcrb->dcrb_dti;
	}

	return 0;
}

btr_ops_t dbtree_dtx_cf_ops = {
	.to_rec_alloc	= dtx_cf_rec_alloc,
	.to_rec_free	= dtx_cf_rec_free,
	.to_rec_fetch	= dtx_cf_rec_fetch,
	.to_rec_update	= dtx_cf_rec_update,
};

#define DTX_CF_BTREE_ORDER	20

static int
dtx_classify_one(struct ds_pool *pool, daos_handle_t tree, d_list_t *head,
		 int *length, struct dtx_entry *dte, int count, d_rank_t my_rank, uint32_t my_tgtid)
{
	struct dtx_memberships		*mbs = dte->dte_mbs;
	struct dtx_cf_rec_bundle	 dcrb;
	int				 rc = 0;
	int				 i;

	if (mbs->dm_tgt_cnt == 0)
		return -DER_INVAL;

	if (daos_handle_is_valid(tree)) {
		dcrb.dcrb_count = count;
		dcrb.dcrb_dti = &dte->dte_xid;
		dcrb.dcrb_head = head;
		dcrb.dcrb_length = length;
	}

	if (mbs->dm_flags & DMF_CONTAIN_LEADER)
		/* mbs->dm_tgts[0] is the (current/old) leader, skip it. */
		i = 1;
	else
		i = 0;
	for (; i < mbs->dm_tgt_cnt && rc >= 0; i++) {
		struct pool_target	*target;

		rc = pool_map_find_target(pool->sp_map,
					  mbs->dm_tgts[i].ddt_id, &target);
		if (rc != 1) {
			D_WARN("Cannot find target %u at %d/%d, flags %x\n",
			       mbs->dm_tgts[i].ddt_id, i, mbs->dm_tgt_cnt,
			       mbs->dm_flags);
			return -DER_UNINIT;
		}

		/* Skip the target that (re-)joined the system after the DTX. */
		if (target->ta_comp.co_ver > dte->dte_ver)
			continue;

		/* Skip non-healthy one. */
		if (target->ta_comp.co_status != PO_COMP_ST_UP &&
		    target->ta_comp.co_status != PO_COMP_ST_UPIN)
			continue;

		/* Skip myself. */
		if (my_rank == target->ta_comp.co_rank && my_tgtid == target->ta_comp.co_index)
			continue;

		if (daos_handle_is_valid(tree)) {
			d_iov_t			 kiov;
			d_iov_t			 riov;

			dcrb.dcrb_rank = target->ta_comp.co_rank;
			dcrb.dcrb_tag = target->ta_comp.co_index;

			d_iov_set(&riov, &dcrb, sizeof(dcrb));
			d_iov_set(&kiov, &dcrb.dcrb_key, sizeof(dcrb.dcrb_key));
			rc = dbtree_upsert(tree, BTR_PROBE_EQ, DAOS_INTENT_UPDATE, &kiov,
					   &riov, NULL);
		} else {
			struct dtx_req_rec	*drr;

			D_ALLOC_PTR(drr);
			if (drr == NULL)
				return -DER_NOMEM;

			drr->drr_rank = target->ta_comp.co_rank;
			drr->drr_tag = target->ta_comp.co_index;
			drr->drr_count = 1;
			drr->drr_dti = &dte->dte_xid;
			d_list_add_tail(&drr->drr_link, head);
			(*length)++;
		}
	}

	return rc > 0 ? 0 : rc;
}

static int
dtx_rpc_internal(struct ds_cont_child *cont, d_list_t *head, struct btr_root *tree_root,
		 daos_handle_t *tree_hdl, struct dtx_req_args *dra, struct dtx_id dtis[],
		 struct dtx_entry **dtes, daos_epoch_t epoch, int count, int opc,
		 int *committed, d_rank_t my_rank, uint32_t my_tgtid)
{
	struct ds_pool		*pool;
	int			 length = 0;
	int			 rc;
	int			 i;

	D_ASSERT(cont->sc_pool != NULL);
	pool = cont->sc_pool->spc_pool;
	D_ASSERT(pool != NULL);

	if (count > 1) {
		struct umem_attr	uma = { 0 };

		uma.uma_id = UMEM_CLASS_VMEM;
		rc = dbtree_create_inplace(DBTREE_CLASS_DTX_CF, 0, DTX_CF_BTREE_ORDER,
					   &uma, tree_root, tree_hdl);
		if (rc != 0)
			return rc;
	}

	ABT_rwlock_rdlock(pool->sp_lock);
	for (i = 0; i < count; i++) {
		rc = dtx_classify_one(pool, *tree_hdl, head, &length, dtes[i], count,
				      my_rank, my_tgtid);
		if (rc < 0) {
			ABT_rwlock_unlock(pool->sp_lock);
			return rc;
		}

		if (dtis != NULL)
			dtis[i] = dtes[i]->dte_xid;
	}
	ABT_rwlock_unlock(pool->sp_lock);

	/* For DTX_CHECK, if no other available target(s), then current target is the
	 * unique valid one (and also 'prepared'), then related DTX can be committed.
	 */
	if (d_list_empty(head))
		return opc == DTX_CHECK ? DTX_ST_PREPARED : 0;

	D_ASSERT(length > 0);

	return dtx_req_list_send(dra, opc, committed, head, length, pool->sp_uuid,
				 cont->sc_uuid, epoch, NULL, NULL, NULL, NULL);
}

struct dtx_helper_args {
	struct ds_cont_child	 *dha_cont;
	d_list_t		 *dha_head;
	struct btr_root		 *dha_tree_root;
	daos_handle_t		 *dha_tree_hdl;
	struct dtx_req_args	 *dha_dra;
	ABT_thread		 *dha_ult;
	struct dtx_entry	**dha_dtes;
	daos_epoch_t		  dha_epoch;
	int			  dha_count;
	int			  dha_opc;
	int			 *dha_committed;
	d_rank_t		  dha_rank;
	uint32_t		  dha_tgtid;
};

static void
dtx_rpc_helper(void *arg)
{
	struct dtx_helper_args	*dha = arg;

	dtx_rpc_internal(dha->dha_cont, dha->dha_head, dha->dha_tree_root, dha->dha_tree_hdl,
			 dha->dha_dra, NULL, dha->dha_dtes, dha->dha_epoch, dha->dha_count,
			 dha->dha_opc, dha->dha_committed, dha->dha_rank, dha->dha_tgtid);
	D_FREE(dha);
}

static int
dtx_rpc_prep(struct ds_cont_child *cont, d_list_t *head, struct btr_root *tree_root,
	     daos_handle_t *tree_hdl, struct dtx_req_args *dra, ABT_thread *helper,
	     struct dtx_id dtis[], struct dtx_entry **dtes, daos_epoch_t epoch,
	     uint32_t count, int opc, int *committed)
{
	d_rank_t	my_rank;
	uint32_t	my_tgtid;
	int		rc;

	D_INIT_LIST_HEAD(head);
	dra->dra_future = ABT_FUTURE_NULL;
	crt_group_rank(NULL, &my_rank);
	my_tgtid = dss_get_module_info()->dmi_tgt_id;

	/* Use helper ULT to handle DTX RPC if there are enough helper XS. */
	if (dss_has_enough_helper() &&
	    (dtes[0]->dte_mbs->dm_tgt_cnt - 1) * count >= dtx_rpc_helper_thd) {
		struct dtx_helper_args	*dha = NULL;

		D_ALLOC_PTR(dha);
		if (dha == NULL)
			return -DER_NOMEM;

		dha->dha_cont = cont;
		dha->dha_head = head;
		dha->dha_tree_root = tree_root;
		dha->dha_tree_hdl = tree_hdl;
		dha->dha_dra = dra;
		dha->dha_ult = helper;
		dha->dha_dtes = dtes;
		dha->dha_epoch = epoch;
		dha->dha_count = count;
		dha->dha_opc = opc;
		dha->dha_committed = committed;
		dha->dha_rank = my_rank;
		dha->dha_tgtid = my_tgtid;

		rc = dss_ult_create(dtx_rpc_helper, dha, DSS_XS_IOFW,
				    my_tgtid, DSS_DEEP_STACK_SZ, helper);
		if (rc != 0) {
			D_FREE(dha);
		} else if (dtis != NULL) {
			int	i;

			for (i = 0; i < count; i++)
				dtis[i] = dtes[i]->dte_xid;
		}
	} else {
		rc = dtx_rpc_internal(cont, head, tree_root, tree_hdl, dra, dtis, dtes, epoch,
				      count, opc, committed, my_rank, my_tgtid);
	}

	return rc;
}

static int
dtx_rpc_post(d_list_t *head, daos_handle_t tree_hdl, struct dtx_req_args *dra,
	     ABT_thread *helper, int ret)
{
	struct dtx_req_rec	*drr;
	int			 rc = 0;
	bool			 free_dti = false;

	if (*helper != ABT_THREAD_NULL)
		ABT_thread_free(helper);

	if (dra->dra_future != ABT_FUTURE_NULL)
		rc = dtx_req_wait(dra);

	if (daos_handle_is_valid(tree_hdl)) {
		dbtree_destroy(tree_hdl, NULL);
		free_dti = true;
	}

	while ((drr = d_list_pop_entry(head, struct dtx_req_rec, drr_link)) != NULL) {
		if (free_dti)
			D_FREE(drr->drr_dti);
		D_FREE(drr);
	}

	return ret != 0 ? ret : rc;
}

/**
 * Commit the given DTX array globally.
 *
 * For each DTX in the given array, classify its shards. It is quite possible
 * that the shards for different DTXs reside on the same server (rank + tag),
 * then they can be sent to remote server via single DTX_COMMIT RPC and then
 * be committed by remote server via single PMDK transaction.
 *
 * After the DTX classification, send DTX_COMMIT RPC to related servers, and
 * then call DTX commit locally. For a DTX, it is possible that some targets
 * have committed successfully, but others failed. That is no matter. As long
 * as one target has committed, then the DTX logic can re-sync those failed
 * targets when dtx_resync() is triggered next time.
 */
int
dtx_commit(struct ds_cont_child *cont, struct dtx_entry **dtes,
	   struct dtx_cos_key *dcks, int count, daos_epoch_t epoch)
{
	d_list_t		 head;
	struct btr_root		 tree_root = { 0 };
	daos_handle_t		 tree_hdl = DAOS_HDL_INVAL;
	struct dtx_req_args	 dra;
	ABT_thread		 helper = ABT_THREAD_NULL;
	struct dtx_id		*dtis = NULL;
	bool			*rm_cos = NULL;
	struct dtx_id		 dti = { 0 };
	bool			 cos = false;
	int			 committed = 0;
	int			 rc;
	int			 rc1 = 0;
	int			 rc2 = 0;
	int			 i;

	if (count > 1) {
		D_ALLOC_ARRAY(dtis, count);
		if (dtis == NULL)
			D_GOTO(log, rc = -DER_NOMEM);
	} else {
		dtis = &dti;
	}

	rc = dtx_rpc_prep(cont, &head, &tree_root, &tree_hdl, &dra, &helper, dtis,
			  dtes, 0, count, DTX_COMMIT, &committed);
	if (rc < 0)
		goto out;

	if (dcks != NULL) {
		if (count > 1) {
			D_ALLOC_ARRAY(rm_cos, count);
			if (rm_cos == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		} else {
			rm_cos = &cos;
		}
	}

	rc1 = vos_dtx_commit(cont->sc_hdl, dtis, count, rm_cos);
	if (rc1 >= 0 && rm_cos != NULL) {
		for (i = 0; i < count; i++) {
			if (rm_cos[i]) {
				D_ASSERT(!daos_oid_is_null(dcks[i].oid.id_pub));
				dtx_del_cos(cont, &dtis[i], &dcks[i].oid, dcks[i].dkey_hash);
			}
		}
	}

	/* -DER_NONEXIST may be caused by race or repeated commit, ignore it. */
	if (rc1 > 0) {
		committed += rc1;
		rc1 = 0;
	} else if (rc1 == -DER_NONEXIST) {
		rc1 = 0;
	}

out:
	rc2 = dtx_rpc_post(&head, tree_hdl, &dra, &helper, rc);
	if (rc2 > 0 || rc2 == -DER_NONEXIST)
		rc2 = 0;

	if (dtis != &dti)
		D_FREE(dtis);

	if (rm_cos != &cos)
		D_FREE(rm_cos);

log:
	if (rc != 0 || rc1 != 0 || rc2 != 0) {
		D_ERROR("Failed to commit DTXs "DF_DTI", count %d: rc %d %d %d\n",
			DP_DTI(&dtes[0]->dte_xid), count, rc, rc1, rc2);

		if (epoch != 0 && committed == 0) {
			D_ASSERT(count == 1);

			dtx_abort(cont, dtes[0], epoch);
		}
	} else {
		D_DEBUG(DB_IO, "Commit DTXs " DF_DTI", count %d\n",
			DP_DTI(&dtes[0]->dte_xid), count);
	}

	return rc != 0 ? rc : (rc1 != 0 ? rc1 : rc2);
}


int
dtx_abort(struct ds_cont_child *cont, struct dtx_entry *dte, daos_epoch_t epoch)
{
	d_list_t		head;
	struct btr_root		tree_root = { 0 };
	daos_handle_t		tree_hdl = DAOS_HDL_INVAL;
	struct dtx_req_args	dra;
	ABT_thread		helper = ABT_THREAD_NULL;
	int			rc;
	int			rc1;
	int			rc2;

	rc = dtx_rpc_prep(cont, &head, &tree_root, &tree_hdl, &dra, &helper, NULL,
			  &dte, epoch, 1, DTX_ABORT, NULL);

	if (epoch != 0)
		rc1 = vos_dtx_abort(cont->sc_hdl, &dte->dte_xid, epoch);
	else
		rc1 = vos_dtx_set_flags(cont->sc_hdl, &dte->dte_xid, DTE_CORRUPTED);
	if (rc1 > 0 || rc1 == -DER_NONEXIST)
		rc1 = 0;

	rc2 = dtx_rpc_post(&head, tree_hdl, &dra, &helper, rc);
	if (rc2 > 0 || rc2 == -DER_NONEXIST)
		rc2 = 0;

	D_CDEBUG(rc1 != 0 || rc2 != 0, DLOG_ERR, DB_IO, "Abort DTX "DF_DTI": rc %d %d %d\n",
		 DP_DTI(&dte->dte_xid), rc, rc1, rc2);

	return rc1 != 0 ? rc1 : rc2;
}

int
dtx_check(struct ds_cont_child *cont, struct dtx_entry *dte, daos_epoch_t epoch)
{
	d_list_t		head;
	struct btr_root		tree_root = { 0 };
	daos_handle_t		tree_hdl = DAOS_HDL_INVAL;
	struct dtx_req_args	dra;
	ABT_thread		helper = ABT_THREAD_NULL;
	int			rc;
	int			rc1;

	/* If no other target, then current target is the unique
	 * one and 'prepared', then related DTX can be committed.
	 */
	if (dte->dte_mbs->dm_tgt_cnt == 1)
		return DTX_ST_PREPARED;

	rc = dtx_rpc_prep(cont, &head, &tree_root, &tree_hdl, &dra, &helper, NULL,
			  &dte, epoch, 1, DTX_CHECK, NULL);

	rc1 = dtx_rpc_post(&head, tree_hdl, &dra, &helper, rc);

	D_CDEBUG(rc1 < 0, DLOG_ERR, DB_IO, "Check DTX "DF_DTI": rc %d %d\n",
		 DP_DTI(&dte->dte_xid), rc, rc1);

	return rc1;
}

int
dtx_refresh_internal(struct ds_cont_child *cont, int *check_count,
		     d_list_t *check_list, d_list_t *cmt_list,
		     d_list_t *abt_list, d_list_t *act_list, bool failout)
{
	struct ds_pool		*pool = cont->sc_pool->spc_pool;
	struct dtx_share_peer	*dsp;
	struct dtx_share_peer	*tmp;
	struct dtx_req_rec	*drr;
	struct dtx_req_args	 dra;
	d_list_t		 head;
	d_list_t		 self;
	d_rank_t		 myrank;
	int			 len = 0;
	int			 rc = 0;

	D_INIT_LIST_HEAD(&head);
	D_INIT_LIST_HEAD(&self);
	crt_group_rank(NULL, &myrank);

	d_list_for_each_entry_safe(dsp, tmp, check_list, dsp_link) {
		struct pool_target *target;
		int		count = 0;
		bool		drop = false;
again:
		rc = dtx_leader_get(pool, &dsp->dsp_mbs, &target);
		if (rc < 0) {
			/**
			 * Currently, for EC object, if parity node is
			 * in rebuilding, we will get -DER_STALE, that
			 * is not fatal, the caller or related request
			 * sponsor can retry sometime later.
			 */
			D_WARN("Failed to find DTX leader for "DF_DTI", ver %d: "DF_RC"\n",
			       DP_DTI(&dsp->dsp_xid), pool->sp_map_version, DP_RC(rc));
			if (failout)
				goto out;

			drop = true;
			goto next;
		}

		/* If current server is the leader, then two possible cases:
		 *
		 * 1. In DTX resync, the status may be resolved sometime later.
		 * 2. The DTX resync is done, but failed to handle related DTX.
		 */
		if (myrank == target->ta_comp.co_rank &&
		    dss_get_module_info()->dmi_tgt_id ==
		    target->ta_comp.co_index) {
			d_list_del(&dsp->dsp_link);
			d_list_add_tail(&dsp->dsp_link, &self);
			if (--(*check_count) == 0)
				break;
			continue;
		}

		/* Usually, we will not elect in-rebuilding server as DTX
		 * leader. But we may be blocked by the ABT_rwlock_rdlock,
		 * then pool map may be refreshed during that. Let's retry
		 * to find out the new leader.
		 */
		if (target->ta_comp.co_status != PO_COMP_ST_UP &&
		    target->ta_comp.co_status != PO_COMP_ST_UPIN) {
			if (unlikely(++count % 10 == 3))
				D_WARN("Get stale DTX leader %u/%u (st: %x) for "DF_DTI
				       " %d times, maybe dead loop\n",
				       target->ta_comp.co_rank, target->ta_comp.co_id,
				       target->ta_comp.co_status, DP_DTI(&dsp->dsp_xid), count);

			goto again;
		}

		d_list_for_each_entry(drr, &head, drr_link) {
			if (drr->drr_rank == target->ta_comp.co_rank &&
			    drr->drr_tag == target->ta_comp.co_index) {
				drr->drr_dti[drr->drr_count] = dsp->dsp_xid;
				drr->drr_cb_args[drr->drr_count++] = dsp;
				goto next;
			}
		}

		D_ALLOC_PTR(drr);
		if (drr == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		D_ALLOC_ARRAY(drr->drr_dti, *check_count);
		if (drr->drr_dti == NULL) {
			D_FREE(drr);
			D_GOTO(out, rc = -DER_NOMEM);
		}

		D_ALLOC_ARRAY(drr->drr_cb_args, *check_count);
		if (drr->drr_cb_args == NULL) {
			D_FREE(drr->drr_dti);
			D_FREE(drr);
			D_GOTO(out, rc = -DER_NOMEM);
		}

		drr->drr_rank = target->ta_comp.co_rank;
		drr->drr_tag = target->ta_comp.co_index;
		drr->drr_count = 1;
		drr->drr_dti[0] = dsp->dsp_xid;
		drr->drr_cb_args[0] = dsp;
		d_list_add_tail(&drr->drr_link, &head);
		len++;

next:
		d_list_del_init(&dsp->dsp_link);
		if (drop)
			D_FREE(dsp);
		if (--(*check_count) == 0)
			break;
	}

	if (len > 0) {
		rc = dtx_req_list_send(&dra, DTX_REFRESH, NULL, &head, len,
				       pool->sp_uuid, cont->sc_uuid, 0, cont,
				       cmt_list, abt_list, act_list);
		if (rc == 0)
			rc = dtx_req_wait(&dra);

		if (rc != 0)
			goto out;
	}

	/* Handle the entries whose leaders are on current server. */
	d_list_for_each_entry_safe(dsp, tmp, &self, dsp_link) {
		struct dtx_entry	dte;

		d_list_del(&dsp->dsp_link);

		dte.dte_xid = dsp->dsp_xid;
		dte.dte_ver = pool->sp_map_version;
		dte.dte_refs = 1;
		dte.dte_mbs = &dsp->dsp_mbs;

		rc = dtx_status_handle_one(cont, &dte, dsp->dsp_epoch,
					   NULL, NULL);
		switch (rc) {
		case DSHR_NEED_COMMIT: {
			struct dtx_entry	*pdte = &dte;
			struct dtx_cos_key	 dck;

			dck.oid = dsp->dsp_oid;
			dck.dkey_hash = dsp->dsp_dkey_hash;
			rc = dtx_commit(cont, &pdte, &dck, 1, 0);
			if (rc < 0 && rc != -DER_NONEXIST && cmt_list != NULL)
				d_list_add_tail(&dsp->dsp_link, cmt_list);
			else
				D_FREE(dsp);
			continue;
		}
		case DSHR_NEED_RETRY:
			D_FREE(dsp);
			if (failout)
				D_GOTO(out, rc = -DER_INPROGRESS);
			continue;
		case DSHR_IGNORE:
			D_FREE(dsp);
			continue;
		case DSHR_ABORT_FAILED:
			if (abt_list != NULL)
				d_list_add_tail(&dsp->dsp_link, abt_list);
			else
				D_FREE(dsp);
			continue;
		case DSHR_CORRUPT:
			D_FREE(dsp);
			if (failout)
				D_GOTO(out, rc = -DER_DATA_LOSS);
			continue;
		default:
			D_FREE(dsp);
			if (failout)
				goto out;
			continue;
		}
	}

	rc = 0;

out:
	while ((drr = d_list_pop_entry(&head, struct dtx_req_rec,
				       drr_link)) != NULL) {
		D_FREE(drr->drr_cb_args);
		D_FREE(drr->drr_dti);
		D_FREE(drr);
	}

	while ((dsp = d_list_pop_entry(&self, struct dtx_share_peer,
				       dsp_link)) != NULL)
		D_FREE(dsp);

	return rc;
}

/*
 * Because of async batched commit semantics, the DTX status on the leader
 * maybe different from the one on non-leaders. For the leader, it exactly
 * knows whether the DTX is committable or not, but the non-leader does not
 * know if the DTX is in 'prepared' status. If someone on non-leader wants
 * to know whether some 'prepared' DTX is real committable or not, it needs
 * to refresh such DTX status from the leader. The DTX_REFRESH RPC is used
 * for such purpose.
 */
int
dtx_refresh(struct dtx_handle *dth, struct ds_cont_child *cont)
{
	int	rc;

	if (DAOS_FAIL_CHECK(DAOS_DTX_NO_RETRY))
		return -DER_IO;

	rc = dtx_refresh_internal(cont, &dth->dth_share_tbd_count,
				  &dth->dth_share_tbd_list,
				  &dth->dth_share_cmt_list,
				  &dth->dth_share_abt_list,
				  &dth->dth_share_act_list, true);

	/* If we can resolve the DTX status, then return -DER_AGAIN
	 * to the caller that will retry related operation locally.
	 */
	if (rc == 0) {
		D_ASSERT(dth->dth_share_tbd_count == 0);

		vos_dtx_cleanup(dth);
		dtx_handle_reinit(dth);
		rc = -DER_AGAIN;
	}

	return rc;
}
