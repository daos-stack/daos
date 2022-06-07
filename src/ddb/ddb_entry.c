/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdio.h>
#include <daos_types.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "ddb_main.h"

static char *
get_input(char *buf, uint32_t buf_len)
{
	return fgets(buf, buf_len, stdin);
}

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

static int
get_lines(const char *path, ddb_io_line_cb line_cb, void *cb_args)
{
	FILE	*f;
	char	*line = NULL;
	uint64_t len = 0;
	uint64_t read;
	int	 rc = 0;

	f = fopen(path, "r");
	if (f == NULL) {
		rc = daos_errno2der(errno);
		print_error("Unable to open path '%s': "DF_RC"\n", path, DP_RC(rc));
		return rc;
	}

	while ((read = getline(&line, &len, f)) != -1) {
		rc = line_cb(cb_args, line, read);
		if (!SUCCESS(rc)) {
			print_error("Issue with line '%s': "DF_RC"\n", line, DP_RC(rc));
			break;
		}
	}

	rc = daos_errno2der(errno);
	if (!SUCCESS(rc))
		print_error("Error reading line from file '%s': "DF_RC"\n", path, DP_RC(rc));

	fclose(f);
	if (line)
		D_FREE(line);

	return rc;
}

int main(int argc, char *argv[])
{
	int rc;
	struct ddb_io_ft ft = {
		.ddb_print_message = printf,
		.ddb_print_error = print_error,
		.ddb_get_input = get_input,
		.ddb_write_file = write_file,
		.ddb_read_file = read_file,
		.ddb_get_file_size = get_file_size,
		.ddb_get_file_exists = file_exists,
		.ddb_get_lines = get_lines,
	};

	rc = ddb_init();
	if (rc != 0) {
		fprintf(stderr, "Error with ddb_init: "DF_RC"\n", DP_RC(rc));
		return -rc;
	}
	rc = ddb_main(&ft, argc, argv);
	if (rc != 0)
		fprintf(stderr, "Error: "DF_RC"\n", DP_RC(rc));

	ddb_fini();

	return -rc;
}
