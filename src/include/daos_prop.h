/**
 * (C) Copyright 2015-2023 Intel Corporation.
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
	/**
	 * Pool cell size.
	 */
	DAOS_PROP_PO_EC_CELL_SZ,
	/**
	 * Media selection policy
	 */
	DAOS_PROP_PO_POLICY,
	/**
	 * Pool redundancy factor.
	 */
	DAOS_PROP_PO_REDUN_FAC,
	/**
	 * The pool performance domain affinity level of EC object.
	 */
	DAOS_PROP_PO_EC_PDA,
	/**
	 * The pool performance domain affinity level
	 * of replicated object.
	 */
	DAOS_PROP_PO_RP_PDA,

	/**
	 * Aggregation of pool/container/object/key disk format
	 * version.
	 */
	DAOS_PROP_PO_GLOBAL_VERSION,
	/**
	 * Pool upgrade status.
	 */
	DAOS_PROP_PO_UPGRADE_STATUS,
	/*
	 * Schedule that the checksum scrubber will run. See
	 * DAOS_SCRUBBER_SCHED_*
	 *
	 * default: DAOS_SCRUB_MODE_OFF
	 */
	DAOS_PROP_PO_SCRUB_MODE,
	/**
	 * How frequently the schedule will run. In seconds.
	 *
	 * default: 604800 seconds (1 week)
	 */
	DAOS_PROP_PO_SCRUB_FREQ,
	/**
	 * Number of checksum errors before auto eviction is engaged.
	 *
	 * default: 0 (disabled)
	 */
	DAOS_PROP_PO_SCRUB_THRESH,
	/**
	 * Pool service redundancy factor.
	 */
	DAOS_PROP_PO_SVC_REDUN_FAC,
	/** object global version */
	DAOS_PROP_PO_OBJ_VERSION,
	/**
	 * The pool performance domain
	 */
	DAOS_PROP_PO_PERF_DOMAIN,
	/** Checkpoint mode, only applicable to MD_ON_SSD */
	DAOS_PROP_PO_CHECKPOINT_MODE,
	/** Frequency of timed checkpoint in seconds, default is 5 */
	DAOS_PROP_PO_CHECKPOINT_FREQ,
	/** WAL usage threshold to trigger checkpoint, default is 50% */
	DAOS_PROP_PO_CHECKPOINT_THRESH,
	/** Reintegration mode for pool, data_sync|no_data_sync default is data_sync*/
	DAOS_PROP_PO_REINT_MODE,
	DAOS_PROP_PO_MAX,
};

#define DAOS_PROP_PO_EC_CELL_SZ_MIN	(1UL << 10)
#define DAOS_PROP_PO_EC_CELL_SZ_MAX	(1UL << 30)

#define DAOS_PROP_PO_REDUN_FAC_MAX	4
#define DAOS_PROP_PO_REDUN_FAC_DEFAULT	0

static inline bool
daos_rf_is_valid(unsigned long long rf)
{
	return rf <= DAOS_PROP_PO_REDUN_FAC_MAX;
}

#define DAOS_PROP_PDA_MAX	((uint32_t)-1)
/**
 * The default PDA for replica object or non-replica obj (S1/S2/.../SX).
 * Default value (-1) means will try to put all replica shards of same RDG on same PD,
 * for non-replica obj will put all shards for the object within a PD if
 * the #targets in the PD is enough.
 */
#define DAOS_PROP_PO_RP_PDA_DEFAULT	DAOS_PROP_PDA_MAX
/**
 * the placement algorithm always tries to scatter shards of EC
 * object to different PDs.
 */
#define DAOS_PROP_PO_EC_PDA_DEFAULT	((uint32_t)1)

/** DAOS pool upgrade status */
enum {
	DAOS_UPGRADE_STATUS_NOT_STARTED = 0,
	DAOS_UPGRADE_STATUS_IN_PROGRESS = 1,
	DAOS_UPGRADE_STATUS_COMPLETED = 2,
	DAOS_UPGRADE_STATUS_FAILED = 3,
};

#define DAOS_PROP_PO_SVC_REDUN_FAC_MAX		4
#define DAOS_PROP_PO_SVC_REDUN_FAC_DEFAULT	2

static inline bool
daos_svc_rf_is_valid(uint64_t svc_rf)
{
	return svc_rf <= DAOS_PROP_PO_SVC_REDUN_FAC_MAX;
}

/**
 * Level of perf_domain, should be same value as PO_COMP_TP_xxx (enum pool_comp_type).
 */
enum {
	DAOS_PROP_PERF_DOMAIN_ROOT = 255,
	DAOS_PROP_PERF_DOMAIN_GROUP = 3,
};

/**
 * default performance domain is root
 */
#define DAOS_PROP_PO_PERF_DOMAIN_DEFAULT	DAOS_PROP_PERF_DOMAIN_ROOT
#define DAOS_PROP_CO_PERF_DOMAIN_DEFAULT	DAOS_PROP_PERF_DOMAIN_ROOT

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

enum {
	DAOS_REINT_MODE_DATA_SYNC = 0,
	DAOS_REINT_MODE_NO_DATA_SYNC = 1,
};

/**
 * default reintegration mode is data_sync
 */
#define DAOS_PROP_PO_REINT_MODE_DEFAULT	DAOS_REINT_MODE_DATA_SYNC

/**
 * Pool checksum scrubbing schedule type
 * It is expected that these stay contiguous.
 */
enum {
	DAOS_SCRUB_MODE_OFF = 0,
	DAOS_SCRUB_MODE_LAZY = 1,
	DAOS_SCRUB_MODE_TIMED = 2,
	DAOS_SCRUB_MODE_INVALID = 3,
};

/* Checksum Scrubbing Defaults */
#define DAOS_PROP_PO_SCRUB_MODE_DEFAULT DAOS_SCRUB_MODE_OFF

#define DAOS_PROP_PO_SCRUB_FREQ_DEFAULT 604800 /* 1 week in seconds */
#define DAOS_PROP_PO_SCRUB_THRESH_DEFAULT 0

/** Checkpoint strategy */
enum {
	DAOS_CHECKPOINT_DISABLED = 0,
	DAOS_CHECKPOINT_TIMED,
	DAOS_CHECKPOINT_LAZY,
};

#define DAOS_PROP_PO_CHECKPOINT_MODE_DEFAULT   DAOS_CHECKPOINT_TIMED
#define DAOS_PROP_PO_CHECKPOINT_FREQ_DEFAULT   5  /* 5 seconds */
#define DAOS_PROP_PO_CHECKPOINT_FREQ_MIN       1  /* 1 seconds */
#define DAOS_PROP_PO_CHECKPOINT_FREQ_MAX       (1 << 20) /* 1 million seconds */
#define DAOS_PROP_PO_CHECKPOINT_THRESH_DEFAULT 50 /* 50 % WAL capacity */
#define DAOS_PROP_PO_CHECKPOINT_THRESH_MAX     75 /* 75 % WAL capacity */
#define DAOS_PROP_PO_CHECKPOINT_THRESH_MIN     10 /* 10 % WAL capacity */

/** self healing strategy bits */
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
	/** EC cell size, it can overwrite DAOS_PROP_CO_EC_CELL_SZ of pool */
	DAOS_PROP_CO_EC_CELL_SZ,
	/** Performance domain affinity level of EC object */
	DAOS_PROP_CO_EC_PDA,
	/**  performance domain affinity level of RP object */
	DAOS_PROP_CO_RP_PDA,
	/** immutable container global version */
	DAOS_PROP_CO_GLOBAL_VERSION,
	/** Override the pool scrubbing property. */
	DAOS_PROP_CO_SCRUBBER_DISABLED,
	/** immutable container object global version */
	DAOS_PROP_CO_OBJ_VERSION,
	/** The container performance domain, now always inherit from pool */
	DAOS_PROP_CO_PERF_DOMAIN,
	DAOS_PROP_CO_MAX,
};

/** first citizen objects of a container, stored as container property */
struct daos_prop_co_roots {
	/** array that stores root, SB OIDs */
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
	/* server rank (engine) level */
	DAOS_PROP_CO_REDUN_RANK	= 1,
	/* server node level */
	DAOS_PROP_CO_REDUN_NODE	= 2,
	DAOS_PROP_CO_REDUN_MAX	= 254,
};

/** default fault domain level */
#define DAOS_PROP_CO_REDUN_DEFAULT	DAOS_PROP_CO_REDUN_NODE

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

/** daos container status */
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

enum {
	DAOS_PROP_ENTRY_NOT_SET = (1 << 0),
};

/** daos property entry */
struct daos_prop_entry {
	/** property type, see enum daos_pool_props/daos_cont_props */
	uint32_t		 dpe_type;
	/** property flags, eg negative entry*/
	uint16_t		 dpe_flags;
	/** reserved for future usage (for 64 bits alignment now) */
	uint16_t		 dpe_reserv;
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
/** DAOS_PROP_LABEL_MAX_LEN including NULL terminator */
#define DAOS_PROP_MAX_LABEL_BUF_LEN	(DAOS_PROP_LABEL_MAX_LEN + 1)

/** default values for unset labels */
#define DAOS_PROP_CO_LABEL_DEFAULT "container_label_not_set"
#define DAOS_PROP_PO_LABEL_DEFAULT "pool_label_not_set"

/**
 * Check if DAOS (pool or container property) label string is valid.
 * DAOS labels must consist only of alphanumeric characters, colon ':',
 * period '.', hyphen '-' or underscore '_', and must be of length
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
	bool	maybe_uuid = false;

	/** Label cannot be NULL */
	if (label == NULL)
		return false;

	/** Check the length */
	len = strnlen(label, DAOS_PROP_LABEL_MAX_LEN + 1);
	if (len == 0 || len > DAOS_PROP_LABEL_MAX_LEN)
		return false;

	/** Verify that it contains only alphanumeric characters or :.-_ */
	for (i = 0; i < len; i++) {
		char c = label[i];

		if (isalnum(c) || c == '.' || c == '_' || c == ':')
			continue;
		if (c == '-') {
			maybe_uuid = true;
			continue;
		}

		return false;
	}

	/** Check to see if it could be a valid UUID */
	if (maybe_uuid && strnlen(label, 36) == 36) {
		bool		is_uuid = true;
		const char	*p;

		/** Implement the check directly to avoid uuid_parse() overhead */
		for (i = 0, p = label; i < 36; i++, p++) {
			if (i == 8 || i == 13 || i == 18 || i == 23) {
				if (*p != '-') {
					is_uuid = false;
					break;
				}
				continue;
			}
			if (!isxdigit(*p)) {
				is_uuid = false;
				break;
			}
		}

		if (is_uuid)
			return false;
	}

	return true;
}

/** max length of the policy string */
#define DAOS_PROP_POLICYSTR_MAX_LEN	(127)

/* default policy string */
#define DAOS_PROP_POLICYSTR_DEFAULT	"type=io_size"

/**
 * Check if DAOS pool performance domain string is valid, string
 * has same requirement as label.
 */
static inline bool
daos_perf_domain_is_valid(const char *perf_domain)
{
	return daos_label_is_valid(perf_domain);
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
 * Allocate a new property from a string buffer of property entries and values. That buffer has to
 * be of the format:
 * prop_entry_name1:value1;prop_entry_name2:value2;prop_entry_name3:value3;
 * \a prop must be freed with daos_prop_free() to release allocated space.
 * This supports properties that can be modified on container creation only:
 *   label, cksum, cksum_size, srv_cksum, dedup, dedup_threshold, compression,
 *   encryption, rf, ec_cell_sz
 *
 * \param[in]	str	Serialized string of property entries and their values
 * \param[in]	len	Serialized string length
 * \param[out]	prop	Property that is created
 */
int
daos_prop_from_str(const char *str, daos_size_t len, daos_prop_t **prop);

/**
 * Merge a set of new DAOS properties into a set of existing DAOS properties.
 *
 * \param[in]	old_prop	Existing set of properties
 * \param[in]	new_prop	New properties - may override old entries
 * \param[out]	out_prop	New properties - may override old entries
 *
 * \return		0		Success
 *			-DER_NOMEM	Out of memory
 */
int
daos_prop_merge2(daos_prop_t *old_prop, daos_prop_t *new_prop, daos_prop_t **out_prop);

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
 * Search and return a property entry of type \a type in the property list
 * \a prop
 * Return NULL if not found.
 *
 * \param[in]		prop		Property list
 * \param[in]		type		Type of property to look for
 */
struct daos_prop_entry *
daos_prop_entry_get(daos_prop_t *prop, uint32_t type);

/**
 * Set the string value of a property entry in a property. The property type must expect that it's
 * entry is of a string type. This duplicates the string internally and the entry string is freed
 * with daos_free_prop(). The user does not need to keep the string buffer around after this
 * function is called. If the entry already has a string value set, it frees that and overwrites it
 * with this new string.
 *
 * \param[in]		prop		Property list
 * \param[in]		type		Type of property to look for
 * \param[in]		str		String value to set in the prop entry
 * \param[in]           len		Length of \a str
 */
int
daos_prop_set_str(daos_prop_t *prop, uint32_t type, const char *str, daos_size_t len);

/**
 * Set the entry string value with the provided \a str.
 * Convenience Function.
 *
 * \param[in,out]	entry		Entry where to duplicate the str into.
 * \param[in]		str		String value to set in the prop entry
 * \param[in]           len		Length of \a str
 */
int
daos_prop_entry_set_str(struct daos_prop_entry *entry, const char *str, daos_size_t len);

/**
 * Set the pointer value of a property entry in a property. The property type must expect that it's
 * entry is of a pointer type. This duplicates the buffer internally and the entry buffer is freed
 * with daos_free_prop(). The user does not need to keep the string buffer around after this
 * function is called. If the entry already has a value set, it frees that and overwrites it with
 * this new value.
 *
 * \param[in]		prop		Property list
 * \param[in]		type		Type of property to look for
 * \param[in]		ptr		Pointer to value of entry to set
 * \param[in]           size		Size of value
 */
int
daos_prop_set_ptr(daos_prop_t *prop, uint32_t type, const void *ptr, daos_size_t size);

/**
 * Set the entry pointer value with the provided \a ptr.
 * Convenience Function.
 *
 * \param[in,out]	entry		Entry where to copy the value.
 * \param[in]		ptr             Pointer to value of entry to set
 * \param[in]           size            Size of value
 */
int
daos_prop_entry_set_ptr(struct daos_prop_entry *entry, const void *ptr, daos_size_t size);

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

/**
 * Check if a DAOS prop entry is set or not.
 *
 * \param[in]		entry		Entry to be checked.
 *
 * \return		true		Entry is set
 *			false		Entry is not set.
 */
static inline bool
daos_prop_is_set(struct daos_prop_entry *entry)
{
	if (entry->dpe_flags & DAOS_PROP_ENTRY_NOT_SET)
		return false;

	return true;
}

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_PROP_H__ */
