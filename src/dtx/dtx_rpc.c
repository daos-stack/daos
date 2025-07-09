/**
 * (C) Copyright 2019-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
CRT_RPC_DEFINE(dtx_coll, DAOS_ISEQ_COLL_DTX, DAOS_OSEQ_COLL_DTX);

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
	uint32_t                         dra_version;
	/* The count of sub requests. */
	int				 dra_length;
	/* The collective RPC result. */
	int				 dra_result;
	uint32_t			 dra_local_fail:1;
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
	uint32_t			 drr_comp:1,
					 drr_local_fail:1,
					 drr_single_dti:1;
	uint32_t			 drr_inline_flags;
	struct dtx_id			*drr_dti; /* The DTX array */
	uint32_t			*drr_flags;
	union {
		struct dtx_share_peer	**drr_cb_args; /* Used by dtx_req_cb. */
		struct dtx_share_peer	*drr_single_cb_arg;
	};
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

	if (drr->drr_single_dti == 0) {
		D_ASSERT(drr->drr_flags != &drr->drr_inline_flags);

		if (drr->drr_cb_args != NULL) {
			for (i = 0; i < drr->drr_count; i++) {
				if (drr->drr_cb_args[i] != NULL)
					dtx_dsp_free(drr->drr_cb_args[i]);
			}
			D_FREE(drr->drr_cb_args);
		}
		D_FREE(drr->drr_dti);
		D_FREE(drr->drr_flags);
	} else if (drr->drr_single_cb_arg != NULL) {
		dtx_dsp_free(drr->drr_single_cb_arg);
	}

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

		if (drr->drr_single_dti == 0) {
			dsp = drr->drr_cb_args[i];
			drr->drr_cb_args[i] = NULL;
		} else {
			dsp = drr->drr_single_cb_arg;
			drr->drr_single_cb_arg = NULL;
		}

		if (dsp == NULL)
			continue;

		D_ASSERT(d_list_empty(&dsp->dsp_link));

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
	DL_CDEBUG(rc < 0 && rc != -DER_NONEXIST, DLOG_ERR, DB_TRACE, rc,
		  "DTX req for opc %x (req %p future %p) got reply from %d/%d: "
		  "epoch :"DF_X64, dra->dra_opc, req, dra->dra_future,
		  drr->drr_rank, drr->drr_tag, din != NULL ? din->di_epoch : 0);

	drr->drr_comp = 1;
	drr->drr_result = rc;
	rc = ABT_future_set(dra->dra_future, drr);
	D_ASSERTF(rc == ABT_SUCCESS,
		  "ABT_future_set failed for opc %x to %d/%d: rc = %d.\n",
		  dra->dra_opc, drr->drr_rank, drr->drr_tag, rc);
}

static int
dtx_req_send(struct dtx_req_rec *drr, daos_epoch_t epoch)
{
	struct dtx_req_args	*dra = drr->drr_parent;
	crt_rpc_t		*req = NULL;
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
		din->di_epoch               = epoch;
		din->di_version             = dra->dra_version;
		din->di_dtx_array.ca_count  = drr->drr_count;
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
		/* CAUTION: req and din may have been freed. */
	}

	DL_CDEBUG(rc != 0, DLOG_ERR, DB_TRACE, rc,
		  "DTX req for opc %x to %d/%d (req %p future %p) sent epoch "DF_X64,
		  dra->dra_opc, drr->drr_rank, drr->drr_tag, req, dra->dra_future, epoch);

	if (rc != 0) {
		drr->drr_local_fail = 1;
		if (drr->drr_comp == 0) {
			drr->drr_comp = 1;
			drr->drr_result = rc;
			ABT_future_set(dra->dra_future, drr);
		}
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
			if (drr->drr_local_fail)
				dra->dra_local_fail = 1;
			dtx_merge_check_result(&dra->dra_result, drr->drr_result);
			D_DEBUG(DB_TRACE, "The DTX "DF_DTI" RPC req result %d, status is %d.\n",
				DP_DTI(&drr->drr_dti[0]), drr->drr_result, dra->dra_result);
		}
	} else {
		for (i = 0; i < dra->dra_length; i++) {
			drr = args[i];
			if (drr->drr_local_fail)
				dra->dra_local_fail = 1;
			if (dra->dra_result == 0 || dra->dra_result == -DER_NONEXIST)
				dra->dra_result = drr->drr_result;
		}

		drr = args[0];
		D_CDEBUG(dra->dra_result < 0 && dra->dra_result != -DER_NONEXIST &&
			     dra->dra_result != -DER_INPROGRESS,
			 DLOG_ERR, DB_TRACE,
			 "DTX req for opc %x (" DF_DTI ") %s, count %d: " DF_RC "\n", dra->dra_opc,
			 DP_DTI(&drr->drr_dti[0]), dra->dra_result < 0 ? "failed" : "succeed",
			 dra->dra_length, DP_RC(dra->dra_result));
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
	struct dss_chore	  dca_chore;
	ABT_eventual		  dca_chore_eventual;
	struct dtx_req_args	  dca_dra;
	d_list_t		  dca_head;
	struct btr_root		  dca_tree_root;
	daos_handle_t		  dca_tree_hdl;
	daos_epoch_t		  dca_epoch;
	int			  dca_count;
	int			  dca_steps;
	d_rank_t		  dca_rank;
	uint32_t		  dca_tgtid;
	struct ds_cont_child	 *dca_cont;
	struct dtx_id		  dca_dti_inline;
	struct dtx_id		 *dca_dtis;
	struct dtx_entry	**dca_dtes;

	/* Chore-internal state variables */
	struct dtx_req_rec	 *dca_drr;
	int			  dca_i;
};

static int
dtx_req_list_send(struct dtx_common_args *dca, bool is_reentrance)
{
	struct dtx_req_args	*dra = &dca->dca_dra;
	int			 rc;

	if (!is_reentrance) {
		dra->dra_length = dca->dca_steps;
		dca->dca_i = 0;

		rc = ABT_future_create(dca->dca_steps, dtx_req_list_cb, &dra->dra_future);
		if (rc != ABT_SUCCESS) {
			D_ERROR("ABT_future_create failed for opc %x, len %d: rc %d.\n",
				dra->dra_opc, dca->dca_steps, rc);
			dra->dra_local_fail = 1;
			if (dra->dra_opc == DTX_CHECK)
				dtx_merge_check_result(&dra->dra_result, dss_abterr2der(rc));
			else if (dra->dra_result == 0 || dra->dra_result == -DER_NONEXIST)
				dra->dra_result = dss_abterr2der(rc);
			return DSS_CHORE_DONE;
		}

		D_DEBUG(DB_TRACE, "%p: DTX req for opc %x, future %p (%d) start.\n",
			&dca->dca_chore, dra->dra_opc, dra->dra_future, dca->dca_steps);
	}

	while (1) {
		D_DEBUG(DB_TRACE, "chore=%p: drr=%p i=%d\n", &dca->dca_chore, dca->dca_drr,
			dca->dca_i);

		dca->dca_drr->drr_parent = dra;
		dca->dca_drr->drr_result = 0;

		if (unlikely(dra->dra_opc == DTX_COMMIT && dca->dca_i == 0 &&
			     DAOS_FAIL_CHECK(DAOS_DTX_FAIL_COMMIT)))
			dtx_req_send(dca->dca_drr, 1);
		else
			dtx_req_send(dca->dca_drr, dca->dca_epoch);
		/*
		 * Do not care dtx_req_send result, itself or its cb func will set dra->dra_future.
		 * Each RPC is independent from the others, let's go head to handle the other RPCs
		 * and set dra->dra_future that will avoid blocking the RPC sponsor - dtx_req_wait.
		 */

		/* dca->dca_drr maybe not points to a real entry if all RPCs have been sent. */
		dca->dca_drr = d_list_entry(dca->dca_drr->drr_link.next,
					    struct dtx_req_rec, drr_link);

		if (++(dca->dca_i) >= dca->dca_steps)
			break;

		/* Yield to avoid holding CPU for too long time. */
		if (dca->dca_i % DTX_RPC_YIELD_THD == 0)
			return DSS_CHORE_YIELD;
	}

	return DSS_CHORE_DONE;
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
		 struct dtx_entry *dte, int count, d_rank_t my_rank, uint32_t my_tgtid,
		 uint32_t opc)
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

			D_ASSERT(count == 1);

			D_ALLOC_PTR(drr);
			if (drr == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			drr->drr_single_dti = 1;

			/*
			 * Usually, sync commit handles single DTX and batched commit handles
			 * multiple ones. So we roughly regard single DTX commit case as sync
			 * commit for metrics purpose.
			 */
			if (opc == DTX_COMMIT) {
				drr->drr_inline_flags = DRF_SYNC_COMMIT;
				drr->drr_flags = &drr->drr_inline_flags;
			}

			drr->drr_rank = target->ta_comp.co_rank;
			drr->drr_tag = target->ta_comp.co_index;
			drr->drr_count = 1;
			drr->drr_dti = &dte->dte_xid;
			d_list_add_tail(&drr->drr_link, head);
			(*length)++;
		}
	}

out:
	return rc > 0 ? 0 : rc;
}

static enum dss_chore_status
dtx_rpc_helper(struct dss_chore *chore, bool is_reentrance)
{
	struct dtx_common_args	*dca = container_of(chore, struct dtx_common_args, dca_chore);
	int			 rc;

	rc = dtx_req_list_send(dca, is_reentrance);
	if (rc == DSS_CHORE_YIELD)
		return DSS_CHORE_YIELD;
	D_ASSERTF(rc == DSS_CHORE_DONE, "Unexpected helper return value for RPC %u: %d\n",
		  dca->dca_dra.dra_opc, rc);
	if (dca->dca_chore_eventual != ABT_EVENTUAL_NULL) {
		rc = ABT_eventual_set(dca->dca_chore_eventual, NULL, 0);
		D_ASSERTF(rc == ABT_SUCCESS, "ABT_eventual_set: %d\n", rc);
	}
	return DSS_CHORE_DONE;
}

static int
dtx_rpc(struct ds_cont_child *cont,d_list_t *dti_list,  struct dtx_entry **dtes, uint32_t count,
	int opc, daos_epoch_t epoch, d_list_t *cmt_list, d_list_t *abt_list, d_list_t *act_list,
	bool keep_head, struct dtx_common_args *dca)
{
	struct ds_pool		*pool = cont->sc_pool->spc_pool;
	struct dtx_req_rec	*drr;
	struct dtx_req_args	*dra;
	struct umem_attr	 uma = { 0 };
	int			 length = 0;
	int			 rc = 0;
	int			 i;

	memset(dca, 0, sizeof(*dca));

	dca->dca_chore_eventual = ABT_EVENTUAL_NULL;
	D_INIT_LIST_HEAD(&dca->dca_head);
	dca->dca_tree_hdl = DAOS_HDL_INVAL;
	dca->dca_epoch = epoch;
	dca->dca_count = count;
	crt_group_rank(NULL, &dca->dca_rank);
	dca->dca_tgtid = dss_get_module_info()->dmi_tgt_id;
	dca->dca_cont = cont;
	dca->dca_dtes = dtes;

	dra = &dca->dca_dra;
	dra->dra_future = ABT_FUTURE_NULL;
	dra->dra_cmt_list = cmt_list;
	dra->dra_abt_list = abt_list;
	dra->dra_act_list = act_list;
	dra->dra_version  = pool->sp_map_version;
	dra->dra_opc      = opc;
	uuid_copy(dra->dra_po_uuid, pool->sp_uuid);
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

	if (dca->dca_dtes != NULL) {
		D_ASSERT(dca->dca_dtis != NULL);

		if (dca->dca_count > 1) {
			uma.uma_id = UMEM_CLASS_VMEM;
			rc = dbtree_create_inplace(DBTREE_CLASS_DTX_CF, 0, DTX_CF_BTREE_ORDER,
						   &uma, &dca->dca_tree_root, &dca->dca_tree_hdl);
			if (rc != 0)
				goto out;
		}

		ABT_rwlock_rdlock(pool->sp_lock);
		for (i = 0; i < dca->dca_count; i++) {
			rc = dtx_classify_one(pool, dca->dca_tree_hdl, &dca->dca_head, &length,
					      dca->dca_dtes[i], dca->dca_count,
					      dca->dca_rank, dca->dca_tgtid, dca->dca_dra.dra_opc);
			if (rc != 0) {
				ABT_rwlock_unlock(pool->sp_lock);
				goto out;
			}

			daos_dti_copy(&dca->dca_dtis[i], &dca->dca_dtes[i]->dte_xid);
		}
		ABT_rwlock_unlock(pool->sp_lock);

		/* For DTX_CHECK, if no other available target(s), then current target is the
		 * unique valid one (and also 'prepared'), then related DTX can be committed.
		 */
		if (d_list_empty(&dca->dca_head)) {
			rc = (dca->dca_dra.dra_opc == DTX_CHECK ? DTX_ST_PREPARED : 0);
			goto out;
		}
	} else {
		D_ASSERT(!d_list_empty(&dca->dca_head));

		length = dca->dca_count;
	}

	dca->dca_chore.cho_func     = dtx_rpc_helper;
	dca->dca_chore.cho_priority = 1;
	dca->dca_drr = d_list_entry(dca->dca_head.next, struct dtx_req_rec, drr_link);

	/*
	 * Do not send out the batched RPCs all together, instead, we do that step by step to
	 * avoid holding too much system resources for relative long time. It is also helpful
	 * to reduce the whole network peak load and the pressure on related peers.
	 */
	while (length > 0) {
		if (length > DTX_PRI_RPC_STEP_LENGTH && opc != DTX_CHECK)
			dca->dca_steps = DTX_PRI_RPC_STEP_LENGTH;
		else
			dca->dca_steps = length;

		/* Use helper ULT to handle DTX RPC if there are enough helper XS. */
		if (dss_has_enough_helper()) {
			rc = ABT_eventual_create(0, &dca->dca_chore_eventual);
			if (rc != ABT_SUCCESS) {
				D_ERROR("failed to create eventual: %d\n", rc);
				rc = dss_abterr2der(rc);
				goto out;
			}

			dca->dca_chore.cho_credits = dca->dca_steps;
			dca->dca_chore.cho_hint    = NULL;
			rc                         = dss_chore_register(&dca->dca_chore);
			if (rc != 0) {
				ABT_eventual_free(&dca->dca_chore_eventual);
				goto out;
			}

			rc = ABT_eventual_wait(dca->dca_chore_eventual, NULL);
			D_ASSERTF(rc == ABT_SUCCESS, "ABT_eventual_wait: %d\n", rc);

			rc = ABT_eventual_free(&dca->dca_chore_eventual);
			D_ASSERTF(rc == ABT_SUCCESS, "ABT_eventual_free: %d\n", rc);
		} else {
			dss_chore_diy(&dca->dca_chore);
		}

		rc = dtx_req_wait(&dca->dca_dra);
		dss_chore_deregister(&dca->dca_chore);
		if (rc == 0 || rc == -DER_NONEXIST)
			goto next;

		switch (opc) {
		case DTX_COMMIT:
		case DTX_ABORT:
			/*
			 * Continue to send out more RPCs as long as there is no local failure,
			 * then other healthy participants can commit/abort related DTX entries
			 * without being affected by the bad one(s).
			 */
			if (dca->dca_dra.dra_local_fail)
				goto out;
			break;
		case DTX_CHECK:
			if (rc == DTX_ST_COMMITTED || rc == DTX_ST_COMMITTABLE)
				goto out;
			/*
			 * Go ahead even if someone failed, there may be 'COMMITTED'
			 * in subsequent check, that will overwrite former failure.
			 */
			break;
		case DTX_REFRESH:
			D_ASSERTF(length < DTX_PRI_RPC_STEP_LENGTH,
				  "Too long list for DTX refresh: %u vs %u\n", length,
				  DTX_PRI_RPC_STEP_LENGTH);
			break;
		default:
			D_ASSERTF(0, "Invalid DTX opc %u\n", opc);
		}

next:
		length -= dca->dca_steps;
	}

out:
	if (daos_handle_is_valid(dca->dca_tree_hdl))
		dbtree_destroy(dca->dca_tree_hdl, NULL);

	if (!keep_head) {
		while ((drr = d_list_pop_entry(&dca->dca_head, struct dtx_req_rec,
					       drr_link)) != NULL)
			dtx_drr_cleanup(drr);
	}

	return rc;
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
	   struct dtx_cos_key *dcks, int count, bool has_cos)
{
	struct dtx_common_args	 dca;
	struct dtx_req_args	*dra = &dca.dca_dra;
	bool			*rm_cos = NULL;
	bool			 cos = false;
	int			 rc;
	int			 rc1 = 0;
	int			 i;

	/*
	 * NOTE: Before committing the DTX on remote participants, we cannot remove the active
	 *	 DTX locally; otherwise, the local committed DTX entry may be removed via DTX
	 *	 aggregation before remote participants commit done. Under such case, if some
	 *	 remote DTX participant triggere DTX_REFRESH for such DTX during the interval,
	 *	 then it will get -DER_TX_UNCERTAIN, that may cause related application to be
	 *	 failed. So here, we let remote participants to commit firstly, if failed, we
	 *	 will ask the leader to retry the commit until all participants got committed.
	 */
	rc = dtx_rpc(cont, NULL, dtes, count, DTX_COMMIT, 0, NULL, NULL, NULL, false, &dca);
	if (rc > 0 || rc == -DER_NONEXIST || rc == -DER_EXCLUDED || rc == -DER_OOG)
		rc = 0;

	if (rc == 0 || dra->dra_committed > 0) {
		if (rc == 0 && has_cos) {
			if (count > 1) {
				D_ALLOC_ARRAY(rm_cos, count);
				if (rm_cos == NULL)
					D_GOTO(out, rc1 = -DER_NOMEM);
			} else {
				rm_cos = &cos;
			}
		}

		/*
		 * Some DTX entries may have been committed on some participants. Then mark all
		 * the DTX entries (in the dtis) as "PARTIAL_COMMITTED" and re-commit them later.
		 * It is harmless to re-commit the DTX that has ever been committed.
		 */
		rc1 = vos_dtx_commit(cont->sc_hdl, dca.dca_dtis, count, rc != 0, rm_cos);
		if (rc1 > 0) {
			dra->dra_committed += rc1;
			rc1 = 0;
		} else if (rc1 == -DER_NONEXIST) {
			/* -DER_NONEXIST may be caused by race or repeated commit, ignore it. */
			rc1 = 0;
		}

		/*
		 * For partial commit case, move related DTX entries to the tail of the
		 * committable list, then the next batched commit can commit others and
		 * retry those partial committed sometime later instead of blocking the
		 * others committable with continuously retry the failed ones.
		 *
		 * The side-effect of such behavior is that the DTX which is committable
		 * earlier maybe delay committed than the later ones.
		 */
		if (rc1 == 0 && has_cos) {
			if (dcks != NULL) {
				if (rm_cos != NULL) {
					for (i = 0; i < count; i++) {
						if (!rm_cos[i])
							continue;
						dtx_cos_del(cont, &dca.dca_dtis[i], &dcks[i].oid,
							    dcks[i].dkey_hash, false);
					}
				} else {
					for (i = 0; i < count; i++) {
						dtx_cos_del(cont, &dca.dca_dtis[i], &dcks[i].oid,
							    dcks[i].dkey_hash, true);
					}
				}
			} else {
				dtx_cos_batched_del(cont, dca.dca_dtis, rm_cos, count);
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
		D_DEBUG(DB_TRACE, "Commit DTXs " DF_DTI", count %d\n",
			DP_DTI(&dtes[0]->dte_xid), count);

	return rc != 0 ? rc : rc1;
}


int
dtx_abort(struct ds_cont_child *cont, struct dtx_entry *dte, daos_epoch_t epoch)
{
	struct dtx_common_args	dca;
	int			rc;
	int			rc1;

	rc = dtx_rpc(cont, NULL, &dte, 1, DTX_ABORT, epoch, NULL, NULL, NULL, false, &dca);
	if (rc > 0 || rc == -DER_NONEXIST)
		rc = 0;

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

	D_CDEBUG(rc1 != 0 || rc != 0, DLOG_ERR, DB_TRACE, "Abort DTX "DF_DTI": rc %d %d\n",
		 DP_DTI(&dte->dte_xid), rc, rc1);

	return rc != 0 ? rc : rc1;
}

int
dtx_check(struct ds_cont_child *cont, struct dtx_entry *dte, daos_epoch_t epoch)
{
	struct dtx_common_args	dca;
	int			rc;

	/* If no other target, then current target is the unique
	 * one and 'prepared', then related DTX can be committed.
	 */
	if (dte->dte_mbs->dm_tgt_cnt == 1)
		return DTX_ST_PREPARED;

	rc = dtx_rpc(cont, NULL, &dte, 1, DTX_CHECK, epoch, NULL, NULL, NULL, false, &dca);

	D_CDEBUG(rc < 0 && rc != -DER_NONEXIST, DLOG_ERR, DB_TRACE,
		 "Check DTX "DF_DTI": rc %d\n", DP_DTI(&dte->dte_xid), rc);

	return rc;
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
			rc = vos_dtx_load_mbs(cont->sc_hdl, &dsp->dsp_xid, NULL, &dsp->dsp_mbs);
			if (rc != 0) {
				if (rc < 0 && rc != -DER_NONEXIST && for_io) {
					D_ERROR("Failed to load mbs for "DF_DTI": "DF_RC"\n",
						DP_DTI(&dsp->dsp_xid), DP_RC(rc));
					goto out;
				}

				drop = true;
				goto next;
			}
		}

again:
		rc = dtx_leader_get(pool, dsp->dsp_mbs, &dsp->dsp_oid, dsp->dsp_version, &target);
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
			if (for_io)
				d_list_add_tail(&dsp->dsp_link, &self);
			else
				dtx_dsp_free(dsp);
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
				D_ASSERT(drr->drr_single_dti == 0);

				drr->drr_dti[drr->drr_count] = dsp->dsp_xid;
				drr->drr_flags[drr->drr_count] = flags;
				drr->drr_cb_args[drr->drr_count++] = dsp;
				goto next;
			}
		}

		D_ALLOC_PTR(drr);
		if (drr == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		if (*check_count == 1) {
			drr->drr_single_dti = 1;
			drr->drr_dti = &dsp->dsp_xid;
			drr->drr_inline_flags = flags;
			drr->drr_flags = &drr->drr_inline_flags;
			drr->drr_single_cb_arg = dsp;
		} else {
			D_ALLOC_ARRAY(drr->drr_dti, *check_count);
			D_ALLOC_ARRAY(drr->drr_flags, *check_count);
			D_ALLOC_ARRAY(drr->drr_cb_args, *check_count);
			if (drr->drr_dti == NULL || drr->drr_flags == NULL ||
			    drr->drr_cb_args == NULL) {
				dtx_drr_cleanup(drr);
				D_GOTO(out, rc = -DER_NOMEM);
			}

			drr->drr_dti[0] = dsp->dsp_xid;
			drr->drr_flags[0] = flags;
			drr->drr_cb_args[0] = dsp;
		}

		drr->drr_rank = target->ta_comp.co_rank;
		drr->drr_tag = target->ta_comp.co_index;
		drr->drr_count = 1;
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
		rc = dtx_rpc(cont, &head, NULL, len, DTX_REFRESH, 0, cmt_list, abt_list, act_list,
			     for_io, &dca);

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

				if (drr->drr_single_dti == 0) {
					for (i = 0; i < drr->drr_count; i++) {
						if (drr->drr_cb_args[i] == NULL)
							continue;

						dsp = drr->drr_cb_args[i];
						drr->drr_cb_args[i] = NULL;
						dsp->dsp_status = -DER_INPROGRESS;
						d_list_add_tail(&dsp->dsp_link, act_list);
					}
				} else {
					dsp = drr->drr_single_cb_arg;
					drr->drr_single_cb_arg = NULL;
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
			rc1 = vos_dtx_commit(cont->sc_hdl, &dsp->dsp_xid, 1, false, NULL);
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
					    NULL, NULL, NULL, false);
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
						    NULL, NULL, NULL, false);
				if (rc1 != DTX_ST_COMMITTED && rc1 != DTX_ST_ABORTED &&
				    rc1 != -DER_NONEXIST) {
					if (!for_io)
						D_WARN("Hit unexpected long-time DTX "
						       DF_DTI": %d\n", DP_DTI(&dsp->dsp_xid), rc1);
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
		struct dtx_entry	*pdte = &dte;
		struct dtx_cos_key	 dck;


		d_list_del(&dsp->dsp_link);

		dte.dte_xid = dsp->dsp_xid;
		dte.dte_ver = pool->sp_map_version;
		dte.dte_refs = 1;
		dte.dte_mbs = dsp->dsp_mbs;

		if (for_io) {
			rc = vos_dtx_check(cont->sc_hdl, &dsp->dsp_xid, NULL, NULL, NULL, false);
			switch(rc) {
			case DTX_ST_COMMITTABLE:
				dck.oid = dsp->dsp_oid;
				dck.dkey_hash = dsp->dsp_dkey_hash;
				rc = dtx_commit(cont, &pdte, &dck, 1, true);
				if (rc < 0 && rc != -DER_NONEXIST && for_io)
					d_list_add_tail(&dsp->dsp_link, cmt_list);
				else
					dtx_dsp_free(dsp);
				continue;
			case DTX_ST_COMMITTED:
			case -DER_NONEXIST: /* Aborted */
				dtx_dsp_free(dsp);
				continue;
			default:
				break;
			}
		}

		rc = dtx_status_handle_one(cont, &dte, dsp->dsp_oid, dsp->dsp_dkey_hash,
					   dsp->dsp_epoch, NULL, NULL);
		switch (rc) {
		case DSHR_NEED_COMMIT: {
			dck.oid = dsp->dsp_oid;
			dck.dkey_hash = dsp->dsp_dkey_hash;
			rc = dtx_commit(cont, &pdte, &dck, 1, true);
			if (rc < 0 && rc != -DER_NONEXIST && for_io)
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
		case 0:
		case DSHR_IGNORE:
			dtx_dsp_free(dsp);
			continue;
		case DSHR_ABORT_FAILED:
			if (for_io)
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

static int
dtx_coll_commit_aggregator(crt_rpc_t *source, crt_rpc_t *target, void *priv)
{
	struct dtx_coll_out	*out_source = crt_reply_get(source);
	struct dtx_coll_out	*out_target = crt_reply_get(target);

	out_target->dco_misc += out_source->dco_misc;
	if (out_target->dco_status == 0)
		out_target->dco_status = out_source->dco_status;

	return 0;
}

static int
dtx_coll_abort_aggregator(crt_rpc_t *source, crt_rpc_t *target, void *priv)
{
	struct dtx_coll_out	*out_source = crt_reply_get(source);
	struct dtx_coll_out	*out_target = crt_reply_get(target);

	if (out_source->dco_status != 0 &&
	    (out_target->dco_status == 0 || out_target->dco_status == -DER_NONEXIST))
		out_target->dco_status = out_source->dco_status;

	return 0;
}

static int
dtx_coll_check_aggregator(crt_rpc_t *source, crt_rpc_t *target, void *priv)
{
	struct dtx_coll_out	*out_source = crt_reply_get(source);
	struct dtx_coll_out	*out_target = crt_reply_get(target);

	dtx_merge_check_result(&out_target->dco_status, out_source->dco_status);

	return 0;
}

struct crt_corpc_ops dtx_coll_commit_co_ops = {
	.co_aggregate = dtx_coll_commit_aggregator,
	.co_pre_forward = NULL,
	.co_post_reply = NULL,
};

struct crt_corpc_ops dtx_coll_abort_co_ops = {
	.co_aggregate = dtx_coll_abort_aggregator,
	.co_pre_forward = NULL,
	.co_post_reply = NULL,
};

struct crt_corpc_ops dtx_coll_check_co_ops = {
	.co_aggregate = dtx_coll_check_aggregator,
	.co_pre_forward = NULL,
	.co_post_reply = NULL,
};

struct dtx_coll_rpc_args {
	struct dss_chore	 dcra_chore;
	struct ds_cont_child	*dcra_cont;
	struct dtx_id		 dcra_xid;
	uint32_t		 dcra_opc;
	uint32_t		 dcra_ver;
	uint32_t                 dcra_min_rank;
	uint32_t                 dcra_max_rank;
	daos_epoch_t		 dcra_epoch;
	d_rank_list_t		*dcra_ranks;
	uint8_t			*dcra_hints;
	uint32_t		 dcra_hint_sz;
	uint32_t		 dcra_committed;
	uint32_t		 dcra_completed:1;
	int			 dcra_result;
	ABT_future		 dcra_future;
};

static void
dtx_coll_rpc_cb(const struct crt_cb_info *cb_info)
{
	struct dtx_coll_rpc_args	*dcra = cb_info->cci_arg;
	crt_rpc_t			*req = cb_info->cci_rpc;
	struct dtx_coll_out		*dco;
	int				 rc = cb_info->cci_rc;

	if (rc != 0) {
		dcra->dcra_result = rc;
	} else {
		dco = crt_reply_get(req);
		dcra->dcra_result = dco->dco_status;
		dcra->dcra_committed = dco->dco_misc;
	}

	dcra->dcra_completed = 1;
	rc = ABT_future_set(dcra->dcra_future, NULL);
	D_ASSERTF(rc == ABT_SUCCESS,
		  "ABT_future_set failed for opc %u: rc = %d\n", dcra->dcra_opc, rc);
}

static int
dtx_coll_rpc(struct dtx_coll_rpc_args *dcra)
{
	crt_rpc_t		*req = NULL;
	struct dtx_coll_in	*dci;
	int			 rc;

	rc = crt_corpc_req_create(dss_get_module_info()->dmi_ctx, NULL, dcra->dcra_ranks,
				  DAOS_RPC_OPCODE(dcra->dcra_opc, DAOS_DTX_MODULE,
						  DAOS_DTX_VERSION),
				  NULL, NULL, CRT_RPC_FLAG_FILTER_INVERT,
				  crt_tree_topo(CRT_TREE_KNOMIAL, DTX_COLL_TREE_WIDTH), &req);
	if (rc != 0) {
		D_ERROR("crt_corpc_req_create failed for coll DTX ("DF_DTI") RPC %u: "DF_RC"\n",
			DP_DTI(&dcra->dcra_xid), dcra->dcra_opc, DP_RC(rc));
		D_GOTO(out, rc);
	}

	dci = crt_req_get(req);

	uuid_copy(dci->dci_po_uuid, dcra->dcra_cont->sc_pool_uuid);
	uuid_copy(dci->dci_co_uuid, dcra->dcra_cont->sc_uuid);
	dci->dci_xid = dcra->dcra_xid;
	dci->dci_version = dcra->dcra_ver;
	dci->dci_min_rank        = dcra->dcra_min_rank;
	dci->dci_max_rank        = dcra->dcra_max_rank;
	dci->dci_epoch = dcra->dcra_epoch;
	dci->dci_hints.ca_count = dcra->dcra_hint_sz;
	dci->dci_hints.ca_arrays = dcra->dcra_hints;

	rc = crt_req_send(req, dtx_coll_rpc_cb, dcra);
	if (rc != 0)
		D_ERROR("crt_req_send failed for coll DTX ("DF_DTI") RPC %u: "DF_RC"\n",
			DP_DTI(&dcra->dcra_xid), dcra->dcra_opc, DP_RC(rc));

out:
	if (rc != 0 && !dcra->dcra_completed) {
		dcra->dcra_result = rc;
		dcra->dcra_completed = 1;
		ABT_future_set(dcra->dcra_future, NULL);
	}

	return rc;
}

static enum dss_chore_status
dtx_coll_rpc_helper(struct dss_chore *chore, bool is_reentrance)
{
	struct dtx_coll_rpc_args	*dcra;
	int				 rc;

	dcra = container_of(chore, struct dtx_coll_rpc_args, dcra_chore);

	rc = dtx_coll_rpc(dcra);

	D_CDEBUG(rc < 0, DLOG_ERR, DB_TRACE,
		 "Collective DTX helper chore for %u done: %d\n", dcra->dcra_opc, rc);

	return DSS_CHORE_DONE;
}

static int
dtx_coll_rpc_prep(struct ds_cont_child *cont, struct dtx_coll_entry *dce, uint32_t opc,
		  daos_epoch_t epoch, struct dtx_coll_rpc_args *dcra)
{
	int	rc;

	dcra->dcra_cont = cont;
	dcra->dcra_xid = dce->dce_xid;
	dcra->dcra_opc = opc;
	dcra->dcra_ver = dce->dce_ver;
	dcra->dcra_min_rank = dce->dce_min_rank;
	dcra->dcra_max_rank = dce->dce_max_rank;
	dcra->dcra_epoch = epoch;
	dcra->dcra_ranks = dce->dce_ranks;
	dcra->dcra_hints = dce->dce_hints;
	dcra->dcra_hint_sz = dce->dce_hint_sz;

	dcra->dcra_chore.cho_func     = dtx_coll_rpc_helper;
	dcra->dcra_chore.cho_priority = 1;

	rc = ABT_future_create(1, NULL, &dcra->dcra_future);
	if (rc != ABT_SUCCESS) {
		D_ERROR("ABT_future_create failed for coll DTX ("DF_DTI") RPC %u: rc = %d\n",
			DP_DTI(&dcra->dcra_xid), dcra->dcra_opc, rc);
		return dss_abterr2der(rc);
	}

	if (dss_has_enough_helper()) {
		/* The cho_credits maybe over-estimated, no matter. */
		dcra->dcra_chore.cho_credits = dcra->dcra_ranks->rl_nr < DTX_COLL_TREE_WIDTH
						   ? dcra->dcra_ranks->rl_nr
						   : DTX_COLL_TREE_WIDTH;
		dcra->dcra_chore.cho_hint    = NULL;
		rc                           = dss_chore_register(&dcra->dcra_chore);
		if (rc != 0)
			ABT_future_free(&dcra->dcra_future);
	} else {
		dss_chore_diy(&dcra->dcra_chore);
		rc = 0;
	}

	return rc;
}

static int
dtx_coll_rpc_post(struct dtx_coll_rpc_args *dcra, int ret)
{
	int	rc;

	if (dcra->dcra_future != ABT_FUTURE_NULL) {
		rc = ABT_future_wait(dcra->dcra_future);
		D_CDEBUG(rc != ABT_SUCCESS, DLOG_ERR, DB_TRACE,
			 "Collective DTX wait req for opc %u, future %p done, rc %d, result %d\n",
			 dcra->dcra_opc, dcra->dcra_future, rc, dcra->dcra_result);
		ABT_future_free(&dcra->dcra_future);
		dss_chore_deregister(&dcra->dcra_chore);
	}

	return ret != 0 ? ret : dcra->dcra_result;
}

int
dtx_coll_commit(struct ds_cont_child *cont, struct dtx_coll_entry *dce, struct dtx_cos_key *dck,
		bool has_cos)
{
	struct dtx_coll_rpc_args	 dcra = { 0 };
	int				*results = NULL;
	uint32_t			 committed = 0;
	int				 len;
	int				 rc = 0;
	int				 rc1 = 0;
	int				 rc2 = 0;
	int				 i;
	bool				 cos = true;

	if (dce->dce_ranks != NULL)
		rc = dtx_coll_rpc_prep(cont, dce, DTX_COLL_COMMIT, 0, &dcra);

	/*
	 * NOTE: Before committing the DTX on remote participants, we cannot remove the active
	 *	 DTX locally; otherwise, the local committed DTX entry may be removed via DTX
	 *	 aggregation before remote participants commit done. Under such case, if some
	 *	 remote DTX participant triggere DTX_REFRESH for such DTX during the interval,
	 *	 then it will get -DER_TX_UNCERTAIN, that may cause related application to be
	 *	 failed. So here, we let remote participants to commit firstly, if failed, we
	 *	 will ask the leader to retry the commit until all participants got committed.
	 */
	if (dce->dce_bitmap != NULL) {
		clrbit(dce->dce_bitmap, dss_get_module_info()->dmi_tgt_id);
		len = dtx_coll_local_exec(cont->sc_pool_uuid, cont->sc_uuid, &dce->dce_xid, 0,
					  DTX_COLL_COMMIT, dce->dce_bitmap_sz, dce->dce_bitmap,
					  &results);
		if (len < 0) {
			rc1 = len;
		} else {
			D_ASSERT(results != NULL);
			for (i = 0; i < len; i++) {
				if (results[i] > 0)
					committed += results[i];
				else if (results[i] < 0 && results[i] != -DER_NONEXIST && rc1 == 0)
					rc1 = results[i];
			}
		}
		D_FREE(results);
	}

	if (dce->dce_ranks != NULL) {
		rc = dtx_coll_rpc_post(&dcra, rc);
		if (rc > 0 || rc == -DER_NONEXIST || rc == -DER_EXCLUDED || rc == -DER_OOG)
			rc = 0;

		committed += dcra.dcra_committed;
	}

	if ((rc == 0 && rc1 == 0) || committed > 0) {
		/* Mark the DTX as "PARTIAL_COMMITTED" and re-commit it later via cleanup logic. */
		rc2 = vos_dtx_commit(cont->sc_hdl, &dce->dce_xid, 1, rc != 0 || rc1 != 0, NULL);
		if (rc2 > 0 || rc2 == -DER_NONEXIST)
			rc2 = 0;
	}

	/*
	 * For partial commit case, move related DTX entries to the tail of the
	 * committable list, then the next batched commit can commit others and
	 * retry those partial committed sometime later instead of blocking the
	 * others committable with continuously retry the failed ones.
	 *
	 * The side-effect of such behavior is that the DTX which is committable
	 * earlier maybe delay committed than the later ones.
	 */
	if (rc2 == 0 && has_cos) {
		if (dck != NULL)
			dtx_cos_del(cont, &dce->dce_xid, &dck->oid, dck->dkey_hash,
				    rc != 0 || rc1 != 0);
		else
			dtx_cos_batched_del(cont, &dce->dce_xid,
					    rc != 0 || rc1 != 0 ? NULL : &cos, 1);
	}

	D_CDEBUG(rc != 0 || rc1 != 0 || rc2 != 0, DLOG_ERR, DB_TRACE,
		 "Collectively commit DTX "DF_DTI" in "DF_UUID"/"DF_UUID": %d/%d/%d\n",
		 DP_DTI(&dce->dce_xid), DP_UUID(cont->sc_pool_uuid), DP_UUID(cont->sc_uuid),
		 rc, rc1, rc2);

	return rc != 0 ? rc : rc1 != 0 ? rc1 : rc2;
}

int
dtx_coll_abort(struct ds_cont_child *cont, struct dtx_coll_entry *dce, daos_epoch_t epoch)
{
	struct dtx_coll_rpc_args	 dcra = { 0 };
	int				*results = NULL;
	int				 len;
	int				 rc = 0;
	int				 rc1 = 0;
	int				 rc2 = 0;
	int				 i;

	if (dce->dce_ranks != NULL)
		rc = dtx_coll_rpc_prep(cont, dce, DTX_COLL_ABORT, epoch, &dcra);

	/*
	 * NOTE: The DTX abort maybe triggered by dtx_leader_end() for timeout on some DTX
	 *	 participant(s). Under such case, the client side RPC sponsor may also hit
	 *	 the RPC timeout and resends related RPC to the leader. Here, to avoid DTX
	 *	 abort and resend RPC forwarding being executed in parallel, we will abort
	 *	 local DTX after remote done, before that the logic of handling resent RPC
	 *	 on server will find the local pinned DTX entry then notify related client
	 *	 to resend sometime later.
	 */
	if (dce->dce_bitmap != NULL) {
		clrbit(dce->dce_bitmap, dss_get_module_info()->dmi_tgt_id);
		len = dtx_coll_local_exec(cont->sc_pool_uuid, cont->sc_uuid, &dce->dce_xid, epoch,
					  DTX_COLL_ABORT, dce->dce_bitmap_sz, dce->dce_bitmap,
					  &results);
		if (len < 0) {
			rc1 = len;
		} else {
			D_ASSERT(results != NULL);
			for (i = 0; i < len; i++) {
				if (results[i] < 0 && results[i] != -DER_NONEXIST && rc1 == 0)
					rc1 = results[i];
			}
		}
		D_FREE(results);
	}

	if (dce->dce_ranks != NULL) {
		rc = dtx_coll_rpc_post(&dcra, rc);
		if (rc > 0 || rc == -DER_NONEXIST || rc == -DER_EXCLUDED || rc == -DER_OOG)
			rc = 0;
	}

	if (epoch != 0)
		rc2 = vos_dtx_abort(cont->sc_hdl, &dce->dce_xid, epoch);
	else
		rc2 = vos_dtx_set_flags(cont->sc_hdl, &dce->dce_xid, 1, DTE_CORRUPTED);
	if (rc2 > 0 || rc2 == -DER_NONEXIST)
		rc2 = 0;

	D_CDEBUG(rc != 0 || rc1 != 0 || rc2 != 0, DLOG_ERR, DB_TRACE,
		 "Collectively abort DTX "DF_DTI" with epoch "DF_X64" in "
		 DF_UUID"/"DF_UUID": %d/%d/%d\n", DP_DTI(&dce->dce_xid), epoch,
		 DP_UUID(cont->sc_pool_uuid), DP_UUID(cont->sc_uuid), rc, rc1, rc2);

	return rc != 0 ? rc : rc1 != 0 ? rc1 : rc2;
}

int
dtx_coll_check(struct ds_cont_child *cont, struct dtx_coll_entry *dce, daos_epoch_t epoch)
{
	struct dtx_coll_rpc_args	 dcra = { 0 };
	int				*results = NULL;
	int				 len;
	int				 rc = 0;
	int				 rc1 = 0;
	int				 i;

	/*
	 * If no other target, then current target is the unique
	 * one and 'prepared', then related DTX can be committed.
	 */
	if (unlikely(dce->dce_ranks == NULL && dce->dce_bitmap == NULL))
		return DTX_ST_PREPARED;

	if (dce->dce_ranks != NULL)
		rc = dtx_coll_rpc_prep(cont, dce, DTX_COLL_CHECK, epoch, &dcra);

	if (dce->dce_bitmap != NULL) {
		len = dtx_coll_local_exec(cont->sc_pool_uuid, cont->sc_uuid, &dce->dce_xid, epoch,
					  DTX_COLL_CHECK, dce->dce_bitmap_sz, dce->dce_bitmap,
					  &results);
		if (len < 0) {
			rc1 = len;
		} else {
			D_ASSERT(results != NULL);
			for (i = 0; i < len; i++) {
				if (isset(dce->dce_bitmap, i))
					dtx_merge_check_result(&rc1, results[i]);
			}
		}
		D_FREE(results);
	}

	if (dce->dce_ranks != NULL) {
		rc = dtx_coll_rpc_post(&dcra, rc);
		if (dce->dce_bitmap != NULL)
			dtx_merge_check_result(&rc, rc1);
	}

	D_CDEBUG((rc < 0 && rc != -DER_NONEXIST) || (rc1 < 0 && rc1 != -DER_NONEXIST), DLOG_ERR,
		 DB_TRACE, "Collectively check DTX "DF_DTI" in "DF_UUID"/"DF_UUID": %d/%d/\n",
		 DP_DTI(&dce->dce_xid), DP_UUID(cont->sc_pool_uuid), DP_UUID(cont->sc_uuid),
		 rc, rc1);

	return dce->dce_ranks != NULL  ? rc : rc1;
}
