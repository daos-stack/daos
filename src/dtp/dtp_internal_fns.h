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
 * This file is part of daos_transport. It gives out the main dtp internal
 * function declarations.
 */

#ifndef __DTP_INTERNAL_FNS_H__
#define __DTP_INTERNAL_FNS_H__

#include <dtp_internal_types.h>

/** dtp_init.c */
bool dtp_initialized();

/** dtp_group.c */
dtp_group_id_t *dtp_global_grp_id(void);

/** dtp_register.c */
int dtp_opc_map_create(unsigned int bits, struct dtp_opc_map **opc_map);
void dtp_opc_map_destroy(struct dtp_opc_map *opc_map);
struct dtp_opc_info *dtp_opc_lookup(struct dtp_opc_map *map, dtp_opcode_t opc,
				    int locked);
/** dtp_context.c */
bool dtp_context_empty(int locked);

/** dtp_rpc.c */
void dtp_rpc_priv_init(struct dtp_rpc_priv *rpc_priv, dtp_context_t dtp_ctx,
		       dtp_opcode_t opc, int srv_flag);
void dtp_rpc_inout_buff_fini(dtp_rpc_t *rpc_pub);
int dtp_rpc_inout_buff_init(dtp_rpc_t *rpc_pub);

/** some simple helper functions */
static inline void
dtp_common_hdr_init(struct dtp_common_hdr *hdr, dtp_opcode_t opc)
{
	D_ASSERT(hdr != NULL);
	hdr->dch_opc = opc;
	hdr->dch_magic = DTP_RPC_MAGIC;
	hdr->dch_version = DTP_RPC_VERSION;
	uuid_copy(hdr->dch_grp_id, *dtp_global_grp_id());
	D_ASSERT(dtp_group_rank(0, &hdr->dch_rank) == 0);
}

static inline void
dtp_bulk_desc_dup(struct dtp_bulk_desc *bulk_desc_new,
		  struct dtp_bulk_desc *bulk_desc)
{
	D_ASSERT(bulk_desc_new != NULL && bulk_desc != NULL);
	*bulk_desc_new = *bulk_desc;
}

#endif /* __DTP_INTERNAL_FNS_H__ */
