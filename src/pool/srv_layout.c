/*
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * ds_pool: Pool Server Storage Layout Definitions
 */

#define D_LOGFAC DD_FAC(pool)

#include <daos_srv/rdb.h>
#include <daos_srv/security.h>
#include <daos/pool.h>
#include "srv_layout.h"

/** Root KVS */
RDB_STRING_KEY(ds_pool_prop_, map_version);
RDB_STRING_KEY(ds_pool_prop_, map_buffer);
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
RDB_STRING_KEY(ds_pool_prop_, ec_cell_sz);
RDB_STRING_KEY(ds_pool_prop_, redun_fac);
RDB_STRING_KEY(ds_pool_prop_, ec_pda);
RDB_STRING_KEY(ds_pool_prop_, rp_pda);
RDB_STRING_KEY(ds_pool_prop_, perf_domain);
RDB_STRING_KEY(ds_pool_attr_, user);
RDB_STRING_KEY(ds_pool_prop_, policy);
RDB_STRING_KEY(ds_pool_prop_, global_version);
RDB_STRING_KEY(ds_pool_prop_, upgrade_status);
RDB_STRING_KEY(ds_pool_prop_, upgrade_global_version);
RDB_STRING_KEY(ds_pool_prop_, scrub_mode);
RDB_STRING_KEY(ds_pool_prop_, scrub_freq);
RDB_STRING_KEY(ds_pool_prop_, scrub_thresh);
RDB_STRING_KEY(ds_pool_prop_, svc_redun_fac);
RDB_STRING_KEY(ds_pool_prop_, obj_version);
RDB_STRING_KEY(ds_pool_prop_, checkpoint_mode);
RDB_STRING_KEY(ds_pool_prop_, checkpoint_freq);
RDB_STRING_KEY(ds_pool_prop_, checkpoint_thresh);

/** default properties, should cover all optional pool properties */
struct daos_prop_entry pool_prop_entries_default[DAOS_PROP_PO_NUM] = {
    {
	.dpe_type = DAOS_PROP_PO_LABEL,
	.dpe_str  = DAOS_PROP_PO_LABEL_DEFAULT,
    },
    {
	.dpe_type = DAOS_PROP_PO_SPACE_RB,
	.dpe_val  = 0,
    },
    {
	.dpe_type = DAOS_PROP_PO_SELF_HEAL,
	.dpe_val  = DAOS_SELF_HEAL_AUTO_EXCLUDE | DAOS_SELF_HEAL_AUTO_REBUILD,
    },
    {
	.dpe_type = DAOS_PROP_PO_RECLAIM,
	.dpe_val  = DAOS_RECLAIM_LAZY,
    },
    {
	.dpe_type = DAOS_PROP_PO_ACL, .dpe_val_ptr = NULL, /* generated dynamically */
    },
    {
	.dpe_type = DAOS_PROP_PO_OWNER,
	.dpe_str  = "NOBODY@",
    },
    {
	.dpe_type = DAOS_PROP_PO_OWNER_GROUP,
	.dpe_str  = "NOBODY@",
    },
    {
	.dpe_type    = DAOS_PROP_PO_SVC_LIST,
	.dpe_val_ptr = NULL,
    },
    {
	.dpe_type = DAOS_PROP_PO_EC_CELL_SZ,
	.dpe_val  = DAOS_EC_CELL_DEF,
    },
    {
	.dpe_type = DAOS_PROP_PO_REDUN_FAC,
	.dpe_val  = DAOS_PROP_PO_REDUN_FAC_DEFAULT,
    },
    {
	.dpe_type = DAOS_PROP_PO_EC_PDA,
	.dpe_val  = DAOS_PROP_PO_EC_PDA_DEFAULT,
    },
    {
	.dpe_type = DAOS_PROP_PO_RP_PDA,
	.dpe_val  = DAOS_PROP_PO_RP_PDA_DEFAULT,
    },
    {
	.dpe_type = DAOS_PROP_PO_POLICY,
	.dpe_str  = DAOS_PROP_POLICYSTR_DEFAULT,
    },
    {
	.dpe_type = DAOS_PROP_PO_GLOBAL_VERSION,
	.dpe_val  = DAOS_POOL_GLOBAL_VERSION,
    },
    {
	.dpe_type = DAOS_PROP_PO_UPGRADE_STATUS,
	.dpe_val  = DAOS_UPGRADE_STATUS_NOT_STARTED,
    },
    {
	.dpe_type = DAOS_PROP_PO_SCRUB_MODE,
	.dpe_val  = DAOS_PROP_PO_SCRUB_MODE_DEFAULT,
    },
    {
	.dpe_type = DAOS_PROP_PO_SCRUB_FREQ,
	.dpe_val  = DAOS_PROP_PO_SCRUB_FREQ_DEFAULT,
    },
    {
	.dpe_type = DAOS_PROP_PO_SCRUB_THRESH,
	.dpe_val  = DAOS_PROP_PO_SCRUB_THRESH_DEFAULT,
    },
    {
	.dpe_type = DAOS_PROP_PO_SVC_REDUN_FAC,
	.dpe_val  = DAOS_PROP_PO_SVC_REDUN_FAC_DEFAULT,
    },
    {
	.dpe_type = DAOS_PROP_PO_OBJ_VERSION,
	.dpe_val  = DS_POOL_OBJ_VERSION,
    }, {
	.dpe_type = DAOS_PROP_PO_PERF_DOMAIN,
	.dpe_val  = DAOS_PROP_PO_PERF_DOMAIN_DEFAULT,
    },
    {
	.dpe_type = DAOS_PROP_PO_CHECKPOINT_MODE,
	.dpe_val  = DAOS_PROP_PO_CHECKPOINT_MODE_DEFAULT,
    },
    {
	.dpe_type = DAOS_PROP_PO_CHECKPOINT_FREQ,
	.dpe_val  = DAOS_PROP_PO_CHECKPOINT_FREQ_DEFAULT,
    },
    {
	.dpe_type = DAOS_PROP_PO_CHECKPOINT_THRESH,
	.dpe_val  = DAOS_PROP_PO_CHECKPOINT_THRESH_DEFAULT,
    },
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
