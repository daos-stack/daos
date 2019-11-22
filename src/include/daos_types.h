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
	anchor->da_flags = flags;
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
	/** Time (Seconds) for the rebuild */
	uint32_t		rs_seconds;
	/** errno for rebuild failure */
	int32_t			rs_errno;
	/**
	 * rebuild is done or not, it is valid only if @rs_version is non-zero
	 */
	int32_t			rs_done;

	/* padding of rebuild status */
	int32_t			rs_padding32;

	/* Failure on which rank */
	int32_t			rs_fail_rank;
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

	/** rebuild space cost */
	uint64_t		rs_size;
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

/*
 * DAOS management pool information
 */
typedef struct {
	/* TODO? same pool info structure as a pool query?
	 * requires back-end RPC to each pool service.
	 * daos_pool_info_t		 mgpi_info;
	 */
	uuid_t				 mgpi_uuid;
	/** List of current pool service replica ranks */
	d_rank_list_t			*mgpi_svc;
} daos_mgmt_pool_info_t;

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
	/** The minimal "Highest aggregated epoch" among all targets */
	daos_epoch_t		ci_hae;
	/* TODO: add more members, e.g., size, # objects, uid, gid... */
} daos_cont_info_t;

typedef d_iov_t daos_key_t;

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

/**
 * Pool target list, each target is identified by rank & target
 * index within the rank
 */
struct d_tgt_list {
	d_rank_t	*tl_ranks;
	int32_t		*tl_tgts;
	/** number of ranks & tgts */
	uint32_t	tl_nr;
};

struct daos_eq;

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
