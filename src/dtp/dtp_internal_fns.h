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
/**
 * This file is part of daos_transport. It gives out the main dtp internal
 * function declarations.
 */

#ifndef __DTP_INTERNAL_FNS_H__
#define __DTP_INTERNAL_FNS_H__

#include <dtp_internal_types.h>

bool dtp_initialized();

int dtp_opc_map_create(unsigned int bits, struct dtp_opc_map **opc_map);
void dtp_opc_map_destroy(struct dtp_opc_map *opc_map);
struct dtp_opc_info *dtp_opc_lookup(struct dtp_opc_map *map, dtp_opcode_t opc,
				    int locked);
bool dtp_context_empty(int locked);

static inline void
dtp_common_hdr_init(struct dtp_common_hdr *hdr, dtp_opcode_t opc)
{
	D_ASSERT(hdr != NULL);
	hdr->dch_opc = opc;
	hdr->dch_magic = DTP_RPC_MAGIC;
	hdr->dch_version = DTP_RPC_VERSION;
}

#endif /* __DTP_INTERNAL_FNS_H__ */
