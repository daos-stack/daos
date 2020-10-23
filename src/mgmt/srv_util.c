/*
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
 * \file
 *
 * ds_mgmt: Management Server Utilities
 */

#define D_LOGFAC DD_FAC(mgmt)

#include "srv_internal.h"

/* Update the system group. */
int
ds_mgmt_group_update(crt_group_mod_op_t op, struct server_entry *servers,
		     int nservers, uint32_t version)
{
	struct dss_module_info *info = dss_get_module_info();
	uint32_t		version_current;
	d_rank_list_t	       *ranks = NULL;
	char		      **uris = NULL;
	int			i;
	int			rc;

	D_ASSERTF(info->dmi_ctx_id == 0, "%d\n", info->dmi_ctx_id);

	rc = crt_group_version(NULL /* grp */, &version_current);
	D_ASSERTF(rc == 0, "%d\n", rc);
	D_ASSERTF(version_current < version, "%u < %u\n", version_current,
		  version);
	D_DEBUG(DB_MGMT, "%u -> %u\n", version_current, version);

	ranks = d_rank_list_alloc(nservers);
	if (ranks == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	for (i = 0; i < nservers; i++)
		ranks->rl_ranks[i] = servers[i].se_rank;

	D_ALLOC_ARRAY(uris, nservers);
	if (uris == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	for (i = 0; i < nservers; i++)
		uris[i] = servers[i].se_uri;

	rc = crt_group_primary_modify(NULL /* grp */, &info->dmi_ctx,
				      1 /* num_ctxs */, ranks, uris, op,
				      version);
	if (rc != 0)
		D_ERROR("failed to update group (op=%d version=%u): %d\n",
			op, version, rc);

out:
	if (uris != NULL)
		D_FREE(uris);
	if (ranks != NULL)
		d_rank_list_free(ranks);
	return rc;
}

static int
map_update_bcast(crt_context_t ctx, uint32_t map_version,
		 int nservers, struct server_entry servers[])
{
	struct mgmt_tgt_map_update_in	*in;
	struct mgmt_tgt_map_update_out	*out;
	crt_opcode_t			opc;
	crt_rpc_t			*rpc;
	int				rc;

	D_DEBUG(DB_MGMT, "enter: version=%u nservers=%d\n", map_version,
		nservers);

	opc = DAOS_RPC_OPCODE(MGMT_TGT_MAP_UPDATE, DAOS_MGMT_MODULE, 1);
	rc = crt_corpc_req_create(ctx, NULL /* grp */,
				  NULL /* excluded_ranks */, opc,
				  NULL /* co_bulk_hdl */, NULL /* priv */,
				  0 /* flags */,
				  crt_tree_topo(CRT_TREE_KNOMIAL, 32), &rpc);
	if (rc != 0) {
		D_ERROR("failed to create system map update RPC: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}
	in = crt_req_get(rpc);
	in->tm_servers.ca_count = nservers;
	in->tm_servers.ca_arrays = servers;
	in->tm_map_version = map_version;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		goto out_rpc;

	out = crt_reply_get(rpc);
	if (out->tm_rc != 0)
		rc = -DER_IO;

out_rpc:
	crt_req_decref(rpc);
out:
	D_DEBUG(DB_MGMT, "leave: version=%u nservers=%d: "DF_RC"\n",
		map_version, nservers, DP_RC(rc));
	return rc;
}

int
ds_mgmt_group_update_handler(struct mgmt_grp_up_in *in)
{
	struct dss_module_info *info = dss_get_module_info();
	int			rc;

	rc = ds_mgmt_group_update(CRT_GROUP_MOD_OP_REPLACE, in->gui_servers,
				  in->gui_n_servers, in->gui_map_version);
	if (rc != 0)
		goto out;

	D_DEBUG(DB_MGMT, "set %d servers in map version %u\n",
		in->gui_n_servers, in->gui_map_version);

	rc = map_update_bcast(info->dmi_ctx, in->gui_map_version,
			      in->gui_n_servers, in->gui_servers);

out:
	return rc;
}
