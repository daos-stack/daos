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
	int rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
	}

	rc = dc_tier_fetch_cont(poh, cont_id, fetch_ep, obj_list, ev);
	if (rc)
		return rc;

	/** wait for completion if blocking mode */
	if (daos_event_is_priv(ev))
		rc = daos_event_priv_wait(ev);

	return rc;
}

int
dct_tier_ping(uint32_t ping_val, daos_event_t *ev)
{
	int rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
	}

	rc = dc_tier_ping(ping_val, ev);
	if (rc)
		return rc;

	/** wait for completion if blocking mode */
	if (daos_event_is_priv(ev))
		rc = daos_event_priv_wait(ev);

	return rc;
}
