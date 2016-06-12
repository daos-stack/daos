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
 * This file is part of daos_sr
 *
 * src/dsr/cli_pool.c
 */
#include "cli_internal.h"

/* XXX This is a workaround, we need a cleaner way to get pool map. */
#include "../dsm/dsmc_internal.h"

static int
dsr_pool_connect_comp(void *args, int rc)
{
	struct dsmc_pool *pool;
	daos_handle_t	 *poh = (daos_handle_t *)args;

	if (rc != 0)
		goto failed;

	pool = dsmc_handle2pool(*poh);
	if (pool == NULL)
		D_GOTO(failed, rc = -DER_NO_HDL);

	D_DEBUG(DF_SR, "Create placement map for the pool.\n");
	rc = dsr_pl_map_init(pool->dp_map);
	dsmc_pool_put(pool);

	return 0;
 failed:
	if (!daos_handle_is_inval(*poh))
		dsm_pool_disconnect(*poh, NULL); /* another async operation? */
	return rc;
}

int
dsr_pool_connect(const uuid_t uuid, const char *grp,
		 const daos_rank_list_t *tgts, unsigned int flags,
		 daos_rank_list_t *failed, daos_handle_t *poh,
		 daos_pool_info_t *info, daos_event_t *ev)
{
	struct daos_oper_grp	*opg;
	int			 rc;

	rc = daos_oper_grp_create(ev, dsr_pool_connect_comp, poh, &opg);
	if (rc != 0)
		return rc;

	*poh = DAOS_HDL_INVAL;
	rc = daos_oper_grp_new_ev(opg, &ev);
	if (rc != 0)
		D_GOTO(failed, rc);

	/* Call it in sync mode to simplify things for now... */
	rc = dsm_pool_connect(uuid, grp, tgts, flags, failed, poh, info, ev);
	if (rc != 0)
		D_GOTO(failed, rc);

	rc = daos_oper_grp_launch(opg);
	if (rc != 0)
		D_GOTO(failed, rc);

	return 0;
 failed:
	daos_oper_grp_destroy(opg, rc);
	return rc;
}

int
dsr_pool_disconnect(daos_handle_t poh, daos_event_t *ev)
{
	dsr_pl_map_fini();
	return dsm_pool_disconnect(poh, ev);
}
