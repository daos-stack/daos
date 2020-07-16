/**
 * (C) Copyright 2020 Intel Corporation.
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
 * ds_mgmt: Container Methods
 */
#define D_LOGFAC	DD_FAC(mgmt)

#include <daos_srv/container.h>
#include <daos/rpc.h>

#include "srv_internal.h"

static int
cont_set_prop(uuid_t pool_uuid, uuid_t cont_uuid, daos_prop_t *prop)
{
	int		rc = 0;
	d_rank_list_t	*ranks;

	/* Container svc colocated with pool svc */
	rc = get_pool_svc_ranks(pool_uuid, &ranks);
	if (rc != 0) {
		D_ERROR("Failed to get pool service ranks "DF_UUID" rc: %d\n",
			DP_UUID(pool_uuid), rc);
		goto out;
	}

	rc = ds_cont_svc_set_prop(pool_uuid, cont_uuid, ranks, prop);
	if (rc != 0)
		goto out_ranks;

out_ranks:
	d_rank_list_free(ranks);
out:
	return rc;
}

int
ds_mgmt_cont_set_owner(uuid_t pool_uuid, uuid_t cont_uuid, const char *user,
		       const char *group)
{
	int		rc = 0;
	daos_prop_t	*prop;
	uint32_t	prop_nr = 0;
	uint32_t	i = 0;

	D_DEBUG(DB_MGMT, "Setting owner for container "DF_UUID" in pool "
		DF_UUID"\n", DP_UUID(cont_uuid), DP_UUID(pool_uuid));

	if (user != NULL)
		prop_nr++;
	if (group != NULL)
		prop_nr++;
	if (prop_nr == 0) {
		D_ERROR("user and group both null\n");
		return -DER_INVAL;
	}

	prop = daos_prop_alloc(prop_nr);
	if (prop == NULL)
		return -DER_NOMEM;

	if (user != NULL) {
		prop->dpp_entries[i].dpe_type = DAOS_PROP_CO_OWNER;
		D_STRNDUP(prop->dpp_entries[i].dpe_str, user,
			  DAOS_ACL_MAX_PRINCIPAL_LEN);
		if (prop->dpp_entries[i].dpe_str == NULL)
			D_GOTO(out_prop, rc = -DER_NOMEM);
		i++;
	}

	if (group != NULL) {
		prop->dpp_entries[i].dpe_type = DAOS_PROP_CO_OWNER_GROUP;
		D_STRNDUP(prop->dpp_entries[i].dpe_str, group,
			  DAOS_ACL_MAX_PRINCIPAL_LEN);
		if (prop->dpp_entries[i].dpe_str == NULL)
			D_GOTO(out_prop, rc = -DER_NOMEM);
		i++;
	}

	rc = cont_set_prop(pool_uuid, cont_uuid, prop);
out_prop:
	daos_prop_free(prop);
	return rc;
}
