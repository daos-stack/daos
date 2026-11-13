/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_ENGINE__
#define __DLCK_ENGINE__

#include <abt.h>

#include "dlck_args.h"

struct dlck_ult {
	ABT_thread thread;
};

struct dlck_xstream {
	ABT_xstream     xstream;
	ABT_pool        pool;

	int             tgt_id;
	struct dlck_ult nvme_poll;
	ABT_eventual    nvme_poll_done;

	int             ult_rc;
};

struct dlck_engine {
	unsigned             targets;
	struct dlck_xstream *xss;
	bool                 join_fail;
};

typedef void (*dlck_ult_func)(void *arg);

/**
 * Start an engine.
 *
 * \param[in]	args		Engine's arguments.
 * \param[out]	engine_ptr	Started engine.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_NOMEM	Out of memory.
 * \retval -DER_*	Other errors.
 */
int
dlck_engine_start(struct dlck_args_engine *args, struct dlck_engine **engine_ptr);

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

/** dlck_abt.c */

/**
 * Just create an ABT execution stream.
 *
 * \param[out]	xs	Where the created execution stream will be stored.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
int
dlck_xstream_create(struct dlck_xstream *xs);

/**
 * Free an ABT execution stream.
 *
 * \param[out]	xs	Execution stream to free.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
int
dlck_xstream_free(struct dlck_xstream *xs);

/**
 * Create an ABT thread (ULT).
 *
 * \param[in]	pool	Pool to put the created ULT in.
 * \param[in]	func	Function to start on the created ULT.
 * \param[in]	arg	Argument pointer for the function.
 * \param[out]	ult	Created ULT.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
int
dlck_ult_create(ABT_pool pool, dlck_ult_func func, void *arg, struct dlck_ult *ult);

typedef int (*arg_alloc_fn_t)(struct dlck_engine *engine, int idx, void *custom, void **arg);
typedef void (*arg_free_fn_t)(void *custom, void **arg);

/**
 * \struct dlck_exec
 *
 * Job batch. ULTs + their arguments + the free function to clean it all up.
 */
struct dlck_exec {
	/** in */
	arg_free_fn_t    arg_free_fn;
	void            *arg_free_fn_arg;
	unsigned         targets;
	/** state */
	struct dlck_ult *ults;
	void           **ult_args;
	/** out */
	bool             join_fail;
};

/**
 * \brief Allocate a job batch and the arguments for all the target ULTs.
 *
 * The \p arg_alloc_fn function is called to allocate a separate argument for each of the targets.
 * On failure, the already allocated arguments are freed using \p arg_free_fn and the batch is
 * released.
 *
 * \param[in]	engine			Engine the batch is prepared for.
 * \param[in]	arg_alloc_fn		Function to allocate an argument for a ULT.
 * \param[in]	arg_alloc_fn_arg	Custom parameter for \p arg_alloc_fn.
 * \param[in]	arg_free_fn		Function to free an argument of a ULT.
 * \param[in]	arg_free_fn_arg		Custom parameter for \p arg_free_fn.
 * \param[out]	de_ptr			Allocated job batch.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_NOMEM	Out of memory.
 * \retval -DER_*	Other errors.
 */
int
dlck_exec_init(struct dlck_engine *engine, arg_alloc_fn_t arg_alloc_fn, void *arg_alloc_fn_arg,
	       arg_free_fn_t arg_free_fn, void *arg_free_fn_arg, struct dlck_exec **de_ptr);

/**
 * \brief Run the \p exec_one function as a ULT on each of the daos_io_* execution streams.
 *
 * On failure, the already started ULTs are joined; the batch remains valid and has to be released
 * using dlck_exec_fini().
 *
 * \param[in]		engine		Engine to run the created ULTs on.
 * \param[in]		exec_one	Function to run in the ULTs.
 * \param[in,out]	de		Job batch with the ULTs' arguments.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
int
dlck_engine_exec(struct dlck_engine *engine, dlck_ult_func exec_one, struct dlck_exec *de);

/**
 * \brief Wait for all the ULTs of the batch to conclude.
 *
 * A ULT which fails to join is marked in the batch; its argument will not be freed by
 * dlck_exec_fini() and the batch has to be reported via the engine's join_fail flag.
 *
 * \param[in,out]	de	Job batch to join.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
int
dlck_exec_join(struct dlck_exec *de);

/**
 * \brief Free the ULTs' arguments and release the batch.
 *
 * The arguments of ULTs which failed to join are left out.
 *
 * \param[in]	de	Job batch to release.
 */
void
dlck_exec_fini(struct dlck_exec *de);

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
 * \param[in]	exec_one		Function to run in the ULTs.
 * \param[in]	arg_alloc_fn		Function to allocate arguments for an ULT.
 * \param[in]	arg_alloc_fn_arg	Custom parameters for \p arg_alloc_fn.
 * \param[in]	arg_free_fn		Function to free arguments.
 * \param[in]	arg_free_fn_arg		Custom parameters for \p arg_free_fn.
 * \param[in]	checker			Checker.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
int
dlck_engine_exec_all(struct dlck_engine *engine, dlck_ult_func exec_one,
		     arg_alloc_fn_t arg_alloc_fn, void *arg_alloc_fn_arg, arg_free_fn_t arg_free_fn,
		     void *arg_free_fn_arg, struct checker *ck);

/**
 * @struct xstream_arg
 *
 * Arguments passed to the main ULT on each of the execution streams.
 */
struct xstream_arg {
	/** in */
	struct dlck_control  *ctrl;   /** Control state. */
	struct dlck_engine   *engine; /** Engine itself. */
	struct dlck_xstream  *xs;     /** The execution stream the ULT is run in. */
	/** out */
	struct checker_status status;
};

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

/**
 * Allocate arguments for a ULT.
 *
 * \param[in]	engine		Engine the ULT is about to be run in.
 * \param[in]	idx		ULT ID.
 * \param[in]	ctrl_ptr	Control state to be passed to the ULT.
 * \param[out]	output_arg	Allocated argument for the ULT.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_NOMEM	Out of memory.
 */
int
dlck_engine_xstream_arg_alloc(struct dlck_engine *engine, int idx, void *ctrl_ptr,
			      void **output_arg);

/**
 * Free arguments of a ULT.
 *
 * \param[out]		ctrl_ptr	Control state to collect stats in.
 * \param[in,out]	arg		ULT arguments to process and free.
 */
void
dlck_engine_xstream_arg_free(void *ctrl_ptr, void **arg);

#endif /** __DLCK_ENGINE__ */
