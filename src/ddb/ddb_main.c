/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/common.h>
#include <daos/object.h>
#include "ddb_main.h"
#include "ddb_common.h"
#include "ddb_parse.h"
#include "ddb_vos.h"
#include "ddb_cmd_options.h"

int
ddb_init()
{
	int rc = daos_debug_init(DAOS_LOG_DEFAULT);

	if (!SUCCESS(rc))
		return rc;

	rc = obj_class_init();

	return rc;
}

void
ddb_fini()
{
	obj_class_fini();
	daos_debug_fini();
}

static int
run_cmd(struct ddb_ctx *ctx, struct argv_parsed *parse_args)
{
	struct ddb_cmd_info info = {0};
	int rc = 0;

	ddb_parse_cmd_args(ctx, parse_args, &info);

	switch (info.dci_cmd) {
	case DDB_CMD_UNKNOWN:
		break;
	case DDB_CMD_QUIT:
		rc = ddb_run_quit(ctx);
		break;
	case DDB_CMD_LS:
		rc = ddb_run_ls(ctx, &info.dci_cmd_option.dci_ls);
		break;
	}

	if (!SUCCESS(rc))
		ddb_errorf(ctx, "Error with command: "DF_RC"\n", DP_RC(rc));

	ddb_str2argv_free(parse_args);

	return rc;
}

#define str_has_value(str) ((str) != NULL && strlen(str) > 0)

int
ddb_main(struct ddb_io_ft *io_ft, int argc, char *argv[])
{
	struct program_args	 pa = {0};
	uint32_t		 input_buf_len = 1024;
	uint32_t		 buf_len = input_buf_len * 2;
	char			 buf[buf_len + 1024];
	char			 input_buf[input_buf_len];
	struct argv_parsed	 parse_args = {0};
	int			 rc;
	struct ddb_ctx		 ctx = {0};

	D_ASSERT(io_ft);
	ctx.dc_io_ft = *io_ft;

	rc = ddb_parse_program_args(argc, argv, &pa);
	if (!SUCCESS(rc))
		return rc;

	if (str_has_value(pa.pa_pool_path)) {
		rc = ddb_vos_pool_open(pa.pa_pool_path, &ctx.dc_poh);
		if (!SUCCESS(rc))
			return rc;
	}

	if (str_has_value(pa.pa_r_cmd_run)) {
		/* Add program name back */
		snprintf(buf, buf_len, "%s %s", argv[0], pa.pa_r_cmd_run);
		rc = ddb_str2argv_create(buf, &parse_args);
		if (!SUCCESS(rc)) {
			ddb_vos_pool_close(ctx.dc_poh);
			return rc;
		}

		rc = run_cmd(&ctx, &parse_args);
		if (!SUCCESS(rc))
			ddb_errorf(&ctx, "Error with command: "DF_RC"\n", DP_RC(rc));

		ddb_str2argv_free(&parse_args);
		return rc;
	}

	if (str_has_value(pa.pa_cmd_file)) {
		/* Still to be implemented */

		return -DER_NOSYS;
	}

	while (!ctx.dc_should_quit) {
		io_ft->ddb_print_message("$ ");
		io_ft->ddb_get_input(input_buf, input_buf_len);
		input_buf[strlen(input_buf) - 1] = '\0'; /* Remove newline */

		/* add program name to beginning of string that will be parsed into argv. That way
		 * is the same as argv from command line into main()
		 */
		snprintf(buf, buf_len, "%s %s", argv[0], input_buf);
		rc = ddb_str2argv_create(buf, &parse_args);
		if (!SUCCESS(rc)) {
			ddb_errorf(&ctx, "Error with input: "DF_RC"\n", DP_RC(rc));
			ddb_str2argv_free(&parse_args);
			continue;
		}

		rc = run_cmd(&ctx, &parse_args);
		if (!SUCCESS(rc))
			ddb_errorf(&ctx, "Error: "DF_RC"\n", DP_RC(rc));
		ddb_str2argv_free(&parse_args);
	}

	ddb_fini();
	return 0;
}
