/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/*
 * dsm: RPC Protocol Definitions
 *
 * This is naturally shared by both dsmc and dsms. The in and out data
 * structures may safely contain compiler-generated paddings, which will be
 * removed dtp's serialization process.
 *
 * Every pool operation shall pass in the UUID of the pool it intends to access
 * and the UUID of its pool handle. The pool UUID enables server to quickly
 * locate the right mpool.
 *
 * Every container operation shall pass in the UUID of the container and the
 * UUID of its container handle.
 */

#ifndef __DSM_RPC_H__
#define __DSM_RPC_H__

#include <uuid/uuid.h>
#include <daos/daos_transport.h>
#include <daos/daos_rpc.h>

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * dtp_req_create(..., opc, ...). See daos_rpc.h.
 */
#define POOL_CONNECT	1
#define POOL_DISCONNECT	2

struct pool_map {
	uint64_t	version;
	uint32_t	ndomains;
	uint32_t	ntargets;
};

static inline int
proc_pool_map(dtp_proc_t proc, void *data)
{
	struct pool_map	       *p = data;
	int			rc;

	rc = dtp_proc_uint64_t(proc, &p->version);
	if (rc != 0)
		return rc;

	rc = dtp_proc_uint32_t(proc, &p->ndomains);
	if (rc != 0)
		return rc;

	rc = dtp_proc_uint32_t(proc, &p->ntargets);
	if (rc != 0)
		return rc;

	return 0;
}

/* TODO: Capability bits. */
/* TODO: Think about where uid and gid really belong. */
struct pool_connect_in {
	uuid_t		pool;
	uuid_t		pool_hdl;
	uint32_t	uid;
	uint32_t	gid;
	uint64_t	pool_capas;
	dtp_bulk_t	pool_map_bulk;
};

struct pool_connect_out {
	int32_t		rc;
	struct pool_map	pool_map;
};

/*
 * "pool" helps the server side to quickly locate the file that should store
 * "pool_hdl".
 */
struct pool_disconnect_in {
	uuid_t	pool;
	uuid_t	pool_hdl;
};

struct pool_disconnect_out {
	int32_t	rc;
};

extern struct daos_rpc dsm_client_rpcs[];
#endif /* __DSM_RPC_H__ */
