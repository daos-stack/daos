/*
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * Mocks for DAOS mgmt unit tests
 */

#include "../svc.pb-c.h"
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
	int rc;

	uuid_copy(ds_mgmt_pool_get_acl_uuid, pool_uuid);
	ds_mgmt_pool_get_acl_acl_ptr = (void *)acl;

	if (acl != NULL && ds_mgmt_pool_get_acl_return_acl != NULL) {
		size_t len = ds_mgmt_pool_get_acl_return_acl->dpp_nr;

		/*
		 * Need to manually copy to allow mock to return potentially
		 * invalid values.
		 */
		*acl = daos_prop_alloc(len);
		rc = daos_prop_copy(*acl, ds_mgmt_pool_get_acl_return_acl);
		if (rc != 0)
			return rc;
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
					true /* pool */, true /* input */);
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
		*result = daos_prop_dup(ds_mgmt_pool_update_acl_result,
					true /* pool */, true /* input */);
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
		*result = daos_prop_dup(ds_mgmt_pool_delete_acl_result,
					true /* pool */, true /* input */);
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
int
ds_mgmt_pool_set_prop(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		      daos_prop_t *prop)
{
	if (prop != NULL)
		ds_mgmt_pool_set_prop_prop = daos_prop_dup(prop, true, true);

	return ds_mgmt_pool_set_prop_return;
}

void
mock_ds_mgmt_pool_set_prop_setup(void)
{
	ds_mgmt_pool_set_prop_return = 0;
	ds_mgmt_pool_set_prop_prop = NULL;
}

void
mock_ds_mgmt_pool_set_prop_teardown(void)
{
	daos_prop_free(ds_mgmt_pool_set_prop_prop);
}

int		ds_mgmt_pool_get_prop_return;
daos_prop_t	*ds_mgmt_pool_get_prop_in;
daos_prop_t	*ds_mgmt_pool_get_prop_out;
int
ds_mgmt_pool_get_prop(uuid_t pool_uuid, d_rank_list_t *svc_ranks,
		      daos_prop_t *prop)
{
	int	rc;

	if (prop != NULL)
		ds_mgmt_pool_get_prop_in = daos_prop_dup(prop, true, true);

	rc = daos_prop_copy(prop, ds_mgmt_pool_get_prop_out);
	if (rc != 0)
		return rc;

	return ds_mgmt_pool_get_prop_return;
}

void
mock_ds_mgmt_pool_get_prop_setup(void)
{
	ds_mgmt_pool_get_prop_return = 0;
	ds_mgmt_pool_get_prop_in = NULL;
	ds_mgmt_pool_get_prop_out = NULL;
}

void
mock_ds_mgmt_pool_get_prop_teardown(void)
{
	daos_prop_free(ds_mgmt_pool_get_prop_in);
	daos_prop_free(ds_mgmt_pool_get_prop_out);
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
d_rank_list_t		*ds_mgmt_pool_query_ranks_out;

int
ds_mgmt_pool_query(uuid_t pool_uuid, d_rank_list_t *svc_ranks, d_rank_list_t **ranks,
		   daos_pool_info_t *pool_info, uint32_t *pool_layout_ver,
		   uint32_t *upgrade_layout_ver)
{
	/* If function is to return with an error, pool_info and ranks will not be filled. */
	if (ds_mgmt_pool_query_return != 0)
		return ds_mgmt_pool_query_return;

	uuid_copy(ds_mgmt_pool_query_uuid, pool_uuid);
	ds_mgmt_pool_query_info_ptr = (void *)pool_info;
	if (pool_info != NULL) {
		ds_mgmt_pool_query_info_in = *pool_info;
		*pool_info = ds_mgmt_pool_query_info_out;
	}
	if (ranks != NULL) {
		*ranks = d_rank_list_alloc(8);		/* 0-7 ; caller must free this */
		ds_mgmt_pool_query_ranks_out = *ranks;
	}
	return ds_mgmt_pool_query_return;	/* 0 */
}

void
mock_ds_mgmt_pool_query_setup(void)
{
	ds_mgmt_pool_query_return = 0;
	uuid_clear(ds_mgmt_pool_query_uuid);
	ds_mgmt_pool_query_info_ptr = NULL;
	memset(&ds_mgmt_pool_query_info_out, 0, sizeof(daos_pool_info_t));
	ds_mgmt_pool_query_ranks_out = NULL;
}

int			ds_mgmt_pool_query_targets_return;
uuid_t			ds_mgmt_pool_query_targets_uuid;
daos_target_info_t	*ds_mgmt_pool_query_targets_info_out;

int
ds_mgmt_pool_query_targets(uuid_t pool_uuid, d_rank_list_t *svc_ranks, d_rank_t rank,
			   d_rank_list_t *tgts, daos_target_info_t **infos)
{
	/* If function is to return with an error, infos will not be filled. */
	if (ds_mgmt_pool_query_targets_return != 0)
		return ds_mgmt_pool_query_targets_return;

	uuid_copy(ds_mgmt_pool_query_targets_uuid, pool_uuid);
	if (infos != NULL && ds_mgmt_pool_query_targets_info_out != NULL) {
		D_ALLOC_ARRAY(*infos, tgts->rl_nr);
		memcpy(*infos, ds_mgmt_pool_query_targets_info_out,
		       tgts->rl_nr * sizeof(daos_target_info_t));
	}

	return ds_mgmt_pool_query_targets_return;	/* 0 */
}

void
mock_ds_mgmt_pool_query_targets_gen_infos(uint32_t n_infos)
{
	uint32_t		 i;
	daos_target_info_t	*infos;

	D_ALLOC_ARRAY(infos, n_infos);
	for (i = 0; i < n_infos; i++) {
		infos[i].ta_type = DAOS_TP_UNKNOWN;
		infos[i].ta_state = (i == 0) ? DAOS_TS_DOWN_OUT : DAOS_TS_UP_IN;
		infos[i].ta_space.s_total[DAOS_MEDIA_SCM] = 1000000000;
		infos[i].ta_space.s_free[DAOS_MEDIA_SCM] = 800000000 + i;
		infos[i].ta_space.s_total[DAOS_MEDIA_NVME] = 9000000000;
		infos[i].ta_space.s_free[DAOS_MEDIA_NVME] = 600000000 + i;
	}
	ds_mgmt_pool_query_targets_info_out = infos;
}

void
mock_ds_mgmt_pool_query_targets_setup(void)
{
	ds_mgmt_pool_query_targets_return = 0;
	uuid_clear(ds_mgmt_pool_query_targets_uuid);
	ds_mgmt_pool_query_targets_info_out = NULL;
}

void
mock_ds_mgmt_pool_query_targets_teardown(void)
{
	if (ds_mgmt_pool_query_targets_info_out != NULL) {
		D_FREE(ds_mgmt_pool_query_targets_info_out);
		ds_mgmt_pool_query_targets_info_out = NULL;
	}
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
				 struct pool_target_addr_list *target_addrs,
				 pool_comp_state_t state, size_t scm_size, size_t nvme_size)
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
		    char *tgt_dev,  size_t scm_size, size_t nvme_size,
		    size_t domains_nr, uint32_t *domains)
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
ds_mgmt_evict_pool(uuid_t pool_uuid, d_rank_list_t *svc_ranks, uuid_t *handles, size_t n_handles,
		   uint32_t destroy, uint32_t force_destroy, char *machine, uint32_t *count)
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
crt_rank_self_set(d_rank_t rank, uint32_t group_version_min)
{
	return 0;
}

void
dss_init_state_set(enum dss_init_state state)
{
}

int
dss_module_setup_all()
{
	return 0;
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
		    size_t nvme_size, daos_prop_t *prop,
		    d_rank_list_t **svcp, int nr_domains,
		    uint32_t *domains)
{
	return 0;
}

int
ds_mgmt_destroy_pool(uuid_t pool_uuid, d_rank_list_t *svc_ranks)
{
	return 0;
}

int
ds_mgmt_bio_health_query(struct mgmt_bio_health *mbh, uuid_t uuid)
{
	return 0;
}

int
ds_mgmt_smd_list_devs(Ctl__SmdDevResp *resp)
{
	return 0;
}

int
ds_mgmt_smd_list_pools(Ctl__SmdPoolResp *resp)
{
	return 0;
}

int	ds_mgmt_pool_upgrade_return;
uuid_t  ds_mgmt_pool_upgrade_uuid;

int
ds_mgmt_pool_upgrade(uuid_t pool_uuid, d_rank_list_t *svc_ranks)
{
	uuid_copy(ds_mgmt_pool_upgrade_uuid, pool_uuid);
	return ds_mgmt_pool_upgrade_return;
}

void
mock_ds_mgmt_pool_upgrade_setup(void)
{
	ds_mgmt_pool_upgrade_return = 0;
	uuid_clear(ds_mgmt_pool_upgrade_uuid);
}

int	ds_mgmt_dev_manage_led_return;
uuid_t  ds_mgmt_dev_manage_led_uuid;

void
mock_ds_mgmt_dev_manage_led_setup(void)
{
	ds_mgmt_dev_manage_led_return = 0;
	uuid_clear(ds_mgmt_dev_manage_led_uuid);
}

int
ds_mgmt_dev_manage_led(Ctl__LedManageReq *req, Ctl__DevManageResp *resp)
{
	if (uuid_parse(req->ids, ds_mgmt_dev_manage_led_uuid) != 0)
		return -DER_INVAL;

	return ds_mgmt_dev_manage_led_return;
}

int	ds_mgmt_dev_replace_return;
uuid_t  ds_mgmt_dev_replace_old_uuid;
uuid_t  ds_mgmt_dev_replace_new_uuid;

int
ds_mgmt_dev_replace(uuid_t old_uuid, uuid_t new_uuid, Ctl__DevManageResp *resp)
{
	uuid_copy(ds_mgmt_dev_replace_old_uuid, old_uuid);
	uuid_copy(ds_mgmt_dev_replace_new_uuid, new_uuid);
	return ds_mgmt_dev_replace_return;
}

void
mock_ds_mgmt_dev_replace_setup(void)
{
	ds_mgmt_dev_replace_return = 0;
	uuid_clear(ds_mgmt_dev_replace_old_uuid);
	uuid_clear(ds_mgmt_dev_replace_new_uuid);
}

int	ds_mgmt_dev_set_faulty_return;
uuid_t  ds_mgmt_dev_set_faulty_uuid;

int
ds_mgmt_dev_set_faulty(uuid_t uuid, Ctl__DevManageResp *resp)
{
	uuid_copy(ds_mgmt_dev_set_faulty_uuid, uuid);
	return ds_mgmt_dev_set_faulty_return;
}

void
mock_ds_mgmt_dev_set_faulty_setup(void)
{
	ds_mgmt_dev_set_faulty_return = 0;
	uuid_clear(ds_mgmt_dev_set_faulty_uuid);
}
