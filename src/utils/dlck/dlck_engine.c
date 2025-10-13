/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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

int
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

int
dlck_engine_xstream_fini(struct dlck_xstream *xs)
{
	struct dss_module_info *dmi;
	void                   *tls = dss_tls_get();
	int                     rc  = DER_SUCCESS;

	D_ASSERT(tls != NULL);

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

/**
 * Create and initialize daos_sys_0 execution stream (XS) and create all daos_io_* XSes.
 * No daos_io_* initialization here yet. They ought to be initialized by the first ULT run in them.
 *
 * \param[in,out]	engine	Engine to start its XSes.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
static int
xstream_start_all(struct dlck_engine *engine)
{
	struct dlck_xstream *xs;
	struct dlck_ult      daos_sys_init;
	int                  rc;

	/** create and initialize daos_sys_0 execution stream (XS) */
	xs         = &engine->xss[engine->targets]; /** there is one more XS than targets */
	xs->tgt_id = -1;
	rc         = dlck_xstream_create(xs);
	if (rc != 0) {
		return rc;
	}

	rc = dlck_ult_create(xs->pool, dlck_engine_xstream_init_ult, xs, &daos_sys_init);
	if (rc != DER_SUCCESS) {
		/** ULT has not been created - the daos_sys_0 XS can be safely freed */
		(void)dlck_xstream_free(xs);
		return dss_abterr2der(rc);
	}

	/** wait for the daos_sys_0 initialization to conclude */
	rc = ABT_thread_join(daos_sys_init.thread);
	if (rc != ABT_SUCCESS) {
		D_ERROR("ULT has not joined - cannot safely free the daos_sys_0 XS\n");
		return dss_abterr2der(rc);
	}

	rc = ABT_thread_free(&daos_sys_init.thread);
	if (rc != ABT_SUCCESS) {
		/** ULT has joined - the daos_sys_0 XS can be safely freed */
		(void)dlck_xstream_free(xs);
		return dss_abterr2der(rc);
	}

	if (xs->ult_rc != DER_SUCCESS) {
		/** ULT has joined - the daos_sys_0 XS can be safely freed */
		(void)dlck_xstream_free(xs);
		return xs->ult_rc;
	}

	/**
	 * The daos_sys_0 XS initialization succeeded. It may have spawned a NVMe polling ULT.
	 */

	/** create all daos_io_* execution streams (XS) */
	for (int i = 0; i < engine->targets; ++i) {
		xs         = &engine->xss[i];
		xs->tgt_id = i;
		rc         = dlck_xstream_create(xs);
		if (rc != 0) {
			goto fail;
		}
	}

	return 0;

fail:
	/** free all daos_io_* and the daos_sys_0 XS */
	for (int i = 0; i <= engine->targets; ++i) {
		xs = &engine->xss[i];
		(void)dlck_xstream_free(xs);
	}

	return rc;
}

/**
 * Stop and free the daos_sys_0 execution stream (XS) and all the daos_io_* XSes belonging to
 * the provided engine.
 *
 * Note: All the XSes have to be idle before calling this function. Except for daos_sys_0 which
 * still may have the NMVe polling ULT but no other ULTs present in its pool.
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
	struct dlck_ult      daos_sys_fini;
	ABT_bool             is_empty;
	int                  rc = DER_SUCCESS;

	/** check on the daos_sys_0 XS */
	xs = &engine->xss[engine->targets];

	/** Stop the NVMe polling ULT if present. */
	if (dlck_engine_xstream_has_nvme(xs->tgt_id)) {
		rc = dlck_ult_create(xs->pool, dlck_engine_xstream_fini_ult, xs, &daos_sys_fini);
		if (rc != DER_SUCCESS) {
			/** ULT has not been created - the daos_sys_0 XS can be safely freed */
			return dss_abterr2der(rc);
		}

		/** wait for the daos_sys_0 finalization to conclude */
		rc = ABT_thread_join(daos_sys_fini.thread);
		if (rc != ABT_SUCCESS) {
			D_ERROR("ULT has not joined - cannot safely free the daos_sys_0 XS\n");
			return dss_abterr2der(rc);
		}

		rc = ABT_thread_free(&daos_sys_fini.thread);
		/**
		 * This RC is not so important as long as the finalization RC says the finalization
		 * has succeeded the procedure should continue undisturbed.
		 */
		D_ASSERT(rc == ABT_SUCCESS);

		if (xs->ult_rc != DER_SUCCESS) {
			D_ERROR("the daos_sys_0 finalization failed - cannot safely free the "
				"daos_sys_0 XS\n");
			return xs->ult_rc;
		}
	}

	/** free all daos_io_* and the daos_sys_0 XS */
	for (int i = 0; i <= engine->targets; ++i) {
		xs = &engine->xss[i];
		/** make sure the XS is idle */
		rc = ABT_pool_is_empty(xs->pool, &is_empty);
		if (rc != ABT_SUCCESS) {
			D_ERROR("can't tell whether XS[%d] can be freed or not\n", i);
			return dss_abterr2der(rc);
		} else {
			if (is_empty != ABT_TRUE) {
				D_ERROR("cannot free XS[%d] - it is busy\n", i);
				return -DER_BUSY;
			} else {
				rc = dlck_xstream_free(xs);
				if (rc != DER_SUCCESS) {
					return rc;
				}
			}
		}
	}

	return rc;
}

int
dlck_engine_start(struct dlck_args_engine *args, struct dlck_engine **engine_ptr)
{
	struct dlck_engine *engine;
	const bool          bypass_health_chk = false;
	int                 tag               = DAOS_SERVER_TAG - DAOS_TGT_TAG;
	int                 rc;

	if (DAOS_FAIL_CHECK(DLCK_FAULT_ENGINE_START)) {
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

int
dlck_engine_exec_all_async(struct dlck_engine *engine, dlck_ult_func exec_one,
			   arg_alloc_fn_t arg_alloc_fn, void *custom, arg_free_fn_t arg_free_fn,
			   struct dlck_exec *de)
{
	int rc;

	D_ASSERT(de != NULL);

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

	de->custom      = custom;
	de->arg_free_fn = arg_free_fn;

	for (int i = 0; i < engine->targets; ++i) {
		/** prepare arguments */
		rc = arg_alloc_fn(engine, i, custom, &de->ult_args[i]);
		if (rc != DER_SUCCESS) {
			dlck_engine_join_all_no_error(engine, de);
			return rc;
		}

		/** start an ULT */
		rc = dlck_ult_create(engine->xss[i].pool, exec_one, de->ult_args[i], &de->ults[i]);
		if (rc != DER_SUCCESS) {
			dlck_engine_join_all_no_error(engine, de);
			return rc;
		}
	}

	return DER_SUCCESS;
}

int
dlck_engine_join_all(struct dlck_engine *engine, struct dlck_exec *de, int *rcs)
{
	int rc;

	D_ASSERT(rcs != NULL);

	if (DAOS_FAIL_CHECK(DLCK_FAULT_ENGINE_JOIN)) { /** fault injection */
		return daos_errno2der(daos_fail_value_get());
	}

	for (int i = 0; i < engine->targets; ++i) {
		rc = ABT_thread_join(de->ults[i].thread);
		if (rc != ABT_SUCCESS) {
			rc = dss_abterr2der(rc);
			goto fail_join_and_free;
		}

		rc = ABT_thread_free(&de->ults[i].thread);
		if (rc != ABT_SUCCESS) {
			rc = dss_abterr2der(rc);
			goto fail_join_and_free;
		}

		rcs[i] = de->arg_free_fn(de->custom, &de->ult_args[i]);
	}

	D_FREE(de->ult_args);
	D_FREE(de->ults);

	return DER_SUCCESS;

fail_join_and_free:
	dlck_engine_join_all_no_error(engine, de);
	return rc;
}

int
dlck_engine_exec_all_sync(struct dlck_engine *engine, dlck_ult_func exec_one,
			  arg_alloc_fn_t arg_alloc_fn, void *custom, arg_free_fn_t arg_free_fn)
{
	struct dlck_exec de = {0};
	int             *rcs;
	int              rc;

	D_ALLOC_ARRAY(rcs, engine->targets);
	if (rcs == NULL) {
		return -DER_NOMEM;
	}

	rc = dlck_engine_exec_all_async(engine, exec_one, arg_alloc_fn, custom, arg_free_fn, &de);
	if (rc != DER_SUCCESS) {
		D_FREE(rcs);
		return rc;
	}

	rc = dlck_engine_join_all(engine, &de, rcs);
	if (rc != DER_SUCCESS) {
		D_FREE(rcs);
		return rc;
	}

	for (int i = 0; i < engine->targets; ++i) {
		if (rcs[i] != DER_SUCCESS) {
			rc = rcs[i];
			break;
		}
	}

	D_FREE(rcs);
	return rc;
}

int
dlck_engine_exec(struct dlck_engine *engine, int idx, dlck_ult_func exec,
		 arg_alloc_fn_t arg_alloc_fn, void *custom, arg_free_fn_t arg_free_fn)
{
	struct dlck_ult ult;
	void           *ult_args;
	int             rc;
	int             rc2;

	/** prepare arguments */
	rc = arg_alloc_fn(engine, idx, custom, &ult_args);
	if (rc != DER_SUCCESS) {
		goto fail_join_and_free;
	}

	/** start an ULT */
	rc = dlck_ult_create(engine->xss[idx].pool, exec, ult_args, &ult);
	if (rc != DER_SUCCESS) {
		goto fail_join_and_free;
	}

	rc = ABT_thread_join(ult.thread);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto fail_join_and_free;
	}

	rc = ABT_thread_free(&ult.thread);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto fail_join_and_free;
	}

	return arg_free_fn(custom, &ult_args);

fail_join_and_free:
	if (ult.thread != ABT_THREAD_NULL) {
		rc2 = ABT_thread_join(ult.thread);
		if (rc2 != ABT_SUCCESS) {
			/** the ULT did not join - can't free the thread nor free the arguments */
			return rc;
		}

		(void)ABT_thread_free(&ult.thread);
	}
	(void)arg_free_fn(custom, &ult_args);

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
	struct xstream_arg *xa = *arg;
	int                 rc;

	if (xa == NULL) {
		return DER_SUCCESS;
	}

	rc = xa->rc;

	D_FREE(*arg);
	*arg = NULL;

	return rc;
}
