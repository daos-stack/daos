/**
 * (C) Copyright 2022-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <ctype.h>
#include <getopt.h>
#include <gurt/debug.h>
#include <daos_srv/vos.h>

#include "ddb.h"
#include "ddb_common.h"
#include "ddb_parse.h"

#define MAX_COMMAND_LEN              1024

#define same(a, b) (strcmp((a), (b)) == 0)
#define COMMAND_NAME_HELP "help"
#define COMMAND_NAME_QUIT "quit"
#define COMMAND_NAME_LS "ls"
#define COMMAND_NAME_OPEN "open"
#define COMMAND_NAME_VERSION "version"
#define COMMAND_NAME_CLOSE "close"
#define COMMAND_NAME_SUPERBLOCK_DUMP "superblock_dump"
#define COMMAND_NAME_VALUE_DUMP "value_dump"
#define COMMAND_NAME_RM "rm"
#define COMMAND_NAME_VALUE_LOAD "value_load"
#define COMMAND_NAME_ILOG_DUMP "ilog_dump"
#define COMMAND_NAME_ILOG_COMMIT "ilog_commit"
#define COMMAND_NAME_ILOG_CLEAR "ilog_clear"
#define COMMAND_NAME_DTX_DUMP "dtx_dump"
#define COMMAND_NAME_DTX_CMT_CLEAR "dtx_cmt_clear"
#define COMMAND_NAME_SMD_SYNC "smd_sync"
#define COMMAND_NAME_VEA_DUMP "vea_dump"
#define COMMAND_NAME_VEA_UPDATE "vea_update"
#define COMMAND_NAME_DTX_ACT_COMMIT "dtx_act_commit"
#define COMMAND_NAME_DTX_ACT_ABORT "dtx_act_abort"
#define COMMAND_NAME_FEATURE         "feature"
#define COMMAND_NAME_RM_POOL         "rm_pool"
#define COMMAND_NAME_DTX_ACT_DISCARD_INVALID "dtx_act_discard_invalid"
#define COMMAND_NAME_DEV_LIST                "dev_list"
#define COMMAND_NAME_DEV_REPLACE             "dev_replace"

/* Parse command line options for the 'ls' command */
static int
ls_option_parse(struct ddb_ctx *ctx, struct ls_options *cmd_args,
		uint32_t argc, char **argv)
{
	char		 *options_short = "rd";
	int		  index = 0, opt;
	struct option	  options_long[] = {
		{ "recursive", no_argument, NULL, 'r' },
		{ "details", no_argument, NULL, 'd' },
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
		case 'd':
			cmd_args->details = true;
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

/* Parse command line options for the 'value_dump' command */
static int
value_dump_option_parse(struct ddb_ctx *ctx, struct value_dump_options *cmd_args,
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

/* Parse command line options for the 'value_load' command */
static int
value_load_option_parse(struct ddb_ctx *ctx, struct value_load_options *cmd_args,
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

/* Parse command line options for the 'ilog_dump' command */
static int
ilog_dump_option_parse(struct ddb_ctx *ctx, struct ilog_dump_options *cmd_args,
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

/* Parse command line options for the 'ilog_commit' command */
static int
ilog_commit_option_parse(struct ddb_ctx *ctx, struct ilog_commit_options *cmd_args,
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

/* Parse command line options for the 'ilog_clear' command */
static int
ilog_clear_option_parse(struct ddb_ctx *ctx, struct ilog_clear_options *cmd_args,
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

/* Parse command line options for the 'dtx_dump' command */
static int
dtx_dump_option_parse(struct ddb_ctx *ctx, struct dtx_dump_options *cmd_args,
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

/* Parse command line options for the 'dtx_cmt_clear' command */
static int
dtx_cmt_clear_option_parse(struct ddb_ctx *ctx, struct dtx_cmt_clear_options *cmd_args,
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

/* Parse command line options for the 'smd_sync' command */
static int
smd_sync_option_parse(struct ddb_ctx *ctx, struct smd_sync_options *cmd_args,
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
		cmd_args->nvme_conf = argv[index];
		index++;
	}
	if (argc - index > 0) {
		cmd_args->db_path = argv[index];
		index++;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\n", argv[index]);
		return -DER_INVAL;
	}

	return 0;
}

/* Parse command line options for the 'vea_update' command */
static int
vea_update_option_parse(struct ddb_ctx *ctx, struct vea_update_options *cmd_args,
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

/**
 * Parse command line options for the 'dtx_act_commit', 'dtx_act_abort', and 'dtx_act_abort'
 * commands.
 */
static int
dtx_act_option_parse(struct ddb_ctx *ctx, struct dtx_act_options *cmd_args, uint32_t argc,
		     char **argv)
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
		cmd_args->dtx_id = argv[index];
		index++;
	} else {
		ddb_print(ctx, "Expected argument 'dtx_id'\n");
		return -DER_INVAL;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\n", argv[index]);
		return -DER_INVAL;
	}

	return 0;
}

int
ddb_feature_string2flags(struct ddb_ctx *ctx, const char *string, uint64_t *compat_flags,
			 uint64_t *incompat_flags)
{
	char    *tmp;
	char    *tok;
	int      rc = 0;
	uint64_t flag;
	uint64_t ret_compat_flags   = 0;
	uint64_t ret_incompat_flags = 0;
	bool     compat_feature;

	tmp = strndup(string, PATH_MAX);
	if (tmp == NULL)
		return -DER_NOMEM;
	tok = strtok(tmp, ",");
	while (tok != NULL) {
		flag = vos_pool_name2flag(tok, &compat_feature);
		if (flag == 0) {
			ddb_printf(ctx, "Unknown flag: '%s'\n", tok);
			rc = -DER_INVAL;
			break;
		}
		if (compat_feature)
			ret_compat_flags |= flag;
		else
			ret_incompat_flags |= flag;
		tok = strtok(NULL, ",");
	}

	free(tmp);
	if (rc == 0) {
		*compat_flags   = ret_compat_flags;
		*incompat_flags = ret_incompat_flags;
	}

	return rc;
}

static int
feature_option_parse(struct ddb_ctx *ctx, struct feature_options *cmd_args, uint32_t argc,
		     char **argv)
{
	char         *options_short  = "e:d:s";
	int           index          = 0, opt;
	int           rc             = 0;
	struct option options_long[] = {{"enable", required_argument, NULL, 'e'},
					{"disable", required_argument, NULL, 'd'},
					{"show", no_argument, NULL, 's'},
					{NULL}};

	memset(cmd_args, 0, sizeof(*cmd_args));
	/* Restart getopt */
	optind = 1;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, options_short, options_long, &index)) != -1) {
		switch (opt) {
		case 'e':
			rc = ddb_feature_string2flags(ctx, optarg, &cmd_args->set_compat_flags,
						      &cmd_args->set_incompat_flags);
			if (rc)
				return rc;
			break;
		case 'd':
			rc = ddb_feature_string2flags(ctx, optarg, &cmd_args->clear_compat_flags,
						      &cmd_args->clear_incompat_flags);
			if (rc)
				return rc;
			break;
		case 's':
			cmd_args->show_features = true;
			break;
		case '?':
			ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
			break;
		default:
			return -DER_INVAL;
		}
	}

	index = optind;
	if (argc - index > 0) {
		cmd_args->path = argv[index];
		index++;
	} else if (ctx->dc_pool_path) {
		cmd_args->path = ctx->dc_pool_path;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\n", argv[index]);
		return -DER_INVAL;
	}

	return 0;
}

/* Parse command line options for the 'rm_pool' command */
static int
rm_pool_option_parse(struct ddb_ctx *ctx, struct rm_pool_options *cmd_args, uint32_t argc,
		     char **argv)
{
	char         *options_short  = "";
	int           index          = 0;
	struct option options_long[] = {{NULL}};

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
	} else if (ctx->dc_pool_path) {
		cmd_args->path = ctx->dc_pool_path;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\n", argv[index]);
		return -DER_INVAL;
	}

	return 0;
}

/* Parse command line options for the 'dev_list' command */
static int
dev_list_option_parse(struct ddb_ctx *ctx, struct dev_list_options *cmd_args, uint32_t argc,
		      char **argv)
{
	char         *options_short  = "";
	int           index          = 0;
	struct option options_long[] = {{NULL}};

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
		cmd_args->db_path = argv[index];
		index++;
	}

	if (argc - index > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\n", argv[index]);
		return -DER_INVAL;
	}

	return 0;
}

/* Parse command line options for the 'dev_replace' command */
static int
dev_replace_option_parse(struct ddb_ctx *ctx, struct dev_replace_options *cmd_args, uint32_t argc,
			 char **argv)
{
	uuid_t        dev_uuid;
	char         *options_short  = "o:n:";
	int           index          = 0, opt, rc;
	struct option options_long[] = {{"old_dev", required_argument, NULL, 'o'},
					{"new_dev", required_argument, NULL, 'n'},
					{NULL}};

	memset(cmd_args, 0, sizeof(*cmd_args));
	/* Restart getopt */
	optind = 1;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, options_short, options_long, &index)) != -1) {
		switch (opt) {
		case 'o':
			rc = uuid_parse(optarg, dev_uuid);
			if (rc) {
				ddb_printf(ctx, "Invalid UUID string %s for old device\n", optarg);
				return rc;
			}
			cmd_args->old_devid = optarg;
			break;
		case 'n':
			rc = uuid_parse(optarg, dev_uuid);
			if (rc) {
				ddb_printf(ctx, "Invalid UUID string %s for new device\n", optarg);
				return rc;
			}
			cmd_args->new_devid = optarg;
			break;
		case '?':
			ddb_printf(ctx, "Unknown option: '%c'\n", optopt);
			break;
		default:
			return -DER_INVAL;
		}
	}

	index = optind;
	if (argc - index > 0) {
		cmd_args->db_path = argv[index];
		index++;
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
	if (same(cmd, COMMAND_NAME_VERSION)) {
		info->dci_cmd = DDB_CMD_VERSION;
		return 0;
	}
	if (same(cmd, COMMAND_NAME_CLOSE)) {
		info->dci_cmd = DDB_CMD_CLOSE;
		return 0;
	}
	if (same(cmd, COMMAND_NAME_SUPERBLOCK_DUMP)) {
		info->dci_cmd = DDB_CMD_SUPERBLOCK_DUMP;
		return 0;
	}
	if (same(cmd, COMMAND_NAME_VALUE_DUMP)) {
		info->dci_cmd = DDB_CMD_VALUE_DUMP;
		return value_dump_option_parse(ctx, &info->dci_cmd_option.dci_value_dump,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_RM)) {
		info->dci_cmd = DDB_CMD_RM;
		return rm_option_parse(ctx, &info->dci_cmd_option.dci_rm,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_VALUE_LOAD)) {
		info->dci_cmd = DDB_CMD_VALUE_LOAD;
		return value_load_option_parse(ctx, &info->dci_cmd_option.dci_value_load,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_ILOG_DUMP)) {
		info->dci_cmd = DDB_CMD_ILOG_DUMP;
		return ilog_dump_option_parse(ctx, &info->dci_cmd_option.dci_ilog_dump,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_ILOG_COMMIT)) {
		info->dci_cmd = DDB_CMD_ILOG_COMMIT;
		return ilog_commit_option_parse(ctx, &info->dci_cmd_option.dci_ilog_commit,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_ILOG_CLEAR)) {
		info->dci_cmd = DDB_CMD_ILOG_CLEAR;
		return ilog_clear_option_parse(ctx, &info->dci_cmd_option.dci_ilog_clear,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_DTX_DUMP)) {
		info->dci_cmd = DDB_CMD_DTX_DUMP;
		return dtx_dump_option_parse(ctx, &info->dci_cmd_option.dci_dtx_dump,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_DTX_CMT_CLEAR)) {
		info->dci_cmd = DDB_CMD_DTX_CMT_CLEAR;
		return dtx_cmt_clear_option_parse(ctx, &info->dci_cmd_option.dci_dtx_cmt_clear,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_SMD_SYNC)) {
		info->dci_cmd = DDB_CMD_SMD_SYNC;
		return smd_sync_option_parse(ctx, &info->dci_cmd_option.dci_smd_sync,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_VEA_DUMP)) {
		info->dci_cmd = DDB_CMD_VEA_DUMP;
		return 0;
	}
	if (same(cmd, COMMAND_NAME_VEA_UPDATE)) {
		info->dci_cmd = DDB_CMD_VEA_UPDATE;
		return vea_update_option_parse(ctx, &info->dci_cmd_option.dci_vea_update,
		       argc, argv);
	}
	if (same(cmd, COMMAND_NAME_DTX_ACT_COMMIT)) {
		info->dci_cmd = DDB_CMD_DTX_ACT_COMMIT;
		return dtx_act_option_parse(ctx, &info->dci_cmd_option.dci_dtx_act, argc, argv);
	}
	if (same(cmd, COMMAND_NAME_DTX_ACT_ABORT)) {
		info->dci_cmd = DDB_CMD_DTX_ACT_ABORT;
		return dtx_act_option_parse(ctx, &info->dci_cmd_option.dci_dtx_act, argc, argv);
	}
	if (same(cmd, COMMAND_NAME_DTX_ACT_DISCARD_INVALID)) {
		info->dci_cmd = DDB_CMD_DTX_ACT_DISCARD_INVALID;
		return dtx_act_option_parse(ctx, &info->dci_cmd_option.dci_dtx_act, argc, argv);
	}
	if (same(cmd, COMMAND_NAME_RM_POOL)) {
		info->dci_cmd = DDB_CMD_RM_POOL;
		return rm_pool_option_parse(ctx, &info->dci_cmd_option.dci_rm_pool, argc, argv);
	}
	if (same(cmd, COMMAND_NAME_FEATURE)) {
		info->dci_cmd = DDB_CMD_FEATURE;
		return feature_option_parse(ctx, &info->dci_cmd_option.dci_feature, argc, argv);
	}

	if (same(cmd, COMMAND_NAME_DEV_LIST)) {
		info->dci_cmd = DDB_CMD_DEV_LIST;
		return dev_list_option_parse(ctx, &info->dci_cmd_option.dci_dev_list, argc, argv);
	}

	if (same(cmd, COMMAND_NAME_DEV_REPLACE)) {
		info->dci_cmd = DDB_CMD_DEV_REPLACE;
		return dev_replace_option_parse(ctx, &info->dci_cmd_option.dci_dev_replace, argc,
						argv);
	}

	ddb_errorf(ctx,
		   "'%s' is not a valid command. Available commands are:"
		   "'help', "
		   "'quit', "
		   "'ls', "
		   "'open', "
		   "'version', "
		   "'close', "
		   "'superblock_dump', "
		   "'value_dump', "
		   "'rm', "
		   "'value_load', "
		   "'ilog_dump', "
		   "'ilog_commit', "
		   "'ilog_clear', "
		   "'dtx_dump', "
		   "'dtx_cmt_clear', "
		   "'smd_sync', "
		   "'vea_dump', "
		   "'vea_update', "
		   "'dtx_act_commit', "
		   "'dtx_act_abort', "
		   "'feature', "
		   "'rm_pool', "
		   "'dev_list', "
		   "'dev_replace'\n",
		   cmd);

	return -DER_INVAL;
}

int
ddb_parse_cmd_str(struct ddb_ctx *ctx, const char *cmd_str, bool *open)
{
	struct argv_parsed       parse_args = {0};
	int			 rc;
	char                    *cmd_copy;

	D_STRNDUP(cmd_copy, cmd_str, MAX_COMMAND_LEN);
	if (cmd_copy == NULL)
		return -DER_NOMEM;

	/* Remove newline if needed */
	if (cmd_copy[strlen(cmd_copy) - 1] == '\n')
		cmd_copy[strlen(cmd_copy) - 1] = '\0';

	rc = ddb_str2argv_create(cmd_copy, &parse_args);
	if (!SUCCESS(rc)) {
		D_FREE(cmd_copy);
		return rc;
	}

	if (parse_args.ap_argc == 0) {
		D_ERROR("Nothing parsed\n");
		D_GOTO(done, rc = -DER_INVAL);
	}

	if (same(parse_args.ap_argv[0], COMMAND_NAME_RM_POOL) ||
	    same(parse_args.ap_argv[0], COMMAND_NAME_FEATURE))
		*open = false;
	else
		*open = true;
done:
	ddb_str2argv_free(&parse_args);
	D_FREE(cmd_copy);

	return rc;
}

int
ddb_run_cmd(struct ddb_ctx *ctx, const char *cmd_str)
{
	struct ddb_cmd_info info = {0};
	int                 rc;
	struct argv_parsed  parse_args = {0};
	char               *cmd_copy;

	D_STRNDUP(cmd_copy, cmd_str, MAX_COMMAND_LEN);
	if (cmd_copy == NULL)
		return -DER_NOMEM;

	/* Remove newline if needed */
	if (cmd_copy[strlen(cmd_copy) - 1] == '\n')
		cmd_copy[strlen(cmd_copy) - 1] = '\0';

	rc = ddb_str2argv_create(cmd_copy, &parse_args);
	if (!SUCCESS(rc)) {
		D_FREE(cmd_copy);
		return rc;
	}

	if (parse_args.ap_argc == 0) {
		D_ERROR("Nothing parsed\n");
		D_GOTO(done, rc = -DER_INVAL);
	}

	rc = ddb_parse_cmd_args(ctx, parse_args.ap_argc, parse_args.ap_argv, &info);
	if (!SUCCESS(rc))
		D_GOTO(done, rc);

	switch (info.dci_cmd) {
	case DDB_CMD_HELP:
		rc = ddb_run_help(ctx);
		break;

	case DDB_CMD_QUIT:
		rc = ddb_run_quit(ctx);
		break;

	case DDB_CMD_LS:
		rc = ddb_run_ls(ctx, &info.dci_cmd_option.dci_ls);
		break;

	case DDB_CMD_OPEN:
		rc = ddb_run_open(ctx, &info.dci_cmd_option.dci_open);
		break;

	case DDB_CMD_VERSION:
		rc = ddb_run_version(ctx);
		break;

	case DDB_CMD_CLOSE:
		rc = ddb_run_close(ctx);
		break;

	case DDB_CMD_SUPERBLOCK_DUMP:
		rc = ddb_run_superblock_dump(ctx);
		break;

	case DDB_CMD_VALUE_DUMP:
		rc = ddb_run_value_dump(ctx, &info.dci_cmd_option.dci_value_dump);
		break;

	case DDB_CMD_RM:
		rc = ddb_run_rm(ctx, &info.dci_cmd_option.dci_rm);
		break;

	case DDB_CMD_VALUE_LOAD:
		rc = ddb_run_value_load(ctx, &info.dci_cmd_option.dci_value_load);
		break;

	case DDB_CMD_ILOG_DUMP:
		rc = ddb_run_ilog_dump(ctx, &info.dci_cmd_option.dci_ilog_dump);
		break;

	case DDB_CMD_ILOG_COMMIT:
		rc = ddb_run_ilog_commit(ctx, &info.dci_cmd_option.dci_ilog_commit);
		break;

	case DDB_CMD_ILOG_CLEAR:
		rc = ddb_run_ilog_clear(ctx, &info.dci_cmd_option.dci_ilog_clear);
		break;

	case DDB_CMD_DTX_DUMP:
		rc = ddb_run_dtx_dump(ctx, &info.dci_cmd_option.dci_dtx_dump);
		break;

	case DDB_CMD_DTX_CMT_CLEAR:
		rc = ddb_run_dtx_cmt_clear(ctx, &info.dci_cmd_option.dci_dtx_cmt_clear);
		break;

	case DDB_CMD_SMD_SYNC:
		rc = ddb_run_smd_sync(ctx, &info.dci_cmd_option.dci_smd_sync);
		break;

	case DDB_CMD_VEA_DUMP:
		rc = ddb_run_vea_dump(ctx);
		break;

	case DDB_CMD_VEA_UPDATE:
		rc = ddb_run_vea_update(ctx, &info.dci_cmd_option.dci_vea_update);
		break;

	case DDB_CMD_DTX_ACT_COMMIT:
		rc = ddb_run_dtx_act_commit(ctx, &info.dci_cmd_option.dci_dtx_act);
		break;

	case DDB_CMD_DTX_ACT_ABORT:
		rc = ddb_run_dtx_act_abort(ctx, &info.dci_cmd_option.dci_dtx_act);
		break;

	case DDB_CMD_DTX_ACT_DISCARD_INVALID:
		rc = ddb_run_dtx_act_discard_invalid(ctx, &info.dci_cmd_option.dci_dtx_act);
		break;

	case DDB_CMD_FEATURE:
		rc = ddb_run_feature(ctx, &info.dci_cmd_option.dci_feature);
		break;

	case DDB_CMD_RM_POOL:
		rc = ddb_run_rm_pool(ctx, &info.dci_cmd_option.dci_rm_pool);
		break;

	case DDB_CMD_DEV_LIST:
		rc = ddb_run_dev_list(ctx, &info.dci_cmd_option.dci_dev_list);
		break;

	case DDB_CMD_DEV_REPLACE:
		rc = ddb_run_dev_replace(ctx, &info.dci_cmd_option.dci_dev_replace);
		break;

	case DDB_CMD_UNKNOWN:
		ddb_error(ctx, "Unknown command\n");
		rc = -DER_INVAL;
		break;
	}

done:
	ddb_str2argv_free(&parse_args);
	D_FREE(cmd_copy);

	return rc;
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
	ddb_print(ctx, "    -d, --details\n");
	ddb_print(ctx, "\tList more details of items in path\n");
	ddb_print(ctx, "\n");

	/* Command: open */
	ddb_print(ctx, "open <path>\n");
	ddb_print(ctx, "\tOpens the vos file at <path>\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tPath to the vos file to open.\n");
	ddb_print(ctx, "Options:\n");
	ddb_print(ctx, "    -w, --write_mode\n");
	ddb_print(ctx, "\tOpen the vos file in write mode.\n");
	ddb_print(ctx, "\n");

	/* Command: version */
	ddb_print(ctx, "version\n");
	ddb_print(ctx, "\tPrint ddb version\n");
	ddb_print(ctx, "\n");

	/* Command: close */
	ddb_print(ctx, "close\n");
	ddb_print(ctx, "\tClose the currently opened vos pool shard\n");
	ddb_print(ctx, "\n");

	/* Command: superblock_dump */
	ddb_print(ctx, "superblock_dump\n");
	ddb_print(ctx, "\tDump the pool superblock information\n");
	ddb_print(ctx, "\n");

	/* Command: value_dump */
	ddb_print(ctx, "value_dump <path> [dst]\n");
	ddb_print(ctx, "\tDump a value\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tVOS tree path to dump.\n");
	ddb_print(ctx, "    [dst]\n");
	ddb_print(ctx, "\tFile path to dump the value to.\n");
	ddb_print(ctx, "\n");

	/* Command: rm */
	ddb_print(ctx, "rm <path>\n");
	ddb_print(ctx, "\tRemove a branch of the VOS tree.\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tVOS tree path to remove.\n");
	ddb_print(ctx, "\n");

	/* Command: value_load */
	ddb_print(ctx, "value_load <src> <dst>\n");
	ddb_print(ctx, "\tLoad a value to a vos path.\n");
	ddb_print(ctx, "    <src>\n");
	ddb_print(ctx, "\tSource file path.\n");
	ddb_print(ctx, "    <dst>\n");
	ddb_print(ctx, "\tDestination vos tree path to a value.\n");
	ddb_print(ctx, "\n");

	/* Command: ilog_dump */
	ddb_print(ctx, "ilog_dump <path>\n");
	ddb_print(ctx, "\tDump the ilog\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tVOS tree path to an object, dkey, or akey.\n");
	ddb_print(ctx, "\n");

	/* Command: ilog_commit */
	ddb_print(ctx, "ilog_commit <path>\n");
	ddb_print(ctx, "\tProcess the ilog\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tVOS tree path to an object, dkey, or akey.\n");
	ddb_print(ctx, "\n");

	/* Command: ilog_clear */
	ddb_print(ctx, "ilog_clear <path>\n");
	ddb_print(ctx, "\tRemove all the ilog entries\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tVOS tree path to an object, dkey, or akey.\n");
	ddb_print(ctx, "\n");

	/* Command: dtx_dump */
	ddb_print(ctx, "dtx_dump <path>\n");
	ddb_print(ctx, "\tDump the dtx tables\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tVOS tree path to a container.\n");
	ddb_print(ctx, "Options:\n");
	ddb_print(ctx, "    -a, --active\n");
	ddb_print(ctx, "\tOnly dump entries from the active table\n");
	ddb_print(ctx, "    -c, --committed\n");
	ddb_print(ctx, "\tOnly dump entries from the committed table\n");
	ddb_print(ctx, "\n");

	/* Command: dtx_cmt_clear */
	ddb_print(ctx, "dtx_cmt_clear <path>\n");
	ddb_print(ctx, "\tClear the dtx committed table\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tVOS tree path to a container.\n");
	ddb_print(ctx, "\n");

	/* Command: smd_sync */
	ddb_print(ctx, "smd_sync [nvme_conf] [db_path]\n");
	ddb_print(ctx, "\tRestore the SMD file with backup from blob\n");
	ddb_print(ctx, "    [nvme_conf]\n");
	ddb_print(ctx, "\tPath to the nvme conf file. (default /mnt/daos/daos_nvme.conf)\n");
	ddb_print(ctx, "    [db_path]\n");
	ddb_print(ctx, "\tPath to the vos db. (default /mnt/daos)\n");
	ddb_print(ctx, "\n");

	/* Command: vea_dump */
	ddb_print(ctx, "vea_dump\n");
	ddb_print(ctx, "\tDump information from the vea about free regions\n");
	ddb_print(ctx, "\n");

	/* Command: vea_update */
	ddb_print(ctx, "vea_update <offset> <blk_cnt>\n");
	ddb_print(ctx, "\tAlter the VEA tree to mark a region as free.\n");
	ddb_print(ctx, "    <offset>\n");
	ddb_print(ctx, "\tBlock offset of the region to mark free.\n");
	ddb_print(ctx, "    <blk_cnt>\n");
	ddb_print(ctx, "\tTotal blocks of the region to mark free.\n");
	ddb_print(ctx, "\n");

	/* Command: dtx_act_commit */
	ddb_print(ctx, "dtx_act_commit <path> <dtx_id>\n");
	ddb_print(ctx, "\tMark the active dtx entry as committed\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tVOS tree path to a container.\n");
	ddb_print(ctx, "    <dtx_id>\n");
	ddb_print(ctx, "\tDTX id of the entry to commit.\n");
	ddb_print(ctx, "\n");

	/* Command: dtx_act_abort */
	ddb_print(ctx, "dtx_act_abort <path> <dtx_id>\n");
	ddb_print(ctx, "\tMark the active dtx entry as aborted\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\tVOS tree path to a container.\n");
	ddb_print(ctx, "    <dtx_id>\n");
	ddb_print(ctx, "\tDTX id of the entry to abort.\n");
	ddb_print(ctx, "\n");

	/* Command: rm_pool */
	ddb_print(ctx, "rm_pool <path>\n");
	ddb_print(ctx, "\tremove pool shard\n");
	ddb_print(ctx, "    <path>\n");
	ddb_print(ctx, "\n");

	/* Command: feature */
	ddb_print(ctx, "feature\n");
	ddb_print(ctx, "\tManage vos pool features\n");
	ddb_print(ctx, "Options:\n");
	ddb_print(ctx, "    -e, --enable\n");
	ddb_print(ctx, "\tEnable vos pool features\n");
	ddb_print(ctx, "    -d, --disable\n");
	ddb_print(ctx, "\tDisable vos pool features\n");
	ddb_print(ctx, "    -s, --show\n");
	ddb_print(ctx, "\tShow current features\n");
	ddb_print(ctx, "\n");

	/* Command: dev_list */
	ddb_print(ctx, "dev_list [db_path]\n");
	ddb_print(ctx, "\tList all devices\n");
	ddb_print(ctx, "    [db_path]\n");
	ddb_print(ctx, "\tPath to the vos db. (default /mnt/daos)\n");
	ddb_print(ctx, "\n");

	/* Command: dev_replace */
	ddb_print(ctx, "dev_replace [db_path]\n");
	ddb_print(ctx, "\tReplaced an old device with a new unused device\n");
	ddb_print(ctx, "    [db_path]\n");
	ddb_print(ctx, "\tPath to the vos db. (default /mnt/daos)\n");
	ddb_print(ctx, "Options:\n");
	ddb_print(ctx, "    -o, --old_dev\n");
	ddb_print(ctx, "\tSpecify the old device UUID\n");
	ddb_print(ctx, "    -n, --new_dev\n");
	ddb_print(ctx, "\tSpecify the new device UUID\n");
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
	ddb_print(ctx, "\tOpen the vos file in write mode. This allows for modifying\n"
		       "\tVOS file with the rm, load,\n"
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
	ddb_print(ctx, "   version           Print ddb version\n");
	ddb_print(ctx, "   close             Close the currently opened vos pool shard\n");
	ddb_print(ctx, "   superblock_dump   Dump the pool superblock information\n");
	ddb_print(ctx, "   value_dump        Dump a value\n");
	ddb_print(ctx, "   rm                Remove a branch of the VOS tree.\n");
	ddb_print(ctx, "   value_load        Load a value to a vos path.\n");
	ddb_print(ctx, "   ilog_dump         Dump the ilog\n");
	ddb_print(ctx, "   ilog_commit       Process the ilog\n");
	ddb_print(ctx, "   ilog_clear        Remove all the ilog entries\n");
	ddb_print(ctx, "   dtx_dump          Dump the dtx tables\n");
	ddb_print(ctx, "   dtx_cmt_clear     Clear the dtx committed table\n");
	ddb_print(ctx, "   smd_sync          Restore the SMD file with backup from blob\n");
	ddb_print(ctx, "   vea_dump          Dump information from the vea about free regions\n");
	ddb_print(ctx, "   vea_update        Alter the VEA tree to mark a region as free.\n");
	ddb_print(ctx, "   dtx_act_commit    Mark the active dtx entry as committed\n");
	ddb_print(ctx, "   dtx_act_abort     Mark the active dtx entry as aborted\n");
	ddb_print(ctx, "   feature	     Manage vos pool features\n");
	ddb_print(ctx, "   rm_pool	     Remove pool shard\n");
	ddb_print(ctx, "   dev_list	     List all devices\n");
	ddb_print(ctx, "   dev_replace	     Replace an old device with a new unused device\n");
}
