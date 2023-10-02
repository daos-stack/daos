/**
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_CHECKSUM_H
#define __DAOS_CHECKSUM_H

#include <daos_types.h>
#include <daos_obj.h>
#include <daos_prop.h>

#include <daos/multihash.h>

#define	CSUM_NO_CHUNK -1

/*
 * Tag used in debug logs to easily find important checksum info, making it
 * easier to trace checksums through the log files
 */
#define CSTAG "[CSUM]"

#define DF_C_IOD CSTAG"IOD {akey: "DF_KEY", type: %s, nr: %d, size: %lu} CSUM"
#define DP_C_IOD(i) DP_KEY(&(i)->iod_name), \
	(i)->iod_type == DAOS_IOD_SINGLE ? "SINGLE" : \
	(i)->iod_type == DAOS_IOD_ARRAY ? "ARRAY" : "UNKNOWN", \
	(i)->iod_nr, (i)->iod_size

#define DF_C_UOID_DKEY CSTAG"OBJ ("DF_UOID", "DF_KEY")"
#define DP_C_UOID_DKEY(oid, dkey) DP_UOID(oid), DP_KEY(dkey)

#define DF_C_OID_DKEY CSTAG"OBJ ("DF_OID", "DF_KEY")"
#define DP_C_OID_DKEY(oid, dkey) DP_OID(oid), DP_KEY(dkey)

#define DF_LAYOUT "{bytes: %lu, nr: %d, even_dist: %s, cell_align: %s}"
#define DP_LAYOUT(l) (l).cs_bytes, (l).cs_nr, DP_BOOL((l).cs_even_dist), \
			DP_BOOL((l).cs_cell_align)
#define	DF_CI_BUF "%"PRIu64
#define	DP_CI_BUF(buf, len) ci_buf2uint64(buf, len)
#define	DF_CI "{nr: %d, len: %d, first_csum: %lu, csum_buf_len: %d}"
#define	DP_CI(ci) (ci).cs_nr, (ci).cs_len, ci2csum(ci), (ci).cs_buf_len
#define DF_RANGE "{lo: %lu, hi: %lu, nr: %lu}"
#define DP_RANGE(r) (r).dcr_lo, (r).dcr_hi, (r).dcr_nr
/**
 * -----------------------------------------------------------
 * DAOS Checksummer
 * -----------------------------------------------------------
 */

struct dcs_csum_info {
	/** buffer to store the checksums */
	uint8_t		*cs_csum;
	/** number of checksums stored in buffer */
	uint32_t	 cs_nr;
	/** type of checksum */
	uint16_t	 cs_type;
	/** length of each checksum in bytes */
	uint16_t	 cs_len;
	/** length of entire buffer (cs_csum). buf_len can be larger than
	*  nr * len, but never smaller
	*/
	uint32_t	 cs_buf_len;
	/** bytes of data each checksum verifies (if value type is array) */
	uint32_t	 cs_chunksize;
};

struct dcs_iod_csums {
	/** akey checksum */
	struct dcs_csum_info	 ic_akey;
	/** csum for the data. will be 1 for each recx for arrays */
	struct dcs_csum_info	*ic_data;
	/** number of dcs_csum_info in ic_data. should be 1 for SV */
	uint32_t		 ic_nr;
};

/** Single value layout info for checksum */
struct dcs_layout {
	/** #bytes on evenly distributed targets */
	uint64_t	cs_bytes;
	/** targets number */
	uint32_t	cs_nr;
	/** even distribution flag */
	uint32_t	cs_even_dist:1,
	/**
	 * Align flag, used only for single value fetch that parity buffer's
	 * location possibly not immediately following data buffer (to align
	 * with cell size to avoid data movement in data recovery). For update
	 * the data immediately followed by parity (this flag is zero).
	 */
			cs_cell_align:1;
};

struct daos_csummer {
	/** Size of csum_buf. */
	uint32_t	 dcs_csum_buf_size;
	/** Cached configuration for chunk size*/
	uint32_t	 dcs_chunk_size;
	/** Pointer to the function table to be used for calculating csums */
	struct hash_ft	*dcs_algo;
	/** Pointer to function table specific contexts */
	void		*dcs_ctx;
	/** Points to the buffer where the calculated csum is to be written */
	uint8_t		*dcs_csum_buf;
	/** Whether or not to verify on the server on an update */
	bool		 dcs_srv_verify;
	/** Disable aspects of the checksum process */
	bool		 dcs_skip_key_calc;
	bool		 dcs_skip_key_verify;
	bool		 dcs_skip_data_verify;
	pthread_mutex_t	 dcs_lock;
};

/**
 * -----------------------------------------------------------------------------
 * daos_csummer Functions
 * -----------------------------------------------------------------------------
 */

/**
 * Initialize the daos_csummer
 *
 * @param obj		daos_csummer to be initialized. Memory will be allocated
 *			for it.
 * @param ft		Pointer to the function table for checksum calculations
 * @param chunk_bytes	Chunksize, typically from the container configuration
 * @param srv_verify	whether server-side checksum verification is enabled
 * @param dedup		whether deduplication is enabled on the server
 * @param dedup_verify	whether to memcmp data on the server for deduplication
 * @param dedup_bytes	deduplication size threshold in bytes
 *
 * @return		0 for success, or an error code
 */
int
daos_csummer_init(struct daos_csummer **obj, struct hash_ft *ft,
		  size_t chunk_bytes, bool srv_verify);

/**
 * Initialize the daos_csummer with a known DAOS_HASH_TYPE
 *
 * @param obj		daos_csummer to be initialized. Memory will be allocated
 *			for it.
 * @param type		Type of the checksum algorithm that will be used
 * @param chunk_bytes	Chunksize, typically from the container configuration
 * @param srv_verify	Whether to verify checksum on the server on update
 * @param dedup		Whether deduplication is enabled
 * @param dedup_verify	Whether to memcmp data on the server for deduplication
 * @param dedup_bytes	Deduplication size threshold
 *
 * @return		0 for success, or an error code
 */
int
daos_csummer_init_with_type(struct daos_csummer **obj, enum DAOS_HASH_TYPE type,
			    size_t chunk_bytes, bool srv_verify);

/**
 * Initialize the daos_csummer using container properties
 * @param obj		daos_csummer to be initialized. Memory will be allocated
 *			for it.
 * @param props		Container properties used to configure the daos_csummer.
 *
 * @return		0 for success, or an error code
 */
int
daos_csummer_init_with_props(struct daos_csummer **obj, daos_prop_t *props);

/**
 * Initialize the daos_csummer using information that's packed into the iov
 * @param obj		daos_csummer to be initialized. Memory will be allocated
 *			for it.
 * @param csums_iov	iov that has csum_info structures in it.
 * @return
 */
int
daos_csummer_csum_init_with_packed(struct daos_csummer **csummer,
				   const d_iov_t *csums_iov);

/**
 * Initialize a daos_csummer as a copy of an existing daos_csummer
 * @param obj		daos_csummer to be copied.
 *
 * @return		Allocated daos_csummer, or NULL if not enough memory.
 */
struct daos_csummer *
daos_csummer_copy(const struct daos_csummer *obj);

/** Destroy the daos_csummer */
void
daos_csummer_destroy(struct daos_csummer **obj);

/** Get the checksum length from the configured csummer. */
uint16_t
daos_csummer_get_csum_len(struct daos_csummer *obj);

/** Determine if the checksums is configured. */
bool
daos_csummer_initialized(struct daos_csummer *obj);

/** Get an integer representing the csum type the csummer is configured with */
uint16_t
daos_csummer_get_type(struct daos_csummer *obj);

uint32_t
daos_csummer_get_chunksize(struct daos_csummer *obj);

/** Get an appropriate chunksize (based on configured chunksize) for a
 * record in bytes. Appropriate means that the chunksize should not be larger
 * than record size and that records should evenly divide into chunk size.
 */
uint32_t
daos_csummer_get_rec_chunksize(struct daos_csummer *obj, uint64_t rec_size);

bool
daos_csummer_get_srv_verify(struct daos_csummer *obj);

/** Get a string representing the csum the csummer is configured with */
char *
daos_csummer_get_name(struct daos_csummer *obj);

/** Set the csum buffer where the calculated checksumm will be written to */
void
daos_csummer_set_buffer(struct daos_csummer *obj, uint8_t *buf,
			uint32_t buf_len);

/** Reset the csummer */
int
daos_csummer_reset(struct daos_csummer *obj);

/** Updates the checksum calculation with new input data. Can be called
 * repeatedly.\a daos_csummer_finish should be called when all data is
 * processed
 */
int
daos_csummer_update(struct daos_csummer *obj, uint8_t *buf, size_t buf_len);

/** Indicates all data has been processed for the calculation of a checksum */
int
daos_csummer_finish(struct daos_csummer *obj);

bool
daos_csummer_compare_csum_info(struct daos_csummer *obj,
			       struct dcs_csum_info *a,
			       struct dcs_csum_info *b);

bool
daos_csummer_csum_compare(struct daos_csummer *obj, uint8_t *a,
			  uint8_t *b, uint32_t csum_len);

int
daos_csummer_calc_for_iov(struct daos_csummer *csummer, daos_key_t *iov,
			  uint8_t *csum_buf, uint16_t csum_buf_len);

int
daos_csummer_calc_one(struct daos_csummer *obj, d_sg_list_t *sgl,
		       struct dcs_csum_info *csums, size_t rec_len, size_t nr,
		       size_t idx);

/**
 * Using the data from the sgl, calculates the checksums
 * for each extent. Will allocate memory for the struct daos_csum_info
 * structures and the memory buffer for the checksums with in the struct
 * daos_csum_info.
 * When the checksums are not needed anymore, daos_csummer_free_ic
 * should be called on each struct daos_csum_info
 *
 * @param[in]	obj		the daos_csummer object
 * @param[in]	sgls		Scatter Gather Lists with the data to be used
 *				for the extents \a recxs. The total data
 *				length of each  sgl should be the same as the
 *				sum of the lengths of all recxs. There should
 *				be 1 sgl for each iod.
 * @param[in]	iods		I/O descriptors describing the data in sgls.
 * @param[in]	nr		Number of iods and sgls as well as number of
 *				daos_iod_csums that will be created.
 * @param[in]	akey_only	Only calculate the checksum for the iod name
 * @param[in]	singv_los	Optional layout description for single values,
 *				as for erasure-coding single value possibly
 *				distributed to multiple targets. When it is NULL
 *				it means replica object, or EC object located
 *				in single target.
 * @param[in]	singv_idx	single value target index, valid when singv_los
 *				is non-NULL. -1 means calculating csum for all
 *				shards.
 * @param[out]	p_iods_csums	Pointer that will reference the structures
 *				to hold the csums described by the iods. In case
 *				of error, memory will be freed internally to
 *				this function.
 *
 * @return			0 for success, or an error code
 */
int
daos_csummer_calc_iods(struct daos_csummer *obj, d_sg_list_t *sgls,
		       daos_iod_t *iods, daos_iom_t *maps, uint32_t nr,
		       bool akey_only, struct dcs_layout *singv_los,
		       int singv_idx, struct dcs_iod_csums **p_iods_csums);

/**
 * Calculate a checksum for a daos key. Memory will be allocated for the
 * checksum info which must be freed by calling free_cis.
 *
 * @param[in]	csummer		the daos_csummer object
 * @param[in]	key		Key from which the checksum is derived
 * @param[out]	p_dcb		checksum buffer created. In case of error,
 *				memory will be freed internally to this function
 *
 * @return			0 for success, or an error code.
 */
int
daos_csummer_calc_key(struct daos_csummer *csummer, daos_key_t *key,
		      struct dcs_csum_info **p_csum);

/**
 * Using the data from the sgl, calculates the checksums for each extent and
 * then compare the calculated checksum with the checksum held in the iod to
 * verify the data is still valid. If a difference in checksum is found, an
 * error is returned.
 *
 * @param obj		the daos_csummer obj
 * @param iod(s)	The IOD that holds the already calculated checksums
 * @param sgl(s)	Scatter Gather List with the data to be used
 *			for the extents \a recxs. The total data
 *			length of the sgl should be the same as the sum
 *			of the lengths of all recxs
 * @param singv_lo	Optional layout description for single value,
 *			as for erasure-coding single value possibly
 *			distributed to multiple targets. When it is NULL
 *			it means replica object, or EC object located
 *			in single target.
 * @param singv_idx	single value target index, valid when singv_los
 *			is non-NULL. -1 means verifying csum for all shards.
 * @param iod_csum(s)	checksum of the iod
 *
 * @return		0 for success, -DER_CSUM if corruption is detected
 */
int
daos_csummer_verify_iods(struct daos_csummer *obj, daos_iod_t *iods, d_sg_list_t *sgls,
			 struct dcs_iod_csums *iods_csum, uint32_t nr,
			 struct dcs_layout *singv_lo, int singv_idx, daos_iom_t *map);

int
daos_csummer_verify_iod(struct daos_csummer *obj, daos_iod_t *iod,
			d_sg_list_t *sgl, struct dcs_iod_csums *iod_csum,
			struct dcs_layout *singv_lo, int singv_idx,
			daos_iom_t *map);

/**
 * Verify a key to a checksum
 *
 * @param obj		The daos_csummer obj
 * @param key		The key to verify
 * @param dcb		The dcs_csum_info that describes the checksum
 *
 * @return		0 for success, -DER_CSUM if corruption is detected
 */
int
daos_csummer_verify_key(struct daos_csummer *obj, daos_key_t *key,
			struct dcs_csum_info *csum);

/**
 * Calculate the needed memory for all the structures that will
 * store the checksums for the iods.
 *
 * @param[in]	obj		the daos_csummer object
 * @param[in]	iods		list of iods
 * @param[in]	nr		number of iods
 * @param[in]	akey_only	if true, don't include the csums for the data
 *				(useful on client side fetch when only akey
 *				csum is needed)
 * @param[in]	singv_los	Optional layout description for single values,
 *				as for erasure-coding single value possibly
 *				distributed to multiple targets. When it is NULL
 *				it means replica object, or EC object located
 *				in single target.
 *
 * @return			0 for success, or an error code
 */
uint64_t
daos_csummer_allocation_size(struct daos_csummer *obj, daos_iod_t *iods,
			     uint32_t nr, bool akey_only,
			     struct dcs_layout *singv_los);

/**
 * Allocate the checksum structures needed for the iods. This will also
 * setup the structures appropriately, so that everything is set except
 * the actual checksums
 *
 * @param[in]	obj		the daos_csummer obj
 * @param[in]	iods		list of iods
 * @param[in]	nr		number of iods
 * @param[in]	akey_only	Only calculate the checksum for the iod name
 * @param[in]	singv_los	Optional layout description for single values,
 *				as for erasure-coding single value possibly
 *				distributed to multiple targets. When it is NULL
 *				it means replica object, or EC object located
 *				in single target.
 * @param[out]	p_cds		pointer that will reference the
 *				the memory allocated
 * @return			number of iod_csums allocated, or
 *				negative if error
 */
int
daos_csummer_alloc_iods_csums(struct daos_csummer *obj, daos_iod_t *iods,
			      uint32_t nr, bool akey_only,
			      struct dcs_layout *singv_los,
			      struct dcs_iod_csums **p_cds);

/**
 * Using the information from the iods and csums packed into the csum_iov
 * allocate the memory for iod_csums structures and populate it appropriately
 *
 * @param iods_csums[out]		structure that will be allocated and
 *					populated
 * @param iods[in]			list of iod structures
 * @param iod_cnt[in]			Number of iods
 * @param csum_iov[in]			packed csum infos
 * @param csummer			csummer
 * @return
 */
int
daos_csummer_alloc_iods_csums_with_packed(struct daos_csummer *csummer,
					  daos_iod_t *iods, int iod_cnt,
					  d_iov_t *csum_iov,
					  struct dcs_iod_csums **iods_csums);

/** Destroy the iods csums */
void
daos_csummer_free_ic(struct daos_csummer *obj,
		     struct dcs_iod_csums **p_cds);

/** Destroy the csum infos allocated by daos_csummer_calc_key */
void
daos_csummer_free_ci(struct daos_csummer *obj,
		     struct dcs_csum_info **p_cis);

/**
 * -----------------------------------------------------------------------------
 * struct dcs_iod_csums Functions
 * -----------------------------------------------------------------------------
 */
 /** return a specific csum buffer from a specific iod csum info */
uint8_t *
ic_idx2csum(struct dcs_iod_csums *iod_csum, uint32_t iod_idx,
	    uint32_t csum_idx);

/**
 * -----------------------------------------------------------------------------
 * struct dcs_csum_info Functions
 * -----------------------------------------------------------------------------
 */

/** Setup the \a struct daos_csum_info with the checksum buffer,
 * buffer size, csum size, number of checksums, and the chunksize the
 * checksum represents
 */
void
ci_set(struct dcs_csum_info *csum_buf, void *buf, uint32_t csum_buf_size,
	uint16_t csum_size, uint32_t csum_count, uint32_t chunksize,
	uint16_t type);

/**
 * Setup the daos_csum_info with an existing daos_csum_info. This is not a copy,
 * meaning that the same csum buf from the source is used in the destination.
 */
void
ci_set_from_ci(struct dcs_csum_info *csum_buf, struct dcs_csum_info *csum2copy);

/** Set the csum buf to a NULL value */
void
ci_set_null(struct dcs_csum_info *csum_buf);

/** Is the \a struct daos_csum_info setup appropriately to be used */
bool
ci_is_valid(const struct dcs_csum_info *csum);

/** insert a csum into a dcb at a specific index */
void
ci_insert(struct dcs_csum_info *dcb, int idx, uint8_t *csum_buf, size_t len);

/** Returns the index of the checksum provided the offset into the data
 * the checksums are derived from.
 */
uint32_t
ci_off2idx(struct dcs_csum_info *csum_buf, uint32_t offset_bytes);

/** Returns the appropriate checksum given the index of the checksum. */
uint8_t *
ci_idx2csum(struct dcs_csum_info *csum_buf, uint32_t idx);

/** Returns the appropriate checksum given the data offset based on chunk
 *	information.
 */
uint8_t *
ci_off2csum(struct dcs_csum_info *csum_buf, uint32_t offset);

uint64_t
ci_buf2uint64(const uint8_t *buf, uint16_t len);

uint64_t
ci2csum(struct dcs_csum_info ci);


/**
 * return the number of bytes needed to serialize a dcs_csum_info into a
 * buffer
 */
#define	ci_size(obj) (sizeof(obj) + (obj).cs_nr * (obj).cs_len)

/** return the actual length for the csums stored in obj. Note that the buffer
 * (cs_buf_len) might be larger than the actual csums len
 */
#define	ci_csums_len(obj) ((obj).cs_nr * (obj).cs_len)

/** Serialze a \dcs_csum_info structure to an I/O vector. First the structure
* fields are added to the memory buf, then the actual csum.
*/
int
ci_serialize(struct dcs_csum_info *obj, d_iov_t *iov);
void
ci_cast(struct dcs_csum_info **obj, const d_iov_t *iov);

/**
 * A dcs_ci_list and associated functions manages the memory for storing a list of csum_infos.
 */
struct dcs_ci_list {
	uint8_t			*dcl_csum_infos;
	uint32_t		 dcl_csum_infos_nr;
	uint32_t		 dcl_buf_used;
	uint32_t		 dcl_buf_size;
	/* hack for supporting biov_csums_used usage in csum_add2iods */
	uint32_t		 dcl_csum_offset;
};

/**
 * Initialize memory for storing the csum_infos. The nr helps to allocate an amount of memory,
 * but depending on the number of checksums in each csum_info, the list may need to reallocate
 * more memory to store nr checksums. The reallocation happens internally and the caller doesn't
 * need to worry about that. dcs_csum_info_list_fini must be called when done so that the memory
 * allocated is freed.
 */
int dcs_csum_info_list_init(struct dcs_ci_list *list, uint32_t nr);
void dcs_csum_info_list_fini(struct dcs_ci_list *list);
int dcs_csum_info_save(struct dcs_ci_list *list, struct dcs_csum_info *info);
struct dcs_csum_info *dcs_csum_info_get(struct dcs_ci_list *list, uint32_t idx);

/**
 * change the iov so that buf points to the next csum_info, assuming the
 * current csum info's csum buf is right after it in the buffer.
 */
void
ci_move_next_iov(struct dcs_csum_info *obj, d_iov_t *iov);

/**
 * -----------------------------------------------------------------------------
 * Helper Functions
 * -----------------------------------------------------------------------------
 */

/** Gets the number checksums needed given a record extent.  */
uint32_t
daos_recx_calc_chunks(daos_recx_t extent, uint32_t record_size,
		      uint32_t chunk_size);


/** Helper function for dividing a range (lo-hi) into number of chunks, using
 * absolute alignment
 */
uint32_t
csum_chunk_count(uint32_t chunk_size, uint64_t lo_idx, uint64_t hi_idx,
		 uint64_t rec_size);

static inline bool
csum_iod_is_supported(daos_iod_t *iod)
{
	/**
	 * Needs to have something to actually checksum
	 * iod_size must be greater than 1 and have records if it's an array
	 */
	return iod->iod_size > 0 &&
	       !(iod->iod_type == DAOS_IOD_ARRAY && iod->iod_nr == 0);
}

/**
 * ----------------------------------------------------------------------
 * Chunk operations for alignment and getting boundaries
 * ----------------------------------------------------------------------
 */

/** Get the floor/ceiling of a specific chunk given the offset and chunksize */
daos_off_t
csum_chunk_align_floor(daos_off_t off, size_t chunksize);
daos_off_t
csum_chunk_align_ceiling(daos_off_t off, size_t chunksize);

/** get appropriate chunksize for the record size */
daos_off_t
csum_record_chunksize(daos_off_t default_chunksize, daos_off_t rec_size);

/** Represents a chunk, extent, or some calculated alignment for a range
 */
struct daos_csum_range {
	daos_off_t	dcr_lo; /** idx to first record in chunk */
	daos_off_t	dcr_hi; /** idx to last record in chunk */
	daos_size_t	dcr_nr; /** num of records in chunk  */
};

static inline void
dcr_set_idxs(struct daos_csum_range *range, daos_off_t lo, daos_off_t hi)
{
	range->dcr_lo = lo;
	range->dcr_hi = hi;
	range->dcr_nr = hi - lo + 1;
}

static inline void
dcr_set_idx_nr(struct daos_csum_range *range, daos_off_t lo, size_t nr)
{
	range->dcr_lo = lo;
	range->dcr_nr = nr;
	range->dcr_hi = lo + nr - 1;
}

/**
 * Given a recx, get chunk boundaries for a chunk index not exceeding the
 * recx
 */
struct daos_csum_range
csum_recx_chunkidx2range(daos_recx_t *recx, uint32_t rec_size,
			 uint32_t chunksize, uint64_t chunk_idx);

/**
 * get chunk boundaries for chunk with record offset \record_idx that doesn't
 * exceed lo/hi
 */
struct daos_csum_range
csum_recidx2range(size_t chunksize, daos_off_t record_idx, size_t lo_boundary,
		  daos_off_t hi_boundary, size_t rec_size);

/**
 * get chunk boundaries for chunk of index \chunk_idx.
 * boundaries must not exceed lo/hi
 */
struct daos_csum_range
csum_chunkidx2range(uint64_t rec_size, uint64_t chunksize, uint64_t chunk_idx,
		    uint64_t lo, uint64_t hi);

struct daos_csum_range
csum_chunkrange(uint64_t chunksize, uint64_t idx);

/**
 * will grow the selected range to align to chunk boundaries, not exceeding
 * lo/hi
 */
struct daos_csum_range
csum_align_boundaries(daos_off_t lo, daos_off_t hi, daos_off_t lo_boundary,
		      daos_off_t hi_boundary, daos_off_t record_size,
		      size_t chunksize);

/**
 * return start index and number of recxs within the map.recxs that have
 * data for the provided range.
 */
struct daos_csum_range
get_maps_idx_nr_for_range(struct daos_csum_range *req_range, daos_iom_t *map);

/**
 * DAOS Checksum Fault Injection ... corrupt data
 */
void
dcf_corrupt(d_sg_list_t *data, uint32_t nr);

#endif /** __DAOS_CHECKSUM_H */
