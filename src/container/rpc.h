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
 * dc_cont, ds_cont: RPC Protocol Definitions
 *
 * This is naturally shared by both dc_cont and ds_cont. The in and out data
 * structures must be absent of any compiler-generated paddings.
 */

#ifndef __CONTAINER_RPC_H__
#define __CONTAINER_RPC_H__

#include <stdint.h>
#include <uuid/uuid.h>
#include <daos/event.h>
#include <daos/rpc.h>

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See daos_rpc.h.
 */
enum dsm_operation {
	DSM_CONT_CREATE		= 1,
	DSM_CONT_DESTROY	= 2,
	DSM_CONT_OPEN		= 3,
	DSM_CONT_CLOSE		= 4,
	DSM_CONT_QUERY		= 5,

	DSM_CONT_ATTR_LIST	= 10,
	DSM_CONT_ATTR_SET	= 11,
	DSM_CONT_ATTR_GET	= 12,

	DSM_CONT_EPOCH_QUERY	= 20,
	DSM_CONT_EPOCH_HOLD	= 21,
	DSM_CONT_EPOCH_SLIP	= 22,
	DSM_CONT_EPOCH_FLUSH	= 23,
	DSM_CONT_EPOCH_DISCARD	= 24,
	DSM_CONT_EPOCH_COMMIT	= 25,
	DSM_CONT_EPOCH_WAIT	= 26,

	DSM_CONT_SNAP_LIST	= 30,
	DSM_CONT_SNAP_CREATE	= 31,
	DSM_CONT_SNAP_DESTROY	= 32,

	DSM_TGT_CONT_DESTROY	= 44,
	DSM_TGT_CONT_OPEN	= 45,
	DSM_TGT_CONT_CLOSE	= 46,

	DSM_TGT_EPOCH_FLUSH	= 50,
	DSM_TGT_EPOCH_DISCARD	= 51,
};

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
	uint32_t		coo_padding;
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

struct cont_op_in {
	uuid_t	cpi_pool;
	uuid_t	cpi_cont;
	uuid_t	cpi_cont_hdl;
};

struct cont_op_out {
	int32_t	cpo_ret;
};

struct epoch_op_in {
	struct cont_op_in	eoi_cont_op_in;
	daos_epoch_t		eoi_epoch;
};

struct epoch_op_out {
	struct cont_op_out	eoo_cont_op_out;
	uint32_t		eoo_padding;
	daos_epoch_state_t	eoo_epoch_state;
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

struct tgt_cont_destroy_in {
	uuid_t	tcdi_pool;
	uuid_t	tcdi_cont;
};

struct tgt_cont_destroy_out {
	int32_t tcdo_ret;	/* number of errors */
};

struct tgt_cont_open_in {
	uuid_t		tcoi_pool;
	uuid_t		tcoi_pool_hdl;
	uuid_t		tcoi_cont;
	uuid_t		tcoi_cont_hdl;
	uint64_t	tcoi_capas;
};

struct tgt_cont_open_out {
	int32_t	tcoo_ret;	/* number of errors */
};

struct tgt_cont_close_in {
	uuid_t	tcci_cont_hdl;
};

struct tgt_cont_close_out {
	int32_t	tcco_ret;	/* number of errors */
};

int
cont_req_create(crt_context_t crt_ctx, crt_endpoint_t tgt_ep,
	       crt_opcode_t opc, crt_rpc_t **req);

extern struct daos_rpc cont_rpcs[];
extern struct daos_rpc cont_srv_rpcs[];

static inline void
dsm_set_reply_status(crt_rpc_t *rpc, int status)
{
	int *ret;

	/* FIXME; The right way to do it might be find the
	 * status offset and set it, but let's put status
	 * in front of the bulk reply for now
	 **/
	D_ASSERT(rpc != NULL);
	ret = crt_reply_get(rpc);
	D_ASSERT(ret != NULL);
	*ret = status;
}

static inline int
dsm_get_reply_status(crt_rpc_t *rpc)
{
	int *ret;
	/* FIXME; The right way to do it might be find the
	 * status offset and set it, but let's put status
	 * in front of the bulk reply for now
	 **/
	ret = crt_reply_get(rpc);
	return *ret;
}

#endif /* __CONTAINER_RPC_H__ */
