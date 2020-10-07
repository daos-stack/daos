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
#define D_LOGFAC	DD_FAC(container)

#include <daos/pool.h>
#include <daos/container.h>
#include <daos/cont_props.h>
#include <daos/event.h>
#include <daos/task.h>
#include <daos_types.h>
#include <daos_errno.h>
#include <daos_event.h>
#include <daos_task.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/container.h>
#include "cli_internal.h"

int
dsc_cont_close(daos_handle_t poh, daos_handle_t coh)
{
	struct dc_cont *cont = NULL;
	struct dc_pool *pool = NULL;
	int		rc = 0;

	cont = dc_hdl2cont(coh);
	if (cont == NULL)
		return 0;

	pool = dc_hdl2pool(poh);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	dc_cont_put(cont);

	/* Remove the container from pool container list */
	D_RWLOCK_WRLOCK(&pool->dp_co_list_lock);
	d_list_del_init(&cont->dc_po_list);
	D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);

	daos_csummer_destroy(&cont->dc_csummer);

out:
	if (cont != NULL)
		dc_cont_put(cont);
	if (pool != NULL)
		dc_pool_put(pool);

	return rc;
}

static int
dsc_cont_csummer_init(struct daos_csummer **csummer,
		      uuid_t pool_uuid, uuid_t cont_uuid)
{
	struct ds_pool	*pool;
	int		 rc;
	struct cont_props cont_props;

	pool = ds_pool_lookup(pool_uuid);
	if (pool == NULL)
		return -DER_NONEXIST;
	rc = ds_get_cont_props(&cont_props, pool->sp_iv_ns, cont_uuid);

	if (rc == 0 &&
	    daos_cont_csum_prop_is_enabled(cont_props.dcp_csum_type))
		rc = daos_csummer_init_with_type(csummer,
			 daos_contprop2hashtype(cont_props.dcp_csum_type),
			 cont_props.dcp_chunksize,
			 cont_props.dcp_srv_verify);

	ds_pool_put(pool);

	return rc;
}

int
dsc_cont_open(daos_handle_t poh, uuid_t cont_uuid, uuid_t coh_uuid,
	      unsigned int flags, daos_handle_t *coh)
{
	struct dc_cont	*cont = NULL;
	struct dc_pool	*pool = NULL;
	int		rc = 0;

	if (!daos_handle_is_inval(*coh)) {
		cont = dc_hdl2cont(*coh);
		if (cont != NULL)
			D_GOTO(out, rc);
	}

	D_ASSERT(!daos_handle_is_inval(poh));
	pool = dc_hdl2pool(poh);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	cont = dc_cont_alloc(cont_uuid);
	if (cont == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/** destroyed in dsc_cont_close */
	rc = dsc_cont_csummer_init(&cont->dc_csummer, pool->dp_pool,
				   cont_uuid);
	if (rc != 0) {
		dc_cont_free(cont);
		cont = NULL;
		D_GOTO(out, rc);
	}

	uuid_copy(cont->dc_cont_hdl, coh_uuid);
	cont->dc_capas = flags;

	D_RWLOCK_WRLOCK(&pool->dp_co_list_lock);
	d_list_add(&cont->dc_po_list, &pool->dp_co_list);
	cont->dc_pool_hdl = poh;
	D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);

	dc_cont_hdl_link(cont);
	dc_cont2hdl(cont, coh);
out:
	if (cont != NULL)
		dc_cont_put(cont);
	if (pool != NULL)
		dc_pool_put(pool);

	return rc;
}
