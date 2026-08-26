/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/mem.h>
#include <daos_srv/mgmt_tgt_common.h>
#include <daos_srv/vos.h>

#include "../dlck_args.h"
#include "../dlck_bitmap.h"
#include "../dlck_checker.h"
#include "../dlck_engine.h"
#include "../dlck_pool.h"
#include "../dlck_report.h"

#define DLCK_CHECK_RESULT_PREFIX_FMT(TYPE_STR) "[%d] " TYPE_STR " " DF_UUIDF " check result"
#define DLCK_WARNINGS_NUM_FMT                  " (%u warning(s))"

#define REPORT_RESULT(CK, PREFIX_FMT, TGT_ID, UUID, RC, WARN_NUM)                                  \
	do {                                                                                       \
		if (RC == DER_SUCCESS && WARN_NUM > 0) {                                           \
			CK_PRINTF(CK, PREFIX_FMT CHECKER_OK_INFIX DLCK_WARNINGS_NUM_FMT ".\n",     \
				  TGT_ID, DP_UUID(UUID), WARN_NUM);                                \
		} else {                                                                           \
			CK_PRINTFL_RC(CK, RC, PREFIX_FMT, TGT_ID, DP_UUID(UUID));                  \
		}                                                                                  \
	} while (0)

#define POOL_REPORT_RESULT(CK, TGT_ID, UUID, RC, WARN_NUM)                                         \
	REPORT_RESULT(CK, DLCK_CHECK_RESULT_PREFIX_FMT("pool     "), TGT_ID, UUID, RC, WARN_NUM)

#define CONT_REPORT_RESULT(CK, TGT_ID, UUID, RC, WARN_NUM)                                         \
	REPORT_RESULT(CK, DLCK_CHECK_RESULT_PREFIX_FMT("container"), TGT_ID, UUID, RC, WARN_NUM)

struct bundle {
	struct xstream_arg *xa;
	struct checker     *ck;
};

static int
obj_process(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
	     vos_iter_param_t *param, void *cb_arg, unsigned int *acts)
{
	struct bundle      *bndl    = cb_arg;
	// struct xstream_arg *xa      = bndl->xa;
	// struct checker     *main_ck = &xa->ctrl->checker;
	struct checker     *ck      = bndl->ck;

	CK_PRINTF(ck, "oid: "DF_UOID"\n", DP_UOID(entry->ie_oid));

	return 0;
}

/**
 * Target thread (worker). Check trees of a single container.
 *
 * \param[in]	ck	Checker.
 * \param[in]	cont	Container to check.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Errors returned by the tree checking logic.
 */
static int
trees_process(daos_handle_t coh, struct bundle *bndl)
{
	vos_iter_param_t        param   = {0};
	struct vos_iter_anchors anchors = {0};

	param.ip_hdl        = coh;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	param.ip_flags      = VOS_IT_FOR_CHECK;

	return vos_iterate(&param, VOS_ITER_OBJ, false, &anchors, obj_process, NULL, bndl, NULL);
}

/**
 * Target thread (worker). VOS iterator callback. Check a single container.
 *
 * \param[in]	ih	Iterator handle.
 * \param[in]	entry	Iterator entry.
 * \param[in]	type	Iteration type.
 * \param[in]	param	Iterator parameters.
 * \param[in]	cb_arg	Callback argument.
 * \param[in]	acts	Actions.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Errors returned by vos_cont_open_ex().
 */
static int
cont_process(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
	     vos_iter_param_t *param, void *cb_arg, unsigned int *acts)
{
	struct bundle      *bndl    = cb_arg;
	struct xstream_arg *xa      = bndl->xa;
	struct checker     *main_ck = &xa->ctrl->checker;
	struct checker     *ck      = bndl->ck;
	daos_handle_t       coh;
	int                 rc;

	rc = vos_cont_open_ex(param->ip_hdl, entry->ie_couuid, ck, &coh);
	if (rc == DER_SUCCESS) {
		CONT_REPORT_RESULT(main_ck, xa->xs->tgt_id, entry->ie_couuid, rc, ck->ck_warnings_num);

		trees_process(coh, bndl);

		(void)vos_cont_close(coh);
	}

	/** continue checking other containers even if this one failed */
	return 0;
}

/**
 * Target thread (worker). Check all containers of a single pool.
 *
 * \param[in]	poh	Pool handle.
 * \param[in]	ck	Checker.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Errors either from the VOS iterator or vos_cont_open_ex().
 */
static int
conts_process(struct xstream_arg *xa, daos_handle_t poh, struct checker *ck)
{
	vos_iter_param_t        param   = {0};
	struct vos_iter_anchors anchors = {0};
	struct bundle           cb_arg  = {.xa = xa, .ck = ck};

	param.ip_hdl        = poh;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	param.ip_flags      = VOS_IT_FOR_CHECK;

	return vos_iterate(&param, VOS_ITER_COUUID, false, &anchors, cont_process, NULL, &cb_arg,
			   NULL);
}

/**
 * Target thread (worker). Check a single pool.
 *
 * \param[in]	xa	Target's arguments.
 * \param[in]	file	File to process.
 * \param[in]	ck	Checker.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_NOMEM	Out of memory.
 * \retval -DER_*	Other errors.
 */
static int
pool_process(struct xstream_arg *xa, struct dlck_file *file, struct checker *ck)
{
	struct checker *main_ck = &xa->ctrl->checker;
	char         *path;
	daos_handle_t poh;
	int           rc;

	rc = dlck_pool_file_preallocate(xa->ctrl->engine.storage_path, file->po_uuid,
					xa->xs->tgt_id);
	CK_PRINTL_RC(ck, xa->rc, "VOS file allocation");
	if (rc != DER_SUCCESS) {
		return rc;
	}

	/** generate a VOS file path */
	rc = ds_mgmt_file(xa->ctrl->engine.storage_path, file->po_uuid, VOS_FILE, &xa->xs->tgt_id,
			  &path);
	if (rc != DER_SUCCESS) {
		CK_PRINTL_RC(ck, xa->rc, "VOS file path allocation failed");
		return rc;
	}

	rc = vos_pool_open_metrics(path, file->po_uuid, DLCK_POOL_OPEN_FLAGS, NULL, ck, &poh);
	if (rc == DER_SUCCESS) {
		POOL_REPORT_RESULT(main_ck, xa->xs->tgt_id, file->po_uuid, rc, ck->ck_warnings_num);
		rc = conts_process(xa, poh, ck);

		(void)vos_pool_close(poh);
	}
	D_FREE(path);

	/** check */
	if (rc != DER_SUCCESS) {
		/** ignore a possible error from the unlock */
		return rc;
	}

	return DER_SUCCESS;
}

/**
 * Target thread (worker).
 */
static void
exec_one(void *arg)
{
	struct xstream_arg *xa = arg;
	struct dlck_file   *file;
	struct checker     *main_ck = &xa->ctrl->checker;
	struct checker      ck;
	int                 rc;

	/** initialize the daos_io_* thread */
	rc = dlck_engine_xstream_init(xa->xs);
	if (rc != DER_SUCCESS) {
		xa->rc       = rc;
		xa->progress = DLCK_XSTREAM_PROGRESS_END;
		return;
	}

	d_list_for_each_entry(file, &xa->ctrl->files.list, link) {
		/** do not process the given file if the target is not requested */
		if (dlck_bitmap_isclr32(file->targets_bitmap, xa->xs->tgt_id)) {
			/** report the progress to the main thread */
			++xa->progress;
			continue;
		}

		/** initialize the logfile and its print utility */
		rc = dlck_checker_worker_init(&xa->ctrl->common.options, xa->ctrl->log_dir,
					      file->po_uuid, xa->xs->tgt_id, main_ck, &ck);
		if (rc != DER_SUCCESS) {
			/** There is no point continuing without a logfile. */
			dlck_xstream_set_rc(xa, rc);
			xa->progress = DLCK_XSTREAM_PROGRESS_END;
			break;
		}

		/** check the pool */
		rc = pool_process(xa, file, &ck);
		dlck_xstream_set_rc(xa, rc);
		dlck_uadd_no_overflow(xa->warnings_num, ck.ck_warnings_num, &xa->warnings_num);
		/** Continue to the next pool regardless of the result. */

		/** close the logfile */
		dlck_checker_worker_fini(&ck);

		/** report the progress to the main thread */
		++xa->progress;
	}

	if (xa->rc != DER_SUCCESS) {
		(void)dlck_engine_xstream_fini(xa->xs);
		return;
	}

	rc = dlck_engine_xstream_fini(xa->xs);
	dlck_xstream_set_rc(xa, rc);
}

/**
 * The main thread spawns and waits for other threads to complete their tasks.
 */
int
dlck_cmd_check(struct dlck_control *ctrl)
{
	D_ASSERT(ctrl != NULL);

	struct checker     *ck                 = &ctrl->checker;
	char                log_dir_template[] = "/tmp/dlck_check_XXXXXX";
	struct dlck_engine *engine             = NULL;
	int                *rcs;
	int                 rc;

	/** create a log directory */
	if (DAOS_FAIL_CHECK(DLCK_FAULT_CREATE_LOG_DIR)) { /** fault injection */
		ctrl->log_dir = NULL;
		errno         = daos_fail_value_get();
	} else {
		ctrl->log_dir = mkdtemp(log_dir_template);
	}
	if (ctrl->log_dir == NULL) {
		rc = daos_errno2der(errno);
		CK_PRINTL_RC(ck, rc, "Cannot create log directory");
		return rc;
	}
	CK_PRINTF(ck, "Log directory: %s\n", ctrl->log_dir);

	CK_PRINT(ck, "Start the engine... ");
	rc = dlck_engine_start(&ctrl->engine, &engine);
	CK_APPENDL_RC(ck, rc);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	if (d_list_empty(&ctrl->files.list)) {
		/** no files specified means all files are requested */
		CK_PRINT(ck, "Read the list of pools... ");
		rc = dlck_pool_list(&ctrl->files.list);
		CK_APPENDL_RC(ck, rc);
		if (rc != DER_SUCCESS) {
			goto err_stop_engine;
		}
		/** no files exist */
		if (d_list_empty(&ctrl->files.list)) {
			CK_PRINT(ck, "No pools exist. Exiting...\n");
			goto err_stop_engine;
		}
	}

	CK_PRINT(ck, "Create pools directories... ");
	rc = dlck_pool_mkdir_all(ctrl->engine.storage_path, &ctrl->files.list, ck);
	CK_APPENDL_RC(ck, rc);
	if (rc != DER_SUCCESS) {
		goto err_stop_engine;
	}

	/** allocate an array of return codes for targets */
	D_ALLOC_ARRAY(rcs, ctrl->engine.targets);
	if (rcs == NULL) {
		rc = -DER_NOMEM;
		CK_PRINTL_RC(ck, rc, "");
		goto err_stop_engine;
	}

	rc = dlck_engine_exec_all(engine, exec_one, dlck_engine_xstream_arg_alloc, ctrl,
				  dlck_engine_xstream_arg_free, ck);
	if (rc != DER_SUCCESS) {
		goto err_free_rcs;
	}

	CK_PRINT(ck, "Stop the engine... ");
	rc = dlck_engine_stop(engine);
	CK_APPENDL_RC(ck, rc);

	/** Ignore an error for now to print the collected results. */
	dlck_report_results(rcs, ctrl->engine.targets, ctrl->warnings_num, ck);
	D_FREE(rcs);

	/** Return the first encountered error. */
	return rc;

err_free_rcs:
	D_FREE(rcs);
err_stop_engine:
	(void)dlck_engine_stop(engine);

	return rc;
}
