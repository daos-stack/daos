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
 * Mock ds_mgmt_list_pools
 */
extern int				ds_mgmt_list_pools_return;
extern char				ds_mgmt_list_pools_group[];
extern void				*ds_mgmt_list_pools_npools_ptr;
extern uint64_t				ds_mgmt_list_pools_npools;
extern void				*ds_mgmt_list_pools_poolsp_ptr;
extern struct mgmt_list_pools_one	*ds_mgmt_list_pools_poolsp_out;
extern void				*ds_mgmt_list_pools_len_ptr;
extern size_t				ds_mgmt_list_pools_len_out;

void mock_ds_mgmt_list_pools_setup(void);
void mock_ds_mgmt_list_pools_teardown(void);
void mock_ds_mgmt_list_pools_gen_pools(size_t num_pools);

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


#endif /* __MGMT_TESTS_MOCKS_H__ */
