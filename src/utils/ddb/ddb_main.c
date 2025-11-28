/**
 * (C) Copyright 2022-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 * (C) Copyright 2025 Vdura Inc.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/common.h>
#include <daos/object.h>
#include "ddb_common.h"
#include "ddb_parse.h"
#include "ddb_vos.h"
#include "ddb.h"
#include <stdarg.h>
#include <sys/stat.h>

int
ddb_init()
{
	int rc = daos_debug_init(DAOS_LOG_DEFAULT);

	return rc;
}

void
ddb_fini()
{
	daos_debug_fini();
}

/* Default implementations */

static int
print_error(const char *fmt, ...)
{
	va_list args;
	int	rc;

	va_start(args, fmt);
	rc = vfprintf(stderr, fmt, args);
	va_end(args);

	return rc;
}

static int
write_file(const char *dst_path, d_iov_t *contents)
{
	FILE *f;
	int rc;

	f = fopen(dst_path, "w");
	if (f == NULL) {
		rc = daos_errno2der(errno);
		print_error("Unable to open path '%s': "DF_RC"\n", dst_path, DP_RC(rc));
		return rc;
	}

	fwrite(contents->iov_buf, 1, contents->iov_len, f);

	fclose(f);

	return 0;
}

static size_t
get_file_size(const char *path)
{
	struct stat st;

	if (stat(path, &st) == 0)
		return st.st_size;

	return -DER_INVAL;
}

static size_t
read_file(const char *path, d_iov_t *contents)
{
	FILE	*f;
	int	 rc;
	size_t	 result;

	f = fopen(path, "r");
	if (f == NULL) {
		rc = daos_errno2der(errno);
		print_error("Unable to open path '%s': "DF_RC"\n", path, DP_RC(rc));
		return rc;
	}

	result = fread(contents->iov_buf, 1, contents->iov_buf_len, f);

	fclose(f);

	contents->iov_len = result;

	return result;
}

static bool
file_exists(const char *path)
{
	return access(path, F_OK) == 0;
}

void
ddb_ctx_init(struct ddb_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));

	ctx->dc_io_ft.ddb_print_message   = printf;
	ctx->dc_io_ft.ddb_print_error     = print_error;
	ctx->dc_io_ft.ddb_write_file      = write_file;
	ctx->dc_io_ft.ddb_read_file       = read_file;
	ctx->dc_io_ft.ddb_get_file_size   = get_file_size;
	ctx->dc_io_ft.ddb_get_file_exists = file_exists;
}
