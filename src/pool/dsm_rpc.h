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

	DSM_POOL_QUERY		= 3,
	DSM_POOL_EXCLUDE	= 4,

	DSM_TGT_POOL_CONNECT	= 20,
	DSM_TGT_POOL_DISCONNECT	= 21,
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
	int32_t		pco_ret;
	uint32_t	pco_mode;
	uint32_t	pco_pool_map_version;
	uint32_t	pco_pool_map_buf_size;	/* only set on -DER_TRUNC */
};

struct pool_disconnect_in {
	uuid_t	pdi_pool;
	uuid_t	pdi_pool_hdl;
};

struct pool_disconnect_out {
	int32_t pdo_ret;
};

struct tgt_pool_connect_in {
	uuid_t		tpci_pool;
	uuid_t		tpci_pool_hdl;
	uint64_t	tpci_capas;
	uint32_t	tpci_pool_map_version;
};

struct tgt_pool_connect_out {
	int32_t	tpco_ret;	/* number of errors */
};

struct tgt_pool_disconnect_in {
	uuid_t		tpdi_pool;
	uuid_t		tpdi_pool_hdl;
};

struct tgt_pool_disconnect_out {
	int32_t	tpdo_ret;	/* number of errors */
};

int
pool_req_create(dtp_context_t dtp_ctx, dtp_endpoint_t tgt_ep,
		dtp_opcode_t opc, dtp_rpc_t **req);

extern struct daos_rpc pool_rpcs[];
extern struct daos_rpc pool_srv_rpcs[];

static inline void
dsm_set_reply_status(dtp_rpc_t *rpc, int status)
{
	int *ret;

	/* FIXME; The right way to do it might be find the
	 * status offset and set it, but let's put status
	 * in front of the bulk reply for now
	 **/
	D_ASSERT(rpc != NULL);
	ret = dtp_reply_get(rpc);
	D_ASSERT(ret != NULL);
	*ret = status;
}

static inline int
dsm_get_reply_status(dtp_rpc_t *rpc)
{
	int *ret;
	/* FIXME; The right way to do it might be find the
	 * status offset and set it, but let's put status
	 * in front of the bulk reply for now
	 **/
	ret = dtp_reply_get(rpc);
	return *ret;
}

#endif /* __DSM_RPC_H__ */
