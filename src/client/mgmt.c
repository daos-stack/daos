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

#include <daos/mgmt.h>
#include <client_internal.h>

int
daos_pool_create(unsigned int mode, unsigned int uid, unsigned int gid,
		 const char *grp, const daos_rank_list_t *tgts, const char *dev,
		 daos_size_t size, daos_rank_list_t *svc, uuid_t uuid,
		 daos_event_t *ev)
{
	int rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc)
			return rc;
	}

	rc = dc_pool_create(mode, uid, gid, grp, tgts, dev, size, svc, uuid,
			    ev);
	if (rc)
		return rc;

	/** wait for completion if blocking mode */
	if (daos_event_is_priv(ev))
		rc = daos_event_priv_wait(ev);

	return rc;
}

int
daos_pool_destroy(const uuid_t uuid, const char *grp, int force,
		  daos_event_t *ev)
{
	int rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc)
			return rc;
	}

	rc = dc_pool_destroy(uuid, grp, force, ev);
	if (rc)
		return rc;

	/** wait for completion if blocking mode */
	if (daos_event_is_priv(ev))
		rc = daos_event_priv_wait(ev);

	return rc;
}
