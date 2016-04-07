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
 * This file is part of daos_transport. It implements the main group APIs.
 */

#include <dtp_internal.h>

/* TODO - currently only with one global service group and one client group */
int
dtp_group_rank(dtp_group_id_t grp_id, daos_rank_t *rank)
{
	if (rank == NULL) {
		D_ERROR("invalid parameter of NULL rank pointer.\n");
		return -DER_INVAL;
	}

	*rank = (dtp_gdata.dg_server == true) ? dtp_gdata.dg_mcl_srv_set->self :
						dtp_gdata.dg_mcl_cli_set->self;

	return 0;
}

int
dtp_group_size(dtp_group_id_t grp_id, uint32_t *size)
{
	if (size == NULL) {
		D_ERROR("invalid parameter of NULL size pointer.\n");
		return -DER_INVAL;
	}

	*size = (dtp_gdata.dg_server == true) ? dtp_gdata.dg_mcl_srv_set->size :
						dtp_gdata.dg_mcl_cli_set->size;

	return 0;
}

dtp_group_id_t *
dtp_global_grp_id(void)
{
	return (dtp_gdata.dg_server == true) ? &dtp_gdata.dg_srv_grp_id :
					       &dtp_gdata.dg_cli_grp_id;
}
