/**
 * (C) Copyright 2017 Intel Corporation.
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
 * ds_pool: Pool Server Storage Layout Definitions
 */
#define D_LOGFAC	DD_FAC(pool)

#include <daos_srv/rdb.h>
#include "srv_layout.h"

/* Root KVS */
RDB_STRING_KEY(ds_pool_attr_, uid);
RDB_STRING_KEY(ds_pool_attr_, gid);
RDB_STRING_KEY(ds_pool_attr_, mode);
RDB_STRING_KEY(ds_pool_attr_, map_version);
RDB_STRING_KEY(ds_pool_attr_, map_buffer);
RDB_STRING_KEY(ds_pool_attr_, map_uuids);
RDB_STRING_KEY(ds_pool_attr_, nhandles);
RDB_STRING_KEY(ds_pool_attr_, handles);
RDB_STRING_KEY(ds_pool_attr_, user);
