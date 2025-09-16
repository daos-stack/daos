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
#include <daos/dlck.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/mgmt_tgt_common.h>
#include <gurt/common.h>

#include "dlck_print.h"

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
dlck_printf_main(struct dlck_print *dp, const char *fmt, ...)
{
	struct dlck_print_main *dpm = dlck_print_main_get_custom(dp);
	va_list                 args;
	int                     rc_abt;
	int                     rc;

	rc_abt = ABT_mutex_lock(dpm->stream_mutex);
	if (rc_abt != ABT_SUCCESS) {
		rc = dss_abterr2der(rc_abt);
		D_ERROR(DLCK_PRINT_MAIN_LOCK_FAIL_FMT, DP_RC(rc));
		return rc;
	}

	va_start(args, fmt);
	rc = dlck_vprintf_internal(dpm->stream, fmt, args);
	va_end(args);

	if (rc != DER_SUCCESS) {
		(void)ABT_mutex_unlock(dpm->stream_mutex);
		return rc;
	}

	rc_abt = ABT_mutex_unlock(dpm->stream_mutex);
	if (rc_abt != ABT_SUCCESS) {
		rc = dss_abterr2der(rc_abt);
		D_ERROR(DLCK_PRINT_MAIN_UNLOCK_FAIL_FMT, DP_RC(rc));
		return rc;
	}

	return rc;
}

int
dlck_print_main_init(struct dlck_print *dp)
{
	struct dlck_print_main *dpm;
	int                     rc_abt;
	int                     rc;

	D_ALLOC_PTR(dpm);
	if (dpm == NULL) {
		return -DER_NOMEM;
	}

	dpm->magic  = DLCK_PRINT_MAIN_MAGIC;
	dpm->stream = stdout;

	rc_abt = ABT_mutex_create(&dpm->stream_mutex);
	if (rc_abt != ABT_SUCCESS) {
		rc = dss_abterr2der(rc_abt);
		D_ERROR("Cannot create a stream synchronization mutex: " DF_RC "\n", DP_RC(rc));
		D_FREE(dpm);
		return rc;
	}

	dp->printf_custom = dpm;
	dp->dp_printf     = dlck_printf_main;

	return DER_SUCCESS;
}

int
dlck_print_main_fini(struct dlck_print *dp)
{
	struct dlck_print_main *dpm = dlck_print_main_get_custom(dp);
	int                     rc_abt;
	int                     rc = DER_SUCCESS;

	rc_abt = ABT_mutex_free(&dpm->stream_mutex);
	if (rc_abt != ABT_SUCCESS) {
		rc = dss_abterr2der(rc_abt);
		D_ERROR("Failed to free the stream synchronization mutex: " DF_RC "\n", DP_RC(rc));
	}

	D_FREE(dpm);
	memset(dp, 0, sizeof(*dp));

	return rc;
}

/**
 * Just print.
 */
static int
dlck_printf_worker(struct dlck_print *dp, const char *fmt, ...)
{
	FILE   *stream = dp->printf_custom;
	va_list args;
	int     rc;

	va_start(args, fmt);
	rc = dlck_vprintf_internal(stream, fmt, args);
	va_end(args);

	return rc;
}

int
dlck_print_worker_init(struct dlck_options *options, const char *log_dir, uuid_t po_uuid,
		       int tgt_id, struct dlck_print *main_dp, struct dlck_print *dp)
{
	char *log_file;
	FILE *stream;
	int   rc;

	/** open the logfile */
	D_ASPRINTF(log_file, "%s/" DF_UUIDF "_%s%d", log_dir, DP_UUID(po_uuid), VOS_FILE, tgt_id);
	if (log_file == NULL) {
		rc = -DER_NOMEM;
		DLCK_PRINTFL_RC(main_dp, rc, "[%d] Log file path allocation failed", tgt_id);
		/**
		 * It is very unlikely we can continue work without an ability to allocate more
		 * memory.
		 */
		return rc;
	}

	stream = fopen(log_file, "w");
	if (stream == NULL) {
		rc = daos_errno2der(errno);
		DLCK_PRINTFL_RC(main_dp, rc, "[%d] Log file open failed: %s", tgt_id, log_file);
		D_FREE(log_file);
		return rc;
	}
	D_FREE(log_file);

	memset(dp, 0, sizeof(*dp));
	dp->options       = options;
	dp->dp_printf     = dlck_printf_worker;
	dp->printf_custom = stream;

	return DER_SUCCESS;
}

void
dlck_print_worker_fini(struct dlck_print *dp)
{
	FILE *stream = dp->printf_custom;

	(void)fclose(stream);
	memset(dp, 0, sizeof(*dp));
}
