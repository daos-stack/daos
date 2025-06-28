/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdlib.h>
#include <stdio.h>
#include <daos/mem.h>
#include <daos/btree_class.h>
#include <gurt/telemetry_producer.h>
#include <daos_srv/vos.h>
#include <daos_srv/dlck.h>
#include <daos_srv/daos_mgmt_srv.h>
#include <daos_version.h>

#include <libpmemobj.h>

#include "dlck_args.h"
#include "dlck_engine.h"
#include "dlck_common.h"

// const char pool_uuid[] = "3676cebe-bc38-4add-b2a6-bc2025f7e277";
// const char pool2_uuid[] = "07e9e5fb-4388-4e81-9d07-cdd139899739";
// const char cont2_uuid[] = "001a010c-4b51-4855-a5cb-fbf582b37000";

static int
process_cont(daos_handle_t poh, uuid_t co_uuid)
{
	daos_handle_t coh;
	int           rc;

	rc = vos_cont_open(poh, co_uuid, &coh);
	if (rc != 0) {
		return rc;
	}

	d_vector_t dv;
	d_vector_init(sizeof(struct dlck_dtx_rec), &dv);

	rc = dlck_vos_cont_rec_get_active(coh, &dv, NULL);
	if (rc != 0) {
		return rc;
	}

	rc = dlck_dtx_act_recs_remove(coh);
	if (rc != 0) {
		return rc;
	}

	rc = dlck_dtx_act_recs_set(coh, &dv);
	if (rc != 0) {
		return rc;
	}

	d_vector_free(&dv);

	rc = vos_cont_close(coh);
	if (rc != 0) {
		return rc;
	}

	return 0;
}

static int
process_pool(daos_handle_t poh)
{
	d_list_t                  co_uuids = D_LIST_HEAD_INIT(co_uuids);
	struct co_uuid_list_elem *elm, *next;
	int                       rc;

	rc = dlck_pool_cont_list(poh, &co_uuids);
	if (rc != 0) {
		return rc;
	}

	d_list_for_each_entry_safe(elm, next, &co_uuids, link) {
		rc = process_cont(poh, elm->uuid);
		if (rc != 0) {
			return rc;
		}

		d_list_del(&elm->link);
		D_FREE(elm);
	}

	D_ASSERT(d_list_empty(&co_uuids));

	return 0;
}

struct xstream_arg {
	struct dlck_args    *args;
	struct dlck_engine  *engine;
	struct dlck_xstream *xs;
	int                  rc;
};

static void
exec_one(void *arg)
{
	struct xstream_arg *xa = arg;
	struct dlck_file   *file;
	daos_handle_t       poh;
	int                 rc;

	rc = dlck_engine_xstream_init(xa->xs);
	if (rc != 0) {
		xa->rc = rc;
		return;
	}

	d_list_for_each_entry(file, &xa->args->files.list, link) {
		if ((file->targets & (1 << xa->xs->tgt_id)) == 0) {
			continue;
		}

		ABT_mutex_lock(xa->engine->open_mtx);
		rc = dlck_pool_open(xa->args->engine.storage_path, file, xa->xs->tgt_id, &poh);
		ABT_mutex_unlock(xa->engine->open_mtx);
		if (rc != 0) {
			xa->rc = rc;
			return;
		}

		if (uuid_is_null(xa->args->files.co_uuid)) {
			rc = process_pool(poh);
		} else {
			rc = process_cont(poh, xa->args->files.co_uuid);
		}

		if (rc != 0) {
			xa->rc = rc;
			return;
		}

		ABT_mutex_lock(xa->engine->open_mtx);
		rc = vos_pool_close(poh);
		if (rc != 0) {
			xa->rc = rc;
			return;
		}
		ABT_mutex_unlock(xa->engine->open_mtx);
	}

	rc = dlck_engine_xstream_fini(xa->xs);
	if (rc != 0) {
		xa->rc = rc;
		return;
	}
}

static int
arg_alloc(struct dlck_engine *engine, int idx, void *args, void **output_arg)
{
	struct xstream_arg *xa;

	D_ALLOC_PTR(xa);
	if (xa == NULL) {
		return ENOMEM;
	}

	xa->args   = args;
	xa->engine = engine;
	xa->xs     = &engine->xss[idx];

	*output_arg = xa;

	return 0;
}

static int
arg_free(void **arg)
{
	struct xstream_arg *xa = *arg;
	int                 rc = xa->rc;

	D_FREE(*arg);
	*arg = NULL;

	return rc;
}

/**
 * XXX teardown
 */
static int
pool_mkdir_all(struct dlck_args *args, struct dlck_engine *engine)
{
	struct dlck_file *file;
	int               rc;

	d_list_for_each_entry(file, &args->files.list, link) {
		rc = dlck_pool_mkdir(args->engine.storage_path, file->po_uuid);
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
}

/**
 * XXX teardown
 */
int
dlck_dtx_act_recs_recover(struct dlck_args *args)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	rc = dlck_engine_start(&args->engine, &engine);
	if (rc != 0) {
		return rc;
	}

	rc = pool_mkdir_all(args, engine);
	if (rc != 0) {
		return rc;
	}

	rc = dlck_engine_exec_all(engine, exec_one, arg_alloc, args, arg_free);
	if (rc != 0) {
		return rc;
	}

	rc = dlck_engine_stop(engine);
	if (rc != 0) {
		return rc;
	}

	return 0;
}
