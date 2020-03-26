/**
 * (C) Copyright 2017-2020 Intel Corporation.
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
 * ds_pool: Pool Server Storage Layout Definitions
 */
#define D_LOGFAC	DD_FAC(pool)

#include <daos_srv/rdb.h>
#include <daos_srv/security.h>
#include "srv_layout.h"

/** Root KVS */
RDB_STRING_KEY(ds_pool_prop_, map_version);
RDB_STRING_KEY(ds_pool_prop_, map_buffer);
RDB_STRING_KEY(ds_pool_prop_, map_uuids);
RDB_STRING_KEY(ds_pool_prop_, label);
RDB_STRING_KEY(ds_pool_prop_, acl);
RDB_STRING_KEY(ds_pool_prop_, space_rb);
RDB_STRING_KEY(ds_pool_prop_, self_heal);
RDB_STRING_KEY(ds_pool_prop_, reclaim);
RDB_STRING_KEY(ds_pool_prop_, owner);
RDB_STRING_KEY(ds_pool_prop_, owner_group);
RDB_STRING_KEY(ds_pool_prop_, connectable);
RDB_STRING_KEY(ds_pool_prop_, nhandles);

/** pool handle KVS */
RDB_STRING_KEY(ds_pool_prop_, handles);

/** user attributed KVS */
RDB_STRING_KEY(ds_pool_attr_, user);

/** default properties, should cover all optional pool properties */
struct daos_prop_entry pool_prop_entries_default[DAOS_PROP_PO_NUM] = {
	{
		.dpe_type	= DAOS_PROP_PO_LABEL,
		.dpe_str	= "pool label not set",
	}, {
		.dpe_type	= DAOS_PROP_PO_SPACE_RB,
		.dpe_val	= 0,
	}, {
		.dpe_type	= DAOS_PROP_PO_SELF_HEAL,
		.dpe_val	= DAOS_SELF_HEAL_AUTO_EXCLUDE |
				  DAOS_SELF_HEAL_AUTO_REBUILD,
	}, {
		.dpe_type	= DAOS_PROP_PO_RECLAIM,
		.dpe_val	= DAOS_RECLAIM_LAZY,
	}, {
		.dpe_type	= DAOS_PROP_PO_ACL,
		.dpe_val_ptr	= NULL, /* generated dynamically */
	}, {
		.dpe_type	= DAOS_PROP_PO_OWNER,
		.dpe_str	= "NOBODY@",
	}, {
		.dpe_type	= DAOS_PROP_PO_OWNER_GROUP,
		.dpe_str	= "NOBODY@",
	}, {
		.dpe_type	= DAOS_PROP_PO_SVC_LIST,
		.dpe_val_ptr	= NULL,
	}
};

daos_prop_t pool_prop_default = {
	.dpp_nr		= DAOS_PROP_PO_NUM,
	.dpp_entries	= pool_prop_entries_default,
};

int
ds_pool_prop_default_init(void)
{
	struct daos_prop_entry	*entry;

	entry = daos_prop_entry_get(&pool_prop_default, DAOS_PROP_PO_ACL);
	if (entry != NULL) {
		D_DEBUG(DB_MGMT,
			"Initializing default ACL pool prop\n");
		entry->dpe_val_ptr = ds_sec_alloc_default_daos_pool_acl();
		if (entry->dpe_val_ptr == NULL)
			return -DER_NOMEM;
	}

	return 0;
}

void
ds_pool_prop_default_fini(void)
{
	struct daos_prop_entry	*entry;

	entry = daos_prop_entry_get(&pool_prop_default, DAOS_PROP_PO_ACL);
	if (entry != NULL) {
		D_DEBUG(DB_MGMT, "Freeing default ACL pool prop\n");
		D_FREE(entry->dpe_val_ptr);
	}
}
