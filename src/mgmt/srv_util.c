/*
 * (C) Copyright 2019-2022 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
		DL_CDEBUG(rc == -DER_GRPVER, DLOG_INFO, DLOG_ERR, rc,
			  "failed to update group: %u -> %u", version_current, version);
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

int
ds_mgmt_get_group_status(uint32_t group_version, d_rank_t **dead_ranks_out,
			 size_t *n_dead_ranks_out)
{
	struct dss_module_info *info = dss_get_module_info();
	crt_group_t            *group;
	uint32_t                version;
	d_rank_list_t          *ranks;
	d_rank_t               *dead_ranks;
	size_t                  n_dead_ranks;
	int                     i;
	int                     rc;

	D_ASSERTF(info->dmi_ctx_id == 0, "%d\n", info->dmi_ctx_id);

	group = crt_group_lookup(NULL /* grp_id */);
	D_ASSERT(group != NULL);

	rc = crt_group_version(group, &version);
	D_ASSERTF(rc == 0, DF_RC "\n", DP_RC(rc));
	if (group_version != 0 && group_version != version) {
		rc = -DER_GRPVER;
		goto out;
	}

	rc = crt_group_ranks_get(group, &ranks);
	if (rc != 0) {
		DL_ERROR(rc, "failed to get group ranks");
		goto out;
	}

	D_ALLOC_ARRAY(dead_ranks, ranks->rl_nr);
	if (dead_ranks == NULL) {
		rc = -DER_NOMEM;
		goto out_ranks;
	}
	n_dead_ranks = 0;

	for (i = 0; i < ranks->rl_nr; i++) {
		struct swim_member_state state;

		rc = crt_rank_state_get(group, ranks->rl_ranks[i], &state);
		if (rc != 0) {
			DL_ERROR(rc, "failed to get rank state for rank %u", ranks->rl_ranks[i]);
			goto out_dead_ranks;
		}

		if (state.sms_status == SWIM_MEMBER_DEAD) {
			dead_ranks[n_dead_ranks] = ranks->rl_ranks[i];
			n_dead_ranks++;
		}
	}

out_dead_ranks:
	if (rc == 0) {
		*dead_ranks_out   = dead_ranks;
		*n_dead_ranks_out = n_dead_ranks;
	} else {
		D_FREE(dead_ranks);
	}
out_ranks:
	d_rank_list_free(ranks);
out:
	return rc;
}

static struct d_uuid *pool_blacklist;
static int            pool_blacklist_len;

static int
pbl_append(char *uuid_str)
{
	uuid_t         uuid;
	struct d_uuid *p;
	int            rc;

	rc = uuid_parse(uuid_str, uuid);
	if (rc != 0)
		return -DER_INVAL;

	/* Grow pool_blacklist by one element. */
	D_REALLOC_ARRAY(p, pool_blacklist, pool_blacklist_len, pool_blacklist_len + 1);
	if (p == NULL)
		return -DER_NOMEM;
	pool_blacklist = p;

	uuid_copy(pool_blacklist[pool_blacklist_len].uuid, uuid);
	pool_blacklist_len++;
	return 0;
}

/**
 * Create the (global) pool blacklist, a list of UUIDs of pools that shall be
 * skipped during the engine setup process, based on the environment variable
 * DAOS_POOL_BLACKLIST.
 */
int
ds_mgmt_pbl_create(void)
{
	char *name = "DAOS_POOL_BLACKLIST";
	char *value;
	char *sep = ",";
	char *uuid_str;
	char *c;
	int   rc;

	rc = d_agetenv_str(&value, name);
	if (rc == -DER_NONEXIST)
		return 0;
	else if (rc != 0)
		return rc;

	for (uuid_str = strtok_r(value, sep, &c); uuid_str != NULL;
	     uuid_str = strtok_r(NULL, sep, &c)) {
		rc = pbl_append(uuid_str);
		if (rc != 0) {
			DL_ERROR(rc, "failed to parse pool UUID in %s: '%s'", name, uuid_str);
			D_FREE(pool_blacklist);
			pool_blacklist_len = 0;
			break;
		}
	}

	d_freeenv_str(&value);
	return rc;
}

bool
ds_mgmt_pbl_has_pool(uuid_t uuid)
{
	int i;

	for (i = 0; i < pool_blacklist_len; i++)
		if (uuid_compare(pool_blacklist[i].uuid, uuid) == 0)
			return true;
	return false;
}

void
ds_mgmt_pbl_destroy(void)
{
	D_FREE(pool_blacklist);
	pool_blacklist_len = 0;
}