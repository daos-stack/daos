/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(client)

#ifndef __DAOS_S3_INTERNAL_H__
#define __DAOS_S3_INTERNAL_H__

#include <fcntl.h>
#include "daos.h"
#include "daos_fs.h"
#include "daos_s3.h"
#include <daos/event.h>

#define METADATA_BUCKET        "_METADATA"
#define MULTIPART_MAX_PARTS    10000
#define LATEST_INSTANCE        "latest"
#define LATEST_INSTANCE_SUFFIX "[latest]"
#define RGW_BUCKET_INFO        "rgw_info"

/* #define METADATA_DIR_LIST \
// 	X(USERS_DIR, "users")                                                                      \
// 	X(EMAILS_DIR, "emails")                                                                    \
// 	X(ACCESS_KEYS_DIR, "access_keys")                                                          \
// 	X(MULTIPART_DIR, "multipart")
*/

/* Define for RPC enum population below */
// #define X(a, b) a,
// enum meta_dir { METADATA_DIR_LIST METADATA_DIR_LAST };
// #undef X

/** DAOS S3 Pool handle */
// struct ds3 {
// 	/** Pool handle */
// 	daos_handle_t    poh;
// 	/** Pool information */
// 	daos_pool_info_t pinfo;
// 	/** Metadata container handle */
// 	daos_handle_t    meta_coh;
// 	/** Metadata dfs mount */
// 	dfs_t           *meta_dfs;
// 	/** Array of metadata dir handle */
// 	dfs_obj_t       *meta_dirs[METADATA_DIR_LAST];
// };

// /** DAOS S3 Bucket handle */
// struct ds3_bucket {
// 	/** DAOS container handle */
// 	daos_handle_t    coh;
// 	/** Container information */
// 	daos_cont_info_t cont_info;
// 	/** DFS handle */
// 	dfs_t           *dfs;
// };

/** DAOS S3 Object handle */
struct ds3_obj {
};

/** Helper function, returns the meta dir name from the enum value */
const char *
meta_dir_name(enum meta_dir dir);

#endif
