/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
