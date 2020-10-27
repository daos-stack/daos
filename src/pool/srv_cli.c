/**
 * (C) Copyright 2017-2020 Intel Corporation.
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
 * This file includes functions to call client daos API on the server side.
 */
#define D_LOGFAC	DD_FAC(pool)

#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/event.h>
#include <daos/task.h>
#include <daos_types.h>
#include <daos_errno.h>
#include <daos_event.h>
#include <daos_task.h>
#include <daos_srv/daos_server.h>
#include "cli_internal.h"

int
dsc_pool_close(daos_handle_t ph)
{
	struct dc_pool	*pool;

	pool = dc_hdl2pool(ph);
	if (pool == NULL)
		return 0;

	pl_map_disconnect(pool->dp_pool);
	dc_pool_put(pool);
	return 0;
}

int
dsc_pool_open(uuid_t pool_uuid, uuid_t poh_uuid, unsigned int flags,
	      const char *grp, struct pool_map *map, d_rank_list_t *svc_list,
	      daos_handle_t *ph)
{
	struct dc_pool	*pool;
	int		rc = 0;

	if (!daos_handle_is_inval(*ph)) {
		pool = dc_hdl2pool(*ph);
		if (pool != NULL)
			D_GOTO(out, rc = 0);
	}

	/** allocate and fill in pool connection */
	pool = dc_pool_alloc(pool_map_comp_cnt(map));
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_DEBUG(DB_TRACE, "after alloc "DF_UUIDF"\n", DP_UUID(pool_uuid));
	uuid_copy(pool->dp_pool, pool_uuid);
	uuid_copy(pool->dp_pool_hdl, poh_uuid);
	pool->dp_capas = flags;

	/** attach to the server group and initialize rsvc_client */
	rc = dc_mgmt_sys_attach(NULL, &pool->dp_sys);
	if (rc != 0)
		D_GOTO(out, rc);

	D_ASSERT(svc_list != NULL);
	rc = rsvc_client_init(&pool->dp_client, svc_list);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DB_TRACE, "before update "DF_UUIDF"\n", DP_UUID(pool_uuid));
	rc = dc_pool_map_update(pool, map, pool_map_get_version(map), true);
	if (rc)
		D_GOTO(out, rc);

	D_DEBUG(DF_DSMC, DF_UUID": create: hdl="DF_UUIDF" flags=%x\n",
		DP_UUID(pool_uuid), DP_UUID(pool->dp_pool_hdl), flags);

	dc_pool_hdl_link(pool);
	dc_pool2hdl(pool, ph);
out:
	if (pool != NULL)
		dc_pool_put(pool);

	return rc;
}

int
dsc_pool_tgt_exclude(const uuid_t uuid, const char *grp,
		     const d_rank_list_t *svc, struct d_tgt_list *tgts)
{
	daos_pool_update_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, POOL_EXCLUDE);

	rc = dc_task_create(dc_pool_exclude, dsc_scheduler(), NULL, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->grp	= grp;
	args->svc	= (d_rank_list_t *)svc;
	args->tgts	= tgts;
	uuid_copy((unsigned char *)args->uuid, uuid);

	return dsc_task_run(task, NULL, NULL, 0, true);
}

int
dsc_pool_tgt_reint(const uuid_t uuid, const char *grp,
		   const d_rank_list_t *svc, struct d_tgt_list *tgts)
{
	daos_pool_update_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, POOL_EXCLUDE);

	rc = dc_task_create(dc_pool_reint, dsc_scheduler(), NULL, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->grp	= grp;
	args->svc	= (d_rank_list_t *)svc;
	args->tgts	= tgts;
	uuid_copy((unsigned char *)args->uuid, uuid);

	return dsc_task_run(task, NULL, NULL, 0, true);
}
