/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_ENGINE_INTERNAL__
#define __DLCK_ENGINE_INTERNAL__

#include "dlck_engine.h"

struct dlck_exec {
	arg_alloc_fn_t  arg_alloc_fn;
	void           *arg_alloc_fn_arg;
	arg_free_fn_t   arg_free_fn;
	void           *arg_free_fn_arg;
	dlck_ult_func   exec_one;
	struct checker *ck;
	unsigned        targets;
	ABT_thread     *ults;
	void          **ult_args;
	bool            join_fail;
};

/**
 * Allocate the per-target resources of a caller-initialized batch.
 * On failure, release allocated resources.
 *
 * The caller initializes the callbacks, their arguments and target count, and zeroes the remaining
 * state.
 */
int
dlck_exec_init(struct dlck_engine *engine, struct dlck_exec *de);

/**
 * Start the batch's exec_one ULT per-target. On failure, join started ULTs; the caller must still
 * release the batch with dlck_exec_fini().
 */
int
dlck_engine_exec(struct dlck_engine *engine, struct dlck_exec *de);

/**
 * Join all started ULTs without returning an error. Failed joins set join_fail and leave their
 * thread handles intact; successfully joined threads are freed.
 */
void
dlck_exec_join_no_error(struct dlck_exec *de);

/**
 * Join the batch and report the result. A failed join sets join_fail of \p de; the caller must
 * propagate it to the engine.
 */
int
dlck_exec_join(struct dlck_exec *de);

/**
 * Release batch resources.
 *
 * Some ULTs may fail to join. In this case their related resources are left so the potentially
 * running ULTs won't crash. Please check \p de's join_fail to see if a ULT failed to join.
 */
void
dlck_exec_fini(struct dlck_exec *de);

#endif /* __DLCK_ENGINE_INTERNAL__ */
