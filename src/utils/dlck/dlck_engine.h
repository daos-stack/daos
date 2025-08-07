/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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

	int             rc_init;
};

struct dlck_engine {
	unsigned             targets;
	struct dlck_xstream *xss;
	ABT_mutex            open_mtx;
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
 * \retval -DER_*	Errors.
 */
int
dlck_engine_stop(struct dlck_engine *engine);

/**
 * Initialize an execution stream.
 *
 * \param[in,out]	xs	Execution stream to initialize.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_INVAL	Thread name generation failed.
 * \retval -DER_NOMEM	Out of memory.
 * \retval -DER_*	Other errors.
 */
int
dlck_engine_xstream_init(struct dlck_xstream *xs);

/**
 * Finalize an execution stream.
 *
 * \param[in,out]	xs	Execution stream to finalize.
 *
 * \retval DER_SUCCESS	Success. Supposedly it can't fail.
 */
int
dlck_engine_xstream_fini(struct dlck_xstream *xs);

/** dlck_abt.c */

/**
 * Initialize ABT as it is about to be used by the \p engine.
 *
 * \param[out]	engine	Engine for which ABT is initialized for.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
int
dlck_abt_init(struct dlck_engine *engine);

/**
 * Finalize ABT for the \p engine.
 *
 * \param[in,out]	engine	Engine for which ABT is finalized for.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
int
dlck_abt_fini(struct dlck_engine *engine);

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
typedef int (*arg_free_fn_t)(void *custom, void **arg);

/**
 * \brief Run the \p exec_one function as a set of ULTs on all the daos_io_* execution streams
 * of the \p engine.
 *
 * The function does not return as along as all ULTs conclude.
 *
 * The \p arg_alloc_func and \p arg_free_fn are called to allocate and free arguments respectively.
 * Each of ULTs has a separate arguments allocated for its own use.
 *
 * \param[in]	engine		Engine to run the created ULTs.
 * \param[in]	exec_one	Function to run in the ULTs.
 * \param[in]	arg_alloc_fn	Function to allocate arguments for an ULT.
 * \param[in]	custom		Custom parameters for \p arg_alloc_fn and \p arg_free_fn function.
 * \param[in]	arg_free_fn	Function to free arguments.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
int
dlck_engine_exec_all(struct dlck_engine *engine, dlck_ult_func exec_one,
		     arg_alloc_fn_t arg_alloc_fn, void *input_arg, arg_free_fn_t arg_free_fn);

#endif /** __DLCK_ENGINE__ */
