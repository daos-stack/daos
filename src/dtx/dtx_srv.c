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

static void
dtx_handler(crt_rpc_t *rpc)
{
	struct dtx_in		*din = crt_req_get(rpc);
	struct dtx_out		*dout = crt_reply_get(rpc);
	struct ds_cont_child	*cont = NULL;
	uint32_t		 opc = opc_get(rpc->cr_opc);
	int			 rc;

	rc = ds_cont_child_lookup(din->di_po_uuid, din->di_co_uuid, &cont);
	if (rc != 0) {
		D_ERROR("Failed to locate pool="DF_UUID" cont="DF_UUID
			" for DTX rpc %u: rc = %d\n", DP_UUID(din->di_po_uuid),
			DP_UUID(din->di_co_uuid), opc, rc);
		goto out;
	}

	switch (opc) {
	case DTX_COMMIT:
		rc = vos_dtx_commit(cont->sc_hdl, din->di_dtx_array.ca_arrays,
				    din->di_dtx_array.ca_count);
		break;
	case DTX_ABORT:
		rc = vos_dtx_abort(cont->sc_hdl, din->di_dtx_array.ca_arrays,
				   din->di_dtx_array.ca_count, true);
		break;
	case DTX_CHECK:
		/* Currently, only support to check single DTX state. */
		if (din->di_dtx_array.ca_count != 1)
			rc = -DER_PROTO;
		else
			/* For the remote query about DTX check, it is NOT
			 * necessary to lookup CoS cache, so set the 'oid'
			 * as zero to bypass CoS cache.
			 */
			rc = vos_dtx_check_committable(cont->sc_hdl, NULL,
					din->di_dtx_array.ca_arrays, 0, false);
		break;
	default:
		rc = -DER_INVAL;
		break;
	}

out:
	D_DEBUG(DB_TRACE, "Handle DTX ("DF_DTI") rpc %u: rc = %d\n",
		DP_DTI(din->di_dtx_array.ca_arrays), opc, rc);

	dout->do_status = rc;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed for DTX rpc %u: rc = %d\n", opc, rc);

	if (cont != NULL)
		ds_cont_child_put(cont);
}

static int
dtx_init(void)
{
	int	rc;

	rc = dbtree_class_register(DBTREE_CLASS_DTX_CF, BTR_FEAT_UINT_KEY,
				   &dbtree_dtx_cf_ops);
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

	rc = dss_ult_create_all(dtx_batched_commit, NULL, true);
	if (rc != 0)
		D_ERROR("Failed to create DTX batched commit ULT: %d\n", rc);

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
