/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(dlck)

#include <stdlib.h>
#include <stdio.h>
#include <abt.h>

#include <daos_errno.h>
#include <daos/debug.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/mgmt_tgt_common.h>
#include <gurt/common.h>

#include "dlck_checker.h"

/**
 * Flush output immediately in case DLCK crashes unexpectedly.
 * Intended to ensure no useful diagnostic information is lost due to not flushed buffers.
 */
static int
dlck_vprintf_internal(FILE *stream, const char *fmt, va_list args)
{
	int rc;

	rc = vfprintf(stream, fmt, args);
	if (rc < 0) {
		rc = daos_errno2der(errno);
		D_ERROR("vfprintf() failed: " DF_RC "\n", DP_RC(rc));
		return rc;
	}

	rc = fflush(stream);
	if (rc == EOF) {
		rc = daos_errno2der(errno);
		D_ERROR("fflush() failed: " DF_RC "\n", DP_RC(rc));
		return rc;
	}

	return rc;
}

/**
 * Wrap printing in a lock/unlock block to guarantee thread-safe output.
 */
static int
dlck_checker_main_printf(struct checker *ck, const char *fmt, ...)
{
	struct dlck_checker_main *dcm = dlck_checker_main_get_custom(ck);
	va_list                   args;
	int                       rc_abt;
	int                       rc;

	rc_abt = ABT_mutex_lock(dcm->stream_mutex);
	if (rc_abt != ABT_SUCCESS) {
		rc = dss_abterr2der(rc_abt);
		D_ERROR(DLCK_PRINT_MAIN_LOCK_FAIL_FMT, DP_RC(rc));
		return rc;
	}

	va_start(args, fmt);
	rc = dlck_vprintf_internal(dcm->core.stream, fmt, args);
	va_end(args);

	if (rc != DER_SUCCESS) {
		(void)ABT_mutex_unlock(dcm->stream_mutex);
		return rc;
	}

	rc_abt = ABT_mutex_unlock(dcm->stream_mutex);
	if (rc_abt != ABT_SUCCESS) {
		rc = dss_abterr2der(rc_abt);
		D_ERROR(DLCK_PRINT_MAIN_UNLOCK_FAIL_FMT, DP_RC(rc));
		return rc;
	}

	return rc;
}

static int
dlck_checker_core_indent_set(struct dlck_checker_worker *dwc, int level)
{
	memset(dwc->prefix, DLCK_PRINT_INDENT, CHECKER_INDENT_MAX);
	if (level > 0) {
		dwc->prefix[level]     = ' ';
		dwc->prefix[level + 1] = '\0';
	} else {
		dwc->prefix[0] = '\0';
	}

	return DER_SUCCESS;
}

static int
dlck_checker_main_indent_set(struct checker *ck)
{
	struct dlck_checker_main *dcm = dlck_checker_main_get_custom(ck);
	return dlck_checker_core_indent_set(&dcm->core, ck->ck_level);
}

int
dlck_checker_main_init(struct checker *ck)
{
	struct dlck_checker_main *dcm;
	int                       rc_abt;
	int                       rc;

	D_ALLOC_PTR(dcm);
	if (dcm == NULL) {
		return -DER_NOMEM;
	}

	dcm->core.magic  = DLCK_CHECKER_MAIN_MAGIC;
	dcm->core.stream = stdout;

	rc_abt = ABT_mutex_create(&dcm->stream_mutex);
	if (rc_abt != ABT_SUCCESS) {
		rc = dss_abterr2der(rc_abt);
		D_ERROR("Cannot create a stream synchronization mutex: " DF_RC "\n", DP_RC(rc));
		D_FREE(dcm);
		return rc;
	}

	ck->ck_private    = dcm;
	ck->ck_printf     = dlck_checker_main_printf;
	ck->ck_indent_set = dlck_checker_main_indent_set;
	ck->ck_prefix     = dcm->core.prefix;

	return DER_SUCCESS;
}

int
dlck_checker_main_fini(struct checker *ck)
{
	struct dlck_checker_main *dcm = dlck_checker_main_get_custom(ck);
	int                       rc_abt;
	int                       rc = DER_SUCCESS;

	rc_abt = ABT_mutex_free(&dcm->stream_mutex);
	if (rc_abt != ABT_SUCCESS) {
		rc = dss_abterr2der(rc_abt);
		D_ERROR("Failed to free the stream synchronization mutex: " DF_RC "\n", DP_RC(rc));
	}

	D_FREE(dcm);
	memset(ck, 0, sizeof(*ck));

	return rc;
}

/**
 * Get the custom payload from the worker's checker.
 *
 * \param[in]   ck      Print utility (only the worker one will work).
 *
 * \return The custom payload.
 */
static inline struct dlck_checker_worker *
dlck_checker_worker_get_custom(struct checker *ck)
{
	struct dlck_checker_worker *dcw = ck->ck_private;
	D_ASSERT(dcw->magic == DLCK_CHECKER_WORKER_MAGIC);
	return dcw;
}

static int
dlck_checker_worker_indent_set(struct checker *ck)
{
	struct dlck_checker_worker *dcw = dlck_checker_worker_get_custom(ck);
	return dlck_checker_core_indent_set(dcw, ck->ck_level);
}

/**
 * Just print.
 */
static int
dlck_checker_worker_printf(struct checker *ck, const char *fmt, ...)
{
	struct dlck_checker_worker *dcw    = dlck_checker_worker_get_custom(ck);
	FILE                       *stream = dcw->stream;
	va_list                     args;
	int                         rc;

	va_start(args, fmt);
	rc = dlck_vprintf_internal(stream, fmt, args);
	va_end(args);

	return rc;
}

int
dlck_checker_worker_init(struct checker_options *options, const char *log_dir, uuid_t po_uuid,
			 int tgt_id, struct checker *main_ck, struct checker *ck)
{
	struct dlck_checker_worker *dcw;
	char                       *log_file;
	FILE                       *stream;
	int                         rc;

	D_ALLOC_PTR(dcw);
	if (dcw == NULL) {
		return -DER_NOMEM;
	}

	/** open the logfile */
	D_ASPRINTF(log_file, "%s/" DF_UUIDF "_%s%d", log_dir, DP_UUID(po_uuid), VOS_FILE, tgt_id);
	if (log_file == NULL) {
		rc = -DER_NOMEM;
		CK_PRINTFL_RC(main_ck, rc, "[%d] Log file path allocation failed", tgt_id);
		/**
		 * It is very unlikely we can continue work without an ability to allocate more
		 * memory.
		 */
		D_FREE(dcw);
		return rc;
	}

	stream = fopen(log_file, "w");
	if (stream == NULL) {
		rc = daos_errno2der(errno);
		CK_PRINTFL_RC(main_ck, rc, "[%d] Log file open failed: %s", tgt_id, log_file);
		D_FREE(log_file);
		D_FREE(dcw);
		return rc;
	}
	D_FREE(log_file);

	dcw->magic  = DLCK_CHECKER_WORKER_MAGIC;
	dcw->stream = stream;

	memset(ck, 0, sizeof(*ck));
	memcpy(&ck->ck_options, options, sizeof(*options));
	ck->ck_printf         = dlck_checker_worker_printf;
	ck->ck_indent_set     = dlck_checker_worker_indent_set;
	ck->ck_private        = dcw;
	ck->ck_prefix         = dcw->prefix;

	return DER_SUCCESS;
}

void
dlck_checker_worker_fini(struct checker *ck)
{
	struct dlck_checker_worker *dcw = dlck_checker_worker_get_custom(ck);

	(void)fclose(dcw->stream);
	D_FREE(dcw);
	memset(ck, 0, sizeof(*ck));
}
