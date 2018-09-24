/**
 * (C) Copyright 2015-2018 Intel Corporation.
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
 * DAOS Types and Functions Common to Layers/Components
 */

#ifndef DAOS_TYPES_H
#define DAOS_TYPES_H

#if defined(__cplusplus)
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/** uuid_t */
#include <uuid/uuid.h>
/** for d_rank_t & d_rank_list_t */
#include <cart/types.h>

#include <daos_errno.h>

/** Scatter/gather list for memory buffers */
#define daos_sg_list_t d_sg_list_t

/** free function for d_rank_list_t */
#define daos_rank_list_free d_rank_list_free

/**
 * Generic data type definition
 */

typedef uint64_t	daos_size_t;
typedef uint64_t	daos_off_t;

#define daos_iov_t	d_iov_t
static inline void
daos_iov_set(daos_iov_t *iov, void *buf, daos_size_t size)
{
	iov->iov_buf = buf;
	iov->iov_len = iov->iov_buf_len = size;
}

/** size of SHA-256 */
#define DAOS_HKEY_MAX	32

/** buffer to store checksum */
typedef struct {
	/* TODO: typedef enum for it */
	unsigned int	 cs_type;
	unsigned short	 cs_len;
	unsigned short	 cs_buf_len;
	void		*cs_csum;
} daos_csum_buf_t;

static inline void
daos_csum_set(daos_csum_buf_t *csum, void *buf, uint16_t size)
{
	csum->cs_csum = buf;
	csum->cs_len = csum->cs_buf_len = size;
}


typedef enum {
	DAOS_ANCHOR_TYPE_ZERO	= 0,
	DAOS_ANCHOR_TYPE_HKEY	= 1,
	DAOS_ANCHOR_TYPE_KEY	= 2,
	DAOS_ANCHOR_TYPE_EOF	= 3,
} daos_anchor_type_t;

/** Iteration Anchor */
typedef struct {
	uint64_t	da_type; /** daos_anchor_type_t */
	char da_hkey[DAOS_HKEY_MAX];
} daos_anchor_t;

#define DAOS_HASH_HKEY_START	0
#define DAOS_HASH_HKEY_LENGTH	24


static inline void
daos_anchor_set_zero(daos_anchor_t *anchor)
{
	anchor->da_type = DAOS_ANCHOR_TYPE_ZERO;
}

static inline void
daos_anchor_set_eof(daos_anchor_t *anchor)
{
	anchor->da_type = DAOS_ANCHOR_TYPE_EOF;
}

static inline bool
daos_anchor_is_zero(daos_anchor_t *anchor)
{
	return anchor->da_type == DAOS_ANCHOR_TYPE_ZERO;
}

static inline bool
daos_anchor_is_eof(daos_anchor_t *anchor)
{
	return anchor->da_type == DAOS_ANCHOR_TYPE_EOF;
}

/** Generic handle for various DAOS components like container, object, etc. */
typedef struct {
	uint64_t	cookie;
} daos_handle_t;

#define DAOS_HDL_INVAL	((daos_handle_t){0})

static inline bool
daos_handle_is_inval(daos_handle_t hdl)
{
	return hdl.cookie == 0;
}

/**
 * Storage Targets
 */

/** Type of storage target */
typedef enum {
	DAOS_TP_UNKNOWN,
	/** Rotating disk */
	DAOS_TP_HDD,
	/** Flash-based */
	DAOS_TP_SSD,
	/** Persistent memory */
	DAOS_TP_PM,
	/** Volatile memory */
	DAOS_TP_VM,
} daos_target_type_t;

/** Current state of the storage target */
typedef enum {
	DAOS_TS_UNKNOWN,
	/* Not available */
	DAOS_TS_DOWN_OUT,
	/* Not available, may need rebuild */
	DAOS_TS_DOWN,
	/* Up */
	DAOS_TS_UP,
	/* Up and running */
	DAOS_TS_UP_IN,
} daos_target_state_t;

/** Description of target performance */
typedef struct {
	/** TODO: storage/network bandwidth, latency etc */
	int			foo;
} daos_target_perf_t;

typedef struct {
	/** TODO: space usage */
	int			foo;
} daos_space_t;

/** Target information */
typedef struct {
	daos_target_type_t	ta_type;
	daos_target_state_t	ta_state;
	daos_target_perf_t	ta_perf;
	daos_space_t		ta_space;
} daos_target_info_t;

struct daos_rebuild_status {
	/** pool map version in rebuilding or last completed rebuild */
	uint32_t		rs_version;
	/** padding bytes */
	uint32_t		rs_pad_32;
	/** errno for rebuild failure */
	int32_t			rs_errno;
	/**
	 * rebuild is done or not, it is valid only if @rs_version is non-zero
	 */
	int32_t			rs_done;
	/** # total to-be-rebuilt objects, it's non-zero and increase when
	 * rebuilding in progress, when rs_done is 1 it will not change anymore
	 * and should equal to rs_obj_nr. With both rs_toberb_obj_nr and
	 * rs_obj_nr the user can know the progress of the rebuilding.
	 */
	uint64_t		rs_toberb_obj_nr;
	/** # rebuilt objects, it's non-zero only if rs_done is 1 */
	uint64_t		rs_obj_nr;
	/** # rebuilt records, it's non-zero only if rs_done is 1 */
	uint64_t		rs_rec_nr;
};

/**
 * Storage pool
 */
typedef struct {
	/** Pool UUID */
	uuid_t				pi_uuid;
	/** Number of targets */
	uint32_t			pi_ntargets;
	/** Number of deactivated targets */
	uint32_t			pi_ndisabled;
	/** Latest pool map version */
	uint32_t			pi_map_ver;
	/** pool UID */
	uid_t				pi_uid;
	/** pool GID */
	gid_t				pi_gid;
	/** Mode */
	uint32_t			pi_mode;
	/** current raft leader */
	uint32_t			pi_leader;
	/** Space usage */
	daos_space_t			pi_space;
	/** rebuild status */
	struct daos_rebuild_status	pi_rebuild_st;
} daos_pool_info_t;

/**
 * DAOS_PC_RO connects to the pool for reading only.
 *
 * DAOS_PC_RW connects to the pool for reading and writing.
 *
 * DAOS_PC_EX connects to the pool for reading and writing exclusively. In the
 * presence of an exclusive pool handle, no connection with DSM_PC_RW is
 * permitted.
 *
 * The three flags above are mutually exclusive.
 */
#define DAOS_PC_RO	(1U << 0)
#define DAOS_PC_RW	(1U << 1)
#define DAOS_PC_EX	(1U << 2)

#define DAOS_PC_NBITS	3
#define DAOS_PC_MASK	((1U << DAOS_PC_NBITS) - 1)

/**
 * Epoch
 */

typedef uint64_t	daos_epoch_t;

typedef struct {
	/** Low bound of the epoch range */
	daos_epoch_t	epr_lo;
	/** High bound of the epoch range */
	daos_epoch_t	epr_hi;
} daos_epoch_range_t;

/** Highest possible epoch */
#define DAOS_EPOCH_MAX	(~0ULL)
#define DAOS_PURGE_CREDITS_MAX 1000

/** Epoch State */
typedef struct {
	/**
	 * Epoch state specific to the container handle.
	 */

	/**
	 * Highest Committed Epoch (HCE).
	 * Any changes submitted by this container handle with an epoch <= HCE
	 * are guaranteed to be durable. On the other hand, any updates
	 * submitted with an epoch > HCE are automatically rolled back on
	 * failure of the container handle.
	 * The HCE is increased on successful commit.
	 */
	daos_epoch_t	es_hce;

	/**
	 * Lowest Referenced Epoch (LRE).
	 * Each container handle references all epochs equal to or higher than
	 * its LRE and thus guarantees these epochs to be readable.
	 * The LRE is moved forward with the slip operation.
	 */
	daos_epoch_t	es_lre;

	/**
	 * Lowest Held Epoch (LHE).
	 * Each container handle with write permission holds all epochs equal to
	 * or higher than its LHE and thus guarantees these epochs to be
	 * mutable.  The LHE of a new container handle with write permission is
	 * equal to DAOS_EPOCH_MAX, indicating that the container handle does
	 * not hold any epochs.
	 * The LHE can be modified with the epoch hold operation and is
	 * increased on successful commit.
	 */
	daos_epoch_t	es_lhe;

	/**
	 * Global epoch state for the container.
	 */

	/** Global Highest Committed Epoch (gHCE). */
	daos_epoch_t	es_ghce;

	/** Global Lowest Referenced Epoch (gLRE). */
	daos_epoch_t	es_glre;

	/** Global Highest Partially Committed Epoch (gHPCE) */
	daos_epoch_t	es_ghpce;
} daos_epoch_state_t;

/**
 * Container
 */

/**
 * DAOS_COO_RO opens the container for reading only. This flag conflicts with
 * DAOS_COO_RW.
 *
 * DAOS_COO_RW opens the container for reading and writing. This flag conflicts
 * with DAOS_COO_RO.
 *
 * DAOS_COO_NOSLIP disables the automatic epoch slip at epoch commit time. See
 * daos_epoch_commit().
 */
#define DAOS_COO_RO	(1U << 0)
#define DAOS_COO_RW	(1U << 1)
#define DAOS_COO_NOSLIP	(1U << 2)

/** Container information */
typedef struct {
	/** Container UUID */
	uuid_t			ci_uuid;
	/** Epoch information (e.g. HCE, LRE & LHE) */
	daos_epoch_state_t	ci_epoch_state;
	/** Number of snapshots */
	uint32_t		ci_nsnapshots;
	/** Epochs of returns snapshots */
	daos_epoch_t	       *ci_snapshots;
	/*
	 * Min GLRE at all streams -
	 * verify all streams have completed slipping to GLRE.
	 */
	daos_epoch_t		ci_min_slipped_epoch;
	/* TODO: add more members, e.g., size, # objects, uid, gid... */
} daos_cont_info_t;

/**
 * Object
 */

/**
 * ID of an object, 192 bits
 * The high 32-bit of daos_obj_id_t::hi are reserved for DAOS, the rest is
 * provided by the user and assumed to be unique inside a container.
 */
typedef struct {
	uint64_t	lo;
	uint64_t	hi;
} daos_obj_id_t;

enum {
	/** Shared read */
	DAOS_OO_RO             = (1 << 1),
	/** Shared read & write, no cache for write */
	DAOS_OO_RW             = (1 << 2),
	/** Exclusive write, data can be cached */
	DAOS_OO_EXCL           = (1 << 3),
	/** Random I/O */
	DAOS_OO_IO_RAND        = (1 << 4),
	/** Sequential I/O */
	DAOS_OO_IO_SEQ         = (1 << 5),
};

typedef struct {
	/** Input/output number of oids */
	uint32_t	ol_nr;
	uint32_t	ol_nr_out;
	/** OID buffer */
	daos_obj_id_t	*ol_oids;
} daos_oid_list_t;

enum {
	/** DKEY's are hashed and sorted in hashed order */
	DAOS_OF_DKEY_HASHED	= 0,
	/** AKEY's are hashed and sorted in hashed order */
	DAOS_OF_AKEY_HASHED	= 0,
	/** DKEY keys not hashed and sorted numerically.   Keys are accepted
	 *  in client's byte order and DAOS is responsible for correct behavior
	 */
	DAOS_OF_DKEY_UINT64	= (1 << 0),
	/** DKEY keys not hashed and sorted lexically */
	DAOS_OF_DKEY_LEXICAL	= (1 << 1),
	/** AKEY keys not hashed and sorted numerically.   Keys are accepted
	 *  in client's byte order and DAOS is responsible for correct behavior
	 */
	DAOS_OF_AKEY_UINT64	= (1 << 2),
	/** AKEY keys not hashed and sorted lexically */
	DAOS_OF_AKEY_LEXICAL	= (1 << 3),
	/** Mask for convenience */
	DAOS_OF_MASK		= ((1 << 4) - 1),
};

/** Number of reserved by daos in object id for version */
#define DAOS_OVERSION_BITS	8
/** Number of reserved by daos in object id for features */
#define DAOS_OFEAT_BITS		8
/** Number of reserved by daos in object id for class id */
#define DAOS_OCLASS_BITS	(32 - DAOS_OVERSION_BITS - DAOS_OFEAT_BITS)
/** Bit shift for object version in object id */
#define DAOS_OVERSION_SHIFT	(64 - DAOS_OVERSION_BITS)
/** Bit shift for object features in object id */
#define DAOS_OFEAT_SHIFT	(DAOS_OVERSION_SHIFT - DAOS_OFEAT_BITS)
/** Bit shift for object class id in object id */
#define DAOS_OCLASS_SHIFT	(DAOS_OFEAT_SHIFT - DAOS_OCLASS_BITS)
/** Maximum valid object version setting */
#define DAOS_OVERSION_MAX	((1ULL << DAOS_OVERSION_BITS) - 1)
/** Maximum valid object feature setting */
#define DAOS_OFEAT_MAX		((1ULL << DAOS_OFEAT_BITS) - 1)
/** Maximum valid object class setting */
#define DAOS_OCLASS_MAX		((1ULL << DAOS_OCLASS_BITS) - 1)
/** Mask for object version */
#define DAOS_OVERSION_MASK	(DAOS_OVERSION_MAX << DAOS_OVERSION_SHIFT)
/** Mask for object features */
#define DAOS_OFEAT_MASK		(DAOS_OFEAT_MAX << DAOS_OFEAT_SHIFT)
/** Mask for object class id */
#define DAOS_OCLASS_MASK	(DAOS_OCLASS_MAX << DAOS_OCLASS_SHIFT)

typedef uint16_t daos_oclass_id_t;
typedef uint8_t daos_ofeat_t;

enum {
	/** Use private class for the object */
	DAOS_OCLASS_NONE		= 0,
};

typedef enum {
	DAOS_OS_SINGLE,		/**< Single stripe object */
	DAOS_OS_STRIPED,	/**< Fix striped object */
	DAOS_OS_DYN_STRIPED,	/**< Dynamically striped object */
	DAOS_OS_DYN_CHUNKED,	/**< Dynamically chunked object */
} daos_obj_schema_t;

typedef enum {
	DAOS_RES_EC,		/**< Erasure code */
	DAOS_RES_REPL,		/**< Replication */
} daos_obj_resil_t;

#define DAOS_OBJ_GRP_MAX	(~0)
#define DAOS_OBJ_REPL_MAX	(~0)

/**
 * List of default object class
 * R = replicated (number after R is number of replicas
 * S = small (1 stripe)
 */
enum {
	DAOS_OC_UNKNOWN,
	DAOS_OC_TINY_RW,
	DAOS_OC_SMALL_RW,
	DAOS_OC_LARGE_RW,
	DAOS_OC_R2S_RW,
	DAOS_OC_R2_RW,
	DAOS_OC_R3S_RW,		/* temporary class for testing */
	DAOS_OC_R3_RW,		/* temporary class for testing */
	DAOS_OC_R4S_RW,		/* temporary class for testing */
	DAOS_OC_R4_RW,		/* temporary class for testing */
	DAOS_OC_REPL_MAX_RW,
	DAOS_OC_ECHO_RW,	/* Echo class */
	DAOS_OC_R1S_SPEC_RANK,	/* 1 replica with specified rank */
	DAOS_OC_R2S_SPEC_RANK,	/* 2 replica start with specified rank */
	DAOS_OC_R3S_SPEC_RANK,	/* 3 replica start with specified rank.
				 * These 3 XX_SPEC are mostly for testing
				 * purpose.
				 */
};

/** bits for the specified rank */
#define DAOS_OC_SR_SHIFT	24
#define DAOS_OC_SR_BITS		8
#define DAOS_OC_SR_MASK		\
	(((1ULL << DAOS_OC_SR_BITS) - 1) << DAOS_OC_SR_SHIFT)

/** Object class attributes */
typedef struct daos_oclass_attr {
	/** Object placement schema */
	daos_obj_schema_t		 ca_schema;
	/**
	 * TODO: define HA degrees for object placement
	 * - performance oriented
	 * - high availability oriented
	 * ......
	 */
	unsigned int			 ca_resil_degree;
	/** Resilience method, replication or erasure code */
	daos_obj_resil_t		 ca_resil;
	/** Initial # redundancy group, unnecessary for some schemas */
	unsigned int			 ca_grp_nr;
	union {
		/** Replication attributes */
		struct daos_repl_attr {
			/** Method of replicating */
			unsigned int	 r_method;
			/** Number of replicas */
			unsigned int	 r_num;
			/** TODO: add members to describe */
		} repl;

		/** Erasure coding attributes */
		struct daos_ec_attr {
			/** Type of EC */
			unsigned int	 e_type;
			/** EC group size */
			unsigned int	 e_grp_size;
			/**
			 * TODO: add members to describe erasure coding
			 * attributes
			 */
		} ec;
	} u;
	/** TODO: add more attributes */
} daos_oclass_attr_t;

/** List of object classes, used for class enumeration */
typedef struct {
	/** List length, actual buffer size */
	uint32_t		 cl_llen;
	/** Number of object classes in the list */
	uint32_t		 cl_cn;
	/** Actual list of class IDs */
	daos_oclass_id_t	*cl_cids;
	/** Attributes of each listed class, optional */
	daos_oclass_attr_t	*cl_cattrs;
} daos_oclass_list_t;

/**
 * Object attributes (metadata).
 * \a oa_class and \a oa_oa are mutually exclusive.
 */
typedef struct {
	/** Optional, affinity target for the object */
	d_rank_t		 oa_rank;
	/** Optional, class attributes of object with private class */
	daos_oclass_attr_t	*oa_oa;
} daos_obj_attr_t;

/** key type */
typedef daos_iov_t daos_key_t;

/**
 * Record
 *
 * A record is an atomic blob of arbitrary length which is always
 * fetched/updated as a whole. The size of a record can change over time.
 * A record is uniquely identified by the following composite key:
 * - the distribution key (aka dkey) denotes a set of arrays co-located on the
 *   same storage targets. The dkey has an arbitrary size.
 * - the attribute key (aka akey) distinguishes individual arrays. Likewise, the
 *   akey has a arbitrary size.
 * - the index within an array discriminates individual records. The index
 *   is an integer that ranges from zero to infinity. A range of indices
 *   identifies a contiguous set of records called extent. All records inside an
 *   extent must have the same size.
 */

/**
 * A record extent is a range of contiguous records of the same size inside an
 * array. \a rx_idx is the first array index of the extent and \a rx_nr is the
 * number of records covered by the extent.
 */
typedef struct {
	/** Indice of the first record in the extent */
	uint64_t	rx_idx;
	/**
	 * Number of contiguous records in the extent
	 * If \a rx_nr is equal to 1, the extent is composed of a single record
	 * at indice \a rx_idx
	 */
	uint64_t	rx_nr;
} daos_recx_t;

/** Type of the value accessed in an IOD */
typedef enum {
	/** is a dkey */
	DAOS_IOD_NONE		= 0,
	/** one indivisble value udpate atomically */
	DAOS_IOD_SINGLE		= (1 << 0),
	/** an array of records where each record is update atomically */
	DAOS_IOD_ARRAY		= (1 << 1),
} daos_iod_type_t;

/**
 * An I/O descriptor is a list of extents (effectively records associated with
 * contiguous array indices) to update/fetch in a particular array identified by
 * its akey.
 */
typedef struct {
	/** akey for this iod */
	daos_key_t		iod_name;
	/** akey checksum */
	daos_csum_buf_t		iod_kcsum;
	/*
	 * Type of the value in an iod can be either a single type that is
	 * always overwritten when updated, or it can be an array of EQUAL sized
	 * records where the record is updated atomically. Note than an akey can
	 * have both type of values, but to access both would require a separate
	 * iod for each. If \a iod_type == DAOS_IOD_SINGLE, then iod_nr has to
	 * be 1, and \a iod_size would be the size of the single atomic
	 * value. The idx is ignored and the rx_nr is also required to be 1.
	 */
	daos_iod_type_t		iod_type;
	/* DAOS_IOD_SINGLE:	Size of the value
	 * DAOS_IOD_ARRAY:	iod_nr > 0:  Record size
	 *			iod_nr == 0: Array size
	 */
	daos_size_t		iod_size;
	/*
	 * Number of entries in the \a iod_recxs, \a iod_csums, and \a iod_eprs
	 * arrays, should be 1 if single value,
	 * 0 for array size query
	 */
	unsigned int		iod_nr;
	/*
	 * Array of extents, where each extent defines the index of the first
	 * record in the extent and the number of records to access. If the
	 * type of the iod is single, this is ignored.
	 */
	daos_recx_t		*iod_recxs;
	/** Checksum associated with each extent */
	daos_csum_buf_t		*iod_csums;
	/** Epoch range associated with each extent */
	daos_epoch_range_t	*iod_eprs;
} daos_iod_t;

/**
 * A I/O map represents the physical extent mapping inside an array for a
 * given range of indices.
 */
typedef struct {
	/** akey associated with the array */
	daos_key_t		 iom_name;
	/** akey checksum */
	daos_csum_buf_t		 iom_kcsum;
	/** type of akey value (SV or AR)*/
	daos_iod_type_t		 iom_type;
	/** First index of this mapping (0 for SV) */
	uint64_t		 iom_start;
	/** Logical number of indices covered by this mapping (1 for SV) */
	uint64_t                 iom_len;
	/** Size of the single value or the record size */
	daos_size_t		 iom_size;
	/**
	 * Number of extents in the mapping, that's the size of all the
	 * external arrays listed below. 1 for SV.
	 */
	unsigned int		 iom_nr;
	/** External array of extents - NULL for SV */
	daos_recx_t		*iom_recxs;
	/** Checksum associated with each extent */
	daos_csum_buf_t		*iom_xcsums;
	/** Epoch range associated with each extent */
	daos_epoch_range_t	*iom_eprs;
} daos_iom_t;

/** record status */
enum {
	/** Reserved for cache miss */
	DAOS_REC_MISSING	= -1,
	/** Any record size, it is used by fetch */
	DAOS_REC_ANY		= 0,
};

typedef enum {
	/* Hole extent */
	VOS_EXT_HOLE	= (1 << 0),
} vos_ext_flag_t;

/**
 * Key descriptor used for key enumeration. The actual key and checksum are
 * stored in a separate buffer (i.e. sgl)
 */
typedef struct {
	/** Key length */
	daos_size_t	kd_key_len;
	/**
	 * flag for akey value types: DAOS_IOD_SINGLE, DAOS_IOD_ARRAY, or
	 * both. Ignored for dkey enumeration.
	 */
	uint32_t	kd_val_types;
	/** Checksum type */
	unsigned int	kd_csum_type;
	/** Checksum length */
	unsigned short	kd_csum_len;
} daos_key_desc_t;

/**
 * 256-bit object ID, it can identify a unique bottom level object.
 * (a shard of upper level object).
 */
typedef struct {
	/** Public section, high level object ID */
	daos_obj_id_t		id_pub;
	/** Private section, object shard index */
	uint32_t		id_shard;
	/** Padding */
	uint32_t		id_pad_32;
} daos_unit_oid_t;

static inline bool
daos_obj_id_is_null(daos_obj_id_t oid)
{
	return oid.lo == 0 && oid.hi == 0;
}

static inline int
daos_obj_id_compare(daos_obj_id_t a, daos_obj_id_t b)
{
	if (a.hi < b.hi)
		return -1;
	else if (a.hi > b.hi)
		return 1;

	if (a.lo < b.lo)
		return -1;
	else if (a.lo > b.lo)
		return 1;

	return 0;
}

static inline bool
daos_unit_oid_is_null(daos_unit_oid_t oid)
{
	return oid.id_shard == 0 && daos_obj_id_is_null(oid.id_pub);
}

static inline int
daos_unit_oid_compare(daos_unit_oid_t a, daos_unit_oid_t b)
{
	int rc;

	rc = daos_obj_id_compare(a.id_pub, b.id_pub);
	if (rc != 0)
		return rc;

	if (a.id_shard < b.id_shard)
		return -1;
	else if (a.id_shard > b.id_shard)
		return 1;

	return 0;
}

/**
 * Event and event queue
 */

typedef struct daos_event {
	int			ev_error;
	/** Internal use, please do not modify */
	struct {
		uint64_t	space[19];
	}			ev_private;
	/** Used for debugging */
	uint64_t		ev_debug;
} daos_event_t;

/** Wait for completion event forever */
#define DAOS_EQ_WAIT            -1
/** Always return immediately */
#define DAOS_EQ_NOWAIT          0

typedef enum {
	/** Query outstanding completed event */
	DAOS_EQR_COMPLETED	= (1),
	/** Query # inflight event */
	DAOS_EQR_WAITING	= (1 << 1),
	/** Query # inflight + completed events in EQ */
	DAOS_EQR_ALL		= (DAOS_EQR_COMPLETED | DAOS_EQR_WAITING),
} daos_eq_query_t;

typedef enum {
	DAOS_EVS_READY,
	DAOS_EVS_RUNNING,
	DAOS_EVS_COMPLETED,
	DAOS_EVS_ABORTED,
} daos_ev_status_t;

struct daos_eq;

#if defined(__cplusplus)
}
#endif

#endif /* DAOS_TYPES_H */
