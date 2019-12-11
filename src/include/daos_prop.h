/**
 * (C) Copyright 2015-2019 Intel Corporation.
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
 * DAOS pool/container intialization properties
 */

#ifndef __DAOS_PROP_H__
#define __DAOS_PROP_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <daos_types.h>

/**
 * DAOS pool property types
 * valid in range (DAOS_PROP_PO_MIN, DAOS_PROP_PO_MAX)
 */
enum daos_pool_props {
	DAOS_PROP_PO_MIN = 0,
	/**
	 * Label - a string that a user can associated with a pool.
	 * default = ""
	 */
	DAOS_PROP_PO_LABEL,
	/**
	 * ACL: access control list for pool
	 * An ordered list of access control entries detailing user and group
	 * access privileges.
	 * Expected to be in the order: Owner, User(s), Group(s), Everyone
	 */
	DAOS_PROP_PO_ACL,
	/**
	 * Reserve space ratio: amount of space to be reserved on each target
	 * for rebuild purpose. default = 0%.
	 */
	DAOS_PROP_PO_SPACE_RB,
	/**
	 * Automatic/manual self-healing. default = auto
	 * auto/manual exclusion
	 * auto/manual rebuild
	 */
	DAOS_PROP_PO_SELF_HEAL,
	/**
	 * Space reclaim strategy = time|batched|snapshot. default = snapshot
	 * time interval
	 * batched commits
	 * snapshot creation
	 */
	DAOS_PROP_PO_RECLAIM,
	/**
	 * The user who acts as the owner of the pool.
	 * Format: user@[domain]
	 */
	DAOS_PROP_PO_OWNER,
	/**
	 * The group that acts as the owner of the pool.
	 * Format: group@[domain]
	 */
	DAOS_PROP_PO_OWNER_GROUP,
	DAOS_PROP_PO_MAX,
};

/**
 * Number of pool property types
 */
#define DAOS_PROP_PO_NUM	(DAOS_PROP_PO_MAX - DAOS_PROP_PO_MIN - 1)

/** DAOS space reclaim strategy */
enum {
	DAOS_RECLAIM_SNAPSHOT,
	DAOS_RECLAIM_BATCH,
	DAOS_RECLAIM_TIME,
};

/** self headling strategy bits */
#define DAOS_SELF_HEAL_AUTO_EXCLUDE	(1U << 0)
#define DAOS_SELF_HEAL_AUTO_REBUILD	(1U << 1)

/**
 * DAOS container property types
 * valid in rage (DAOS_PROP_CO_MIN, DAOS_PROP_CO_MAX).
 */
enum daos_cont_props {
	DAOS_PROP_CO_MIN = 0x1000,
	/**
	 * Label - a string that a user can associated with a container.
	 * default = ""
	 */
	DAOS_PROP_CO_LABEL,
	/**
	 * Layout type: unknown, POSIX, MPI-IO, HDF5, Apache Arrow, ...
	 * default value = DAOS_PROP_CO_LAYOUT_UNKOWN
	 */
	DAOS_PROP_CO_LAYOUT_TYPE,
	/**
	 * Layout version: specific to middleware for interop.
	 * default = 1
	 */
	DAOS_PROP_CO_LAYOUT_VER,
	/**
	 * Checksum on/off + checksum type (CRC16, CRC32, SHA-1 & SHA-2).
	 * default = DAOS_PROP_CO_CSUM_OFF
	 */
	DAOS_PROP_CO_CSUM,
	/**
	 * Checksum chunk size
	 * default = 32K
	 */
	DAOS_PROP_CO_CSUM_CHUNK_SIZE,
	/**
	* Checksum verification on server. Value = ON/OFF
	* default = DAOS_PROP_CO_CSUM_SV_OFF
	*/
	DAOS_PROP_CO_CSUM_SERVER_VERIFY,
	/**
	 * Redundancy factor:
	 * RF0: Container I/O restricted after single fault.
	 * RF1: no data protection. scratched data.
	 * RF3: 3-way replication, EC 4+2, 8+2, 16+2
	 * default = RF1 (DAOS_PROP_CO_REDUN_RF1)
	 */
	DAOS_PROP_CO_REDUN_FAC,
	/**
	 * Redundancy level: default fault domain level for placement.
	 * default = rack (DAOS_PROP_CO_REDUN_RACK)
	 */
	DAOS_PROP_CO_REDUN_LVL,
	/**
	 * Maximum number of snapshots to retain.
	 */
	DAOS_PROP_CO_SNAPSHOT_MAX,
	/** ACL: access control list for container */
	DAOS_PROP_CO_ACL,
	/** Compression on/off + compression type */
	DAOS_PROP_CO_COMPRESS,
	/** Encryption on/off + encryption type */
	DAOS_PROP_CO_ENCRYPT,
	DAOS_PROP_CO_MAX,
};

typedef uint16_t daos_cont_layout_t;

/** container layout type */
enum {
	DAOS_PROP_CO_LAYOUT_UNKOWN,
	DAOS_PROP_CO_LAYOUT_POSIX,
	DAOS_PROP_CO_LAYOUT_HDF5,
};

/** container checksum type */
enum {
	DAOS_PROP_CO_CSUM_OFF,
	DAOS_PROP_CO_CSUM_CRC16,
	DAOS_PROP_CO_CSUM_CRC32,
	DAOS_PROP_CO_CSUM_CRC64,
	DAOS_PROP_CO_CSUM_SHA1
};

/** container checksum server verify */
enum {
	DAOS_PROP_CO_CSUM_SV_OFF,
	DAOS_PROP_CO_CSUM_SV_ON
};


/** container compress type */
enum {
	DAOS_PROP_CO_COMPRESS_OFF,
};

/** container encryption type */
enum {
	DAOS_PROP_CO_ENCRYPT_OFF,
};

/** container redundancy factor */
enum {
	DAOS_PROP_CO_REDUN_RF0,
	DAOS_PROP_CO_REDUN_RF1,
	DAOS_PROP_CO_REDUN_RF3,
};

enum {
	DAOS_PROP_CO_REDUN_RACK,
	DAOS_PROP_CO_REDUN_NODE,
};

struct daos_prop_entry {
	/** property type, see enum daos_pool_props/daos_cont_props */
	uint32_t		 dpe_type;
	/** reserved for future usage (for 64 bits alignment now) */
	uint32_t		 dpe_reserv;
	/**
	 * value can be either a uint64_t, or a string, or any other type
	 * data such as the struct daos_acl pointer.
	 */
	union {
		uint64_t	 dpe_val;
		d_string_t	 dpe_str;
		void		*dpe_val_ptr;
	};
};

/** Allowed max number of property entries in daos_prop_t */
#define DAOS_PROP_ENTRIES_MAX_NR	(128)
/** max length for pool/container label */
#define DAOS_PROP_LABEL_MAX_LEN		(256)

/** daos properties, for pool or container */
typedef struct {
	/** number of entries */
	uint32_t		 dpp_nr;
	/** reserved for future usage (for 64 bits alignment now) */
	uint32_t		 dpp_reserv;
	/** property entries array */
	struct daos_prop_entry	*dpp_entries;
} daos_prop_t;

/**
 * Allocate DAOS properties.
 *
 * \param[in]	entries_nr	number of entries
 *
 * \return	allocated daos_prop_t pointer, NULL if failed.
 */
daos_prop_t *
daos_prop_alloc(uint32_t entries_nr);

/**
 * Free the DAOS properties.
 *
 * \param[in]	prop	properties to be freed.
 */
void
daos_prop_free(daos_prop_t *prop);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_PROP_H__ */
