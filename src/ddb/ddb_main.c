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

	return rc;
}

void
ddb_fini()
{
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
		ddb_print(ctx, "Unknown command\n");
		ddb_run_help(ctx);
		rc = -DER_INVAL;
		break;
	case DDB_CMD_HELP:
		rc = ddb_run_help(ctx);
		break;
	case DDB_CMD_QUIT:
		rc = ddb_run_quit(ctx);
		break;
	case DDB_CMD_LS:
		rc = ddb_run_ls(ctx, &info.dci_cmd_option.dci_ls);
		break;
	case DDB_CMD_DUMP_SUPERBLOCK:
		rc = ddb_run_dump_superblock(ctx);
		break;
	case DDB_CMD_DUMP_ILOG:
		rc = ddb_run_dump_ilog(ctx, &info.dci_cmd_option.dci_dump_ilog);
		break;
	case DDB_CMD_DUMP_VALUE:
		rc = ddb_run_dump_value(ctx, &info.dci_cmd_option.dci_dump_value);
		break;
	case DDB_CMD_RM:
		rc = ddb_run_rm(ctx, &info.dci_cmd_option.dci_rm);
		break;
	case DDB_CMD_DUMP_DTX:
		rc = ddb_run_dump_dtx(ctx, &info.dci_cmd_option.dci_dump_dtx);
		break;
	case DDB_CMD_LOAD:
		rc = ddb_run_load(ctx, &info.dci_cmd_option.dci_load);
		break;
	case DDB_CMD_PROCESS_ILOG:
		rc = ddb_run_process_ilog(ctx, &info.dci_cmd_option.dci_process_ilog);
		break;
	case DDB_CMD_RM_ILOG:
		rc = ddb_run_rm_ilog(ctx, &info.dci_cmd_option.dci_rm_ilog);
		break;
	case DDB_CMD_CLEAR_DTX:
		rc = ddb_run_clear_dtx(ctx, &info.dci_cmd_option.dci_clear_dtx);
		break;
	}

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
	char			*buf;
	char			*input_buf;
	struct argv_parsed	 parse_args = {0};
	int			 rc = 0;
	struct ddb_ctx		 ctx = {0};

	D_ASSERT(io_ft);
	ctx.dc_io_ft = *io_ft;

	D_ALLOC(buf, buf_len + 1024);
	if (buf == NULL)
		return -DER_NOMEM;
	D_ALLOC(input_buf, input_buf_len);
	if (input_buf == NULL) {
		D_FREE(buf);
		return -DER_NOMEM;
	}

	rc = ddb_parse_program_args(&ctx, argc, argv, &pa);
	if (!SUCCESS(rc))
		D_GOTO(done, rc);

	if (str_has_value(pa.pa_pool_path)) {
		rc = ddb_vos_pool_open(pa.pa_pool_path, &ctx.dc_poh);
		if (!SUCCESS(rc))
			D_GOTO(done, rc);
	}

	if (str_has_value(pa.pa_r_cmd_run)) {
		/* Add program name back */
		snprintf(buf, buf_len, "%s %s", argv[0], pa.pa_r_cmd_run);
		rc = ddb_str2argv_create(buf, &parse_args);
		if (!SUCCESS(rc)) {
			ddb_vos_pool_close(ctx.dc_poh);
			D_GOTO(done, rc);
		}

		rc = run_cmd(&ctx, &parse_args);

		ddb_str2argv_free(&parse_args);
		D_GOTO(done, rc);
	}

	if (str_has_value(pa.pa_cmd_file)) {
		/* Still to be implemented */
		D_GOTO(done, rc = -DER_NOSYS);
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
		ddb_str2argv_free(&parse_args);
	}

done:
	D_FREE(buf);
	D_FREE(input_buf);

	return rc;
}
