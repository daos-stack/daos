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
#define COMMAND_NAME_LS "ls"
#define COMMAND_NAME_QUIT "quit"

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
	D_ASSERT(argc > optind);
	D_ASSERT(same(argv[optind], COMMAND_NAME_LS));
	optind++;

	if (argc - optind > 0) {
		cmd_args->path = argv[optind];
		optind++;
	}

	if (argc - optind > 0) {
		ddb_printf(ctx, "Unexpected argument: %s\\n", argv[optind]);
		return -DER_INVAL;
	}

	return 0;
}

int
ddb_parse_cmd_args(struct ddb_ctx *ctx, struct argv_parsed *parsed, struct ddb_cmd_info *info)
{
	char *cmd = parsed->ap_argv[1];

	if (same(cmd, COMMAND_NAME_LS)) {
		info->dci_cmd = DDB_CMD_LS;
		return ls_option_parse(ctx, &info->dci_cmd_option.dci_ls, parsed);
	}

	if (same(cmd, COMMAND_NAME_QUIT)) {
		info->dci_cmd = DDB_CMD_QUIT;
		return 0;
	}

	return -DER_INVAL;
}
