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
