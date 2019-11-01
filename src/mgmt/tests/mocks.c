/*
 * (C) Copyright 2019 Intel Corporation.
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/**
 * Mocks for DAOS mgmt unit tests
 */

#include "../srv.pb-c.h"
#include "../srv_internal.h"
#include "mocks.h"

/*
 * Mocks
 */

int		ds_mgmt_pool_get_acl_return;
struct daos_acl	*ds_mgmt_pool_get_acl_return_acl;
uuid_t		ds_mgmt_pool_get_acl_uuid;
void		*ds_mgmt_pool_get_acl_acl_ptr;
int
ds_mgmt_pool_get_acl(uuid_t pool_uuid, struct daos_acl **acl)
{
	uuid_copy(ds_mgmt_pool_get_acl_uuid, pool_uuid);
	ds_mgmt_pool_get_acl_acl_ptr = (void *)acl;

	if (acl != NULL)
		*acl = daos_acl_dup(ds_mgmt_pool_get_acl_return_acl);

	return ds_mgmt_pool_get_acl_return;
}

void
mock_ds_mgmt_pool_get_acl_setup(void)
{
	ds_mgmt_pool_get_acl_return = 0;
	ds_mgmt_pool_get_acl_return_acl = NULL;
	uuid_clear(ds_mgmt_pool_get_acl_uuid);
	ds_mgmt_pool_get_acl_acl_ptr = NULL;
}

void
mock_ds_mgmt_pool_get_acl_teardown(void)
{
	daos_acl_free(ds_mgmt_pool_get_acl_return_acl);
	ds_mgmt_pool_get_acl_return_acl = NULL;
}

int		ds_mgmt_pool_overwrite_acl_return;
uuid_t		ds_mgmt_pool_overwrite_acl_uuid;
struct daos_acl	*ds_mgmt_pool_overwrite_acl_acl;
struct daos_acl	*ds_mgmt_pool_overwrite_acl_result;
void		*ds_mgmt_pool_overwrite_acl_result_ptr;
int
ds_mgmt_pool_overwrite_acl(uuid_t pool_uuid, struct daos_acl *acl,
			   struct daos_acl **result)
{
	uuid_copy(ds_mgmt_pool_overwrite_acl_uuid, pool_uuid);
	if (acl != NULL)
		ds_mgmt_pool_overwrite_acl_acl = daos_acl_dup(acl);
	ds_mgmt_pool_overwrite_acl_result_ptr = (void *)result;
	if (result != NULL)
		*result = daos_acl_dup(ds_mgmt_pool_overwrite_acl_result);
	return ds_mgmt_pool_overwrite_acl_return;
}

void
mock_ds_mgmt_pool_overwrite_acl_setup(void)
{
	ds_mgmt_pool_overwrite_acl_return = 0;
	uuid_clear(ds_mgmt_pool_overwrite_acl_uuid);
	ds_mgmt_pool_overwrite_acl_acl = NULL;
	ds_mgmt_pool_overwrite_acl_result = NULL;
	ds_mgmt_pool_overwrite_acl_result_ptr = NULL;
}

void
mock_ds_mgmt_pool_overwrite_acl_teardown(void)
{
	daos_acl_free(ds_mgmt_pool_overwrite_acl_acl);
	daos_acl_free(ds_mgmt_pool_overwrite_acl_result);
}

/*
 * Stubs, to avoid linker errors
 * TODO: Implement mocks when there is a test that uses these
 */
int
crt_rank_self_set(d_rank_t rank)
{
	return 0;
}

void
dss_init_state_set(enum dss_init_state state)
{
}

int
ds_mgmt_svc_start(bool create, size_t size, bool bootstrap, uuid_t srv_uuid,
		  char *addr)
{
	return 0;
}

size_t
ds_rsvc_get_md_cap(void)
{
	return 0;
}

int
ds_mgmt_get_attach_info_handler(Mgmt__GetAttachInfoResp *resp)
{
	return 0;
}

int
ds_mgmt_join_handler(struct mgmt_join_in *in, struct mgmt_join_out *out)
{
	return 0;
}

int
ds_mgmt_create_pool(uuid_t pool_uuid, const char *group, char *tgt_dev,
		    d_rank_list_t *targets, size_t scm_size,
		    size_t nvme_size, daos_prop_t *prop, uint32_t svc_nr,
		    d_rank_list_t **svcp)
{
	return 0;
}

int
ds_mgmt_destroy_pool(uuid_t pool_uuid, const char *group, uint32_t force)
{
	return 0;
}

int
ds_mgmt_bio_health_query(struct mgmt_bio_health *mbh, uuid_t uuid,
			 char *tgt_id)
{
	return 0;
}

int
ds_mgmt_smd_list_devs(Mgmt__SmdDevResp *resp)
{
	return 0;
}

int
ds_mgmt_smd_list_pools(Mgmt__SmdPoolResp *resp)
{
	return 0;
}
