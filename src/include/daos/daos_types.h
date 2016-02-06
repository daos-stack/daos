/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2015, 2016 Intel Corporation.
 */
/**
 * DAOS Types and Functions Common to Layers/Components
 */

#ifndef DAOS_TYPES_H
#define DAOS_TYPES_H

#include <stdint.h>

/** uuid_t */
#include <uuid/uuid.h>

/** Size */
typedef uint64_t	daos_size_t;

/** Offset */
typedef uint64_t	daos_off_t;

/** size of SHA-256 */
#define DAOS_HKEY_MAX	32

/** Generic hash format */
typedef struct {
	char		body[DAOS_HKEY_MAX];
} daos_hash_out_t;

/** Generic handle for various DAOS components like container, object, etc. */
typedef struct {
	uint64_t	cookie;
} daos_handle_t;

typedef uint32_t	daos_rank_t;

/**
 * Server Identification & Addressing
 */
typedef struct {
	/** XXX use dtp group descriptor at here */
	int		dg_grp;
} daos_group_t;

/**
 * One way to understand this: An array of "session network addresses", each of
 * which consists of a "UUID" part shares with all others, identifying the
 * session, and a "rank" part, uniquely identifies a process within this
 * session.
 */
typedef struct {
	/** list length, it is actual buffer size */
	uint32_t	 rl_llen;
	/** number of ranks in the list */
	uint32_t	 rl_rankn;
	daos_rank_t	*rl_ranks;
} daos_rank_list_t;

/**
 * Storage Targets
 */

/** Type of storage target */
typedef enum {
	DAOS_TP_UNKNOWN,
	/** rotating disk */
	DAOS_TP_HDD,
	/** flash-based */
	DAOS_TP_SSD,
	/** persistent memory */
	DAOS_TP_PM,
	/** volatile memory */
	DAOS_TP_VM,
} daos_target_type_t;

/** Current state of the storage target */
typedef enum {
	DAOS_TS_UNKNOWN,
	/* not available */
	DAOS_TS_DOWN_OUT,
	/* not available, may need rebuild */
	DAOS_TS_DOWN,
	/* up */
	DAOS_TS_UP,
	/* up and running */
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
	/** pool UUID */
	uuid_t			pi_uuid;
	/** number of containers */
	uint32_t		pi_nconns;
	/** number of targets */
	uint32_t		pi_ntargets;
	/** number of deactivated targets */
	uint32_t		pi_ndisabled;
	/** number of containers */
	uint32_t		pi_ncontainers;
	/** space usage */
	daos_space_t		pi_space;
} daos_pool_info_t;

/**
 * Epoch
 */

typedef uint64_t	daos_epoch_t;

typedef struct {
	/** low bound of the epoch range */
	daos_epoch_t	epr_lo;
	/** high bound of the epoch range */
	daos_epoch_t	epr_hi;
} daos_epoch_range_t;

/** highest possible epoch */
#define DAOS_EPOCH_MAX	(~0ULL)

/** Epoch State */
typedef struct {
	/** Highest Committed Epoch (HCE) of the container. */
	daos_epoch_t	es_hce;

	/** Highest Committed Epoch (HCE) of the container handle. */
	daos_epoch_t	es_h_hce;

	/** Lowest Referenced Epoch (LRE) of the container handle.
	 * Each container handle references all epochs equal to or higher than
	 * its LRE and thus guarantees these epochs to be readable. The LRE of a
	 * new container handle is equal to the HCE.
	 * See also the epoch slip operation. */
	daos_epoch_t	es_h_lre;

	/** Lowest Held Epoch (LHE) of the container handle.
	 * Each container handle with write permission holds all epochs equal to
	 * or higher than its LHE and thus guarantees these epochs to be
	 * mutable.  The LHE of a new container handle with write permission is
	 * equal to DAOS_EPOCH_MAX, indicating that the container handle does
	 * not hold any epochs. See also the epoch hold functionality. */
	daos_epoch_t	es_h_lhe;
} daos_epoch_state_t;

/**
 * DAOS Containers
 */

/** Container information */
typedef struct {
	/** container UUID */
	uuid_t			ci_uuid;
	/** epoch information (e.g. HCE, LRE & LHE) */
	daos_epoch_state_t	ci_epoch_state;
	/** number of snapshots */
	uint32_t		ci_nsnapshots;
	/** epochs of returns snapshots */
	daos_epoch_t	       *ci_snapshots;
	/* TODO: add more members, e.g., size, # objects, uid, gid... */
} daos_co_info_t;

/**
 * DAOS Objects
 */

/** ID of an object */
typedef struct {
	uint64_t	body[2];
} daos_obj_id_t;

typedef struct {
	/** list length, it is the actual buffer size */
	unsigned int	 ol_llen;
	/** number of OIDs */
	unsigned int	 ol_oidn;
	/** OID buffer */
	daos_obj_id_t	*ol_oids;
} daos_oid_list_t;

/**
 * Byte Array Objects
 */

/** iovec for memory buffer */
typedef struct {
	/** buffer address */
	void	       *iov_buf;
	/** buffer length */
	daos_size_t	iov_buf_len;
	/** data length */
	daos_size_t	iov_len;
} daos_iov_t;

/** buffer to store checksum */
typedef struct {
	/* TODO: typedef enum for it */
	unsigned int	 cs_type;
	unsigned int	 cs_len;
	void		*cs_csum;
} daos_csum_buf_t;

/** Scatter/gather list for memory buffers */
typedef struct {
	/** list length, it is actual buffer size */
	unsigned int	 sg_llen;
	/** number of iovs */
	unsigned int	 sg_iovn;
	daos_iov_t	*sg_iovs;
	/** checksums, it is optional */
	daos_csum_buf_t	*el_csums;
} daos_sg_list_t;

typedef enum {
	/* hole extent */
	VOS_EXT_HOLE	= (1 << 0),
} vos_ext_flag_t;
/**
 * Extent for byte-array object.
 * NB: this is a wire struct.
 */
typedef struct {
	/** offset within object */
	daos_off_t	e_offset;
	/** number of bytes */
	uint64_t	e_nob;
	/** see vos_ext_flag_t */
	uint16_t	e_flags;
	/** reserved */
	uint16_t	e_reserv_16;
	uint32_t	e_reserv_32;
} daos_ext_t;

/** a list of object extents */
typedef struct {
	/** list length, it is actual buffer size */
	unsigned int		 el_llen;
	/** number of extents */
	unsigned int		 el_extn;
	daos_ext_t		*el_exts;
	/** Optional, epoch validity range for the I/O */
	daos_epoch_range_t	*el_epr;
} daos_ext_list_t;

typedef daos_ext_list_t		daos_ext_layout_t;

/** 2-dimensional key of KV */
typedef struct {
	/** distribution key */
	daos_iov_t		dk_dkey;
	/** attribute key */
	daos_iov_t		dk_akey;
} daos_key_t;

typedef struct {
	daos_csum_buf_t		dk_dk_cs;
	daos_csum_buf_t		dk_ak_cs;
} daos_key_csum_t;

/**
 * Key-Value Store Objects
 */

/** Descriptor of a key-value pair */
typedef struct {
	/** list length, it is actual buffer size */
	unsigned int		 kv_llen;
	/** nubmer of kvs and epoch ranges */
	unsigned int		 kv_kvn;
	/** key array */
	daos_key_t		*kv_keys;
	/** value array */
	daos_iov_t		*kv_vals;
	/** checksums for \a kv_keys */
	daos_key_csum_t		*kv_key_csums;
	/** checksums for \a kv_vals */
	daos_csum_buf_t		*kv_val_csums;
	/** Optional, array of epoch ranges for the \a kv_keys */
	daos_epoch_range_t	*kv_eprs;
} daos_kv_list_t;

#endif /* DAOS_TYPES_H */
