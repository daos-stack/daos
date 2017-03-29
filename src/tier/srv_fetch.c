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
#include <daos_srv/daos_server.h>
#include <daos/rpc.h>
#include "rpc.h"
#include "srv_internal.h"
#include <daos_srv/container.h>
#include <daos_srv/vos.h>

/* holds extents as returned by VOS. plus spc for a checksum */
struct tier_ext_rec {
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

struct tier_dkey_iod_list {
	daos_list_t	     dkl_lh;
	struct tier_key_iod *dkl_dki;
};

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
	/* list heads for collecting what to fetch */
	daos_list_t          dfc_head;
	daos_list_t          dfc_iods;
	daos_list_t	     dfc_dkios;
};
static int
tier_fetche(uuid_t pool, daos_handle_t coh, daos_epoch_t ev);

static int tier_latch_oid(void *ctx, vos_iter_entry_t *ie);
static int tier_latch_dkey(void *ctx, vos_iter_entry_t *ie);
static int tier_proc_dkey(void *ctx, vos_iter_entry_t *ie);
static int tier_latch_akey(void *ctx, vos_iter_entry_t *ie);
static int tier_proc_akey(void *ctx, vos_iter_entry_t *ie);
static int tier_rec_cb(void *ctx, vos_iter_entry_t *ie);

/* called collectively */
static int
tier_hdlr_fetch_one(void *vin)
{
	int		      rc;
	struct tier_fetch_in *in = (struct tier_fetch_in *)vin;
	struct ds_cont_hdl   *coh;

	coh = ds_cont_hdl_lookup(in->tfi_co_hdl);
	if (coh != NULL)
		rc = tier_fetche(in->tfi_pool, coh->sch_cont->sc_hdl,
				 in->tfi_ep);
	else
		rc = -DER_INVAL;
	return rc;
}

int
ds_tier_fetch_handler(crt_rpc_t *rpc)
{

	struct tier_fetch_in  *in = crt_req_get(rpc);
	struct tier_fetch_out *out = crt_reply_get(rpc);
	int		       rc = 0;

	D_DEBUG(DF_TIERS, "ds_tier_fetch_handler\n");
	D_DEBUG(DF_TIERS, "\tpool:"DF_UUIDF"\n", in->tfi_pool);
	D_DEBUG(DF_TIERS, "\tcont_id:"DF_UUIDF"\n", in->tfi_co_hdl);
	D_DEBUG(DF_TIERS, "\tepoch:"DF_U64"\n", in->tfi_ep);

	out->tfo_ret = dss_collective(tier_hdlr_fetch_one, in);
	rc = crt_reply_send(rpc);
	return rc;
}


/**
 * tier_fetche - fetch everything in an epoch
 */
static int
tier_fetche(uuid_t pool, daos_handle_t co, daos_epoch_t ev)
{
	int			 rc;
	struct tier_enum_params  params;
	struct tier_fetch_ctx    ctx;

	ctx.dfc_co       = co;
	ctx.dfc_ev	 = ev;
	uuid_copy(ctx.dfc_pool, pool);

	DAOS_INIT_LIST_HEAD(&ctx.dfc_head);
	DAOS_INIT_LIST_HEAD(&ctx.dfc_dkios);
	DAOS_INIT_LIST_HEAD(&ctx.dfc_iods);

	params.dep_type	     = VOS_ITER_RECX;
	params.dep_ev        = ev;
	params.dep_cbctx     = &ctx;
	params.dep_obj_pre   = tier_latch_oid;
	params.dep_obj_post  = NULL;
	params.dep_dkey_pre  = tier_latch_dkey;
	params.dep_dkey_post = tier_proc_dkey;
	params.dep_akey_pre  = tier_latch_akey;
	params.dep_akey_post = tier_proc_akey;
	params.dep_recx_cbfn = tier_rec_cb;

	rc = ds_tier_enum(co, &params);
	return rc;
}


static int
tier_latch_oid(void *ctx, vos_iter_entry_t *ie)
{
	struct tier_fetch_ctx *fctx = (struct tier_fetch_ctx *)ctx;

	fctx->dfc_oid = ie->ie_oid;
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

/* dkey post-decent callback - collect all the Iods into a op */
static int
tier_proc_dkey(void *ctx, vos_iter_entry_t *ie)
{
	struct tier_fetch_ctx		*fctx = (struct tier_fetch_ctx *)ctx;
	int				 rc = 0;
	struct tier_dkey_iod_list	*vec;
	struct tier_key_iod		*ptmp;
	struct tier_vec_iod		*src;
	int				 nrecs = fctx->dfc_na;
	daos_list_t			*iter;
	daos_list_t			*tmp;

	D_ALLOC_PTR(vec);
	if (vec == NULL)
		D_GOTO(out, (rc = -DER_NOMEM));
	DAOS_INIT_LIST_HEAD(&vec->dkl_lh);
	D_ALLOC(ptmp, sizeof(struct tier_key_iod) +
		      (sizeof(daos_iod_t) +
		       sizeof(daos_sg_list_t)) * nrecs);
	if (ptmp == NULL) {
		D_FREE_PTR(vec);
		D_GOTO(out, (rc = -DER_NOMEM));
	}
	vec->dkl_dki   = ptmp;
	ptmp->dki_iods = (daos_iod_t *)&vec->dkl_dki[2];
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
		free(src);
	}

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

	nrecs = fctx->dfc_ne;

	if (nrecs  == 0) {
		D_DEBUG(DF_TIERS, "tier_proc_akey: akey had no extents\n");
		D_GOTO(out, 0);
	}
	/* allocate the list wrapper */
	D_ALLOC(vio, sizeof(struct tier_vec_iod));
	if (vio == NULL)
		D_GOTO(out, -DER_NOMEM);

	D_ALLOC(ptmp, sizeof(daos_recx_t) * nrecs);
	if (ptmp == NULL) {
		D_FREE_PTR(vio);
		D_GOTO(out, -DER_NOMEM);
	}

	vio->dvi_viod.iod_recxs = (daos_recx_t *)ptmp;

	D_ALLOC(ptmp, sizeof(daos_csum_buf_t) * nrecs);
	if (ptmp == NULL) {
		D_FREE_PTR(vio);
		D_GOTO(out, -DER_NOMEM);
	}

	vio->dvi_viod.iod_csums = (daos_csum_buf_t *)ptmp;
	D_ALLOC(ptmp, sizeof(daos_epoch_range_t) * nrecs);
	if (ptmp == NULL) {
		D_FREE_PTR(vio);
		D_GOTO(out, -DER_NOMEM);
	}

	vio->dvi_viod.iod_eprs = (daos_epoch_range_t *)ptmp;

	DAOS_INIT_LIST_HEAD(&vio->dvi_lh);
	daos_list_add(&vio->dvi_lh, &fctx->dfc_iods);
	/* carve up the allocated block */
	vio->dvi_viod.iod_recxs = (daos_recx_t *)ptmp;
	vio->dvi_viod.iod_csums
		= (daos_csum_buf_t *)&vio->dvi_viod.iod_recxs[nrecs];
	vio->dvi_viod.iod_eprs
		= (daos_epoch_range_t *)&vio->dvi_viod.iod_csums[nrecs];

	tier_cp_iov(&vio->dvi_viod.iod_name, &fctx->dfc_akey);
	tier_csum(&vio->dvi_viod.iod_kcsum, &fctx->dfc_dkey,
		  sizeof(daos_key_t));

	/* pass to copy recxs */
	daos_list_for_each_safe(iter, tmp, &fctx->dfc_head) {
		dei = (struct tier_ext_list *)
		      daos_list_entry(iter, struct tier_ext_list, del_lh);
		for (j = 0; j < dei->del_nrecs; j++) {
			daos_iod_t *p = &vio->dvi_viod;

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
	tier_cp_iov(&el->del_recs[el->del_nrecs].der_iov, &ie->ie_iov);

	/* fix this to add real checksum support */
	daos_csum_set(&el->del_recs[el->del_nrecs].der_ckrec, NULL, 0);

	el->del_nrecs++;
	fctx->dfc_ne++;

out:
	return rc;
}

