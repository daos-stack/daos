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
daos_prop_t	*ds_mgmt_pool_get_acl_return_acl;
uuid_t		ds_mgmt_pool_get_acl_uuid;
void		*ds_mgmt_pool_get_acl_acl_ptr;
int
ds_mgmt_pool_get_acl(uuid_t pool_uuid, daos_prop_t **acl)
{
	uuid_copy(ds_mgmt_pool_get_acl_uuid, pool_uuid);
	ds_mgmt_pool_get_acl_acl_ptr = (void *)acl;

	if (acl != NULL && ds_mgmt_pool_get_acl_return_acl != NULL) {
		size_t len = ds_mgmt_pool_get_acl_return_acl->dpp_nr;

		/*
		 * Need to manually copy to allow mock to return potentially
		 * invalid values.
		 */
		*acl = daos_prop_alloc(len);
		daos_prop_copy(*acl, ds_mgmt_pool_get_acl_return_acl);
	}

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
	daos_prop_free(ds_mgmt_pool_get_acl_return_acl);
	ds_mgmt_pool_get_acl_return_acl = NULL;
}

int		ds_mgmt_pool_overwrite_acl_return;
uuid_t		ds_mgmt_pool_overwrite_acl_uuid;
struct daos_acl	*ds_mgmt_pool_overwrite_acl_acl;
daos_prop_t	*ds_mgmt_pool_overwrite_acl_result;
void		*ds_mgmt_pool_overwrite_acl_result_ptr;
int
ds_mgmt_pool_overwrite_acl(uuid_t pool_uuid, struct daos_acl *acl,
			   daos_prop_t **result)
{
	uuid_copy(ds_mgmt_pool_overwrite_acl_uuid, pool_uuid);
	if (acl != NULL)
		ds_mgmt_pool_overwrite_acl_acl = daos_acl_dup(acl);
	ds_mgmt_pool_overwrite_acl_result_ptr = (void *)result;
	if (result != NULL)
		*result = daos_prop_dup(ds_mgmt_pool_overwrite_acl_result,
					true);
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
	daos_prop_free(ds_mgmt_pool_overwrite_acl_result);
}

int		ds_mgmt_pool_update_acl_return;
uuid_t		ds_mgmt_pool_update_acl_uuid;
struct daos_acl	*ds_mgmt_pool_update_acl_acl;
daos_prop_t	*ds_mgmt_pool_update_acl_result;
void		*ds_mgmt_pool_update_acl_result_ptr;
int
ds_mgmt_pool_update_acl(uuid_t pool_uuid, struct daos_acl *acl,
			daos_prop_t **result)
{
	uuid_copy(ds_mgmt_pool_update_acl_uuid, pool_uuid);
	if (acl != NULL)
		ds_mgmt_pool_update_acl_acl = daos_acl_dup(acl);
	ds_mgmt_pool_update_acl_result_ptr = (void *)result;
	if (result != NULL)
		*result = daos_prop_dup(ds_mgmt_pool_update_acl_result, true);
	return ds_mgmt_pool_update_acl_return;
}

void
mock_ds_mgmt_pool_update_acl_setup(void)
{
	ds_mgmt_pool_update_acl_return = 0;
	uuid_clear(ds_mgmt_pool_update_acl_uuid);
	ds_mgmt_pool_update_acl_acl = NULL;
	ds_mgmt_pool_update_acl_result = NULL;
	ds_mgmt_pool_update_acl_result_ptr = NULL;
}

void
mock_ds_mgmt_pool_update_acl_teardown(void)
{
	daos_acl_free(ds_mgmt_pool_update_acl_acl);
	daos_prop_free(ds_mgmt_pool_update_acl_result);
}

int		ds_mgmt_pool_delete_acl_return;
uuid_t		ds_mgmt_pool_delete_acl_uuid;
const char	*ds_mgmt_pool_delete_acl_principal;
daos_prop_t	*ds_mgmt_pool_delete_acl_result;
void		*ds_mgmt_pool_delete_acl_result_ptr;
int
ds_mgmt_pool_delete_acl(uuid_t pool_uuid, const char *principal,
			daos_prop_t **result)
{
	uuid_copy(ds_mgmt_pool_delete_acl_uuid, pool_uuid);
	ds_mgmt_pool_delete_acl_principal = principal;
	ds_mgmt_pool_delete_acl_result_ptr = (void *)result;
	if (result != NULL)
		*result = daos_prop_dup(ds_mgmt_pool_delete_acl_result, true);
	return ds_mgmt_pool_delete_acl_return;
}

void
mock_ds_mgmt_pool_delete_acl_setup(void)
{
	ds_mgmt_pool_delete_acl_return = 0;
	uuid_clear(ds_mgmt_pool_delete_acl_uuid);
	ds_mgmt_pool_delete_acl_principal = NULL;
	ds_mgmt_pool_delete_acl_result = NULL;
	ds_mgmt_pool_delete_acl_result_ptr = NULL;
}

void
mock_ds_mgmt_pool_delete_acl_teardown(void)
{
	daos_prop_free(ds_mgmt_pool_delete_acl_result);
}

int				ds_mgmt_list_pools_return;
char				ds_mgmt_list_pools_group[DAOS_SYS_NAME_MAX + 1];
void				*ds_mgmt_list_pools_npools_ptr;
uint64_t			ds_mgmt_list_pools_npools;
void				*ds_mgmt_list_pools_poolsp_ptr;
struct mgmt_list_pools_one	*ds_mgmt_list_pools_poolsp_out;
void				*ds_mgmt_list_pools_len_ptr;
size_t				ds_mgmt_list_pools_len_out;
int
ds_mgmt_list_pools(const char *group, uint64_t *npools,
		   struct mgmt_list_pools_one **poolsp, size_t *pools_len)
{
	size_t i;

	strncpy(ds_mgmt_list_pools_group, group, DAOS_SYS_NAME_MAX);

	ds_mgmt_list_pools_npools_ptr = (void *)npools;
	if (npools != NULL)
		ds_mgmt_list_pools_npools = *npools;

	ds_mgmt_list_pools_poolsp_ptr = (void *)poolsp;
	if (poolsp != NULL && ds_mgmt_list_pools_poolsp_out != NULL) {
		D_ALLOC_ARRAY(*poolsp, ds_mgmt_list_pools_len_out);
		for (i = 0; i < ds_mgmt_list_pools_len_out; i++) {
			uuid_copy((*poolsp)[i].lp_puuid,
				  ds_mgmt_list_pools_poolsp_out[i].lp_puuid);
			(*poolsp)[i].lp_svc = d_rank_list_alloc(0);
			d_rank_list_copy((*poolsp)[i].lp_svc,
				ds_mgmt_list_pools_poolsp_out[i].lp_svc);
		}
	}

	ds_mgmt_list_pools_len_ptr = (void *)pools_len;
	if (pools_len != NULL)
		*pools_len = ds_mgmt_list_pools_len_out;

	return ds_mgmt_list_pools_return;
}

void
mock_ds_mgmt_list_pools_setup(void)
{
	ds_mgmt_list_pools_return = 0;

	memset(ds_mgmt_list_pools_group, 0, sizeof(ds_mgmt_list_pools_group));

	ds_mgmt_list_pools_npools_ptr = NULL;
	ds_mgmt_list_pools_npools = 0;

	ds_mgmt_list_pools_len_ptr = NULL;
	ds_mgmt_list_pools_len_out = 0;

	ds_mgmt_list_pools_poolsp_ptr = NULL;
	ds_mgmt_list_pools_poolsp_out = NULL;
}

void
mock_ds_mgmt_list_pools_gen_pools(size_t num_pools)
{
	size_t i;

	ds_mgmt_list_pools_len_out = num_pools;

	D_ALLOC_ARRAY(ds_mgmt_list_pools_poolsp_out, num_pools);
	for (i = 0; i < num_pools; i++) {
		struct mgmt_list_pools_one *pool;

		pool = &(ds_mgmt_list_pools_poolsp_out[i]);
		uuid_generate(pool->lp_puuid);

		pool->lp_svc = d_rank_list_alloc(1);
		pool->lp_svc->rl_ranks[0] = i;
	}
}

void
mock_ds_mgmt_list_pools_teardown(void)
{
	ds_mgmt_free_pool_list(&ds_mgmt_list_pools_poolsp_out,
			       ds_mgmt_list_pools_len_out);
}

void
ds_mgmt_free_pool_list(struct mgmt_list_pools_one **poolsp, uint64_t len)
{
	uint64_t i;

	if (*poolsp != NULL) {
		for (i = 0; i < len; i++)
			d_rank_list_free((*poolsp)[i].lp_svc);

		D_FREE(*poolsp);
	}
}

int		ds_mgmt_pool_set_prop_return;
daos_prop_t	*ds_mgmt_pool_set_prop_prop;
daos_prop_t	*ds_mgmt_pool_set_prop_result;
void		*ds_mgmt_pool_set_prop_result_ptr;
int
ds_mgmt_pool_set_prop(uuid_t pool_uuid, daos_prop_t *prop,
		      daos_prop_t **result)
{
	if (prop != NULL)
		ds_mgmt_pool_set_prop_prop = daos_prop_dup(prop, true);
	ds_mgmt_pool_set_prop_result_ptr = (void *)result;

	if (result != NULL && ds_mgmt_pool_set_prop_result != NULL) {
		size_t len = ds_mgmt_pool_set_prop_result->dpp_nr;

		*result = daos_prop_alloc(len);
		daos_prop_copy(*result, ds_mgmt_pool_set_prop_result);
	}

	return ds_mgmt_pool_set_prop_return;
}

void
mock_ds_mgmt_pool_set_prop_setup(void)
{
	ds_mgmt_pool_set_prop_return = 0;
	ds_mgmt_pool_set_prop_prop = NULL;
	ds_mgmt_pool_set_prop_result = NULL;
	ds_mgmt_pool_set_prop_result_ptr = NULL;
}

void
mock_ds_mgmt_pool_set_prop_teardown(void)
{
	daos_prop_free(ds_mgmt_pool_set_prop_result);
}

/*
 * Mock ds_mgmt_pool_list_cont
 */
int				 ds_mgmt_pool_list_cont_return;
struct daos_pool_cont_info	*ds_mgmt_pool_list_cont_out;
uint64_t			 ds_mgmt_pool_list_cont_nc_out;

int ds_mgmt_pool_list_cont(uuid_t uuid,
			   struct daos_pool_cont_info **containers,
			   uint64_t *ncontainers)
{
	if (containers != NULL && ncontainers != NULL &&
	    ds_mgmt_pool_list_cont_out != NULL) {
		*ncontainers = ds_mgmt_pool_list_cont_nc_out;
		D_ALLOC_ARRAY(*containers, *ncontainers);
		memcpy(*containers, ds_mgmt_pool_list_cont_out,
		       *ncontainers * sizeof(struct daos_pool_cont_info));
	}

	return ds_mgmt_pool_list_cont_return;
}

void
mock_ds_mgmt_list_cont_gen_cont(size_t ncont) {
	size_t i;

	D_ALLOC_ARRAY(ds_mgmt_pool_list_cont_out, ncont);
	ds_mgmt_pool_list_cont_nc_out = ncont;
	for (i = 0; i < ncont; i++)
		uuid_generate(ds_mgmt_pool_list_cont_out[i].pci_uuid);
}

void
mock_ds_mgmt_pool_list_cont_setup(void)
{
	ds_mgmt_pool_list_cont_return = 0;
	ds_mgmt_pool_list_cont_nc_out = 0;
	ds_mgmt_pool_list_cont_out = NULL;
}

void mock_ds_mgmt_pool_list_cont_teardown(void)
{
	if (ds_mgmt_pool_list_cont_out != NULL) {
		D_FREE(ds_mgmt_pool_list_cont_out);
		ds_mgmt_pool_list_cont_out = NULL;
	}
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

int
ds_mgmt_dev_state_query(uuid_t uuid, Mgmt__DevStateResp *resp)
{
	return 0;
}

int
ds_mgmt_dev_set_faulty(uuid_t uuid, Mgmt__DevStateResp *resp)
{
	return 0;
}
