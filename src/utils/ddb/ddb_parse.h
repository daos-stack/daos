/**
 * (C) Copyright 2019-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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

#define DB_PATH_LEN 64
struct vos_file_parts {
	char            vf_db_path[DB_PATH_LEN];
	uuid_t		vf_pool_uuid;
	char		vf_vos_file[16];
	uint32_t	vf_target_idx;
};

/* Parse a path to a VOS file to get needed parts for initializing vos */
int vos_path_parse(const char *path, struct vos_file_parts *vos_file_parts);

/* See ddb_iov_to_printable_buf for how the keys will be printed */
int ddb_parse_key(const char *input, daos_key_t *key);

/* Parse a string into the parts of a dtx_id. See DF_DTIF for how the format of the dtx_id is
 * expected to be.
 */
int ddb_parse_dtx_id(const char *dtx_id_str, struct dtx_id *dtx_id);

/* Parse a string representing a date into a DTX commit time */
int
ddb_date2cmt_time(const char *date, uint64_t *cmt_time);

#endif /** __DAOS_DDB_PARSE_H */
