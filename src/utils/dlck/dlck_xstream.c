/*
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdio.h>

#include <daos/mem.h>
#include <daos_srv/bio.h>
#include <engine/srv_internal.h>

#include "dlck_engine.h"
#include "dlck_xstream.h"

#ifdef DLCK_XSTREAM_UNIT_TEST
struct dss_module_info *
dlck_engine_get_module_info(void);
#else
#define dlck_engine_get_module_info dss_get_module_info
#endif

#define ULT_FINI_FAIL_FMT "[%d] ULT finalization failed - cannot safely free the XS: " DF_RC "\n"

static void
nvme_polling_ult(void *arg)
{
	struct dlck_xstream    *xs = arg;
	ABT_bool                is_ready;
	struct dss_module_info *dmi;
	int                     rc;

	dmi = dlck_engine_get_module_info();
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
dlck_xstream_is_sys(int tgt_id)
{
	return tgt_id < 0;
}

static inline bool
dlck_xstream_has_nvme(int tgt_id)
{
	if (!dlck_xstream_is_sys(tgt_id)) {
		return true;
	}

	return bio_nvme_configured(SMD_DEV_TYPE_META);
}

int
dlck_xstream_create(struct dlck_xstream *xs)
{
	int rc;

	rc = ABT_xstream_create(ABT_SCHED_NULL, &xs->xstream);
	if (rc != ABT_SUCCESS) {
		return dss_abterr2der(rc);
	}

	rc = ABT_xstream_get_main_pools(xs->xstream, 1, &xs->pool);
	if (rc != ABT_SUCCESS) {
		(void)ABT_xstream_free(&xs->xstream);
		return dss_abterr2der(rc);
	}

	return DER_SUCCESS;
}

int
dlck_xstream_free(struct dlck_xstream *xs)
{
	int rc;

	if (xs->xstream == ABT_XSTREAM_NULL) {
		return DER_SUCCESS;
	}

	rc = ABT_xstream_free(&xs->xstream);
	if (rc != ABT_SUCCESS) {
		return dss_abterr2der(rc);
	}

	return DER_SUCCESS;
}

static int
dlck_xstream_init_ult_body(struct dlck_xstream *xs)
{
	int                     tag;
	int                     xs_id;
	char                    name[DSS_XS_NAME_LEN];
	void                   *tls;
	struct dss_module_info *dmi;
	int                     rc;

	if (dlck_xstream_is_sys(xs->tgt_id)) {
		tag   = DAOS_SERVER_TAG - DAOS_TGT_TAG;
		xs_id = 0;
		rc    = snprintf(name, DSS_XS_NAME_LEN, DSS_SYS_XS_NAME_FMT, 0);
	} else {
		tag   = DAOS_SERVER_TAG;
		xs_id = DSS_MAIN_XS_ID_WITH_HELPER_POOL(xs->tgt_id, DSS_SYS_XS_NR_DEFAULT);
		rc    = snprintf(name, DSS_XS_NAME_LEN, DSS_IO_XS_NAME_FMT, xs->tgt_id);
	}

	if (rc < 0 || rc >= DSS_XS_NAME_LEN) {
		return -DER_INVAL;
	}

	(void)pthread_setname_np(pthread_self(), name);

	tls = dss_tls_init(tag, xs_id, xs->tgt_id);
	if (tls == NULL) {
		return -DER_NOMEM;
	}

	if (dlck_xstream_has_nvme(xs->tgt_id)) {
		dmi = dlck_engine_get_module_info();
		D_ASSERT(dmi != NULL);

		rc = bio_xsctxt_alloc(&dmi->dmi_nvme_ctxt, xs->tgt_id, false);
		if (rc != DER_SUCCESS) {
			goto fail_tls_fini;
		}

		rc = ABT_eventual_create(0, &xs->nvme_poll_done);
		if (rc != ABT_SUCCESS) {
			rc = dss_abterr2der(rc);
			goto fail_xsctxt_free;
		}

		rc = dlck_ult_create(xs->pool, nvme_polling_ult, xs, &xs->nvme_poll);
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
dlck_xstream_init_ult(void *arg)
{
	struct dlck_xstream *xs = arg;

	xs->ult_rc = dlck_xstream_init_ult_body(xs);
}

int
dlck_xstream_init(struct dlck_xstream *xs)
{
	ABT_thread      init_ult;
	int             rc;
	int             rc_abt;

	rc = dlck_ult_create(xs->pool, dlck_xstream_init_ult, xs, &init_ult);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	/* ABT_thread_free() waits for the ULT to join internally. */
	rc_abt = ABT_thread_free(&init_ult);
	D_ASSERT(rc_abt == ABT_SUCCESS);

	if (xs->ult_rc != DER_SUCCESS) {
		D_ERROR("[%d] Initialization of XS failed\n", xs->tgt_id);
	} else {
		D_EMIT("[%d] Initialization of XS succeeded\n", xs->tgt_id);
	}

	return xs->ult_rc;
}

static int
dlck_xstream_fini_ult_body(struct dlck_xstream *xs)
{
	struct dss_module_info *dmi;
	void                   *tls = dss_tls_get();
	int                     rc  = DER_SUCCESS;

	if (tls == NULL) {
		return DER_SUCCESS;
	}

	if (dlck_xstream_has_nvme(xs->tgt_id)) {
		rc = dss_abterr2der(ABT_eventual_set(xs->nvme_poll_done, NULL, 0));
		if (rc != DER_SUCCESS) {
			goto fail;
		}

		rc = dss_abterr2der(ABT_thread_join(xs->nvme_poll));
		if (rc != DER_SUCCESS) {
			goto fail;
		}

		rc = dss_abterr2der(ABT_thread_free(&xs->nvme_poll));
		if (rc != DER_SUCCESS) {
			/* The joined polling thread lets us safely finalize TLS despite this error.
			 */
		}

		dmi = dlck_engine_get_module_info();
		D_ASSERT(dmi != NULL);
		bio_xsctxt_free(dmi->dmi_nvme_ctxt);
	}

	dss_tls_fini(tls);

fail:
	/* On failure retain resources if the NVMe polling ULT may still be active. */
	return rc;
}

static void
dlck_xstream_fini_ult(void *arg)
{
	struct dlck_xstream *xs = arg;

	xs->ult_rc = dlck_xstream_fini_ult_body(xs);
}

void
dlck_xstream_stop_all_no_error(struct dlck_xstream *xss, unsigned targets)
{
	ABT_thread      fini_ult;
	int             rc;
	int             rc_abt;

	for (unsigned i = 0; i <= targets; ++i) {
		rc = dlck_ult_create(xss[i].pool, dlck_xstream_fini_ult, &xss[i], &fini_ult);
		if (rc != DER_SUCCESS) {
			continue;
		}

		/* ABT_thread_free() waits for the ULT to join internally. */
		rc_abt = ABT_thread_free(&fini_ult);
		D_ASSERT(rc_abt == ABT_SUCCESS);

		(void)dlck_xstream_free(&xss[i]);
	}
}

int
dlck_xstream_stop(struct dlck_xstream *xs)
{
	ABT_thread      fini_ult;
	ABT_bool        is_empty;
	int             rc;
	int             rc_abt;

	rc = dlck_ult_create(xs->pool, dlck_xstream_fini_ult, xs, &fini_ult);
	if (rc != DER_SUCCESS) {
		D_ERROR(ULT_FINI_FAIL_FMT, xs->tgt_id, DP_RC(rc));
		return rc;
	}

	/* ABT_thread_free() waits for the ULT to join internally. */
	rc_abt = ABT_thread_free(&fini_ult);
	D_ASSERT(rc_abt == ABT_SUCCESS);

	if (xs->ult_rc != DER_SUCCESS) {
		rc = xs->ult_rc;
		D_ERROR(ULT_FINI_FAIL_FMT, xs->tgt_id, DP_RC(rc));
		return rc;
	}

	rc_abt = ABT_pool_is_empty(xs->pool, &is_empty);
	D_ASSERT(rc_abt == ABT_SUCCESS);
	if (is_empty != ABT_TRUE) {
		D_ERROR("[%d] cannot free XS - it is busy\n", xs->tgt_id);
		return -DER_BUSY;
	}

	rc = dlck_xstream_free(xs);
	if (rc != DER_SUCCESS) {
		D_ERROR("[%d] XS free failed: " DF_RC "\n", xs->tgt_id, DP_RC(rc));
	}

	return rc;
}

int
dlck_xstream_arg_alloc(struct dlck_engine *engine, int idx, void *ctrl_ptr, void **output_arg)
{
	struct xstream_arg *xa;

	D_ALLOC_PTR(xa);
	if (xa == NULL) {
		return -DER_NOMEM;
	}

	xa->ctrl   = ctrl_ptr;
	xa->engine = engine;
	xa->xs     = &engine->xss[idx];

	*output_arg = xa;

	return DER_SUCCESS;
}

void
dlck_xstream_arg_free(void *sts_ptr, void **arg)
{
	struct checker_status *sts = sts_ptr;
	struct xstream_arg    *xa  = *arg;

	if (xa == NULL) {
		return;
	}

	sts[xa->xs->tgt_id].rc           = xa->status.rc;
	sts[xa->xs->tgt_id].warnings_num = xa->status.warnings_num;
	sts[xa->xs->tgt_id].errors_num   = xa->status.errors_num;

	D_FREE(*arg);
	*arg = NULL;
}
