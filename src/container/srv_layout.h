/*
 * (C) Copyright 2016-2022 Intel Corporation.
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
 *
 * The version of the whole layout is defined by ds_pool_prop_global_version.
 */

#ifndef __CONTAINER_SRV_LAYOUT_H__
#define __CONTAINER_SRV_LAYOUT_H__

#include <daos_types.h>

/*
 * Root KVS (RDB_KVS_GENERIC)
 *
 * All keys are strings. Value types are specified for each key below.
 *
 * IMPORTANT! Please add new keys to this KVS like this:
 *
 *   extern d_iov_t ds_cont_prop_new_key;	comment_on_value_type
 *
 *   Note 1. The "new_key" name in ds_cont_prop_new_key must not appear in the
 *   root KVS in src/pool/srv_layout.h, that is, there must not be a
 *   ds_pool_prop_new_key, because the two root KVSs are the same RDB KVS.
 *
 *   Note 2. The comment_on_value_type shall focus on the value type only;
 *   usage shall be described above in this comment following existing
 *   examples. If the value is another KVS, its type shall be the KVS name.
 */
extern d_iov_t ds_cont_prop_cuuids;		/* container UUIDs KVS */
extern d_iov_t ds_cont_prop_conts;		/* container KVS */
extern d_iov_t ds_cont_prop_cont_handles;	/* container handle KVS */
/* Please read the IMPORTANT notes above before adding new keys. */

/*
 * Container UUIDs KVS (RDB_KVS_GENERIC)
 *
 * This maps container labels (string, without '\0') to container UUID (uuid_t).
 * Used to get UUID key for lookup in container KVS.
 */

/*
 * Container KVS (RDB_KVS_GENERIC)
 *
 * This maps container UUIDs (uuid_t) to container property KVSs.
 */

/*
 * Container property KVS (RDB_KVS_GENERIC)
 *
 * All keys are strings. Value types are specified for each key below.
 *
 * IMPORTANT! Please add new keys to this KVS like this:
 *
 *   extern d_iov_t ds_cont_prop_new_key;	comment_on_value_type
 *
 *   Note. The comment_on_value_type shall focus on the value type only;
 *   usage shall be described above in this comment following existing
 *   examples. If the value is another KVS, its type shall be the KVS name.
 */
extern d_iov_t ds_cont_prop_ghce;		/* uint64_t */
extern d_iov_t ds_cont_prop_alloced_oid;	/* uint64_t */
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
extern d_iov_t ds_cont_prop_acl;		/* daos_acl */
extern d_iov_t ds_cont_prop_owner;		/* string */
extern d_iov_t ds_cont_prop_owner_group;	/* string */
extern d_iov_t ds_cont_prop_nsnapshots;		/* uint32_t */
extern d_iov_t ds_cont_prop_snapshots;		/* snapshot KVS */
extern d_iov_t ds_cont_prop_co_status;		/* uint64_t */
extern d_iov_t ds_cont_attr_user;		/* user attribute KVS */
extern d_iov_t ds_cont_prop_handles;		/* handle index KVS */
extern d_iov_t ds_cont_prop_roots;		/* daos_prop_co_roots */
extern d_iov_t ds_cont_prop_ec_cell_sz;		/* uint64_t */
extern d_iov_t ds_cont_prop_ec_pda;		/* uint64_t */
extern d_iov_t ds_cont_prop_rp_pda;		/* uint64_t */
extern d_iov_t ds_cont_prop_perf_domain;	/* uint64_t */
extern d_iov_t ds_cont_prop_cont_global_version;/* uint32_t */
extern d_iov_t ds_cont_prop_scrubber_disabled;	/* uint64_t */
extern d_iov_t ds_cont_prop_co_md_times;	/* co_md_times */
extern d_iov_t ds_cont_prop_cont_obj_version;	/* uint32_t */
extern d_iov_t ds_cont_prop_nhandles;		/* uint32_t */
extern d_iov_t ds_cont_prop_oit_oids;		/* snapshot OIT oids KVS */
/* Please read the IMPORTANT notes above before adding new keys. */

struct co_md_times {
	uint64_t	otime;	/* container open time */
	uint64_t	mtime;	/* container metadata modify time */
};

/*
 * Snapshot KVS (RDB_KVS_INTEGER)
 *
 * A key is an epoch (daos_epoch_t). A value is an unused byte (char), as RDB
 * values must be nonempty.
 */

/*
 * User attribute KVS (RDB_KVS_GENERIC)
 *
 * A key is a (null-terminated) string. A value is a user-defined byte array.
 * Sizes of keys (or values) may vary.
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
extern daos_prop_t cont_prop_default_v0;

#define CONT_PROP_NUM_V0 20
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
