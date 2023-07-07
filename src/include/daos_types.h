/**
 * (C) Copyright 2015-2023 Intel Corporation.
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

#define DAOS_ANCHOR_BUF_MAX	104
/** Iteration Anchor */
typedef struct {
	uint16_t	da_type;	/**< daos_anchor_type_t */
	uint16_t	da_shard;	/**< internal, do not use */
	uint32_t	da_flags;	/**< see enum daos_anchor_flags */
	uint64_t	da_sub_anchors;	/**< record the offset for each shards for EC object */
	uint8_t		da_buf[DAOS_ANCHOR_BUF_MAX];	/**< internal, do not use */
} daos_anchor_t;

#define DAOS_ANCHOR_INIT	{ .da_type = DAOS_ANCHOR_TYPE_ZERO,	\
				  .da_shard = 0,			\
				  .da_flags = 0,			\
				  .da_sub_anchors = 0,			\
				  .da_buf = { 0 } }

/** Generic handle for various DAOS components like container, object, etc. */
typedef struct {
	/** generic handle value */
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
 * DAOS_PC_EX connects to the pool for reading and writing exclusively.
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

/**
 * Epoch range
 */
typedef struct {
	/** Low bound of the epoch range */
	daos_epoch_t	epr_lo;
	/** High bound of the epoch range */
	daos_epoch_t	epr_hi;
} daos_epoch_range_t;

/** Highest possible epoch */
#define DAOS_EPOCH_MAX	(~0ULL)

/** Container information */
typedef struct {
	/** Container UUID */
	uuid_t			ci_uuid;
	/** Epoch of latest persistent snapshot */
	daos_epoch_t		ci_lsnapshot;
	/** Number of open handles */
	uint32_t		ci_nhandles;
	/** Number of snapshots */
	uint32_t		ci_nsnapshots;
	/** Latest open time (hybrid logical clock) */
	uint64_t		ci_md_otime;
	/** Latest close/modify time (hybrid logical clock) */
	uint64_t		ci_md_mtime;
	/* TODO: add more members, e.g., size, # objects, uid, gid... */
} daos_cont_info_t;

typedef d_iov_t daos_key_t;

/**
 * Event and event queue
 */

typedef struct daos_event {
	/** return code of non-blocking operation */
	int			ev_error;
	/** Internal use - 152 + 8 bytes pad for pthread_mutex_t size difference on __aarch64__ */
	struct {
		uint64_t	space[20];
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
	/** Query # in-flight event */
	DAOS_EQR_WAITING	= (1 << 1),
	/** Query # in-flight + completed events in EQ */
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
	/** least significant (low) bits of object ID */
	uint64_t	lo;
	/** most significant (high) bits of object ID */
	uint64_t	hi;
} daos_obj_id_t;

/**
 * Corresponding rank and URI for a DAOS engine
 */
struct daos_rank_uri {
	/** DAOS engine rank */
	uint32_t	 dru_rank;
	/** URI associated with rank */
	char		*dru_uri;
};

/**
 * DAOS general system information for clients
 */
struct daos_sys_info {
	/** name of DAOS system */
	char			 dsi_system_name[DAOS_SYS_INFO_STRING_MAX + 1];
	/** fabric provider in use by this system */
	char			 dsi_fabric_provider[DAOS_SYS_INFO_STRING_MAX + 1];
	/** length of ranks array */
	uint32_t		 dsi_nr_ranks;
	/** ranks and their client-accessible URIs */
	struct daos_rank_uri	*dsi_ranks;
};

/** max pool/cont attr size */
#define DAOS_ATTR_NAME_MAX 511

#if defined(__cplusplus)
}
#endif

#endif /* DAOS_TYPES_H */
