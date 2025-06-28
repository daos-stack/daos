/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <libgen.h>
#include <stdlib.h>
#include <stdio.h>
#include <daos/mem.h>
#include <daos/btree_class.h>
#include <gurt/telemetry_producer.h>
#include <daos_srv/vos.h>
#include <daos_srv/dlck.h>
#include <daos_version.h>

#include "dlck_args.h"
#include "dlck_engine.h"

static int
dlck_engine_alloc(struct dlck_args *args, struct dlck_engine **engine_ptr)
{
	struct dlck_engine *engine;

	D_ALLOC_PTR(engine);
	if (engine == NULL) {
		return ENOMEM;
	}

	/** each of the targets will get its own xstream + 1 for daos_sys */
	D_ALLOC_ARRAY(engine->xss, args->common.targets + 1);
	if (engine->xss == NULL) {
		D_FREE(engine);
		return ENOMEM;
	}

	engine->targets = args->common.targets;

	*engine_ptr = engine;

	return 0;
}

/**
 * XXX should be shared with the DAOS engine.
 */
static int
dlck_register_dbtree_classes(void)
{
	int rc;

	rc = dbtree_class_register(DBTREE_CLASS_IFV, BTR_FEAT_UINT_KEY | BTR_FEAT_DIRECT_KEY,
				   &dbtree_ifv_ops);
	if (rc != 0) {
		return rc;
	}

	return DER_SUCCESS;
}

/**
 * XXX should be shared with dss_sys_db_init().
 */
static int
dlck_sys_db_init(struct dlck_args *args)
{
	int   rc;
	char *sys_db_path    = NULL;
	char *nvme_conf_path = NULL;

	if (!bio_nvme_configured(SMD_DEV_TYPE_META))
		goto db_init;

	if (args->common.nvme_conf == NULL) {
		D_ERROR("nvme conf path not set\n");
		return -DER_INVAL;
	}

	D_STRNDUP(nvme_conf_path, args->common.nvme_conf, PATH_MAX);
	if (nvme_conf_path == NULL)
		return -DER_NOMEM;
	D_STRNDUP(sys_db_path, dirname(nvme_conf_path), PATH_MAX);
	D_FREE(nvme_conf_path);
	if (sys_db_path == NULL)
		return -DER_NOMEM;

db_init:
	rc = vos_db_init(bio_nvme_configured(SMD_DEV_TYPE_META) ? sys_db_path
								: args->common.storage_path);
	if (rc)
		goto out;

	rc = smd_init(vos_db_get());
	if (rc)
		vos_db_fini();
out:
	D_FREE(sys_db_path);

	return rc;
}

static void
nvme_polling(void *arg)
{
	ABT_eventual           *done = arg;
	ABT_bool                is_ready;
	struct dss_module_info *dmi;
	int                     rc;

	dmi = dss_get_module_info();
	D_ASSERT(dmi != NULL);

	do {
		(void)bio_nvme_poll(dmi->dmi_nvme_ctxt);
		ABT_thread_yield();

		rc = ABT_eventual_test(*done, NULL, &is_ready);
		if (rc != 0) {
			return;
		}
	} while (is_ready == ABT_FALSE);
}

unsigned int dss_sys_xs_nr = 3;

/** main XS id of (vos) tgt_id */
#define DSS_MAIN_XS_ID(tgt_id) ((tgt_id) + dss_sys_xs_nr)

/** XXX should be shared with the DAOS engine */
#define DSS_SYS_XS_NAME_FMT    "daos_sys_%d"
#define DSS_IO_XS_NAME_FMT     "daos_io_%d"

/**
 * XXX teardown
 */
int
dlck_engine_xstream_init(struct dlck_xstream *xs)
{
	struct dss_module_info *dmi;
	int                     tag;
	int                     xs_id;
	int                     tgt_id = xs->tgt_id;
	char                    name[DSS_XS_NAME_LEN];
	int                     rc;

	if (tgt_id < 0) {
		tag   = DAOS_SERVER_TAG - DAOS_TGT_TAG;
		xs_id = 0;

		rc = snprintf(name, DSS_XS_NAME_LEN, DSS_SYS_XS_NAME_FMT, 0);
		if (rc < 0) {
			return ENOMEM;
		}
	} else {
		tag   = DAOS_SERVER_TAG;
		xs_id = DSS_MAIN_XS_ID(tgt_id);

		rc = snprintf(name, DSS_XS_NAME_LEN, DSS_IO_XS_NAME_FMT, tgt_id);
		if (rc < 0) {
			return ENOMEM;
		}
	}

	(void)pthread_setname_np(pthread_self(), name);

	/**
	 * for xstream:
	 * - dss_tls_init
	 * - bio_xsctxt_alloc
	 * - thread_create(dss_nvme_poll_ult)
	 */

	(void)dss_tls_init(tag, xs_id, tgt_id);

	dmi = dss_get_module_info();
	D_ASSERT(dmi != NULL);

	if (bio_nvme_configured(SMD_DEV_TYPE_META)) {
		rc = ABT_eventual_create(0, &xs->nvme_poll_done);
		if (rc != 0) {
			return rc;
		}

		rc = bio_xsctxt_alloc(&dmi->dmi_nvme_ctxt, tgt_id, false);
		if (rc != 0) {
			return rc;
		}

		dlck_ult_create(xs->pool, nvme_polling, &xs->nvme_poll_done, &xs->nvme_poll);
	}

	return 0;
}

static void
dlck_engine_xstream_init_ult(void *arg)
{
	struct dlck_xstream *xs = arg;

	int                  rc = dlck_engine_xstream_init(xs);
	D_ASSERT(rc == 0);
}

/**
 * XXX bits missing
 */
int
dlck_engine_xstream_fini(struct dlck_xstream *xs)
{
	int rc;

	rc = ABT_eventual_set(xs->nvme_poll_done, NULL, 0);
	if (rc != 0) {
		return rc;
	}

	rc = ABT_thread_join(xs->nvme_poll.thread);
	if (rc != 0) {
		return rc;
	}

	rc = ABT_thread_free(&xs->nvme_poll.thread);
	if (rc != 0) {
		return rc;
	}

	return 0;
}

extern struct dss_module     vos_srv_module;

extern struct dss_module_key vos_module_key;

/**
 * XXX teardown missing
 */
static int
xstream_start_all(struct dlck_args *args, struct dlck_engine *engine)
{
	struct dlck_xstream *xs;
	struct dlck_ult      daos_sys_init;
	int                  rc;

	/** start daos_sys_0 */
	xs         = &engine->xss[engine->targets]; /** there is one more XS than targets */
	xs->tgt_id = -1;
	rc         = dlck_xstream_create(xs);
	if (rc != 0) {
		return rc;
	}

	rc = dlck_ult_create(xs->pool, dlck_engine_xstream_init_ult, xs, &daos_sys_init);
	if (rc != 0) {
		return rc;
	}

	/** XXX the user may ask to process a subset of targets */

	/** start daos_io_X */
	for (int i = 0; i < engine->targets; ++i) {
		xs         = &engine->xss[i];
		xs->tgt_id = i;
		rc         = dlck_xstream_create(xs);
		if (rc != 0) {
			return rc;
		}
	}

	rc = ABT_thread_join(daos_sys_init.thread);
	if (rc != 0) {
		return rc;
	}

	rc = ABT_thread_free(&daos_sys_init.thread);
	if (rc != 0) {
		return rc;
	}

	return 0;
}

static uint64_t
dlck_metrics_region_size(int num_tgts)
{
	const uint64_t est_std_metrics = 1024; /* high estimate to allow for pool links */
	const uint64_t est_tgt_metrics = 128;  /* high estimate */

	return (est_std_metrics + est_tgt_metrics * num_tgts) * D_TM_METRIC_SIZE;
}

/**
 * XXX TODO:
 * - clean up on fail before return
 */
int
dlck_engine_start(struct dlck_args *args, struct dlck_engine **engine_ptr)
{
	struct dlck_engine            *engine;
	const struct dlck_args_common *argsc             = &args->common;
	const bool                     bypass_health_chk = false;
	int                            tag               = DAOS_SERVER_TAG - DAOS_TGT_TAG;
	const unsigned                 instance_idx      = 0;
	int                            rc;

	rc = dlck_engine_alloc(args, &engine);
	if (rc != 0) {
		return rc;
	}

	/**
	 * List of steps executed by the DAOS engine while starting:
	 * - d_tm_init
	 * - register_dbtree_classes
	 * - ABT_init
	 * - bio_nvme_init
	 * - dss_module_init_all -> vos init?
	 * - vos_standalone_tls_init
	 * - dss_sys_db_init
	 * - dss_xstreams_init:
	 *   - start system service XS
	 *   - start main IO service XS
	 */

	/** XXX is it still necessary? */
	rc = d_tm_init(instance_idx, dlck_metrics_region_size(argsc->targets), D_TM_SERVER_PROCESS);
	if (rc != 0) {
		return rc;
	}

	rc = dlck_register_dbtree_classes();
	if (rc != 0) {
		return rc;
	}

	rc = dlck_abt_init(engine);
	if (rc != 0) {
		return rc;
	}

	rc = bio_nvme_init(argsc->nvme_conf, argsc->numa_node, argsc->nvme_mem_size,
			   argsc->nvme_hugepage_size, argsc->targets, bypass_health_chk);
	if (rc != 0) {
		return rc;
	}

	dss_register_key(&daos_srv_modkey);
	dss_register_key(&vos_module_key);
	rc = vos_srv_module.sm_init();
	if (rc != 0) {
		return rc;
	}

	rc = vos_standalone_tls_init(tag);
	if (rc != 0) {
		return rc;
	}

	rc = dlck_sys_db_init(args);
	if (rc != 0) {
		return rc;
	}

	rc = xstream_start_all(args, engine);
	if (rc != 0) {
		return rc;
	}

	*engine_ptr = engine;

	return 0;
}

int
dlck_engine_stop(struct dlck_engine *engine)
{
	struct dlck_xstream *xs = &engine->xss[engine->targets];
	int                  rc;

	rc = ABT_eventual_set(xs->nvme_poll_done, NULL, 0);
	if (rc != 0) {
		return rc;
	}

	rc = ABT_thread_join(xs->nvme_poll.thread);
	if (rc != 0) {
		return rc;
	}

	rc = ABT_thread_free(&xs->nvme_poll.thread);
	if (rc != 0) {
		return rc;
	}

	return 0;
}
