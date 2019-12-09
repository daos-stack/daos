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

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <isa-l.h>
#include <gurt/types.h>
#include <daos.h>
#include <daos/common.h>
#include <daos/checksum.h>


/** File function signatures */
static int
checksum_sgl_cb(uint8_t *buf, size_t len, void *args);

/** Container Property knowledge */
uint32_t
daos_cont_prop2csum(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_CSUM);

	return prop == NULL ? DAOS_PROP_CO_CSUM_OFF : (uint32_t)prop->dpe_val;
}

uint64_t
daos_cont_prop2chunksize(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_CSUM_CHUNK_SIZE);

	return prop == NULL ? 0 : prop->dpe_val;
}

bool
daos_cont_prop2serververify(daos_prop_t *props)
{
	struct daos_prop_entry *prop =
		daos_prop_entry_get(props, DAOS_PROP_CO_CSUM_SERVER_VERIFY);

	return prop == NULL ? false : prop->dpe_val == DAOS_PROP_CO_CSUM_SV_ON;
}

bool
daos_cont_csum_prop_is_valid(uint16_t val)
{
	if (daos_cont_csum_prop_is_enabled(val) || val == DAOS_PROP_CO_CSUM_OFF)
		return true;
	return false;
}

bool
daos_cont_csum_prop_is_enabled(uint16_t val)
{
	if (val != DAOS_PROP_CO_CSUM_CRC16 &&
	    val != DAOS_PROP_CO_CSUM_CRC32 &&
	    val != DAOS_PROP_CO_CSUM_CRC64)
		return false;
	return true;
}


enum DAOS_CSUM_TYPE
daos_contprop2csumtype(int contprop_csum_val)
{
	switch (contprop_csum_val) {
	case DAOS_PROP_CO_CSUM_CRC16:
		return CSUM_TYPE_ISAL_CRC16_T10DIF;
	case DAOS_PROP_CO_CSUM_CRC32:
		return CSUM_TYPE_ISAL_CRC32_ISCSI;
	case DAOS_PROP_CO_CSUM_CRC64:
		return CSUM_TYPE_ISAL_CRC64_REFL;
	case DAOS_PROP_CO_CSUM_SHA1:
	default:
		return CSUM_TYPE_UNKNOWN;
	}
}

/**
 * ---------------------------------------------------------------------------
 * Algorithms
 * ---------------------------------------------------------------------------
 */

/** CSUM_TYPE_ISAL_CRC16_T10DIF*/
static int
crc16_update(struct daos_csummer *obj, uint8_t *buf, size_t buf_len)
{
	uint16_t *crc16 = (uint16_t *)obj->dcs_csum_buf;
	*crc16 = crc16_t10dif(*crc16, buf, (int)buf_len);
	return 0;
}

struct csum_ft crc16_algo = {
	.cf_update = crc16_update,
	.cf_csum_len = sizeof(uint16_t),
	.cf_name = "crc16"
};

/** CSUM_TYPE_ISAL_CRC32_ISCSI */
static int
crc32_update(struct daos_csummer *obj, uint8_t *buf, size_t buf_len)
{
	uint32_t *crc32 = (uint32_t *) obj->dcs_csum_buf;

	*crc32 = crc32_iscsi(buf, (int) buf_len, *crc32);
	return 0;
}

struct csum_ft crc32_algo = {
	.cf_update = crc32_update,
	.cf_csum_len = sizeof(uint32_t),
	.cf_name = "crc32"
};

/** CSUM_TYPE_ISAL_CRC64_REFL */
static int
crc64_update(struct daos_csummer *obj, uint8_t *buf, size_t buf_len)
{
	uint64_t *csum = (uint64_t *)obj->dcs_csum_buf;

	*csum = crc64_ecma_refl(*csum, buf, buf_len);
	return 0;
}

struct csum_ft crc64_algo = {
	.cf_update = crc64_update,
	.cf_csum_len = sizeof(uint64_t),
	.cf_name = "crc64"
};

/** ------------------------------------------------------------- */
static char *csum_unknown_name = "unknown checksum type";

struct csum_ft *
daos_csum_type2algo(enum DAOS_CSUM_TYPE type)
{
	struct csum_ft *result = NULL;

	switch (type) {
	case CSUM_TYPE_ISAL_CRC16_T10DIF:
		result = &crc16_algo;
		break;
	case CSUM_TYPE_ISAL_CRC32_ISCSI:
		result = &crc32_algo;
		break;
	case CSUM_TYPE_ISAL_CRC64_REFL:
		result = &crc64_algo;
		break;
	case CSUM_TYPE_UNKNOWN:
	case CSUM_TYPE_END:
		break;
	}
	if (result && result->cf_type == CSUM_TYPE_UNKNOWN)
		result->cf_type = type;
	return result;
}

/**
 * struct daos_csummer functions
 */

int
daos_csummer_init(struct daos_csummer **obj, struct csum_ft *ft,
		      size_t chunk_bytes)
{
	struct daos_csummer	*result;
	int			 rc = 0;

	if (!ft)
		return -DER_INVAL;

	D_ALLOC(result, sizeof(*result));
	if (result == NULL)
		return -DER_NOMEM;

	result->dcs_algo = ft;
	result->dcs_chunk_size = chunk_bytes;

	if (result->dcs_algo->cf_init)
		rc = result->dcs_algo->cf_init(result);

	if (rc == 0)
		*obj = result;

	return rc;
}

int
daos_csummer_type_init(struct daos_csummer **obj, enum DAOS_CSUM_TYPE type,
		       size_t chunk_bytes)
{
	return daos_csummer_init(obj, daos_csum_type2algo(type), chunk_bytes);
}

void daos_csummer_destroy(struct daos_csummer **obj)
{
	struct daos_csummer *csummer = *obj;

	if (!*obj)
		return;

	if (csummer->dcs_algo->cf_destroy)
		csummer->dcs_algo->cf_destroy(csummer);
	D_FREE(csummer);
	*obj = NULL;
}

uint16_t
daos_csummer_get_csum_len(struct daos_csummer *obj)
{
	if (obj->dcs_algo->cf_get_size)
		return obj->dcs_algo->cf_get_size(obj);
	return obj->dcs_algo->cf_csum_len;
}

bool
daos_csummer_initialized(struct daos_csummer *obj)
{
	return obj != NULL && obj->dcs_algo != NULL;
}

uint16_t
daos_csummer_get_type(struct daos_csummer *obj)
{
	return obj->dcs_algo->cf_type;
}

uint32_t
daos_csummer_get_chunksize(struct daos_csummer *obj)
{
	return obj->dcs_chunk_size;
}

char *
daos_csummer_get_name(struct daos_csummer *obj)
{
	if (obj->dcs_algo->cf_name)
		return obj->dcs_algo->cf_name;
	return csum_unknown_name;
}

void
daos_csummer_set_buffer(struct daos_csummer *obj, uint8_t *buf,
			uint32_t buf_len)
{
	D_ASSERT(buf_len >= daos_csummer_get_csum_len(obj));

	obj->dcs_csum_buf = buf;
	obj->dcs_csum_buf_size = buf_len;
}

void
daos_csummer_reset(struct daos_csummer *obj)
{
	if (obj->dcs_algo->cf_reset)
		obj->dcs_algo->cf_reset(obj);
}

int
daos_csummer_update(struct daos_csummer *obj, uint8_t *buf, size_t buf_len)
{
	if (obj->dcs_csum_buf && obj->dcs_csum_buf_size > 0)
		return obj->dcs_algo->cf_update(obj, buf, buf_len);

	return 0;
}

int
daos_csummer_finish(struct daos_csummer *obj)
{
	if (obj->dcs_algo->cf_finish)
		return obj->dcs_algo->cf_finish(obj);
	return 0;
}

bool
daos_csummer_compare(struct daos_csummer *obj, daos_csum_buf_t *a,
		     daos_csum_buf_t *b)
{
	uint32_t a_len = a->cs_len * a->cs_nr;
	uint32_t b_len = b->cs_len * b->cs_nr;

	D_ASSERT(a->cs_type == b->cs_type);

	if (a_len != b_len)
		return false;
	return memcmp(a->cs_csum, b->cs_csum, a_len) == 0;
}

/**
 * Assign values for each daos_csum_buf_t using info from daos_csummer and
 * appropriate iod/recx
 */
static void
daos_csummer_set_dcbs(struct daos_csummer *obj, daos_csum_buf_t *dcbs,
		      uint32_t dcb_nr, daos_iod_t *iods, uint32_t iods_nr,
		      uint8_t *csum_buf, size_t csum_buf_len)
{
	uint32_t	chunksize;
	size_t		csum_len;
	uint8_t		*buf_end;
	uint32_t	iod_idx;
	uint32_t	recx_idx;
	uint32_t	cd_idx;

	chunksize	= daos_csummer_get_chunksize(obj);
	csum_len	= daos_csummer_get_csum_len(obj);
	buf_end		= csum_buf + csum_buf_len;

	iod_idx = recx_idx = 0;
	for (cd_idx = 0; cd_idx < dcb_nr; cd_idx++) {
		daos_csum_buf_t	*dcb;
		daos_iod_t	*iod;
		daos_recx_t	*recx;
		uint32_t	 csum_nr;
		size_t		 buf_len;

		dcb = &dcbs[cd_idx];
		iod = &iods[iod_idx];
		D_ASSERT(recx_idx < iod->iod_nr);
		recx = &iod->iod_recxs[recx_idx];

		csum_nr = daos_recx_calc_chunks(*recx,
						iod->iod_size,
						chunksize);

		buf_len = csum_len * csum_nr;

		dcb->cs_type = daos_csummer_get_type(obj);
		dcb->cs_chunksize = chunksize;
		dcb->cs_len = csum_len;

		dcb->cs_nr = csum_nr;
		dcb->cs_buf_len = buf_len;
		dcb->cs_csum = csum_buf;

		csum_buf += buf_len;
		D_ASSERT(csum_buf <= buf_end);

		/** go to the next recx/iod as needed */
		recx_idx++;
		if (recx_idx == iod->iod_nr) {
			recx_idx = 0;
			iod_idx++;
			D_ASSERT(iod_idx < iods_nr);
		}
	}
}

int
daos_csummer_alloc_dcbs(struct daos_csummer *obj,
			daos_iod_t *iods, uint32_t nr,
			daos_csum_buf_t **p_dcbs, uint32_t *p_dcbs_nr)
{
	daos_csum_buf_t *dcbs = NULL;
	uint8_t		*csum_buf;
	size_t		 csum_len;
	uint32_t	 alloc_size;
	size_t		 csum_buf_len;
	uint32_t	 total_dcb_nr = 0;
	uint32_t	 total_csum_nr = 0;

	int rc = 0;

	if (!(daos_csummer_initialized(obj)) || iods == NULL)
		goto done;

	csum_len = daos_csummer_get_csum_len(obj);
	daos_iods_count_needed_csum(iods, nr, obj->dcs_chunk_size,
				    &total_dcb_nr, &total_csum_nr);

	/** Allocate enough memory for the csum desc struct and
	 * the actual checksums
	 */
	csum_buf_len = csum_len * total_csum_nr;
	alloc_size = sizeof(daos_csum_buf_t) * total_dcb_nr +
		     csum_buf_len;
	D_ALLOC(dcbs, alloc_size);

	if (dcbs == NULL)
		D_GOTO(done, rc = -DER_NOMEM);

	/** initialize the daos_csum_buf_t's with the info in the iods */
	csum_buf = (uint8_t *) (dcbs + total_dcb_nr);
	daos_csummer_set_dcbs(obj, dcbs, total_dcb_nr, iods, nr, csum_buf,
			      csum_buf_len);

done:
	if (p_dcbs != NULL)
		*p_dcbs = dcbs;
	if (p_dcbs_nr != NULL)
		*p_dcbs_nr = total_dcb_nr;

	return rc;
}

void
daos_iods_link_dcbs(daos_iod_t *iods, uint32_t iods_nr, daos_csum_buf_t *dcbs,
		    uint32_t dcbs_nr)
{
	uint32_t iod_idx;
	uint32_t next_cd;

	if (dcbs_nr == 0)
		return;

	next_cd = 0;
	for (iod_idx = 0; iod_idx < iods_nr; iod_idx++) {
		iods[iod_idx].iod_csums = &dcbs[next_cd];
		next_cd += iods[iod_idx].iod_nr;
		D_ASSERT(next_cd <= dcbs_nr);
	}
}

void
daos_iods_unlink_dcbs(daos_iod_t *iods, uint32_t iods_nr)
{
	uint32_t iod_idx = 0;

	for (; iod_idx < iods_nr; iod_idx++)
		iods[iod_idx].iod_csums = NULL;
}

static int
calc_csum(struct daos_csummer *obj, d_sg_list_t *sgl,
	  size_t rec_len, daos_recx_t *recxs, size_t nr,
	  daos_csum_buf_t *csums)
{
	uint8_t			*buf;
	size_t			 bytes_for_csum;
	size_t			 csum_nr;
	uint64_t		 bytes;
	uint32_t		 chunk_size = daos_csummer_get_chunksize(obj);
	uint32_t		 i;
	uint32_t		 j;
	struct daos_sgl_idx	 idx = {0};
	int			 rc;

	if (!(daos_csummer_initialized(obj) && recxs && sgl))
		return 0;

	for (i = 0; i < nr; i++) { /** for each extent/checksum buf */
		csum_nr = daos_recx_calc_chunks(recxs[i], rec_len, chunk_size);
		bytes = recxs[i].rx_nr * rec_len;

		for (j = 0; j < csum_nr; j++) {
			buf = dcb_idx2csum(&csums[i], j);
			daos_csummer_set_buffer(obj, buf, csums->cs_len);
			daos_csummer_reset(obj);

			bytes_for_csum = MIN(chunk_size, bytes);
			rc = daos_sgl_processor(sgl, &idx, bytes_for_csum,
						checksum_sgl_cb, obj);
			if (rc)
				return rc;

			daos_csummer_finish(obj);
			bytes -= bytes_for_csum;
		}
	}

	return 0;
}

int
daos_csummer_calc(struct daos_csummer *obj, d_sg_list_t *sgl,
		  daos_iod_t *iod, daos_csum_buf_t **pcsum_bufs)
{
	int		rc = 0;
	uint32_t	csum_nr = 0;

	if (!(daos_csummer_initialized(obj) && iod && sgl))
		return 0;

	rc = daos_csummer_alloc_dcbs(obj, iod, 1, pcsum_bufs, &csum_nr);
	if (rc != 0)
		return rc;

	return calc_csum(obj, sgl, iod->iod_size,
			 iod->iod_recxs, iod->iod_nr,
			 *pcsum_bufs);
}

void
daos_csummer_free_dcbs(struct daos_csummer *obj,
		       daos_csum_buf_t **p_cds)
{
	if (!(daos_csummer_initialized(obj) && *p_cds))
		return;
	D_FREE((*p_cds));
}

int
daos_csummer_verify(struct daos_csummer *obj,
		    daos_iod_t *iod, d_sg_list_t *sgl)
{
	daos_csum_buf_t		*csum_bufs;
	int			 i;
	int			 rc;

	rc = daos_csummer_calc(obj, sgl, iod, &csum_bufs);
	if (rc != 0)
		return rc;

	for (i = 0; i < iod->iod_nr && rc == 0; i++) {
		bool match = daos_csummer_compare(obj, &csum_bufs[i],
						  &iod->iod_csums[i]);
		if (!match) {
			D_ERROR("Data corruption found\n");
			/** TODO: change rc to a new more appropriate
			 * error code
			 */
			rc = -DER_IO;
		}
	}

	daos_csummer_free_dcbs(obj, &csum_bufs);
	return rc;
}

/**
 * daos_csum_buf_t functions
 */
void
dcb_set(daos_csum_buf_t *csum_buf, void *buf,
	uint32_t csum_buf_size,
	uint16_t csum_size,
	uint32_t csum_count,
	uint32_t chunksize)
{
	csum_buf->cs_csum = buf;
	csum_buf->cs_len = csum_size;
	csum_buf->cs_buf_len = csum_buf_size;
	csum_buf->cs_nr = csum_count;
	csum_buf->cs_chunksize = chunksize;
}

void
dcb_set_null(daos_csum_buf_t *csum_buf)
{
	dcb_set(csum_buf, NULL, 0, 0, 0, 0);
}

bool
dcb_is_valid(const daos_csum_buf_t *csum)
{
	return csum != NULL &&
	       csum->cs_len > 0 &&
	       csum->cs_buf_len > 0 &&
	       csum->cs_csum != NULL &&
	       csum->cs_chunksize > 0 &&
	       csum->cs_nr > 0;
}

uint32_t
dcb_off2idx(daos_csum_buf_t *csum_buf, uint32_t offset_bytes)
{
	if (csum_buf->cs_chunksize == 0)
		return 0;
	return offset_bytes / csum_buf->cs_chunksize;
}

uint8_t *
dcb_idx2csum(daos_csum_buf_t *csum_buf, uint32_t idx)
{
	uint32_t offset = csum_buf->cs_len * idx;

	/** Make sure the buffer is big enough for the indexed checksum */
	if (csum_buf->cs_buf_len < offset + csum_buf->cs_len)
		return NULL;
	return csum_buf->cs_csum + offset;
}

uint8_t *
dcb_off2csum(daos_csum_buf_t *csum_buf, uint32_t offset)
{
	return dcb_idx2csum(csum_buf,
			    dcb_off2idx(csum_buf, offset));
}

/** Other Functions */

uint32_t
daos_recx_calc_chunks(daos_recx_t extent, uint32_t record_size,
		      uint32_t chunk_size)
{
	return csum_chunk_count(chunk_size,
				extent.rx_idx,
				extent.rx_idx + extent.rx_nr,
				record_size);
}


void
daos_iods_count_needed_csum(daos_iod_t *iods, int nr, int chunksize,
			    uint32_t *p_dcb_nr, uint32_t *p_csum_nr)
{
	uint32_t dcb_nr = 0;
	uint32_t csum_nr = 0;
	int i, j;

	for (i = 0; i < nr; i++) {
		dcb_nr += iods[i].iod_nr;

		for (j = 0; j < iods[i].iod_nr; j++) {
			csum_nr +=
				daos_recx_calc_chunks(iods[i].iod_recxs[j],
						      iods[i].iod_size,
						      chunksize);
		}
	}

	*p_dcb_nr = dcb_nr;
	*p_csum_nr = csum_nr;

}

uint32_t
csum_chunk_count(uint32_t chunk_size, uint64_t lo_idx, uint64_t hi_idx,
		 uint64_t rec_size)
{
	uint64_t	lo = lo_idx * rec_size;
	uint64_t	hi = hi_idx * rec_size;
	daos_off_t	width;
	uint64_t	unaligned_hi;

	if (chunk_size == 0)
		return 0;

	/** Align to chunk size */
	lo = lo - lo % chunk_size;
	unaligned_hi = hi % chunk_size;
	if (unaligned_hi)
		hi = hi + chunk_size - unaligned_hi;
	width = hi - lo;

	return (uint32_t)(width / chunk_size);
}

static int
checksum_sgl_cb(uint8_t *buf, size_t len, void *args)
{
	struct daos_csummer *obj = args;

	return daos_csummer_update(obj, buf, len);
}

int
daos_csum_check_sgl(daos_iod_t *iod, d_sg_list_t *sgl)
{
	struct daos_csummer	*csummer;
	daos_csum_buf_t		*csum = iod->iod_csums;
	int			 rc;

	if (!dcb_is_valid(csum))
		return 0;

	rc = daos_csummer_type_init(&csummer,
				    (enum DAOS_CSUM_TYPE) csum->cs_type,
				    csum->cs_chunksize);
	if (rc != 0) {
		D_ERROR("Issue initializing csummer. Unable to check data. "
			"Error: %d", rc);
		return -DER_MISC;
	}

	rc = daos_csummer_verify(csummer, iod, sgl);

	daos_csummer_destroy(&csummer);

	return rc;
}
