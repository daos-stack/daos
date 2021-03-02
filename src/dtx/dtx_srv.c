/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dtx: DTX rpc service
 */
#define D_LOGFAC	DD_FAC(dtx)

#include <daos/rpc.h>
#include <daos/btree_class.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/container.h>
#include <daos_srv/vos.h>
#include <daos_srv/dtx_srv.h>
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_producer.h>
#include "dtx_internal.h"

#define DTX_YIELD_CYCLE		(DTX_THRESHOLD_COUNT >> 3)

static inline char *
dtx_opc_to_str(crt_opcode_t opc)
{
	switch (opc) {
#define X(a, b, c, d, e, f) case a: return f;
		DTX_PROTO_SRV_RPC_LIST
#undef X
	}
	return "dtx_unknown";
}

struct dtx_tls {
	struct d_tm_node_t	*ot_op_total[DTX_PROTO_SRV_RPC_COUNT];
};

static void *
dtx_tls_init(int xs_id, int tgt_id)
{
	struct dtx_tls	*tls;
	char		*path;
	uint32_t	 opc;
	int		 rc;

	D_ALLOC_PTR(tls);
	if (tls == NULL)
		return NULL;

	/** Skip sensor setup on system xstreams */
	if (tgt_id < 0)
		return tls;

	/** Register different per-opcode sensors */
	for (opc = 0; opc < DTX_PROTO_SRV_RPC_COUNT; opc++) {
		D_ASPRINTF(path, "io/%u/ops/%s/total_cnt", tgt_id,
			   dtx_opc_to_str(opc));
		rc = d_tm_add_metric(&tls->ot_op_total[opc], path, D_TM_COUNTER,
				     "total number of processed DTX RPCs", "");
		if (rc != D_TM_SUCCESS)
			D_WARN("Failed to create DTX RPC cnt sensor for %s: "
			       DF_RC"\n", dtx_opc_to_str(opc), DP_RC(rc));
		D_FREE(path);
	}

	return tls;
}

static void
dtx_tls_fini(void *data)
{
	D_FREE(data);
}

struct dss_module_key dtx_module_key = {
	.dmk_tags	= DAOS_SERVER_TAG,
	.dmk_index	= -1,
	.dmk_init	= dtx_tls_init,
	.dmk_fini	= dtx_tls_fini,
};

static inline struct dtx_tls *
dtx_tls_get(void)
{
	return dss_module_key_get(dss_tls_get(), &dtx_module_key);
}

static void
dtx_handler(crt_rpc_t *rpc)
{
	struct dtx_tls		*tls = dtx_tls_get();
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
			D_GOTO(out, rc = -DER_PROTO);

		rc = vos_dtx_check(cont->sc_hdl, din->di_dtx_array.ca_arrays,
				   NULL, NULL, NULL, false);
		if (rc == -DER_NONEXIST && cont->sc_dtx_reindex)
			rc = -DER_INPROGRESS;

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
			if ((*ptr == DTX_ST_PREPARED &&
			     cont->sc_dtx_resyncing) ||
			    (*ptr == -DER_NONEXIST && cont->sc_dtx_reindex))
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

	rc = d_tm_increment_counter(&tls->ot_op_total[opc], NULL);
	if (rc != D_TM_SUCCESS)
		D_WARN("Failed to increase DTX RPC cnt for %s: "DF_RC"\n",
		       dtx_opc_to_str(opc), DP_RC(rc));

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
		rc = dtx_commit(cont, pdte, j, true);
		if (rc < 0)
			D_WARN("Failed to commit DTX "DF_DTI", count %d: "
			       DF_RC"\n", DP_DTI(&dtes[0].dte_xid), j,
			       DP_RC(rc));

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

	rc = dss_ult_create_all(dtx_batched_commit, NULL, true);
	if (rc != 0)
		D_ERROR("Failed to create DTX batched commit ULT: "DF_RC"\n",
			DP_RC(rc));

	return rc;
}

#define X(a, b, c, d, e, f)	\
{				\
	.dr_opc       = a,	\
	.dr_hdlr      = d,	\
	.dr_corpc_ops = e,	\
},

static struct daos_rpc_handler dtx_handlers[] = {
	DTX_PROTO_SRV_RPC_LIST
};

#undef X

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
	.sm_key		= &dtx_module_key,
};
