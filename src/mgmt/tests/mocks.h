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

#ifndef __MGMT_TESTS_MOCKS_H__
#define __MGMT_TESTS_MOCKS_H__

#include <gurt/types.h>
#include <daos_security.h>

/*
 * Mock ds_mgmt_pool_get_acl
 */
extern int		ds_mgmt_pool_get_acl_return;
extern struct daos_acl	*ds_mgmt_pool_get_acl_return_acl;
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
extern struct daos_acl	*ds_mgmt_pool_overwrite_acl_result;
extern void		*ds_mgmt_pool_overwrite_acl_result_ptr;

void mock_ds_mgmt_pool_overwrite_acl_setup(void);
void mock_ds_mgmt_pool_overwrite_acl_teardown(void);

#endif /* __MGMT_TESTS_MOCKS_H__ */
