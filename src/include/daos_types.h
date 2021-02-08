/**
 * (C) Copyright 2015-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

/** Maximum length (excluding the '\0') of info string info via GetAttachInfo */
#define DAOS_SYS_INFO_STRING_MAX 63

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

typedef enum {
	DAOS_ANCHOR_TYPE_ZERO	= 0,
	DAOS_ANCHOR_TYPE_HKEY	= 1,
	DAOS_ANCHOR_TYPE_KEY	= 2,
	DAOS_ANCHOR_TYPE_EOF	= 3,
} daos_anchor_type_t;

/** Iteration Anchor */
#define DAOS_ANCHOR_BUF_MAX	104
typedef struct {
	uint16_t	da_type; /** daos_anchor_type_t */
	uint16_t	da_shard;
	uint32_t	da_flags; /** see enum daos_anchor_flags */
	/* record the offset for each shards for EC object */
	uint64_t	da_sub_anchors;
	uint8_t		da_buf[DAOS_ANCHOR_BUF_MAX];
} daos_anchor_t;

#define DAOS_ANCHOR_INIT	((daos_anchor_t){DAOS_ANCHOR_TYPE_ZERO})

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

static inline bool
daos_handle_is_valid(daos_handle_t hdl)
{
	return !daos_handle_is_inval(hdl);
}

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
	/** array of ranks */
	d_rank_t	*tl_ranks;
	/** array of targets */
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
	DAOS_HTYPE_KV		= 13, /**< KV */
	/* Must enlarge D_HTYPE_BITS to add more types */
};

/**
 * ID of an object, 128 bits
 * The high 32-bit of daos_obj_id_t::hi are reserved for DAOS, the rest is
 * provided by the user and assumed to be unique inside a container.
 *
 * See daos_obj.h for more details
 * It is put here because it's almost used by everyone.
 */
typedef struct {
	uint64_t	lo;
	uint64_t	hi;
} daos_obj_id_t;

#if defined(__cplusplus)
}
#endif

#endif /* DAOS_TYPES_H */
