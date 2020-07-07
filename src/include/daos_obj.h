/**
 * (C) Copyright 2015-2020 Intel Corporation.
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
#ifndef __DAOS_OBJ_H__
#define __DAOS_OBJ_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <daos_types.h>
#include <daos_event.h>
#include <daos_obj_class.h>

/**
 * ID of an object, 128 bits
 * The high 32-bit of daos_obj_id_t::hi are reserved for DAOS, the rest is
 * provided by the user and assumed to be unique inside a container.
 */
typedef struct {
	uint64_t	lo;
	uint64_t	hi;
} daos_obj_id_t;

/** the current OID version */
#define OID_FMT_VER		1

/** 32 bits for DAOS internal use */
#define OID_FMT_INTR_BITS	32
/** Number of reserved by daos in object id for version */
#define OID_FMT_VER_BITS	4
/** Number of reserved by daos in object id for features */
#define OID_FMT_FEAT_BITS	16
/** Number of reserved by daos in object id for class id */
#define OID_FMT_CLASS_BITS	(OID_FMT_INTR_BITS - OID_FMT_VER_BITS - \
				 OID_FMT_FEAT_BITS)

/** Bit shift for object version in object id */
#define OID_FMT_VER_SHIFT	(64 - OID_FMT_VER_BITS)
/** Bit shift for object features in object id */
#define OID_FMT_FEAT_SHIFT	(OID_FMT_VER_SHIFT - OID_FMT_FEAT_BITS)
/** Bit shift for object class id in object id */
#define OID_FMT_CLASS_SHIFT	(OID_FMT_FEAT_SHIFT - OID_FMT_CLASS_BITS)

/** Maximum valid object version setting */
#define OID_FMT_VER_MAX		((1ULL << OID_FMT_VER_BITS) - 1)
/** Maximum valid object feature setting */
#define OID_FMT_FEAT_MAX	((1ULL << OID_FMT_FEAT_BITS) - 1)
/** Maximum valid object class setting */
#define OID_FMT_CLASS_MAX	((1ULL << OID_FMT_CLASS_BITS) - 1)

/** Mask for object version */
#define OID_FMT_VER_MASK	(OID_FMT_VER_MAX << OID_FMT_VER_SHIFT)
/** Mask for object features */
#define OID_FMT_FEAT_MASK	(OID_FMT_FEAT_MAX << OID_FMT_FEAT_SHIFT)
/** Mask for object class id */
#define OID_FMT_CLASS_MASK	(OID_FMT_CLASS_MAX << OID_FMT_CLASS_SHIFT)

enum {
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
	/** reserved: 1-level flat KV store */
	DAOS_OF_KV_FLAT		= (1 << 4),
	/** reserved: 1D Array with metadata stored in the DAOS object */
	DAOS_OF_ARRAY		= (1 << 5),
	/** reserved: Multi Dimensional Array */
	DAOS_OF_ARRAY_MD	= (1 << 6),
	/** reserved: Byte Array with no metadata (eg DFS/POSIX) */
	DAOS_OF_ARRAY_BYTE	= (1 << 7),
	DAOS_OF_NO_INL_COPY	= (1 << 8),
	/**
	 * benchmark-only feature bit, I/O is a network echo, no data is going
	 * to be stored/returned
	 *
	 * NB: this is the last feature bits.
	 */
	DAOS_OF_ECHO		= (1 << 15),
	/** Mask for convenience (16-bit) */
	DAOS_OF_MASK		= ((1 << OID_FMT_FEAT_BITS) - 1),
};

/** Number of bits reserved in IO flags bitmap for conditional checks.  */
#define IO_FLAGS_COND_BITS	8

enum {
	/* Conditional Op: Insert dkey if it doesn't exist, fail otherwise */
	DAOS_COND_DKEY_INSERT	= (1 << 0),
	/* Conditional Op: Update dkey if it exists, fail otherwise */
	DAOS_COND_DKEY_UPDATE	= (1 << 1),
	/* Conditional Op: Fetch dkey if it exists, fail otherwise */
	DAOS_COND_DKEY_FETCH	= (1 << 2),
	/* Conditional Op: Punch dkey if it exists, fail otherwise */
	DAOS_COND_DKEY_PUNCH	= (1 << 3),

	/* Conditional Op: Insert akey if it doesn't exist, fail otherwise */
	DAOS_COND_AKEY_INSERT	= (1 << 4),
	/* Conditional Op: Update akey if it exists, fail otherwise */
	DAOS_COND_AKEY_UPDATE	= (1 << 5),
	/* Conditional Op: Fetch akey if it exists, fail otherwise */
	DAOS_COND_AKEY_FETCH	= (1 << 6),
	/* Conditional Op: Punch akey if it exists, fail otherwise */
	DAOS_COND_AKEY_PUNCH	= (1 << 7),
	/** Mask for convenience */
	DAOS_COND_MASK		= ((1 << IO_FLAGS_COND_BITS) - 1),
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

struct daos_oid_list {
	/** Input/output number of oids */
	uint32_t		 ol_nr;
	uint32_t		 ol_nr_out;
	/** OID buffer */
	daos_obj_id_t		*ol_oids;
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
	/** one indivisble value udpate atomically */
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
	/*
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
	/*
	 * Number of entries in the \a iod_recxs for arrays,
	 * should be 1 if single value.
	 */
	unsigned int		iod_nr;
	/*
	 * Array of extents, where each extent defines the index of the first
	 * record in the extent and the number of records to access. If the
	 * type of the iod is single, this is ignored.
	 */
	daos_recx_t		*iod_recxs;
} daos_iod_t;

/**
 * A I/O map represents the physical extent mapping inside an array for a
 * given range of indices.
 */
typedef struct {
	/** type of akey value (SV or AR)*/
	daos_iod_type_t		 iom_type;
	/**
	 * Number of extents in the mapping, that's the size of all the
	 * external arrays listed below. 1 for SV.
	 */
	unsigned int		 iom_nr;
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
	/** All the returned recxs within the requested extents */
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
	/** Checksum type */
	uint16_t	kd_csum_type;
	/** Checksum length */
	uint16_t	kd_csum_len;
} daos_key_desc_t;

/**
 * Generate a DAOS object ID by encoding the private DAOS bits of the object
 * address space.
 *
 * \param[in,out]
 *		oid	[in]: Object ID with low 96 bits set and unique inside
 *			the container. [out]: Fully populated DAOS object
 *			identifier with the the low 96 bits untouched and the
 *			DAOS private bits (the high 32 bits) encoded.
 * \param[in]	ofeat	Feature bits specific to object
 * \param[in]	cid	Class Identifier
 * \param[in]	args	Reserved.
 */
static inline int
daos_obj_generate_id(daos_obj_id_t *oid, daos_ofeat_t ofeats,
		     daos_oclass_id_t cid, uint32_t args)
{
	uint64_t hdr;

	/* TODO: add check at here, it should return error if user specified
	 * bits reserved by DAOS
	 */
	oid->hi &= (1ULL << OID_FMT_INTR_BITS) - 1;
	/**
	 * | Upper bits contain
	 * | OID_FMT_VER_BITS (version)		 |
	 * | OID_FMT_FEAT_BITS (object features) |
	 * | OID_FMT_CLASS_BITS (object class)	 |
	 * | 96-bit for upper layer ...		 |
	 */
	hdr  = ((uint64_t)OID_FMT_VER << OID_FMT_VER_SHIFT);
	hdr |= ((uint64_t)ofeats << OID_FMT_FEAT_SHIFT);
	hdr |= ((uint64_t)cid << OID_FMT_CLASS_SHIFT);
	oid->hi |= hdr;

	return 0;
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
 * \param[in]	flags	Punch flags (currently ignored).
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
 * \param[in]	flags	Punch flags (currently ignored).
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
daos_obj_query(daos_handle_t oh, daos_handle_t th, struct daos_obj_attr *oa,
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
 * \param[in]	flags	Fetch flags (currently ignored).
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
 * \param[out]	maps	Optional, upper layers can simply pass in NULL.
 *			It is the sink buffer to store the returned actual
 *			layout of the iods used in fetch. It gives information
 *			for every iod on the highest/lowest extent in that dkey,
 *			in additional to the valid extents from the ones fetched
 *			(if asked for). If the extents don't fit in the io_map,
 *			the number required is set on the fetch in
 *			\a maps[]::iom_nr for that particular iod.
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
	       d_sg_list_t *sgls, daos_iom_t *maps, daos_event_t *ev);

/**
 * Insert or update object records stored in co-located arrays.
 *
 * \param[in]	oh	Object open handle.
 *
 * \param[in]	th	Optional transaction handle to update with.
 *			Use DAOS_TX_NONE for an independent transaction.
 *
 * \param[in]	flags	Update flags (currently ignored).
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

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_OBJ_H__ */
