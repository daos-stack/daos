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
	DDB_CMD_HELP = 1,
	DDB_CMD_QUIT = 2,
	DDB_CMD_LS = 3,
	DDB_CMD_OPEN = 4,
	DDB_CMD_CLOSE = 5,
	DDB_CMD_DUMP_SUPERBLOCK = 6,
	DDB_CMD_DUMP_VALUE = 7,
	DDB_CMD_RM = 8,
	DDB_CMD_LOAD = 9,
	DDB_CMD_DUMP_ILOG = 10,
	DDB_CMD_COMMIT_ILOG = 11,
	DDB_CMD_RM_ILOG = 12,
	DDB_CMD_DUMP_DTX = 13,
	DDB_CMD_CLEAR_CMT_DTX = 14,
	DDB_CMD_SMD_SYNC = 15,
};

/* option and argument structures for commands that need them */
struct ls_options {
	bool recursive;
	char *path;
};

struct open_options {
	bool write_mode;
	char *vos_pool_shard;
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
};

struct dump_ilog_options {
	char *path;
};

struct commit_ilog_options {
	char *path;
};

struct rm_ilog_options {
	char *path;
};

struct dump_dtx_options {
	bool active;
	bool committed;
	char *path;
};

struct clear_cmt_dtx_options {
	char *path;
};

struct ddb_cmd_info {
	enum ddb_cmd dci_cmd;
	union {
		struct ls_options dci_ls;
		struct open_options dci_open;
		struct dump_value_options dci_dump_value;
		struct rm_options dci_rm;
		struct load_options dci_load;
		struct dump_ilog_options dci_dump_ilog;
		struct commit_ilog_options dci_commit_ilog;
		struct rm_ilog_options dci_rm_ilog;
		struct dump_dtx_options dci_dump_dtx;
		struct clear_cmt_dtx_options dci_clear_cmt_dtx;
	} dci_cmd_option;
};

int ddb_parse_cmd_args(struct ddb_ctx *ctx, uint32_t argc, char **argv, struct ddb_cmd_info *info);

/* Run commands ... */
int ddb_run_help(struct ddb_ctx *ctx);
int ddb_run_quit(struct ddb_ctx *ctx);
int ddb_run_ls(struct ddb_ctx *ctx, struct ls_options *opt);
int ddb_run_open(struct ddb_ctx *ctx, struct open_options *opt);
int ddb_run_close(struct ddb_ctx *ctx);
int ddb_run_dump_superblock(struct ddb_ctx *ctx);
int ddb_run_dump_value(struct ddb_ctx *ctx, struct dump_value_options *opt);
int ddb_run_rm(struct ddb_ctx *ctx, struct rm_options *opt);
int ddb_run_load(struct ddb_ctx *ctx, struct load_options *opt);
int ddb_run_dump_ilog(struct ddb_ctx *ctx, struct dump_ilog_options *opt);
int ddb_run_commit_ilog(struct ddb_ctx *ctx, struct commit_ilog_options *opt);
int ddb_run_rm_ilog(struct ddb_ctx *ctx, struct rm_ilog_options *opt);
int ddb_run_dump_dtx(struct ddb_ctx *ctx, struct dump_dtx_options *opt);
int ddb_run_clear_cmt_dtx(struct ddb_ctx *ctx, struct clear_cmt_dtx_options *opt);
int ddb_run_smd_sync(struct ddb_ctx *ctx);

#endif /* __DDB_RUN_CMDS_H */
