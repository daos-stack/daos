/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_ENGINE__
#define __DLCK_ENGINE__

#include <abt.h>

#include <daos_srv/dlck.h>

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
 * \struct dlck_exec
 *
 * Job batch. ULTs + their arguments + the free function to clean it all up.
 */
struct dlck_exec {
	struct dlck_ult *ults;
	void           **ult_args;
	void            *custom;
	arg_free_fn_t    arg_free_fn;
};

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
dlck_engine_exec_all_sync(struct dlck_engine *engine, dlck_ult_func exec_one,
			  arg_alloc_fn_t arg_alloc_fn, void *input_arg, arg_free_fn_t arg_free_fn);

/**
 * \brief Run the \p exec_one function as a set of ULTs on all the daos_io_* execution streams
 * of the \p engine.
 *
 * The function returns immediately and does not wait for the ULTs to conclude.
 *
 * The \p arg_alloc_func and \p arg_free_fn are called to allocate and free arguments respectively.
 * Each of ULTs has a separate arguments allocated for its own use.
 *
 * All the allocated resources and information required to stop the created ULTs are stored in \p
 * de.
 *
 * \note In case of an error, all ULTs are stopped immediately and resources are freed.
 *
 * \param[in]	engine		Engine to run the created ULTs.
 * \param[in]	exec_one	Function to run in the ULTs.
 * \param[in]	arg_alloc_fn	Function to allocate arguments for an ULT.
 * \param[in]	custom		Custom parameters for \p arg_alloc_fn and \p arg_free_fn function.
 * \param[in]	arg_free_fn	Function to free arguments.
 * \param[out]	de		Execution describing object.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
int
dlck_engine_exec_all_async(struct dlck_engine *engine, dlck_ult_func exec_one,
			   arg_alloc_fn_t arg_alloc_fn, void *input_arg, arg_free_fn_t arg_free_fn,
			   struct dlck_exec *de);

/**
 * \brief Wait for the execution \p de to conclude.
 *
 * \note All the allocated resources are freed and ULTs stopped regardless of the result.
 *
 * \param[in]		engine	Engine to run the created ULTs.
 * \param[in,out]	de	Execution describing object.
 * \param[out]		rcs	Targets' return codes.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
int
dlck_engine_join_all(struct dlck_engine *engine, struct dlck_exec *de, int *rcs);

/**
 * \brief Run the \p exec function as a ULT on an execution stream of the \p engine as indicated by
 * \p idx.
 *
 * The function does not return as along as the ULT concludes.
 *
 * The \p arg_alloc_func and \p arg_free_fn are called to allocate and free arguments respectively.
 *
 * \param[in]	engine		Engine to run the created ULT.
 * \param[in]	idx		ID of an execution stream to use.
 * \param[in]	exec		Function to run in the ULT.
 * \param[in]	arg_alloc_fn	Function to allocate arguments for an ULT.
 * \param[in]	custom		Custom parameters for \p arg_alloc_fn and \p arg_free_fn function.
 * \param[in]	arg_free_fn	Function to free arguments.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
int
dlck_engine_exec(struct dlck_engine *engine, int idx, dlck_ult_func exec,
		 arg_alloc_fn_t arg_alloc_fn, void *custom, arg_free_fn_t arg_free_fn);

/**
 * Open a pool but lock the \p mtx mutex first and unlock it after. Thread-safe.
 *
 * \param[in]	mtx		Mutex.
 * \param[in]	storage_path	Storage path.
 * \param[in]	po_uuid		Pool UUID.
 * \param[in]	tgt_id		Target ID.
 * \param[out]	poh		Pool handle.
 *
 * \retval DER_SUCCESS		Success.
 * \retval -DER_NOMEM		Out of memory.
 * \retval -DER_NO_PERM		Permission problem. Please see open(3) and fallocate(2).
 * \retval -DER_EXIST		The file already exists. Please see open(3).
 * \retval -DER_NONEXIST	The file does not exist. Please see open(3).
 * \retval -DER_NOSPACE		There is not enough space left on the device.
 * \retval -DER_*		Possibly other errors.
 */
int
dlck_pool_open_safe(ABT_mutex mtx, const char *storage_path, uuid_t po_uuid, int tgt_id,
		    daos_handle_t *poh);

/**
 * Close a pool but lock the \p mtx mutex first and unlock it after. Thread-safe.
 *
 * \param[in]	mtx		Mutex.
 * \param[in]	poh		Pool handle.
 *
 * \retval DER_SUCCESS		Success.
 * \retval -DER_INVAL		Issues with \p mtx.
 */
int
dlck_pool_close_safe(ABT_mutex mtx, daos_handle_t poh);

#define DLCK_XSTREAM_PROGRESS_END UINT_MAX

/**
 * @struct xstream_arg
 *
 * Arguments passed to the main ULT on each of the execution streams.
 */
struct xstream_arg {
	/** in */
	struct dlck_control *ctrl;   /** Control state. */
	struct dlck_engine  *engine; /** Engine itself. */
	struct dlck_xstream *xs;     /** The execution stream the ULT is run in. */
	/** out */
	unsigned             progress;
	ABT_mutex            progress_mutex;
	int                  rc; /** return code */
};

static inline void
dlck_xstream_set_rc(struct xstream_arg *xa, int rc)
{
	if (rc == DER_SUCCESS) {
		return;
	}

	/** do not overwrite the first error found */
	if (xa->rc == DER_SUCCESS) {
		xa->rc = rc;
	}
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
 *
 * \return The return code for the ULT.
 */
int
dlck_engine_xstream_arg_free(void *ctrl_ptr, void **arg);

/**
 * Mark the end of progress for the given execution stream \p xa.
 *
 * \param[in,out]	xa	Execution stream to mark.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_INVAL	Invalid mutex.
 */
static inline int
dlck_xstream_progress_end(struct xstream_arg *xa, struct dlck_print *dp)
{
	int rc;

	rc = ABT_mutex_lock(xa->progress_mutex);
	if (rc == ABT_SUCCESS) {
		xa->progress = DLCK_XSTREAM_PROGRESS_END;
		rc           = ABT_mutex_unlock(xa->progress_mutex);
		if (rc == ABT_SUCCESS) {
			return DER_SUCCESS;
		}
	}
	rc = dss_abterr2der(rc);
	DLCK_PRINTF_ERRL(&xa->ctrl->print, "[%d] Cannot advance progress: " DF_RC "\n",
			 xa->xs->tgt_id, DP_RC(xa->rc));
	return rc;
}

/**
 * Increment the progress by one for the given execution stream \p xa.
 *
 * \param[in,out]	xa	Execution stream to mark.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_INVAL	Invalid mutex.
 */
static inline int
dlck_xstream_progress_inc(struct xstream_arg *xa, struct dlck_print *dp)
{
	int rc;

	rc = ABT_mutex_lock(xa->progress_mutex);
	if (rc == ABT_SUCCESS) {
		xa->progress += 1;
		rc = ABT_mutex_unlock(xa->progress_mutex);
		if (rc == ABT_SUCCESS) {
			return DER_SUCCESS;
		}
	}
	rc = dss_abterr2der(rc);
	DLCK_PRINTF_ERRL(&xa->ctrl->print, "[%d] Cannot advance progress: " DF_RC "\n",
			 xa->xs->tgt_id, DP_RC(xa->rc));
	return rc;
}

/**
 * Read the progress of the given execution stream \p xa.
 *
 * \param[in]	xa		Execution stream.
 * \param[out]	progress	Progress read from \p xa.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_INVAL	Invalid mutex.
 */
static inline int
dlck_xstream_progress_get(struct xstream_arg *xa, unsigned *progress)
{
	int rc;

	rc = ABT_mutex_lock(xa->progress_mutex);
	if (rc != ABT_SUCCESS) {
		return dss_abterr2der(rc);
	}
	*progress = xa->progress;
	rc        = ABT_mutex_unlock(xa->progress_mutex);
	if (rc != ABT_SUCCESS) {
		return dss_abterr2der(rc);
	}
	return DER_SUCCESS;
}

#endif /** __DLCK_ENGINE__ */
