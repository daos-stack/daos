/**
 * (C) Copyright 2019 Intel Corporation.
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

#ifndef __DAOS_CHECKSUM_H
#define __DAOS_CHECKSUM_H

#include <daos_types.h>
#include <daos_obj.h>
#include <daos_prop.h>

/**
 * -----------------------------------------------------------
 * Container Property Knowledge
 * -----------------------------------------------------------
 */
uint32_t
daos_cont_prop2csum(daos_prop_t *props);

uint64_t
daos_cont_prop2chunksize(daos_prop_t *props);

bool
daos_cont_prop2serververify(daos_prop_t *props);

bool
daos_cont_csum_prop_is_valid(uint16_t val);

bool
daos_cont_csum_prop_is_enabled(uint16_t val);

/**
 * -----------------------------------------------------------
 * DAOS Checksummer
 * -----------------------------------------------------------
 */
/** Type of checksums DAOS supports. Primarily used for looking up the
 * appropriate algorithm functions to be used for the csummer
 */
enum DAOS_CSUM_TYPE {
	CSUM_TYPE_UNKNOWN = 0,

	CSUM_TYPE_ISAL_CRC16_T10DIF = 1,
	CSUM_TYPE_ISAL_CRC32_ISCSI = 2,
	CSUM_TYPE_ISAL_CRC64_REFL = 3,

	CSUM_TYPE_END = 4,
};

/** Lookup the appropriate CSUM_TYPE given daos container property */
enum DAOS_CSUM_TYPE daos_contprop2csumtype(int contprop_csum_val);

struct csum_ft;
struct daos_csummer {
	/** Size of csum_buf. */
	uint32_t dcs_csum_buf_size;
	/** Cached configuration for chunk size*/
	uint32_t dcs_chunk_size;
	/** Pointer to the function table to be used for calculating csums */
	struct csum_ft *dcs_algo;
	/** Pointer to function table specific contexts */
	void *dcs_ctx;
	/** Points to the buffer where the  calculated csum is to be written */
	uint8_t *dcs_csum_buf;
};

struct csum_ft {
	int		(*cf_init)(struct daos_csummer *obj);
	void		(*cf_destroy)(struct daos_csummer *obj);
	int		(*cf_finish)(struct daos_csummer *obj);
	int		(*cf_update)(struct daos_csummer *obj,
				     uint8_t *buf, size_t buf_len);
	void		(*cf_reset)(struct daos_csummer *obj);
	void		(*cf_get)(struct daos_csummer *obj);
	uint16_t	(*cf_get_size)(struct daos_csummer *obj);
	bool		(*cf_compare)(struct daos_csummer *obj,
				      uint8_t *buf1, uint8_t *buf2,
				      size_t buf_len);

	/** Len in bytes. Ft can either statically set csum_len or provide
	 *  a get_len function
	 */
	uint16_t	 cf_csum_len;
	char		*cf_name;
	uint16_t	 cf_type;
};

struct csum_ft *
daos_csum_type2algo(enum DAOS_CSUM_TYPE type);

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
 *
 * @return		0 for success, or an error code
 */
int
daos_csummer_init(struct daos_csummer **obj, struct csum_ft *ft,
		  size_t chunk_bytes);

/**
 * Initialize the daos_csummer with a known DAOS_CSUM_TYPE
 *
 * @param obj		daos_csummer to be initialized. Memory will be allocated
 *			for it.
 * @param type		Type of the checksum algorithm that will be used
 * @param chunk_bytes	Chunksize, typically from the container configuration
 *
 * @return		0 for success, or an error code
 */
int
daos_csummer_type_init(struct daos_csummer **obj, enum DAOS_CSUM_TYPE type,
		  size_t chunk_bytes);

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

/** Get a string representing the csum the csummer is configured with */
char *
daos_csummer_get_name(struct daos_csummer *obj);

/** Set the csum buffer where the calculated checksumm will be written to */
void
daos_csummer_set_buffer(struct daos_csummer *obj, uint8_t *buf,
			uint32_t buf_len);

/** Reset the csummer */
void
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
daos_csummer_compare_dcb(struct daos_csummer *obj, daos_csum_buf_t *a,
			 daos_csum_buf_t *b);

bool
daos_csummer_csum_compare(struct daos_csummer *obj, uint8_t *a,
			  uint8_t *b, uint32_t csum_len);

/**
 * Using the data from the sgl, calculates the checksums
 * for each extent. Will allocate memory for the daos_csum_buf_t structures and
 * the memory buffer for the checksums with in the daos_csum_buf_t.
 * When the checksums are not needed anymore, daos_csummer_destroy_csum_buf
 * should be called on each daos_csum_buf_t
 *
 * @param[in]	obj		the daos_csummer object
 * @param[in]	sgl		Scatter Gather List with the data to be used
 *				for the extents \a recxs. The total data
 *				length of the sgl should be the same as the sum
 *				of the lengths of all recxs
 * @param[in]	iod		I/O descriptor describing the data in sgl.
 *				Note: While iod has daos_csum_buf_t's, they
 *				are not updated in this function. The
 *				calculated checksums will be put into csum_bufs
 * @param[out]	pcsum_bufs	csum_bufs for the checksums created for each
 *				extent
 *
 * @return			0 for success, or an error code
 */
int
daos_csummer_calc(struct daos_csummer *obj, d_sg_list_t *sgl,
		  daos_iod_t *iod, daos_csum_buf_t **pcsum_bufs);

/**
 * Using the data from the sgl, calculates the checksums for each extent and
 * then compare the calculated checksum with the checksum held in the iod to
 * verify the data is still valid. If a difference in checksum is found, an
 * error is returned.
 *
 * @param obj		the daos_csummer obj
 * @param iod		The IOD that holds the already calculated checksums
 * @param sgl		Scatter Gather List with the data to be used
 *			for the extents \a recxs. The total data
 *			length of the sgl should be the same as the sum
 *			of the lengths of all recxs
 *
 * @return		0 for success, -DER_IO if corruption is detected
 */
int
daos_csummer_verify(struct daos_csummer *obj,
		    daos_iod_t *iod, d_sg_list_t *sgl);

/**
 * Allocate memory for a list of  daos_csum_buf_t structures and the
 * memory buffer for csum within the structure. Based on info in IOD. Will
 * also initialize the daos_csum_buf_t to appropriate values.
 * daos_csummer_free_csum_buf should be called when done with the
 * daos_csum_buf_t's
 *
 * @param[in]	obj		the daos_csummer object
 * @param[in]	iods		list of iods used as reference to determine
 *				num of csum desc to create
 * @param[in]	nr		number of iods
 * @param[out]	p_csum		csum_bufs for the checksums created for each.
 *				on error, p_csum will not be allocated.
 * @param[out]	p_dcbs_nr	number of csum descs created
 *
 * @return			0 for success, or an error code
 */
int
daos_csummer_alloc_dcbs(struct daos_csummer *obj,
			daos_iod_t *iods, uint32_t nr,
			daos_csum_buf_t **p_dcbs, uint32_t *p_dcbs_nr);

/** Destroy the csum buf and memory allocated for checksums */
void
daos_csummer_free_dcbs(struct daos_csummer *obj,
		       daos_csum_buf_t **p_cds);

/**
 * -----------------------------------------------------------------------------
 * daos_csum_buf_t Functions
 * -----------------------------------------------------------------------------
 */

/** Setup the \a daos_csum_buf_t with the checksum buffer,
 * buffer size, csum size, number of checksums, and the chunksize the
 * checksum represents
 */
void
dcb_set(daos_csum_buf_t *csum_buf, void *buf,
	uint32_t csum_buf_size,
	uint16_t csum_size,
	uint32_t csum_count,
	uint32_t chunksize);

/** Set the csum buf to a NULL value */
void
dcb_set_null(daos_csum_buf_t *csum_buf);

/** Is the \a daos_csum_buf_t setup appropriately to be used */
bool
dcb_is_valid(const daos_csum_buf_t *csum);

/** insert a csum into a dcb at a specific index */
void
dcb_insert(daos_csum_buf_t *dcb, int idx, uint8_t *csum_buf, size_t len);

/** Returns the index of the checksum provided the offset into the data
 * the checksums are derived from.
 */
uint32_t
dcb_off2idx(daos_csum_buf_t *csum_buf, uint32_t offset_bytes);

/** Returns the appropriate checksum given the index of the checksum. */
uint8_t *
dcb_idx2csum(daos_csum_buf_t *csum_buf, uint32_t idx);

/** Returns the appropriate checksum given the data offset based on chunk
 *	information.
 */
uint8_t *
dcb_off2csum(daos_csum_buf_t *csum_buf, uint32_t offset);

/** Gets the number checksums needed given a record extent.  */
uint32_t
daos_recx_calc_chunks(daos_recx_t extent, uint32_t record_size,
		      uint32_t chunk_size);

/**
 * A helper function to count the needed number of daos_csum_buf_t's and total
 * checksums all will have. This is useful for when allocating memory for
 * a collection of iods.
 *
 * @param iods[in]		list of iods
 * @param nr[in]		number of iods
 * @param chunksize[in]		chunk size is used to determine num
 *				of csums needed
 * @param p_dcb_nr[out]		number of daos_csum_buf_t's
 * @param p_csum_nr[out]	number of total checksums for all p_dcb_nr
 */
void
daos_iods_count_needed_csum(daos_iod_t *iods, int nr, int chunksize,
			    uint32_t *p_dcb_nr, uint32_t *p_csum_nr);

/**
 * take a list of daos_csum_buf_ts and assign to iods.csum, making sure each
 * iod has the appropriate number of daos_csum_buf_t for an iod's recxs.
 *
 * @param iods		list of iods
 * @param iods_nr	number of iods
 * @param dcbs		list of dcbs - there should be one dcb for each recx in
 *			all the iods
 * @param dcbs_nr	number of dcbs
 */
void
daos_iods_link_dcbs(daos_iod_t *iods, uint32_t iods_nr, daos_csum_buf_t *dcbs,
		    uint32_t dcbs_nr);

/**
 * Remove the daos_csum_buf_t's from the iods after they're not needed anymore.
 * This would be the case when the data has been verified with the checksums
 * on the client. It should be done in conjunction with freeing the
 * daos_csum_buf_t's resources.
 *
 * @param iods
 * @param iods_nr
 */
void
daos_iods_unlink_dcbs(daos_iod_t *iods, uint32_t iods_nr);


/** Helper function for dividing a range (lo-hi) into number of chunks, using
 * absolute alignment
 */
uint32_t
csum_chunk_count(uint32_t chunk_size, uint64_t lo_idx, uint64_t hi_idx,
		 uint64_t rec_size);

/**
 * A facade for verifying data (represented by an sgl) is not corrupt. Uses the
 * checksums stored in the iod to compare against calculated checksums.
 *
 * @param iod	I/O Descriptor which contains checksums
 * @param sgl	Scatter Gather List pointing to the data to be verified
 *
 * @return	0 for success, -DER_IO if corruption is detected
 */
int
daos_csum_check_sgl(daos_iod_t *iod, d_sg_list_t *sgl);

static inline bool
csum_iod_is_supported(uint64_t chunksize, daos_iod_t *iod)
{
	/** Only support ARRAY Type currently */
	return iod->iod_type == DAOS_IOD_ARRAY &&
	       iod->iod_size > 0 &&
	       iod->iod_size <= chunksize;
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

#endif /** __DAOS_CHECKSUM_H */

