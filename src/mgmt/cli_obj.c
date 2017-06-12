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
/*
 * DAOS management client library. It exports the mgmt API defined in
 * daos_mgmt.h
 */
#define DD_SUBSYS	DD_FAC(mgmt)

#include <daos/mgmt.h>
#include <daos/event.h>
#include <daos/placement.h>
#include <daos/container.h>
#include <daos_task.h>
#include "rpc.h"

int
daos_obj_layout_free(struct daos_obj_layout *layout)
{
	int i;

	for (i = 0; i < layout->ol_nr; i++) {
		if (layout->ol_shards[i] != NULL) {
			struct daos_obj_shard *shard;

			shard = layout->ol_shards[i];
			D_FREE(shard, sizeof(*shard) + shard->os_replica_nr *
							sizeof(uint32_t));
		}
	}

	D_FREE(layout, sizeof(*layout) +
		       layout->ol_nr * sizeof(layout->ol_shards[0]));

	return 0;
}

int
daos_obj_layout_alloc(struct daos_obj_layout **layout, uint32_t grp_nr,
		      uint32_t grp_size)
{
	int rc = 0;
	int i;

	D_ALLOC(*layout, sizeof(struct daos_obj_layout) +
			 grp_nr * sizeof(struct daos_obj_shard *));
	if (*layout == NULL)
		return -DER_NOMEM;

	(*layout)->ol_nr = grp_nr;
	for (i = 0; i < grp_nr; i++) {
		D_ALLOC((*layout)->ol_shards[i], sizeof(struct daos_obj_shard) +
					     grp_size * sizeof(uint32_t));
		if ((*layout)->ol_shards[i] == NULL)
			D_GOTO(free, rc = -DER_NOMEM);

		(*layout)->ol_shards[i]->os_replica_nr = grp_size;
	}
free:
	if (rc != 0)
		daos_obj_layout_free(*layout);
	return rc;
}

int
daos_obj_layout_get(daos_handle_t coh, daos_obj_id_t oid,
		    struct daos_obj_layout **layout)
{
	daos_handle_t oh;
	struct pl_obj_layout *pl_layout;
	unsigned int grp_nr;
	unsigned int grp_size;
	int i;
	int j;
	int k;
	int rc;

	rc = daos_obj_open(coh, oid, 0, 0, &oh, NULL);
	if (rc)
		return rc;

	rc = dc_obj_layout_get(oh, &pl_layout, &grp_nr, &grp_size);
	if (rc)
		D_GOTO(out, rc);

	rc = daos_obj_layout_alloc(layout, grp_nr, grp_size);
	if (rc)
		D_GOTO(out, rc);

	D_ASSERT(grp_nr * grp_size == pl_layout->ol_nr);
	for (i = 0, k = 0; i < grp_nr; i++) {
		struct daos_obj_shard *shard;

		shard = (*layout)->ol_shards[i];
		shard->os_replica_nr = grp_size;
		for (j = 0; j < grp_size; j++) {
			struct pool_target *map_tgt;

			rc = dc_cont_tgt_idx2ptr(coh,
					pl_layout->ol_shards[k++].po_target,
					&map_tgt);
			if (rc != 0)
				D_GOTO(out, rc);

			shard->os_ranks[j] = map_tgt->ta_comp.co_rank;
		}
	}
out:
	daos_obj_close(oh, NULL);
	if (rc != 0 && *layout != NULL)
		daos_obj_layout_free(*layout);

	return rc;
}
