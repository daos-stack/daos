/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/mem.h>
#include <daos_srv/vos.h>
#include <daos_srv/dlck.h>

#include "dlck_args.h"
#include "dlck_engine.h"
#include "dlck_pool.h"

/**
 * Process a single container.
 *
 * \param[in]	poh		Pool handle.
 * \param[in]	co_uuid		Container UUID.
 * \param[in]	write_mode	Is the write mode enabled?
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
static int
process_cont(daos_handle_t poh, uuid_t co_uuid, bool write_mode)
{
	daos_handle_t coh;
	d_vector_t    dv;
	int           rc;

	rc = vos_cont_open(poh, co_uuid, &coh);
	if (rc != 0) {
		return rc;
	}

	d_vector_init(sizeof(struct dlck_dtx_rec), &dv);

	rc = dlck_vos_cont_rec_get_active(coh, &dv, NULL);
	if (rc != 0) {
		goto fail;
	}

	if (write_mode) {
		rc = dlck_dtx_act_recs_remove(coh);
		if (rc != 0) {
			goto fail;
		}

		rc = dlck_dtx_act_recs_set(coh, &dv);
		if (rc != 0) {
			goto fail;
		}
	}

	d_vector_free(&dv);

	rc = vos_cont_close(coh);

	return rc;

fail:
	d_vector_free(&dv);

	(void)vos_cont_close(coh);

	return rc;
}

/**
 * Process all containers found in the pool.
 *
 * \param[in]	poh		Pool handle.
 * \param[in]	write_mode	Is the write mode enabled?
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
static int
process_pool(daos_handle_t poh, bool write_mode)
{
	d_list_t                  co_uuids = D_LIST_HEAD_INIT(co_uuids);
	struct co_uuid_list_elem *elm, *next;
	int                       rc;

	rc = dlck_pool_cont_list(poh, &co_uuids);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	d_list_for_each_entry(elm, &co_uuids, link) {
		rc = process_cont(poh, elm->uuid, write_mode);
		if (rc != DER_SUCCESS) {
			break;
		}
	}

	d_list_for_each_entry_safe(elm, next, &co_uuids, link) {
		d_list_del(&elm->link);
		D_FREE(elm);
	}

	D_ASSERT(d_list_empty(&co_uuids));

	return rc;
}

/**
 * @struct xstream_arg
 *
 * Arguments passed to to the main ULT on each of the execution streams.
 */
struct xstream_arg {
	struct dlck_args    *args;   /** Complete set of arguments. */
	struct dlck_engine  *engine; /** Engine itself. */
	struct dlck_xstream *xs;     /** The execution stream the ULT is run in. */
	int                  rc;     /** [out] return code */
};

static void
exec_one(void *arg)
{
	struct xstream_arg *xa = arg;
	const bool          write_mode = xa->args->common.write_mode;
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
		rc = dlck_pool_open(xa->args->engine.storage_path, file->po_uuid, xa->xs->tgt_id,
				    &poh);
		ABT_mutex_unlock(xa->engine->open_mtx);
		if (rc != 0) {
			xa->rc = rc;
			return;
		}

		if (uuid_is_null(xa->args->files.co_uuid)) {
			rc = process_pool(poh, write_mode);
		} else {
			rc = process_cont(poh, xa->args->files.co_uuid, write_mode);
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

/**
 * Allocate arguments for an ULT.
 *
 * \param[in]	engine		Engine the ULT is about to be run in.
 * \param[in]	idx		ULT ID.
 * \param[in]	args		Set of arguments.
 * \param[out]	output_arg	Allocated argument for the ULT.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_NOMEM	Out of memory.
 */
static int
arg_alloc(struct dlck_engine *engine, int idx, void *args, void **output_arg)
{
	struct xstream_arg *xa;

	D_ALLOC_PTR(xa);
	if (xa == NULL) {
		return -DER_NOMEM;
	}

	xa->args   = args;
	xa->engine = engine;
	xa->xs     = &engine->xss[idx];

	*output_arg = xa;

	return DER_SUCCESS;
}

/**
 * Free arguments of an ULT.
 *
 * \param[in,out]	arg	ULT arguments to process and free.
 *
 * \return The return code for the ULT.
 */
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
 * Create pool directories for all files provided.
 *
 * \param[in]	storage_path	Engine the ULT is about to be run in.
 * \param[in]	files		List of files.
 *
 * \retval DER_SUCCESS		Success.
 * \retval -DER_NOMEM		Out of memory.
 * \retval -DER_NO_PERM		Permission problem. Please see mkdir(2).
 * \retval -DER_NONEXIST	A component of the \p storage_path does not exist.
 * \retval -DER_*		Possibly other errors but not -DER_EXIST.
 */
static int
pool_mkdir_all(const char *storage_path, d_list_t *files)
{
	struct dlck_file *file;
	int               rc;

	d_list_for_each_entry(file, files, link) {
		rc = dlck_pool_mkdir(storage_path, file->po_uuid);
		if (rc != 0 && rc != -DER_EXIST) {
			return rc;
		}
	}

	return DER_SUCCESS;
}

int
dlck_dtx_act_recs_recover(struct dlck_args *args)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	if (!args->common.write_mode) {
		DLCK_PRINT(args, "Write mode is not enabled. Changes won't be applied.");
	}

	rc = dlck_engine_start(&args->engine, &engine);
	if (rc != 0) {
		return rc;
	}

	rc = pool_mkdir_all(args->engine.storage_path, &args->files.list);
	if (rc != 0) {
		goto fail;
	}

	rc = dlck_engine_exec_all(engine, exec_one, arg_alloc, args, arg_free);
	if (rc != 0) {
		goto fail;
	}

	rc = dlck_engine_stop(engine);

	return rc;

fail:
	(void)dlck_engine_stop(engine);

	return rc;
}
