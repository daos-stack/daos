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
	DDB_CMD_DUMP_SUPERBLOCK = 3,
	DDB_CMD_DUMP_ILOG = 4,
	DDB_CMD_DUMP_DTX = 5,
	DDB_CMD_DUMP_VALUE = 6,
	DDB_CMD_RM = 7,
	DDB_CMD_LOAD = 8,
};

/* option and argument structures for commands that need them */
struct ls_options {
	bool recursive;
	char *path;
};

struct dump_ilog_options {
	char *path;
};

struct dump_dtx_options {
	bool active;
	bool committed;
	char *path;
};

struct dump_value_options {
	char *path;
	char *dst;
};

struct rm_options {
	char *path;
};

struct load_options {
	char *src;
	char *dst;
	char *epoch;
};

struct ddb_cmd_info {
	enum ddb_cmd dci_cmd;
	union {
		struct ls_options dci_ls;
		struct dump_ilog_options dci_dump_ilog;
		struct dump_dtx_options dci_dump_dtx;
		struct dump_value_options dci_dump_value;
		struct rm_options dci_rm;
		struct load_options dci_load;
	} dci_cmd_option;
};

int ddb_parse_cmd_args(struct ddb_ctx *ctx, struct argv_parsed *parsed, struct ddb_cmd_info *info);

/* Run commands ... */
int ddb_run_quit(struct ddb_ctx *ctx);
int ddb_run_ls(struct ddb_ctx *ctx, struct ls_options *opt);
int ddb_run_dump_superblock(struct ddb_ctx *ctx);
int ddb_run_dump_ilog(struct ddb_ctx *ctx, struct dump_ilog_options *opt);
int ddb_run_dump_dtx(struct ddb_ctx *ctx, struct dump_dtx_options *opt);
int ddb_run_dump_value(struct ddb_ctx *ctx, struct dump_value_options *opt);
int ddb_run_rm(struct ddb_ctx *ctx, struct rm_options *opt);
int ddb_run_load(struct ddb_ctx *ctx, struct load_options *opt);

#endif /* __DDB_RUN_CMDS_H */
