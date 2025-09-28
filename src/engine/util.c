/*
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(server)

#include <signal.h>
#include <abt.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <execinfo.h>

#include <daos/btree_class.h>
#include <daos/common.h>
#include <daos/placement.h>
#include <daos/tls.h>
#include "srv_internal.h"
#include "drpc_internal.h"
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_producer.h>

/*
 * Register the dbtree classes used by native server-side modules (e.g.,
 * ds_pool, ds_cont, etc.). Unregistering is currently not supported.
 */
int
dss_register_dbtree_classes(void)
{
	int rc;

	rc = dbtree_class_register(DBTREE_CLASS_KV, 0 /* feats */, &dbtree_kv_ops);
	if (rc != 0) {
		D_ERROR("failed to register DBTREE_CLASS_KV: " DF_RC "\n", DP_RC(rc));
		return rc;
	}

	rc = dbtree_class_register(DBTREE_CLASS_IV, BTR_FEAT_UINT_KEY | BTR_FEAT_DIRECT_KEY,
				   &dbtree_iv_ops);
	if (rc != 0) {
		D_ERROR("failed to register DBTREE_CLASS_IV: " DF_RC "\n", DP_RC(rc));
		return rc;
	}

	rc = dbtree_class_register(DBTREE_CLASS_IFV, BTR_FEAT_UINT_KEY | BTR_FEAT_DIRECT_KEY,
				   &dbtree_ifv_ops);
	if (rc != 0) {
		D_ERROR("failed to register DBTREE_CLASS_IFV: " DF_RC "\n", DP_RC(rc));
		return rc;
	}

	rc = dbtree_class_register(DBTREE_CLASS_NV, BTR_FEAT_DIRECT_KEY, &dbtree_nv_ops);
	if (rc != 0) {
		D_ERROR("failed to register DBTREE_CLASS_NV: " DF_RC "\n", DP_RC(rc));
		return rc;
	}

	rc = dbtree_class_register(DBTREE_CLASS_UV, 0 /* feats */, &dbtree_uv_ops);
	if (rc != 0) {
		D_ERROR("failed to register DBTREE_CLASS_UV: " DF_RC "\n", DP_RC(rc));
		return rc;
	}

	rc = dbtree_class_register(DBTREE_CLASS_EC, BTR_FEAT_UINT_KEY /* feats */, &dbtree_ec_ops);
	if (rc != 0) {
		D_ERROR("failed to register DBTREE_CLASS_EC: " DF_RC "\n", DP_RC(rc));
		return rc;
	}

	return rc;
}
