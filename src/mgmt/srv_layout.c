/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * ds_mgmt: System Metadata (Management Service) Storage Layout Definitions
 */

#define D_LOGFAC DD_FAC(mgmt)

#include <daos_srv/rdb.h>
#include "srv_layout.h"

/* Root KVS */
RDB_STRING_KEY(ds_mgmt_prop_, servers);
RDB_STRING_KEY(ds_mgmt_prop_, uuids);
RDB_STRING_KEY(ds_mgmt_prop_, pools);
RDB_STRING_KEY(ds_mgmt_prop_, map_version);
RDB_STRING_KEY(ds_mgmt_prop_, rank_next);
