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
 * dmgs: Pool Methods
 */

#include "dmgs_internal.h"

int
dmgs_hdlr_pool_create(dtp_rpc_t *rpc_req)
{
	struct dmg_pool_create_in	*pc_in;
	struct dmg_pool_create_out	*pc_out;
	int				rc = 0;

	pc_in = rpc_req->dr_input;
	pc_out = rpc_req->dr_output;
	D_ASSERT(pc_in != NULL && pc_out != NULL);

	return rc;
}


