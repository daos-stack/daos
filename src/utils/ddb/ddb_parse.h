/**
 * (C) Copyright 2019-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_DDB_PARSE_H
#define __DAOS_DDB_PARSE_H

#include <linux/limits.h>
#include <stdint.h>
#include <uuid/uuid.h>
#include <daos_obj.h>
#include <daos_types.h>
#include "ddb_common.h"

struct program_args {
	char *pa_cmd_file;
	char *pa_r_cmd_run;
	char *pa_pool_path;
	bool  pa_write_mode;
	bool  pa_get_help;
};
#define DB_PATH_LEN 64
struct vos_file_parts {
	char		vf_db_path[DB_PATH_LEN];
	uuid_t		vf_pool_uuid;
	char		vf_vos_file[16];
	uint32_t	vf_target_idx;
};

/* Parse a path to a VOS file to get needed parts for initializing vos */
int vos_path_parse(const char *path, struct vos_file_parts *vos_file_parts);

/* Parse a string into an array of words with the count of words */
int ddb_str2argv_create(const char *buf, struct argv_parsed *parse_args);

/* Free resources used for str2argv */
void ddb_str2argv_free(struct argv_parsed *parse_args);

/* Parse argc/argv into the program arguments/options */
int ddb_parse_program_args(struct ddb_ctx *ctx, uint32_t argc, char **argv,
			   struct program_args *pa);

/* See ddb_iov_to_printable_buf for how the keys will be printed */
int ddb_parse_key(const char *input, daos_key_t *key);

/* Parse a string into the parts of a dtx_id. See DF_DTIF for how the format of the dtx_id is
 * expected to be.
 */
int ddb_parse_dtx_id(const char *dtx_id_str, struct dtx_id *dtx_id);

#endif /** __DAOS_DDB_PARSE_H */
