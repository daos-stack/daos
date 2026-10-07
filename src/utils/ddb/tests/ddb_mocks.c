/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdbool.h>
#include <stdio.h>

#include <gurt/common.h>

#include "ddb_mocks.h"

int
__real_vos_dtx_discard_invalid(daos_handle_t coh, struct dtx_id *dti, int *discarded);

static vos_dtx_discard_invalid_fn_t mock_vos_dtx_discard_invalid = __real_vos_dtx_discard_invalid;

void
mock_vos_dtx_discard_invalid_set(vos_dtx_discard_invalid_fn_t mock_fn)
{
	mock_vos_dtx_discard_invalid = __real_vos_dtx_discard_invalid;
	if (mock_fn != NULL)
		mock_vos_dtx_discard_invalid = mock_fn;
}

int
__wrap_vos_dtx_discard_invalid(daos_handle_t coh, struct dtx_id *dti, int *discarded)
{
	D_ASSERT(mock_vos_dtx_discard_invalid != NULL);
	return mock_vos_dtx_discard_invalid(coh, dti, discarded);
}

int
__real_dwa_can_proceed(struct ddb_ctx *ctx, const char *nvme_conf_dir, bool *can_proceed);

static dwa_can_proceed_fn_t mock_dwa_can_proceed = __real_dwa_can_proceed;

void
mock_dwa_can_proceed_set(dwa_can_proceed_fn_t mock_fn)
{
	mock_dwa_can_proceed = __real_dwa_can_proceed;
	if (mock_fn != NULL)
		mock_dwa_can_proceed = mock_fn;
}

int
__wrap_dwa_can_proceed(struct ddb_ctx *ctx, const char *nvme_conf_dir, bool *can_proceed)
{
	D_ASSERT(mock_dwa_can_proceed != NULL);
	return mock_dwa_can_proceed(ctx, nvme_conf_dir, can_proceed);
}

int
__real_ddb_parse_vos_file_parts(const char *vos_path, const char *db_path,
				struct vos_file_parts *vos_file_parts);

static ddb_parse_vos_file_parts_fn_t mock_ddb_parse_vos_file_parts =
    __real_ddb_parse_vos_file_parts;

void
mock_ddb_parse_vos_file_parts_set(ddb_parse_vos_file_parts_fn_t mock_fn)
{
	mock_ddb_parse_vos_file_parts = __real_ddb_parse_vos_file_parts;
	if (mock_fn != NULL)
		mock_ddb_parse_vos_file_parts = mock_fn;
}

int
__wrap_ddb_parse_vos_file_parts(const char *vos_path, const char *db_path,
				struct vos_file_parts *vos_file_parts)
{
	D_ASSERT(mock_ddb_parse_vos_file_parts != NULL);
	return mock_ddb_parse_vos_file_parts(vos_path, db_path, vos_file_parts);
}

static d_asprintf2_fn_t mock_d_asprintf2 = NULL;

void
mock_d_asprintf2_set(d_asprintf2_fn_t mock_fn)
{
	mock_d_asprintf2 = NULL;
	if (mock_fn != NULL)
		mock_d_asprintf2 = mock_fn;
}

char *
__wrap_d_asprintf2(int *rc, const char *fmt, ...)
{
	char   *buf = NULL;
	va_list ap;

	/*
	 * A test-installed mock never reads its own varargs (it only checks rc/fmt), so it's
	 * called without forwarding any of them.
	 */
	if (mock_d_asprintf2 != NULL)
		return mock_d_asprintf2(rc, fmt);

	/*
	 * No mock installed: forward straight to vasprintf(). d_asprintf2() itself has no
	 * va_list-accepting variant to delegate to, and re-forwarding a captured va_list as a
	 * vararg to another variadic function does not correctly pass through the original format
	 * arguments -- it would silently corrupt them.
	 *
	 */
	va_start(ap, fmt);
	*rc = vasprintf(&buf, fmt, ap);
	va_end(ap);
	if (*rc == -1)
		buf = NULL;
	return buf;
}

/* Test-installed dw_open()/dw_close() implementations; NULL selects the real functions. */
static dw_open_fn_t  mock_dw_open;
static dw_close_fn_t mock_dw_close;

void
mock_dw_open_set(dw_open_fn_t mock_fn)
{
	mock_dw_open = mock_fn;
}

void
mock_dw_close_set(dw_close_fn_t mock_fn)
{
	mock_dw_close = mock_fn;
}

int
__real_dw_open(const char *path, const char *db_path, struct ddb_ctx *ctx,
	       struct bio_meta_context **mc);

int
__wrap_dw_open(const char *path, const char *db_path, struct ddb_ctx *ctx,
	       struct bio_meta_context **mc)
{
	if (mock_dw_open != NULL)
		return mock_dw_open(path, db_path, ctx, mc);
	return __real_dw_open(path, db_path, ctx, mc);
}

int
__real_dw_close(struct bio_meta_context *mc);

int
__wrap_dw_close(struct bio_meta_context *mc)
{
	if (mock_dw_close != NULL)
		return mock_dw_close(mc);
	return __real_dw_close(mc);
}
