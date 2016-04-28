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
 * DMG RPC Protocol Serialization Functions
 */
#include <daos/rpc.h>
#include "dmg_rpc.h"

static int
dmg_proc_pool_create_in(dtp_proc_t proc, void *data)
{
	struct dmg_pool_create_in	*pc_in;
	int				rc = 0;

	pc_in = (struct dmg_pool_create_in *)data;
	D_ASSERT(pc_in != NULL);

	rc = dtp_proc_uuid_t(proc, &pc_in->pc_uuid);
	if (rc != 0)
		goto out;

	rc = dtp_proc_uint32_t(proc, &pc_in->pc_mode);
	if (rc != 0)
		goto out;

	rc = dtp_proc_dtp_group_id_t(proc, &pc_in->pc_grp_id);
	if (rc != 0)
		goto out;

	rc = dtp_proc_daos_rank_list_t(proc, &pc_in->pc_tgts);
	if (rc != 0)
		goto out;

	rc = dtp_proc_dtp_const_string_t(proc, &pc_in->pc_tgt_dev);
	if (rc != 0)
		goto out;

	rc = dtp_proc_daos_size_t(proc, &pc_in->pc_tgt_size);
	if (rc != 0)
		goto out;

	rc = dtp_proc_daos_rank_list_t(proc, &pc_in->pc_svc);

out:
	if (rc != 0)
		D_ERROR("dmg_proc_pool_create_in failed rc: %d.\n", rc);
	return rc;
}

static int
dmg_proc_pool_create_out(dtp_proc_t proc, void *data)
{
	struct dmg_pool_create_out	*pc_out;
	int				rc = 0;

	pc_out = (struct dmg_pool_create_out *)data;
	D_ASSERT(pc_out != NULL);

	rc = dtp_proc_int(proc, &pc_out->pc_rc);
	if (rc != 0)
		goto out;

	rc = dtp_proc_daos_rank_list_t(proc, &pc_out->pc_svc);

out:
	if (rc != 0)
		D_ERROR("dmg_proc_pool_create_out failed rc: %d.\n", rc);
	return rc;
}



struct daos_rpc dmg_rpcs[] = {
	{
		.dr_name	= "DMG_POOL_CREATE",
		.dr_opc		= DMG_POOL_CREATE,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_in_hdlr	= dmg_proc_pool_create_in,
		.dr_in_sz	= sizeof(struct dmg_pool_create_in),
		.dr_out_hdlr	= dmg_proc_pool_create_out,
		.dr_out_sz	= sizeof(struct dmg_pool_create_out),
	}, {
		.dr_opc		= 0
	}
};

