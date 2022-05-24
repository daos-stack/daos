/**
* (C) Copyright 2022 Intel Corporation.
*
* SPDX-License-Identifier: BSD-2-Clause-Patent
*/
#include <ctype.h>
#include <getopt.h>
#include <gurt/debug.h>

#include "ddb_cmd_options.h"
#include "ddb_common.h"
#define same(a, b) (strcmp((a), (b)) == 0)
#define COMMAND_NAME_HELP "help"
#define COMMAND_NAME_QUIT "quit"
#define COMMAND_NAME_LS "ls"
#define COMMAND_NAME_DUMP_SUPERBLOCK "dump_superblock"
#define COMMAND_NAME_DUMP_VALUE "dump_value"
#define COMMAND_NAME_RM "rm"
#define COMMAND_NAME_LOAD "load"
#define COMMAND_NAME_DUMP_ILOG "dump_ilog"
#define COMMAND_NAME_PROCESS_ILOG "process_ilog"
#define COMMAND_NAME_RM_ILOG "rm_ilog"
#define COMMAND_NAME_DUMP_DTX "dump_dtx"
#define COMMAND_NAME_CLEAR_DTX "clear_dtx"

/* Parse command line options for the 'ls' command */
static int
ls_option_parse(struct ddb_ctx *ctx, struct ls_options *cmd_args, struct argv_parsed *argc_v)
{
	char		 *options_short = "r";
	int		  index = 0, opt;
	uint32_t	  argc = argc_v->ap_argc;
	char		**argv = argc_v->ap_argv;
	struct option	  options_long[] = {
		{ "recursive", no_argument, NULL, 'r' },
		{ NULL }
	};

	memset(cmd_args, 0, sizeof(*cmd_args));

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, options_short, options_long, &index)) != -1) {
		switch (opt) {
		case 'r':
			cmd_args->recursive = true;
			break;
		case '?':
			ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		default:
			return -DER_INVAL;
		}
	}

	index = optind;

	D_ASSERT(argc > index);
	D_ASSERT(same(argv[index], COMMAND_NAME_LS));
	index++;
	if (argc - index > 0) {
		cmd_args->path = argv[index];
		index++;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\\n", argv[index]);
		return -DER_INVAL;
	}

	return 0;
}

/* Parse command line options for the 'dump_value' command */
static int
dump_value_option_parse(struct ddb_ctx *ctx, struct dump_value_options *cmd_args,
			struct argv_parsed *argc_v)
{
	char		 *options_short = "";
	int		  index = 0, opt;
	uint32_t	  argc = argc_v->ap_argc;
	char		**argv = argc_v->ap_argv;
	struct option	  options_long[] = {
		{ NULL }
	};

	memset(cmd_args, 0, sizeof(*cmd_args));

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, options_short, options_long, &index)) != -1) {
		switch (opt) {
		case '?':
			ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		default:
			return -DER_INVAL;
		}
	}

	index = optind;

	D_ASSERT(argc > index);
	D_ASSERT(same(argv[index], COMMAND_NAME_DUMP_VALUE));
	index++;
	if (argc - index > 0) {
		cmd_args->path = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'path'");
		return -DER_INVAL;
	}
	if (argc - index > 0) {
		cmd_args->dst = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'dst'");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\\n", argv[index]);
		return -DER_INVAL;
	}

	return 0;
}

/* Parse command line options for the 'rm' command */
static int
rm_option_parse(struct ddb_ctx *ctx, struct rm_options *cmd_args, struct argv_parsed *argc_v)
{
	char		 *options_short = "";
	int		  index = 0, opt;
	uint32_t	  argc = argc_v->ap_argc;
	char		**argv = argc_v->ap_argv;
	struct option	  options_long[] = {
		{ NULL }
	};

	memset(cmd_args, 0, sizeof(*cmd_args));

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, options_short, options_long, &index)) != -1) {
		switch (opt) {
		case '?':
			ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		default:
			return -DER_INVAL;
		}
	}

	index = optind;

	D_ASSERT(argc > index);
	D_ASSERT(same(argv[index], COMMAND_NAME_RM));
	index++;
	if (argc - index > 0) {
		cmd_args->path = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'path'");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\\n", argv[index]);
		return -DER_INVAL;
	}

	return 0;
}

/* Parse command line options for the 'load' command */
static int
load_option_parse(struct ddb_ctx *ctx, struct load_options *cmd_args, struct argv_parsed *argc_v)
{
	char		 *options_short = "";
	int		  index = 0, opt;
	uint32_t	  argc = argc_v->ap_argc;
	char		**argv = argc_v->ap_argv;
	struct option	  options_long[] = {
		{ NULL }
	};

	memset(cmd_args, 0, sizeof(*cmd_args));

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, options_short, options_long, &index)) != -1) {
		switch (opt) {
		case '?':
			ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		default:
			return -DER_INVAL;
		}
	}

	index = optind;

	D_ASSERT(argc > index);
	D_ASSERT(same(argv[index], COMMAND_NAME_LOAD));
	index++;
	if (argc - index > 0) {
		cmd_args->src = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'src'");
		return -DER_INVAL;
	}
	if (argc - index > 0) {
		cmd_args->dst = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'dst'");
		return -DER_INVAL;
	}
	if (argc - index > 0) {
		cmd_args->epoch = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'epoch'");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\\n", argv[index]);
		return -DER_INVAL;
	}

	return 0;
}

/* Parse command line options for the 'dump_ilog' command */
static int
dump_ilog_option_parse(struct ddb_ctx *ctx, struct dump_ilog_options *cmd_args,
		       struct argv_parsed *argc_v)
{
	char		 *options_short = "";
	int		  index = 0, opt;
	uint32_t	  argc = argc_v->ap_argc;
	char		**argv = argc_v->ap_argv;
	struct option	  options_long[] = {
		{ NULL }
	};

	memset(cmd_args, 0, sizeof(*cmd_args));

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, options_short, options_long, &index)) != -1) {
		switch (opt) {
		case '?':
			ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		default:
			return -DER_INVAL;
		}
	}

	index = optind;

	D_ASSERT(argc > index);
	D_ASSERT(same(argv[index], COMMAND_NAME_DUMP_ILOG));
	index++;
	if (argc - index > 0) {
		cmd_args->path = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'path'");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\\n", argv[index]);
		return -DER_INVAL;
	}

	return 0;
}

/* Parse command line options for the 'process_ilog' command */
static int
process_ilog_option_parse(struct ddb_ctx *ctx, struct process_ilog_options *cmd_args,
			  struct argv_parsed *argc_v)
{
	char		 *options_short = "";
	int		  index = 0, opt;
	uint32_t	  argc = argc_v->ap_argc;
	char		**argv = argc_v->ap_argv;
	struct option	  options_long[] = {
		{ NULL }
	};

	memset(cmd_args, 0, sizeof(*cmd_args));

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, options_short, options_long, &index)) != -1) {
		switch (opt) {
		case '?':
			ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		default:
			return -DER_INVAL;
		}
	}

	index = optind;

	D_ASSERT(argc > index);
	D_ASSERT(same(argv[index], COMMAND_NAME_PROCESS_ILOG));
	index++;
	if (argc - index > 0) {
		cmd_args->path = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'path'");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\\n", argv[index]);
		return -DER_INVAL;
	}

	return 0;
}

/* Parse command line options for the 'rm_ilog' command */
static int
rm_ilog_option_parse(struct ddb_ctx *ctx, struct rm_ilog_options *cmd_args,
		     struct argv_parsed *argc_v)
{
	char		 *options_short = "";
	int		  index = 0, opt;
	uint32_t	  argc = argc_v->ap_argc;
	char		**argv = argc_v->ap_argv;
	struct option	  options_long[] = {
		{ NULL }
	};

	memset(cmd_args, 0, sizeof(*cmd_args));

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, options_short, options_long, &index)) != -1) {
		switch (opt) {
		case '?':
			ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		default:
			return -DER_INVAL;
		}
	}

	index = optind;

	D_ASSERT(argc > index);
	D_ASSERT(same(argv[index], COMMAND_NAME_RM_ILOG));
	index++;
	if (argc - index > 0) {
		cmd_args->path = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'path'");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\\n", argv[index]);
		return -DER_INVAL;
	}

	return 0;
}

/* Parse command line options for the 'dump_dtx' command */
static int
dump_dtx_option_parse(struct ddb_ctx *ctx, struct dump_dtx_options *cmd_args,
		      struct argv_parsed *argc_v)
{
	char		 *options_short = "ac";
	int		  index = 0, opt;
	uint32_t	  argc = argc_v->ap_argc;
	char		**argv = argc_v->ap_argv;
	struct option	  options_long[] = {
		{ "active", no_argument, NULL, 'a' },
		{ "committed", no_argument, NULL, 'c' },
		{ NULL }
	};

	memset(cmd_args, 0, sizeof(*cmd_args));

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, options_short, options_long, &index)) != -1) {
		switch (opt) {
		case 'a':
			cmd_args->active = true;
			break;
		case 'c':
			cmd_args->committed = true;
			break;
		case '?':
			ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		default:
			return -DER_INVAL;
		}
	}

	index = optind;

	D_ASSERT(argc > index);
	D_ASSERT(same(argv[index], COMMAND_NAME_DUMP_DTX));
	index++;
	if (argc - index > 0) {
		cmd_args->path = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'path'");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\\n", argv[index]);
		return -DER_INVAL;
	}

	return 0;
}

/* Parse command line options for the 'clear_dtx' command */
static int
clear_dtx_option_parse(struct ddb_ctx *ctx, struct clear_dtx_options *cmd_args,
		       struct argv_parsed *argc_v)
{
	char		 *options_short = "";
	int		  index = 0, opt;
	uint32_t	  argc = argc_v->ap_argc;
	char		**argv = argc_v->ap_argv;
	struct option	  options_long[] = {
		{ NULL }
	};

	memset(cmd_args, 0, sizeof(*cmd_args));

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, options_short, options_long, &index)) != -1) {
		switch (opt) {
		case '?':
			ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		default:
			return -DER_INVAL;
		}
	}

	index = optind;

	D_ASSERT(argc > index);
	D_ASSERT(same(argv[index], COMMAND_NAME_CLEAR_DTX));
	index++;
	if (argc - index > 0) {
		cmd_args->path = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'path'");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\\n", argv[index]);
		return -DER_INVAL;
	}

	return 0;
}

int
ddb_parse_cmd_args(struct ddb_ctx *ctx, struct argv_parsed *parsed, struct ddb_cmd_info *info)
{
	char *cmd = parsed->ap_argv[1];

	if (same(cmd, COMMAND_NAME_HELP)) {
		info->dci_cmd = DDB_CMD_HELP;
		return 0;
	}
	if (same(cmd, COMMAND_NAME_QUIT)) {
		info->dci_cmd = DDB_CMD_QUIT;
		return 0;
	}
	if (same(cmd, COMMAND_NAME_LS)) {
		info->dci_cmd = DDB_CMD_LS;
		return ls_option_parse(ctx, &info->dci_cmd_option.dci_ls, parsed);
	}
	if (same(cmd, COMMAND_NAME_DUMP_SUPERBLOCK)) {
		info->dci_cmd = DDB_CMD_DUMP_SUPERBLOCK;
		return 0;
	}
	if (same(cmd, COMMAND_NAME_DUMP_VALUE)) {
		info->dci_cmd = DDB_CMD_DUMP_VALUE;
		return dump_value_option_parse(ctx, &info->dci_cmd_option.dci_dump_value, parsed);
	}
	if (same(cmd, COMMAND_NAME_RM)) {
		info->dci_cmd = DDB_CMD_RM;
		return rm_option_parse(ctx, &info->dci_cmd_option.dci_rm, parsed);
	}
	if (same(cmd, COMMAND_NAME_LOAD)) {
		info->dci_cmd = DDB_CMD_LOAD;
		return load_option_parse(ctx, &info->dci_cmd_option.dci_load, parsed);
	}
	if (same(cmd, COMMAND_NAME_DUMP_ILOG)) {
		info->dci_cmd = DDB_CMD_DUMP_ILOG;
		return dump_ilog_option_parse(ctx, &info->dci_cmd_option.dci_dump_ilog, parsed);
	}
	if (same(cmd, COMMAND_NAME_PROCESS_ILOG)) {
		info->dci_cmd = DDB_CMD_PROCESS_ILOG;
		return process_ilog_option_parse(ctx, &info->dci_cmd_option.dci_process_ilog,
						 parsed);
	}
	if (same(cmd, COMMAND_NAME_RM_ILOG)) {
		info->dci_cmd = DDB_CMD_RM_ILOG;
		return rm_ilog_option_parse(ctx, &info->dci_cmd_option.dci_rm_ilog, parsed);
	}
	if (same(cmd, COMMAND_NAME_DUMP_DTX)) {
		info->dci_cmd = DDB_CMD_DUMP_DTX;
		return dump_dtx_option_parse(ctx, &info->dci_cmd_option.dci_dump_dtx, parsed);
	}
	if (same(cmd, COMMAND_NAME_CLEAR_DTX)) {
		info->dci_cmd = DDB_CMD_CLEAR_DTX;
		return clear_dtx_option_parse(ctx, &info->dci_cmd_option.dci_clear_dtx, parsed);
	}

	return -DER_INVAL;
}

int
ddb_run_help(struct ddb_ctx *ctx)
{
	ddb_print(ctx, "Usage:\n");
	ddb_print(ctx, "ddb [OPTIONS] <command>\n");
	ddb_print(ctx, "\n");
	ddb_print(ctx, "ddb (DAOS Debug) is a tool for connecting to a VOS file for "
		       "the purpose of investigating and resolving issues.\n");

	ddb_print(ctx, "\nOptions:\n");
	ddb_print(ctx, "   -R, --run_cmd\n");

	ddb_print(ctx, "\nCommands:\n");
	ddb_print(ctx, "   help\t\t\tShow this help message\n");
	ddb_print(ctx, "   quit\t\t\tExit interactive mode\n");
	ddb_print(ctx, "   ls\t\t\tList the containers, objects, dkeys, akeys, "
		       "or value depending\n");
	ddb_print(ctx, "   dump_superblock\tDump the pool superblock information\n");
	ddb_print(ctx, "   dump_value\t\tDump a value to a file\n");
	ddb_print(ctx, "   rm\t\t\tRemove a branch of the VOS tree\n");
	ddb_print(ctx, "   load\t\t\tLoad an updated or new value\n");
	ddb_print(ctx, "   dump_ilog\t\tDump the ilog\n");
	ddb_print(ctx, "   process_ilog\t\tProcess the ilog\n");
	ddb_print(ctx, "   rm_ilog\t\tRemove all the ilog entries\n");
	ddb_print(ctx, "   dump_dtx\t\tDump the dtx tables\n");
	ddb_print(ctx, "   clear_dtx\t\tClear the dtx committed table\n");

	return 0;
}
