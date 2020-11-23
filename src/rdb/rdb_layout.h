/*
 * (C) Copyright 2017-2020 Intel Corporation.
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
 * \file
 *
 * rdb: Storage Layout
 *
 * A database replica stores its persistent state in a dedicated VOS pool, with
 * the following layout:
 *
 *   Pool <db_uuid>			// database
 *     Container <db_uuid>		// metadata container (MC)
 *       Object RDB_MC_ATTRS		// attribute object
 *         D-key rdb_dkey
 *           A-key rdb_mc_uuid		// <db_uuid> (see rdb_create())
 *           A-key rdb_mc_term		// term
 *           A-key rdb_mc_vote		// vote for term
 *           A-key rdb_mc_lc		// log container record
 *     Container <lc_uuid>		// log container (LC)
 *       Object RDB_LC_ATTRS		// attribute object
 *         D-key rdb_dkey
 *           A-key rdb_lc_entry_header	// log entry header
 *           A-key rdb_lc_entry_data	// log entry data
 *           A-key rdb_lc_nreplicas	// number of replicas
 *           A-key rdb_lc_replicas	// replica ranks
 *           A-key rdb_lc_oid_next	// result for next object ID allocation
 *           A-key rdb_lc_root		// <root_oid>
 *       Object <root_oid>		// root KVS
 *         D-key rdb_dkey
 *           A-key <kvs0>		// <kvs0_oid>
 *           ...
 *       Object <kvs0_oid>		// root/kvs0
 *         D-key rdb_dkey
 *           ...
 *       ...
 *
 * D-keys are insignificant in the layout. Every object has only one d-key
 * equal to rdb_dkey. A-keys are all DAOS_IOD_SINGLE.
 *
 * The database metadata, such as the Raft term and vote, are stored in a
 * metadata container (MC). To facilitate the bootstrapping of the layout, the
 * MC shares the same UUID with the pool. All queries and updates in the MC use
 * the same epoch RDB_MC_EPOCH.
 *
 * The database contents are stored in log containers (LCs). Each LC has a
 * regular UUID, recorded in the MC under the log container record attribute.
 * Each epoch of an LC is also a Raft log entry index. For instance, epoch 10
 * consists of updates in Raft log entry 10. Querying epoch 10 will see the
 * snapshot represented by Raft log entry 10.
 *
 * Each container contains a object storing attributes. These objects have
 * static IDs: RDB_MC_ATTRS for MCs or RDB_LC_ATTRS for LCs. In an LC, we have
 * the following mapping:
 *
 *   RDB	VOS
 *   --------------------------------
 *   KVS	Object
 *   Key	A-key
 *   Value	DAOS_IOD_SINGLE value
 *
 * Objects representing user KVSs have dynamically allocated IDs, beginning
 * from RDB_LC_OID_NEXT_INIT.
 */

#ifndef RDB_LAYOUT_H
#define RDB_LAYOUT_H

/*
 * Object ID
 *
 * The highest bit represents the object ID class. The remaining 63 bits
 * represent the object number, which must be nonzero.
 */
typedef uint64_t rdb_oid_t;

/* Object ID class (see rdb_oid_t) */
#define RDB_OID_CLASS_MASK	(1ULL << 63)
#define RDB_OID_CLASS_GENERIC	(0ULL << 63)
#define RDB_OID_CLASS_INTEGER	(1ULL << 63)

/* D-key for all a-keys */
extern d_iov_t rdb_dkey;

/* pm_ver for all VOS calls taking a pm_ver argument */
#define RDB_PM_VER 0

/* Anchor for iterating a container */
struct rdb_anchor {
	daos_anchor_t	da_object;
	daos_anchor_t	da_akey;
};

/* Metadata container (MC) ****************************************************/

/*
 * Epoch for all vos_obj_fetch() and vos_obj_update() calls in the metadata
 * container
 */
#define RDB_MC_EPOCH 1

/* Attribute object ID */
#define RDB_MC_ATTRS (RDB_OID_CLASS_GENERIC | 1)

/*
 * Attribute a-keys under RDB_MC_ATTRS
 *
 * Flattened together with the Raft attributes. These are defined in
 * rdb_layout.c using RDB_STRING_KEY().
 */
extern d_iov_t rdb_mc_uuid;		/* uuid_t */
extern d_iov_t rdb_mc_term;		/* uint64_t */
extern d_iov_t rdb_mc_vote;		/* int */
extern d_iov_t rdb_mc_lc;		/* rdb_lc_record */
extern d_iov_t rdb_mc_slc;		/* rdb_lc_record */

/* Log container record */
struct rdb_lc_record {
	uuid_t			dlr_uuid;	/* of log container */
	uint64_t		dlr_base;	/* base index */
	uint64_t		dlr_base_term;	/* base term */
	uint64_t		dlr_tail;	/* last index + 1 */
	uint64_t		dlr_aggregated;	/* last aggregated index */
	uint64_t		dlr_term;	/* in which LC was created */
	uint64_t		dlr_seq;	/* last chunk sequence number */
	struct rdb_anchor	dlr_anchor;	/* last chunk anchor */
};

/* Log container (LC) *********************************************************/

/* Maximal log index */
#define RDB_LC_INDEX_MAX DAOS_EPOCH_MAX

/* Attribute object ID */
#define RDB_LC_ATTRS (RDB_OID_CLASS_GENERIC | 1)

/* Initial value for rdb_lc_oid_next (classless) */
#define RDB_LC_OID_NEXT_INIT 2

/* Attribute a-keys under RDB_LC_ATTRS */
extern d_iov_t rdb_lc_entry_header;	/* rdb_entry */
extern d_iov_t rdb_lc_entry_data;	/* uint8_t[] */
extern d_iov_t rdb_lc_nreplicas;	/* uint8_t */
extern d_iov_t rdb_lc_replicas;		/* uint32_t[] */
extern d_iov_t rdb_lc_oid_next;		/* rdb_oid_t (classless) */
extern d_iov_t rdb_lc_root;		/* rdb_oid_t */

/* Log entry */
struct rdb_entry {
	uint64_t	dre_term;
	uint32_t	dre_type;
	uint32_t	dre_size;	/* of entry data */
};

#endif /* RDB_LAYOUT_H */
