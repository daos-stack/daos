/*
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * Mocks for DAOS mgmt unit tests
 */

#ifndef __MGMT_TESTS_MOCKS_H__
#define __MGMT_TESTS_MOCKS_H__

#include <gurt/types.h>
#include <daos_types.h>
#include <daos_security.h>
#include "../rpc.h"

/*
 * Mock ds_mgmt_pool_get_acl
 */
extern int		ds_mgmt_pool_get_acl_return;
extern daos_prop_t	*ds_mgmt_pool_get_acl_return_acl;
extern uuid_t		ds_mgmt_pool_get_acl_uuid;
extern void		*ds_mgmt_pool_get_acl_acl_ptr;

void mock_ds_mgmt_pool_get_acl_setup(void);
void mock_ds_mgmt_pool_get_acl_teardown(void);

/*
 * Mock ds_mgmt_pool_overwrite_acl
 */
extern int		ds_mgmt_pool_overwrite_acl_return;
extern uuid_t		ds_mgmt_pool_overwrite_acl_uuid;
extern struct daos_acl	*ds_mgmt_pool_overwrite_acl_acl;
extern daos_prop_t	*ds_mgmt_pool_overwrite_acl_result;
extern void		*ds_mgmt_pool_overwrite_acl_result_ptr;

void mock_ds_mgmt_pool_overwrite_acl_setup(void);
void mock_ds_mgmt_pool_overwrite_acl_teardown(void);

/*
 * Mock ds_mgmt_pool_update_acl
 */
extern int		ds_mgmt_pool_update_acl_return;
extern uuid_t		ds_mgmt_pool_update_acl_uuid;
extern struct daos_acl	*ds_mgmt_pool_update_acl_acl;
extern daos_prop_t	*ds_mgmt_pool_update_acl_result;
extern void		*ds_mgmt_pool_update_acl_result_ptr;

void mock_ds_mgmt_pool_update_acl_setup(void);
void mock_ds_mgmt_pool_update_acl_teardown(void);

/*
 * Mock ds_mgmt_pool_delete_acl
 */
extern int		ds_mgmt_pool_delete_acl_return;
extern uuid_t		ds_mgmt_pool_delete_acl_uuid;
extern const char	*ds_mgmt_pool_delete_acl_principal;
extern daos_prop_t	*ds_mgmt_pool_delete_acl_result;
extern void		*ds_mgmt_pool_delete_acl_result_ptr;

void mock_ds_mgmt_pool_delete_acl_setup(void);
void mock_ds_mgmt_pool_delete_acl_teardown(void);

/*
 * Mock ds_mgmt_pool_list_cont
 */
extern int				 ds_mgmt_pool_list_cont_return;
extern struct daos_pool_cont_info	*ds_mgmt_pool_list_cont_out;
extern uint64_t				 ds_mgmt_pool_list_cont_nc_out;

void mock_ds_mgmt_list_cont_gen_cont(size_t ncont);
void mock_ds_mgmt_pool_list_cont_setup(void);
void mock_ds_mgmt_pool_list_cont_teardown(void);

/*
 * Mock ds_mgmt_pool_set_prop
 */
extern int		ds_mgmt_pool_set_prop_return;
extern daos_prop_t	*ds_mgmt_pool_set_prop_result;

void mock_ds_mgmt_pool_set_prop_setup(void);
void mock_ds_mgmt_pool_set_prop_teardown(void);

/*
 * Mock ds_mgmt_pool_get_prop
 */
extern int		ds_mgmt_pool_get_prop_return;
extern daos_prop_t	*ds_mgmt_pool_get_prop_in;
extern daos_prop_t	*ds_mgmt_pool_get_prop_out;

void mock_ds_mgmt_pool_get_prop_setup(void);
void mock_ds_mgmt_pool_get_prop_teardown(void);

/*
 * Mock ds_mgmt_pool_extend
 */
extern int		ds_mgmt_pool_extend_return;
extern uuid_t		ds_mgmt_pool_extend_uuid;
void mock_ds_mgmt_pool_extend_setup(void);

/*
 * Mock ds_mgmt_pool_query
 */
extern int		ds_mgmt_pool_query_return;
extern uuid_t		ds_mgmt_pool_query_uuid;
extern daos_pool_info_t	ds_mgmt_pool_query_info_out;
extern daos_pool_info_t	ds_mgmt_pool_query_info_in;
extern void		*ds_mgmt_pool_query_info_ptr;
extern d_rank_list_t	*ds_mgmt_pool_query_ranks_out;
void mock_ds_mgmt_pool_query_setup(void);

/*
 * Mock ds_mgmt_tgt_state_update
 */
extern int		ds_mgmt_target_update_return;
extern uuid_t		ds_mgmt_target_update_uuid;
void mock_ds_mgmt_tgt_update_setup(void);

/*
 * Mock ds_mgmt_evict
 */
extern int		ds_mgmt_pool_evict_return;
extern uuid_t		ds_mgmt_pool_evict_uuid;
void mock_ds_mgmt_pool_evict_setup(void);

/*
 * Mock ds_mgmt_cont_set_owner
 */
extern int	ds_mgmt_cont_set_owner_return;
extern uuid_t	ds_mgmt_cont_set_owner_pool;
extern uuid_t	ds_mgmt_cont_set_owner_cont;
extern char	*ds_mgmt_cont_set_owner_user;
extern char	*ds_mgmt_cont_set_owner_group;
void mock_ds_mgmt_cont_set_owner_setup(void);
void mock_ds_mgmt_cont_set_owner_teardown(void);

/*
 * Mock ds_mgmt_upgrade
 */
extern int		ds_mgmt_pool_upgrade_return;
extern uuid_t		ds_mgmt_pool_upgrade_uuid;
void mock_ds_mgmt_pool_upgrade_setup(void);


#endif /* __MGMT_TESTS_MOCKS_H__ */
