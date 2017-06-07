/**
 * (C) Copyright 2016 Intel Corporation.
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
/*
 * tier_fetch
 * Implements cross-tier fetching of objects and sub-objects (or will soon).
 **/
#define DD_SUBSYS	DD_FAC(tier)

#include <daos_types.h>
#include <daos_api.h>
#include <daos_srv/daos_server.h>
#include <daos/rpc.h>
#include "rpc.h"
#include "srv_internal.h"
#include <daos_srv/container.h>
#include <daos_srv/vos.h>
#include <daos_srv/pool.h>
#include <daos_task.h>
#include "../client/client_internal.h"
#include "../client/task_internal.h"

/* holds extents as returned by VOS. plus spc for a checksum */
struct tier_ext_rec {
	daos_iod_type_t	der_type;
	daos_size_t	der_rsize;
	daos_recx_t     der_rec;
	daos_iov_t      der_iov;
	daos_csum_buf_t der_ckrec;
};

#define NUM_BUNDLED_EXTS	2
#define NUM_BUNDLED_IODS	2

/* a buffer for collecting tier_ext_rec structs */
struct tier_ext_list {
	daos_list_t	      del_lh;
	struct tier_ext_rec   del_recs[NUM_BUNDLED_EXTS];
	int		      del_nrecs;
};

/* a buffer for collecting sg list info */
struct tier_sgiod_list {
	daos_list_t	      dsl_lh;
	daos_iov_t            dsl_iovs[NUM_BUNDLED_IODS];
	int		      dsl_niods;
};

/* buffer for collection daos_iod_t structs */
struct tier_vec_iod {
	daos_list_t	      dvi_lh;
	daos_iod_t	      dvi_viod;
};

#define tier_vec_iod_size(num_recs)   \
	(sizeof(struct tier_vec_iod) +   ((sizeof(daos_recx_t) \
					 + sizeof(daos_csum_buf_t) \
					 + sizeof(daos_epoch_range_t)) \
					 * num_recs))

/* dynamic array of daos_iod_t structs
 * Each instance of this struct conains the args needed
 * for calling daos_obj_update on the next tier
 */
struct tier_key_iod {
	daos_unit_oid_t	     dki_oid;
	daos_key_t	     dki_dkey;
	unsigned int	     dki_nr;
	/* array of daos_iod_t's */
	daos_iod_t	    *dki_iods;
	/* array of daos_sg_list_t's */
	daos_sg_list_t      *dki_sgs;
};
#define tier_key_iod_size(num_recs) \
	(sizeof(struct tier_key_iod) +   ((sizeof(daos_iod_t) \
					 + sizeof(daos_sg_list_t)) \
					 * num_recs))
#define DCTF_FLAG_ZC_ADDRS   (1 << 0)

/* context for enumeration callback functions -*/
struct tier_fetch_ctx {
	/* fetch parameters */
	uuid_t		     dfc_pool;
	daos_handle_t	     dfc_co;
	daos_epoch_t	     dfc_ev;
	int		     dfc_flags;
	/* working area */
	daos_unit_oid_t      dfc_oid;
	daos_key_t	     dfc_dkey;
	daos_key_t	     dfc_akey;
	unsigned int	     dfc_na;
	unsigned int	     dfc_ne;
	/* cross-tier godies */
	daos_handle_t	     dfc_eqh;
	daos_event_t	     dfc_evt;
	daos_event_t	    *dfc_evp;
	daos_handle_t	     dfc_oh;
	daos_handle_t	     dfc_coh;
	daos_handle_t	     dfc_ioh;
	struct daos_sched   *dfc_sched;
	/* list heads for collecting what to fetch */
	daos_list_t          dfc_head;
	daos_list_t          dfc_iods;
	daos_list_t	     dfc_dkios;
};
static int
tier_fetche(uuid_t pool, daos_handle_t coh, daos_epoch_t ev, uuid_t cid,
	    daos_handle_t wcoh);

static int tier_latch_oid(void *ctx, vos_iter_entry_t *ie);
static int tier_proc_obj(void *ctx, vos_iter_entry_t *ie);
static int tier_latch_dkey(void *ctx, vos_iter_entry_t *ie);
static int tier_proc_dkey(void *ctx, vos_iter_entry_t *ie);
static int tier_latch_akey(void *ctx, vos_iter_entry_t *ie);
static int tier_proc_akey(void *ctx, vos_iter_entry_t *ie);
static int tier_rec_cb(void *ctx, vos_iter_entry_t *ie);


/*
 * this callback occurs after receiving container is opened and an
 * epoch hold has completed. Callback latches and saves open
 * container handle
 */
static int
tf_cont_cb(struct daos_task *task, void *data)
{
	daos_handle_t **pc  = data;
	daos_handle_t *pcoh = *pc;
	daos_epoch_hold_t *args = daos_task_get_args(DAOS_OPC_EPOCH_HOLD, task);
	int rc = 0;

	D_DEBUG(DF_TIERS, "got "DF_X64"\n", args->coh.cookie);
	if (args == NULL) {
		D_ERROR("daos_task_get_args failed\n");
		rc = -DER_INVAL;
	} else
		*pcoh = args->coh;

	return rc;
}

/* commit held epoch and close receiving container */
static int
tf_cont_close(daos_handle_t coh, uuid_t cid, daos_epoch_t epoch)
{
	daos_cont_close_t	args2;
	daos_epoch_commit_t	args1;
	struct daos_task	*task1;
	struct daos_task	*task2;
	int			rc;
	bool			empty;
	daos_epoch_state_t	state;
	struct daos_sched	*sched;

	D_ENTER;
	args1.coh	= coh;
	args1.epoch	= epoch;
	args1.state     = &state;
	args2.coh	= coh;

	sched = &(dss_get_module_info()->dmi_sched);

	D_DEBUG(DF_TIERS, "epoch:"DF_U64"\n", epoch);

	rc = daos_task_create(DAOS_OPC_EPOCH_COMMIT, sched, &args1, 0, NULL,
			      &task1);
	if (rc) {
		D_ERROR("daos_task_create returned %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = daos_task_create(DAOS_OPC_CONT_CLOSE, sched, &args2, 1, &task1,
			      &task2);
	if (rc) {
		D_ERROR("daos_task_create returned %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = daos_task_schedule(task2, false);
	if (rc) {
		D_ERROR("daos_task_schedule returned %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = daos_task_schedule(task1, false);
	if (rc) {
		D_ERROR("daos_task_schedule returned %d\n", rc);
		D_GOTO(out, rc);
	}
	/* make sure this completes before we return */
	daos_progress(sched, DAOS_EQ_WAIT, &empty);
out:
	D_EXIT;
	return rc;
}

/* open receiving container and setup held epoch */
static int
tf_cont_open(daos_handle_t *pcoh, uuid_t cid, daos_epoch_t *epoch)
{
	daos_cont_open_t	 args1;
	daos_epoch_hold_t	 args2;
	struct daos_task	*task1;
	struct daos_task	*task2;
	daos_cont_open_t	*dta1;
	daos_epoch_hold_t	*dta2;
	int			 rc;
	bool			 empty;
	daos_epoch_state_t	 epstate;
	struct daos_sched	*sched;

	args1.poh	= warmer_poh;
	args1.flags	= DAOS_COO_RW;
	args1.coh	= &args2.coh;
	args1.info	= NULL;
	uuid_copy((unsigned char *)args1.uuid, cid);

	args2.epoch = epoch;
	args2.state = &epstate;
	args2.coh   = DAOS_HDL_INVAL;

	sched = &(dss_get_module_info()->dmi_sched);

	rc = daos_task_create(DAOS_OPC_CONT_OPEN, sched, &args1, 0, NULL,
			      &task1);
	if (rc) {
		D_ERROR("daos_task_create returned %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = daos_task_create(DAOS_OPC_EPOCH_HOLD, sched, &args2, 1, &task1,
			      &task2);
	if (rc) {
		D_ERROR("daos_task_create returned %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = daos_task_register_comp_cb(task2, tf_cont_cb,
					&pcoh, sizeof(daos_handle_t **));
	if (rc) {
		D_ERROR("daos_task_register_comp_cb returned %d\n", rc);
		D_GOTO(out, rc);
	}
	dta1 = daos_task_get_args(DAOS_OPC_CONT_OPEN, task1);
	dta2 = daos_task_get_args(DAOS_OPC_EPOCH_HOLD, task2);
	dta1->coh = &dta2->coh;

	rc = daos_task_schedule(task2, false);
	if (rc) {
		D_ERROR("daos_task_schedule returned %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = daos_task_schedule(task1, false);
	if (rc) {
		D_ERROR("daos_task_schedule returned %d\n", rc);
		D_GOTO(out, rc);
	}
	daos_sched_progress(sched);
	empty = false;
	while (!empty) {
		rc = daos_progress(sched, DAOS_EQ_NOWAIT, &empty);
		ABT_thread_yield();
	}

out:
	return rc;
}

struct tier_cofetch {
	struct tier_bcast_fetch_in *tfi;
	daos_handle_t	            coh;
};

/* called collectively for all tasks on one node */
static int
tier_hdlr_fetch_one(void *vin)
{
	int		     rc;
	struct tier_cofetch *in = (struct tier_cofetch *)vin;
	daos_handle_t        coh;
	struct ds_pool_child *child;

	child = ds_pool_child_lookup(in->tfi->bfi_pool);
	if (child == NULL) {
		D_DEBUG(DF_TIERS, "ds_pool_child_lookup returned NULL\n");
		return -DER_NONEXIST;
	}
	rc = vos_cont_open(child->spc_hdl, in->tfi->bfi_co_id, &coh);
	if (rc != 0) {
		D_DEBUG(DF_TIERS, "vos_cont_open returned %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = tier_fetche(in->tfi->bfi_pool, coh, in->tfi->bfi_ep,
			 in->tfi->bfi_co_id, in->coh);
	if (rc != 0)
		D_DEBUG(DF_TIERS, "tier_fetche returned %d\n", rc);
out:
	ds_pool_child_put(child);
	return rc;
}

/*
 * handler for fetch broadcast to all nodes on tier
 */
void
ds_tier_fetch_bcast_handler(crt_rpc_t *rpc)
{

	struct tier_bcast_fetch_in  *in = crt_req_get(rpc);
	struct tier_fetch_out *out = crt_reply_get(rpc);
	int		       rc = 0;
	struct tier_cofetch    cof;

	cof.tfi = in;
	daos_cont_global2local(warmer_poh, in->bfi_dst_hdl, &cof.coh);

	out->tfo_ret = dss_task_collective(tier_hdlr_fetch_one, &cof);
	rc = crt_reply_send(rpc);
	if (rc < 0)
		D_ERROR("crt_reply_send returned %d\n", rc);
}

/* Primary fetch handler - runs on single node */
void
ds_tier_fetch_handler(crt_rpc_t *rpc)
{

	struct tier_fetch_in  *in = crt_req_get(rpc);
	struct tier_fetch_out *out = crt_reply_get(rpc);
	int  rc = 0;
	struct tier_bcast_fetch_in *inb;
	struct tier_fetch_out  *outb;
	daos_handle_t	       coh = DAOS_HDL_INVAL;
	crt_rpc_t	      *brpc;
	daos_iov_t	       gh;

	D_DEBUG(DF_TIERS, "\tpool:"DF_UUIDF"\n", DP_UUID(in->tfi_pool));
	D_DEBUG(DF_TIERS, "\tcont_id:"DF_UUIDF"\n", DP_UUID(in->tfi_co_id));
	D_DEBUG(DF_TIERS, "\tepoch:"DF_U64"\n", in->tfi_ep);

	rc = tf_cont_open(&coh, in->tfi_co_id, &in->tfi_ep);
	if (rc) {
		D_ERROR("tf_cont_open returned %d\n", rc);
		D_GOTO(out, rc);
	}

	gh.iov_buf = NULL;
	daos_cont_local2global(coh, &gh);
	D_ALLOC(gh.iov_buf, gh.iov_buf_len);
	gh.iov_len = gh.iov_buf_len;

	daos_cont_local2global(coh, &gh);

	rc = ds_tier_bcast_create(rpc->cr_ctx, in->tfi_pool,
				  TIER_BCAST_FETCH, &brpc);
	if (rc) {
		D_ERROR("ds_tier_bcast_create returned %d\n", rc);
		D_GOTO(out_free, rc);
	}
	inb = crt_req_get(brpc);
	uuid_copy(inb->bfi_pool, in->tfi_pool);
	uuid_copy(inb->bfi_co_id, in->tfi_co_id);
	inb->bfi_ep    = in->tfi_ep;
	inb->bfi_dst_hdl = gh;

	rc = dss_rpc_send(brpc);
	if (rc)
		D_GOTO(out_free, rc);
	outb = crt_reply_get(brpc);
	rc = outb->tfo_ret;
	if (rc != 0)
		D_GOTO(out_free, rc);

	rc = tf_cont_close(coh, in->tfi_co_id, in->tfi_ep);
	if (rc)
		D_ERROR("tf_cont_close returned %d\n", rc);

out_free:
	D_FREE(gh.iov_buf, gh.iov_buf_len);
out:
	out->tfo_ret = rc;
	rc = crt_reply_send(rpc);
}


/**
 * tier_fetche - fetch everything in an epoch
 */
static int
tier_fetche(uuid_t pool, daos_handle_t co, daos_epoch_t ev, uuid_t cid,
	    daos_handle_t wcoh)
{
	int			 rc;
	struct tier_enum_params  params;
	struct tier_fetch_ctx    ctx;

	ctx.dfc_co       = co;
	ctx.dfc_coh      = wcoh;
	ctx.dfc_ev       = ev;
	uuid_copy(ctx.dfc_pool, pool);

	DAOS_INIT_LIST_HEAD(&ctx.dfc_head);
	DAOS_INIT_LIST_HEAD(&ctx.dfc_dkios);
	DAOS_INIT_LIST_HEAD(&ctx.dfc_iods);


	ctx.dfc_sched  = &(dss_get_module_info()->dmi_sched);


	params.dep_type	     = VOS_ITER_RECX;
	params.dep_ev        = ev;
	params.dep_cbctx     = &ctx;
	params.dep_obj_pre   = tier_latch_oid;
	params.dep_obj_post  = tier_proc_obj;
	params.dep_dkey_pre  = tier_latch_dkey;
	params.dep_dkey_post = tier_proc_dkey;
	params.dep_akey_pre  = tier_latch_akey;
	params.dep_akey_post = tier_proc_akey;
	params.dep_recx_cbfn = tier_rec_cb;

	rc = ds_tier_enum(co, &params);
	return rc;
}

/* called after all object dkeys have been enumerated */
static int
tier_proc_obj(void *ctx, vos_iter_entry_t *ie)
{
	struct tier_fetch_ctx	*fctx = (struct tier_fetch_ctx *)ctx;
	struct daos_task	*task;
	int			rc;
	daos_obj_close_t	args;

	D_DEBUG(DF_TIERS, "closing object:"DF_UOID" on dest tier\n",
		DP_UOID(fctx->dfc_oid));
	args.oh	= fctx->dfc_oh;
	DAOS_API_ARG_ASSERT(args, OBJ_CLOSE);

	rc = daos_task_create(DAOS_OPC_OBJ_CLOSE,  fctx->dfc_sched,
			      &args, 0, NULL, &task);
	if (rc == 0) {
		daos_task_schedule(task, true);
		daos_sched_progress(fctx->dfc_sched);
	}
	return rc;

}

/* open object on recieving tier */
static int
tf_obj_open(struct tier_fetch_ctx *fctx)
{
	daos_obj_open_t		args;
	struct daos_task	*task;
	int			rc;
	bool			empty;

	D_DEBUG(DF_TIERS, "opening object:"DF_UOID" on dest tier\n",
		DP_UOID(fctx->dfc_oid));
	args.coh	= fctx->dfc_coh;
	args.oid	= fctx->dfc_oid.id_pub;
	args.epoch	= fctx->dfc_ev;
	args.mode	= DAOS_OO_RW;
	args.oh		= &fctx->dfc_oh;


	DAOS_API_ARG_ASSERT(args, OBJ_OPEN);

	rc = daos_task_create(DAOS_OPC_OBJ_OPEN, fctx->dfc_sched, &args,
			      0, NULL, &task);
	if (rc == 0) {
		daos_task_schedule(task, true);
		daos_progress(fctx->dfc_sched,
			      DAOS_EQ_WAIT, &empty);
	}
	return rc;
}

struct tf_ou_cb_args {
	daos_handle_t         ioh;
	daos_key_t           *dkey;
	int	              nrecs;
	daos_iod_t           *iods;
	struct tier_key_iod  *tki;
};

/* object update callback - releases VOS ZC resources */
static int
tf_obj_update_cb(struct daos_task *task, void *data)
{
	struct tf_ou_cb_args *cba = (struct tf_ou_cb_args *)data;
	int rc;
	int j;

	D_DEBUG(DF_TIERS, "object update complete\n");
	rc = vos_obj_zc_fetch_end(cba->ioh, cba->dkey, cba->nrecs,
				  cba->iods, 0);
	if (rc)
		D_ERROR("vox_obj_zc_fetch_end returned %d\n", rc);

	for (j = 0; j < cba->nrecs; j++) {
		daos_iod_t *piod = &cba->iods[j];
		int nr = piod->iod_nr;

		D_FREE(piod->iod_recxs, sizeof(daos_recx_t) * nr);
		D_FREE(piod->iod_csums, sizeof(daos_csum_buf_t) * nr);
		D_FREE(piod->iod_eprs, sizeof(daos_epoch_range_t) * nr);
	}
	D_FREE(cba->tki, tier_key_iod_size(cba->nrecs));
	return rc;
}

/* update object on receiving tier */
static int
tf_obj_update(struct tier_fetch_ctx *fctx, struct tier_key_iod *tki)
{
	daos_obj_update_t	args;
	struct daos_task       *task;
	int			rc;
	struct tf_ou_cb_args	cba;
	bool			empty;

	D_DEBUG(DF_TIERS, "updating object on dest tier\n");
	args.oh		= fctx->dfc_oh;
	args.epoch	= fctx->dfc_ev;
	args.dkey	= &tki->dki_dkey;
	args.nr		= tki->dki_nr;
	args.iods	= tki->dki_iods;
	args.sgls	= tki->dki_sgs;

	DAOS_API_ARG_ASSERT(args, OBJ_UPDATE);

	rc = daos_task_create(DAOS_OPC_OBJ_UPDATE, fctx->dfc_sched, &args, 0,
			      NULL, &task);
	if (rc)
		D_GOTO(out, rc);

	cba.ioh   = fctx->dfc_ioh;
	cba.dkey  = &fctx->dfc_dkey;
	cba.nrecs = args.nr;
	cba.iods  = args.iods;
	cba.tki   = tki;

	rc = daos_task_register_comp_cb(task, tf_obj_update_cb,
					&cba, sizeof(struct tf_ou_cb_args));
	if (rc) {
		D_ERROR("das_task_register_comp_cb returned %d\n", rc);
		D_GOTO(out, rc);
	} else {
		daos_task_schedule(task, true);
		daos_progress(fctx->dfc_sched,
			      DAOS_EQ_WAIT, &empty);
	}
out:
	return rc;
}

static char *
tier_pr_key(daos_key_t k, char *kbuf)
{
	int len = (k.iov_len < 80) ? k.iov_len : 79;

	memcpy(kbuf, k.iov_buf, len);
	kbuf[len] = '\0';
	return kbuf;
}

/* object pre-decent callback - latches OID */
static int
tier_latch_oid(void *ctx, vos_iter_entry_t *ie)
{
	struct tier_fetch_ctx *fctx = (struct tier_fetch_ctx *)ctx;
	int rc;

	D_DEBUG(DF_TIERS, " "DF_UOID"\n", DP_UOID(ie->ie_oid));
	fctx->dfc_oid = ie->ie_oid;
	rc = tf_obj_open(fctx);
	if (rc)
		D_ERROR("tf_obj_open returned %d\n", rc);

	return 0;
}

/* dkey pre-decent callback - just latch the key */
static int
tier_latch_dkey(void *ctx, vos_iter_entry_t *ie)
{
	struct tier_fetch_ctx *fctx = (struct tier_fetch_ctx *)ctx;

	fctx->dfc_dkey = ie->ie_key;
	fctx->dfc_na = 0;
	return 0;
}

/* dkey post-decent callback - collect all the Iods into an update op */
static int
tier_proc_dkey(void *ctx, vos_iter_entry_t *ie)
{
	struct tier_fetch_ctx		*fctx = (struct tier_fetch_ctx *)ctx;
	int				 rc = 0;
	struct tier_key_iod		*ptmp;
	struct tier_vec_iod		*src;
	int				 nrecs = fctx->dfc_na;
	daos_list_t			*iter;
	daos_list_t			*tmp;
	int				 j;
	daos_epoch_t			 epoch = DAOS_EPOCH_MAX;


	D_ALLOC(ptmp, tier_key_iod_size(nrecs));
	if (ptmp == NULL)
		D_GOTO(out, (rc = -DER_NOMEM));

	ptmp->dki_iods = (daos_iod_t *)&ptmp[1];
	ptmp->dki_sgs  = (daos_sg_list_t *)&ptmp->dki_iods[nrecs];
	ptmp->dki_nr   = 0;
	tier_cp_oid(&ptmp->dki_oid, &fctx->dfc_oid);
	ptmp->dki_dkey = fctx->dfc_dkey;

	daos_list_for_each_safe(iter, tmp, &fctx->dfc_iods) {
		src = (struct tier_vec_iod *)
		      daos_list_entry(iter, struct tier_vec_iod, dvi_lh);
		tier_cp_vec_iod(&ptmp->dki_iods[ptmp->dki_nr],
				 &src->dvi_viod);
		(ptmp->dki_nr)++;
		daos_list_del(iter);
		D_FREE(src, tier_vec_iod_size(src->dvi_viod.iod_nr));
	}
	rc = vos_obj_zc_fetch_begin(fctx->dfc_co, fctx->dfc_oid, epoch,
				    &fctx->dfc_dkey, nrecs,
				    ptmp->dki_iods, &fctx->dfc_ioh);
	if (rc != 0) {
		D_ERROR("vos_obj_zc_fetch returned %d\n", rc);
		D_GOTO(out, rc);
	}
	for (j = 0; j < nrecs; j++) {
		daos_sg_list_t *psg;

		rc = vos_obj_zc_sgl_at(fctx->dfc_ioh, j, &psg);
		if (rc != 0) {
			D_ERROR("vos_obj_zc_sgl_at returned %d\n", rc);
			break;
		}
		ptmp->dki_sgs[j].sg_nr.num_out   = psg->sg_nr.num_out;
		ptmp->dki_sgs[j].sg_nr.num       = psg->sg_nr.num_out;
		ptmp->dki_sgs[j].sg_iovs         = psg->sg_iovs;
	}
	tf_obj_update(fctx, ptmp);
out:
	return rc;
}

/* akey pre-decent callback - latches akey */
static int
tier_latch_akey(void *ctx, vos_iter_entry_t *ie)
{
	struct tier_fetch_ctx *fctx = (struct tier_fetch_ctx *)ctx;

	fctx->dfc_akey = ie->ie_key;
	fctx->dfc_ne   = 0;
	return 0;
}

/* key post-decent callback
 * build 1 iod for this akey
 */
static int
tier_proc_akey(void *ctx, vos_iter_entry_t *ie)
{
	struct tier_vec_iod	*vio;
	struct tier_ext_list	*dei;
	struct tier_fetch_ctx	*fctx = (struct tier_fetch_ctx *)ctx;
	int			 rc = 0;
	int			 nrecs = 0;
	int			 j;
	daos_list_t		*iter;
	daos_list_t		*tmp;
	unsigned char		*ptmp;
	char			 kbuf[80];

	nrecs = fctx->dfc_ne;
	D_DEBUG(DF_TIERS, "(%d) %s\n", nrecs, tier_pr_key(ie->ie_key, kbuf));


	if (nrecs  == 0) {
		D_DEBUG(DF_TIERS, "akey had no extents\n");
		D_GOTO(out, 0);
	}

	/* allocate the list wrapper */
	D_ALLOC_PTR(vio);
	if (vio == NULL)
		D_GOTO(out, -DER_NOMEM);

	vio->dvi_viod.iod_size = 0;

	D_ALLOC(ptmp, sizeof(daos_recx_t) * nrecs);
	if (ptmp == NULL) {
		D_FREE_PTR(vio);
		D_GOTO(out, -DER_NOMEM);
	}

	vio->dvi_viod.iod_recxs = (daos_recx_t *)ptmp;

	D_ALLOC(ptmp, sizeof(daos_csum_buf_t) * nrecs);
	if (ptmp == NULL) {
		D_FREE(vio->dvi_viod.iod_recxs, sizeof(daos_recx_t) * nrecs);
		D_FREE_PTR(vio);
		D_GOTO(out, -DER_NOMEM);
	}

	vio->dvi_viod.iod_csums = (daos_csum_buf_t *)ptmp;
	D_ALLOC(ptmp, sizeof(daos_epoch_range_t) * nrecs);
	if (ptmp == NULL) {
		D_FREE_PTR(vio);
		D_FREE(vio->dvi_viod.iod_recxs, sizeof(daos_recx_t) * nrecs);
		D_FREE(vio->dvi_viod.iod_csums,
		       sizeof(daos_csum_buf_t) * nrecs);
		D_GOTO(out, -DER_NOMEM);
	}

	vio->dvi_viod.iod_eprs = (daos_epoch_range_t *)ptmp;

	DAOS_INIT_LIST_HEAD(&vio->dvi_lh);
	daos_list_add(&vio->dvi_lh, &fctx->dfc_iods);

	tier_cp_iov(&vio->dvi_viod.iod_name, &fctx->dfc_akey);
	tier_csum(&vio->dvi_viod.iod_kcsum, &fctx->dfc_dkey,
		  sizeof(daos_key_t));

	/* pass to copy recxs */
	daos_list_for_each_safe(iter, tmp, &fctx->dfc_head) {
		dei = (struct tier_ext_list *)
		      daos_list_entry(iter, struct tier_ext_list, del_lh);
		for (j = 0; j < dei->del_nrecs; j++) {
			daos_iod_t *p = &vio->dvi_viod;

			if (p->iod_size == 0) {
				p->iod_size = dei->del_recs[j].der_rsize;
				p->iod_type = dei->del_recs[j].der_type;
			} else if (p->iod_size != dei->del_recs[j].der_rsize) {
				D_ERROR("mutilple sizes %lu  %lu\n",
					p->iod_size,
					dei->del_recs[j].der_rsize);
			}
			tier_cp_recx(&p->iod_recxs[p->iod_nr],
				      &dei->del_recs[j].der_rec);
			tier_csum(&p->iod_csums[p->iod_nr], &dei->del_recs[j],
				   sizeof(daos_recx_t));
			p->iod_eprs[p->iod_nr].epr_hi = ie->ie_epr.epr_hi;
			p->iod_eprs[p->iod_nr].epr_lo = ie->ie_epr.epr_lo;
			p->iod_nr++;

		}
		daos_list_del(iter);
		D_FREE_PTR(dei);
	}
	fctx->dfc_na++;

out:
	return rc;
}

/* callback handler for extents */
static int
tier_rec_cb(void *ctx, vos_iter_entry_t *ie)
{
	struct tier_fetch_ctx *fctx = (struct tier_fetch_ctx *)ctx;
	struct tier_ext_list  *el;
	int		       rc = 0;

	if (daos_list_empty(&fctx->dfc_head)) {
		D_ALLOC_PTR(el);
		if (el == NULL)
			D_GOTO(out, -DER_NOMEM);
		DAOS_INIT_LIST_HEAD(&el->del_lh);
		el->del_nrecs = 0;
		daos_list_add_tail(&el->del_lh, &fctx->dfc_head);
	}
	el = daos_list_entry(fctx->dfc_head.prev, struct tier_ext_list, del_lh);
	if (el->del_nrecs == NUM_BUNDLED_EXTS) {
		D_ALLOC_PTR(el);
		if (el == NULL)
			D_GOTO(out, -DER_NOMEM);
		DAOS_INIT_LIST_HEAD(&el->del_lh);
		el->del_nrecs = 0;
		daos_list_add_tail(&el->del_lh, &fctx->dfc_head);
	}

	tier_cp_recx(&el->del_recs[el->del_nrecs].der_rec, &ie->ie_recx);
	el->del_recs[el->del_nrecs].der_rsize = ie->ie_rsize;
	el->del_recs[el->del_nrecs].der_type = DAOS_IOD_ARRAY;

	/*  IOV doesn't get filled out - not useful
	tier_cp_iov(&el->del_recs[el->del_nrecs].der_iov, &ie->ie_iov);
	*/

	/* fix this to add real checksum support */
	daos_csum_set(&el->del_recs[el->del_nrecs].der_ckrec, NULL, 0);

	el->del_nrecs++;
	fctx->dfc_ne++;

out:
	return rc;
}

