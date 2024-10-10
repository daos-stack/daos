/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(client)

#ifndef __DAOS_S3_INTERNAL_H__
#define __DAOS_S3_INTERNAL_H__

#include <fcntl.h>
#include <daos.h>
#include <daos_fs.h>
#include <daos_s3.h>
#include <daos/event.h>

#define METADATA_BUCKET        "_METADATA"
#define MULTIPART_MAX_PARTS    10000
#define LATEST_INSTANCE_SUFFIX "[" DS3_LATEST_INSTANCE "]"
#define RGW_BUCKET_INFO        "rgw_info"
#define RGW_DIR_ENTRY_XATTR    "rgw_entry"
#define RGW_KEY_XATTR          "rgw_key"
#define RGW_PART_XATTR         "rgw_part"

#define METADATA_DIR_LIST                                                                          \
	X(USERS_DIR, "users")                                                                      \
	X(EMAILS_DIR, "emails")                                                                    \
	X(ACCESS_KEYS_DIR, "access_keys")                                                          \
	X(MULTIPART_DIR, "multipart")

/* Define for RPC enum population below */
#define X(a, b) a,
enum meta_dir { METADATA_DIR_LIST METADATA_DIR_LAST };
#undef X

/** DAOS S3 Pool handle */
struct ds3 {
	/** Pool name */
	char             pool[DAOS_PROP_MAX_LABEL_BUF_LEN];
	/** Pool handle */
	daos_handle_t    poh;
	/** Pool information */
	daos_pool_info_t pinfo;
	/** Metadata DFS handle */
	dfs_t           *meta_dfs;
	/** Array of metadata dir handle */
	dfs_obj_t       *meta_dirs[METADATA_DIR_LAST];
};

/** DAOS S3 Bucket handle */
struct ds3_bucket {
	/** DFS handle */
	dfs_t *dfs;
};

/** DAOS S3 Object handle */
struct ds3_obj {
	/** DFS object handle */
	dfs_obj_t *dfs_obj;
};

/** DAOS S3 Upload Part handle */
struct ds3_part {
	/** DFS object handle */
	dfs_obj_t *dfs_obj;
};

/** Helper function, returns the meta dir name from the enum value */
const char *
meta_dir_name(enum meta_dir dir);

#endif
