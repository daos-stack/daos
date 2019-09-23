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

	/** Len in bytes. Ft can either statically set csum_len or provide
	 *  a get_len function
	 */
	uint16_t csum_len;
	char *name;
	uint16_t type;
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
daos_csummer_get_size(struct daos_csummer *obj);

/** Determine if the checksums is configured. */
bool
daos_csummer_initialized(struct daos_csummer *obj);

/** Get an integer representing the csum type the csummer is configured with */
uint16_t
daos_csummer_get_type(struct daos_csummer *obj);

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
daos_csummer_compare(struct daos_csummer *obj, daos_csum_buf_t *a,
		     daos_csum_buf_t *b);

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
 * @param[in]	rec_len		number of bytes for each record
 * @param[in]	recxs		record extents
 * @param[in]	nr		number of record extents and number of
 *				csum_arrays that will be allocated
 * @param[out]	pcsum_bufs	csum_bufs for the checksums created for each
 *				extent
 *
 * @return			0 for success, or an error code
 */
int
daos_csummer_calc_csum(struct daos_csummer *obj, d_sg_list_t *sgl,
		       size_t rec_len, daos_recx_t *recxs, size_t nr,
		       daos_csum_buf_t **pcsum_bufs);

/**
 * Allocate memory for the daos_csum_buf_t structures and the memory buffer for
 * each csum within the structure. Will initialize the daos_csum_buf_t to
 * appropriate values.
 * @param[in]	obj		the daos_csummer object
 * @param[in]	rec_len		record length
 * @param[in]	nr		number of record extents
 * @param[in]	recxs		record extents
 * @param[out]	pcsum_bufs	csum_bufs for the checksums created for each
 *				extent
 *
 * @return			0 for success, or an error code
 */
int
daos_csummer_prep_csum_buf(struct daos_csummer *obj, uint32_t rec_len,
			   uint32_t nr, daos_recx_t *recxs,
			   daos_csum_buf_t **pcsum_bufs);

/** Destroy the csum buf and memory allocated for checksums */
void
daos_csummer_destroy_csum_buf(struct daos_csummer *obj, size_t nr,
			      daos_csum_buf_t **pcsum_bufs);

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
daos_csum_check_sgl(const daos_iod_t *iod, d_sg_list_t *sgl);

#endif /** __DAOS_CHECKSUM_H */

