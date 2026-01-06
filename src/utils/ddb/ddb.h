/**
 * (C) Copyright 2022-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
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
	const char              *dc_pool_path;
};

void ddb_ctx_init(struct ddb_ctx *ctx);
int ddb_init(void);
void ddb_fini(void);

enum ddb_cmd {
	DDB_CMD_UNKNOWN                 = 0,
	DDB_CMD_HELP                    = 1,
	DDB_CMD_QUIT                    = 2,
	DDB_CMD_LS                      = 3,
	DDB_CMD_OPEN                    = 4,
	DDB_CMD_VERSION                 = 5,
	DDB_CMD_CLOSE                   = 6,
	DDB_CMD_SUPERBLOCK_DUMP         = 7,
	DDB_CMD_VALUE_DUMP              = 8,
	DDB_CMD_RM                      = 9,
	DDB_CMD_VALUE_LOAD              = 10,
	DDB_CMD_ILOG_DUMP               = 11,
	DDB_CMD_ILOG_COMMIT             = 12,
	DDB_CMD_ILOG_CLEAR              = 13,
	DDB_CMD_DTX_DUMP                = 14,
	DDB_CMD_DTX_CMT_CLEAR           = 15,
	DDB_CMD_SMD_SYNC                = 16,
	DDB_CMD_VEA_DUMP                = 17,
	DDB_CMD_VEA_UPDATE              = 18,
	DDB_CMD_DTX_ACT_COMMIT          = 19,
	DDB_CMD_DTX_ACT_ABORT           = 20,
	DDB_CMD_FEATURE                 = 21,
	DDB_CMD_RM_POOL                 = 22,
	DDB_CMD_DTX_ACT_DISCARD_INVALID = 23,
	DDB_CMD_DEV_LIST                = 24,
	DDB_CMD_DEV_REPLACE             = 25,
};

/* option and argument structures for commands that need them */
struct ls_options {
	bool recursive;
	bool details;
	char *path;
};

struct open_options {
	bool write_mode;
	char *path;
};

struct value_dump_options {
	char *path;
	char *dst;
};

struct rm_options {
	char *path;
};

struct value_load_options {
	char *src;
	char *dst;
};

struct ilog_dump_options {
	char *path;
};

struct ilog_commit_options {
	char *path;
};

struct ilog_clear_options {
	char *path;
};

struct dtx_dump_options {
	bool active;
	bool committed;
	char *path;
};

struct dtx_cmt_clear_options {
	char *path;
};

struct smd_sync_options {
	char *nvme_conf;
	char *db_path;
};

struct vea_update_options {
	char *offset;
	char *blk_cnt;
};

struct dtx_act_options {
	char *path;
	char *dtx_id;
};

struct feature_options {
	uint64_t    set_compat_flags;
	uint64_t    set_incompat_flags;
	uint64_t    clear_compat_flags;
	uint64_t    clear_incompat_flags;
	bool        show_features;
	const char *path;
};

struct rm_pool_options {
	const char *path;
};

struct dev_list_options {
	char *db_path;
};

struct dev_replace_options {
	char *db_path;
	char *old_devid;
	char *new_devid;
};

struct ddb_cmd_info {
	enum ddb_cmd dci_cmd;
	union {
		struct ls_options dci_ls;
		struct open_options dci_open;
		struct value_dump_options dci_value_dump;
		struct rm_options dci_rm;
		struct value_load_options dci_value_load;
		struct ilog_dump_options dci_ilog_dump;
		struct ilog_commit_options dci_ilog_commit;
		struct ilog_clear_options dci_ilog_clear;
		struct dtx_dump_options dci_dtx_dump;
		struct dtx_cmt_clear_options dci_dtx_cmt_clear;
		struct smd_sync_options dci_smd_sync;
		struct vea_update_options     dci_vea_update;
		struct feature_options        dci_feature;
		struct rm_pool_options        dci_rm_pool;
		struct dtx_act_options        dci_dtx_act;
		struct dev_list_options       dci_dev_list;
		struct dev_replace_options    dci_dev_replace;
	} dci_cmd_option;
};

int ddb_parse_cmd_args(struct ddb_ctx *ctx, uint32_t argc, char **argv, struct ddb_cmd_info *info);
int
ddb_parse_cmd_str(struct ddb_ctx *ctx, const char *cmd_str, bool *open);
int
     ddb_run_cmd(struct ddb_ctx *ctx, const char *cmd_str);
/* Run commands ... */
int ddb_run_help(struct ddb_ctx *ctx);
int ddb_run_quit(struct ddb_ctx *ctx);
int ddb_run_ls(struct ddb_ctx *ctx, struct ls_options *opt);
bool ddb_pool_is_open(struct ddb_ctx *ctx);
int ddb_run_open(struct ddb_ctx *ctx, struct open_options *opt);
int ddb_run_version(struct ddb_ctx *ctx);
int ddb_run_close(struct ddb_ctx *ctx);
int ddb_run_superblock_dump(struct ddb_ctx *ctx);
int ddb_run_value_dump(struct ddb_ctx *ctx, struct value_dump_options *opt);
int ddb_run_rm(struct ddb_ctx *ctx, struct rm_options *opt);
int ddb_run_value_load(struct ddb_ctx *ctx, struct value_load_options *opt);
int ddb_run_ilog_dump(struct ddb_ctx *ctx, struct ilog_dump_options *opt);
int ddb_run_ilog_commit(struct ddb_ctx *ctx, struct ilog_commit_options *opt);
int ddb_run_ilog_clear(struct ddb_ctx *ctx, struct ilog_clear_options *opt);
int ddb_run_dtx_dump(struct ddb_ctx *ctx, struct dtx_dump_options *opt);
int ddb_run_dtx_cmt_clear(struct ddb_ctx *ctx, struct dtx_cmt_clear_options *opt);
int ddb_run_smd_sync(struct ddb_ctx *ctx, struct smd_sync_options *opt);
int ddb_run_vea_dump(struct ddb_ctx *ctx);
int ddb_run_vea_update(struct ddb_ctx *ctx, struct vea_update_options *opt);
int
ddb_run_dtx_act_commit(struct ddb_ctx *ctx, struct dtx_act_options *opt);
int
ddb_run_dtx_act_abort(struct ddb_ctx *ctx, struct dtx_act_options *opt);
int
ddb_run_feature(struct ddb_ctx *ctx, struct feature_options *opt);
int
ddb_feature_string2flags(struct ddb_ctx *ctx, const char *string, uint64_t *compat_flags,
			 uint64_t *incompat_flags);
int
ddb_run_rm_pool(struct ddb_ctx *ctx, struct rm_pool_options *opt);
int
     ddb_run_dtx_act_discard_invalid(struct ddb_ctx *ctx, struct dtx_act_options *opt);
int
ddb_run_dev_list(struct ddb_ctx *ctx, struct dev_list_options *opt);
int
     ddb_run_dev_replace(struct ddb_ctx *ctx, struct dev_replace_options *opt);

void ddb_program_help(struct ddb_ctx *ctx);
void ddb_commands_help(struct ddb_ctx *ctx);

#endif /* __DDB_RUN_CMDS_H */
