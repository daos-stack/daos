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

/** Maximum length (excluding the '\0') of a DAOS system name */
#define DAOS_SYS_NAME_MAX 15

/**
 * Generic data type definition
 */

typedef uint64_t	daos_size_t;
typedef uint64_t	daos_off_t;

/**
 * daos_sg_list_t/daos_iov_t/daos_iov_set is for keeping compatibility for
 * upper layer.
 */
#define daos_sg_list_t			d_sg_list_t
#define daos_iov_t			d_iov_t
#define daos_iov_set(iov, buf, size)	d_iov_set((iov), (buf), (size))
#define daos_rank_list_free(r)		d_rank_list_free((r))

#define crt_proc_daos_key_t	crt_proc_d_iov_t
#define crt_proc_daos_size_t	crt_proc_uint64_t
#define crt_proc_daos_epoch_t	crt_proc_uint64_t

/** size of SHA-256 */
#define DAOS_HKEY_MAX	32

/** buffer to store an array of checksums */
typedef struct {
	/** buffer to store the checksums */
	uint8_t		*cs_csum;
	/** number of checksums stored in buffer */
	uint32_t	 cs_nr;
	/** type of checksum */
	uint16_t	 cs_type;
	/** length of each checksum in bytes*/
	uint16_t	 cs_len;
	/** length of entire buffer (cs_csum). buf_len can be larger than
	 *  nr * len, but never smaller
	 */
	uint32_t	 cs_buf_len;
	/** bytes of data each checksum verifies (if value type is array) */
	uint32_t	 cs_chunksize;
} daos_csum_buf_t;

static inline void daos_csum_set_multiple(daos_csum_buf_t *csum_buf, void *buf,
					  uint32_t csum_buf_size,
					  uint16_t csum_size,
					  uint32_t csum_count,
					  uint32_t chunksize)
{
	csum_buf->cs_csum = buf;
	csum_buf->cs_len = csum_size;
	csum_buf->cs_buf_len = csum_buf_size;
	csum_buf->cs_nr = csum_count;
	csum_buf->cs_chunksize = chunksize;
}

static inline bool
daos_csum_isvalid(const daos_csum_buf_t *csum)
{
	return csum != NULL &&
	       csum->cs_len > 0 &&
	       csum->cs_buf_len > 0 &&
	       csum->cs_csum != NULL &&
	       csum->cs_chunksize > 0 &&
	       csum->cs_nr > 0;
}

static inline void
daos_csum_set(daos_csum_buf_t *csum, void *buf, uint16_t size)
{
	daos_csum_set_multiple(csum, buf, size, size, 1, 0);
}

static inline uint32_t
daos_csum_idx_from_off(daos_csum_buf_t *csum, uint32_t offset_bytes)
{
	return offset_bytes / csum->cs_chunksize;
}

static inline uint8_t *
daos_csum_from_idx(daos_csum_buf_t *csum, uint32_t idx)
{
	return csum->cs_csum + csum->cs_len * idx;
}

static inline uint8_t *
daos_csum_from_offset(daos_csum_buf_t *csum, uint32_t offset_bytes)
{
	return daos_csum_from_idx(csum,
				  daos_csum_idx_from_off(csum, offset_bytes));
}

enum daos_anchor_flags {
	/* The RPC will be sent to leader replica. */
	DAOS_ANCHOR_FLAGS_TO_LEADER	= 1,
};

typedef enum {
	DAOS_ANCHOR_TYPE_ZERO	= 0,
	DAOS_ANCHOR_TYPE_HKEY	= 1,
	DAOS_ANCHOR_TYPE_KEY	= 2,
	DAOS_ANCHOR_TYPE_EOF	= 3,
} daos_anchor_type_t;

/** Iteration Anchor */
#define DAOS_ANCHOR_BUF_MAX	120
typedef struct {
	uint16_t	da_type; /** daos_anchor_type_t */
	uint16_t	da_shard;
	uint32_t	da_flags; /** see enum daos_anchor_flags */
	uint8_t		da_buf[DAOS_ANCHOR_BUF_MAX];
} daos_anchor_t;

static inline void
daos_anchor_set_flags(daos_anchor_t *anchor, uint32_t flags)
{
	anchor->da_flags |= flags;
}

static inline uint32_t
daos_anchor_get_flags(daos_anchor_t *anchor)
{
	return anchor->da_flags;
}

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
#define DAOS_TX_NONE	DAOS_HDL_INVAL

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

enum {
	DAOS_MEDIA_SCM	= 0,
	DAOS_MEDIA_NVME,
	DAOS_MEDIA_MAX
};

/** Pool target space usage information */
struct daos_space {
	/* Total space in bytes */
	uint64_t	s_total[DAOS_MEDIA_MAX];
	/* Free space in bytes */
	uint64_t	s_free[DAOS_MEDIA_MAX];
};

/** Target information */
typedef struct {
	daos_target_type_t	ta_type;
	daos_target_state_t	ta_state;
	daos_target_perf_t	ta_perf;
	struct daos_space	ta_space;
} daos_target_info_t;

/** Pool space usage information */
struct daos_pool_space {
	/* Aggregated space for all live targets */
	struct daos_space	ps_space;
	/* Min target free space in bytes */
	uint64_t	ps_free_min[DAOS_MEDIA_MAX];
	/* Max target free space in bytes */
	uint64_t	ps_free_max[DAOS_MEDIA_MAX];
	/* Average target free space in bytes */
	uint64_t	ps_free_mean[DAOS_MEDIA_MAX];
	/* Target(VOS) count */
	uint32_t	ps_ntargets;
	uint32_t	ps_padding;
};

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
 * Pool info query bits.
 * The basic pool info like fields from pi_uuid to pi_leader will always be
 * queried for each daos_pool_query() calling. But the pi_space and
 * pi_rebuild_st are optional based on pi_mask's value.
 */
enum daos_pool_info_bit {
	/** true to query pool space usage */
	DPI_SPACE		= 1ULL << 0,
	/** true to query rebuild status */
	DPI_REBUILD_STATUS	= 1ULL << 1,
	/** query all above optional info */
	DPI_ALL			= -1,
};

/**
 * Storage pool
 */
typedef struct {
	/** Pool UUID */
	uuid_t				pi_uuid;
	/** Number of targets */
	uint32_t			pi_ntargets;
	/** Number of nodes */
	uint32_t			pi_nnodes;
	/** Number of deactivated targets */
	uint32_t			pi_ndisabled;
	/** Latest pool map version */
	uint32_t			pi_map_ver;
	/** current raft leader */
	uint32_t			pi_leader;
	/** pool info bits, see daos_pool_info_bit */
	uint64_t			pi_bits;
	/** Space usage */
	struct daos_pool_space		pi_space;
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

static inline daos_epoch_t
daos_ts2epoch(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_sec * 1e9 + ts.tv_nsec;
}

typedef struct {
	/** Low bound of the epoch range */
	daos_epoch_t	epr_lo;
	/** High bound of the epoch range */
	daos_epoch_t	epr_hi;
} daos_epoch_range_t;

/** Highest possible epoch */
#define DAOS_EPOCH_MAX	(~0ULL)

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
	/** Epoch of latest persisten snapshot */
	daos_epoch_t		ci_lsnapshot;
	/** Number of snapshots */
	uint32_t		ci_nsnapshots;
	/** Epochs of returns snapshots */
	daos_epoch_t	       *ci_snapshots;
	/* TODO: add more members, e.g., size, # objects, uid, gid... */
} daos_cont_info_t;

/**
 * Object
 */

/**
 * ID of an object, 128 bits
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

/** Mask for daos_obj_key_query() flags to indicate what is being queried */
enum {
	/** retrieve the max of dkey, akey, and/or idx of array value */
	DAOS_GET_MAX	= (1 << 0),
	/** retrieve the min of dkey, akey, and/or idx of array value */
	DAOS_GET_MIN	= (1 << 1),
	/** retrieve the dkey */
	DAOS_GET_DKEY	= (1 << 2),
	/** retrieve the akey */
	DAOS_GET_AKEY	= (1 << 3),
	/** retrieve the idx of array value */
	DAOS_GET_RECX	= (1 << 4),
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
	DAOS_OC_ECHO_TINY_RW,	/* Echo class, tiny */
	DAOS_OC_ECHO_R2S_RW,	/* Echo class, 2 replica single stripe */
	DAOS_OC_ECHO_R3S_RW,	/* Echo class, 3 replica single stripe */
	DAOS_OC_ECHO_R4S_RW,	/* Echo class, 4 replica single stripe */
	DAOS_OC_R1S_SPEC_RANK,	/* 1 replica with specified rank */
	DAOS_OC_R2S_SPEC_RANK,	/* 2 replica start with specified rank */
	DAOS_OC_R3S_SPEC_RANK,	/* 3 replica start with specified rank.
				 * These 3 XX_SPEC are mostly for testing
				 * purpose.
				 */
	DAOS_OC_EC_K2P1_L32K,	/* Erasure code, 2 data cells, 1 parity cell,
				 * cell size 32KB.
				 */
	DAOS_OC_EC_K2P2_L32K,	/* Erasure code, 2 data cells, 2 parity cells,
				 * cell size 32KB.
				 */
	DAOS_OC_EC_K8P2_L1M,	/* Erasure code, 8 data cells, 2 parity cells,
				 * cell size 1MB.
				 */
};

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
			/** number of data cells (k) */
			unsigned short	 e_k;
			/** number of parity cells (p) */
			unsigned short	 e_p;
			/** length of each block of data (cell) */
			unsigned int	 e_len;
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
typedef d_iov_t daos_key_t;

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
	 * records where the record is updated atomically. Note that an akey can
	 * only support one type of value which is set on the first update. If
	 * user attempts to mix types in the same akey, the behavior is
	 * undefined, even after the object, key, or value is punched. If
	 * \a iod_type == DAOS_IOD_SINGLE, then iod_nr has to be 1, and
	 * \a iod_size would be the size of the single atomic value. The idx is
	 * ignored and the rx_nr is also required to be 1.
	 */
	daos_iod_type_t		iod_type;
	/** Size of the single value or the record size of the array */
	daos_size_t		iod_size;
	/*
	 * Number of entries in the \a iod_recxs, \a iod_csums, and \a iod_eprs
	 * arrays, should be 1 if single value.
	 */
	unsigned int		iod_nr;
	/*
	 * Array of extents, where each extent defines the index of the first
	 * record in the extent and the number of records to access. If the
	 * type of the iod is single, this is ignored.
	 */
	daos_recx_t		*iod_recxs;
	/*
	 * Checksum associated with each extent. If the type of the iod is
	 * single, will only have a single checksum.
	 */
	daos_csum_buf_t		*iod_csums;
	/** Epoch range associated with each extent */
	daos_epoch_range_t	*iod_eprs;
} daos_iod_t;

/** Get a specific checksum given an index */
static inline daos_csum_buf_t *
daos_iod_csum(daos_iod_t *iod, int csum_index)
{
	return iod->iod_csums ? &iod->iod_csums[csum_index] : NULL;
}

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
daos_obj_is_null_id(daos_obj_id_t oid)
{
	return oid.lo == 0 && oid.hi == 0;
}

static inline int
daos_obj_compare_id(daos_obj_id_t a, daos_obj_id_t b)
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

/* rank/target list for target */
struct d_tgt_list {
	d_rank_t	*tl_ranks;
	int32_t		*tl_tgts;
	/** number of ranks & tgts */
	uint32_t	tl_nr;
};

struct daos_eq;
/**
 * DAOS pool property types
 * valid in range (DAOS_PROP_PO_MIN, DAOS_PROP_PO_MAX)
 */
enum daos_pool_prop_type {
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
enum daos_cont_prop_type {
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
	 * Redundancy factor:
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
	DAOS_PROP_CO_CSUM_SHA1,
	DAOS_PROP_CO_CSUM_SHA2,
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
	DAOS_PROP_CO_REDUN_RF1,
	DAOS_PROP_CO_REDUN_RF3,
};

enum {
	DAOS_PROP_CO_REDUN_RACK,
	DAOS_PROP_CO_REDUN_NODE,
};

struct daos_prop_entry {
	/** property type, see enum daos_pool_prop_type/daos_cont_prop_type */
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
 * DAOS Hash Table Handle Types
 * The handle type, uses the least significant 4-bits in the 64-bits hhash key.
 * The bit 0 is only used for D_HYTPE_PTR (pointer type), all other types MUST
 * set bit 0 to 1.
 */
enum {
	DAOS_HTYPE_EQ		= 1, /**< event queue */
	DAOS_HTYPE_POOL		= 3, /**< pool */
	DAOS_HTYPE_CO		= 5, /**< container */
	DAOS_HTYPE_OBJ		= 7, /**< object */
	DAOS_HTYPE_ARRAY	= 9, /**< array */
	DAOS_HTYPE_TX		= 11, /**< transaction */
	/* Must enlarge D_HTYPE_BITS to add more types */
};

#if defined(__cplusplus)
}
#endif

#endif /* DAOS_TYPES_H */
