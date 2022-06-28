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
#define COMMAND_NAME_DUMP_VEA "dump_vea"
#define COMMAND_NAME_UPDATE_VEA "update_vea"

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
		ddb_printf(ctx, "Unexpected argument: %s\n", argv[index]);
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
		cmd_args->path = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'path'\n");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\n", argv[index]);
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
		ddb_print(ctx, "Expected argument 'path'\n");
		return -DER_INVAL;
	}
	if (argc - index > 0) {
		cmd_args->dst = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'dst'\n");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\n", argv[index]);
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
		ddb_print(ctx, "Expected argument 'path'\n");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\n", argv[index]);
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
		ddb_print(ctx, "Expected argument 'src'\n");
		return -DER_INVAL;
	}
	if (argc - index > 0) {
		cmd_args->dst = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'dst'\n");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\n", argv[index]);
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
		ddb_print(ctx, "Expected argument 'path'\n");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\n", argv[index]);
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
		ddb_print(ctx, "Expected argument 'path'\n");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\n", argv[index]);
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
		ddb_print(ctx, "Expected argument 'path'\n");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\n", argv[index]);
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
		ddb_print(ctx, "Expected argument 'path'\n");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\n", argv[index]);
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
		ddb_print(ctx, "Expected argument 'path'\n");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\n", argv[index]);
		return -DER_INVAL;
	}

	return 0;
}

/* Parse command line options for the 'update_vea' command */
static int
update_vea_option_parse(struct ddb_ctx *ctx, struct update_vea_options *cmd_args,
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
		cmd_args->offset = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'offset'\n");
		return -DER_INVAL;
	}
	if (argc - index > 0) {
		cmd_args->blk_cnt = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'blk_cnt'\n");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\n", argv[index]);
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
	if (same(cmd, COMMAND_NAME_DUMP_VEA)) {
		info->dci_cmd = DDB_CMD_DUMP_VEA;
		return 0;
	}
	if (same(cmd, COMMAND_NAME_UPDATE_VEA)) {
		info->dci_cmd = DDB_CMD_UPDATE_VEA;
		return update_vea_option_parse(ctx, &info->dci_cmd_option.dci_update_vea,
		       argc, argv);
	}

	ddb_errorf(ctx, "'%s' is not a valid command. Available commands are:"
			"'help', "
			"'quit', "
			"'ls', "
			"'open', "
			"'close', "
			"'dump_superblock', "
			"'dump_value', "
			"'rm', "
			"'load', "
			"'dump_ilog', "
			"'commit_ilog', "
			"'rm_ilog', "
			"'dump_dtx', "
			"'clear_cmt_dtx', "
			"'smd_sync', "
			"'dump_vea', "
			"'update_vea'\n", cmd);

	return -DER_INVAL;
}

void
ddb_commands_help(struct ddb_ctx *ctx)
{
	/* Command: help */
	ddb_print(ctx, "help\n");
	ddb_print(ctx, "\tShow help message for all the commands.\n");
	ddb_print(ctx, "\n");

	/* Command: quit */
	ddb_print(ctx, "quit\n");
	ddb_print(ctx, "\tQuit interactive mode\n");
	ddb_print(ctx, "\n");

	/* Command: ls */
	ddb_print(ctx, "ls [path]\n");
	ddb_print(ctx, "\tList containers, objects, dkeys, akeys, and values\n");
	ddb_print(ctx, "    [path]\n");
	ddb_print(ctx, "\tOptional, list contents of the provided path\n");
	ddb_print(ctx, "Options:\n");
	ddb_print(ctx, "    -r, --recursive\n");
	ddb_print(ctx, "\tRecursively list the contents of the path\n");
	ddb_print(ctx, "\n");

	/* Command: open */
	ddb_print(ctx, "open <path>\n");
	ddb_print(ctx, "\tOpens the vos file at <path>\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tPath to the vos file to open. This should be an absolute path to the\n"
		       "\tpool shard. Part of the path is used to determine what the pool uuid\n"
		       "\tis.\n");
	ddb_print(ctx, "Options:\n");
	ddb_print(ctx, "    -w, --write_mode\n");
	ddb_print(ctx, "\tOpen the vos file in write mode. This allows for modifying the vos\n"
		       "\tfile with the load, commit_ilog, etc commands.\n");
	ddb_print(ctx, "\n");

	/* Command: close */
	ddb_print(ctx, "close\n");
	ddb_print(ctx, "\tClose the currently opened vos pool shard\n");
	ddb_print(ctx, "\n");

	/* Command: dump_superblock */
	ddb_print(ctx, "dump_superblock\n");
	ddb_print(ctx, "\tDump the pool superblock information\n");
	ddb_print(ctx, "\n");

	/* Command: dump_value */
	ddb_print(ctx, "dump_value <path> <dst>\n");
	ddb_print(ctx, "\tDump a value to a file\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tVOS tree path to dump. Should be a complete path including the akey\n"
		       "\tand if the value is an array value it should include the extent.\n");
	ddb_print(ctx, "    <dst>\n");
	ddb_print(ctx, "\tFile path to dump the value to.\n");
	ddb_print(ctx, "\n");

	/* Command: rm */
	ddb_print(ctx, "rm <path>\n");
	ddb_print(ctx, "\tRemove a branch of the VOS tree. The branch can be anything from a\n"
		       "\tcontainer and everything under it, to a single value.\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tVOS tree path to remove.\n");
	ddb_print(ctx, "\n");

	/* Command: load */
	ddb_print(ctx, "load <src> <dst>\n");
	ddb_print(ctx, "\tLoad a value to a vos path. This can be used to update the value of an\n"
		       "\texisting key, or create a new key.\n");
	ddb_print(ctx, "    <src>\n");
	ddb_print(ctx, "\tSource file path that contains the data for the value to load.\n");
	ddb_print(ctx, "    <dst>\n");
	ddb_print(ctx, "\tDestination vos tree path to the value where the data will be loaded.\n"
		       "\tIf the path currently exists, then the destination path must match the\n"
		       "\tvalue type, meaning, if the value type is an array, then the path must\n"
		       "\tinclude the extent, otherwise, it must not.\n");
	ddb_print(ctx, "\n");

	/* Command: dump_ilog */
	ddb_print(ctx, "dump_ilog <path>\n");
	ddb_print(ctx, "\tDump the ilog\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tVOS tree path to an object, dkey, or akey.\n");
	ddb_print(ctx, "\n");

	/* Command: commit_ilog */
	ddb_print(ctx, "commit_ilog <path>\n");
	ddb_print(ctx, "\tProcess the ilog\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tVOS tree path to an object, dkey, or akey.\n");
	ddb_print(ctx, "\n");

	/* Command: rm_ilog */
	ddb_print(ctx, "rm_ilog <path>\n");
	ddb_print(ctx, "\tRemove all the ilog entries\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tVOS tree path to an object, dkey, or akey.\n");
	ddb_print(ctx, "\n");

	/* Command: dump_dtx */
	ddb_print(ctx, "dump_dtx <path>\n");
	ddb_print(ctx, "\tDump the dtx tables\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tVOS tree path to a container.\n");
	ddb_print(ctx, "Options:\n");
	ddb_print(ctx, "    -a, --active\n");
	ddb_print(ctx, "\tOnly dump entries from the active table\n");
	ddb_print(ctx, "    -c, --committed\n");
	ddb_print(ctx, "\tOnly dump entries from the committed table\n");
	ddb_print(ctx, "\n");

	/* Command: clear_cmt_dtx */
	ddb_print(ctx, "clear_cmt_dtx <path>\n");
	ddb_print(ctx, "\tClear the dtx committed table\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tVOS tree path to a container.\n");
	ddb_print(ctx, "\n");

	/* Command: smd_sync */
	ddb_print(ctx, "smd_sync\n");
	ddb_print(ctx, "\tRestore the SMD file with backup from blob\n");
	ddb_print(ctx, "\n");

	/* Command: dump_vea */
	ddb_print(ctx, "dump_vea\n");
	ddb_print(ctx, "\tDump information from the vea tree about free regions on NVMe SSDs\n");
	ddb_print(ctx, "\n");

	/* Command: update_vea */
	ddb_print(ctx, "update_vea <offset> <blk_cnt>\n");
	ddb_print(ctx, "\tAlter the VEA tree to mark a region as free.\n");
	ddb_print(ctx, "    <offset>\n");
	ddb_print(ctx, "\tBlock offset of the region to mark free.\n");
	ddb_print(ctx, "    <blk_cnt>\n");
	ddb_print(ctx, "\tTotal blocks of the region to mark free.\n");
	ddb_print(ctx, "\n");
}

void
ddb_program_help(struct ddb_ctx *ctx)
{
	ddb_print(ctx, "The DAOS Debug Tool (ddb) allows a user to navigate through and modify\n"
		       "a file in the VOS format. It offers both a command line and interactive\n"
		       "shell mode. If the '-R' or '-f' options are not provided, then it will\n"
		       "run in interactive mode. In order to modify the file, the '-w' option\n"
		       "must be included.\n"
		       "\n"
		       "Many of the commands take a vos tree path. The format for this path\n"
		       "is [cont]/[obj]/[dkey]/[akey]/[extent]. The container is the container\n"
		       "uuid. The object is the object id.  The keys parts currently only\n"
		       "support string keys and must be surrounded with a single quote (') unless\n"
		       "using indexes (explained later). The extent for array values is the\n"
		       "format {lo-hi}. To make it easier to navigate the tree, indexes can be\n"
		       "used instead of the path part. The index is in the format [i]\n");
	ddb_print(ctx, "\n");
	ddb_print(ctx, "Usage:\n");
	ddb_print(ctx, "ddb [path] [options]\n");
	ddb_print(ctx, "\n");
	ddb_print(ctx, "    [path]\n");
	ddb_print(ctx, "\tPath to the vos file to open. This should be an absolute\n"
		       "\tpath to the pool shard. Part of the path is used to\n"
		       "\tdetermine what the pool uuid is. If a path is not provided\n"
		       "\tinitially, the open command can be used later to open the\n"
		       "\tvos file.\n");

	ddb_print(ctx, "\nOptions:\n");
	ddb_print(ctx, "   -w, --write_mode\n");
	ddb_print(ctx, "\tOpen the vos file in write mode. This allows for modifying the\n"
		       "\tvos file with the load,\n"
		       "\tcommit_ilog, etc commands.\n");
	ddb_print(ctx, "   -R, --run_cmd <cmd>\n");
	ddb_print(ctx, "\tExecute the single command <cmd>, then exit.\n");
	ddb_print(ctx, "   -f, --file_cmd <path>\n");
	ddb_print(ctx, "\tPath to a file container a list of ddb commands, one command\n"
		       "\tper line, then exit.\n");
	ddb_print(ctx, "   -h, --help\n");
	ddb_print(ctx, "\tShow tool usage.\n");

	ddb_print(ctx, "Commands:\n");
	ddb_print(ctx, "   help              Show help message for all the commands.\n");
	ddb_print(ctx, "   quit              Quit interactive mode\n");
	ddb_print(ctx, "   ls                List containers, objects, dkeys, akeys, and values\n");
	ddb_print(ctx, "   open              Opens the vos file at <path>\n");
	ddb_print(ctx, "   close             Close the currently opened vos pool shard\n");
	ddb_print(ctx, "   dump_superblock   Dump the pool superblock information\n");
	ddb_print(ctx, "   dump_value        Dump a value to a file\n");
	ddb_print(ctx, "   rm                Remove a branch of the VOS tree.\n");
	ddb_print(ctx, "   load              Load a value to a vos path.\n");
	ddb_print(ctx, "   dump_ilog         Dump the ilog\n");
	ddb_print(ctx, "   commit_ilog       Process the ilog\n");
	ddb_print(ctx, "   rm_ilog           Remove all the ilog entries\n");
	ddb_print(ctx, "   dump_dtx          Dump the dtx tables\n");
	ddb_print(ctx, "   clear_cmt_dtx     Clear the dtx committed table\n");
	ddb_print(ctx, "   smd_sync          Restore the SMD file with backup from blob\n");
	ddb_print(ctx, "   dump_vea          Dump information from the vea about free regions\n");
	ddb_print(ctx, "   update_vea        Alter the VEA tree to mark a region as free.\n");
}
