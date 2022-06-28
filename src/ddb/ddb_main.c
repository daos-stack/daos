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

#define error_msg_write_mode_only "Can only modify the VOS tree in 'write mode'\n"

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
run_cmd(struct ddb_ctx *ctx, const char *cmd_str, bool write_mode)
{
	struct argv_parsed	 parse_args = {0};
	struct ddb_cmd_info	 info = {0};
	int			 rc;
	char			*cmd_copy = strdup(cmd_str);

	/* Remove newline if needed */
	if (cmd_copy[strlen(cmd_copy) - 1] == '\n')
		cmd_copy[strlen(cmd_copy) - 1] = '\0';

	rc = ddb_str2argv_create(cmd_copy, &parse_args);
	if (!SUCCESS(rc))
		D_GOTO(done, rc);

	if (parse_args.ap_argc == 0) {
		D_ERROR("Nothing parsed\n");
		return -DER_INVAL;
	}

	rc = ddb_parse_cmd_args(ctx, parse_args.ap_argc, parse_args.ap_argv, &info);
	if (!SUCCESS(rc))
		D_GOTO(done, rc);
	switch (info.dci_cmd) {
	case DDB_CMD_UNKNOWN:
		ddb_error(ctx, "Unknown command\n");
		ddb_run_help(ctx);
		rc = -DER_INVAL;
		break;
	case DDB_CMD_HELP:
		rc = ddb_run_help(ctx);
		break;
	case DDB_CMD_QUIT:
		rc = ddb_run_quit(ctx);
		break;
	case DDB_CMD_OPEN:
		rc = ddb_run_open(ctx, &info.dci_cmd_option.dci_open);
		break;
	case DDB_CMD_CLOSE:
		rc = ddb_run_close(ctx);
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
		/* The 'rm' command deletes branches of the tree */
		if (!write_mode) {
			ddb_print(ctx, error_msg_write_mode_only);
			rc = -DER_INVAL;
			break;
		}
		rc = ddb_run_rm(ctx, &info.dci_cmd_option.dci_rm);
		break;
	case DDB_CMD_DUMP_DTX:
		rc = ddb_run_dump_dtx(ctx, &info.dci_cmd_option.dci_dump_dtx);
		break;
	case DDB_CMD_LOAD:
		/*
		 * The load command loads alters the tree by modifying an existing value or
		 * creating a new one.
		 */
		if (!write_mode) {
			ddb_print(ctx, error_msg_write_mode_only);
			rc = -DER_INVAL;
			break;
		}
		rc = ddb_run_load(ctx, &info.dci_cmd_option.dci_load);
		break;
	case DDB_CMD_COMMIT_ILOG:
		rc = ddb_run_commit_ilog(ctx, &info.dci_cmd_option.dci_commit_ilog);
		break;
	case DDB_CMD_RM_ILOG:
		rc = ddb_run_rm_ilog(ctx, &info.dci_cmd_option.dci_rm_ilog);
		break;
	case DDB_CMD_CLEAR_CMT_DTX:
		/*
		 * The clear_cmt_dtx command removes dtx entries.
		 */
		if (!write_mode) {
			ddb_print(ctx, error_msg_write_mode_only);
			rc = -DER_INVAL;
			break;
		}
		rc = ddb_run_clear_cmt_dtx(ctx, &info.dci_cmd_option.dci_clear_cmt_dtx);
		break;
	case DDB_CMD_SMD_SYNC:
		rc = ddb_run_smd_sync(ctx);
		break;
	case DDB_CMD_DUMP_VEA:
		rc = ddb_run_dump_vea(ctx);
		break;
	case DDB_CMD_UPDATE_VEA:
		rc = ddb_run_update_vea(ctx, &info.dci_cmd_option.dci_update_vea);
		break;
	}
done:
	ddb_str2argv_free(&parse_args);
	D_FREE(cmd_copy);

	return rc;
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
	return run_cmd(ctx, line, ctx->dc_write_mode);
}

#define str_has_value(str) ((str) != NULL && strlen(str) > 0)

int
ddb_main(struct ddb_io_ft *io_ft, int argc, char *argv[])
{
	struct program_args	 pa = {0};
	uint32_t		 input_buf_len = 1024;
	char			*input_buf;
	int			 rc;
	struct ddb_ctx		 ctx = {0};

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

	if (str_has_value(pa.pa_pool_path)) {
		rc = dv_pool_open(pa.pa_pool_path, &ctx.dc_poh);
		if (!SUCCESS(rc))
			D_GOTO(done, rc);
	}

	if (str_has_value(pa.pa_r_cmd_run)) {
		rc = run_cmd(&ctx, pa.pa_r_cmd_run, pa.pa_write_mode);
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

		rc = run_cmd(&ctx, input_buf, pa.pa_write_mode);
		if (!SUCCESS(rc)) {
			D_ERROR("Command '%s' failed: "DF_RC"\n", input_buf, DP_RC(rc));
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
