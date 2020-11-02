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
ds_mgmt_pool_get_acl(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		     daos_prop_t **acl)
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
ds_mgmt_pool_overwrite_acl(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			   struct daos_acl *acl, daos_prop_t **result)
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
ds_mgmt_pool_update_acl(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			struct daos_acl *acl, daos_prop_t **result)
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
ds_mgmt_pool_delete_acl(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
			const char *principal, daos_prop_t **result)
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

int		ds_mgmt_pool_set_prop_return;
daos_prop_t	*ds_mgmt_pool_set_prop_prop;
daos_prop_t	*ds_mgmt_pool_set_prop_result;
void		*ds_mgmt_pool_set_prop_result_ptr;
int
ds_mgmt_pool_set_prop(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		      daos_prop_t *prop,
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
	daos_prop_free(ds_mgmt_pool_set_prop_prop);
}

/*
 * Mock ds_mgmt_pool_list_cont
 */
int				 ds_mgmt_pool_list_cont_return;
struct daos_pool_cont_info	*ds_mgmt_pool_list_cont_out;
uint64_t			 ds_mgmt_pool_list_cont_nc_out;

int ds_mgmt_pool_list_cont(uuid_t uuid, d_rank_list_t *svc_ranks,
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

int			ds_mgmt_pool_query_return;
uuid_t			ds_mgmt_pool_query_uuid;
daos_pool_info_t	ds_mgmt_pool_query_info_out;
daos_pool_info_t	ds_mgmt_pool_query_info_in;
void			*ds_mgmt_pool_query_info_ptr;
int
ds_mgmt_pool_query(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		   daos_pool_info_t *pool_info)
{
	uuid_copy(ds_mgmt_pool_query_uuid, pool_uuid);
	ds_mgmt_pool_query_info_ptr = (void *)pool_info;
	if (pool_info != NULL) {
		ds_mgmt_pool_query_info_in = *pool_info;
		*pool_info = ds_mgmt_pool_query_info_out;
	}
	return ds_mgmt_pool_query_return;
}

void
mock_ds_mgmt_pool_query_setup(void)
{
	ds_mgmt_pool_query_return = 0;
	uuid_clear(ds_mgmt_pool_query_uuid);
	ds_mgmt_pool_query_info_ptr = NULL;
	memset(&ds_mgmt_pool_query_info_out, 0, sizeof(daos_pool_info_t));
}

int	ds_mgmt_cont_set_owner_return;
uuid_t	ds_mgmt_cont_set_owner_pool;
uuid_t	ds_mgmt_cont_set_owner_cont;
char	*ds_mgmt_cont_set_owner_user;
char	*ds_mgmt_cont_set_owner_group;
int
ds_mgmt_cont_set_owner(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		       uuid_t cont_uuid, const char *user,
		       const char *group)
{
	uuid_copy(ds_mgmt_cont_set_owner_pool, pool_uuid);
	uuid_copy(ds_mgmt_cont_set_owner_cont, cont_uuid);
	if (user != NULL)
		D_STRNDUP(ds_mgmt_cont_set_owner_user, user,
			  DAOS_ACL_MAX_PRINCIPAL_LEN);
	if (group != NULL)
		D_STRNDUP(ds_mgmt_cont_set_owner_group, group,
			  DAOS_ACL_MAX_PRINCIPAL_LEN);

	return ds_mgmt_cont_set_owner_return;
}

void
mock_ds_mgmt_cont_set_owner_setup(void)
{
	ds_mgmt_cont_set_owner_return = 0;

	uuid_clear(ds_mgmt_cont_set_owner_pool);
	uuid_clear(ds_mgmt_cont_set_owner_cont);
	ds_mgmt_cont_set_owner_user = NULL;
	ds_mgmt_cont_set_owner_group = NULL;
}
void mock_ds_mgmt_cont_set_owner_teardown(void)
{
	D_FREE(ds_mgmt_cont_set_owner_user);
	D_FREE(ds_mgmt_cont_set_owner_group);
}

int     ds_mgmt_target_update_return;
uuid_t  ds_mgmt_target_update_uuid;
int
ds_mgmt_pool_target_update_state(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
				 uint32_t rank,
				 struct pool_target_id_list *target_list,
				 pool_comp_state_t state)
{
	uuid_copy(ds_mgmt_target_update_uuid, pool_uuid);
	return ds_mgmt_target_update_return;
}

void
mock_ds_mgmt_tgt_update_setup(void)
{
	ds_mgmt_target_update_return = 0;
	uuid_clear(ds_mgmt_target_update_uuid);
}

int     ds_mgmt_pool_extend_return;
uuid_t  ds_mgmt_pool_extend_uuid;
int
ds_mgmt_pool_extend(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		    d_rank_list_t *rank_list,
		    char *tgt_dev,  size_t scm_size, size_t nvme_size)
{
	uuid_copy(ds_mgmt_pool_extend_uuid, pool_uuid);
	return ds_mgmt_pool_extend_return;
}

void
mock_ds_mgmt_pool_extend_setup(void)
{
	ds_mgmt_pool_extend_return = 0;
	uuid_clear(ds_mgmt_pool_extend_uuid);
}

int     ds_mgmt_pool_evict_return;
uuid_t  ds_mgmt_pool_evict_uuid;
int
ds_mgmt_evict_pool(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		   const char *group)
{
	uuid_copy(ds_mgmt_pool_evict_uuid, pool_uuid);
	return ds_mgmt_pool_evict_return;
}

void
mock_ds_mgmt_pool_evict_setup(void)
{
	ds_mgmt_pool_evict_return = 0;
	uuid_clear(ds_mgmt_pool_evict_uuid);
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

size_t
ds_rsvc_get_md_cap(void)
{
	return 0;
}

int
ds_mgmt_get_attach_info_handler(Mgmt__GetAttachInfoResp *resp, bool all_ranks)
{
	return 0;
}

int
ds_mgmt_svc_start(void)
{
	return 0;
}

int
ds_mgmt_group_update_handler(struct mgmt_grp_up_in *in)
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
ds_mgmt_destroy_pool(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		     const char *group, uint32_t force)
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
