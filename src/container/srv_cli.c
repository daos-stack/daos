/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include <daos_srv/daos_engine.h>
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

	dc_cont_hdl_unlink(cont); /* -1 ref from dc_cont_hdl_link(cont); */
	dc_cont_put(cont);	  /* -1 ref from dc_cont2hdl(cont, coh); */

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
dsc_cont_init_props(struct dc_cont *cont, uuid_t pool_uuid, uuid_t cont_uuid)
{
	int		 rc;

	rc = ds_cont_get_props(&cont->dc_props, pool_uuid, cont_uuid);
	if (rc)
		return rc;

	if (!daos_cont_csum_prop_is_enabled(cont->dc_props.dcp_csum_type))
		return 0;

	/** destroyed in dsc_cont_close */
	rc = daos_csummer_init_with_type(&cont->dc_csummer,
			 daos_contprop2hashtype(cont->dc_props.dcp_csum_type),
			 cont->dc_props.dcp_chunksize,
			 cont->dc_props.dcp_srv_verify);
	return rc;
}

int
dsc_cont_open(daos_handle_t poh, uuid_t cont_uuid, uuid_t coh_uuid,
	      unsigned int flags, daos_handle_t *coh)
{
	struct dc_cont	*cont = NULL;
	struct dc_pool	*pool = NULL;
	int		rc = 0;

	if (daos_handle_is_valid(*coh)) {
		cont = dc_hdl2cont(*coh);
		if (cont != NULL)
			D_GOTO(out, rc);
	}

	D_ASSERT(daos_handle_is_valid(poh));
	pool = dc_hdl2pool(poh);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	cont = dc_cont_alloc(cont_uuid);
	if (cont == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = dsc_cont_init_props(cont, pool->dp_pool, cont_uuid);
	if (rc != 0)
		D_GOTO(out, rc);

	uuid_copy(cont->dc_cont_hdl, coh_uuid);
	cont->dc_capas = flags;

	D_RWLOCK_WRLOCK(&pool->dp_co_list_lock);
	d_list_add(&cont->dc_po_list, &pool->dp_co_list);
	cont->dc_pool_hdl = poh;
	D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);

	dc_cont_hdl_link(cont); /* +1 ref */
	dc_cont2hdl(cont, coh); /* +1 ref */

out:
	if (cont != NULL)
		dc_cont_put(cont);
	if (pool != NULL)
		dc_pool_put(pool);

	return rc;
}

struct daos_csummer *
dsc_cont2csummer(daos_handle_t coh)
{
	struct dc_cont		*cont = NULL;
	struct daos_csummer	*csummer;

	cont = dc_hdl2cont(coh);
	if (cont == NULL)
		return NULL;
	csummer = cont->dc_csummer;
	dc_cont_put(cont);

	return csummer;
}

int
dsc_cont_get_props(daos_handle_t coh, struct cont_props *props)
{
	struct dc_cont *cont = NULL;

	cont = dc_hdl2cont(coh);
	if (cont == NULL)
		return -DER_NO_HDL;

	*props = cont->dc_props;
	dc_cont_put(cont);
	return 0;
}
