/*
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * ds_mgmt: System Metadata (Management Service) Storage Layout
 *
 *   Root KVS (GENERIC):
 *     Server KVS (INTEGER)
 *     UUID KVS (GENERIC)
 *     Pool KVS (GENERIC)
 */

#ifndef __MGMT_SRV_LAYOUT_H__
#define __MGMT_SRV_LAYOUT_H__

#include <daos_types.h>
#include "srv_internal.h"

/* Root KVS (RDB_KVS_GENERIC) */
extern d_iov_t ds_mgmt_prop_servers;		/* server KVS */
extern d_iov_t ds_mgmt_prop_uuids;		/* UUID KVS */
extern d_iov_t ds_mgmt_prop_pools;		/* pool KVS */
extern d_iov_t ds_mgmt_prop_map_version;	/* uint32_t */
extern d_iov_t ds_mgmt_prop_rank_next;		/* uint32_t */

/*
 * Server KVS (RDB_KVS_INTEGER)
 *
 * Each key is the server's rank (uint64_t, casted from d_rank_t). Each value
 * is of the type server_rec.
 */

/* server_rec.sr_flags */
#define SERVER_IN	(1U << 0)

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

/*
 * Pool KVS (RDB_KVS_GENERIC)
 *
 * Each key is a pool UUID (uuid_t). Each value is of the type pool_rec.
 */

/* pool_rec.pr_state */
enum pool_state {
	POOL_CREATING,
	POOL_READY,
	POOL_DESTROYING
};

struct pool_rec {
	uint8_t		pr_nreplicas;	/* number of pool service replicas */
	uint8_t		pr_state;
	uint16_t	pr_padding;
	uint32_t	pr_replicas[];	/* pool service replica ranks */
};

#endif /* __MGMT_SRV_LAYOUT_H__ */
