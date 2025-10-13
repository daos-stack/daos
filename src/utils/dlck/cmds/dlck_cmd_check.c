/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/mem.h>
#include <daos_srv/dlck.h>
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
		DLCK_PRINTF_ERRL(dp, "VOS file path allocation failed: " DF_RC "\n", DP_RC(xa->rc));
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

#define DLCK_POOL_CHECK_RESULT_PREFIX_FMT "[%d] pool " DF_UUIDF " check result: "

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
		xa->rc = rc;
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
		rc = dlck_print_worker_init(xa->ctrl->log_dir, file->po_uuid, xa->xs->tgt_id,
					    main_dp, &dp);
		if (rc != DER_SUCCESS) {
			/** There is no point continuing without a logfile. */
			dlck_xstream_set_rc(xa, rc);
			xa->progress = DLCK_XSTREAM_PROGRESS_END;
			break;
		}

		/** check the pool */
		rc = pool_process(xa, file, main_dp, &dp);
		/** report the result */
		if (rc != DER_SUCCESS) {
			dlck_xstream_set_rc(xa, rc);
			DLCK_PRINTF_ERRL(main_dp, DLCK_POOL_CHECK_RESULT_PREFIX_FMT DF_RC "\n",
					 xa->xs->tgt_id, DP_UUID(file->po_uuid), DP_RC(rc));
			/** Continue to the next pool regardless of the result. */
		} else {
			DLCK_PRINTF(main_dp, DLCK_POOL_CHECK_RESULT_PREFIX_FMT DLCK_OK_SUFFIX "\n",
				    xa->xs->tgt_id, DP_UUID(file->po_uuid));
		}

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
	int                 rc2;

	/** create a log directory */
	ctrl->log_dir = mkdtemp(log_dir_template);
	if (DAOS_FAIL_CHECK(DLCK_FAULT_CREATE_LOG_DIR)) {
		D_ASSERT(ctrl->log_dir != NULL);
		ctrl->log_dir = NULL;
		errno         = daos_fail_value_get();
	}
	if (ctrl->log_dir == NULL) {
		rc = daos_errno2der(errno);
		DLCK_PRINTF_ERRL(dp, "Cannot create log directory: " DF_RC "\n", DP_RC(rc));
		return rc;
	}
	DLCK_PRINTF(dp, "Log directory: %s\n", ctrl->log_dir);

	DLCK_PRINT(dp, "Start the engine... ");
	rc = dlck_engine_start(&ctrl->engine, &engine);
	if (DAOS_FAIL_CHECK(DLCK_FAULT_ENGINE_START)) {
		D_ASSERT(rc == DER_SUCCESS);
		rc = daos_errno2der(daos_fail_value_get());
	}
	if (rc != DER_SUCCESS) {
		DLCK_PRINT_RC(dp, rc);
		return rc;
	}
	DLCK_PRINT_OK(dp);

	if (d_list_empty(&ctrl->files.list)) {
		/** no files specified means all files are requested */
		DLCK_PRINT(dp, "Read the list of pools... ");
		rc = dlck_pool_list(&ctrl->files.list);
		if (rc != DER_SUCCESS) {
			DLCK_PRINT_RC(dp, rc);
			(void)dlck_engine_stop(engine);
			return rc;
		}
		DLCK_PRINT_OK(dp);
	}

	DLCK_PRINT(dp, "Create pools directories... ");
	rc = dlck_pool_mkdir_all(ctrl->engine.storage_path, &ctrl->files.list, dp);
	if (rc != DER_SUCCESS) {
		DLCK_PRINT_RC(dp, rc);
		(void)dlck_engine_stop(engine);
		return rc;
	}
	DLCK_PRINT_OK(dp);

	DLCK_PRINT(dp, "Start targets... ");
	rc = dlck_engine_exec_all_async(engine, exec_one, dlck_engine_xstream_arg_alloc, ctrl,
					dlck_engine_xstream_arg_free, &de);
	if (rc != DER_SUCCESS) {
		DLCK_PRINT_RC(dp, rc);
		(void)dlck_engine_stop(engine);
		return rc;
	}
	DLCK_PRINT_OK(dp);

	/** allocate an array of return codes for targets */
	D_ALLOC_ARRAY(rcs, ctrl->engine.targets);
	if (rcs == NULL) {
		DLCK_PRINT_ERRL(dp, "Out of memory.\n");
		(void)dlck_engine_join_all(engine, &de, NULL);
		(void)dlck_engine_stop(engine);
		return -DER_NOMEM;
	}

	DLCK_PRINT(dp, STOP_TGT_STR "\n");
	rc = dlck_engine_join_all(engine, &de, rcs);
	if (rc != DER_SUCCESS) {
		DLCK_PRINT_MSG_RC(dp, STOP_TGT_STR, rc);
		/** Cannot stop the engine in this case. It will probably crash. */
		return rc;
	}
	DLCK_PRINT_MSG_OK(dp, STOP_TGT_STR);

	DLCK_PRINT(dp, "Stop the engine... ");
	rc = dlck_engine_stop(engine);
	if (rc != DER_SUCCESS) {
		DLCK_PRINT_RC(dp, rc);
		/** Ignore this error for now in an attempt to print the collected results. */
	} else {
		DLCK_PRINT_OK(dp);
	}

	rc2 = dlck_report_results(rcs, ctrl->engine.targets, dp);
	D_FREE(rcs);
	if (rc2 != DER_SUCCESS) {
		DLCK_PRINTF_ERRL(dp, "Cannot report results: " DF_RC "\n", DP_RC(rc2));
	}

	/** Return the first encountered error. */
	return rc != DER_SUCCESS ? rc : rc2;
}
