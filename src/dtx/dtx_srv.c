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
 * dtx: DTX rpc service
 */
#define D_LOGFAC	DD_FAC(dtx)

#include <daos/rpc.h>
#include <daos/btree_class.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/container.h>
#include <daos_srv/vos.h>
#include <daos_srv/dtx_srv.h>
#include "dtx_internal.h"

#define DTX_YIELD_CYCLE		(DTX_THRESHOLD_COUNT >> 3)

static void
dtx_handler(crt_rpc_t *rpc)
{
	struct dtx_in		*din = crt_req_get(rpc);
	struct dtx_out		*dout = crt_reply_get(rpc);
	struct ds_cont_child	*cont = NULL;
	struct dtx_id		*dtis;
	struct dtx_memberships	*mbs[DTX_REFRESH_MAX] = { 0 };
	uint32_t		 vers[DTX_REFRESH_MAX] = { 0 };
	uint32_t		 opc = opc_get(rpc->cr_opc);
	int			 count = DTX_YIELD_CYCLE;
	int			 i = 0;
	int			 rc1 = 0;
	int			 rc;

	rc = ds_cont_child_lookup(din->di_po_uuid, din->di_co_uuid, &cont);
	if (rc != 0) {
		D_ERROR("Failed to locate pool="DF_UUID" cont="DF_UUID
			" for DTX rpc %u: rc = "DF_RC"\n",
			DP_UUID(din->di_po_uuid), DP_UUID(din->di_co_uuid),
			opc, DP_RC(rc));
		goto out;
	}

	switch (opc) {
	case DTX_COMMIT:
		if (DAOS_FAIL_CHECK(DAOS_DTX_MISS_COMMIT))
			break;

		while (i < din->di_dtx_array.ca_count) {
			if (i + count > din->di_dtx_array.ca_count)
				count = din->di_dtx_array.ca_count - i;

			dtis = (struct dtx_id *)din->di_dtx_array.ca_arrays + i;
			rc1 = vos_dtx_commit(cont->sc_hdl, dtis, count, NULL);
			if (rc == 0 && rc1 < 0)
				rc = rc1;

			i += count;
		}
		break;
	case DTX_ABORT:
		if (DAOS_FAIL_CHECK(DAOS_DTX_MISS_ABORT))
			break;

		while (i < din->di_dtx_array.ca_count) {
			if (i + count > din->di_dtx_array.ca_count)
				count = din->di_dtx_array.ca_count - i;

			dtis = (struct dtx_id *)din->di_dtx_array.ca_arrays + i;
			rc1 = vos_dtx_abort(cont->sc_hdl, din->di_epoch,
					    dtis, count);
			if (rc == 0 && rc1 < 0)
				rc = rc1;

			i += count;
		}
		break;
	case DTX_CHECK:
		/* Currently, only support to check single DTX state. */
		if (din->di_dtx_array.ca_count != 1)
			rc = -DER_PROTO;
		else
			rc = vos_dtx_check(cont->sc_hdl,
					   din->di_dtx_array.ca_arrays,
					   NULL, NULL, NULL, false);
		break;
	case DTX_REFRESH:
		count = din->di_dtx_array.ca_count;
		if (count == 0)
			D_GOTO(out, rc = 0);

		if (count > DTX_REFRESH_MAX)
			D_GOTO(out, rc = -DER_PROTO);

		D_ALLOC(dout->do_sub_rets.ca_arrays, sizeof(int32_t) * count);
		if (dout->do_sub_rets.ca_arrays == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		dout->do_sub_rets.ca_count = count;

		for (i = 0, rc1 = 0; i < count; i++) {
			int	*ptr = (int *)dout->do_sub_rets.ca_arrays + i;

			dtis = (struct dtx_id *)din->di_dtx_array.ca_arrays + i;
			*ptr = vos_dtx_check(cont->sc_hdl, dtis, NULL, &vers[i],
					     &mbs[i], false);
			/* The DTX status may be changes by DTX resync soon. */
			if (*ptr == DTX_ST_PREPARED &&
			    vers[i] < cont->sc_dtx_resync_ver)
				*ptr = -DER_INPROGRESS;
			if (mbs[i] != NULL)
				rc1++;
		}
		break;
	default:
		rc = -DER_INVAL;
		break;
	}

out:
	D_DEBUG(DB_TRACE, "Handle DTX ("DF_DTI") rpc %u, count %d, epoch "
		DF_X64" : rc = "DF_RC"\n",
		DP_DTI(din->di_dtx_array.ca_arrays), opc,
		(int)din->di_dtx_array.ca_count, din->di_epoch, DP_RC(rc));

	dout->do_status = rc;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed for DTX rpc %u: rc = "DF_RC"\n", opc,
			DP_RC(rc));

	if (opc == DTX_REFRESH && rc1 > 0) {
		struct dtx_entry	 dtes[DTX_REFRESH_MAX] = { 0 };
		struct dtx_entry	*pdte[DTX_REFRESH_MAX] = { 0 };
		int			 j;

		for (i = 0, j = 0; i < count; i++) {
			if (mbs[i] == NULL)
				continue;

			daos_dti_copy(&dtes[j].dte_xid,
				      (struct dtx_id *)
				      din->di_dtx_array.ca_arrays + i);
			dtes[j].dte_ver = vers[i];
			dtes[j].dte_refs = 1;
			dtes[j].dte_mbs = mbs[i];

			pdte[j] = &dtes[j];
			j++;
		}

		D_ASSERT(j == rc1);

		/* Commit the DTX after replied the original refresh request to
		 * avoid further query the same DTX.
		 */
		dtx_commit(cont, pdte, j, true);

		for (i = 0; i < j; i++)
			D_FREE(pdte[i]->dte_mbs);
	}

	D_FREE(dout->do_sub_rets.ca_arrays);
	dout->do_sub_rets.ca_count = 0;

	if (cont != NULL)
		ds_cont_child_put(cont);
}

static int
dtx_init(void)
{
	int	rc;

	rc = dbtree_class_register(DBTREE_CLASS_DTX_CF,
				   BTR_FEAT_UINT_KEY | BTR_FEAT_DYNAMIC_ROOT,
				   &dbtree_dtx_cf_ops);
	if (rc == 0)
		rc = dbtree_class_register(DBTREE_CLASS_DTX_COS, 0,
					   &dtx_btr_cos_ops);

	return rc;
}

static int
dtx_fini(void)
{
	return 0;
}

static int
dtx_setup(void)
{
	int	rc;

	rc = dss_ult_create_all(dtx_batched_commit, NULL, DSS_ULT_GC, true);
	if (rc != 0)
		D_ERROR("Failed to create DTX batched commit ULT: "DF_RC"\n",
			DP_RC(rc));

	return rc;
}

#define X_SRV(a, b, c, d, e)	\
{				\
	.dr_opc       = a,	\
	.dr_hdlr      = d,	\
	.dr_corpc_ops = e,	\
}

static struct daos_rpc_handler dtx_handlers[] = {
	DTX_PROTO_SRV_RPC_LIST(X_SRV)
};

struct dss_module dtx_module =  {
	.sm_name	= "dtx",
	.sm_mod_id	= DAOS_DTX_MODULE,
	.sm_ver		= DAOS_DTX_VERSION,
	.sm_init	= dtx_init,
	.sm_fini	= dtx_fini,
	.sm_setup	= dtx_setup,
	.sm_proto_fmt	= &dtx_proto_fmt,
	.sm_cli_count	= 0,
	.sm_handlers	= dtx_handlers,
};
