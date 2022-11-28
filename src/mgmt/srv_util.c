/*
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
ds_mgmt_group_update(struct server_entry *servers, int nservers, uint32_t version)
{
	struct dss_module_info *info = dss_get_module_info();
	uint32_t		version_current;
	d_rank_list_t	       *ranks = NULL;
	uint64_t	       *incarnations = NULL;
	char		      **uris = NULL;
	int			i;
	int			rc;

	D_ASSERTF(info->dmi_ctx_id == 0, "%d\n", info->dmi_ctx_id);

	rc = crt_group_version(NULL /* grp */, &version_current);
	D_ASSERTF(rc == 0, "%d\n", rc);
	D_DEBUG(DB_MGMT, "current=%u in=%u in_nservers=%d\n", version_current, version, nservers);
	if (version <= version_current) {
		rc = 0;
		goto out;
	}

	ranks = d_rank_list_alloc(nservers);
	if (ranks == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	for (i = 0; i < nservers; i++)
		ranks->rl_ranks[i] = servers[i].se_rank;

	D_ALLOC_ARRAY(incarnations, nservers);
	if (incarnations == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	for (i = 0; i < nservers; i++)
		incarnations[i] = servers[i].se_incarnation;

	D_ALLOC_ARRAY(uris, nservers);
	if (uris == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	for (i = 0; i < nservers; i++)
		uris[i] = servers[i].se_uri;

	rc = crt_group_primary_modify(NULL /* grp */, &info->dmi_ctx, 1 /* num_ctxs */, ranks,
				      incarnations, uris, CRT_GROUP_MOD_OP_REPLACE, version);
	if (rc != 0) {
		D_ERROR("failed to update group: %u -> %u: %d\n", version_current, version, rc);
		goto out;
	}

	D_INFO("updated group: %u -> %u: %d ranks\n", version_current, version, nservers);
out:
	if (uris != NULL)
		D_FREE(uris);
	if (incarnations != NULL)
		D_FREE(incarnations);
	if (ranks != NULL)
		d_rank_list_free(ranks);
	return rc;
}
