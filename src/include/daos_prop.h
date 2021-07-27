/**
 * (C) Copyright 2015-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * DAOS pool/container initialization properties
 */

#ifndef __DAOS_PROP_H__
#define __DAOS_PROP_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <ctype.h>
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
	DAOS_PROP_PO_EC_CELL_SZ,
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
	 * Layout type: unknown, POSIX, HDF5, Python, Database, Parquet, ...
	 * default value = DAOS_PROP_CO_LAYOUT_UNKNOWN
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
	 * default = RF0 (DAOS_PROP_CO_REDUN_RF0)
	 */
	DAOS_PROP_CO_REDUN_FAC,
	/**
	 * Redundancy level: default fault domain level for placement.
	 * default = 1 (rank level)
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
	/** First citizen objects of container, see \a daos_cont_root_oids */
	DAOS_PROP_CO_ROOTS,
	/**
	 * Container status
	 * Value "struct daos_co_status".
	 */
	DAOS_PROP_CO_STATUS,
	/** OID value to start allocation from */
	DAOS_PROP_CO_ALLOCED_OID,
	/** EC cell size, it can overwrite DAOS_PROP_EC_CELL_SZ of pool */
	DAOS_PROP_CO_EC_CELL_SZ,
	DAOS_PROP_CO_MAX,
};

/** first citizen objects of a container, stored as container property */
struct daos_prop_co_roots {
	daos_obj_id_t	cr_oids[4];
};

/**
 * Number of container property types
 */
#define DAOS_PROP_CO_NUM	(DAOS_PROP_CO_MAX - DAOS_PROP_CO_MIN - 1)

typedef uint16_t daos_cont_layout_t;

/** container layout type */
enum {
	DAOS_PROP_CO_LAYOUT_UNKNOWN,
	DAOS_PROP_CO_LAYOUT_UNKOWN = DAOS_PROP_CO_LAYOUT_UNKNOWN,
	DAOS_PROP_CO_LAYOUT_POSIX,	/** DFS/dfuse/MPI-IO */
	DAOS_PROP_CO_LAYOUT_HDF5,	/** HDF5 DAOS VOL connector */
	DAOS_PROP_CO_LAYOUT_PYTHON,	/** PyDAOS */
	DAOS_PROP_CO_LAYOUT_SPARK,	/** Specific layout for Spark shuffle */
	DAOS_PROP_CO_LAYOUT_DATABASE,	/** SQL Database */
	DAOS_PROP_CO_LAYOUT_ROOT,	/** ROOT/RNTuple format */
	DAOS_PROP_CO_LAYOUT_SEISMIC,	/** Seismic Graph, aka SEGY */
	DAOS_PROP_CO_LAYOUT_METEO,	/** Meteorology, aka Field Data Base */
	DAOS_PROP_CO_LAYOUT_MAX
};

/** container checksum type */
enum {
	DAOS_PROP_CO_CSUM_OFF,
	DAOS_PROP_CO_CSUM_CRC16,
	DAOS_PROP_CO_CSUM_CRC32,
	DAOS_PROP_CO_CSUM_CRC64,
	DAOS_PROP_CO_CSUM_SHA1,
	DAOS_PROP_CO_CSUM_SHA256,
	DAOS_PROP_CO_CSUM_SHA512,
	DAOS_PROP_CO_CSUM_ADLER32
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

/**
 * Level of fault-domain to use for object allocation
 * rank is hardcoded to 1, [2-254] are defined by the admin
 */
enum {
	DAOS_PROP_CO_REDUN_MIN	= 1,
	DAOS_PROP_CO_REDUN_RANK	= 1, /** hard-coded */
	DAOS_PROP_CO_REDUN_MAX	= 254,
};

/** container status flag */
enum {
	/* in healthy status, data protection work as expected */
	DAOS_PROP_CO_HEALTHY,
	/* in unclean status, data protection possibly cannot work.
	 * typical scenario - cascading failed targets exceed the container
	 * redundancy factor, that possibly cause lost data cannot be detected
	 * or rebuilt.
	 */
	DAOS_PROP_CO_UNCLEAN,
};

/** clear the UNCLEAN status */
#define DAOS_PROP_CO_CLEAR	(0x1)
struct daos_co_status {
	/** DAOS_PROP_CO_HEALTHY/DAOS_PROP_CO_UNCLEAN */
	uint16_t	dcs_status;
	/** flags for DAOS internal usage, DAOS_PROP_CO_CLEAR */
	uint16_t	dcs_flags;
	/** pool map version when setting the dcs_status */
	uint32_t	dcs_pm_ver;
};

#define DAOS_PROP_CO_STATUS_VAL(status, flag, pm_ver)			\
	((((uint64_t)(flag)) << 48)		|			\
	 (((uint64_t)(status) & 0xFFFF) << 32)	|			\
	 ((uint64_t)(pm_ver)))

static inline uint64_t
daos_prop_co_status_2_val(struct daos_co_status *co_status)
{
	return DAOS_PROP_CO_STATUS_VAL(co_status->dcs_status,
				       co_status->dcs_flags,
				       co_status->dcs_pm_ver);
}

static inline void
daos_prop_val_2_co_status(uint64_t val, struct daos_co_status *co_status)
{
	co_status->dcs_flags = (uint16_t)(val >> 48);
	co_status->dcs_status = (uint16_t)((val >> 32) & 0xFFFF);
	co_status->dcs_pm_ver = (uint32_t)(val & 0xFFFFFFFF);
}

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

/** max length for pool/container label - NB: POOL_LIST_CONT RPC wire format */
#define DAOS_PROP_LABEL_MAX_LEN		(127)

/**
 * Check if DAOS (pool or container property) label string is valid.
 * DAOS labels must consist only of alphanumeric characters, colon ':',
 * period '.' or underscore '_', and must be of length
 * [1 - DAOS_PROP_LABEL_MAX_LEN].
 *
 * \param[in]	label	Label string
 *
 * \return		true		Label meets length/format requirements
 *			false		Label is not valid length or format
 */
static inline bool
daos_label_is_valid(const char *label)
{
	int	len;
	int	i;

	/** Label cannot be NULL */
	if (label == NULL)
		return false;

	/** Check the length */
	len = strnlen(label, DAOS_PROP_LABEL_MAX_LEN + 1);
	if (len == 0 || len > DAOS_PROP_LABEL_MAX_LEN)
		return false;

	/** Verify that it contains only alphanumeric characters or :._ */
	for (i = 0; i < len; i++) {
		char c = label[i];

		if (isalnum(c) || c == '.' || c == '_' || c == ':')
			continue;

		return false;
	}

	return true;
}

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
daos_prop_fini(daos_prop_t *prop);

/**
 * Free the DAOS properties and the \a prop.
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
 * \param[in,out]	entry_dst	Destination entry
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

/**
 * Duplicate container roots from one DAOS prop entry to another.
 * Convenience function.
 *
 * \param[in,out]	dst		Destination entry
 * \param[in]		src		Entry to be copied
 *
 * \return		0		Success
 *			-DER_NOMEM	Out of memory
 */
int
daos_prop_entry_dup_co_roots(struct daos_prop_entry *dst,
			     struct daos_prop_entry *src);

/**
 * Check a DAOS prop entry for a string value.
 *
 * \param[in]		entry		Entry to be checked.
 *
 * \return		true		Has a string value.
 *			false		Does not have a string value.
 */
bool
daos_prop_has_str(struct daos_prop_entry *entry);

/**
 * Check a DAOS prop entry for a pointer value.
 *
 * \param[in]		entry		Entry to be checked.
 *
 * \return		true		Has a pointer value.
 *			false		Does not have a pointer value.
 */
bool
daos_prop_has_ptr(struct daos_prop_entry *entry);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_PROP_H__ */
