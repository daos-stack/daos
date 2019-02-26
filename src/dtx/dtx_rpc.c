/**
 * (C) Copyright 2019 Intel Corporation.
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
#include <daos/placement.h>
#include <daos/btree_class.h>
#include <daos_srv/vos.h>
#include <daos_srv/dtx_srv.h>
#include <daos_srv/container.h>
#include <daos_srv/daos_server.h>
#include "dtx_internal.h"

static int
crt_proc_struct_daos_tx_id(crt_proc_t proc, struct daos_tx_id *dti)
{
	int rc;

	rc = crt_proc_uuid_t(proc, &dti->dti_uuid);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &dti->dti_sec);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

CRT_RPC_DEFINE(dtx, DAOS_ISEQ_DTX, DAOS_OSEQ_DTX);

#define X(a, b, c, d, e)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
}

static struct crt_proto_rpc_format dtx_proto_rpc_fmt[] = {
	DTX_PROTO_SRV_RPC_LIST,
};

#undef X

struct crt_proto_format dtx_proto_fmt = {
	.cpf_name  = "dtx-proto",
	.cpf_ver   = DAOS_DTX_VERSION,
	.cpf_count = ARRAY_SIZE(dtx_proto_rpc_fmt),
	.cpf_prf   = dtx_proto_rpc_fmt,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_DTX_MODULE, 0)
};

static int
dtx_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep,
	       crt_opcode_t opc, crt_rpc_t **req)
{
	crt_opcode_t	opcode;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_DTX_MODULE, DAOS_DTX_VERSION);
	/* call daos_rpc_tag to get the target tag/context idx */
	tgt_ep->ep_tag = daos_rpc_tag(DAOS_REQ_DTX, tgt_ep->ep_tag);

	return crt_req_create(crt_ctx, tgt_ep, opcode, req);
}

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
 * Also used as the sub-request (to related rank/tag) args.
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
	int				 drr_result; /* The sub-req result */
	struct daos_tx_id		*drr_dti; /* The DTX array */
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
	struct daos_tx_id		*dcrb_dti;
	/* The number of DTXs to be classified that will be used as
	 * the dtx_req_rec::drr_dti array size when allocating it.
	 */
	int				 dcrb_count;
};

static void
dtx_sub_req_cb(const struct crt_cb_info *cb_info)
{
	crt_rpc_t		*req = cb_info->cci_rpc;
	struct dtx_req_rec	*drr = cb_info->cci_arg;
	struct dtx_req_args	*dra = drr->drr_parent;
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
		"DTX req for opc %x got reply from %d/%d: rc = %d.\n",
		dra->dra_opc, drr->drr_rank, drr->drr_tag, rc);
}

static int
dtx_sub_req_send(struct dtx_req_rec *drr)
{
	struct dtx_req_args	*dra = drr->drr_parent;
	crt_rpc_t		*req;
	crt_endpoint_t		 tgt_ep;
	int			 rc;

	tgt_ep.ep_grp = NULL;
	tgt_ep.ep_rank = drr->drr_rank;
	tgt_ep.ep_tag = drr->drr_tag;

	rc = dtx_req_create(dss_get_module_info()->dmi_ctx, &tgt_ep,
			    dra->dra_opc, &req);
	if (rc == 0) {
		struct dtx_in	*din;

		din = crt_req_get(req);
		uuid_copy(din->di_po_uuid, dra->dra_po_uuid);
		uuid_copy(din->di_co_uuid, dra->dra_co_uuid);
		din->di_dtx_array.ca_count = drr->drr_count;
		din->di_dtx_array.ca_arrays = drr->drr_dti;

		rc = crt_req_send(req, dtx_sub_req_cb, drr);
		if (rc != 0)
			crt_req_decref(req);
	}

	D_DEBUG(DB_TRACE, "DTX req for opc %x sent: rc = %d.\n",
		dra->dra_opc, rc);

	if (rc != 0) {
		drr->drr_result = rc;
		ABT_future_set(dra->dra_future, drr);
	}

	return rc;
}

static void
dtx_req_cb(void **args)
{
	struct dtx_req_rec	*drr = args[0];
	struct dtx_req_args	*dra = drr->drr_parent;
	int			 i;

	if (dra->dra_opc == DTX_CHECK) {
		for (i = 0; i < dra->dra_length; i++) {
			drr = args[i];
			switch (drr->drr_result) {
			case DTX_ST_INIT:
				dra->dra_result = DTX_ST_INIT;
				break;
			case DTX_ST_COMMITTED:
				dra->dra_result = DTX_ST_COMMITTED;
				/* As long as one replica has committed the DTX,
				 * then the DTX is committable on all replicas.
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
				if (dra->dra_result != DTX_ST_INIT)
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
			if (dra->dra_result == 0)
				dra->dra_result = drr->drr_result;

			if (dra->dra_result != 0) {
				D_ERROR("DTX req for opc %x failed: rc = %d.\n",
					dra->dra_opc, dra->dra_result);
				return;
			}
		}

		D_DEBUG(DB_TRACE, "DTX req for opc %x succeed.\n",
			dra->dra_opc);
	}
}

static int
dtx_req_send(crt_opcode_t opc, d_list_t *head, int length, uuid_t po_uuid,
	     uuid_t co_uuid)
{
	ABT_future		 future;
	struct dtx_req_args	 dra;
	struct dtx_req_rec	*drr;
	int			 rc;

	dra.dra_opc = opc;
	uuid_copy(dra.dra_po_uuid, po_uuid);
	uuid_copy(dra.dra_co_uuid, co_uuid);
	dra.dra_list = head;
	dra.dra_length = length;
	dra.dra_result = 0;

	rc = ABT_future_create(length, dtx_req_cb, &future);
	if (rc != ABT_SUCCESS) {
		D_ERROR("ABT_future_create failed for opc %x, length = %d: "
			"rc = %d.\n", opc, length, rc);
		return dss_abterr2der(rc);
	}

	dra.dra_future = future;
	d_list_for_each_entry(drr, head, drr_link) {
		drr->drr_parent = &dra;
		drr->drr_result = 0;
		rc = dtx_sub_req_send(drr);
		if (rc != 0)
			break;
	}

	if (rc != 0) {
		while (drr->drr_link.next != head) {
			drr = d_list_entry(drr->drr_link.next,
					   struct dtx_req_rec, drr_link);
			drr->drr_parent = &dra;
			drr->drr_result = rc;
			ABT_future_set(future, drr);
		}
	}

	rc = ABT_future_wait(future);
	D_ASSERTF(rc == ABT_SUCCESS,
		  "ABT_future_wait failed for opc %x, length = %d: rc = %d.\n",
		  opc, length, rc);

	ABT_future_free(&future);
	rc = dra.dra_result;

	D_DEBUG(DB_TRACE, "DTX req for opc %x: rc = %d\n", opc, rc);

	return rc;
}

static int
dtx_cf_rec_alloc(struct btr_instance *tins, daos_iov_t *key_iov,
		 daos_iov_t *val_iov, struct btr_record *rec)
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

	rec->rec_mmid = umem_ptr2id(&tins->ti_umm, drr);
	return 0;
}

static int
dtx_cf_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct dtx_req_rec	*drr;

	D_ASSERT(tins->ti_umm.umm_id == UMEM_CLASS_VMEM);

	drr = (struct dtx_req_rec *)umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	d_list_del(&drr->drr_link);
	D_FREE(drr->drr_dti);
	D_FREE_PTR(drr);

	return 0;
}

static int
dtx_cf_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
		 daos_iov_t *key_iov, daos_iov_t *val_iov)
{
	D_ASSERTF(0, "We should not come here.\n");
	return 0;
}

static int
dtx_cf_rec_update(struct btr_instance *tins, struct btr_record *rec,
		  daos_iov_t *key, daos_iov_t *val)
{
	struct dtx_req_rec		*drr;
	struct dtx_cf_rec_bundle	*dcrb;

	drr = (struct dtx_req_rec *)umem_id2ptr(&tins->ti_umm, rec->rec_mmid);
	dcrb = (struct dtx_cf_rec_bundle *)val->iov_buf;
	drr->drr_dti[drr->drr_count++] = *dcrb->dcrb_dti;

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
dtx_dti_classify_one(struct ds_pool *pool, uuid_t po_uuid, uuid_t co_uuid,
		     daos_handle_t tree, d_list_t *head, int *length,
		     daos_unit_oid_t *oid, struct daos_tx_id *dti,
		     int count, uint32_t version)
{
	struct daos_oclass_attr		*oc_attr;
	struct pl_obj_layout		*layout = NULL;
	struct daos_obj_md		 md = { 0 };
	struct dtx_cf_rec_bundle	 dcrb;
	d_rank_t			 myrank;
	uint32_t			 replicas;
	int				 start;
	int				 rc;
	int				 i;

	oc_attr = daos_oclass_attr_find(oid->id_pub);
	if (oc_attr->ca_resil != DAOS_RES_REPL)
		return -DER_NOTAPPLICABLE;

	md.omd_id = oid->id_pub;
	md.omd_ver = version;

	ABT_rwlock_rdlock(pool->sp_lock);
	D_ASSERT(pool->sp_map != NULL);
	D_ASSERT(pool->sp_pl_map != NULL);

	rc = pl_obj_place(pool->sp_pl_map, &md, NULL, &layout);
	ABT_rwlock_unlock(pool->sp_lock);
	if (rc != 0)
		return rc;

	if (layout->ol_nr <= oid->id_shard)
		D_GOTO(out, rc = -DER_INVAL);

	replicas = oc_attr->u.repl.r_num;
	if (replicas == DAOS_OBJ_REPL_MAX)
		replicas = layout->ol_nr;

	if (replicas < 1)
		D_GOTO(out, rc = -DER_INVAL);

	/* Skip single replicated object. */
	if (replicas == 1)
		D_GOTO(out, rc = 0);

	dcrb.dcrb_count = count;
	dcrb.dcrb_dti = dti;
	dcrb.dcrb_head = head;
	dcrb.dcrb_length = length;

	crt_group_rank(pool->sp_group, &myrank);
	start = (oid->id_shard / replicas) * replicas;
	for (i = start; i < start + replicas && rc == 0; i++) {
		struct pl_obj_shard	*shard;
		struct pool_target	*target;
		daos_iov_t		 kiov;
		daos_iov_t		 riov;

		/* skip unavailable replica(s). */
		shard = &layout->ol_shards[i];
		if (shard->po_target == -1 || shard->po_rebuilding)
			continue;

		rc = pool_map_find_target(pool->sp_map, shard->po_target,
					  &target);
		if (rc != 1)
			D_GOTO(out, rc = -DER_INVAL);

		/* skip myself. */
		if (myrank == target->ta_comp.co_rank)
			continue;

		dcrb.dcrb_rank = target->ta_comp.co_rank;
		dcrb.dcrb_tag = target->ta_comp.co_index;

		daos_iov_set(&riov, &dcrb, sizeof(dcrb));
		daos_iov_set(&kiov, &dcrb.dcrb_key, sizeof(dcrb.dcrb_key));
		rc = dbtree_upsert(tree, BTR_PROBE_EQ, DAOS_INTENT_UPDATE,
				   &kiov, &riov);
	}

out:
	if (layout != NULL)
		pl_obj_layout_free(layout);
	return rc;
}

static int
dtx_dti_classify(uuid_t po_uuid, uuid_t co_uuid, daos_handle_t tree,
		 struct daos_tx_entry *dtes, int count, uint32_t version,
		 d_list_t *head, struct daos_tx_id **dtis)
{
	struct daos_tx_id	*dti = NULL;
	struct ds_pool		*pool;
	int			 length = 0;
	int			 rc = 0;
	int			 i;

	pool = ds_pool_lookup(po_uuid);
	if (pool == NULL)
		return -DER_INVAL;

	D_ALLOC_ARRAY(dti, count);
	if (dti == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < count; i++) {
		rc = dtx_dti_classify_one(pool, po_uuid, co_uuid, tree, head,
					  &length, &dtes[i].dte_oid,
					  &dtes[i].dte_xid, count, version);
		if (rc < 0)
			break;

		dti[i] = dtes[i].dte_xid;
	}

out:
	if (rc >= 0)
		*dtis = dti;
	else if (dti != NULL)
		D_FREE(dti);

	ds_pool_put(pool);
	return rc < 0 ? rc : length;
}

int
dtx_commit(uuid_t po_uuid, uuid_t co_uuid, struct daos_tx_entry *dtes,
	   int count, uint32_t version)
{
	struct ds_cont		*cont = NULL;
	struct daos_tx_id	*dti = NULL;
	struct umem_attr	 uma;
	struct btr_root		 tree_root = { 0 };
	daos_handle_t		 tree_hdl = DAOS_HDL_INVAL;
	d_list_t		 head;
	int			 length;
	int			 rc;
	int			 rc1 = 0;

	rc = ds_cont_lookup(po_uuid, co_uuid, &cont);
	if (rc != 0)
		return rc;

	D_INIT_LIST_HEAD(&head);
	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
	rc = dbtree_create_inplace(DBTREE_CLASS_DTX_CF, 0, DTX_CF_BTREE_ORDER,
				   &uma, &tree_root, &tree_hdl);
	if (rc != 0)
		D_GOTO(out, rc);

	length = dtx_dti_classify(po_uuid, co_uuid, tree_hdl, dtes, count,
				  version, &head, &dti);
	if (length < 0)
		D_GOTO(out, rc = length);

	if (!d_list_empty(&head))
		rc = dtx_req_send(DTX_COMMIT, &head, length, po_uuid, co_uuid);

	if (dti != NULL)
		/* We cannot rollback the commit, so commit locally anyway. */
		rc1 = vos_dtx_commit(cont->sc_hdl, dti, count);

out:
	if (dti != NULL)
		D_FREE(dti);

	if (!daos_handle_is_inval(tree_hdl))
		dbtree_destroy(tree_hdl);

	D_ASSERT(d_list_empty(&head));

	if (cont != NULL)
		ds_cont_put(cont);

	return rc >= 0 ? rc1 : rc;
}

int
dtx_abort(uuid_t po_uuid, uuid_t co_uuid, struct daos_tx_entry *dtes,
	  int count, uint32_t version)
{
	struct ds_cont		*cont = NULL;
	struct daos_tx_id	*dti = NULL;
	struct umem_attr	 uma;
	struct btr_root		 tree_root = { 0 };
	daos_handle_t		 tree_hdl = DAOS_HDL_INVAL;
	d_list_t		 head;
	int			 length;
	int			 rc;

	/* Currently we only support to abort DTXs one by one. */
	D_ASSERT(count == 1);

	rc = ds_cont_lookup(po_uuid, co_uuid, &cont);
	if (rc != 0)
		return rc;

	D_INIT_LIST_HEAD(&head);
	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
	rc = dbtree_create_inplace(DBTREE_CLASS_DTX_CF, 0, DTX_CF_BTREE_ORDER,
				   &uma, &tree_root, &tree_hdl);
	if (rc != 0)
		D_GOTO(out, rc);

	length = dtx_dti_classify(po_uuid, co_uuid, tree_hdl, dtes, count,
				  version, &head, &dti);
	if (length < 0)
		D_GOTO(out, rc = length);

	D_ASSERT(dti != NULL);

	/* Local abort firstly. */
	rc = vos_dtx_abort(cont->sc_hdl, dti, count, false);
	if (rc != 0)
		D_GOTO(out, rc);

	if (!d_list_empty(&head))
		rc = dtx_req_send(DTX_ABORT, &head, length, po_uuid, co_uuid);

out:
	if (dti != NULL)
		D_FREE(dti);

	if (!daos_handle_is_inval(tree_hdl))
		dbtree_destroy(tree_hdl);

	D_ASSERT(d_list_empty(&head));

	if (cont != NULL)
		ds_cont_put(cont);

	return rc == -DER_NONEXIST ? 0 : rc;
}

int
dtx_check(uuid_t po_uuid, uuid_t co_uuid, struct daos_tx_entry *dte,
	  struct pl_obj_layout *layout)
{
	struct ds_pool		*pool;
	struct daos_oclass_attr *oc_attr;
	daos_unit_oid_t		*oid = &dte->dte_oid;
	struct dtx_req_rec	*drr;
	struct dtx_req_rec	*next;
	d_list_t		 head;
	d_rank_t		 myrank;
	uint32_t		 replicas;
	int			 length = 0;
	int			 start;
	int			 rc = 0;
	int			 i;

	if (layout->ol_nr <= oid->id_shard)
		return -DER_INVAL;

	oc_attr = daos_oclass_attr_find(oid->id_pub);
	if (oc_attr->ca_resil != DAOS_RES_REPL)
		return -DER_NOTAPPLICABLE;

	replicas = oc_attr->u.repl.r_num;
	if (replicas == DAOS_OBJ_REPL_MAX)
		replicas = layout->ol_nr;

	if (replicas < 1)
		return -DER_INVAL;

	/* If no other replica, then currnet replica is the unique
	 * one that can be committed if it is 'prepared'.
	 */
	if (replicas == 1)
		return DTX_ST_PREPARED;

	pool = ds_pool_lookup(po_uuid);
	if (pool == NULL)
		return -DER_INVAL;

	D_INIT_LIST_HEAD(&head);
	crt_group_rank(pool->sp_group, &myrank);
	start = (oid->id_shard / replicas) * replicas;
	for (i = start; i < start + replicas; i++) {
		struct pl_obj_shard	*shard;
		struct pool_target	*target;

		/* skip unavailable replica(s). */
		shard = &layout->ol_shards[i];
		if (shard->po_target == -1 || shard->po_rebuilding)
			continue;

		rc = pool_map_find_target(pool->sp_map, shard->po_target,
					  &target);
		if (rc != 1) {
			if (rc >= 0)
				rc = -DER_INVAL;
			goto out;
		}

		/* skip myself. */
		if (myrank == target->ta_comp.co_rank)
			continue;

		D_ALLOC_PTR(drr);
		if (drr == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		drr->drr_rank = target->ta_comp.co_rank;
		drr->drr_tag = target->ta_comp.co_index;
		drr->drr_count = 1;
		drr->drr_dti = &dte->dte_xid;
		d_list_add_tail(&drr->drr_link, &head);
		length++;
	}

	/* If no other available replicas, then currnet replica is the
	 * unique valid one, it can be committed if it is also 'prepared'.
	 */
	if (d_list_empty(&head))
		rc = DTX_ST_PREPARED;
	else
		rc = dtx_req_send(DTX_CHECK, &head, length, po_uuid, co_uuid);

out:
	d_list_for_each_entry_safe(drr, next, &head, drr_link) {
		d_list_del(&drr->drr_link);
		D_FREE_PTR(drr);
	}

	ds_pool_put(pool);
	return rc;
}
