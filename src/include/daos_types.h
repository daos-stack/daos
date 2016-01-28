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
 * (C) Copyright 2015 Intel Corporation.
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

/** Generaic hash format */
typedef struct {
	uint64_t	body[2];
} daos_hash_out_t;

/** Generic handle for various DAOS components like container, object, etc. */
typedef struct {
	uint64_t	cookie;
} daos_handle_t;

/**
 * Server Identification & Addressing
 */

/** Address of a process in a session */
typedef uint32_t	daos_rank_t;

/**
 * One way to understand this: An array of "session network addresses", each of
 * which consists of a "UUID" part shares with all others, identifying the
 * session, and a "rank" part, uniquely identifies a process within this
 * session.
 */
typedef struct {
	uuid_t		rg_uuid;
	uint32_t	rg_nranks;
	daos_rank_t    *rg_ranks;
} daos_rank_group_t;

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
	/* up and running */
	DAOS_TS_UP,
	/* not available */
	DAOS_TS_DOWN,
	/** TODO: add more states? */
} daos_target_state_t;

/** Description of target performance */
typedef struct {
	/** TODO: storage/network bandwidth, latency etc */
} daos_target_perf_t;

/** Target information */
typedef struct {
	daos_target_type_t	ta_type;
	daos_target_state_t	ta_state;
	daos_target_perf_t	ta_perf;
} daos_target_info_t;

/**
 * Epoch
 */

typedef uint64_t	daos_epoch_t;

/** highest possible epoch */
#define DAOS_EPOCH_MAX	(~0ULL)

/** Epoch State */
typedef struct {
	/** Highest Committed Epoch (HCE) of the container. */
	daos_epoch_t	es_hce;

	/** Lowest Referenced Epoch (LRE) of the container handle.
	 * Each container handle references all epochs equal to or higher than
	 * its LRE and thus guarantees these epochs to be readable. The LRE of a
	 * new container handle is equal to the HCE.
	 * See also the epoch slip operation. */
	daos_epoch_t	es_lre;

	/** Lowest Held Epoch (LHE) of the container handle.
	 * Each container handle with write permission holds all epochs equal to
	 * or higher than its LHE and thus guarantees these epochs to be
	 * mutable.  The LHE of a new container handle with write permission is
	 * equal to DAOS_EPOCH_MAX, indicating that the container handle does
	 * not hold any epochs. See also the epoch hold functionality. */
	daos_epoch_t	es_lhe;
} daos_epoch_state_t;

/**
 * DAOS Containers
 */

/** Container information */
typedef struct {
	/** container UUID */
	uuid_t			ci_uuid;
	/** number of shards */
	uint32_t		ci_nshards;
	/** number of deactivated shards */
	uint32_t		ci_ndisabled;
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

/**
 * Byte Array Objects
 */

/** iovec for memory buffer */
typedef struct {
	daos_size_t	iov_len;
	void	       *iov_addr;
} daos_sg_iov_t;

/** Scatter/gather list for memory buffers */
typedef struct {
	unsigned long	 sg_num;
	daos_sg_iov_t	*sg_iovs;
} daos_sg_list_t;

/** extent for bype-array object */
typedef struct {
	daos_off_t	e_offset;
	daos_size_t	e_nob;
} daos_ext_t;

/** a list of object extents */
typedef struct {
	unsigned long	 el_num;
	daos_ext_t	*el_exts;
} daos_ext_list_t;

/**
 * Key-Value Store Objects
 */

/** Descriptor of a key-value pair */
typedef struct {
	void		*kv_key;
	void		*kv_val;
	unsigned int	 kv_delete:1;
	unsigned int	 kv_key_len:30;
	unsigned int	 kv_val_len;
} daos_kv_t;

#endif /* DAOS_TYPES_H */
