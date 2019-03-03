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

/** Scatter/gather list for memory buffers */
#define daos_sg_list_t d_sg_list_t

/** free function for d_rank_list_t */
#define daos_rank_list_free d_rank_list_free

/**
 * Generic data type definition
 */

typedef uint64_t	daos_size_t;
typedef uint64_t	daos_off_t;

#define daos_iov_t		d_iov_t
#define crt_proc_daos_key_t	crt_proc_d_iov_t
#define crt_proc_daos_size_t	crt_proc_uint64_t
#define crt_proc_daos_epoch_t	crt_proc_uint64_t

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
#define DAOS_ANCHOR_BUF_MAX	120
typedef struct {
	uint16_t	da_type; /** daos_anchor_type_t */
	uint16_t	da_shard;
	uint32_t	da_padding;
	uint8_t		da_buf[DAOS_ANCHOR_BUF_MAX];
} daos_anchor_t;

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
 * Object
 */

typedef enum {
	/* Hole extent */
	VOS_EXT_HOLE	= (1 << 0),
} vos_ext_flag_t;

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

/** bits of ae_tag entry in struct daos_acl_entry */
enum {
	/** access rights for the pool/container/obj's owner */
	DAOS_ACL_USER_OBJ	= (1U << 0),
	/** access rights for user identified by the entry's ae_id */
	DAOS_ACL_USER		= (1U << 1),
	/** access rights for the pool/container/obj's group */
	DAOS_ACL_GROUP_OBJ	= (1U << 2),
	/** access rights for group identified by the entry's ae_id */
	DAOS_ACL_GROUP		= (1U << 3),
	/**
	 * the maximum access rights that can be granted by entries of ae_tag
	 * DAOS_ACL_USER, DAOS_ACL_GROUP_OBJ, or DAOS_ACL_GROUP.
	 */
	DAOS_ACL_MASK		= (1U << 4),
	/**
	 * access rights for processes that do not match any other entry
	 * in the ACL.
	 */
	DAOS_ACL_OTHER		= (1U << 5),
};

struct daos_acl_entry {
	/** DAOS_ACL_USER/GROUP/MASK/OTHER */
	uint16_t		ae_tag;
	/** permissions DAOS_PC_RO/RW */
	uint16_t		ae_perm;
	/** uid or gid, meaningful only when ae_tag is DAOS_ACL_USER/GROUP */
	uint32_t		ae_id;
};

struct daos_acl {
	/** number of acl entries */
	uint32_t		 da_nr;
	/** reserved for future usage (for 64 bits alignment now) */
	uint32_t		 da_reserv;
	/** acl entries array */
	struct daos_acl_entry	*da_entries;
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
 * \param[int]	prop	properties to be freed.
 */
void
daos_prop_free(daos_prop_t *prop);

#if defined(__cplusplus)
}
#endif

#endif /* DAOS_TYPES_H */
