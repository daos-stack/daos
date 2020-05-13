/*
 * (C) Copyright 2020 Intel Corporation.
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

#include <stdio.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <daos.h>
#include <gurt/common.h>
#include "libdaos_control.h"

int main(int argc, char *argv[])
{
	int			i, j, rc;
	daos_size_t		npools;
	daos_mgmt_pool_info_t	*pools = NULL;
	char			uuid_str[37];

	rc = daos_control_init(NULL);
	if (rc != 0) {
		D_ERROR("daos_control_init() rc %d\n", rc);
		exit(rc);
	}

	rc = daos_control_list_pools(NULL, &npools, NULL, NULL);
	if (rc != 0) {
		D_ERROR("daos_control_list_pools rc %d\n", rc);
		exit(rc);
	}

	printf("found %d daos pools\n", npools);

	D_ALLOC_ARRAY(pools, npools);
	if (!pools) {
		D_ERROR("failed to alloc pool array");
		exit(-DER_NOMEM);
	}

	rc = daos_control_list_pools(NULL, &npools, pools, NULL);
	if (rc != 0) {
		D_ERROR("daos_control_list_pools rc %d\n", rc);
		exit(rc);
	}

	for (i = 0; i < npools; i++) {
		uuid_unparse_lower(pools[i].mgpi_uuid, uuid_str);
		printf("pool uuid=%s: ", uuid_str);
		if (pools[i].mgpi_svc) {
			printf("ranks=");
			for (j = 0; j < pools[i].mgpi_svc->rl_nr; j++) {
				printf("%d", pools[i].mgpi_svc->rl_ranks[j]);
				if (j+1 < pools[i].mgpi_svc->rl_nr)
					printf(",");
			}
			d_rank_list_free(pools[i].mgpi_svc);
		}
		printf("\n");

	}

	exit(daos_control_fini());
}
