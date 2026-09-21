/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdlib.h>
#include <stdio.h>
#include <daos/mem.h>
#include <daos/btree_class.h>
#include <daos_srv/vos.h>
#include <daos_version.h>
#include <engine/srv_internal.h>

#include "dlck_args.h"
#include "dlck_engine.h"
#include "dlck_pool.h"

int
			     dss_register_dbtree_classes(void);

extern struct dss_module     vos_srv_module;
extern struct dss_module_key vos_module_key;

/**
 * Allocate an engine.
 *
 * \param[in]	targets		Number of targets.
 * \param[out]	engine_ptr	Allocated engine.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_NOMEM	Out of memory.
 */
static int
dlck_engine_alloc(unsigned targets, struct dlck_engine **engine_ptr)
{
	struct dlck_engine *engine;

	D_ALLOC_PTR(engine);
	if (engine == NULL) {
		return -DER_NOMEM;
	}

	/** each of the targets will get its own xstream + 1 for daos_sys */
	D_ALLOC_ARRAY(engine->xss, targets + 1);
	if (engine->xss == NULL) {
		D_FREE(engine);
		return -DER_NOMEM;
	}

	engine->targets = targets;

	*engine_ptr = engine;

	return DER_SUCCESS;
}

/**
 * Free an engine.
 *
 * \param[in]	engine	An engine to free.
 */
static void
dlck_engine_free(struct dlck_engine *engine)
{
	D_FREE(engine->xss);
	D_FREE(engine);
}

/**
 * Poll for NVMe operations.
 *
 * \param[in]	arg	ABT_eventual too wait for.
 */
static void
nvme_polling(void *arg)
{
	struct dlck_xstream    *xs = arg;
	ABT_bool                is_ready;
	struct dss_module_info *dmi;
	int                     rc;

	dmi = dss_get_module_info();
	D_ASSERT(dmi != NULL);

	do {
		(void)bio_nvme_poll(dmi->dmi_nvme_ctxt);
		ABT_thread_yield();

		rc = ABT_eventual_test(xs->nvme_poll_done, NULL, &is_ready);
		if (rc != 0) {
			return;
		}
	} while (is_ready == ABT_FALSE);
}

static inline bool
dlck_engine_xstream_is_sys(int tgt_id)
{
	return (tgt_id < 0);
}

/**
 * This function ought to strictly follow it counterpart in daos_engine (dss_xstream_has_nvme).
 */
static inline bool
dlck_engine_xstream_has_nvme(int tgt_id)
{
	/**
	 * Since there are no helper execution streams (XS) right now. All non-sys XSes are main
	 * XSes as defined for daos_engine.
	 */
	if (!dlck_engine_xstream_is_sys(tgt_id)) {
		return true;
	}

	/** DLCK employs only one sys XS and this is the one which talks to NVMe as necessary. */
	if (bio_nvme_configured(SMD_DEV_TYPE_META)) {
		return true;
	}

	return false;
}

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
static int
dlck_engine_xstream_init(struct dlck_xstream *xs)
{
	int                     tag;
	int                     tgt_id = xs->tgt_id;
	int                     xs_id;
	char                    name[DSS_XS_NAME_LEN];
	void                   *tls;
	struct dss_module_info *dmi;
	int                     rc;

	if (dlck_engine_xstream_is_sys(tgt_id)) {
		tag   = DAOS_SERVER_TAG - DAOS_TGT_TAG;
		xs_id = 0;

		rc = snprintf(name, DSS_XS_NAME_LEN, DSS_SYS_XS_NAME_FMT, 0);
	} else {
		tag   = DAOS_SERVER_TAG;
		xs_id = DSS_MAIN_XS_ID_WITH_HELPER_POOL(tgt_id, DSS_SYS_XS_NR_DEFAULT);

		rc = snprintf(name, DSS_XS_NAME_LEN, DSS_IO_XS_NAME_FMT, tgt_id);
	}

	/**
	 * >= DSS_XS_NAME_LEN	the output was truncated
	 * < 0			other error
	 */
	if (rc < 0 || rc >= DSS_XS_NAME_LEN) {
		return -DER_INVAL;
	}

	(void)pthread_setname_np(pthread_self(), name);

	tls = dss_tls_init(tag, xs_id, tgt_id);
	if (tls == NULL) {
		/** Note:  dss_tls_init() returns NULL also on other issues */
		return -DER_NOMEM;
	}

	if (dlck_engine_xstream_has_nvme(tgt_id)) {
		dmi = dss_get_module_info();
		D_ASSERT(dmi != NULL);

		rc = bio_xsctxt_alloc(&dmi->dmi_nvme_ctxt, tgt_id, false);
		if (rc != DER_SUCCESS) {
			goto fail_tls_fini;
		}

		rc = ABT_eventual_create(0, &xs->nvme_poll_done);
		if (rc != ABT_SUCCESS) {
			rc = dss_abterr2der(rc);
			goto fail_xsctxt_free;
		}

		rc = dlck_ult_create(xs->pool, nvme_polling, xs, &xs->nvme_poll);
		if (rc != DER_SUCCESS) {
			goto fail_eventual_free;
		}
	}

	return DER_SUCCESS;

fail_eventual_free:
	ABT_eventual_free(&xs->nvme_poll_done);
fail_xsctxt_free:
	bio_xsctxt_free(dmi->dmi_nvme_ctxt);
fail_tls_fini:
	dss_tls_fini(tls);

	return rc;
}

static void
dlck_engine_xstream_init_ult(void *arg)
{
	struct dlck_xstream *xs = arg;

	xs->ult_rc = dlck_engine_xstream_init(xs);
}

/**
 * Finalize an execution stream.
 *
 * \param[in,out]	xs	Execution stream to finalize.
 *
 * \retval DER_SUCCESS	Success. Supposedly it can't fail.
 */
static int
dlck_engine_xstream_fini(struct dlck_xstream *xs)
{
	struct dss_module_info *dmi;
	void                   *tls = dss_tls_get();
	int                     rc  = DER_SUCCESS;

	if (tls == NULL) {
		/** Nothing to do here. */
		return DER_SUCCESS;
	}

	if (dlck_engine_xstream_has_nvme(xs->tgt_id)) {
		rc = ABT_eventual_set(xs->nvme_poll_done, NULL, 0);
		rc = dss_abterr2der(rc);
		if (rc != DER_SUCCESS) {
			goto fail;
		}

		rc = ABT_thread_join(xs->nvme_poll.thread);
		rc = dss_abterr2der(rc);
		if (rc != DER_SUCCESS) {
			goto fail;
		}

		rc = ABT_thread_free(&xs->nvme_poll.thread);
		rc = dss_abterr2der(rc);
		if (rc != DER_SUCCESS) {
			/**
			 * After the NVMe polling thread joined we can safely free TLS irrespective
			 * of the error occurred while freeing the thread.
			 */
		}

		dmi = dss_get_module_info();
		D_ASSERT(dmi != NULL);
		bio_xsctxt_free(dmi->dmi_nvme_ctxt);
	}

	dss_tls_fini(tls);

fail:
	/**
	 * In case of a fail we can't join/free the NVMe polling thread nor free TLS which may
	 * result in a SIGSEGV. The best we can do is to leave the resources as they are and pass
	 * error the caller.
	 */

	return rc;
}

static void
dlck_engine_xstream_fini_ult(void *arg)
{
	struct dlck_xstream *xs = arg;

	xs->ult_rc = dlck_engine_xstream_fini(xs);
}

static void
xstream_stop_all_no_error(struct dlck_engine *engine)
{
	struct dlck_xstream *xs;
	struct dlck_ult      fini_ult;
	int                  rc;
	int                  rc_abt;

	for (int i = 0; i <= engine->targets; ++i) {
		xs = &engine->xss[i];

		rc = dlck_ult_create(xs->pool, dlck_engine_xstream_fini_ult, xs, &fini_ult);
		if (rc != DER_SUCCESS) {
			/** Cannot free a thread possibly with running NVMe polling ULT. */
			continue;
		}

		/** ABT_thread_free() waits for the ULT to join internally */
		rc_abt = ABT_thread_free(&fini_ult.thread);
		D_ASSERT(rc_abt == ABT_SUCCESS);

		(void)dlck_xstream_free(&engine->xss[i]);
	}
}

static int
xstream_init(struct dlck_xstream *xs)
{
	struct dlck_ult init_ult;
	int             rc;
	int             rc_abt;

	rc = dlck_ult_create(xs->pool, dlck_engine_xstream_init_ult, xs, &init_ult);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	/** ABT_thread_free() waits for the ULT to join internally */
	rc_abt = ABT_thread_free(&init_ult.thread);
	/**
	 * There are two possible issues here:
	 * 1. Something is wrong with the provided argument. This is an internal error, so an
	 * assertion is used here.
	 * 2. The ULT has not joined. In this case, ABT_thread_free() will never return anyway.
	 */
	D_ASSERT(rc_abt == ABT_SUCCESS);

	if (xs->ult_rc != DER_SUCCESS) {
		D_ERROR("[%d] Initialization of XS failed\n", xs->tgt_id);
	} else {
		D_EMIT("[%d] Initialization of XS succeeded\n", xs->tgt_id);
	}

	return xs->ult_rc;
}

/**
 * Create and initialize daos_sys_0 and daos_io_* execution streams (XS).
 *
 * \param[in,out]	engine	Engine to start its XSes.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
static int
xstream_start_all(struct dlck_engine *engine)
{
	const int            daos_sys_idx = engine->targets; /** the last one is daos_sys_0 */
	struct dlck_xstream *xs;
	int                  rc;

	/** create all daos_io_* and daos_sys_0 XSes */
	for (int i = 0; i <= engine->targets; ++i) {
		xs         = &engine->xss[i];
		xs->tgt_id = (i == daos_sys_idx ? -1 : i);
		rc         = dlck_xstream_create(xs);
		if (rc != DER_SUCCESS) {
			goto xstream_stop_all;
		}
	}

	/** initialize the daos_sys_0 XS */
	xs = &engine->xss[daos_sys_idx];
	rc = xstream_init(xs);
	if (rc != DER_SUCCESS) {
		goto xstream_stop_all;
	}

	/**
	 * The daos_sys_0 XS initialization succeeded. It may have spawned a NVMe polling ULT.
	 */

	/** initialize all daos_io_* XSes */
	for (int i = 0; i < engine->targets; ++i) {
		rc = xstream_init(&engine->xss[i]);
		if (rc != DER_SUCCESS) {
			goto xstream_stop_all;
		}
	}

	return DER_SUCCESS;

xstream_stop_all:
	xstream_stop_all_no_error(engine);

	return rc;
}

#define ULT_FINI_FAIL_FMT "[%d] ULT finalization failed - cannot safely free the XS: " DF_RC "\n"

/**
 * Stop and free daos_sys_0 and all the daos_io_* execution streams belonging to the provided
 * engine.
 *
 * Note: All the XSes have to be idle before calling this function. No other ULTs present except for
 * the NMVe polling ULT in their pools.
 *
 * \param[in,out]	engine	Engine to stop the xstream of.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
static int
xstream_stop_all(struct dlck_engine *engine)
{
	struct dlck_xstream *xs;
	struct dlck_ult      fini_ult;
	ABT_bool             is_empty;
	int                  rc = DER_SUCCESS;
	int                  rc_abt;

	/** Note: daos_sys_0 XS is the last and it has to be stopped as the last one. */
	for (int i = 0; i <= engine->targets; ++i) {
		xs = &engine->xss[i];

		/** Stop and release all resources hold by the ULT. */

		rc = dlck_ult_create(xs->pool, dlck_engine_xstream_fini_ult, xs, &fini_ult);
		if (rc != DER_SUCCESS) {
			D_ERROR(ULT_FINI_FAIL_FMT, xs->tgt_id, DP_RC(rc));
			goto xstream_free_all;
		}

		/** ABT_thread_free() waits for the ULT to join internally */
		rc_abt = ABT_thread_free(&fini_ult.thread);
		/**
		 * There are two possible issues here:
		 * 1. Something is wrong with the provided argument. This is an internal error that
		 * should never occur, so an assertion is used here.
		 * 2. The ULT has not joined. In this case, ABT_thread_free() will never return
		 * anyway.
		 */
		D_ASSERT(rc_abt == ABT_SUCCESS);

		if (xs->ult_rc != DER_SUCCESS) {
			rc = xs->ult_rc;
			D_ERROR(ULT_FINI_FAIL_FMT, xs->tgt_id, DP_RC(rc));
			goto xstream_free_all;
		}

		/** make sure the XS is idle */
		rc_abt = ABT_pool_is_empty(xs->pool, &is_empty);
		/** Can fail only because of an internal error; hence, an assertion is used. */
		D_ASSERT(rc_abt == ABT_SUCCESS);
		if (is_empty != ABT_TRUE) {
			D_ERROR("[%d] cannot free XS - it is busy\n", xs->tgt_id);
			rc = -DER_BUSY;
			goto xstream_free_all;
		}

		rc = dlck_xstream_free(xs);
		if (rc != DER_SUCCESS) {
			D_ERROR("[%d] XS free failed: " DF_RC "\n", xs->tgt_id, DP_RC(rc));
			goto xstream_free_all;
		}
	}

	return DER_SUCCESS;

xstream_free_all:
	xstream_stop_all_no_error(engine);

	return rc;
}

int
dlck_engine_start(struct dlck_args_engine *args, struct dlck_engine **engine_ptr)
{
	struct dlck_engine *engine;
	const bool          bypass_health_chk = false;
	int                 tag               = DAOS_SERVER_TAG - DAOS_TGT_TAG;
	int                 rc;

	if (DAOS_FAIL_CHECK(DLCK_FAULT_ENGINE_START)) { /** fault injection */
		return daos_errno2der(daos_fail_value_get());
	}

	rc = dlck_engine_alloc(args->targets, &engine);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	rc = dss_register_dbtree_classes();
	if (rc != DER_SUCCESS) {
		goto fail_engine_free;
	}

	rc = bio_nvme_init(args->nvme_conf, args->numa_node, args->max_dma_buf_size,
			   args->nvme_hugepage_size, args->targets, bypass_health_chk);
	if (rc != DER_SUCCESS) {
		goto fail_engine_free;
	}

	dss_register_key(&daos_srv_modkey);
	dss_register_key(&vos_module_key);

	rc = vos_srv_module.sm_init();
	if (rc != DER_SUCCESS) {
		goto fail_unregister_keys;
	}

	rc = ds_tls_key_create();
	if (rc != 0) {
		rc = daos_errno2der(rc);
		goto fail_vos_sm_fini;
	}

	rc = vos_standalone_tls_init(tag);
	if (rc != DER_SUCCESS) {
		goto fail_tls_key_delete;
	}

	rc = vos_sys_db_init(args->nvme_conf, args->storage_path);
	if (rc != DER_SUCCESS) {
		goto fail_vos_tls_fini;
	}

	rc = xstream_start_all(engine);
	if (rc != DER_SUCCESS) {
		goto fail_vos_fini;
	}

	*engine_ptr = engine;

	return 0;

fail_vos_fini:
	vos_db_fini();
fail_vos_tls_fini:
	vos_standalone_tls_fini();
fail_tls_key_delete:
	ds_tls_key_delete();
fail_vos_sm_fini:
	(void)vos_srv_module.sm_fini();
fail_unregister_keys:
	dss_unregister_key(&vos_module_key);
	dss_unregister_key(&daos_srv_modkey);
	bio_nvme_fini();
fail_engine_free:
	dlck_engine_free(engine);

	return rc;
}

int
dlck_engine_stop(struct dlck_engine *engine)
{
	int rc;

	if (DAOS_FAIL_CHECK(DLCK_FAULT_ENGINE_STOP)) { /** fault injection */
		return daos_errno2der(daos_fail_value_get());
	}

	if (engine->join_fail) {
		/** Cannot stop the engine in this case. It will probably crash. */
		return -DER_BUSY;
	}

	rc = xstream_stop_all(engine);
	if (rc != DER_SUCCESS) {
		/** not all execution streams were stopped - can't pull out other resources */
		return rc;
	}

	vos_db_fini();

	vos_standalone_tls_fini();

	ds_tls_key_delete();

	rc = vos_srv_module.sm_fini();
	if (rc != DER_SUCCESS) {
		/** this is odd - do not free other resources just in case */
		return rc;
	}

	dss_unregister_key(&vos_module_key);
	dss_unregister_key(&daos_srv_modkey);

	bio_nvme_fini();

	dlck_engine_free(engine);

	return rc;
}

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
 * \brief Join all ULTs but ignore errors. No error returned neither.
 *
 * \note It is designed as a cleanup procedure in case of an error either while starting or stopping
 * ULTs.
 *
 * \param[in]		engine	Engine to clean up.
 * \param[in,out]	de	Execution to stop and cleanup after.
 */
static void
dlck_engine_join_all_no_error(struct dlck_engine *engine, struct dlck_exec *de)
{
	int rc;

	for (int i = 0; i < engine->targets; ++i) {
		if (de->ults[i].thread != ABT_THREAD_NULL) {
			rc = ABT_thread_join(de->ults[i].thread);
			if (rc != ABT_SUCCESS) {
				engine->join_fail = true;
				/**
				 * the ULT did not join - can't free the thread nor free the
				 * arguments
				 */
				continue;
			}

			(void)ABT_thread_free(&de->ults[i].thread);
		}
		(void)de->arg_free_fn(de->custom, &de->ult_args[i]);
	}

	D_FREE(de->ult_args);
	D_FREE(de->ults);
}

/**
 * Spawn an ULT on each of the targets execution stream.
 *
 * \param[in] engine		Engine to run the created ULTs on.
 * \param[in] exec_one		Function to run in the ULTs.
 * \param[in] arg_alloc_fn	Function to allocate arguments for an ULT.
 * \param[in,out] de		Execution state to store the created resources.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_NOMEM	Out of memory.
 * \retval -DER_*	Other error.
 */
static int
dlck_engine_targets_start(struct dlck_engine *engine, dlck_ult_func exec_one,
			  arg_alloc_fn_t arg_alloc_fn, struct dlck_exec *de)
{
	int rc = DER_SUCCESS;

	if (DAOS_FAIL_CHECK(DLCK_FAULT_ENGINE_EXEC)) { /** fault injection */
		return daos_errno2der(daos_fail_value_get());
	}

	D_ALLOC_ARRAY(de->ults, engine->targets);
	if (de->ults == NULL) {
		return -DER_NOMEM;
	}

	D_ALLOC_ARRAY(de->ult_args, engine->targets);
	if (de->ult_args == NULL) {
		D_FREE(de->ults);
		return -DER_NOMEM;
	}

	for (int i = 0; i < engine->targets; ++i) {
		/** prepare arguments */
		rc = arg_alloc_fn(engine, i, de->custom, &de->ult_args[i]);
		if (rc != DER_SUCCESS) {
			goto fail_join_and_free;
		}

		/** start an ULT */
		rc = dlck_ult_create(engine->xss[i].pool, exec_one, de->ult_args[i], &de->ults[i]);
		if (rc != DER_SUCCESS) {
			goto fail_join_and_free;
		}
	}

	return rc;

fail_join_and_free:
	dlck_engine_join_all_no_error(engine, de);

	return rc;
}

/**
 * Wait for all the target ULTs to conclude.
 *
 * \param[in] engine	Engine where the ULTs run.
 * \param[in] de	Execution state to wait for and release.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Other error.
 */
static int
dlck_engine_targets_stop(struct dlck_engine *engine, struct dlck_exec *de)
{
	int rc = DER_SUCCESS;

	if (DAOS_FAIL_CHECK(DLCK_FAULT_ENGINE_JOIN)) { /** fault injection */
		engine->join_fail = true;
		return daos_errno2der(daos_fail_value_get());
	}

	for (int i = 0; i < engine->targets; ++i) {
		rc = ABT_thread_join(de->ults[i].thread);
		if (rc != ABT_SUCCESS) {
			rc = dss_abterr2der(rc);
			engine->join_fail = true;
			goto fail_join_and_free;
		}

		rc = ABT_thread_free(&de->ults[i].thread);
		if (rc != ABT_SUCCESS) {
			rc = dss_abterr2der(rc);
			goto fail_join_and_free;
		}

		rc = de->arg_free_fn(de->custom, &de->ult_args[i]);
		if (rc != 0) {
			goto fail_join_and_free;
		}
	}

	D_FREE(de->ult_args);
	D_FREE(de->ults);

	return rc;

fail_join_and_free:
	dlck_engine_join_all_no_error(engine, de);

	return rc;
}

#define STOP_TGT_STR "Wait for targets to stop"

int
dlck_engine_exec_all(struct dlck_engine *engine, dlck_ult_func exec_one,
		     arg_alloc_fn_t arg_alloc_fn, void *custom, arg_free_fn_t arg_free_fn,
		     struct checker *ck)
{
	struct dlck_exec de = {0};
	int              rc;

	/** initialize batch */
	de.arg_free_fn = arg_free_fn;
	de.custom      = custom;

	CK_PRINT(ck, "Start targets... ");
	rc = dlck_engine_targets_start(engine, exec_one, arg_alloc_fn, &de);
	CK_APPENDL_RC(ck, rc);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	CK_PRINT(ck, STOP_TGT_STR "...\n");
	rc = dlck_engine_targets_stop(engine, &de);
	CK_PRINTL_RC(ck, rc, STOP_TGT_STR);

	return rc;
}

int
dlck_engine_xstream_arg_alloc(struct dlck_engine *engine, int idx, void *ctrl_ptr,
			      void **output_arg)
{
	struct xstream_arg *xa;

	D_ALLOC_PTR(xa);
	if (xa == NULL) {
		return -DER_NOMEM;
	}

	xa->ctrl   = ctrl_ptr;
	xa->engine = engine;
	xa->xs     = &engine->xss[idx];
	xa->rc     = DER_SUCCESS;

	*output_arg = xa;

	return DER_SUCCESS;
}

int
dlck_engine_xstream_arg_free(void *ctrl_ptr, void **arg)
{
	struct dlck_control *ctrl = ctrl_ptr;
	struct xstream_arg  *xa   = *arg;
	int                  rc;

	if (xa == NULL) {
		return DER_SUCCESS;
	}

	rc = xa->rc;
	dlck_uadd_no_overflow(ctrl->warnings_num, xa->warnings_num, &ctrl->warnings_num);

	D_FREE(*arg);
	*arg = NULL;

	return rc;
}
