/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DDB_RUN_CMDS_H
#define __DDB_RUN_CMDS_H
#include "ddb_common.h"

enum ddb_cmd {

	DDB_CMD_UNKNOWN = 0,
	DDB_CMD_QUIT = 1,
	DDB_CMD_LS = 2,
};

struct ls_options {
	bool recursive;
	char *path;
};

struct ddb_cmd_info {
	enum ddb_cmd dci_cmd;
	union {
		struct ls_options dci_ls;

	} dci_cmd_option;
};

/* Run commands ... */

int ddb_parse_cmd_args(struct ddb_ctx *ctx, struct argv_parsed *parsed, struct ddb_cmd_info *info);

int ddb_run_ls(struct ddb_ctx *ctx, struct ls_options *opt);
int ddb_run_quit(struct ddb_ctx *ctx);

#endif /* __DDB_RUN_CMDS_H */
