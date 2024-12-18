/**
 * (C) Copyright 2022-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/common.h>
#include <daos/object.h>
#include "ddb_main.h"
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

void
ddb_ctx_init(struct ddb_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));

	ctx->dc_io_ft.ddb_print_message = printf;
	ctx->dc_io_ft.ddb_print_error = print_error;
	ctx->dc_io_ft.ddb_get_input = get_input;
	ctx->dc_io_ft.ddb_write_file = write_file;
	ctx->dc_io_ft.ddb_read_file = read_file;
	ctx->dc_io_ft.ddb_get_file_size = get_file_size;
	ctx->dc_io_ft.ddb_get_file_exists = file_exists;
	ctx->dc_io_ft.ddb_get_lines = get_lines;
}

static bool
all_whitespace(const char *str, uint32_t str_len)
{
	int i;

	for (i = 0; i < str_len; i++) {
		if (!isspace(str[i]))
			return false;
	}
	return true;
}

static int
process_line_cb(void *cb_args, char *line, uint32_t line_len)
{
	struct ddb_ctx *ctx = cb_args;

	ddb_printf(ctx, "Command: %s", line);
	/* ignore empty lines */
	if (all_whitespace(line, line_len))
		return 0;
	return ddb_run_cmd(ctx, line);
}

#define str_has_value(str) ((str) != NULL && strlen(str) > 0)

static int
open_if_needed(struct ddb_ctx *ctx, struct program_args *pa, bool *open)
{
	int rc = 0;

	if (!str_has_value(pa->pa_pool_path)) {
		*open = false;
		goto out;
	}
	ctx->dc_pool_path = pa->pa_pool_path;

	if (str_has_value(pa->pa_r_cmd_run)) {
		rc = ddb_parse_cmd_str(ctx, pa->pa_r_cmd_run, open);
		if (rc)
			return rc;
	} else if (str_has_value(pa->pa_cmd_file)) {
		*open = true;
	} else {
		*open = false;
	}

out:
	return rc;
}

int
ddb_main(struct ddb_io_ft *io_ft, int argc, char *argv[])
{
	struct program_args	 pa = {0};
	uint32_t		 input_buf_len = 1024;
	char			*input_buf;
	int			 rc;
	struct ddb_ctx		 ctx = {0};
	bool                     open = true;

	D_ASSERT(io_ft);
	ctx.dc_io_ft = *io_ft;

	D_ALLOC(input_buf, input_buf_len);
	if (input_buf == NULL)
		return -DER_NOMEM;

	rc = ddb_parse_program_args(&ctx, argc, argv, &pa);
	if (!SUCCESS(rc))
		D_GOTO(done, rc);

	if (pa.pa_get_help) {
		ddb_program_help(&ctx);
		D_GOTO(done, rc);
	}

	ctx.dc_write_mode = pa.pa_write_mode;

	if (str_has_value(pa.pa_r_cmd_run) && str_has_value(pa.pa_cmd_file)) {
		ddb_print(&ctx, "Cannot use both '-R' and '-f'.\n");
		D_GOTO(done, rc = -DER_INVAL);
	}

	rc = open_if_needed(&ctx, &pa, &open);
	if (!SUCCESS(rc))
		D_GOTO(done, rc);
	if (open) {
		rc = dv_pool_open(pa.pa_pool_path, &ctx.dc_poh, 0);
		if (!SUCCESS(rc))
			D_GOTO(done, rc);
	}

	if (str_has_value(pa.pa_r_cmd_run)) {
		rc = ddb_run_cmd(&ctx, pa.pa_r_cmd_run);
		if (!SUCCESS(rc))
			D_ERROR("Command '%s' failed: "DF_RC"\n", input_buf, DP_RC(rc));
		D_GOTO(done, rc);
	}

	if (str_has_value(pa.pa_cmd_file)) {
		if (!io_ft->ddb_get_file_exists(pa.pa_cmd_file)) {
			ddb_errorf(&ctx, "Unable to access file: '%s'\n", pa.pa_cmd_file);
			D_GOTO(done, rc = -DER_INVAL);
		}

		rc = io_ft->ddb_get_lines(pa.pa_cmd_file, process_line_cb, &ctx);
		D_GOTO(done, rc);
	}

	while (!ctx.dc_should_quit) {
		io_ft->ddb_print_message("$ ");
		io_ft->ddb_get_input(input_buf, input_buf_len);

		/* Remove newline */
		if (input_buf[strlen(input_buf) - 1] == '\n')
			input_buf[strlen(input_buf) - 1] = '\0';

		rc = ddb_run_cmd(&ctx, input_buf);
		if (!SUCCESS(rc)) {
			D_ERROR("Command '%s' failed: "DF_RC"\n", input_buf, DP_RC(rc));
			ddb_printf(&ctx, "Command '%s' failed: "DF_RC"\n", input_buf, DP_RC(rc));
		}
	}

done:
	if (daos_handle_is_valid(ctx.dc_poh)) {
		int tmp_rc = dv_pool_close(ctx.dc_poh);

		if (rc == 0)
			rc = tmp_rc;
	}
	D_FREE(input_buf);

	return rc;
}
