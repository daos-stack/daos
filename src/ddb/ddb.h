/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DDB_RUN_CMDS_H
#define __DDB_RUN_CMDS_H


#include <daos_types.h>

typedef int (*ddb_io_line_cb)(void *cb_args, char *line, uint32_t str_len);

struct ddb_io_ft {
	/**
	 * Print a message.
	 *
	 * @param fmt	Typically printf string format
	 * @param ...	Additional args will be formatted into the printed string
	 * @return	Total number of characters written
	 */
	int (*ddb_print_message)(const char *fmt, ...);

	/**
	 * Print an error message.
	 *
	 * @param fmt	Typically printf string format
	 * @param ...	Additional args will be formatted into the printed string
	 * @return	Total number of characters written
	 */
	int (*ddb_print_error)(const char *fmt, ...);

	/**
	 * Read a line from stdin and stores into buf.
	 *
	 * @param buf		Pointer to an array where the string read is stored
	 * @param buf_len	Length of buf
	 * @return		On success the same buf parameter, else NULL
	 */
	char *(*ddb_get_input)(char *buf, uint32_t buf_len);

	/**
	 * Check if a file exists
	 *
	 * @param path	Path to file to check
	 * @return	true if the file exists, else false
	 */
	bool (*ddb_get_file_exists)(const char *path);

	/**
	 * Write the contents of the iov to a file
	 *
	 * @param dst_path	File to write to
	 * @param contents	Contents to be written
	 * @return		0 on success, else an error code
	 */
	int (*ddb_write_file)(const char *dst_path, d_iov_t *contents);

	/**
	 * Determine the size of a file at path
	 * @param path	Path of file to check
	 * @return	the size of the file at path in bytes
	 */
	size_t (*ddb_get_file_size)(const char *path);

	/**
	 * Read the contents of a file and store into the iov
	 * @param src_path	Path of the file to read
	 * @param contents	Where to load the contents of the file into
	 * @return		number of bytes read from the src_path
	 */
	size_t (*ddb_read_file)(const char *src_path, d_iov_t *contents);

	/**
	 * Read contents of a file line by line. For each line, the line_cb will be called.
	 * @param path		Path of the file to read
	 * @param line_cb	Callback function used for each line
	 * @param cb_args	Caller arguments passed to the callback function
	 * @return		0 on success, else an error code
	 */
	int (*ddb_get_lines)(const char *path, ddb_io_line_cb line_cb, void *cb_args);
};

struct ddb_ctx {
	struct ddb_io_ft	 dc_io_ft;
	daos_handle_t		 dc_poh;
	bool			 dc_should_quit;
	bool			 dc_write_mode;
};

void ddb_ctx_init(struct ddb_ctx *ctx);
int ddb_init();
void ddb_fini();

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
	DDB_CMD_DUMP_VEA = 16,
	DDB_CMD_UPDATE_VEA = 17,
	DDB_CMD_DTX_COMMIT = 18,
	DDB_CMD_DTX_ABORT = 19,
};

/* option and argument structures for commands that need them */
struct ls_options {
	bool recursive;
	char *path;
};

struct open_options {
	bool write_mode;
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

struct smd_sync_options {
	char *nvme_conf;
	char *db_path;
};

struct update_vea_options {
	char *offset;
	char *blk_cnt;
};

struct dtx_commit_options {
	char *path;
	char *dtx_id;
};

struct dtx_abort_options {
	char *path;
	char *dtx_id;
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
		struct smd_sync_options dci_smd_sync;
		struct update_vea_options dci_update_vea;
		struct dtx_commit_options dci_dtx_commit;
		struct dtx_abort_options dci_dtx_abort;
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
int ddb_run_smd_sync(struct ddb_ctx *ctx, struct smd_sync_options *opt);
int ddb_run_dump_vea(struct ddb_ctx *ctx);
int ddb_run_update_vea(struct ddb_ctx *ctx, struct update_vea_options *opt);
int ddb_run_dtx_commit(struct ddb_ctx *ctx, struct dtx_commit_options *opt);
int ddb_run_dtx_abort(struct ddb_ctx *ctx, struct dtx_abort_options *opt);


void ddb_program_help(struct ddb_ctx *ctx);
void ddb_commands_help(struct ddb_ctx *ctx);

#endif /* __DDB_RUN_CMDS_H */