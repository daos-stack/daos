/**
 * (C) Copyright 2017-2021 Intel Corporation.
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
 * rdb: Storage Layout Definitions
 */

#define D_LOGFAC	DD_FAC(rdb)

#include <daos_srv/rdb.h>

#include "rdb_layout.h"

RDB_STRING_KEY(rdb_, dkey);

RDB_STRING_KEY(rdb_mc_, uuid);
RDB_STRING_KEY(rdb_mc_, version);
RDB_STRING_KEY(rdb_mc_, term);
RDB_STRING_KEY(rdb_mc_, vote);
RDB_STRING_KEY(rdb_mc_, lc);
RDB_STRING_KEY(rdb_mc_, slc);

RDB_STRING_KEY(rdb_lc_, oid_next);
RDB_STRING_KEY(rdb_lc_, entry_header);
RDB_STRING_KEY(rdb_lc_, entry_data);
RDB_STRING_KEY(rdb_lc_, nreplicas);
RDB_STRING_KEY(rdb_lc_, replicas);
RDB_STRING_KEY(rdb_lc_, root);
