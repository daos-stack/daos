/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_ENGINE__
#define __DLCK_ENGINE__

#include <stdbool.h>

#include <abt.h>

#include "dlck_abt.h"
#include "dlck_args.h"
#include "dlck_xstream.h"

struct dlck_engine {
	unsigned             targets;
	struct dlck_xstream *xss;
	bool                 join_fail;
};

/** Initialize the non-xstream resources of an engine. */
int
dlck_engine_init(struct dlck_args_engine *args, struct dlck_engine **engine_ptr);

/**
 * Create and initialize daos_sys_0 and daos_io_* execution streams (XS).
 *
 * \param[in,out]	engine	Engine to start its XSes.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
int
dlck_engine_start(struct dlck_engine *engine);

/**
 * Stop an engine.
 *
 * \param[in]	engine	Engine to stop.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_BUSY	Joining ULTs failed. Unrecoverable.
 * \retval -DER_*	Errors.
 */
int
dlck_engine_stop(struct dlck_engine *engine);

/** Free the non-xstream resources of an engine after it has stopped. */
int
dlck_engine_fini(struct dlck_engine *engine);

typedef int (*arg_alloc_fn_t)(struct dlck_engine *engine, int idx, void *custom, void **arg);
typedef void (*arg_free_fn_t)(void *custom, void **arg);

/**
 * \brief Run the \p exec_one function as a set of ULTs on all the daos_io_* execution streams
 * of the \p engine.
 *
 * The function does not return as along as all ULTs conclude.
 *
 * The \p arg_alloc_func and \p arg_free_fn are called to allocate and free arguments respectively.
 * Each of ULTs has a separate arguments allocated for its own use.
 *
 * \param[in]	engine			Engine to run the created ULTs.
 * \param[in]	arg_alloc_fn		Function to allocate arguments for an ULT.
 * \param[in]	arg_alloc_fn_arg	Custom parameters for \p arg_alloc_fn.
 * \param[in]	arg_free_fn		Function to free arguments.
 * \param[in]	arg_free_fn_arg		Custom parameters for \p arg_free_fn.
 * \param[in]	exec_one		Function to run in the ULTs.
 * \param[in]	ck			Checker.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
int
dlck_engine_exec_all(struct dlck_engine *engine, arg_alloc_fn_t arg_alloc_fn,
		     void *arg_alloc_fn_arg, arg_free_fn_t arg_free_fn, void *arg_free_fn_arg,
		     dlck_ult_func exec_one, struct checker *ck);

static inline unsigned
dlck_uadd_no_overflow(unsigned a, unsigned b)
{
	unsigned result;

	/** safeguard against integer overflow */
	if (__builtin_uadd_overflow(a, b, &result)) {
		result = UINT_MAX;
	}

	return result;
}

static inline void
dlck_status_set_rc(struct checker_status *status, int rc)
{
	/** do not overwrite the first error found */
	if (status->rc == DER_SUCCESS) {
		status->rc = rc;
	}
}

static inline void
dlck_status_set(struct checker_status *status, struct checker_status *cks)
{
	/** Update the \p status with the information from \p cks. */
	status->warnings_num = dlck_uadd_no_overflow(status->warnings_num, cks->warnings_num);
	status->errors_num   = dlck_uadd_no_overflow(status->errors_num, cks->errors_num);
	dlck_status_set_rc(status, cks->rc);

	/** Clear the information in \p cks. */
	cks->warnings_num = 0;
	cks->errors_num   = 0;
	cks->rc           = DER_SUCCESS;
}

#endif /** __DLCK_ENGINE__ */
