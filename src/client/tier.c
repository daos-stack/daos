/**
 * (C) Copyright 2015, 2016 Intel Corporation.
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
#define DD_SUBSYS	DD_FAC(client)

#include <daos/tier.h>
#include "client_internal.h"

int
daos_tier_fetch_cont(daos_handle_t poh, const uuid_t cont_id,
		     daos_epoch_t fetch_ep, daos_oid_list_t *obj_list,
		     daos_event_t *ev)
{
	struct daos_task	*task;
	int			rc;

	rc = daos_client_task_prep(daos_event_comp_cb, NULL, 0, &task, &ev);
	if (rc != 0)
		return rc;

	dc_tier_fetch_cont(poh, cont_id, fetch_ep, obj_list, task);

	return daos_client_result_wait(ev);
}

int
dct_tier_ping(uint32_t ping_val, daos_event_t *ev)
{
	struct daos_task	*task;
	int			rc;

	rc = daos_client_task_prep(daos_event_comp_cb, NULL, 0, &task, &ev);
	if (rc != 0)
		return rc;

	dc_tier_ping(ping_val, task);

	return daos_client_result_wait(ev);
}
