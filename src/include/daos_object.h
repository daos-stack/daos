/*
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
 * \file
 *
 * DAOS Object API methods
 */

#ifndef __DAOS_OBJECT_H__
#define __DAOS_OBJECT_H__

#include <daos_types.h>

#if defined(__cplusplus)
extern "C" {
#endif

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
	d_rank_t		 oa_rank;
	/** Optional, class attributes of object with private class */
	daos_oclass_attr_t	*oa_oa;
} daos_obj_attr_t;

/** key type */
typedef daos_iov_t daos_key_t;

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


/*
 * Object managementAPI
 */

/**
 * Register a new object class in addition to the default ones (see DAOS_OC_*).
 * An object class cannot be unregistered for the time being.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	cid	ID for the new object class.
 * \param[in]	cattr	Attributes for the new object class.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EXIST	Object class ID already existed
 */
int
daos_obj_register_class(daos_handle_t coh, daos_oclass_id_t cid,
			daos_oclass_attr_t *cattr, daos_event_t *ev);

/**
 * Query attributes of an object class by its ID.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	cid	Class ID to query.
 * \param[out]	cattr	Returned attributes of the object class.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NONEXIST	Nonexistent class ID
 */
int
daos_obj_query_class(daos_handle_t coh, daos_oclass_id_t cid,
		     daos_oclass_attr_t *cattr, daos_event_t *ev);

/**
 * List existing object classes.
 *
 * \param[in]	coh	Container open handle.
 * \param[out]	clist	Sink buffer for returned class list.
 * \param[in,out]
 *		anchor	Hash anchor for the next call. It should be set to
 *			zeroes for the first call. It should not be altered
 *			by caller between calls.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 */
int
daos_obj_list_class(daos_handle_t coh, daos_oclass_list_t *clist,
		    daos_anchor_t *anchor, daos_event_t *ev);

/**
 * Generate a DAOS object ID by encoding the private DAOS bits of the object
 * address space.
 *
 * \param[in,out]
 *		oid	[in]: Object ID with low 160 bits set and unique inside
 *			the container. [out]: Fully populated DAOS object
 *			identifier with the the low 96 bits untouched and the
 *			DAOS private bits (the high 32 bits) encoded.
 * \param[in]	ofeat	Feature bits specific to object
 * \param[in]	cid	Class Identifier
 */
static inline void
daos_obj_generate_id(daos_obj_id_t *oid, daos_ofeat_t ofeats,
		     daos_oclass_id_t cid)
{
	uint64_t hdr = cid;

	oid->hi &= 0x00000000ffffffff;
	/**
	 * | Upper bits contain
	 * | DAOS_OVERSION_BITS version        |
	 * | DAOS_OFEAT_BITS object features   |
	 * | DAOS_OCLASS_BITS object class     |
	 * | 96-bit for upper layer ...        |
	 */
	hdr <<= DAOS_OCLASS_SHIFT;
	hdr |= 0x1ULL << DAOS_OVERSION_SHIFT;
	hdr |= ((uint64_t)ofeats << DAOS_OFEAT_SHIFT);
	oid->hi |= hdr;
}

/**
 * Generate a rank list from a string with a seprator argument. This is a
 * convenience function to generate the rank list required by
 * daos_pool_connect().
 *
 * \param[in]	str	string with the rank list
 * \param[in]	sep	separator of the ranks in \a str.
 *			dmg uses ":" as the separator.
 *
 * \return		allocated rank list that user is responsible to free
 *			with daos_rank_list_free().
 */
d_rank_list_t *
daos_rank_list_parse(const char *str, const char *sep);

static inline daos_oclass_id_t
daos_obj_id2class(daos_obj_id_t oid)
{
	daos_oclass_id_t ocid;

	ocid = (oid.hi & DAOS_OCLASS_MASK) >> DAOS_OCLASS_SHIFT;
	return ocid;
}

static inline bool
daos_oc_echo_type(daos_oclass_id_t oc)
{
	return oc == DAOS_OC_ECHO_TINY_RW ||
	       oc == DAOS_OC_ECHO_R2S_RW ||
	       oc == DAOS_OC_ECHO_R3S_RW ||
	       oc == DAOS_OC_ECHO_R4S_RW;
}

static inline daos_ofeat_t
daos_obj_id2feat(daos_obj_id_t oid)
{
	daos_ofeat_t ofeat;

	ofeat = (oid.hi & DAOS_OFEAT_MASK) >> DAOS_OFEAT_SHIFT;
	return ofeat;
}

static inline uint8_t
daos_obj_id2version(daos_obj_id_t oid)
{
	uint8_t version;

	version = (oid.hi & DAOS_OVERSION_MASK) >> DAOS_OVERSION_SHIFT;
	return version;
}

/**
 * Open an DAOS object.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	oid	Object ID.
 * \param[in]	mode	Open mode: DAOS_OO_RO/RW/EXCL/IO_RAND/IO_SEQ
 * \param[out]	oh	Returned object open handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 *			-DER_NONEXIST	Cannot find object
 *			-DER_EP_OLD	Epoch is too old and has no data for
 *					this object
 */
int
daos_obj_open(daos_handle_t coh, daos_obj_id_t oid, unsigned int mode,
	      daos_handle_t *oh, daos_event_t *ev);

/**
 * Close an opened object.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 */
int
daos_obj_close(daos_handle_t oh, daos_event_t *ev);

/**
 * Punch an entire object with all keys associated with it.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Optional transaction handle to punch object in.
 *			Use DAOS_TX_NONE for an independent transaction.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Permission denied
 *			-DER_NOEXIST	Nonexistent object ID
 */
int
daos_obj_punch(daos_handle_t oh, daos_handle_t th, daos_event_t *ev);

/**
 * Punch dkeys (with all akeys) from an object.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Optional transaction handle to punch dkeys in.
 *			Use DAOS_TX_NONE for an independent transaction.
 * \param[in]	nr	number of dkeys to punch.
 * \param[in]	dkeys	Array of dkeys to punch.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Permission denied
 *			-DER_NOEXIST	Nonexistent object ID
 */
int
daos_obj_punch_dkeys(daos_handle_t oh, daos_handle_t th, unsigned int nr,
		     daos_key_t *dkeys, daos_event_t *ev);

/**
 * Punch akeys (with all records) from an object.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Optional transaction handle to punch akeys in.
 *			Use DAOS_TX_NONE for an independent transaction.
 * \param[in]	dkey	dkey to punch akeys from.
 * \param[in]	nr	number of akeys to punch.
 * \param[in]	akeys	Array of akeys to punch.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Permission denied
 *			-DER_NOEXIST	Nonexistent object ID
 */
int
daos_obj_punch_akeys(daos_handle_t oh, daos_handle_t th, daos_key_t *dkey,
		     unsigned int nr, daos_key_t *akeys, daos_event_t *ev);

/**
 * Query attributes of an object.
 * Caller should provide at least one of the output parameters.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Optional transaction handle to query with.
 *			Use DAOS_TX_NONE for an independent transaction.
 * \param[out]	oa	Returned object attributes.
 * \param[out]	ranks	Ordered list of ranks where the object is stored.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 */
int
daos_obj_query(daos_handle_t oh, daos_handle_t th, daos_obj_attr_t *oa,
	       d_rank_list_t *ranks, daos_event_t *ev);

/*
 * Object I/O API
 */

/**
 * Fetch object records from co-located arrays.
 *
 * \param[in]	oh	Object open handle.
 *
 * \param[in]	th	Optional transaction handle to fetch with.
 *			Use DAOS_TX_NONE for an independent transaction.
 *
 * \param[in]	dkey	Distribution key associated with the fetch operation.
 *
 * \param[in]	nr	Number of I/O descriptor and scatter/gather lists in
 *			respectively \a iods and \a sgls.
 *
 * \param[in,out]
 *		iods	[in]: Array of I/O descriptors. Each descriptor is
 *			associated with a given akey and describes the list of
 *			record extents to fetch from the array.
 *			A different epoch can be passed for each extent via
 *			\a iods[]::iod_eprs[] and in this case, \a epoch will be
 *			ignored. [out]: Checksum of each extent is returned via
 *			\a iods[]::iod_csums[]. If the record size of an
 *			extent is unknown (i.e. set to DAOS_REC_ANY as input),
 *			then the actual record size will be returned in
 *			\a iods[]::iod_size.
 *
 * \param[in]	sgls	Scatter/gather lists (sgl) to store records. Each array
 *			is associated with a separate sgl in \a sgls.
 *			I/O descs in each sgl can be arbitrary as long as their
 *			total size is sufficient to fill in all returned data.
 *			For example, extents with records of different sizes can
 *			be adjacently stored in the same iod of the sgl of the
 *			I/O descriptor start offset of an extent is the end
 *			offset of the previous extent.
 *			For an unfound record, the output length of the
 *			corresponding sgl is set to zero.
 *
 * \param[out]	map	Optional, upper layers can simply pass in NULL.
 *			It is the sink buffer to store the returned actual
 *			index layouts and their epoch validities. The returned
 *			layout covers the record extents as \a iods.
 *			However, the returned extents could be fragmented if
 *			these extents were partially updated in different
 *			epochs. Additionally, the returned extents should also
 *			allow to discriminate punched extents from punched
 *			holes.
 *
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_REC2BIG	Record is too large and can't be
 *					fit into output buffer
 *			-DER_EP_OLD	Epoch is too old and has no data
 */
int
daos_obj_fetch(daos_handle_t oh, daos_handle_t th, daos_key_t *dkey,
	       unsigned int nr, daos_iod_t *iods, daos_sg_list_t *sgls,
	       daos_iom_t *maps, daos_event_t *ev);

/**
 * Insert or update object records stored in co-located arrays.
 *
 * \param[in]	oh	Object open handle.
 *
 * \param[in]	th	Optional transaction handle to update with.
 *			Use DAOS_TX_NONE for an independent transaction.
 *
 * \param[in]	dkey	Distribution key associated with the update operation.
 *
 * \param[in]	nr	Number of descriptors and scatter/gather lists in
 *			respectively \a iods and \a sgls.
 *
 * \param[in]	iods	Array of I/O descriptor. Each descriptor is associated
 *			with an array identified by its akey and describes the
 *			list of record extent to update.
 *			A different epoch can be passed for each extent via
 *			\a iods[]::iod_eprs[] and in this case, \a epoch will be
 *			ignored.
 *			Checksum of each record extent is stored in
 *			\a iods[]::iod_csums[]. If the record size of an extent
 *			is zero, then it is effectively a punch for the
 *			specified index range.
 *
 * \param[in]	sgls	Scatter/gather list (sgl) to store the input data
 *			records. Each I/O descriptor owns a separate sgl in
 *			\a sgls.
 *			Different records of the same extent can either be
 *			stored in separate iod of the sgl, or contiguously
 *			stored in arbitrary iods as long as total buffer size
 *			can match the total extent size.
 *
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_REC2BIG	Record is larger than the buffer in
 *					input \a sgls buffer.
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Epoch is read-only
 */
int
daos_obj_update(daos_handle_t oh, daos_handle_t th, daos_key_t *dkey,
		unsigned int nr, daos_iod_t *iods, daos_sg_list_t *sgls,
		daos_event_t *ev);

/**
 * Distribution key enumeration.
 *
 * \param[in]	oh	Object open handle.
 *
 * \param[in]	th	Optional transaction handle to enumerate with.
 *			Use DAOS_TX_NONE for an independent transaction.
 *
 * \param[in,out]
 *		nr	[in]: number of key descriptors in \a kds. [out]: number
 *			of returned key descriptors.
 *
 * \param[in,out]
 *		kds	[in]: preallocated array of \nr key descriptors. [out]:
 *			size of each individual key along with checksum type
 *			and size stored just after the key in \a sgl.
 *
 * \param[in]	sgl	Scatter/gather list to store the dkey list.
 *			All dkeys are written contiguously with their checksum,
 *			actual boundaries can be calculated thanks to \a kds.
 *
 * \param[in,out]
 *		anchor	Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 *
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_KEY2BIG	Key is too large and can't be fit into
 *					the \a sgl, the required minimal length
 *					to fit the key is returned by
 *					\a kds[0].kd_key_len. This error code
 *					only returned for the first key in this
 *					enumeration, then user can provide a
 *					larger buffer (for example two or three
 *					times \a kds[0].kd_key_len) and do the
 *					enumerate again.
 */
int
daos_obj_list_dkey(daos_handle_t oh, daos_handle_t th, uint32_t *nr,
		   daos_key_desc_t *kds, daos_sg_list_t *sgl,
		   daos_anchor_t *anchor, daos_event_t *ev);

/**
 * Attribute key enumeration.
 *
 * \param[in]	oh	Object open handle.
 *
 * \param[in]	th	Optional transaction handle to enumerate with.
 *			Use DAOS_TX_NONE for an independent transaction.
 *
 * \param[in]	dkey	distribution key for the akey enumeration
 *
 * \param[in,out]
 *		nr	[in]: number of key descriptors in \a kds. [out]: number
 *			of returned key descriptors.
 *
 * \param[in,out]
 *		kds	[in]: preallocated array of \nr key descriptors. [out]:
 *			size of each individual key along with checksum type,
 *			size, and type stored just after the key in \a sgl.
 *
 * \param[in]	sgl	Scatter/gather list to store the akey list.
 *			All akeys are written contiguously with their checksum,
 *			actual boundaries can be calculated thanks to \a kds.
 *
 * \param[in,out]
 *		anchor	Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 *
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_KEY2BIG	Key is too large and can't be fit into
 *					the \a sgl, the required minimal length
 *					to fit the key is returned by
 *					\a kds[0].kd_key_len. This error code
 *					only returned for the first key in this
 *					enumeration, then user can provide a
 *					larger buffer (for example two or three
 *					times \a kds[0].kd_key_len) and do the
 *					enumerate again.
 */
int
daos_obj_list_akey(daos_handle_t oh, daos_handle_t th, daos_key_t *dkey,
		   uint32_t *nr, daos_key_desc_t *kds, daos_sg_list_t *sgl,
		   daos_anchor_t *anchor, daos_event_t *ev);

/**
 * Extent enumeration of valid records in the array.
 *
 * \param[in]	oh	Object open handle.
 *
 * \param[in]	th	Optional transaction handle to enumerate with.
 *			Use DAOS_TX_NONE for an independent transaction.
 *
 * \param[in]	dkey	distribution key for the enumeration
 *
 * \param[in]	akey	attribute key for the enumeration
 *
 * \param[out]	size	record size
 *
 * \param[in,out]
 *		nr	[in]: number of records in \a recxs. [out]: number of
 *			returned recxs.
 *
 * \param[in,out]
 *		recxs	[in]: preallocated array of \nr records. [out]: returned
 *			records.
 *
 * \param[in,out]
 *		eprs	[in]: preallocated array of \nr epoch ranges. [out]:
 *			returned epoch ranges.
 *
 * \param[in,out]
 *		anchor	Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 *
 * \param[in]	incr_order
 *			If this is set to true, extents will be listed in
 *			increasing index order, otherwise if false, they are
 *			listed in decreasing order. Once an anchor is associated
 *			with an order, further calls with that anchor should use
 *			the same order setting.
 *
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 */
int
daos_obj_list_recx(daos_handle_t oh, daos_handle_t th, daos_key_t *dkey,
		   daos_key_t *akey, daos_size_t *size, uint32_t *nr,
		   daos_recx_t *recxs, daos_epoch_range_t *eprs,
		   daos_anchor_t *anchor, bool incr_order,
		   daos_event_t *ev);

/**
 * Retrieve the largest or smallest integer DKEY, AKEY, and array offset from an
 * object. If object does not have an array value, 0 is returned in extent. User
 * has to specify what is being queried (dkey, akey, and/or recx) along with the
 * query type (max or min) in flags. If one of those is not provided the
 * function will fail. If the dkey or akey are not being queried, there value
 * must be provided by the user.
 *
 * If searching in a particular dkey for the max akey and max offset in that
 * akey, user would supply the dkey value and a flag of: DAOS_GET_MAX |
 * DAOS_GET_AKEY | DAOS_GET_RECX.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Optional transaction handle to query at.
 *			Use DAOS_TX_NONE for an independent transaction.
 * \param[in]	flags	mask with the following options:
 *			DAOS_GET_DKEY, DAOS_GET_AKEY, DAOS_GET_RECX,
 *			DAOS_GET_MAX, DAOS_GET_MIN
 *			User has to indicate whether to query the MAX or MIN, in
 *			addition to what needs to be queried. Providing
 *			(MAX | MIN) in any combination will return an error.
 *			i.e. user can only query MAX or MIN in one call.
 * \param[in,out]
 *		dkey	[in]: allocated integer dkey. User can provide the dkey
 *			if not querying the max or min dkey.
 *			[out]: max or min dkey (if flag includes dkey query).
 * \param[in,out]
 *		akey	[in]: allocated integer akey. User can provide the akey
 *			if not querying the max or min akey.
 *			[out]: max or min akey (if flag includes akey query).
 * \param[out]	recx	max or min offset in dkey/akey, and the size of the
 *			extent at the offset. If there are no visible array
 *			records, the size in the recx returned will be 0.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 */
int
daos_obj_query_key(daos_handle_t oh, daos_handle_t th, uint32_t flags,
		   daos_key_t *dkey, daos_key_t *akey, daos_recx_t *recx,
		   daos_event_t *ev);

/**
 * Fetch Multiple Dkeys in a single call. Behaves the same as daos_obj_fetch but
 * for multiple dkeys.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Transaction handle.
 * \param[in]	nr	Number of dkeys in \a io_array.
 * \param[in,out]
 *		io_array
 *			Array of io descriptors for all dkeys, which describes
 *			another array of iods for akeys within each dkey.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Epoch is read-only
 */
int
daos_obj_fetch_multi(daos_handle_t oh, daos_handle_t th, unsigned int nr,
		     daos_dkey_io_t *io_array, daos_event_t *ev);

/**
 * Update/Insert/Punch Multiple Dkeys in a single call. Behaves the same as
 * daos_obj_fetch but for multiple dkeys.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Transaction handle.
 * \param[in]	nr	Number of dkeys in \a io_array.
 * \param[in]	io_array
 *			Array of io descriptors for all dkeys, which describes
 *			another array of iods for akeys within each dkey.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Epoch is read-only
 */
int
daos_obj_update_multi(daos_handle_t oh, daos_handle_t th, unsigned int nr,
		      daos_dkey_io_t *io_array, daos_event_t *ev);

#if defined(__cplusplus)
}
#endif
#endif /* __DAOS_OBJECT_H__ */
