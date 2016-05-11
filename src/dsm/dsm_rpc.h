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

#include <stdint.h>
#include <uuid/uuid.h>
#include <daos/event.h>
#include <daos/rpc.h>
#include <daos/transport.h>
#include <daos_event.h>

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * dtp_req_create(..., opc, ...). See daos_rpc.h.
 */
enum dsm_operation {
	DSM_POOL_CONNECT	= 1,
	DSM_POOL_DISCONNECT	= 2,
	DSM_PING		= 3,
};

/* DSM RPC request structure */
struct pool_map {
	uint64_t	pm_version;
	uint32_t	pm_ndomains;
	uint32_t	pm_ntargets;
};

struct pool_connect_in {
	uuid_t		pci_pool;
	uuid_t		pci_pool_hdl;
	uint32_t	pci_uid;
	uint32_t	pci_gid;
	uint64_t	pci_capas;
	dtp_bulk_t	pci_pool_map_bulk;
};

struct pool_connect_out {
	int32_t pco_ret;
	struct pool_map pco_map;
};

struct pool_disconnect_in {
	uuid_t	pdi_pool;
	uuid_t	pdi_pool_hdl;
};

struct pool_disconnect_out {
	int32_t pdo_ret;
};

/*
 * pool_connect_in::pci_capas
 *
 * POOL_CAPA_RO, POOL_CAPA_RW, and POOL_CAPA_EX are mutually exclusive.
 */
#define POOL_CAPA_RO	(1ULL << 0)	/* read-only */
#define POOL_CAPA_RW	(1ULL << 1)	/* read-write */
#define POOL_CAPA_EX	(1ULL << 2)	/* exclusive read-write */

static inline int
proc_pool_map(dtp_proc_t proc, void *data)
{
	struct pool_map        *p = data;
	int                     rc;

	rc = dtp_proc_uint64_t(proc, &p->pm_version);
	if (rc != 0)
		return rc;

	rc = dtp_proc_uint32_t(proc, &p->pm_ndomains);
	if (rc != 0)
		return rc;

	rc = dtp_proc_uint32_t(proc, &p->pm_ntargets);
	if (rc != 0)
		return rc;

	return 0;
}

int
dsm_client_async_rpc(dtp_rpc_t *rpc_req, struct daos_event *event);
int
dsm_req_create(dtp_context_t dtp_ctx, dtp_endpoint_t tgt_ep,
	       dtp_opcode_t opc, dtp_rpc_t **req);

int
dsms_hdlr_pool_connect(dtp_rpc_t *rpc);

int
dsms_hdlr_pool_disconnect(dtp_rpc_t *rpc);

int
dsms_hdlr_ping(dtp_rpc_t *rpc);

extern struct daos_rpc dsm_rpcs[];

#endif /* __DSM_RPC_H__ */
