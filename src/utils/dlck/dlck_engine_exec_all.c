/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/checker.h>

#include "dlck_engine_internal.h"

#define STOP_TGT_STR "Wait for targets to stop"

int
dlck_engine_exec_all(struct dlck_engine *engine, arg_alloc_fn_t arg_alloc_fn,
		     void *arg_alloc_fn_arg, arg_free_fn_t arg_free_fn, void *arg_free_fn_arg,
		     dlck_ult_func exec_one, struct checker *ck)
{
	struct dlck_exec de = {
	    .arg_alloc_fn     = arg_alloc_fn,
	    .arg_alloc_fn_arg = arg_alloc_fn_arg,
	    .arg_free_fn      = arg_free_fn,
	    .arg_free_fn_arg  = arg_free_fn_arg,
	    .exec_one         = exec_one,
	    .ck               = ck,
	    .targets          = engine->targets,
	};
	int rc;

	CK_PRINTF(ck, "Start targets... ");
	rc = dlck_exec_init(engine, &de);
	if (rc != DER_SUCCESS) {
		CK_APPENDL_RC(ck, rc);
		return rc;
	}

	rc = dlck_exec_start(engine, &de);
	CK_APPENDL_RC(ck, rc);

	if (rc == DER_SUCCESS) {
		CK_PRINTF(ck, STOP_TGT_STR "...\n");
		rc = dlck_exec_join(&de);
		CK_PRINTFL_RC(ck, rc, STOP_TGT_STR);
	}
	engine->join_fail = engine->join_fail || de.join_fail;
	dlck_exec_fini(&de);

	return rc;
}
