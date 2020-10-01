/**
 * (C) Copyright 2019-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
#include <daos_srv/daos_server.h>
#include "dtx_internal.h"

CRT_RPC_DEFINE(dtx, DAOS_ISEQ_DTX, DAOS_OSEQ_DTX);

#define X_RPC(a, b, c, d, e)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
}

static struct crt_proto_rpc_format dtx_proto_rpc_fmt[] = {
	DTX_PROTO_SRV_RPC_LIST(X_RPC)
};

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
	/* Pointer to the global list head for all the dtx_req_rec. */
	d_list_t			*dra_list;
	/* The length of aobve global list. */
	int				 dra_length;
	/* The collective RPC result. */
	int				 dra_result;
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
	struct dtx_id			*drr_dti; /* The DTX array */
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

static void
dtx_req_cb(const struct crt_cb_info *cb_info)
{
	crt_rpc_t		*req = cb_info->cci_rpc;
	struct dtx_req_rec	*drr = cb_info->cci_arg;
	struct dtx_req_args	*dra = drr->drr_parent;
	struct dtx_in		*din = crt_req_get(req);
	struct dtx_out		*dout;
	int			 rc = cb_info->cci_rc;

	if (rc == 0) {
		dout = crt_reply_get(req);
		rc = dout->do_status;
	}

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
	tgt_ep.ep_tag = daos_rpc_tag(DAOS_REQ_DTX, drr->drr_tag);
	opc = DAOS_RPC_OPCODE(dra->dra_opc, DAOS_DTX_MODULE, DAOS_DTX_VERSION);

	rc = crt_req_create(dss_get_module_info()->dmi_ctx, &tgt_ep, opc, &req);
	if (rc == 0) {
		din = crt_req_get(req);
		uuid_copy(din->di_po_uuid, dra->dra_po_uuid);
		uuid_copy(din->di_co_uuid, dra->dra_co_uuid);
		din->di_epoch = epoch;
		din->di_dtx_array.ca_count = drr->drr_count;
		din->di_dtx_array.ca_arrays = drr->drr_dti;

		rc = crt_req_send(req, dtx_req_cb, drr);
		if (rc != 0)
			crt_req_decref(req);
	}

	D_DEBUG(DB_TRACE, "DTX req for opc %x to %d/%d (req %p future %p) sent "
		"epoch "DF_X64" : rc %d.\n", dra->dra_opc, drr->drr_rank,
		drr->drr_tag, req, dra->dra_future,
		din != NULL ? din->di_epoch : 0, rc);

	if (rc != 0) {
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
				dra->dra_result = DTX_ST_COMMITTED;
				/* As long as one target has committed the DTX,
				 * then the DTX is committable on all targets.
				 */
				D_DEBUG(DB_TRACE,
					"The DTX "DF_DTI" has been committed "
					"on %d/%d.\n", DP_DTI(drr->drr_dti),
					drr->drr_rank, drr->drr_tag);
				return;
			case DTX_ST_PREPARED:
				if (dra->dra_result == 0)
					dra->dra_result = DTX_ST_PREPARED;
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
			if ((drr->drr_result < 0) &&
			    (dra->dra_result == 0 ||
			     dra->dra_result == -DER_NONEXIST))
				dra->dra_result = drr->drr_result;
		}

		drr = args[0];
		D_CDEBUG(dra->dra_result < 0, DLOG_ERR, DB_TRACE,
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
dtx_req_list_send(struct dtx_req_args *dra, crt_opcode_t opc, d_list_t *head,
		  int len, uuid_t po_uuid, uuid_t co_uuid, daos_epoch_t epoch)
{
	ABT_future		 future;
	struct dtx_req_rec	*drr;
	int			 rc;
	int			 i = 0;

	dra->dra_opc = opc;
	uuid_copy(dra->dra_po_uuid, po_uuid);
	uuid_copy(dra->dra_co_uuid, co_uuid);
	dra->dra_list = head;
	dra->dra_length = len;
	dra->dra_result = 0;

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

		i++;
	}

	return 0;
}

static int
dtx_cf_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov,
		 d_iov_t *val_iov, struct btr_record *rec)
{
	struct dtx_req_rec		*drr;
	struct dtx_cf_rec_bundle	*dcrb;

	D_ASSERT(tins->ti_umm.umm_id == UMEM_CLASS_VMEM);

	D_ALLOC_PTR(drr);
	if (drr == NULL)
		return -DER_NOMEM;

	dcrb = (struct dtx_cf_rec_bundle *)val_iov->iov_buf;
	D_ALLOC_ARRAY(drr->drr_dti, dcrb->dcrb_count);
	if (drr->drr_dti == NULL) {
		D_FREE_PTR(drr);
		return -DER_NOMEM;
	}

	drr->drr_rank = dcrb->dcrb_rank;
	drr->drr_tag = dcrb->dcrb_tag;
	drr->drr_count = 1;
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
	D_FREE(drr->drr_dti);
	D_FREE_PTR(drr);

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
		  d_iov_t *key, d_iov_t *val)
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
dtx_dti_classify_one(struct ds_pool *pool, daos_handle_t tree, d_list_t *head,
		     int *length, struct dtx_entry *dte, int count)
{
	struct dtx_memberships		*mbs = dte->dte_mbs;
	struct dtx_cf_rec_bundle	 dcrb;
	d_rank_t			 myrank;
	int				 rc = 0;
	int				 i;

	if (mbs->dm_tgt_cnt == 0)
		return -DER_INVAL;

	dcrb.dcrb_count = count;
	dcrb.dcrb_dti = &dte->dte_xid;
	dcrb.dcrb_head = head;
	dcrb.dcrb_length = length;

	crt_group_rank(NULL, &myrank);
	for (i = 0; i < mbs->dm_tgt_cnt && rc >= 0; i++) {
		struct pool_target	*target;
		d_iov_t			 kiov;
		d_iov_t			 riov;

		rc = pool_map_find_target(pool->sp_map,
					  mbs->dm_tgts[i].ddt_id, &target);
		D_ASSERT(rc == 1);

		/* Skip the target that (re-)joined the system after the DTX. */
		if (target->ta_comp.co_ver > dte->dte_ver)
			continue;

		/* Skip non-healthy one. */
		if (target->ta_comp.co_status != PO_COMP_ST_UP &&
		    target->ta_comp.co_status != PO_COMP_ST_UPIN)
			continue;

		/* skip myself. */
		if (myrank == target->ta_comp.co_rank &&
		    dss_get_module_info()->dmi_tgt_id ==
		    target->ta_comp.co_index)
			continue;

		dcrb.dcrb_rank = target->ta_comp.co_rank;
		dcrb.dcrb_tag = target->ta_comp.co_index;

		d_iov_set(&riov, &dcrb, sizeof(dcrb));
		d_iov_set(&kiov, &dcrb.dcrb_key, sizeof(dcrb.dcrb_key));
		rc = dbtree_upsert(tree, BTR_PROBE_EQ, DAOS_INTENT_UPDATE,
				   &kiov, &riov);
	}

	return rc > 0 ? 0 : rc;
}

static int
dtx_dti_classify(uuid_t po_uuid, daos_handle_t tree, struct dtx_entry **dtes,
		 int count, d_list_t *head, struct dtx_id **dtis)
{
	struct dtx_id		*dti = NULL;
	struct ds_pool		*pool;
	int			 length = 0;
	int			 rc = 0;
	int			 i;

	pool = ds_pool_lookup(po_uuid);
	if (pool == NULL)
		return -DER_INVAL;

	D_ALLOC_ARRAY(dti, count);
	if (dti != NULL) {
		for (i = 0; i < count; i++) {
			rc = dtx_dti_classify_one(pool, tree, head, &length,
						  dtes[i], count);
			if (rc < 0)
				break;

			dti[i] = dtes[i]->dte_xid;
		}

		if (rc >= 0)
			*dtis = dti;
		else if (dti != NULL)
			D_FREE(dti);
	} else {
		rc = -DER_NOMEM;
	}

	ds_pool_put(pool);
	return rc < 0 ? rc : length;
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
dtx_commit(uuid_t po_uuid, uuid_t co_uuid, struct dtx_entry **dtes,
	   int count, bool drop_cos)
{
	struct dtx_req_args	 dra;
	struct ds_cont_child	*cont = NULL;
	struct dtx_id		*dti = NULL;
	struct dtx_cos_key	*dcks = NULL;
	struct umem_attr	 uma;
	struct btr_root		 tree_root = { 0 };
	daos_handle_t		 tree_hdl = DAOS_HDL_INVAL;
	d_list_t		 head;
	int			 length;
	int			 rc;
	int			 rc1 = 0;
	int			 rc2 = 0;

	rc = ds_cont_child_lookup(po_uuid, co_uuid, &cont);
	if (rc != 0)
		return rc;

	D_INIT_LIST_HEAD(&head);
	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
	rc = dbtree_create_inplace(DBTREE_CLASS_DTX_CF, 0, DTX_CF_BTREE_ORDER,
				   &uma, &tree_root, &tree_hdl);
	if (rc != 0)
		goto out;

	length = dtx_dti_classify(po_uuid, tree_hdl, dtes, count, &head, &dti);
	if (length < 0)
		D_GOTO(out, rc = length);

	dra.dra_future = ABT_FUTURE_NULL;
	if (!d_list_empty(&head)) {
		rc = dtx_req_list_send(&dra, DTX_COMMIT, &head, length, po_uuid,
				       co_uuid, crt_hlc_get());
		if (rc != 0)
			goto out;
	}

	if (drop_cos) {
		D_ALLOC_ARRAY(dcks, count);
		if (dcks == NULL)
			D_GOTO(out, rc1 = -DER_NOMEM);
	}

	rc1 = vos_dtx_commit(cont->sc_hdl, dti, count, dcks);

	/* -DER_NONEXIST may be caused by race or repeated commit, ignore it. */
	if (rc1 == -DER_NONEXIST)
		rc1 = 0;

	if (rc1 > 0 && drop_cos) {
		int	i;

		for (i = 0; i < rc1; i++) {
			if (!daos_oid_is_null(dcks[i].oid.id_pub))
				dtx_del_cos(cont, &dti[i], &dcks[i].oid,
					    dcks[i].dkey_hash);
		}
	}

	D_FREE(dcks);

	if (dra.dra_future != ABT_FUTURE_NULL) {
		rc2 = dtx_req_wait(&dra);
		if (rc2 == -DER_NONEXIST)
			rc2 = 0;
	}

out:
	D_CDEBUG(rc < 0 || rc1 < 0 || rc2 < 0, DLOG_ERR, DB_TRACE,
		 "Commit DTXs "DF_DTI", count %d: rc %d %d %d\n",
		 DP_DTI(&dtes[0]->dte_xid), count, rc, rc1, rc2);

	D_FREE(dti);

	if (!daos_handle_is_inval(tree_hdl))
		dbtree_destroy(tree_hdl, NULL);

	D_ASSERT(d_list_empty(&head));

	if (cont != NULL)
		ds_cont_child_put(cont);

	return rc < 0 ? rc : (rc1 < 0 ? rc1 : (rc2 < 0 ? rc2 : 0));
}

int
dtx_abort(uuid_t po_uuid, uuid_t co_uuid, daos_epoch_t epoch,
	  struct dtx_entry **dtes, int count)
{
	struct dtx_req_args	 dra;
	struct ds_cont_child	*cont = NULL;
	struct dtx_id		*dti = NULL;
	struct umem_attr	 uma;
	struct btr_root		 tree_root = { 0 };
	daos_handle_t		 tree_hdl = DAOS_HDL_INVAL;
	d_list_t		 head;
	int			 length;
	int			 rc;

	rc = ds_cont_child_lookup(po_uuid, co_uuid, &cont);
	if (rc != 0)
		return rc;

	D_INIT_LIST_HEAD(&head);
	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
	rc = dbtree_create_inplace(DBTREE_CLASS_DTX_CF, 0, DTX_CF_BTREE_ORDER,
				   &uma, &tree_root, &tree_hdl);
	if (rc != 0)
		goto out;

	length = dtx_dti_classify(po_uuid, tree_hdl, dtes, count, &head, &dti);
	if (length < 0)
		D_GOTO(out, rc = length);

	D_ASSERT(dti != NULL);

	/* Local abort firstly. */
	rc = vos_dtx_abort(cont->sc_hdl, epoch, dti, count);
	if (rc > 0 || rc == -DER_NONEXIST)
		rc = 0;

	if (rc == 0 && !d_list_empty(&head)) {
		rc = dtx_req_list_send(&dra, DTX_ABORT, &head, length, po_uuid,
				       co_uuid, epoch);
		if (rc != 0)
			goto out;

		rc = dtx_req_wait(&dra);
		if (rc == -DER_NONEXIST)
			rc = 0;
	}

out:
	D_CDEBUG(rc != 0, DLOG_ERR, DB_TRACE,
		 "Abort DTXs "DF_DTI", count %d: rc %d\n",
		 DP_DTI(&dtes[0]->dte_xid), count, rc);

	D_FREE(dti);

	if (!daos_handle_is_inval(tree_hdl))
		dbtree_destroy(tree_hdl, NULL);

	D_ASSERT(d_list_empty(&head));

	if (cont != NULL)
		ds_cont_child_put(cont);

	return rc < 0 ? rc : 0;
}

int
dtx_check(uuid_t po_uuid, uuid_t co_uuid, struct dtx_entry *dte)
{
	struct dtx_req_args	 dra;
	struct dtx_memberships	*mbs = dte->dte_mbs;
	struct ds_pool		*pool;
	struct dtx_req_rec	*drr;
	struct dtx_req_rec	*next;
	d_list_t		 head;
	d_rank_t		 myrank;
	int			 length = 0;
	int			 rc = 0;
	int			 i;

	if (mbs->dm_tgt_cnt == 0)
		return -DER_INVAL;

	/* If no other target, then current target is the unique
	 * one that can be committed if it is 'prepared'.
	 */
	if (mbs->dm_tgt_cnt == 1)
		return DTX_ST_PREPARED;

	pool = ds_pool_lookup(po_uuid);
	if (pool == NULL)
		return -DER_INVAL;

	D_INIT_LIST_HEAD(&head);
	crt_group_rank(NULL, &myrank);
	for (i = 0; i < mbs->dm_tgt_cnt; i++) {
		struct pool_target	*target;

		rc = pool_map_find_target(pool->sp_map,
					  mbs->dm_tgts[i].ddt_id, &target);
		D_ASSERT(rc == 1);

		/* Skip the target that (re-)joined the system after the DTX. */
		if (target->ta_comp.co_ver > dte->dte_ver)
			continue;

		/* Skip non-healthy one. */
		if (target->ta_comp.co_status != PO_COMP_ST_UP &&
		    target->ta_comp.co_status != PO_COMP_ST_UPIN)
			continue;

		/* skip myself. */
		if (myrank == target->ta_comp.co_rank &&
		    dss_get_module_info()->dmi_tgt_id ==
		    target->ta_comp.co_index)
			continue;

		D_ALLOC_PTR(drr);
		if (drr == NULL) {
			rc = -DER_NOMEM;
			goto out;
		}

		drr->drr_rank = target->ta_comp.co_rank;
		drr->drr_tag = target->ta_comp.co_index;
		drr->drr_count = 1;
		drr->drr_dti = &dte->dte_xid;
		d_list_add_tail(&drr->drr_link, &head);
		length++;
	}

	/* If no other available targets, then current target is the
	 * unique valid one, it can be committed if it is also 'prepared'.
	 */
	if (d_list_empty(&head)) {
		rc = DTX_ST_PREPARED;
		goto out;
	}

	rc = dtx_req_list_send(&dra, DTX_CHECK, &head, length, po_uuid,
			       co_uuid, crt_hlc_get());
	if (rc == 0)
		rc = dtx_req_wait(&dra);

out:
	d_list_for_each_entry_safe(drr, next, &head, drr_link) {
		d_list_del(&drr->drr_link);
		D_FREE_PTR(drr);
	}

	ds_pool_put(pool);
	return rc;
}
