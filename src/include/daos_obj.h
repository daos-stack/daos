/**
 * (C) Copyright 2015-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DAOS_OBJ_H__
#define __DAOS_OBJ_H__

#define daos_obj_generate_oid daos_obj_generate_oid2

#if defined(__cplusplus)
extern "C" {
#endif

#include <daos_types.h>
#include <daos_event.h>
#include <daos_obj_class.h>
#include <daos_prop.h>

#define DAOS_OBJ_NIL		((daos_obj_id_t){0})

/** 32 bits for DAOS internal use */
#define OID_FMT_INTR_BITS	32
/** Number of reserved bits in object id for type */
#define OID_FMT_TYPE_BITS	8
/** Number of reserved bits in object id for class id */
#define OID_FMT_CLASS_BITS	8
/** Number of reserved bits in object id for object metadata */
#define OID_FMT_META_BITS	16

/** Bit shift for object type in object id */
#define OID_FMT_TYPE_SHIFT	(64 - OID_FMT_TYPE_BITS)
/** Bit shift for object class id in object id */
#define OID_FMT_CLASS_SHIFT	(OID_FMT_TYPE_SHIFT - OID_FMT_CLASS_BITS)
/** Bit shift for object class metadata in object id */
#define OID_FMT_META_SHIFT	(OID_FMT_CLASS_SHIFT - OID_FMT_META_BITS)

/** Maximum valid object type setting */
#define OID_FMT_TYPE_MAX	((1ULL << OID_FMT_TYPE_BITS) - 1)
/** Maximum valid object class setting */
#define OID_FMT_CLASS_MAX	((1ULL << OID_FMT_CLASS_BITS) - 1)
/** Maximum valid object metadatasetting */
#define OID_FMT_META_MAX	((1ULL << OID_FMT_META_BITS) - 1)

/** Mask for object type */
#define OID_FMT_TYPE_MASK	(OID_FMT_TYPE_MAX << OID_FMT_TYPE_SHIFT)
/** Mask for object class id */
#define OID_FMT_CLASS_MASK	(OID_FMT_CLASS_MAX << OID_FMT_CLASS_SHIFT)
/** Mask for object metadata */
#define OID_FMT_META_MASK	(OID_FMT_META_MAX << OID_FMT_META_SHIFT)

/** DAOS object type */
enum daos_otype_t {
	/** default object type, multi-level KV with hashed [ad]keys */
	DAOS_OT_MULTI_HASHED	= 0,

	/**
	 * Object ID table created on snapshot
	 */
	DAOS_OT_OIT		= 1,

	/** KV with uint64 dkeys */
	DAOS_OT_DKEY_UINT64	= 2,

	/** KV with uint64 akeys */
	DAOS_OT_AKEY_UINT64	= 3,

	/** multi-level KV with uint64 [ad]keys */
	DAOS_OT_MULTI_UINT64	= 4,

	/** KV with lexical dkeys */
	DAOS_OT_DKEY_LEXICAL	= 5,

	/** KV with lexical akeys */
	DAOS_OT_AKEY_LEXICAL	= 6,

	/** multi-level KV with lexical [ad]keys */
	DAOS_OT_MULTI_LEXICAL	= 7,

	/** flat KV (no akey) with hashed dkey */
	DAOS_OT_KV_HASHED	= 8,

	/** flat KV (no akey) with integer dkey */
	DAOS_OT_KV_UINT64	= 9,

	/** flat KV (no akey) with lexical dkey */
	DAOS_OT_KV_LEXICAL	= 10,

	/** Array with attributes stored in the DAOS object */
	DAOS_OT_ARRAY		= 11,

	/** Array with attributes provided by the user */
	DAOS_OT_ARRAY_ATTR	= 12,

	/** Byte Array with no metadata (eg DFS/POSIX) */
	DAOS_OT_ARRAY_BYTE	= 13,

	/**
	 * Second version of Object ID table.
	 */
	DAOS_OT_OIT_V2		= 14,

	DAOS_OT_MAX		= 14,

	/**
	 * reserved: Multi Dimensional Array
	 * DAOS_OT_ARRAY_MD	= 64,
	 */

	/**
	 * reserved: Block device
	 * DAOS_OT_BDEV	= 96,
	 */
};

static inline bool
daos_otype_t_is_valid(enum daos_otype_t type)
{
	return type <= DAOS_OT_MAX;
}

static inline bool
daos_pa_domain_is_valid(uint32_t pa_domain)
{
	return pa_domain == DAOS_PROP_CO_REDUN_NODE || pa_domain == DAOS_PROP_CO_REDUN_RANK;
}

static inline enum daos_otype_t
daos_obj_id2type(daos_obj_id_t oid)
{
	uint64_t type;

	type = (oid.hi & OID_FMT_TYPE_MASK) >> OID_FMT_TYPE_SHIFT;

	return (enum daos_otype_t)type;
}

static inline bool
daos_is_dkey_lexical_type(enum daos_otype_t type)
{
	switch (type) {
	case DAOS_OT_DKEY_LEXICAL:
	case DAOS_OT_MULTI_LEXICAL:
	case DAOS_OT_KV_LEXICAL:
		return true;
	default:
		return false;

	}
}

static inline bool
daos_is_dkey_lexical(daos_obj_id_t oid)
{
	return daos_is_dkey_lexical_type(daos_obj_id2type(oid));
}

static inline bool
daos_is_akey_lexical_type(enum daos_otype_t type)
{
	switch (type) {
	case DAOS_OT_AKEY_LEXICAL:
	case DAOS_OT_MULTI_LEXICAL:
		return true;
	default:
		return false;

	}
}

static inline bool
daos_is_akey_lexical(daos_obj_id_t oid)
{
	return daos_is_akey_lexical_type(daos_obj_id2type(oid));
}

static inline bool
daos_is_dkey_uint64_type(enum daos_otype_t type)
{
	switch (type) {
	case DAOS_OT_MULTI_UINT64:
	case DAOS_OT_DKEY_UINT64:
	case DAOS_OT_KV_UINT64:
	case DAOS_OT_ARRAY:
	case DAOS_OT_ARRAY_ATTR:
	case DAOS_OT_ARRAY_BYTE:
		return true;
	default:
		return false;

	}
}

static inline bool
daos_is_dkey_uint64(daos_obj_id_t oid)
{
	return daos_is_dkey_uint64_type(daos_obj_id2type(oid));
}

static inline bool
daos_is_akey_uint64_type(enum daos_otype_t type)
{
	switch (type) {
	case DAOS_OT_MULTI_UINT64:
	case DAOS_OT_AKEY_UINT64:
		return true;
	default:
		return false;

	}
}

static inline bool
daos_is_akey_uint64(daos_obj_id_t oid)
{
	return daos_is_akey_uint64_type(daos_obj_id2type(oid));
}

static inline bool
daos_is_array_type(enum daos_otype_t type)
{
	switch (type) {
	case DAOS_OT_ARRAY:
	case DAOS_OT_ARRAY_ATTR:
	case DAOS_OT_ARRAY_BYTE:
		return true;
	default:
		return false;

	}
}

static inline bool
daos_is_array(daos_obj_id_t oid)
{
	enum daos_otype_t type = daos_obj_id2type(oid);

	return daos_is_array_type(type);
}

static inline bool
daos_is_kv_type(enum daos_otype_t type)
{
	switch (type) {
	case DAOS_OT_KV_HASHED:
	case DAOS_OT_KV_UINT64:
	case DAOS_OT_KV_LEXICAL:
		return true;
	default:
		return false;

	}
}

static inline bool
daos_is_kv(daos_obj_id_t oid)
{
	enum daos_otype_t type = daos_obj_id2type(oid);

	return daos_is_kv_type(type);
}

/** Number of bits reserved in IO flags bitmap for conditional checks.  */
#define IO_FLAGS_COND_BITS	8

enum {
	/* Conditional Op: Punch key if it exists, fail otherwise */
	DAOS_COND_PUNCH = (1 << 0),
	/* Conditional Op: Insert dkey if it doesn't exist, fail otherwise */
	DAOS_COND_DKEY_INSERT = (1 << 1),
	/* Conditional Op: Update dkey if it exists, fail otherwise */
	DAOS_COND_DKEY_UPDATE = (1 << 2),
	/* Conditional Op: Fetch dkey if it exists, fail otherwise */
	DAOS_COND_DKEY_FETCH = (1 << 3),
	/* Conditional Op: Insert akey if it doesn't exist, fail otherwise */
	DAOS_COND_AKEY_INSERT = (1 << 4),
	/* Conditional Op: Update akey if it exists, fail otherwise */
	DAOS_COND_AKEY_UPDATE = (1 << 5),
	/* Conditional Op: Fetch akey if it exists, fail otherwise */
	DAOS_COND_AKEY_FETCH = (1 << 6),
	/* Indication of per akey conditional ops.  If set, the global
	 * flag should not have any akey conditional ops specified. The
	 * per akey flags will be read from the iod_flags field.
	 */
	DAOS_COND_PER_AKEY = (1 << 7),
	/** Mask for convenience */
	DAOS_COND_MASK = ((1 << IO_FLAGS_COND_BITS) - 1),
};

/**
 * Object attributes (metadata).
 * \a oa_class and \a oa_oa are mutually exclusive.
 */
struct daos_obj_attr {
	/** Optional, affinity target for the object */
	d_rank_t		 oa_rank;
	/** Optional, class attributes of object with private class */
	struct daos_oclass_attr	*oa_oa;
};

/** Object open modes */
enum {
	/** Shared read */
	DAOS_OO_RO             = (1 << 1),
	/** Shared read & write, no cache for write */
	DAOS_OO_RW             = (1 << 2),
	/** Exclusive write, data can be cached */
	DAOS_OO_EXCL           = (1 << 3),
	/** unsupported: random I/O */
	DAOS_OO_IO_RAND        = (1 << 4),
	/** unsupported: sequential I/O */
	DAOS_OO_IO_SEQ         = (1 << 5),
};

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
	/** one indivisible value update atomically */
	DAOS_IOD_SINGLE		= 1,
	/** an array of records where each record is update atomically */
	DAOS_IOD_ARRAY		= 2,
} daos_iod_type_t;

/**
 * An I/O descriptor is a list of extents (effectively records associated with
 * contiguous array indices) to update/fetch in a particular array identified by
 * its akey.
 */
typedef struct {
	/** akey for this iod */
	daos_key_t		iod_name;
	/**
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
	/** Per akey conditional. If DAOS_COND_PER_AKEY not set, this is
	 *  ignored.
	 */
	uint64_t		iod_flags;
	/**
	 * Number of entries in the #iod_recxs for arrays,
	 * should be 1 if single value.
	 */
	uint32_t		iod_nr;
	/**
	 * Array of extents, where each extent defines the index of the first
	 * record in the extent and the number of records to access. If the
	 * type of the iod is single, this is ignored.
	 */
	daos_recx_t		*iod_recxs;
} daos_iod_t;

/**
 * I/O map flags -
 * DAOS_IOMF_DETAIL	zero means only need to know the iom_recx_hi/lo.
 *			1 means need to retrieve detailed iom_recxs array, in
 *			that case user can either -
 *			1) provides allocated iom_recxs buffer (iom_nr indicates
 *			   #elements allocated), if returned iom_nr_out is
 *			   greater than iom_nr, iom_recxs will still be
 *			   populated, but it will be a truncated list).
 *			2) provides NULL iod_recxs and zero iom_nr, in that case
 *			   DAOS will internally allocated needed buffer for
 *			   iom_recxs array (#elements is iom_nr, and equals
 *			   iom_nr_out). User is responsible for free the
 *			   iom_recxs buffer after using.
 */
#define DAOS_IOMF_DETAIL		(0x1U)
/**
 * A I/O map represents the physical extent mapping inside an array for a
 * given range of indices.
 */
typedef struct {
	/** type of akey value (SV or AR)*/
	daos_iod_type_t		 iom_type;
	/**
	 * Number of elements allocated in iom_recxs.
	 */
	uint32_t		 iom_nr;
	/**
	 * Number of extents in the mapping. If iom_nr_out is greater than
	 * iom_nr, iom_recxs will still be populated, but it will be a
	 * truncated list.
	 * 1 for SV.
	 */
	uint32_t		 iom_nr_out;
	/** I/O map flags */
	uint32_t		 iom_flags;
	/** Size of the single value or the record size */
	daos_size_t		 iom_size;
	/**
	 * The recx with the lowest offset within the requested extents
	 * daos_io_desc::iod_recxs
	 */
	daos_recx_t		 iom_recx_lo;
	/**
	 * The recx with the highest offset within the requested extents
	 * daos_io_desc::iod_recxs. It is set to zero for single value,
	 * or there is only one returned recx.
	 */
	daos_recx_t		 iom_recx_hi;
	/** All the returned recxs within the requested extents. Must be
	 * allocated and freed by caller.
	 */
	daos_recx_t		*iom_recxs;
} daos_iom_t;

/** record status */
enum {
	/** Any record size, it is used by fetch */
	DAOS_REC_ANY		= 0,
};

/** Mask for daos_obj_query_key() flags to indicate what is being queried */
enum {
	/** retrieve the max of dkey, akey, and/or idx of array value */
	DAOS_GET_MAX		= (1 << 0),
	/** retrieve the min of dkey, akey, and/or idx of array value */
	DAOS_GET_MIN		= (1 << 1),
	/** retrieve the dkey */
	DAOS_GET_DKEY		= (1 << 2),
	/** retrieve the akey */
	DAOS_GET_AKEY		= (1 << 3),
	/** retrieve the idx of array value */
	DAOS_GET_RECX		= (1 << 4),
};

/**
 * Key descriptor used for key enumeration. The actual key and checksum are
 * stored in a separate buffer (i.e. sgl)
 */
typedef struct {
	/** Key length */
	daos_size_t	kd_key_len;
	/**
	 * Flag for akey value types: DAOS_IOD_SINGLE, DAOS_IOD_ARRAY.
	 * It is ignored for dkey enumeration.
	 */
	uint32_t	kd_val_type;
} daos_key_desc_t;

static enum daos_obj_redun
daos_obj_id2ord(daos_obj_id_t oid)
{
	return (enum daos_obj_redun)((oid.hi & OID_FMT_CLASS_MASK) >> OID_FMT_CLASS_SHIFT);
}

static inline daos_oclass_id_t
daos_obj_id2class(daos_obj_id_t oid)
{
	enum daos_obj_redun ord;
	uint32_t nr_grps;

	ord = daos_obj_id2ord(oid);
	nr_grps = (oid.hi & OID_FMT_META_MASK) >> OID_FMT_META_SHIFT;

	return (ord << OC_REDUN_SHIFT) | nr_grps;
}

static inline bool
daos_obj_id_is_nil(daos_obj_id_t oid)
{
	return oid.hi == 0 && oid.lo == 0;
}

#define DAOS_OCH_RDD_BITS	4
#define DAOS_OCH_SHD_BITS	6
#define DAOS_OCH_RDD_SHIFT	0
#define DAOS_OCH_SHD_SHIFT	DAOS_OCH_RDD_BITS
#define DAOS_OCH_RDD_MAX_VAL	((1ULL << DAOS_OCH_RDD_BITS) - 1)
#define DAOS_OCH_SHD_MAX_VAL	((1ULL << DAOS_OCH_SHD_BITS) - 1)
#define DAOS_OCH_RDD_MASK	(DAOS_OCH_RDD_MAX_VAL << DAOS_OCH_RDD_SHIFT)
#define DAOS_OCH_SHD_MASK	(DAOS_OCH_SHD_MAX_VAL << DAOS_OCH_SHD_SHIFT)

/** Flags for oclass hints */
enum {
	/** Flags to control OC Redundancy */
	DAOS_OCH_RDD_DEF	= (1 << 0),	/** Default - use RF prop */
	DAOS_OCH_RDD_NO		= (1 << 1),	/** No redundancy */
	DAOS_OCH_RDD_RP		= (1 << 2),	/** Replication */
	DAOS_OCH_RDD_EC		= (1 << 3),	/** Erasure Code */
	/** Flags to control OC Sharding */
	DAOS_OCH_SHD_DEF	= (1 << 4),	/** Default: Use MAX for array &
						 * flat KV; 1 grp for others.
						 */
	DAOS_OCH_SHD_TINY	= (1 << 5),	/** 1 grp */
	DAOS_OCH_SHD_REG	= (1 << 6),	/** max(128, 25%) */
	DAOS_OCH_SHD_HI		= (1 << 7),	/** max(256, 50%) */
	DAOS_OCH_SHD_EXT	= (1 << 8),	/** max(1024, 80%) */
	DAOS_OCH_SHD_MAX	= (1 << 9),	/** 100% */
};

/**
 * Generate a DAOS object ID by encoding the private DAOS bits of the object
 * address space. This allows the user to either select an object class
 * manually, or ask DAOS to generate one based on some hints provided.
 *
 * \param[in]	coh	Container open handle.
 * \param[in,out]
 *		oid	[in]: Object ID with low 96 bits set and unique inside
 *			the container.
 *			[out]: Fully populated DAOS object identifier with the
 *			the low 96 bits untouched and the DAOS private bits
 *			(the high 32 bits) encoded.
 * \param[in]	type	Object type (e.g. KV or array)
 * \param[in]	cid	Class Identifier. This setting is for advanced users who
 *			are knowledgeable on the specific oclass being set and
 *			what that means for the object in the current system and
 *			the container it's in.
 *			Setting this to 0 (unknown) will check if there are any
 *			hints specified and use an oclass accordingly. If there
 *			are no hints specified we use the container properties
 *			to select the object class.
 * \param[in]   hints	Optional hints (see DAOS_OCH_*) to select oclass with
 *			redundancy type	and sharding. This will be ignored if
 *			cid is not OC_UNKNOWN (0).
 * \param[in]	args	Reserved.
 */
int
daos_obj_generate_oid(daos_handle_t coh, daos_obj_id_t *oid,
		      enum daos_otype_t type, daos_oclass_id_t cid,
		      daos_oclass_hints_t hints, uint32_t args);

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
 * \param[in]	flags	Punch flags (currently ignored).
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
 *			-DER_EP_OLD	Related RPC is resent too late as to
 *					related resent history may have been
 *					aggregated. Punch result is undefined.
 */
int
daos_obj_punch(daos_handle_t oh, daos_handle_t th, uint64_t flags,
	       daos_event_t *ev);

/**
 * Punch dkeys (with all akeys) from an object.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Optional transaction handle to punch dkeys in.
 *			Use DAOS_TX_NONE for an independent transaction.
 * \param[in]	flags	Punch flags (conditional ops).
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
 *			-DER_EP_OLD	Related RPC is resent too late as to
 *					related resent history may have been
 *					aggregated. Punch result is undefined.
 */
int
daos_obj_punch_dkeys(daos_handle_t oh, daos_handle_t th, uint64_t flags,
		     unsigned int nr, daos_key_t *dkeys, daos_event_t *ev);

/**
 * Punch akeys (with all records) from an object.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Optional transaction handle to punch akeys in.
 *			Use DAOS_TX_NONE for an independent transaction.
 * \param[in]	flags	Punch flags (conditional ops).
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
 *			-DER_EP_OLD	Related RPC is resent too late as to
 *					related resent history may have been
 *					aggregated. Punch result is undefined.
 */
int
daos_obj_punch_akeys(daos_handle_t oh, daos_handle_t th, uint64_t flags,
		     daos_key_t *dkey, unsigned int nr, daos_key_t *akeys,
		     daos_event_t *ev);

/**
 * Query attributes of an object.
 * Caller should provide at least one of the output parameters.
 *
 * \param[in]	oh	Object open handle.
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
daos_obj_query(daos_handle_t oh, struct daos_obj_attr *oa, d_rank_list_t *ranks,
	       daos_event_t *ev);

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
 * \param[in]	flags	Fetch flags (conditional ops).
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
 *			[out]: Checksum of each extent is returned via
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
 * \param[out]	ioms	Optional, upper layers can simply pass in NULL.
 *			It is the sink buffer to store the returned actual
 *			layout of the iods used in fetch. It gives information
 *			for every iod on the highest/lowest extent in that dkey,
 *			in additional to the valid extents from the ones fetched
 *			(if asked for). If the extents don't fit in the io_map,
 *			the number required is set on the fetch in
 *			\a ioms[]::iom_nr for that particular iod.
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
daos_obj_fetch(daos_handle_t oh, daos_handle_t th, uint64_t flags,
	       daos_key_t *dkey, unsigned int nr, daos_iod_t *iods,
	       d_sg_list_t *sgls, daos_iom_t *ioms, daos_event_t *ev);

/**
 * Insert or update object records stored in co-located arrays.
 *
 * \param[in]	oh	Object open handle.
 *
 * \param[in]	th	Optional transaction handle to update with.
 *			Use DAOS_TX_NONE for an independent transaction.
 *
 * \param[in]	flags	Update flags (conditional ops).
 *
 * \param[in]	dkey	Distribution key associated with the update operation.
 *
 * \param[in]	nr	Number of descriptors and scatter/gather lists in
 *			respectively \a iods and \a sgls.
 *
 * \param[in]	iods	Array of I/O descriptor. Each descriptor is associated
 *			with an array identified by its akey and describes the
 *			list of record extent to update.
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
 *			-DER_EP_OLD	Related RPC is resent too late as to
 *					related resent history may have been
 *					aggregated. Update result is undefined.
 */
int
daos_obj_update(daos_handle_t oh, daos_handle_t th, uint64_t flags,
		daos_key_t *dkey, unsigned int nr, daos_iod_t *iods,
		d_sg_list_t *sgls, daos_event_t *ev);

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
		   daos_key_desc_t *kds, d_sg_list_t *sgl,
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
 *			size of each individual key in \a sgl.
 *
 * \param[in]	sgl	Scatter/gather list to store the akey list.
 *			All akeys are written contiguously, actual boundaries
 *			can be calculated thanks to \a kds.
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
		   uint32_t *nr, daos_key_desc_t *kds, d_sg_list_t *sgl,
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
daos_obj_query_key(daos_handle_t oh, daos_handle_t th, uint64_t flags,
		   daos_key_t *dkey, daos_key_t *akey, daos_recx_t *recx,
		   daos_event_t *ev);

/**
 * Retrieve the max epoch where the object has been updated.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Optional transaction handle to query at.
 *			Use DAOS_TX_NONE for an independent transaction.
 * \param[out]	epoch	max epoch at which an update to the object happened.
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
daos_obj_query_max_epoch(daos_handle_t oh, daos_handle_t th, daos_epoch_t *epoch, daos_event_t *ev);

/**
 * Verify object data consistency against the specified epoch.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	oid	Object ID.
 * \param[in]	epoch	The (stable) epoch against that the verification will
 *			be done. DAOS_EPOCH_MAX means current highest epoch.
 *
 * \return		0		Success and consistent
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_MISMATCH	Found data inconsistency
 */
int
daos_obj_verify(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch);

/**
 * Provide a function for objects to split an anchor to be able to execute a
 * parallel listing/enumeration. This routine suggests the optimal number of
 * anchors to use instead of just 1 and optionally returns all those
 * anchors. The user would allocate the array of anchors after querying the
 * number of anchors needed. Alternatively, user does not provide an array and
 * can call daos_obj_anchor_set() for every anchor to set.
 *
 * The user could suggest how many anchors to split the iteration over. This
 * feature is not supported yet.
 *
 * \param[in]	oh	Open object handle.
 * \param[in/out]
 *		nr	[in]: Number of anchors requested and allocated in
 *			\a anchors. Pass 0 for DAOS to recommend split num.
 *			[out]: Number of anchors recommended if 0 is passed in.
 * \param[in]	anchors	Optional array of anchors that are split.
 *
 * \return		These values will be returned:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 */
int
daos_obj_anchor_split(daos_handle_t oh, uint32_t *nr, daos_anchor_t *anchors);

/**
 * Set an anchor with an index based on split done with daos_obj_anchor_split.
 * The anchor passed will be re-initialized and set to start and finish
 * iteration based on the specified index.
 *
 * \param[in]   oh	Open object handle.
 * \param[in]	index	Index of set this anchor for iteration.
 * \param[in,out]
 *		anchor	Hash anchor to set.
 *
 * \return		These values will be returned:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 */
int
daos_obj_anchor_set(daos_handle_t oh, uint32_t index, daos_anchor_t *anchor);

/**
 * Set an anchor to start a particular dkey or akey for enumeration.
 *
 * \param[in]   oh	Open object handle.
 * \param[in]   dkey    dkey to set the anchor at (if akey is NULL - dkey enumeration).
 * \param[in]   akey    (optional) akey to set the anchor at (for akey enumeration).
 * \param[out]	anchor	Hash anchor to set.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 */
int
daos_obj_key2anchor(daos_handle_t oh, daos_handle_t th, daos_key_t *dkey, daos_key_t *akey,
		    daos_anchor_t *anchor, daos_event_t *ev);

/**
 * Open Object Index Table (OIT) of an container
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	epoch	epoch of a snapshot
 * \param[out]	oh	Returned OIT open handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 */
int
daos_oit_open(daos_handle_t coh, daos_epoch_t epoch,
	      daos_handle_t *oh, daos_event_t *ev);

/**
 * Close an opened Object Index Table (OIT).
 *
 * \param[in]	oh	OIT open handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 */
int
daos_oit_close(daos_handle_t oh, daos_event_t *ev);

/**
 * Enumerate object IDs snapshotted by the Object Index Table (OIT)
 *
 * \param[in]	oh	OIT open handle.
 * \param[out]	oids	Returned OIDs
 * \param[out]	oids_nr	Number of returned OIDs
 * \param[in,out]
 *		anchor	Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid OIT open handle
 *			-DER_INVAL	Invalid parameter
 */
int
daos_oit_list(daos_handle_t oh, daos_obj_id_t *oids, uint32_t *oids_nr,
	      daos_anchor_t *anchor, daos_event_t *ev);

#define DAOS_OIT_MARKER_MAX_LEN	(32)

/**
 * Mark an object ID in the Object Index Table (OIT).
 *
 * \param[in]	oh	OIT open handle.
 * \param[in]	oid	object ID in the OIT.
 * \param[in]	marker	the data/status to be marked for the OID, the max valid length (in bytes)
 *			is DAOS_OIT_MARKER_MAX_LEN.
 *			NULL to clear previously set marker.
 *			non-NULL marker but with NULL marker->iov_buf or 0 marker->iov_len is
 *			invalid argument.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid OIT open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_NONEXIST	Input OID not in the OIT.
 */
int
daos_oit_mark(daos_handle_t oh, daos_obj_id_t oid, d_iov_t *marker, daos_event_t *ev);

/**
 * Enumerate unmarked object IDs snapshotted by the Object Index Table (OIT)
 *
 * \param[in]	oh	OIT open handle.
 * \param[out]	oids	Returned OIDs
 * \param[out]	oids_nr	Number of returned OIDs
 * \param[in,out]
 *		anchor	Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid OIT open handle
 *			-DER_INVAL	Invalid parameter
 */
int
daos_oit_list_unmarked(daos_handle_t oh, daos_obj_id_t *oids, uint32_t *oids_nr,
		       daos_anchor_t *anchor, daos_event_t *ev);

/**
 * OIT filter callback, will be called for each object ID when enumerates the OIT by calling
 * daos_oit_list_filter().
 *
 * \param[in]	oid	the object ID.
 * \param[in]	marker	the data/status marked for the \a oid. NULL if the OID was not marked.
 *
 * \return		1 or other positive value
 *					the OID will be in the listed result.
 *			0		the OID will be ignored.
 *			negative value	the enumerate will be stopped and error code will be
 *					returned.
 */
typedef int
(daos_oit_filter_cb)(daos_obj_id_t oid, d_iov_t *marker);

/**
 * Enumerate object IDs snapshotted by the Object Index Table (OIT) with a filter.
 *
 * \param[in]	oh	OIT open handle.
 * \param[out]	oids	Returned OIDs
 * \param[out]	oids_nr	Number of returned OIDs
 * \param[in,out]
 *		anchor	Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 * \param[in]	filter	OIT filter callback.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid OIT open handle
 *			-DER_INVAL	Invalid parameter
 */
int
daos_oit_list_filter(daos_handle_t oh, daos_obj_id_t *oids, uint32_t *oids_nr,
		     daos_anchor_t *anchor, daos_oit_filter_cb filter, daos_event_t *ev);

#if defined(__cplusplus)
}
#endif /* __cplusplus */
#endif /* __DAOS_OBJ_H__ */
