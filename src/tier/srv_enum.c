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
 * Framework for driving VOS enumeration.
 */
#define D_LOGFAC       DD_FAC(tier)

#include <daos_srv/daos_server.h>
#include <daos/rpc.h>
#include <daos_types.h>
#include "rpc.h"
#include "srv_internal.h"
#include <daos_srv/vos.h>


static int
ds_tier_enum_dkeys(daos_handle_t coh, struct tier_enum_params *params,
		daos_unit_oid_t oid);

static int
ds_tier_enum_akeys(daos_handle_t coh, struct tier_enum_params *params,
		daos_unit_oid_t oid, daos_key_t dkey);

static int
ds_tier_enum_recs(daos_handle_t coh, struct tier_enum_params *params,
	       daos_unit_oid_t oid, daos_key_t dkey, daos_key_t akey);


int
ds_tier_enum(daos_handle_t coh, struct tier_enum_params *params)
{
	vos_iter_param_t vip;
	int		 rc;
	daos_handle_t    hio;
	vos_iter_entry_t eo;

	memset(&vip, 0, sizeof(vip));
	vip.ip_hdl = coh;
	vip.ip_epr.epr_lo = 0;
	vip.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	rc = vos_iter_prepare(VOS_ITER_OBJ, &vip, &hio);
	if (rc) {
		D_ERROR("failed to prepare object iter %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = vos_iter_probe(hio, NULL);
	if (rc) {
		D_DEBUG(DF_TIERS, "failed to probe object iter %d\n", rc);
		if (rc == -DER_NONEXIST)
			rc = 0;
		else
			D_ERROR("failed to probe object iter %d\n", rc);
		D_GOTO(out_iter, rc);
	}
	do {
		eo.ie_epr.epr_lo = 0;
		eo.ie_epr.epr_hi = DAOS_EPOCH_MAX;
		rc = vos_iter_fetch(hio, &eo, NULL);
		if (rc) {
			D_ERROR("failed to fetch iter %d\n", rc);
			D_GOTO(out_iter, rc);
		}

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
	vos_iter_entry_t edk;

	memset(&vip, 0, sizeof(vip));
	vip.ip_hdl = coh;
	vip.ip_oid = oid;
	vip.ip_epr.epr_lo = 0;
	vip.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	rc = vos_iter_prepare(VOS_ITER_DKEY, &vip, &hidk);
	if (rc) {
		D_ERROR("failed to prepare dkey iter %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = vos_iter_probe(hidk, NULL);
	if (rc) {
		D_DEBUG(DF_TIERS, "failed to probe dkey iter %d\n", rc);
		if (rc == -DER_NONEXIST)
			rc = 0;
		D_GOTO(out_iter, rc);
	}
	do {
		edk.ie_epr.epr_lo = 0;
		edk.ie_epr.epr_hi = DAOS_EPOCH_MAX;
		rc = vos_iter_fetch(hidk, &edk, NULL);
		if (rc) {
			D_ERROR("failed to fetch iter %d\n", rc);
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
		daos_unit_oid_t oid, daos_key_t dkey)
{
	vos_iter_param_t vip;
	int		 rc;
	daos_handle_t    hiak;
	vos_iter_entry_t eak;

	memset(&vip, 0, sizeof(vip));
	vip.ip_hdl  = coh;
	vip.ip_oid  = oid;
	vip.ip_dkey = dkey;
	vip.ip_epr.epr_lo = 0;
	vip.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	rc = vos_iter_prepare(VOS_ITER_AKEY, &vip, &hiak);
	if (rc) {
		D_ERROR("failed to prepare akey iter %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = vos_iter_probe(hiak, NULL);
	if (rc) {
		D_ERROR("failed to probe akey iter %d\n", rc);
		if (rc == -DER_NONEXIST)
			rc = 0;
		D_GOTO(out_iter, rc);
	}
	do {
		eak.ie_epr.epr_lo = 0;
		eak.ie_epr.epr_hi = DAOS_EPOCH_MAX;
		rc = vos_iter_fetch(hiak, &eak, NULL);
		if (rc) {
			D_ERROR("failed to fetch iter %d\n", rc);
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
				D_ERROR("akey cb: nzret(%d)\n", rc);
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
		  daos_unit_oid_t oid, daos_key_t dkey, daos_key_t akey)
{
	vos_iter_param_t vip;
	int		 rc;
	daos_handle_t    hir;
	vos_iter_entry_t er;

	memset(&vip, 0, sizeof(vip));
	vip.ip_hdl  = coh;
	vip.ip_oid  = oid;
	vip.ip_dkey = dkey;
	vip.ip_akey = akey;
	vip.ip_epr.epr_lo = 0;
	vip.ip_epr.epr_hi = DAOS_EPOCH_MAX;

	rc = vos_iter_prepare(VOS_ITER_RECX, &vip, &hir);
	if (rc) {
		D_ERROR("failed to prepare recx iter %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = vos_iter_probe(hir, NULL);
	if (rc) {
		D_ERROR("failed to probe recx iter %d\n", rc);
		if (rc == -DER_NONEXIST)
			rc = 0;
		D_GOTO(out_iter, rc);
	}
	do {
		er.ie_epr.epr_lo = 0;
		er.ie_epr.epr_hi = DAOS_EPOCH_MAX;
		rc = vos_iter_fetch(hir, &er, NULL);
		if (rc) {
			D_ERROR("failed to fetch iter %d\n", rc);
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

