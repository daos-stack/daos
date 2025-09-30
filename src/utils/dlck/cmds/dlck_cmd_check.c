/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/mem.h>
#include <daos_srv/vos.h>
#include <daos_srv/dlck.h>
#include <daos_srv/mgmt_tgt_common.h>

#include "../dlck_args.h"
#include "../dlck_bitmap.h"
#include "../dlck_engine.h"
#include "../dlck_pool.h"

static int
exec_one(struct xstream_arg *xa, int idx)
{
	struct dlck_file *file;
	char             *path;
	daos_handle_t     poh;
	int               rc;

	xa->xs->tgt_id = idx; /** pretend to be a target of the requested ID */

	rc = dlck_engine_xstream_init(xa->xs);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	d_list_for_each_entry(file, &xa->ctrl->files.list, link) {
		/** do not process the given file if the target is excluded */
		if (dlck_bitmap_isclr32(file->targets_bitmap, idx)) {
			continue;
		}

		rc = ds_mgmt_file(xa->ctrl->engine.storage_path, file->po_uuid, VOS_FILE, &idx,
				  &path);
		if (rc != DER_SUCCESS) {
			break;
		}

		DLCK_PRINT(&xa->ctrl->print, "\n");
		rc = vos_pool_open_metrics(path, file->po_uuid, DLCK_POOL_OPEN_FLAGS, NULL,
					   &xa->ctrl->print, &poh);
		if (rc == DER_SUCCESS) {
			(void)vos_pool_close(poh);
		}
		D_FREE(path);
	}

	if (rc != DER_SUCCESS) {
		(void)dlck_engine_xstream_fini(xa->xs);
		return rc;
	}

	return dlck_engine_xstream_fini(xa->xs);
}

static void
exec(void *arg)
{
	struct xstream_arg *xa = arg;
	struct dlck_file   *file;
	int                 file_num_per_idx;
	int                 rc;

	for (int idx = 0; idx < xa->engine->targets; ++idx) {
		/** count how many files mentions this particular target */
		file_num_per_idx = 0;
		d_list_for_each_entry(file, &xa->ctrl->files.list, link) {
			if (dlck_bitmap_isset32(file->targets_bitmap, idx)) {
				++file_num_per_idx;
			}
		}

		if (file_num_per_idx == 0) {
			/** no files to process */
			continue;
		}

		rc = exec_one(xa, idx);
		if (rc != 0) {
			xa->rc = rc;
			break;
		}
	}
}

int
dlck_cmd_check(struct dlck_control *ctrl)
{
	struct dlck_engine *engine = NULL;
	/**
	 * This command processes each of the requested targets one by one. The execution stream
	 * used to run the ULT is irrelevant and selected arbitrarily.
	 */
	const int           idx = 0;
	int                 rc;

	if (ctrl == NULL) {
		return -DER_INVAL;
	}

	rc = dlck_pool_mkdir_all(ctrl->engine.storage_path, &ctrl->files.list);
	if (rc != 0) {
		return rc;
	}

	rc = dlck_engine_start(&ctrl->engine, &engine);
	if (rc != 0) {
		return rc;
	}

	rc = dlck_engine_exec(engine, idx, exec, dlck_engine_xstream_arg_alloc, ctrl,
			      dlck_engine_xstream_arg_free);
	if (rc != 0) {
		(void)dlck_engine_stop(engine);
		return rc;
	}

	return dlck_engine_stop(engine);
}
