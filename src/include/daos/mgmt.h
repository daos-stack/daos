/**
 * (C) Copyright 2016 Intel Corporation.
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
 * dc_mgmt: Management Client API
 */

#ifndef __DC_MGMT_H__
#define __DC_MGMT_H__

#include <daos/common.h>
#include <daos/tse.h>
#include <daos_types.h>

int dc_mgmt_init(void);

void dc_mgmt_fini(void);

int dc_mgmt_svc_rip(tse_task_t *task);
int dc_pool_create(tse_task_t *task);
int dc_pool_destroy(tse_task_t *task);
int dc_pool_evict(tse_task_t *task);
int dc_pool_extend(tse_task_t *task);
int dc_mgmt_set_params(tse_task_t *task);
int dc_mgmt_profile(uint64_t modules, char *path, bool start);

#endif
