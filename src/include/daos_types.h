/**
 * (C) Copyright 2015, 2016 Intel Corporation.
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

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/** uuid_t */
#include <uuid/uuid.h>
/** for crt_rank_t & crt_rank_list_t */
#include <crt_types.h>

#include <daos_errno.h>

/**
 * Generic data type definition
 */

typedef uint64_t	daos_size_t;
typedef uint64_t	daos_off_t;

/** iovec for memory buffer */
typedef struct {
	/** buffer address */
	void		*iov_buf;
	/** buffer length */
	daos_size_t	 iov_buf_len;
	/** data length */
	daos_size_t	 iov_len;
} daos_iov_t;

static inline void
daos_iov_set(daos_iov_t *iov, void *buf, daos_size_t size)
{
	iov->iov_buf = buf;
	iov->iov_len = iov->iov_buf_len = size;
}

typedef struct {
	/** input number */
	uint32_t	num;
	/** output/returned number */
	uint32_t	num_out;
} daos_nr_t;

/** Scatter/gather list for memory buffers */
typedef struct {
	daos_nr_t	 sg_nr;
	daos_iov_t	*sg_iovs;
} daos_sg_list_t;

/** For short-term compatibility, should eventually be removed */
#define daos_rank_t		crt_rank_t
#define daos_rank_list_t	crt_rank_list_t

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

/** Generic hash format */
typedef struct {
	char		body[DAOS_HKEY_MAX];
} daos_hash_out_t;

#define DAOS_HASH_HKEY_START 0
#define DAOS_HASH_HKEY_LENGTH 16

static inline void
daos_hash_set_eof(daos_hash_out_t *hash_out)
{
	memset(&hash_out->body[DAOS_HASH_HKEY_START], -1,
	       DAOS_HASH_HKEY_LENGTH);
}

static inline bool
daos_hash_is_eof(daos_hash_out_t *hash_out)
{
	int i;

	for (i = DAOS_HASH_HKEY_START; i < DAOS_HASH_HKEY_LENGTH; i++) {
		if (hash_out->body[i] != -1)
			return false;
	}

	return true;
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

/**
 * Storage pool
 */
typedef struct {
	/** Pool UUID */
	uuid_t			pi_uuid;
	/** Number of targets */
	uint32_t		pi_ntargets;
	/** Number of deactivated targets */
	uint32_t		pi_ndisabled;
	/** Mode */
	unsigned int		pi_mode;
	/** Space usage */
	daos_space_t		pi_space;
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
	uint64_t	mid;
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
	daos_nr_t	 ol_nr;
	/** OID buffer */
	daos_obj_id_t	*ol_oids;
} daos_oid_list_t;

typedef uint16_t daos_oclass_id_t;

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

#define DAOS_OC_GRP_MAX		(-1)

/**
 * List of default object class
 */
enum {
	DAOS_OC_UNKNOWN,
	DAOS_OC_TINY_RW,
	DAOS_OC_SMALL_RW,
	DAOS_OC_LARGE_RW,
	DAOS_OC_REPLICA_RW,
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
	daos_rank_t		 oa_rank;
	/** Optional, class attributes of object with private class */
	daos_oclass_attr_t	*oa_oa;
} daos_obj_attr_t;

/**
 * Record
 *
 * A record is an atomic blob of arbitrary length which is always
 * fetched/updated as a whole. The size of a record can change over time.
 * A vector is a dynamic array of records.
 * A record is uniquely identified by the following composite key:
 * - the distribution key (aka dkey) denotes a set of vectors co-located on the
 *   same storage targets. The dkey has an arbitrary size.
 * - the attribute key (aka akey) distinguishes individual vectors. Likewise, the
 *   akey has a arbitrary size.
 * - the indice within a vector discriminates individual records. The indice
 *   is an integer that ranges from zero to infinity. A range of indices
 *   identifies a contiguous set of records called extent. All records inside an
 *   extent must have the same size.
 */

/** opaque key type */
typedef daos_iov_t daos_key_t;

/* XXX remove daos_dkey_t and daos_akey_t */
/** Distribution key */
typedef daos_key_t daos_dkey_t;
/** Attribute key */
typedef daos_key_t daos_akey_t;

/**
 * A record extent is a range of contiguous records of the same size inside an
 * array. \a rx_idx is the first array indice of the extent and \a rx_nr is the
 * number of records covered by the extent.
 */
typedef struct {
	/** Individual record size, must be the same for each record of the
	 * extent */
	uint64_t	rx_rsize;
	/* Indice of the first record in the range */
	uint64_t	rx_idx;
	/* Number of records in the range
	 * If rx_nr is equal to 1, the range identifies a single record of
	 * indice rx_idx */
	uint64_t	rx_nr;
} daos_recx_t;

/**
 * A vector I/O descriptor is a list of extents to update/fetch in a particular
 * vector identified by its akey.
 */
typedef struct {
	/** Name associated with the vector, effectively akey */
	daos_akey_t		 vd_name;
	/** Name/akey checksum */
	daos_csum_buf_t		 vd_kcsum;
	/** Number of extents in the \a vd_recxs array */
	unsigned int		 vd_nr;
	/** Array of extents */
	daos_recx_t		*vd_recxs;
	/** Checksum associated with each extent */
	daos_csum_buf_t		*vd_csums;
	/** Epoch range associated with each extent */
	daos_epoch_range_t	*vd_eprs;
} daos_vec_iod_t;

/**
 * A vector map represents the physical extent mapping inside a vector for a
 * given range of indices.
 */
typedef struct {
	/** Name associated with the vector, effectively akey */
	daos_akey_t		 vm_name;
	/** Name/akey checksum */
	daos_csum_buf_t		 vm_kcsum;
	/** First indice of this mapping */
	uint64_t		 vm_start;
	/** Logical number of indices covered by this mapping */
	uint64_t                 vm_len;
	/** Number of extents in the mapping, that's the size of all the
	 * arrays listed below */
	unsigned int		 vm_nr;
	/** Array of extents */
	daos_recx_t		*vm_recxs;
	/** Checksum associated with each extent */
	daos_csum_buf_t		*vm_xcsums;
	/** Epoch range associated with each extent */
	daos_epoch_range_t	*vm_eprs;
} daos_vec_map_t;

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
	daos_size_t	 kd_key_len;
	/** Checksum type */
	unsigned int	 kd_csum_type;
	/** Checksum length */
	unsigned short	 kd_csum_len;
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

/**
 * Event and event queue
 */

typedef struct daos_event {
	int			ev_error;
	/** Internal use, please do not modify */
	struct {
		uint64_t	space[70];
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
	DAOS_EQR_DISPATCH	= (1 << 1),
	/** Query # inflight + completed events in EQ */
	DAOS_EQR_ALL		= (DAOS_EQR_COMPLETED | DAOS_EQR_DISPATCH),
} daos_eq_query_t;

typedef enum {
	DAOS_EVS_INIT,
	DAOS_EVS_DISPATCH,
	DAOS_EVS_COMPLETED,
	DAOS_EVS_ABORT,
} daos_ev_status_t;

struct daos_eq;
#endif /* DAOS_TYPES_H */
