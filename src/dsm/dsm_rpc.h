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

	DSM_CONT_CREATE		= 10,
	DSM_CONT_DESTROY	= 11,
	DSM_CONT_OPEN		= 12,
	DSM_CONT_CLOSE		= 13,
	DSM_CONT_QUERY		= 14,

	DSM_CONT_ATTR_LIST	= 20,
	DSM_CONT_ATTR_SET	= 21,
	DSM_CONT_ATTR_GET	= 22,

	DSM_CONT_EPOCH_QUERY	= 30,
	DSM_CONT_EPOCH_HOLD	= 31,
	DSM_CONT_EPOCH_SLIP	= 32,
	DSM_CONT_EPOCH_FLUSH	= 33,
	DSM_CONT_EPOCH_DISCARD	= 34,
	DSM_CONT_EPOCH_COMMIT	= 35,
	DSM_CONT_EPOCH_WAIT	= 36,

	DSM_CONT_SNAP_LIST	= 40,
	DSM_CONT_SNAP_CREATE	= 41,
	DSM_CONT_SNAP_DESTROY	= 42,

	DSM_TGT_POOL_CONNECT	= 50,
	DSM_TGT_POOL_DISCONNECT	= 51,

	DSM_TGT_CONT_OPEN	= 55,
	DSM_TGT_CONT_CLOSE	= 56,

	DSM_TGT_EPOCH_FLUSH	= 60,
	DSM_TGT_EPOCH_DISCARD	= 61,

	DSM_TGT_OBJ_UPDATE	= 70,
	DSM_TGT_OBJ_FETCH	= 71,
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

struct object_update_in {
	daos_unit_oid_t oui_oid;
	uuid_t		oui_co_uuid;
	uuid_t		oui_pool_uuid;
	uint64_t	oui_epoch;
	uint32_t	oui_nr;
	uint32_t	oui_pad;
	daos_dkey_t	oui_dkey;
	struct dtp_array oui_iods;
	struct dtp_array oui_bulks;
};

/*
 * pool_connect_in::pci_capas
 *
 * POOL_CAPA_RO, POOL_CAPA_RW, and POOL_CAPA_EX are mutually exclusive.
 */
#define POOL_CAPA_RO	(1ULL << 0)	/* read-only */
#define POOL_CAPA_RW	(1ULL << 1)	/* read-write */
#define POOL_CAPA_EX	(1ULL << 2)	/* exclusive read-write */

struct cont_create_in {
	uuid_t	cci_pool;
	uuid_t	cci_pool_hdl;
	uuid_t	cci_cont;
};

struct cont_create_out {
	int32_t	cco_ret;
};

struct cont_destroy_in {
	uuid_t		cdi_pool;
	uuid_t		cdi_pool_hdl;
	uuid_t		cdi_cont;
	uint32_t	cdi_force;
};

struct cont_destroy_out {
	int32_t	cdo_ret;
};

struct cont_open_in {
	uuid_t		coi_pool;
	uuid_t		coi_pool_hdl;
	uuid_t		coi_cont;
	uuid_t		coi_cont_hdl;
	uint64_t	coi_capas;
};

struct cont_open_out {
	int32_t			coo_ret;
	daos_epoch_state_t	coo_epoch_state;
};

struct cont_close_in {
	uuid_t	cci_pool;
	uuid_t	cci_cont;
	uuid_t	cci_cont_hdl;
};

struct cont_close_out {
	int32_t	cco_ret;
};

int
dsm_req_create(dtp_context_t dtp_ctx, dtp_endpoint_t tgt_ep,
	       dtp_opcode_t opc, dtp_rpc_t **req);

extern struct daos_rpc dsm_rpcs[];

#endif /* __DSM_RPC_H__ */
