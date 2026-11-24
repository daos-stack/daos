/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/mem.h>
#include <engine/srv_internal.h>

#include "dlck_engine_internal.h"

void
dlck_exec_join_no_error(struct dlck_exec *de)
{
	int rc;

	for (int i = 0; i < de->targets; ++i) {
		if (de->ults[i].thread == ABT_THREAD_NULL) {
			continue;
		}

		rc = ABT_thread_join(de->ults[i].thread);
		if (rc != ABT_SUCCESS) {
			de->join_fail = true;
			continue;
		}
		(void)ABT_thread_free(&de->ults[i].thread);
		de->ults[i].thread = ABT_THREAD_NULL;
	}
}

int
dlck_exec_init(struct dlck_engine *engine, struct dlck_exec *de)
{
	int rc;

	if (DAOS_FAIL_CHECK(DLCK_FAULT_ENGINE_EXEC)) { /** fault injection */
		rc = daos_errno2der(daos_fail_value_get());
		goto fail_report;
	}

	D_ALLOC_ARRAY(de->ults, de->targets);
	if (de->ults == NULL) {
		rc = -DER_NOMEM;
		goto fail_report;
	}

	for (int i = 0; i < de->targets; ++i) {
		de->ults[i].thread = ABT_THREAD_NULL;
	}

	D_ALLOC_ARRAY(de->ult_args, de->targets);
	if (de->ult_args == NULL) {
		rc = -DER_NOMEM;
		goto fail_ults_free;
	}

	for (int i = 0; i < de->targets; ++i) {
		rc = de->arg_alloc_fn(engine, i, de->arg_alloc_fn_arg, &de->ult_args[i]);
		if (rc != DER_SUCCESS) {
			goto fail_args_free;
		}
	}

	return DER_SUCCESS;

fail_args_free:
	for (int i = 0; i < de->targets; ++i) {
		de->arg_free_fn(de->arg_free_fn_arg, &de->ult_args[i]);
	}
	D_FREE(de->ult_args);
fail_ults_free:
	D_FREE(de->ults);
fail_report:
	return rc;
}

int
dlck_engine_exec(struct dlck_engine *engine, struct dlck_exec *de)
{
	int rc = DER_SUCCESS;

	for (int i = 0; i < de->targets; ++i) {
		rc = dlck_ult_create(engine->xss[i].pool, de->exec_one, de->ult_args[i],
				     &de->ults[i]);
		if (rc != DER_SUCCESS) {
			dlck_exec_join_no_error(de);
			break;
		}
	}

	return rc;
}

int
dlck_exec_join(struct dlck_exec *de)
{
	int rc = DER_SUCCESS;
	int rc_abt;

	if (DAOS_FAIL_CHECK(DLCK_FAULT_ENGINE_JOIN)) { /** fault injection */
		de->join_fail = true;
		dlck_exec_join_no_error(de);
		return daos_errno2der(daos_fail_value_get());
	}

	for (int i = 0; i < de->targets; ++i) {
		rc_abt = ABT_thread_join(de->ults[i].thread);
		if (rc_abt != ABT_SUCCESS) {
			de->join_fail = true;
			if (rc == DER_SUCCESS) {
				rc = dss_abterr2der(rc_abt);
			}
			continue;
		}

		rc_abt = ABT_thread_free(&de->ults[i].thread);
		if (rc_abt != ABT_SUCCESS && rc == DER_SUCCESS) {
			rc = dss_abterr2der(rc_abt);
		}
		de->ults[i].thread = ABT_THREAD_NULL;
	}

	return rc;
}

void
dlck_exec_fini(struct dlck_exec *de)
{
	for (int i = 0; i < de->targets; ++i) {
		if (de->ults[i].thread == ABT_THREAD_NULL) {
			de->arg_free_fn(de->arg_free_fn_arg, &de->ult_args[i]);
		}
	}

	D_FREE(de->ult_args);
	D_FREE(de->ults);
}
