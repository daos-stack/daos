/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/dlck.h>
#include <daos/mem.h>
#include <daos_srv/mgmt_tgt_common.h>
#include <daos_srv/vos.h>

#include "../dlck_args.h"
#include "../dlck_bitmap.h"
#include "../dlck_engine.h"
#include "../dlck_pool.h"
#include "../dlck_print.h"
#include "../dlck_report.h"

/**
 * Target thread (worker). Check a single pool.
 *
 * \param[in]	xa	Target's arguments.
 * \param[in]	file	File to process.
 * \param[in]	main_dp	Main print utility.
 * \param[in]	dp	Target's print utility.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_NOMEM	Out of memory.
 * \retval -DER_*	Other errors.
 */
static int
pool_process(struct xstream_arg *xa, struct dlck_file *file, struct dlck_print *main_dp,
	     struct dlck_print *dp)
{
	char         *path;
	daos_handle_t poh;
	int           rc;

	/** generate a VOS file path */
	rc = ds_mgmt_file(xa->ctrl->engine.storage_path, file->po_uuid, VOS_FILE, &xa->xs->tgt_id,
			  &path);
	if (rc != DER_SUCCESS) {
		DLCK_PRINTL_RC(dp, xa->rc, "VOS file path allocation failed");
		return rc;
	}

	rc = vos_pool_open_metrics(path, file->po_uuid, DLCK_POOL_OPEN_FLAGS, NULL, dp, &poh);
	if (rc == DER_SUCCESS) {
		(void)vos_pool_close(poh);
	}
	D_FREE(path);

	/** check  */
	if (rc != DER_SUCCESS) {
		/** ignore a possible error from the unlock */
		return rc;
	}

	return DER_SUCCESS;
}

#define DLCK_POOL_CHECK_RESULT_PREFIX_FMT "[%d] pool " DF_UUIDF " check result"
#define DLCK_WARNINGS_NUM_FMT             " (%u warning(s))"

/**
 * Target thread (worker).
 */
static void
exec_one(void *arg)
{
	struct xstream_arg *xa = arg;
	struct dlck_file   *file;
	struct dlck_print  *main_dp = &xa->ctrl->print;
	struct dlck_print   dp;
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
		rc = dlck_print_worker_init(&xa->ctrl->common.options, xa->ctrl->log_dir,
					    file->po_uuid, xa->xs->tgt_id, main_dp, &dp);
		if (rc != DER_SUCCESS) {
			/** There is no point continuing without a logfile. */
			dlck_xstream_set_rc(xa, rc);
			xa->progress = DLCK_XSTREAM_PROGRESS_END;
			break;
		}

		/** check the pool */
		rc = pool_process(xa, file, main_dp, &dp);
		/** report the result */
		if (rc == DER_SUCCESS && dp.warnings_num > 0) {
			DLCK_PRINTF(
			    main_dp,
			    DLCK_POOL_CHECK_RESULT_PREFIX_FMT DLCK_OK_INFIX DLCK_WARNINGS_NUM_FMT
			    ".\n",
			    xa->xs->tgt_id, DP_UUID(file->po_uuid), dp.warnings_num);
		} else {
			DLCK_PRINTFL_RC(main_dp, rc, DLCK_POOL_CHECK_RESULT_PREFIX_FMT,
					xa->xs->tgt_id, DP_UUID(file->po_uuid));
		}
		dlck_xstream_set_rc(xa, rc);
		dlck_uadd_no_overflow(xa->warnings_num, dp.warnings_num, &xa->warnings_num);
		/** Continue to the next pool regardless of the result. */

		/** close the logfile */
		dlck_print_worker_fini(&dp);

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

#define STOP_TGT_STR "Wait for targets to stop... "

/**
 * The main thread spawns and waits for other threads to complete their tasks.
 */
int
dlck_cmd_check(struct dlck_control *ctrl)
{
	D_ASSERT(ctrl != NULL);

	struct dlck_print  *dp                 = &ctrl->print;
	char                log_dir_template[] = "/tmp/dlck_check_XXXXXX";
	struct dlck_engine *engine             = NULL;
	struct dlck_exec    de                 = {0};
	int                *rcs;
	int                 rc;

	/** create a log directory */
	ctrl->log_dir = mkdtemp(log_dir_template);
	if (DAOS_FAIL_CHECK(DLCK_FAULT_CREATE_LOG_DIR)) { /** fault injection */
		D_ASSERT(ctrl->log_dir != NULL);
		ctrl->log_dir = NULL;
		errno         = daos_fail_value_get();
	}
	if (ctrl->log_dir == NULL) {
		rc = daos_errno2der(errno);
		DLCK_PRINTL_RC(dp, rc, "Cannot create log directory");
		return rc;
	}
	DLCK_PRINTF(dp, "Log directory: %s\n", ctrl->log_dir);

	DLCK_PRINT(dp, "Start the engine... ");
	rc = dlck_engine_start(&ctrl->engine, &engine);
	if (DAOS_FAIL_CHECK(DLCK_FAULT_ENGINE_START)) { /** fault injection */
		D_ASSERT(rc == DER_SUCCESS);
		rc = daos_errno2der(daos_fail_value_get());
	}
	DLCK_APPENDL_RC(dp, rc);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	if (d_list_empty(&ctrl->files.list)) {
		/** no files specified means all files are requested */
		DLCK_PRINT(dp, "Read the list of pools... ");
		rc = dlck_pool_list(&ctrl->files.list);
		DLCK_APPENDL_RC(dp, rc);
		if (rc != DER_SUCCESS) {
			goto err_stop_engine;
		}
	}

	DLCK_PRINT(dp, "Create pools directories... ");
	rc = dlck_pool_mkdir_all(ctrl->engine.storage_path, &ctrl->files.list, dp);
	DLCK_APPENDL_RC(dp, rc);
	if (rc != DER_SUCCESS) {
		goto err_stop_engine;
	}

	/** allocate an array of return codes for targets */
	D_ALLOC_ARRAY(rcs, ctrl->engine.targets);
	if (rcs == NULL) {
		rc = -DER_NOMEM;
		DLCK_PRINTL_RC(dp, rc, "");
		goto err_stop_engine;
	}

	DLCK_PRINT(dp, "Start targets... ");
	rc = dlck_engine_exec_all_async(engine, exec_one, dlck_engine_xstream_arg_alloc, ctrl,
					dlck_engine_xstream_arg_free, &de);
	DLCK_APPENDL_RC(dp, rc);
	if (rc != DER_SUCCESS) {
		goto err_free_rcs;
	}

	DLCK_PRINT(dp, STOP_TGT_STR "\n");
	rc = dlck_engine_join_all(engine, &de, rcs);
	DLCK_PRINTL_RC(dp, rc, STOP_TGT_STR);
	if (rc != DER_SUCCESS) {
		D_FREE(rcs);
		/** Cannot stop the engine in this case. It will probably crash. */
		return rc;
	}

	DLCK_PRINT(dp, "Stop the engine... ");
	rc = dlck_engine_stop(engine);
	DLCK_APPENDL_RC(dp, rc);

	/** Ignore an error for now to print the collected results. */
	dlck_report_results(rcs, ctrl->engine.targets, ctrl->warnings_num, dp);
	D_FREE(rcs);

	/** Return the first encountered error. */
	return rc;

err_free_rcs:
	D_FREE(rcs);
err_stop_engine:
	(void)dlck_engine_stop(engine);

	return rc;
}
