/**
 * (C) Copyright 2015-2020 Intel Corporation.
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
 * DAOS pool/container initialization properties
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
	/**
	 * The pool svc rank list.
	 */
	DAOS_PROP_PO_SVC_LIST,
	DAOS_PROP_PO_MAX,
};

/**
 * Number of pool property types
 */
#define DAOS_PROP_PO_NUM	(DAOS_PROP_PO_MAX - DAOS_PROP_PO_MIN - 1)

/** DAOS space reclaim strategy */
enum {
	DAOS_RECLAIM_DISABLED = 0,
	DAOS_RECLAIM_LAZY,
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
	 * RF(n): Container I/O restricted after n faults.
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
	/**
	 * ACL: access control list for container
	 * An ordered list of access control entries detailing user and group
	 * access privileges.
	 * Expected to be in the order: Owner, User(s), Group(s), Everyone
	 */
	DAOS_PROP_CO_ACL,
	/**
	 * Determine whether inline compression is enabled
	 * Value: DAOS_PROP_CO_COMPRESS_OFF/LZ4/DEFLATE[1-4]
	 * Default: DAOS_PROP_CO_COMPRESS_OFF
	 */
	DAOS_PROP_CO_COMPRESS,
	/**
	 * Determine whether encryption is enabled
	 * Value:
	 * DAOS_PROP_CO_ENCRYPT_OFF,
	 * DAOS_PROP_CO_ENCRYPT_AES_XTS{128,256},
	 * DAOS_PROP_CO_ENCRYPT_AES_CBC{128,192,256},
	 * DAOS_PROP_CO_ENCRYPT_AES_GCM{128,256}
	 * Default: DAOS_PROP_CO_ENCRYPT_OFF
	 */
	DAOS_PROP_CO_ENCRYPT,
	/**
	 * The user who acts as the owner of the container.
	 * Format: user@[domain]
	 */
	DAOS_PROP_CO_OWNER,
	/**
	 * The group that acts as the owner of the container.
	 * Format: group@[domain]
	 */
	DAOS_PROP_CO_OWNER_GROUP,
	/**
	 * Determine whether deduplication is enabled
	 * Require checksum to be enabled
	 * Value DAOS_PROP_CO_DEDUP_OFF/MEMCMP/HASH
	 * Default: DAOS_PROP_CO_DEDUP_OFF
	 */
	DAOS_PROP_CO_DEDUP,
	/**
	 * Deduplication threshold size
	 * Default: 4K
	 */
	DAOS_PROP_CO_DEDUP_THRESHOLD,
	DAOS_PROP_CO_MAX,
};

/**
 * Number of container property types
 */
#define DAOS_PROP_CO_NUM	(DAOS_PROP_CO_MAX - DAOS_PROP_CO_MIN - 1)

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
	DAOS_PROP_CO_CSUM_SHA1,
	DAOS_PROP_CO_CSUM_SHA256,
	DAOS_PROP_CO_CSUM_SHA512
};

/** container checksum server verify */
enum {
	DAOS_PROP_CO_CSUM_SV_OFF,
	DAOS_PROP_CO_CSUM_SV_ON
};

/** container deduplication */
enum {
	DAOS_PROP_CO_DEDUP_OFF,
	DAOS_PROP_CO_DEDUP_MEMCMP,
	DAOS_PROP_CO_DEDUP_HASH
};

/** container compression type */
enum {
	DAOS_PROP_CO_COMPRESS_OFF,
	DAOS_PROP_CO_COMPRESS_LZ4,
	DAOS_PROP_CO_COMPRESS_DEFLATE, /** deflate default */
	DAOS_PROP_CO_COMPRESS_DEFLATE1,
	DAOS_PROP_CO_COMPRESS_DEFLATE2,
	DAOS_PROP_CO_COMPRESS_DEFLATE3,
	DAOS_PROP_CO_COMPRESS_DEFLATE4,
};

/** container encryption type */
enum {
	DAOS_PROP_CO_ENCRYPT_OFF,
	DAOS_PROP_CO_ENCRYPT_AES_XTS128,
	DAOS_PROP_CO_ENCRYPT_AES_XTS256,
	DAOS_PROP_CO_ENCRYPT_AES_CBC128,
	DAOS_PROP_CO_ENCRYPT_AES_CBC192,
	DAOS_PROP_CO_ENCRYPT_AES_CBC256,
	DAOS_PROP_CO_ENCRYPT_AES_GCM128,
	DAOS_PROP_CO_ENCRYPT_AES_GCM256
};

/** container redundancy factor */
enum {
	DAOS_PROP_CO_REDUN_RF0,
	DAOS_PROP_CO_REDUN_RF1,
	DAOS_PROP_CO_REDUN_RF2,
	DAOS_PROP_CO_REDUN_RF3,
	DAOS_PROP_CO_REDUN_RF4,
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
 * Free the DAOS property entries.
 *
 * \param[in]	prop	property entries to be freed.
 */
void
daos_prop_entries_free(daos_prop_t *prop);


/**
 * Free the DAOS properties.
 *
 * \param[in]	prop	properties to be freed.
 */
void
daos_prop_free(daos_prop_t *prop);

/**
 * Merge a set of new DAOS properties into a set of existing DAOS properties.
 *
 * \param[in]	old_prop	Existing set of properties
 * \param[in]	new_prop	New properties - may override old entries
 *
 * \return	Newly allocated merged property
 */
daos_prop_t *
daos_prop_merge(daos_prop_t *old_prop, daos_prop_t *new_prop);

/**
 * Duplicate a generic pointer value from one DAOS prop entry to another.
 * Convenience function.
 *
 * \param[in][out]	entry_dst	Destination entry
 * \param[in]		entry_src	Entry to be copied
 * \param[in]		len		Length of the memory to be copied
 *
 * \return		0		Success
 *			-DER_NOMEM	Out of memory
 */
int
daos_prop_entry_dup_ptr(struct daos_prop_entry *entry_dst,
			struct daos_prop_entry *entry_src, size_t len);

/**
 * Compare a pair of daos_prop_entry that contain ACLs.
 *
 * \param	entry1	DAOS prop entry for ACL
 * \param	entry2	DAOS prop entry for ACL
 *
 * \return	0		Entries match
 *		-DER_MISMATCH	Entries do NOT match
 */
int
daos_prop_entry_cmp_acl(struct daos_prop_entry *entry1,
			struct daos_prop_entry *entry2);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_PROP_H__ */
