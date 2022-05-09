/**
 * (C) Copyright 2019-2022 Intel Corporation.
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
	char *pa_pool_uuid;
};

/* Parse a string into an array of words with the count of words */
int ddb_str2argv_create(const char *buf, struct argv_parsed *parse_args);

/* Free resources used for str2argv */
void ddb_str2argv_free(struct argv_parsed *parse_args);

/* Parse argc/argv into the program arguments/options */
int
ddb_parse_program_args(struct ddb_ctx *ctx, uint32_t argc, char **argv, struct program_args *pa);

/* Parse a string into the parts of a vos tree path (cont, object, ...) */
int ddb_vtp_init(daos_handle_t poh, const char *path, struct dv_tree_path_builder *vt_path);
void ddb_vtp_fini(struct dv_tree_path_builder *vt_path);

#define DDB_IDX_UNSET ((uint32_t)-1)
static inline void ddb_vos_tree_path_setup(struct dv_tree_path_builder *vt_path)
{
	vt_path->vtp_cont_idx = DDB_IDX_UNSET;
	vt_path->vtp_oid_idx = DDB_IDX_UNSET;
	vt_path->vtp_dkey_idx = DDB_IDX_UNSET;
	vt_path->vtp_akey_idx = DDB_IDX_UNSET;
	vt_path->vtp_recx_idx = DDB_IDX_UNSET;
}

#endif /** __DAOS_DDB_PARSE_H */
