/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdlib.h>
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
static inline void
dlck_engine_free(struct dlck_engine *engine)
{
	D_FREE(engine->xss);
	D_FREE(engine);
}

int
dlck_engine_start(struct dlck_engine *engine)
{
	const int            daos_sys_idx = engine->targets; /** the last one is daos_sys_0 */
	struct dlck_xstream *xs;
	int                  rc;

	if (DAOS_FAIL_CHECK(DLCK_FAULT_ENGINE_START)) { /** fault injection */
		return daos_errno2der(daos_fail_value_get());
	}

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
	rc = dlck_xstream_init(xs);
	if (rc != DER_SUCCESS) {
		goto xstream_stop_all;
	}

	/**
	 * The daos_sys_0 XS initialization succeeded. It may have spawned a NVMe polling ULT.
	 */

	/** initialize all daos_io_* XSes */
	for (int i = 0; i < engine->targets; ++i) {
		rc = dlck_xstream_init(&engine->xss[i]);
		if (rc != DER_SUCCESS) {
			goto xstream_stop_all;
		}
	}

	return DER_SUCCESS;

xstream_stop_all:
	dlck_xstream_stop_all_no_error(engine->xss, engine->targets);

	return rc;
}

int
dlck_engine_stop(struct dlck_engine *engine)
{
	int rc;

	if (engine->join_fail) {
		/** Cannot stop the engine in this case. It will probably crash. */
		return -DER_BUSY;
	}

	/** Note: daos_sys_0 XS is the last and it has to be stopped as the last one. */
	for (int i = 0; i <= engine->targets; ++i) {
		rc = dlck_xstream_stop(&engine->xss[i]);
		if (rc != DER_SUCCESS) {
			goto xstream_free_all;
		}
	}

	return DER_SUCCESS;

xstream_free_all:
	dlck_xstream_stop_all_no_error(engine->xss, engine->targets);

	return rc;
}

int
dlck_engine_init(struct dlck_args_engine *args, struct dlck_engine **engine_ptr)
{
	struct dlck_engine *engine;
	const bool          bypass_health_chk = false;
	int                 tag               = DAOS_SERVER_TAG - DAOS_TGT_TAG;
	int                 rc;

	*engine_ptr = NULL;

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

	*engine_ptr = engine;

	return DER_SUCCESS;

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
dlck_engine_fini(struct dlck_engine *engine)
{
	int rc;

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
