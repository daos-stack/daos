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

#include <daos/pool.h>
#include "client_internal.h"

int
daos_pool_connect(const uuid_t uuid, const char *grp,
		  const daos_rank_list_t *svc, unsigned int flags,
		  daos_handle_t *poh, daos_pool_info_t *info, daos_event_t *ev)
{
	int rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc)
			return rc;
	}

	rc = dc_pool_connect(uuid, grp, svc, flags, poh, info, ev);
	if (rc)
		return rc;

	/** wait for completion if blocking mode */
	if (daos_event_is_priv(ev))
		rc = daos_event_priv_wait(ev);

	return rc;
}

int
daos_pool_disconnect(daos_handle_t poh, daos_event_t *ev)
{
	int rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc)
			return rc;
	}

	rc = dc_pool_disconnect(poh, ev);
	if (rc)
		return rc;

	/** wait for completion if blocking mode */
	if (daos_event_is_priv(ev))
		rc = daos_event_priv_wait(ev);

	return rc;
}

int
daos_pool_local2global(daos_handle_t poh, daos_iov_t *glob)
{
	return dc_pool_local2global(poh, glob);
}

int
daos_pool_global2local(daos_iov_t glob, daos_handle_t *poh)
{
	return dc_pool_global2local(glob, poh);
}

int
daos_pool_exclude(daos_handle_t poh, daos_rank_list_t *tgts, daos_event_t *ev)
{
	int rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc)
			return rc;
	}

	rc = dc_pool_exclude(poh, tgts, ev);
	if (rc)
		return rc;

	/** wait for completion if blocking mode */
	if (daos_event_is_priv(ev))
		rc = daos_event_priv_wait(ev);

	return rc;
}

static void
pool_query_cp(struct dc_pool *pool, void *arg, int rc, daos_rank_list_t *tgts,
	      daos_pool_info_t *info)
{
	daos_task_complete(arg, rc);
}

int
daos_pool_query_async(daos_handle_t ph, daos_rank_list_t *tgts,
		      daos_pool_info_t *info, struct daos_task *task)
{
	struct dc_pool	*pool;
	int		rc;

	if (tgts == NULL && info == NULL)
		D_GOTO(out_task, rc = -DER_INVAL);

	pool = dc_pool_lookup(ph);
	if (pool == NULL)
		D_GOTO(out_task, rc);

	rc = dc_pool_query(pool, daos_task2ctx(task), tgts, info,
			   pool_query_cp, task);

	dc_pool_put(pool);

	return rc;

out_task:
	daos_task_complete(task, rc);
	return rc;
}

int
daos_pool_query(daos_handle_t poh, daos_rank_list_t *tgts,
		daos_pool_info_t *info, daos_event_t *ev)
{
	struct daos_task *task;
	int		  rc;

	rc = daos_client_prep_task(NULL, NULL, 0, &task, &ev);
	if (rc != 0)
		return rc;

	rc = daos_event_launch(ev);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = daos_pool_query_async(poh, tgts, info, task);
out:
	if (daos_event_is_priv(ev))
		rc = daos_event_priv_wait(ev);

	return rc;
}

int
daos_pool_target_query(daos_handle_t poh, daos_rank_list_t *tgts,
		       daos_rank_list_t *failed, daos_target_info_t *info_list,
		       daos_event_t *ev)
{
	return -DER_NOSYS;
}
