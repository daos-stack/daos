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
