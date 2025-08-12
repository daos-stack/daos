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
 * \param[out]	stats		Statistics.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
static int
process_cont(daos_handle_t poh, uuid_t co_uuid, bool write_mode, struct dlck_stats *stats)
{
	daos_handle_t coh;
	d_vector_t    dv;
	int           rc;

	rc = vos_cont_open(poh, co_uuid, &coh);
	if (rc != 0) {
		return rc;
	}

	d_vector_init(sizeof(struct dlck_dtx_rec), &dv);

	rc = dlck_vos_cont_rec_get_active(coh, &dv, stats);
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
 * \param[out]	stats		Statistics.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
static int
process_pool(daos_handle_t poh, bool write_mode, struct dlck_stats *stats)
{
	d_list_t                  co_uuids = D_LIST_HEAD_INIT(co_uuids);
	struct co_uuid_list_elem *elm, *next;
	int                       rc;

	rc = dlck_pool_cont_list(poh, &co_uuids);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	d_list_for_each_entry(elm, &co_uuids, link) {
		rc = process_cont(poh, elm->uuid, write_mode, stats);
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
	struct dlck_control *ctrl;   /** Control state. */
	struct dlck_engine  *engine; /** Engine itself. */
	struct dlck_xstream *xs;     /** The execution stream the ULT is run in. */
	struct dlck_stats    stats;  /** Cumulative stats for all the DLCK calls. */
	int                  rc;     /** [out] return code */
};

static void
exec_one(void *arg)
{
	struct xstream_arg *xa         = arg;
	const bool          write_mode = xa->ctrl->common.write_mode;
	struct dlck_file   *file;
	daos_handle_t       poh;
	int                 rc;

	rc = dlck_engine_xstream_init(xa->xs);
	if (rc != DER_SUCCESS) {
		xa->rc = rc;
		return;
	}

	d_list_for_each_entry(file, &xa->ctrl->files.list, link) {
		/** do not process the given file if the target is excluded */
		if ((file->targets_bitmap & (1 << xa->xs->tgt_id)) == 0) {
			continue;
		}

		rc = dlck_abt_pool_open(xa->engine->open_mtx, xa->ctrl->engine.storage_path,
					file->po_uuid, xa->xs->tgt_id, &poh);
		if (rc != DER_SUCCESS) {
			xa->rc = rc;
			break;
		}

		if (uuid_is_null(xa->ctrl->common.co_uuid)) {
			rc = process_pool(poh, write_mode, &xa->stats);
		} else {
			rc = process_cont(poh, xa->ctrl->common.co_uuid, write_mode, &xa->stats);
		}

		if (rc != DER_SUCCESS) {
			xa->rc = rc;
			(void)dlck_abt_pool_close(xa->engine->open_mtx, poh);
			break;
		}

		rc = dlck_abt_pool_close(xa->engine->open_mtx, poh);
		if (rc != DER_SUCCESS) {
			xa->rc = rc;
			break;
		}
	}

	if (xa->rc != DER_SUCCESS) {
		goto fail_xstream_fini;
	}

	rc = dlck_engine_xstream_fini(xa->xs);
	if (rc != DER_SUCCESS) {
		xa->rc = rc;
	}

	return;

fail_xstream_fini:
	(void)dlck_engine_xstream_fini(xa->xs);
}

/**
 * Allocate arguments for an ULT.
 *
 * \param[in]	engine		Engine the ULT is about to be run in.
 * \param[in]	idx		ULT ID.
 * \param[in]	ctrl_ptr	Control state to be passed to the ULT.
 * \param[out]	output_arg	Allocated argument for the ULT.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_NOMEM	Out of memory.
 */
static int
arg_alloc(struct dlck_engine *engine, int idx, void *ctrl_ptr, void **output_arg)
{
	struct xstream_arg *xa;

	D_ALLOC_PTR(xa);
	if (xa == NULL) {
		return -DER_NOMEM;
	}

	xa->ctrl   = ctrl_ptr;
	xa->engine = engine;
	xa->xs     = &engine->xss[idx];
	xa->rc     = DER_SUCCESS;

	*output_arg = xa;

	return DER_SUCCESS;
}

/**
 * Free arguments of an ULT.
 *
 * \param[out]		ctrl_ptr	Control state to collect stats in.
 * \param[in,out]	arg		ULT arguments to process and free.
 *
 * \return The return code for the ULT.
 */
static int
arg_free(void *ctrl_ptr, void **arg)
{
	struct dlck_control *ctrl = ctrl_ptr;
	struct xstream_arg  *xa   = *arg;
	int                  rc   = xa->rc;

	DLCK_PRINTF(ctrl, "Touched[%d]: %u\n", xa->xs->tgt_id, xa->stats.touched);

	ctrl->stats.touched += xa->stats.touched;

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

	if (d_list_empty(files)) {
		return -DER_ENOENT;
	}

	d_list_for_each_entry(file, files, link) {
		rc = dlck_pool_mkdir(storage_path, file->po_uuid);
		if (rc != 0 && rc != -DER_EXIST) {
			return rc;
		}
	}

	return DER_SUCCESS;
}

int
dlck_dtx_act_recs_recover(struct dlck_control *ctrl)
{
	struct dlck_engine *engine = NULL;
	int                 rc;

	if (ctrl == NULL) {
		return -DER_INVAL;
	}

	if (!ctrl->common.write_mode) {
		DLCK_PRINT(ctrl, "Write mode is not enabled. Changes won't be applied.\n");
	}

	rc = pool_mkdir_all(ctrl->engine.storage_path, &ctrl->files.list);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	rc = dlck_engine_start(&ctrl->engine, &engine);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	rc = dlck_engine_exec_all(engine, exec_one, arg_alloc, ctrl, arg_free);
	if (rc != DER_SUCCESS) {
		goto fail;
	}

	DLCK_PRINTF(ctrl, "Touched: %u\n", ctrl->stats.touched);

	return dlck_engine_stop(engine);

fail:
	(void)dlck_engine_stop(engine);

	return rc;
}
