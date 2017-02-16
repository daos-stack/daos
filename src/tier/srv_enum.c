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
 *
 */
#define DD_SUBSYS       DD_FAC(tier)

#include <daos_types.h>
#include <daos_srv/daos_server.h>
#include <daos/rpc.h>
#include "rpc.h"
#include "srv_internal.h"
#include <daos_srv/vos.h>


static int
ds_tier_enum_dkeys(daos_handle_t coh, struct tier_enum_params *params,
		daos_unit_oid_t oid);

static int
ds_tier_enum_akeys(daos_handle_t coh, struct tier_enum_params *params,
		daos_unit_oid_t oid, daos_dkey_t dkey);

static int
ds_tier_enum_recs(daos_handle_t coh, struct tier_enum_params *params,
	       daos_unit_oid_t oid, daos_dkey_t dkey,
	       daos_akey_t akey);


int
ds_tier_enum(daos_handle_t coh, struct tier_enum_params *params)
{
	vos_iter_param_t vip;
	int		 rc;
	daos_handle_t    hio;
	daos_hash_out_t  aio;
	vos_iter_entry_t eo;

	vip.ip_hdl = coh;
	rc = vos_iter_prepare(VOS_ITER_OBJ, &vip, &hio);
	if (rc) {
		D_DEBUG(DF_TIERS, "failed to prepare object iter %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = vos_iter_probe(hio, &aio);
	if (rc) {
		D_DEBUG(DF_TIERS, "failed to probe object iter %d\n", rc);
		D_GOTO(out_iter, rc);
	}
	do {
		rc = vos_iter_fetch(hio, &eo, &aio);
		if (rc) {
			D_DEBUG(DF_TIERS, "failed to fetch iter %d\n", rc);
			D_GOTO(out_iter, rc);
		}
		/* TODO - check returned epr against tgt epr */

		rc = tier_safecb(params->dep_obj_pre, params->dep_cbctx, &eo);
		if (rc)
			D_GOTO(out_iter, rc);

		if (params->dep_type != VOS_ITER_OBJ)
			rc = ds_tier_enum_dkeys(coh, params, eo.ie_oid);

		if (rc)
			D_GOTO(out_iter, rc);

		rc = tier_safecb(params->dep_obj_post, params->dep_cbctx, &eo);
		if (rc)
			D_GOTO(out_iter, rc);

		rc = vos_iter_next(hio);
	} while (rc == 0);
	rc = 0;
out_iter:
	vos_iter_finish(hio);
out:
	return rc;
}


static int
ds_tier_enum_dkeys(daos_handle_t coh, struct tier_enum_params *params,
		daos_unit_oid_t oid)
{
	vos_iter_param_t vip;
	int		 rc;
	daos_handle_t    hidk;
	daos_hash_out_t  aidk;
	vos_iter_entry_t edk;

	vip.ip_hdl = coh;
	vip.ip_oid = oid;
	rc = vos_iter_prepare(VOS_ITER_DKEY, &vip, &hidk);
	if (rc) {
		D_DEBUG(DF_TIERS, "failed to prepare dkey iter %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = vos_iter_probe(hidk, &aidk);
	if (rc) {
		D_DEBUG(DF_TIERS, "failed to probe dkey iter %d\n", rc);
		D_GOTO(out_iter, rc);
	}
	do {
		rc = vos_iter_fetch(hidk, &edk, &aidk);
		if (rc) {
			D_DEBUG(DF_TIERS, "failed to fetch iter %d\n", rc);
			D_GOTO(out_iter, rc);
		}
		if (tier_rangein(&edk.ie_epr, params->dep_ev)) {
			rc = tier_safecb(params->dep_dkey_pre,
					 params->dep_cbctx, &edk);
			if (rc)
				D_GOTO(out_iter, rc);

			if (params->dep_type != VOS_ITER_DKEY) {
				rc = ds_tier_enum_akeys(coh, params, oid,
						     edk.ie_key);
				if (rc)
					D_GOTO(out_iter, rc);

			}
			rc = tier_safecb(params->dep_dkey_post,
					 params->dep_cbctx,
					 &edk);
			if (rc)
				D_GOTO(out_iter, rc);
		}
		rc = vos_iter_next(hidk);
	} while (rc == 0);
	rc = 0;
out_iter:
	vos_iter_finish(hidk);
out:
	return rc;
}

static int
ds_tier_enum_akeys(daos_handle_t coh, struct tier_enum_params *params,
		daos_unit_oid_t oid, daos_dkey_t dkey)
{
	vos_iter_param_t vip;
	int		 rc;
	daos_handle_t    hiak;
	daos_hash_out_t  aiak;
	vos_iter_entry_t eak;

	vip.ip_hdl  = coh;
	vip.ip_oid  = oid;
	vip.ip_dkey = dkey;
	rc = vos_iter_prepare(VOS_ITER_AKEY, &vip, &hiak);
	if (rc) {
		D_DEBUG(DF_TIERS, "failed to prepare akey iter %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = vos_iter_probe(hiak, &aiak);
	if (rc) {
		D_DEBUG(DF_TIERS, "failed to probe akey iter %d\n", rc);
		D_GOTO(out_iter, rc);
	}
	do {
		rc = vos_iter_fetch(hiak, &eak, &aiak);
		if (rc) {
			D_DEBUG(DF_TIERS, "failed to fetch iter %d\n", rc);
			D_GOTO(out_iter, rc);
		}
		if (tier_rangein(&eak.ie_epr, params->dep_ev)) {
			rc = tier_safecb(params->dep_akey_pre,
					 params->dep_cbctx, &eak);
			if (rc) {
				D_DEBUG(DF_TIERS, "akey cb: nzret(%d)\n", rc);
				D_GOTO(out_iter, rc);
			}
			if (params->dep_type != VOS_ITER_AKEY) {
				rc = ds_tier_enum_recs(coh, params, oid, dkey,
						    eak.ie_key);
				if (rc)
					D_GOTO(out_iter, rc);
			}
			rc = tier_safecb(params->dep_akey_post,
					 params->dep_cbctx, &eak);
			if (rc) {
				D_DEBUG(DF_TIERS, "akey cb: nzret(%d)\n", rc);
				D_GOTO(out_iter, rc);
			}
		}
		rc = vos_iter_next(hiak);
	} while (rc == 0);
	rc = 0;
out_iter:
	vos_iter_finish(hiak);
out:
	return rc;
}

static int
ds_tier_enum_recs(daos_handle_t coh, struct tier_enum_params *params,
		  daos_unit_oid_t oid, daos_dkey_t dkey,
		  daos_akey_t akey)
{
	vos_iter_param_t vip;
	int		 rc;
	daos_handle_t    hir;
	daos_hash_out_t  air;
	vos_iter_entry_t er;

	vip.ip_hdl  = coh;
	vip.ip_oid  = oid;
	vip.ip_dkey = dkey;
	vip.ip_akey = akey;
	rc = vos_iter_prepare(VOS_ITER_RECX, &vip, &hir);
	if (rc) {
		D_DEBUG(DF_TIERS, "failed to prepare recx iter %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = vos_iter_probe(hir, &air);
	if (rc) {
		D_DEBUG(DF_TIERS, "failed to probe recx iter %d\n", rc);
		D_GOTO(out_iter, rc);
	}
	do {
		rc = vos_iter_fetch(hir, &er, &air);
		if (rc) {
			D_DEBUG(DF_TIERS, "failed to fetch iter %d\n", rc);
			D_GOTO(out_iter, rc);
		}
		if (tier_rangein(&er.ie_epr, params->dep_ev)) {
			rc = tier_safecb(params->dep_recx_cbfn,
					 params->dep_cbctx, &er);
			if (rc)
				D_GOTO(out_iter, rc);
		}
		rc = vos_iter_next(hir);
	} while (rc == 0);
	rc = 0;
out_iter:
	vos_iter_finish(hir);
out:
	return rc;
}

