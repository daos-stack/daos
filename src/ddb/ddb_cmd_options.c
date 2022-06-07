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
#define COMMAND_NAME_OPEN "open"
#define COMMAND_NAME_CLOSE "close"
#define COMMAND_NAME_DUMP_SUPERBLOCK "dump_superblock"
#define COMMAND_NAME_DUMP_VALUE "dump_value"
#define COMMAND_NAME_RM "rm"
#define COMMAND_NAME_LOAD "load"
#define COMMAND_NAME_DUMP_ILOG "dump_ilog"
#define COMMAND_NAME_COMMIT_ILOG "commit_ilog"
#define COMMAND_NAME_RM_ILOG "rm_ilog"
#define COMMAND_NAME_DUMP_DTX "dump_dtx"
#define COMMAND_NAME_CLEAR_CMT_DTX "clear_cmt_dtx"
#define COMMAND_NAME_SMD_SYNC "smd_sync"

/* Parse command line options for the 'ls' command */
static int
ls_option_parse(struct ddb_ctx *ctx, struct ls_options *cmd_args,
		uint32_t argc, char **argv)
{
	char		 *options_short = "r";
	int		  index = 0, opt;
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

/* Parse command line options for the 'open' command */
static int
open_option_parse(struct ddb_ctx *ctx, struct open_options *cmd_args,
		  uint32_t argc, char **argv)
{
	char		 *options_short = "w";
	int		  index = 0, opt;
	struct option	  options_long[] = {
		{ "write_mode", no_argument, NULL, 'w' },
		{ NULL }
	};

	memset(cmd_args, 0, sizeof(*cmd_args));

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, options_short, options_long, &index)) != -1) {
		switch (opt) {
		case 'w':
			cmd_args->write_mode = true;
			break;
		case '?':
			ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		default:
			return -DER_INVAL;
		}
	}

	index = optind;
	if (argc - index > 0) {
		cmd_args->vos_pool_shard = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'vos_pool_shard'");
		return -DER_INVAL;
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
			uint32_t argc, char **argv)
{
	char		 *options_short = "";
	int		  index = 0;
	struct option	  options_long[] = {
		{ NULL }
	};

	memset(cmd_args, 0, sizeof(*cmd_args));

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	if (getopt_long(argc, argv, options_short, options_long, &index) != -1) {
		ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		return -DER_INVAL;
	}

	index = optind;
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
rm_option_parse(struct ddb_ctx *ctx, struct rm_options *cmd_args,
		uint32_t argc, char **argv)
{
	char		 *options_short = "";
	int		  index = 0;
	struct option	  options_long[] = {
		{ NULL }
	};

	memset(cmd_args, 0, sizeof(*cmd_args));

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	if (getopt_long(argc, argv, options_short, options_long, &index) != -1) {
		ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		return -DER_INVAL;
	}

	index = optind;
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
load_option_parse(struct ddb_ctx *ctx, struct load_options *cmd_args,
		  uint32_t argc, char **argv)
{
	char		 *options_short = "";
	int		  index = 0;
	struct option	  options_long[] = {
		{ NULL }
	};

	memset(cmd_args, 0, sizeof(*cmd_args));

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	if (getopt_long(argc, argv, options_short, options_long, &index) != -1) {
		ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		return -DER_INVAL;
	}

	index = optind;
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
		       uint32_t argc, char **argv)
{
	char		 *options_short = "";
	int		  index = 0;
	struct option	  options_long[] = {
		{ NULL }
	};

	memset(cmd_args, 0, sizeof(*cmd_args));

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	if (getopt_long(argc, argv, options_short, options_long, &index) != -1) {
		ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		return -DER_INVAL;
	}

	index = optind;
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

/* Parse command line options for the 'commit_ilog' command */
static int
commit_ilog_option_parse(struct ddb_ctx *ctx, struct commit_ilog_options *cmd_args,
			 uint32_t argc, char **argv)
{
	char		 *options_short = "";
	int		  index = 0;
	struct option	  options_long[] = {
		{ NULL }
	};

	memset(cmd_args, 0, sizeof(*cmd_args));

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	if (getopt_long(argc, argv, options_short, options_long, &index) != -1) {
		ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		return -DER_INVAL;
	}

	index = optind;
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
		     uint32_t argc, char **argv)
{
	char		 *options_short = "";
	int		  index = 0;
	struct option	  options_long[] = {
		{ NULL }
	};

	memset(cmd_args, 0, sizeof(*cmd_args));

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	if (getopt_long(argc, argv, options_short, options_long, &index) != -1) {
		ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		return -DER_INVAL;
	}

	index = optind;
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
		      uint32_t argc, char **argv)
{
	char		 *options_short = "ac";
	int		  index = 0, opt;
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

/* Parse command line options for the 'clear_cmt_dtx' command */
static int
clear_cmt_dtx_option_parse(struct ddb_ctx *ctx, struct clear_cmt_dtx_options *cmd_args,
			   uint32_t argc, char **argv)
{
	char		 *options_short = "";
	int		  index = 0;
	struct option	  options_long[] = {
		{ NULL }
	};

	memset(cmd_args, 0, sizeof(*cmd_args));

	/* Restart getopt */
	optind = 1;
	opterr = 0;
	if (getopt_long(argc, argv, options_short, options_long, &index) != -1) {
		ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
		return -DER_INVAL;
	}

	index = optind;
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
ddb_parse_cmd_args(struct ddb_ctx *ctx, uint32_t argc, char **argv, struct ddb_cmd_info *info)
{
	char *cmd = argv[0];

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
		return ls_option_parse(ctx, &info->dci_cmd_option.dci_ls,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_OPEN)) {
		info->dci_cmd = DDB_CMD_OPEN;
		return open_option_parse(ctx, &info->dci_cmd_option.dci_open,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_CLOSE)) {
		info->dci_cmd = DDB_CMD_CLOSE;
		return 0;
	}
	if (same(cmd, COMMAND_NAME_DUMP_SUPERBLOCK)) {
		info->dci_cmd = DDB_CMD_DUMP_SUPERBLOCK;
		return 0;
	}
	if (same(cmd, COMMAND_NAME_DUMP_VALUE)) {
		info->dci_cmd = DDB_CMD_DUMP_VALUE;
		return dump_value_option_parse(ctx, &info->dci_cmd_option.dci_dump_value,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_RM)) {
		info->dci_cmd = DDB_CMD_RM;
		return rm_option_parse(ctx, &info->dci_cmd_option.dci_rm,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_LOAD)) {
		info->dci_cmd = DDB_CMD_LOAD;
		return load_option_parse(ctx, &info->dci_cmd_option.dci_load,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_DUMP_ILOG)) {
		info->dci_cmd = DDB_CMD_DUMP_ILOG;
		return dump_ilog_option_parse(ctx, &info->dci_cmd_option.dci_dump_ilog,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_COMMIT_ILOG)) {
		info->dci_cmd = DDB_CMD_COMMIT_ILOG;
		return commit_ilog_option_parse(ctx, &info->dci_cmd_option.dci_commit_ilog,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_RM_ILOG)) {
		info->dci_cmd = DDB_CMD_RM_ILOG;
		return rm_ilog_option_parse(ctx, &info->dci_cmd_option.dci_rm_ilog,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_DUMP_DTX)) {
		info->dci_cmd = DDB_CMD_DUMP_DTX;
		return dump_dtx_option_parse(ctx, &info->dci_cmd_option.dci_dump_dtx,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_CLEAR_CMT_DTX)) {
		info->dci_cmd = DDB_CMD_CLEAR_CMT_DTX;
		return clear_cmt_dtx_option_parse(ctx, &info->dci_cmd_option.dci_clear_cmt_dtx,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_SMD_SYNC)) {
		info->dci_cmd = DDB_CMD_SMD_SYNC;
		return 0;
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
	ddb_print(ctx, "   -w, --write_mode\n");
	ddb_print(ctx, "   -R, --run_cmd\n");
	ddb_print(ctx, "   -f, --file_cmd\n");

	ddb_print(ctx, "Commands:\n");
	ddb_print(ctx, "   help              Show this help message\n");
	ddb_print(ctx, "   quit              Quit interactive mode\n");
	ddb_print(ctx, "   ls                List containers, objects, dkeys, akeys, and values\n");
	ddb_print(ctx, "   open              Opens the vos_pool_shard\n");
	ddb_print(ctx, "   close             Close the currently opened vos pool shard\n");
	ddb_print(ctx, "   dump_superblock   Dump the pool superblock information\n");
	ddb_print(ctx, "   dump_value        Dump a value to a file\n");
	ddb_print(ctx, "   rm                Remove a branch of the VOS tree\n");
	ddb_print(ctx, "   load              Load an updated or new value\n");
	ddb_print(ctx, "   dump_ilog         Dump the ilog\n");
	ddb_print(ctx, "   commit_ilog       Process the ilog\n");
	ddb_print(ctx, "   rm_ilog           Remove all the ilog entries\n");
	ddb_print(ctx, "   dump_dtx          Dump the dtx tables\n");
	ddb_print(ctx, "   clear_cmt_dtx     Clear the dtx committed table\n");
	ddb_print(ctx, "   smd_sync          Restore the SMD file with backup from blob\n");

	return 0;
}
