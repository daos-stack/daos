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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/*
 * ds_mgmt: System Metadata (Management Service) Storage Layout
 *
 *   Root KVS (GENERIC):
 *     Server KVS (INTEGER)
 *     UUID KVS (GENERIC)
 */

#ifndef __MGMT_SRV_LAYOUT_H__
#define __MGMT_SRV_LAYOUT_H__

#include <daos_types.h>

/* Root KVS (RDB_KVS_GENERIC) */
extern daos_iov_t ds_mgmt_prop_servers;		/* server KVS */
extern daos_iov_t ds_mgmt_prop_uuids;		/* UUID KVS */
extern daos_iov_t ds_mgmt_prop_map_version;	/* uint32_t */
extern daos_iov_t ds_mgmt_prop_rank_next;	/* uint32_t */

/*
 * Server KVS (RDB_KVS_INTEGER)
 *
 * Each key is the server's rank (uint64_t, casted from d_rank_t). Each value
 * is of the type server_rec.
 */

/* server_rec.sr_flags */
#define SERVER_IN	(1U << 0)

/* Length of server_rec.sr_addr and server_rec.sr_uri */
#define ADDR_STR_MAX_LEN 128

struct server_rec {
	uint16_t	sr_flags;
	uint16_t	sr_nctxs;
	uint32_t	sr_padding;
	uuid_t		sr_uuid;
	char		sr_addr[ADDR_STR_MAX_LEN];
	char		sr_uri[ADDR_STR_MAX_LEN];
};

/*
 * UUID KVS (RDB_KVS_GENERIC)
 *
 * Each key is a server UUID (uuid_t). Each value is the server's rank
 * (uint32_t).
 */

#endif /* __MGMT_SRV_LAYOUT_H__ */
