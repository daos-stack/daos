/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * ds_cont: Container Server Storage Layout
 *
 * This header assembles (hopefully) everything related to the persistent
 * storage layout of container metadata used by ds_cont.
 *
 * In the database of the combined pool/container service, we have this layout
 * for ds_cont:
 *
 *   Root KVS (GENERIC):
 *     Container KVS (GENERIC):
 *       Container property KVS (GENERIC):
 *         Snapshot KVS (INTEGER)
 *         User attribute KVS (GENERIC)
 *         Handle index KVS (GENERIC)
 *       ... (more container property KVSs)
 *     Container handle KVS (GENERIC)
 */

#ifndef __CONTAINER_SRV_LAYOUT_H__
#define __CONTAINER_SRV_LAYOUT_H__

#include <daos_types.h>

/*
 * Root KVS (RDB_KVS_GENERIC)
 *
 * All keys are strings. Value types are specified for each key below.
 */
extern d_iov_t ds_cont_prop_conts;		/* container KVS */
extern d_iov_t ds_cont_prop_cont_handles;	/* container handle KVS */

/*
 * Container KVS (RDB_KVS_GENERIC)
 *
 * This maps container UUIDs (uuid_t) to container property KVSs.
 */

/*
 * Container property KVS (RDB_KVS_GENERIC)
 *
 * All keys are strings. Value types are specified for each key below.
 */
extern d_iov_t ds_cont_prop_ghce;		/* uint64_t */
extern d_iov_t ds_cont_prop_max_oid;		/* uint64_t */
extern d_iov_t ds_cont_prop_label;		/* string */
extern d_iov_t ds_cont_prop_layout_type;	/* uint64_t */
extern d_iov_t ds_cont_prop_layout_ver;		/* uint64_t */
extern d_iov_t ds_cont_prop_csum;		/* uint64_t */
extern d_iov_t ds_cont_prop_csum_chunk_size;	/* uint64_t */
extern d_iov_t ds_cont_prop_csum_server_verify;	/* uint64_t */
extern d_iov_t ds_cont_prop_dedup;		/* uint64_t */
extern d_iov_t ds_cont_prop_dedup_threshold;	/* uint64_t */
extern d_iov_t ds_cont_prop_redun_fac;		/* uint64_t */
extern d_iov_t ds_cont_prop_redun_lvl;		/* uint64_t */
extern d_iov_t ds_cont_prop_snapshot_max;	/* uint64_t */
extern d_iov_t ds_cont_prop_compress;		/* uint64_t */
extern d_iov_t ds_cont_prop_encrypt;		/* uint64_t */
extern d_iov_t ds_cont_prop_acl;		/* struct daos_acl */
extern d_iov_t ds_cont_prop_owner;		/* string */
extern d_iov_t ds_cont_prop_owner_group;	/* string */
extern d_iov_t ds_cont_prop_snapshots;		/* snapshot KVS */
extern d_iov_t ds_cont_attr_user;		/* user attribute KVS */
extern d_iov_t ds_cont_prop_handles;		/* handle index KVS */

/*
 * Snapshot KVS (RDB_KVS_INTEGER)
 *
 * A key is an epoch (daos_epoch_t). A value is an unused byte (char), as RDB
 * values must be nonempty.
 */

/*
 * User attribute KVS (RDB_KVS_GENERIC)
 *
 * A key is a user-specified byte array. A value is also a user-specified byte
 * array.
 */

/*
 * Handle index KVS (RDB_KVS_GENERIC)
 *
 * A key is a container handle UUID (uuid_t). A value is an unused byte (char),
 * as RDB values must be nonempty. This KVS stores UUIDs of all handles of
 * _one_ container.
 */

/*
 * Container handle KVS (RDB_KVS_GENERIC)
 *
 * A key is a container handle UUID (uuid_t). A value is a container_hdl object.
 * This KVS stores handles of _all_ containers in the DB.
 */
struct container_hdl {
	uuid_t		ch_pool_hdl;
	uuid_t		ch_cont;
	uint64_t	ch_hce;
	uint64_t	ch_flags;
	uint64_t	ch_sec_capas;
};

extern daos_prop_t cont_prop_default;

#define CONT_PROP_NUM	(DAOS_PROP_CO_MAX - DAOS_PROP_CO_MIN - 1)

/**
 * Initialize the default container properties.
 *
 * \return	0		Success
 *		-DER_NOMEM	Out of memory
 */
int
ds_cont_prop_default_init(void);

/**
 * Clean up the default container properties.
 */
void
ds_cont_prop_default_fini(void);

#endif /* __CONTAINER_SRV_LAYOUT_H__ */
