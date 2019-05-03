/**
 * (C) Copyright 2016-2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
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
 *       Container properties KVS (GENERIC):
 *         HCE KVS (INTEGER)
 *         LRE KVS (INTEGER)
 *         LHE KVS (INTEGER)
 *         Snapshot KVS (INTEGER)
 *         User Attributes KVS (GENERIC)
 *       ... (more container properties KVSs)
 *     Container handle KVS (GENERIC)
 */

#ifndef __CONTAINER_SRV_LAYOUT_H__
#define __CONTAINER_SRV_LAYOUT_H__

#include <daos_types.h>

/* Root KVS (RDB_KVS_GENERIC) */
extern d_iov_t ds_cont_prop_conts;		/* container KVS */
extern d_iov_t ds_cont_prop_cont_handles;	/* container handle KVS */

/*
 * Container KVS (RDB_KVS_GENERIC)
 *
 * This maps container UUIDs (uuid_t) to container properties KVSs.
 */

/*
 * Container properties KVS (RDB_KVS_GENERIC)
 *
 * 1-level KV pairs - ghce, max_oid and the optional properties (label,
 * layout type etc.).
 *
 * And KVS (with next level KV-pairs):
 * Snapshot KVS (RDB_KVS_INTEGER) -
 * This KVS stores an ordered list of snapshotted epochs. The values are
 * unused and empty.
 * User-defined attributes (RDB_KVS_GENERIC) -
 * To store container attributes of upper layers.
 */
extern d_iov_t ds_cont_prop_ghce;		/* uint64_t */
extern d_iov_t ds_cont_prop_max_oid;		/* uint64_t */
extern d_iov_t ds_cont_prop_label;		/* uint64_t */
extern d_iov_t ds_cont_prop_layout_type;	/* uint64_t */
extern d_iov_t ds_cont_prop_layout_ver;		/* uint64_t */
extern d_iov_t ds_cont_prop_csum;		/* uint64_t */
extern d_iov_t ds_cont_prop_redun_fac;		/* uint64_t */
extern d_iov_t ds_cont_prop_redun_lvl;		/* uint64_t */
extern d_iov_t ds_cont_prop_snapshot_max;	/* uint64_t */
extern d_iov_t ds_cont_prop_compress;		/* uint64_t */
extern d_iov_t ds_cont_prop_encrypt;		/* uint64_t */
extern d_iov_t ds_cont_prop_snapshots;		/* snapshot KVS */
extern d_iov_t ds_cont_attr_user;		/* User attributes KVS */

/*
 * Container handle KVS (RDB_KVS_GENERIC)
 *
 * A key is a container handle UUID (uuid_t). A value is a container_hdl object.
 */
struct container_hdl {
	uuid_t		ch_pool_hdl;
	uuid_t		ch_cont;
	uint64_t	ch_hce;
	uint64_t	ch_capas;
};

extern daos_prop_t cont_prop_default;

#define CONT_PROP_NUM	(DAOS_PROP_CO_MAX - DAOS_PROP_CO_MIN - 1)

#endif /* __CONTAINER_SRV_LAYOUT_H__ */
