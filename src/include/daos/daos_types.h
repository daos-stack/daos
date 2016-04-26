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
#include <stdbool.h>

/** uuid_t */
#include <uuid/uuid.h>

/**
 * Generic data type definition
 */

/** Size */
typedef uint64_t	daos_size_t;

/** Offset */
typedef uint64_t	daos_off_t;

/** size of SHA-256 */
#define DAOS_HKEY_MAX	32

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
	unsigned short	 cs_len;
	unsigned short	 cs_buf_len;
	void		*cs_csum;
} daos_csum_buf_t;

/** Generic hash format */
typedef struct {
	char		body[DAOS_HKEY_MAX];
} daos_hash_out_t;

/** Generic handle for various DAOS components like container, object, etc. */
typedef struct {
	uint64_t	cookie;
} daos_handle_t;

typedef uint32_t	daos_rank_t;

typedef struct {
	/** input number */
	uint32_t	num;
	/** output/returned number */
	uint32_t	num_out;
} daos_nr_t;

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
	/** number of ranks */
	daos_nr_t	 rl_nr;
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
	uint32_t		pi_ncnts;
	/** number of targets */
	uint32_t		pi_ntargets;
	/** number of deactivated targets */
	uint32_t		pi_ndisabled;
	/** mode */
	unsigned int		pi_mode;
	/** space usage */
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
#define DAOS_PC_RO	0x0U
#define DAOS_PC_RW	0x1U
#define DAOS_PC_EX	0x2U

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
 * Container
 */

/**
 * DAOS_COO_RO opens the container for reading only. This flag conflicts with
 * DAOS_COO_RW.
 *
 * DAOS_COO_RW opens the container for reading and writing. This flag conflicts
 * with DAOS_COO_RO.
 */
#define DAOS_COO_RO	0x0U
#define DAOS_COO_RW	0x1U

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
 * Object
 */

/** ID of an object, 192 bits */
typedef struct {
	uint64_t	body[3];
} daos_obj_id_t;

typedef struct {
	/** input/output number of oids */
	daos_nr_t	 ol_nr;
	/** OID buffer */
	daos_obj_id_t	*ol_oids;
} daos_oid_list_t;

typedef uint16_t daos_oclass_id_t;

enum {
	/** use private class for the object */
	DAOS_OCLASS_NONE		= 0,
};

typedef enum {
	DAOS_OS_SINGLE,		/**< single stripe object */
	DAOS_OS_STRIPED,		/**< fix striped object */
	DAOS_OS_DYN_STRIPED,	/**< dynamically striped object */
	DAOS_OS_DYN_CHUNKED,	/**< dynamically chunked object */
} daos_obj_schema_t;

typedef enum {
	DAOS_RES_EC,		/**< erasure code */
	DAOS_RES_REPL,		/**< replication */
} daos_obj_resil_t;

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
	/** Initial # stripe count, unnecessary for some schemas */
	unsigned int			 ca_nstripes;
	union {
		/** replication attributes */
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
	/** list length, actual buffer size */
	uint32_t		 cl_llen;
	/** number of object classes in the list */
	uint32_t		 cl_cn;
	/** actual list of class IDs */
	daos_oclass_id_t	*cl_cids;
	/** attributes of each listed class, optional */
	daos_oclass_attr_t	*cl_cattrs;
} daos_oclass_list_t;

/**
 * Record
 *
 * A record is an atomic blob of arbitrary length which is always
 * fetched/updated as a whole. The size of a record can change over time.
 * A vector is a dynamic array of records.
 * A record is uniquely identified by the following composite key:
 * - the distribution key (aka dkey) denotes a set of vectors co-located on the
 *   same storage targets. The dkey has an abitrary size.
 * - the attribute key (aka akey) distinguishes individual vectors. Likewise, the
 *   akey has a arbitrary size.
 * - the indice within a vector discrimites individual records. The indice
 *   is an integer that ranges from zero to infinity. A range of indices
 *   identifies a contiguous set of records called extent. All records inside an
 *   extent must have the same size.
 */

/** distribution key */
typedef daos_iov_t daos_dkey_t;

/** attribute key */
typedef daos_iov_t daos_akey_t;

/**
 * A record extent is a range of contiguous records of the same size inside an
 * array. \a rx_idx is the first array indice of the extent and \a rx_nr is the
 * number of records covered by the extent.
 */
typedef struct {
	/** indidivual record size, must be the same for each record of the
	 * extent */
	uint64_t	rx_rsize;
	/* indice of the first record in the range */
	uint64_t	rx_idx;
	/* number of records in the range
	 * If rx_nr is equal to 1, the range identifies a single record of
	 * indice rx_idx */
	uint64_t	rx_nr;
} daos_recx_t;

/**
 * A vector I/O desriptor is a list of extents to update/fetch in a particular
 * vector identified by its akey.
 */
typedef struct {
	/** name associated with the vector, effectively a akey */
	daos_akey_t		 vd_name;
	/** key checksum covering the parent dkey plus akey specified above */
	daos_csum_buf_t		 vd_kcsum;
	/** number of extents in the \a vd_recxs array */
	unsigned int		 vd_nr;
	/** array of extents */
	daos_recx_t		*vd_recxs;
	/** checksum associated with each extent */
	daos_csum_buf_t		*vd_csums;
	/** epoch range associated with each extent */
	daos_epoch_range_t	*vd_eprs;
} daos_vec_iod_t;

/**
 * A vector map represents the physical extent mapping inside a vectorfor a
 * given range of indices.
 */
typedef struct {
	/** name associated with the vector, effectively a akey */
	daos_akey_t		 vm_name;
	/** key checksum covering the parent dkey plus akey specified above */
	daos_csum_buf_t		 vm_kcsum;
	/** first indice of this mapping */
	uint64_t		 vm_start;
	/** logical number of indices covered by this mapping */
	uint64_t                 vm_len;
	/** number of extents in the mapping, that's the size of all the
	 * arrays listed below */
	unsigned int		 vm_nr;
	/** array of extents */
	daos_recx_t		*vm_recxs;
	/** checksum associated with each extent */
	daos_csum_buf_t		*vm_csums;
	/** epoch range associated with each extent */
	daos_epoch_range_t	*vm_eprs;
} daos_vec_map_t;

/** record status */
enum {
	/** no data */
	DAOS_REC_NODATA		= 0,
	/** record is punched */
	DAOS_REC_PUNCHED	= -1,
	/** reserved for cache miss */
	DAOS_REC_MISSING	= -2,
};

/** Scatter/gather list for memory buffers */
typedef struct {
	daos_nr_t	 sg_nr;
	daos_iov_t	*sg_iovs;
} daos_sg_list_t;

typedef enum {
	/* hole extent */
	VOS_EXT_HOLE	= (1 << 0),
} vos_ext_flag_t;

typedef enum {
	/** list distribution keys only, no record is returned */
	DAOS_DKEY_ONLY	= (1 << 0),
	/** restrict enumeration to attribute keys available for a given
	 * distribution key */
	DAOS_AKEY_ONLY	= (1 << 1),
} daos_filter_type_t;

/** DAOS enumeration filter */
typedef struct {
	/** used with DAOS_DKEY_ONLY to store the specific dkey to limit
	 * the iteration over */
	daos_dkey_t		lf_dkey;
	/* type of enumeration, see daos_filter_type_t */
	uint16_t		lf_type;
	/** reserved */
	uint16_t		lf_reserv_16;
	uint32_t		lf_reserv_32;
} daos_list_filter_t;

/**
 * 256-bit object ID, it can identify a unique bottom level object.
 * (a shard of upper level object).
 */
typedef struct {
	/** public section, high level object ID */
	daos_obj_id_t		id_pub;
	/** private section, object shard index */
	uint32_t		id_shard;
	/** padding */
	uint32_t		id_pad_32;
} daos_unit_oid_t;

#endif /* DAOS_TYPES_H */
