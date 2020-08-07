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
 * ds_cont: Container Server Storage Layout Definitions
 */
#define D_LOGFAC	DD_FAC(container)

#include <daos_srv/rdb.h>
#include <daos_srv/security.h>
#include "srv_layout.h"

/* Root KVS */
RDB_STRING_KEY(ds_cont_prop_, conts);
RDB_STRING_KEY(ds_cont_prop_, cont_handles);

/* Container properties KVS */
RDB_STRING_KEY(ds_cont_prop_, ghce);
RDB_STRING_KEY(ds_cont_prop_, ghpce);
RDB_STRING_KEY(ds_cont_prop_, max_oid);
RDB_STRING_KEY(ds_cont_prop_, label);
RDB_STRING_KEY(ds_cont_prop_, layout_type);
RDB_STRING_KEY(ds_cont_prop_, layout_ver);
RDB_STRING_KEY(ds_cont_prop_, csum);
RDB_STRING_KEY(ds_cont_prop_, csum_chunk_size);
RDB_STRING_KEY(ds_cont_prop_, csum_server_verify);
RDB_STRING_KEY(ds_cont_prop_, dedup);
RDB_STRING_KEY(ds_cont_prop_, dedup_threshold);
RDB_STRING_KEY(ds_cont_prop_, redun_fac);
RDB_STRING_KEY(ds_cont_prop_, redun_lvl);
RDB_STRING_KEY(ds_cont_prop_, snapshot_max);
RDB_STRING_KEY(ds_cont_prop_, compress);
RDB_STRING_KEY(ds_cont_prop_, encrypt);
RDB_STRING_KEY(ds_cont_prop_, acl);
RDB_STRING_KEY(ds_cont_prop_, owner);
RDB_STRING_KEY(ds_cont_prop_, owner_group);
RDB_STRING_KEY(ds_cont_prop_, lres);
RDB_STRING_KEY(ds_cont_prop_, lhes);
RDB_STRING_KEY(ds_cont_prop_, snapshots);
RDB_STRING_KEY(ds_cont_attr_, user);
RDB_STRING_KEY(ds_cont_prop_, handles);

/** default properties, should cover all optional container properties */
struct daos_prop_entry cont_prop_entries_default[CONT_PROP_NUM] = {
	{
		.dpe_type	= DAOS_PROP_CO_LABEL,
		.dpe_str	= "container label not set",
	}, {
		.dpe_type	= DAOS_PROP_CO_LAYOUT_TYPE,
		.dpe_val	= DAOS_PROP_CO_LAYOUT_UNKOWN,
	}, {
		.dpe_type	= DAOS_PROP_CO_LAYOUT_VER,
		.dpe_val	= 1,
	}, {
		.dpe_type	= DAOS_PROP_CO_CSUM,
		.dpe_val	= DAOS_PROP_CO_CSUM_OFF,
	}, {
		.dpe_type	= DAOS_PROP_CO_CSUM_CHUNK_SIZE,
		.dpe_val	= 32 * 1024, /** 32K */
	}, {
		.dpe_type	= DAOS_PROP_CO_CSUM_SERVER_VERIFY,
		.dpe_val	= DAOS_PROP_CO_CSUM_SV_OFF,
	}, {
		.dpe_type	= DAOS_PROP_CO_REDUN_FAC,
		.dpe_val	= DAOS_PROP_CO_REDUN_RF1,
	}, {
		.dpe_type	= DAOS_PROP_CO_REDUN_LVL,
		.dpe_val	= DAOS_PROP_CO_REDUN_RACK,
	}, {
		.dpe_type	= DAOS_PROP_CO_SNAPSHOT_MAX,
		.dpe_val	= 0, /* No limitation */
	}, {
		.dpe_type	= DAOS_PROP_CO_ACL,
		.dpe_val_ptr	= NULL, /* generated dynamically */
	}, {
		.dpe_type	= DAOS_PROP_CO_COMPRESS,
		.dpe_val	= DAOS_PROP_CO_COMPRESS_OFF,
	}, {
		.dpe_type	= DAOS_PROP_CO_ENCRYPT,
		.dpe_val	= DAOS_PROP_CO_ENCRYPT_OFF,
	}, {
		.dpe_type	= DAOS_PROP_CO_OWNER,
		.dpe_str	= "NOBODY@",
	}, {
		.dpe_type	= DAOS_PROP_CO_OWNER_GROUP,
		.dpe_str	= "NOBODY@",
	}, {
		.dpe_type	= DAOS_PROP_CO_DEDUP,
		.dpe_val	= DAOS_PROP_CO_DEDUP_OFF,
	}, {
		.dpe_type	= DAOS_PROP_CO_DEDUP_THRESHOLD,
		.dpe_val	= 4096,
	}
};

daos_prop_t cont_prop_default = {
	.dpp_nr		= CONT_PROP_NUM,
	.dpp_entries	= cont_prop_entries_default,
};

int
ds_cont_prop_default_init(void)
{
	struct daos_prop_entry	*entry;

	entry = daos_prop_entry_get(&cont_prop_default, DAOS_PROP_CO_ACL);
	if (entry != NULL) {
		D_DEBUG(DB_MGMT,
			"Initializing default ACL cont prop\n");
		entry->dpe_val_ptr = ds_sec_alloc_default_daos_cont_acl();
		if (entry->dpe_val_ptr == NULL)
			return -DER_NOMEM;
	}

	return 0;
}

void
ds_cont_prop_default_fini(void)
{
	struct daos_prop_entry	*entry;

	entry = daos_prop_entry_get(&cont_prop_default, DAOS_PROP_CO_ACL);
	if (entry != NULL) {
		D_DEBUG(DB_MGMT, "Freeing default ACL cont prop\n");
		D_FREE(entry->dpe_val_ptr);
	}
}

