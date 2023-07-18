/**
 * (C) Copyright 2019-2023 Intel Corporation.
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
	/* The committed DTX entries on all related participants, for DTX_COMMIT. */
	int				 dra_committed;
	/* pool UUID */
	uuid_t				 dra_po_uuid;
	/* container UUID */
	uuid_t				 dra_co_uuid;
	/* The count of sub requests. */
	int				 dra_length;
	/* The collective RPC result. */
	int				 dra_result;
	/* Pointer to the committed DTX list, used for DTX_REFRESH case. */
	d_list_t			*dra_cmt_list;
	/* Pointer to the aborted DTX list, used for DTX_REFRESH case. */
	d_list_t			*dra_abt_list;
	/* Pointer to the active DTX list, used for DTX_REFRESH case. */
	d_list_t			*dra_act_list;
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
	uint32_t			*drr_flags;
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

static inline void
dtx_drr_cleanup(struct dtx_req_rec *drr)
{
	int	i;

	if (drr->drr_cb_args != NULL) {
		for (i = 0; i < drr->drr_count; i++) {
			if (drr->drr_cb_args[i] != NULL)
				dtx_dsp_free(drr->drr_cb_args[i]);
		}
		D_FREE(drr->drr_cb_args);
	}
	D_FREE(drr->drr_dti);
	D_FREE(drr->drr_flags);
	D_FREE(drr);
}

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
		dra->dra_committed += dout->do_misc;
		D_GOTO(out, rc = dout->do_status);
	}

	if (dout->do_status != 0 || dra->dra_opc != DTX_REFRESH)
		D_GOTO(out, rc = dout->do_status);

	if (din->di_dtx_array.ca_count != dout->do_sub_rets.ca_count)
		D_GOTO(out, rc = -DER_PROTO);

	D_ASSERT(dra->dra_cmt_list != NULL);
	D_ASSERT(dra->dra_abt_list != NULL);
	D_ASSERT(dra->dra_act_list != NULL);

	for (i = 0; i < dout->do_sub_rets.ca_count; i++) {
		struct dtx_share_peer	*dsp;
		int			*ret;

		dsp = drr->drr_cb_args[i];
		if (dsp == NULL)
			continue;

		D_ASSERT(d_list_empty(&dsp->dsp_link));

		drr->drr_cb_args[i] = NULL;
		ret = (int *)dout->do_sub_rets.ca_arrays + i;

		switch (*ret) {
		case DTX_ST_PREPARED:
			d_list_add_tail(&dsp->dsp_link, dra->dra_act_list);
			break;
		case DTX_ST_COMMITTABLE:
			/*
			 * Committable, will be committed soon.
			 * Fall through.
			 */
		case DTX_ST_COMMITTED:
			d_list_add_tail(&dsp->dsp_link, dra->dra_cmt_list);
			break;
		case DTX_ST_CORRUPTED:
			/* The DTX entry is corrupted. */
			dtx_dsp_free(dsp);
			D_GOTO(out, rc = -DER_DATA_LOSS);
		case -DER_TX_UNCERTAIN:
			dsp->dsp_status = -DER_TX_UNCERTAIN;
			d_list_add_tail(&dsp->dsp_link, dra->dra_act_list);
			break;
		case -DER_NONEXIST:
			d_list_add_tail(&dsp->dsp_link, dra->dra_abt_list);
			break;
		case -DER_INPROGRESS:
			dsp->dsp_status = -DER_INPROGRESS;
			d_list_add_tail(&dsp->dsp_link, dra->dra_act_list);
			break;
		default:
			dtx_dsp_free(dsp);
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
		if (drr->drr_flags != NULL) {
			din->di_flags.ca_count = drr->drr_count;
			din->di_flags.ca_arrays = drr->drr_flags;
		} else {
			din->di_flags.ca_count = 0;
			din->di_flags.ca_arrays = NULL;
		}

		if (dra->dra_opc == DTX_REFRESH) {
			if (DAOS_FAIL_CHECK(DAOS_DTX_RESYNC_DELAY))
				rc = crt_req_set_timeout(req, 3);
			else
				/*
				 * If related DTX is committable, then it will be committed
				 * within DTX_COMMIT_THRESHOLD_AGE time. So if need to wait
				 * for longer, then just let related client to retry.
				 */
				rc = crt_req_set_timeout(req, DTX_COMMIT_THRESHOLD_AGE);
			D_ASSERTF(rc == 0, "crt_req_set_timeout failed: %d\n", rc);
		}

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
					"The DTX "DF_DTI" has been committed on %d/%d.\n",
					DP_DTI(&drr->drr_dti[0]), drr->drr_rank, drr->drr_tag);
				return;
			case -DER_EXCLUDED:
				/*
				 * If non-leader is excluded, handle it as 'prepared'. If other
				 * non-leaders are also 'prepared' then related DTX maybe still
				 * committable or 'corrupted'. The subsequent DTX resync logic
				 * will handle related things, see dtx_verify_groups().
				 *
				 * Fall through.
				 */
			case DTX_ST_PREPARED:
				if (dra->dra_result == 0 ||
				    dra->dra_result == DTX_ST_CORRUPTED)
					dra->dra_result = DTX_ST_PREPARED;
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

			D_DEBUG(DB_TRACE, "The DTX "DF_DTI" RPC req result %d, status is %d.\n",
				DP_DTI(&drr->drr_dti[0]), drr->drr_result, dra->dra_result);
		}
	} else {
		for (i = 0; i < dra->dra_length; i++) {
			drr = args[i];
			if (dra->dra_result == 0 || dra->dra_result == -DER_NONEXIST)
				dra->dra_result = drr->drr_result;
		}

		drr = args[0];
		D_CDEBUG(dra->dra_result < 0 && dra->dra_result != -DER_NONEXIST &&
			 dra->dra_result != -DER_INPROGRESS, DLOG_ERR, DB_TRACE,
			 "DTX req for opc %x ("DF_DTI") %s, count %d: %d.\n",
			 dra->dra_opc, DP_DTI(&drr->drr_dti[0]),
			 dra->dra_result < 0 ? "failed" : "succeed",
			 dra->dra_length, dra->dra_result);
	}
}

static int
dtx_req_wait(struct dtx_req_args *dra)
{
	int	rc;

	if (dra->dra_future != ABT_FUTURE_NULL) {
		rc = ABT_future_wait(dra->dra_future);
		D_CDEBUG(rc != ABT_SUCCESS, DLOG_ERR, DB_TRACE,
			 "DTX req for opc %x, length = %d, future %p done, rc = %d, result = %d\n",
			 dra->dra_opc, dra->dra_length, dra->dra_future, rc, dra->dra_result);
		ABT_future_free(&dra->dra_future);
	}

	return dra->dra_result;
}

struct dtx_common_args {
	struct dtx_req_args	  dca_dra;
	d_list_t		  dca_head;
	struct btr_root		  dca_tree_root;
	daos_handle_t		  dca_tree_hdl;
	daos_epoch_t		  dca_epoch;
	int			  dca_count;
	int			  dca_committed;
	d_rank_t		  dca_rank;
	uint32_t		  dca_tgtid;
	struct ds_cont_child	 *dca_cont;
	ABT_thread		  dca_helper;
	struct dtx_id		  dca_dti_inline;
	struct dtx_id		 *dca_dtis;
	struct dtx_entry	**dca_dtes;
};

static int
dtx_req_list_send(struct dtx_common_args *dca, daos_epoch_t epoch, int len)
{
	struct dtx_req_args	*dra = &dca->dca_dra;
	struct dtx_req_rec	*drr;
	int			 rc;
	int			 i = 0;

	dra->dra_length = len;

	rc = ABT_future_create(len, dtx_req_list_cb, &dra->dra_future);
	if (rc != ABT_SUCCESS) {
		D_ERROR("ABT_future_create failed for opc %x, len = %d: "
			"rc = %d.\n", dra->dra_opc, len, rc);
		return dss_abterr2der(rc);
	}

	D_DEBUG(DB_TRACE, "DTX req for opc %x, future %p start.\n", dra->dra_opc, dra->dra_future);

	d_list_for_each_entry(drr, &dca->dca_head, drr_link) {
		drr->drr_parent = dra;
		drr->drr_result = 0;

		if (unlikely(dra->dra_opc == DTX_COMMIT && i == 0 &&
			     DAOS_FAIL_CHECK(DAOS_DTX_FAIL_COMMIT)))
			rc = dtx_req_send(drr, 1);
		else
			rc = dtx_req_send(drr, epoch);
		if (rc != 0) {
			/* If the first sub-RPC failed, then break, otherwise
			 * other remote replicas may have already received the
			 * RPC and executed it, so have to go ahead.
			 */
			if (i == 0) {
				ABT_future_free(&dra->dra_future);
				return rc;
			}
		}

		/* Yield to avoid holding CPU for too long time. */
		if (++i % DTX_RPC_YIELD_THD == 0)
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
	dtx_drr_cleanup(drr);

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
dtx_classify_one(struct ds_pool *pool, daos_handle_t tree, d_list_t *head, int *length,
		 struct dtx_entry *dte, int count, d_rank_t my_rank, uint32_t my_tgtid)
{
	struct dtx_memberships		*mbs = dte->dte_mbs;
	struct pool_target		*target;
	struct dtx_cf_rec_bundle	 dcrb;
	int				 rc = 0;
	int				 i;

	if (mbs->dm_tgt_cnt == 0)
		D_GOTO(out, rc = -DER_INVAL);

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
	for (; i < mbs->dm_tgt_cnt; i++) {
		rc = pool_map_find_target(pool->sp_map,
					  mbs->dm_tgts[i].ddt_id, &target);
		if (rc != 1) {
			D_WARN("Cannot find target %u at %d/%d, flags %x\n",
			       mbs->dm_tgts[i].ddt_id, i, mbs->dm_tgt_cnt,
			       mbs->dm_flags);
			D_GOTO(out, rc = -DER_UNINIT);
		}

		/* Skip the target that (re-)joined the system after the DTX. */
		if (target->ta_comp.co_ver > dte->dte_ver)
			continue;

		/* Skip non-healthy one. */
		if (target->ta_comp.co_status != PO_COMP_ST_UP &&
		    target->ta_comp.co_status != PO_COMP_ST_UPIN &&
		    target->ta_comp.co_status != PO_COMP_ST_NEW &&
		    target->ta_comp.co_status != PO_COMP_ST_DRAIN)
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
			if (rc != 0)
				goto out;
		} else {
			struct dtx_req_rec	*drr;

			D_ALLOC_PTR(drr);
			if (drr == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			D_ALLOC_PTR(drr->drr_dti);
			if (drr->drr_dti == NULL) {
				dtx_drr_cleanup(drr);
				D_GOTO(out, rc = -DER_NOMEM);
			}

			drr->drr_rank = target->ta_comp.co_rank;
			drr->drr_tag = target->ta_comp.co_index;
			drr->drr_count = 1;
			drr->drr_dti[0] = dte->dte_xid;
			d_list_add_tail(&drr->drr_link, head);
			(*length)++;
		}
	}

out:
	return rc > 0 ? 0 : rc;
}

static int
dtx_rpc_internal(struct dtx_common_args *dca)
{
	struct ds_pool		*pool = dca->dca_cont->sc_pool->spc_pool;
	struct umem_attr	 uma = { 0 };
	int			 length = 0;
	int			 rc;
	int			 i;

	if (dca->dca_dra.dra_opc != DTX_REFRESH) {
		D_ASSERT(dca->dca_dtis != NULL);

		if (dca->dca_count > 1) {
			uma.uma_id = UMEM_CLASS_VMEM;
			rc = dbtree_create_inplace(DBTREE_CLASS_DTX_CF, 0, DTX_CF_BTREE_ORDER,
						   &uma, &dca->dca_tree_root, &dca->dca_tree_hdl);
			if (rc != 0)
				return rc;
		}

		ABT_rwlock_rdlock(pool->sp_lock);
		for (i = 0; i < dca->dca_count; i++) {
			rc = dtx_classify_one(pool, dca->dca_tree_hdl, &dca->dca_head, &length,
					      dca->dca_dtes[i], dca->dca_count,
					      dca->dca_rank, dca->dca_tgtid);
			if (rc < 0) {
				ABT_rwlock_unlock(pool->sp_lock);
				return rc;
			}

			daos_dti_copy(&dca->dca_dtis[i], &dca->dca_dtes[i]->dte_xid);
		}
		ABT_rwlock_unlock(pool->sp_lock);

		/* For DTX_CHECK, if no other available target(s), then current target is the
		 * unique valid one (and also 'prepared'), then related DTX can be committed.
		 */
		if (d_list_empty(&dca->dca_head))
			return dca->dca_dra.dra_opc == DTX_CHECK ? DTX_ST_PREPARED : 0;
	} else {
		length = dca->dca_count;
	}

	D_ASSERT(length > 0);

	return dtx_req_list_send(dca, dca->dca_epoch, length);
}

static void
dtx_rpc_helper(void *arg)
{
	struct dtx_common_args	*dca = arg;
	int			 rc;

	rc = dtx_rpc_internal(dca);

	if (rc != 0)
		dca->dca_dra.dra_result = rc;

	D_CDEBUG(rc < 0, DLOG_ERR, DB_TRACE,
		 "DTX helper ULT for %u exit: %d\n", dca->dca_dra.dra_opc, rc);
}

static int
dtx_rpc_prep(struct ds_cont_child *cont,d_list_t *dti_list,  struct dtx_entry **dtes,
	     uint32_t count, int opc, daos_epoch_t epoch, d_list_t *cmt_list,
	     d_list_t *abt_list, d_list_t *act_list, struct dtx_common_args *dca)
{
	struct dtx_req_args	*dra;
	int			 rc = 0;

	memset(dca, 0, sizeof(*dca));

	D_INIT_LIST_HEAD(&dca->dca_head);
	dca->dca_tree_hdl = DAOS_HDL_INVAL;
	dca->dca_epoch = epoch;
	dca->dca_count = count;
	crt_group_rank(NULL, &dca->dca_rank);
	dca->dca_tgtid = dss_get_module_info()->dmi_tgt_id;
	dca->dca_cont = cont;
	dca->dca_helper = ABT_THREAD_NULL;
	dca->dca_dtes = dtes;

	dra = &dca->dca_dra;
	dra->dra_future = ABT_FUTURE_NULL;
	dra->dra_cmt_list = cmt_list;
	dra->dra_abt_list = abt_list;
	dra->dra_act_list = act_list;
	dra->dra_opc = opc;
	uuid_copy(dra->dra_po_uuid, cont->sc_pool->spc_pool->sp_uuid);
	uuid_copy(dra->dra_co_uuid, cont->sc_uuid);

	if (dti_list != NULL) {
		d_list_splice(dti_list, &dca->dca_head);
		D_INIT_LIST_HEAD(dti_list);
	} else {
		if (count > 1) {
			D_ALLOC_ARRAY(dca->dca_dtis, count);
			if (dca->dca_dtis == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		} else {
			dca->dca_dtis = &dca->dca_dti_inline;
		}
	}

	/* Use helper ULT to handle DTX RPC if there are enough helper XS. */
	if (dss_has_enough_helper())
		rc = dss_ult_create(dtx_rpc_helper, dca, DSS_XS_IOFW, dca->dca_tgtid,
				    DSS_DEEP_STACK_SZ, &dca->dca_helper);
	else
		rc = dtx_rpc_internal(dca);

out:
	return rc;
}

static int
dtx_rpc_post(struct dtx_common_args *dca, int ret, bool keep_head)
{
	struct dtx_req_rec	*drr;
	int			 rc;

	if (dca->dca_helper != ABT_THREAD_NULL)
		ABT_thread_free(&dca->dca_helper);

	rc = dtx_req_wait(&dca->dca_dra);

	if (daos_handle_is_valid(dca->dca_tree_hdl))
		dbtree_destroy(dca->dca_tree_hdl, NULL);

	if (!keep_head) {
		while ((drr = d_list_pop_entry(&dca->dca_head, struct dtx_req_rec,
					       drr_link)) != NULL)
			dtx_drr_cleanup(drr);
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
	   struct dtx_cos_key *dcks, int count)
{
	struct dtx_common_args	 dca;
	struct dtx_req_args	*dra = &dca.dca_dra;
	bool			*rm_cos = NULL;
	bool			 cos = false;
	int			 rc;
	int			 rc1 = 0;
	int			 i;

	rc = dtx_rpc_prep(cont, NULL, dtes, count, DTX_COMMIT, 0, NULL, NULL, NULL, &dca);

	/*
	 * NOTE: Before committing the DTX on remote participants, we cannot remove the active
	 *	 DTX locally; otherwise, the local committed DTX entry may be removed via DTX
	 *	 aggregation before remote participants commit done. Under such case, if some
	 *	 remote DTX participant triggere DTX_REFRESH for such DTX during the interval,
	 *	 then it will get -DER_TX_UNCERTAIN, that may cause related application to be
	 *	 failed. So here, we let remote participants to commit firstly, if failed, we
	 *	 will ask the leader to retry the commit until all participants got committed.
	 *
	 * Some RPC may has been sent, so need to wait even if dtx_rpc_prep hit failure.
	 */
	rc = dtx_rpc_post(&dca, rc, false);
	if (rc > 0 || rc == -DER_NONEXIST || rc == -DER_EXCLUDED)
		rc = 0;

	if (rc != 0) {
		/*
		 * Some DTX entries may have been committed on some participants. Then mark all
		 * the DTX entries (in the dtis) as "PARTIAL_COMMITTED" and re-commit them later.
		 * It is harmless to re-commit the DTX that has ever been committed.
		 */
		if (dra->dra_committed > 0)
			rc1 = vos_dtx_set_flags(cont->sc_hdl, dca.dca_dtis, count,
						DTE_PARTIAL_COMMITTED);
	} else {
		if (dcks != NULL) {
			if (count > 1) {
				D_ALLOC_ARRAY(rm_cos, count);
				if (rm_cos == NULL)
					D_GOTO(out, rc1 = -DER_NOMEM);
			} else {
				rm_cos = &cos;
			}
		}

		rc1 = vos_dtx_commit(cont->sc_hdl, dca.dca_dtis, count, rm_cos);
		if (rc1 > 0) {
			dra->dra_committed += rc1;
			rc1 = 0;
		} else if (rc1 == -DER_NONEXIST) {
			/* -DER_NONEXIST may be caused by race or repeated commit, ignore it. */
			rc1 = 0;
		}

		if (rc1 == 0 && rm_cos != NULL) {
			for (i = 0; i < count; i++) {
				if (rm_cos[i]) {
					D_ASSERT(!daos_oid_is_null(dcks[i].oid.id_pub));
					dtx_del_cos(cont, &dca.dca_dtis[i], &dcks[i].oid,
						    dcks[i].dkey_hash);
				}
			}
		}

		if (rm_cos != &cos)
			D_FREE(rm_cos);
	}

out:
	if (dca.dca_dtis != &dca.dca_dti_inline)
		D_FREE(dca.dca_dtis);

	if (rc != 0 || rc1 != 0)
		D_ERROR("Failed to commit DTX entries "DF_DTI", count %d, %s committed: %d %d\n",
			DP_DTI(&dtes[0]->dte_xid), count,
			dra->dra_committed > 0 ? "partial" : "nothing", rc, rc1);
	else
		D_DEBUG(DB_IO, "Commit DTXs " DF_DTI", count %d\n",
			DP_DTI(&dtes[0]->dte_xid), count);

	return rc != 0 ? rc : rc1;
}


int
dtx_abort(struct ds_cont_child *cont, struct dtx_entry *dte, daos_epoch_t epoch)
{
	struct dtx_common_args	dca;
	int			rc;
	int			rc1;
	int			rc2;

	rc = dtx_rpc_prep(cont, NULL, &dte, 1, DTX_ABORT, epoch, NULL, NULL, NULL, &dca);

	rc2 = dtx_rpc_post(&dca, rc, false);
	if (rc2 > 0 || rc2 == -DER_NONEXIST)
		rc2 = 0;

	/*
	 * NOTE: The DTX abort maybe triggered by dtx_leader_end() for timeout on some DTX
	 *	 participant(s). Under such case, the client side RPC sponsor may also hit
	 *	 the RPC timeout and resends related RPC to the leader. Here, to avoid DTX
	 *	 abort and resend RPC forwarding being executed in parallel, we will abort
	 *	 local DTX after remote done, before that the logic of handling resent RPC
	 *	 on server will find the local pinned DTX entry then notify related client
	 *	 to resend sometime later.
	 */
	if (epoch != 0)
		rc1 = vos_dtx_abort(cont->sc_hdl, &dte->dte_xid, epoch);
	else
		rc1 = vos_dtx_set_flags(cont->sc_hdl, &dte->dte_xid, 1, DTE_CORRUPTED);
	if (rc1 > 0 || rc1 == -DER_NONEXIST)
		rc1 = 0;

	D_CDEBUG(rc1 != 0 || rc2 != 0, DLOG_ERR, DB_IO, "Abort DTX "DF_DTI": rc %d %d %d\n",
		 DP_DTI(&dte->dte_xid), rc, rc1, rc2);

	return rc1 != 0 ? rc1 : rc2;
}

int
dtx_check(struct ds_cont_child *cont, struct dtx_entry *dte, daos_epoch_t epoch)
{
	struct dtx_common_args	dca;
	int			rc;
	int			rc1;

	/* If no other target, then current target is the unique
	 * one and 'prepared', then related DTX can be committed.
	 */
	if (dte->dte_mbs->dm_tgt_cnt == 1)
		return DTX_ST_PREPARED;

	rc = dtx_rpc_prep(cont, NULL, &dte, 1, DTX_CHECK, epoch, NULL, NULL, NULL, &dca);

	rc1 = dtx_rpc_post(&dca, rc, false);

	D_CDEBUG(rc1 < 0, DLOG_ERR, DB_IO, "Check DTX "DF_DTI": rc %d %d\n",
		 DP_DTI(&dte->dte_xid), rc, rc1);

	return rc1;
}

int
dtx_refresh_internal(struct ds_cont_child *cont, int *check_count, d_list_t *check_list,
		     d_list_t *cmt_list, d_list_t *abt_list, d_list_t *act_list, bool for_io)
{
	struct ds_pool		*pool = cont->sc_pool->spc_pool;
	struct pool_target	*target;
	struct dtx_share_peer	*dsp;
	struct dtx_share_peer	*tmp;
	struct dtx_req_rec	*drr;
	struct dtx_common_args	 dca;
	d_list_t		 head;
	d_list_t		 self;
	d_rank_t		 myrank;
	uint32_t		 flags;
	int			 len = 0;
	int			 rc = 0;
	int			 rc1;
	int			 count;
	int			 i;
	bool			 drop;

	D_INIT_LIST_HEAD(&head);
	D_INIT_LIST_HEAD(&self);
	crt_group_rank(NULL, &myrank);

	d_list_for_each_entry_safe(dsp, tmp, check_list, dsp_link) {
		count = 0;
		drop = false;

		if (dsp->dsp_mbs == NULL) {
			rc = vos_dtx_load_mbs(cont->sc_hdl, &dsp->dsp_xid, &dsp->dsp_mbs);
			if (rc != 0) {
				if (rc != -DER_NONEXIST && for_io)
					goto out;

				drop = true;
				goto next;
			}
		}

again:
		rc = dtx_leader_get(pool, dsp->dsp_mbs, &target);
		if (rc < 0) {
			/**
			 * Currently, for EC object, if parity node is
			 * in rebuilding, we will get -DER_STALE, that
			 * is not fatal, the caller or related request
			 * sponsor can retry sometime later.
			 */
			D_WARN("Failed to find DTX leader for "DF_DTI", ver %d: "DF_RC"\n",
			       DP_DTI(&dsp->dsp_xid), pool->sp_map_version, DP_RC(rc));
			if (for_io)
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
		    dss_get_module_info()->dmi_tgt_id == target->ta_comp.co_index) {
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

		if (dsp->dsp_mbs->dm_flags & DMF_CONTAIN_LEADER &&
		    dsp->dsp_mbs->dm_tgts[0].ddt_id == target->ta_comp.co_id)
			flags = DRF_INITIAL_LEADER;
		else
			flags = 0;

		d_list_for_each_entry(drr, &head, drr_link) {
			if (drr->drr_rank == target->ta_comp.co_rank &&
			    drr->drr_tag == target->ta_comp.co_index) {
				drr->drr_dti[drr->drr_count] = dsp->dsp_xid;
				drr->drr_flags[drr->drr_count] = flags;
				drr->drr_cb_args[drr->drr_count++] = dsp;
				goto next;
			}
		}

		D_ALLOC_PTR(drr);
		if (drr == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		D_ALLOC_ARRAY(drr->drr_dti, *check_count);
		D_ALLOC_ARRAY(drr->drr_flags, *check_count);
		D_ALLOC_ARRAY(drr->drr_cb_args, *check_count);

		if (drr->drr_dti == NULL || drr->drr_flags == NULL || drr->drr_cb_args == NULL) {
			dtx_drr_cleanup(drr);
			D_GOTO(out, rc = -DER_NOMEM);
		}

		drr->drr_rank = target->ta_comp.co_rank;
		drr->drr_tag = target->ta_comp.co_index;
		drr->drr_count = 1;
		drr->drr_dti[0] = dsp->dsp_xid;
		drr->drr_flags[0] = flags;
		drr->drr_cb_args[0] = dsp;
		d_list_add_tail(&drr->drr_link, &head);
		len++;

next:
		d_list_del_init(&dsp->dsp_link);
		if (drop)
			dtx_dsp_free(dsp);
		if (--(*check_count) == 0)
			break;
	}

	if (len > 0) {
		rc = dtx_rpc_prep(cont, &head, NULL, len, DTX_REFRESH, 0,
				  cmt_list, abt_list, act_list, &dca);
		rc = dtx_rpc_post(&dca, rc, for_io);

		/*
		 * For IO case, the DTX refresh failure caused by network trouble may be not fatal
		 * for its sponsor IO. It is possible that during the DTX refresh RPC, related DTX
		 * entry may have been committed or aborted by its leader. So let's move those DTX
		 * entries that are still in @head list to the act_list and ask related IO sponsor
		 * to retry locally. If still 'conflict' on them, then will trigger client retry.
		 */
		if (for_io) {
			while ((drr = d_list_pop_entry(&dca.dca_head, struct dtx_req_rec,
						       drr_link)) != NULL) {
				if (rc != -DER_TIMEDOUT && !daos_crt_network_error(rc))
					goto next2;

				if (drr->drr_cb_args == NULL)
					goto next2;

				for (i = 0; i < drr->drr_count; i++) {
					if (drr->drr_cb_args[i] == NULL)
						continue;

					dsp = drr->drr_cb_args[i];
					drr->drr_cb_args[i] = NULL;
					dsp->dsp_status = -DER_INPROGRESS;
					d_list_add_tail(&dsp->dsp_link, act_list);
				}

next2:
				dtx_drr_cleanup(drr);
			}

			rc = 0;
		}

		d_list_for_each_entry_safe(dsp, tmp, cmt_list, dsp_link) {
			/*
			 * It has been committed/committable on leader, we may miss
			 * related DTX commit request, so let's commit it locally.
			 */
			rc1 = vos_dtx_commit(cont->sc_hdl, &dsp->dsp_xid, 1, NULL);
			if (rc1 == 0 || rc1 == -DER_NONEXIST || !for_io /* cleanup case */) {
				d_list_del(&dsp->dsp_link);
				dtx_dsp_free(dsp);
			}
		}

		d_list_for_each_entry_safe(dsp, tmp, abt_list, dsp_link) {
			/*
			 * The leader does not have related DTX info, we may miss
			 * related DTX abort request, so let's abort it locally.
			 *
			 * NOTE:
			 * There is race between DTX refresh RPC being triggered on current engine
			 * and DTX commit RPC on remote leader. Related DTX entry on current engine
			 * may has been committed by race before DTX refresh RPC being replied. And
			 * it is possible that related DTX entry on remote leader may be removed by
			 * DTX aggregation before the DTX refresh RPC being handled on the leader.
			 * Under such case, the leader will reply -DER_NONEXIST to the DTX refresh
			 * RPC sponsor. Let's check such case to avoid confused abort failure.
			 */

			rc1 = vos_dtx_check(cont->sc_hdl, &dsp->dsp_xid,
					    NULL, NULL, NULL, NULL, false);
			if (rc1 == DTX_ST_COMMITTED || rc1 == DTX_ST_COMMITTABLE ||
			    rc1 == -DER_NONEXIST) {
				d_list_del(&dsp->dsp_link);
				dtx_dsp_free(dsp);
			} else {
				rc1 = vos_dtx_abort(cont->sc_hdl, &dsp->dsp_xid, dsp->dsp_epoch);
				D_ASSERT(rc1 != -DER_NO_PERM);

				if (rc1 == 0 || !for_io) {
					d_list_del(&dsp->dsp_link);
					dtx_dsp_free(dsp);
				}
			}
		}

		d_list_for_each_entry_safe(dsp, tmp, act_list, dsp_link) {
			if (dsp->dsp_status == -DER_TX_UNCERTAIN) {
				rc1 = vos_dtx_set_flags(cont->sc_hdl, &dsp->dsp_xid, 1, DTE_ORPHAN);
				if (rc1 != -DER_NONEXIST && rc1 != -DER_NO_PERM) {
					D_ERROR("Hit uncertain (may be leaked) DTX "
						DF_DTI", mark it as orphan: "DF_RC"\n",
						DP_DTI(&dsp->dsp_xid), DP_RC(rc1));
					if (rc == 0)
						rc = -DER_TX_UNCERTAIN;
				}

				d_list_del(&dsp->dsp_link);
				dtx_dsp_free(dsp);
			} else if (dsp->dsp_status == -DER_INPROGRESS) {
				rc1 = vos_dtx_check(cont->sc_hdl, &dsp->dsp_xid,
						    NULL, NULL, NULL, NULL, false);
				if (rc1 != DTX_ST_COMMITTED && rc1 != DTX_ST_ABORTED &&
				    rc1 != -DER_NONEXIST) {
					if (!for_io)
						D_INFO("Hit some long-time DTX "DF_DTI", %d\n",
						       DP_DTI(&dsp->dsp_xid), rc1);
					else if (rc == 0)
						rc = -DER_INPROGRESS;
				}

				d_list_del(&dsp->dsp_link);
				dtx_dsp_free(dsp);
			} else if (!for_io) {
				/* For cleanup case. */
				d_list_del(&dsp->dsp_link);
				dtx_dsp_free(dsp);
			}
		}

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
		dte.dte_mbs = dsp->dsp_mbs;

		rc = dtx_status_handle_one(cont, &dte, dsp->dsp_epoch,
					   NULL, NULL);
		switch (rc) {
		case DSHR_NEED_COMMIT: {
			struct dtx_entry	*pdte = &dte;
			struct dtx_cos_key	 dck;

			dck.oid = dsp->dsp_oid;
			dck.dkey_hash = dsp->dsp_dkey_hash;
			rc = dtx_commit(cont, &pdte, &dck, 1);
			if (rc < 0 && rc != -DER_NONEXIST)
				d_list_add_tail(&dsp->dsp_link, cmt_list);
			else
				dtx_dsp_free(dsp);
			continue;
		}
		case DSHR_NEED_RETRY:
			dtx_dsp_free(dsp);
			if (for_io)
				D_GOTO(out, rc = -DER_INPROGRESS);
			continue;
		case DSHR_IGNORE:
			dtx_dsp_free(dsp);
			continue;
		case DSHR_ABORT_FAILED:
			if (abt_list != NULL)
				d_list_add_tail(&dsp->dsp_link, abt_list);
			else
				dtx_dsp_free(dsp);
			continue;
		case DSHR_CORRUPT:
			dtx_dsp_free(dsp);
			if (for_io)
				D_GOTO(out, rc = -DER_DATA_LOSS);
			continue;
		default:
			dtx_dsp_free(dsp);
			if (for_io)
				goto out;
			continue;
		}
	}

	rc = 0;

out:
	while ((drr = d_list_pop_entry(&head, struct dtx_req_rec, drr_link)) != NULL)
		dtx_drr_cleanup(drr);

	while ((dsp = d_list_pop_entry(&self, struct dtx_share_peer,
				       dsp_link)) != NULL)
		dtx_dsp_free(dsp);

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

		if (dth->dth_need_validation) {
			rc = vos_dtx_validation(dth);
			switch (rc) {
			case DTX_ST_INITED:
				if (!dth->dth_aborted)
					break;
				/* Fall through */
			case DTX_ST_PREPARED:
			case DTX_ST_PREPARING:
				/* The DTX has been ever aborted and related resent RPC
				 * is in processing. Return -DER_AGAIN to make this ULT
				 * to retry sometime later without dtx_abort().
				 */
				rc = -DER_AGAIN;
				break;
			case DTX_ST_ABORTED:
				D_ASSERT(dth->dth_ent == NULL);
				/* Aborted, return -DER_INPROGRESS for client retry.
				 *
				 * Fall through.
				 */
			case DTX_ST_ABORTING:
				rc = -DER_INPROGRESS;
				break;
			case DTX_ST_COMMITTED:
			case DTX_ST_COMMITTING:
			case DTX_ST_COMMITTABLE:
				/* Aborted then prepared/committed by race.
				 * Return -DER_ALREADY to avoid repeated modification.
				 */
				dth->dth_already = 1;
				rc = -DER_ALREADY;
				break;
			default:
				D_ASSERTF(0, "Unexpected DTX "DF_DTI" status %d\n",
					  DP_DTI(&dth->dth_xid), rc);
			}
		} else {
			vos_dtx_cleanup(dth, false);
			dtx_handle_reinit(dth);
			rc = -DER_AGAIN;
		}
	}

	return rc;
}
