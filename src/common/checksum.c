/**
 * (C) Copyright 2019-2020 Intel Corporation.
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

#define D_LOGFAC	DD_FAC(csum)

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <isa-l.h>
#include <gurt/types.h>
#include <daos/common.h>
#include <daos/checksum.h>

#define C_TRACE(...) D_DEBUG(DB_CSUM, __VA_ARGS__)
#define C_TRACE_ENABLED() D_LOG_ENABLED(DB_TRACE)

/** File function signatures */
static int
checksum_sgl_cb(uint8_t *buf, size_t len, void *args);

/** helper function to trace bytes */
static void
trace_bytes(const uint8_t *buf, const size_t len, const size_t max)
{
	size_t	char_buf_len = max == 0 ? len : min(max, len);
	char	char_buf[char_buf_len * 2 + 1];
	int	i;

	if (buf == NULL)
		return;

	for (i = 0; i <  len && (i < max || max == 0); i++)
		sprintf(char_buf + i * 2, "%x", buf[i]);
	char_buf[i] = '\0';
	C_TRACE("%s\n", char_buf);
}

/** helper function to trace chars */
static void
trace_chars(const uint8_t *buf, const size_t buf_len, const uint32_t max)
{
	int	i;
	size_t	len = max == 0 ? buf_len : min(buf_len, max);
	char	str[len + 1];

	if (buf == NULL)
		return;

	for (i = 0; i <  len ; i++)
		str[i] = buf[i] == '\0' ? '_' : buf[i];
	str[i] = '\0';
	C_TRACE("%s", str);
}

static void
daos_csummer_trace_csum(struct daos_csummer *obj, uint8_t *csum)
{
	trace_bytes(csum, daos_csummer_get_csum_len(obj), 0);
}

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
	if (daos_csummer_initialized(obj))
		return obj->dcs_algo->cf_type;
	return 0;
}

uint32_t
daos_csummer_get_chunksize(struct daos_csummer *obj)
{
	if (daos_csummer_initialized(obj))
		return obj->dcs_chunk_size;
	return 0;
}

char *
daos_csummer_get_name(struct daos_csummer *obj)
{

	if (daos_csummer_initialized(obj) && obj->dcs_algo->cf_name)
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
	int rc = 0;

	if (C_TRACE_ENABLED()) {
		C_TRACE("Buffer (len=%lu):\n", buf_len);
		trace_chars(buf, buf_len, 50);
		C_TRACE("\n");
	}

	if (obj->dcs_csum_buf && obj->dcs_csum_buf_size > 0) {
		rc = obj->dcs_algo->cf_update(obj, buf, buf_len);
		/* Corrupt data after calculating checksum */
		if (DAOS_FAIL_CHECK(DAOS_CHECKSUM_CDATA_CORRUPT))
			buf[0] += 0x2;
	}


	if (C_TRACE_ENABLED()) {
		C_TRACE("CSUM: ...");
		daos_csummer_trace_csum(obj, obj->dcs_csum_buf);
	}

	return rc;
}

int
daos_csummer_finish(struct daos_csummer *obj)
{
	if (obj->dcs_algo->cf_finish)
		return obj->dcs_algo->cf_finish(obj);
	return 0;
}

bool
daos_csummer_compare_csum_info(struct daos_csummer *obj,
			       struct dcs_csum_info *a,
			       struct dcs_csum_info *b)
{
	uint32_t	a_len = a->cs_len * a->cs_nr;
	uint32_t	b_len = b->cs_len * b->cs_nr;
	bool		match = true;
	int		i;

	D_ASSERT(a->cs_type == b->cs_type);

	if (a_len != b_len)
		return false;

	for (i = 0; i < a->cs_nr && match; i++) {
		match = daos_csummer_csum_compare(obj, ci_idx2csum(a, i),
						  ci_idx2csum(b, i),
						  a->cs_len);
	}

	return match;
}

bool
daos_csummer_csum_compare(struct daos_csummer *obj, uint8_t *a,
			  uint8_t *b, uint32_t csum_len)
{
	if (C_TRACE_ENABLED()) {
		C_TRACE("Comparing: ");
		daos_csummer_trace_csum(obj, a);
		daos_csummer_trace_csum(obj, b);
	}

	if (obj->dcs_algo->cf_compare)
		return obj->dcs_algo->cf_compare(obj, a, b, csum_len);

	return memcmp(a, b, csum_len) == 0;
}

uint64_t
daos_csummer_allocation_size(struct daos_csummer *obj, daos_iod_t *iods,
			     uint32_t nr)
{
	int		i, j;
	uint64_t	result = 0;
	uint32_t	chunksize = daos_csummer_get_chunksize(obj);
	uint16_t	csum_size = daos_csummer_get_csum_len(obj);

	for (i = 0; i < nr; i++) {
		daos_iod_t *iod = &iods[i];

		result += sizeof(struct dcs_iod_csums);
		if (!csum_iod_is_supported(chunksize, iod))
			continue;

		for (j = 0; j < iod->iod_nr; j++) {
			daos_recx_t	*recx = &iod->iod_recxs[j];
			uint32_t	 csum_count;

			csum_count = daos_recx_calc_chunks(*recx,
							   iod->iod_size,
							   chunksize);
			result += sizeof(struct dcs_csum_info) +
				  csum_count * csum_size;
		}
	}

	return result;
}

#define	setptr(ptr, buf, len, used, total_len) do {\
	D_ASSERT(used + len <= total_len);\
	ptr = (__typeof__(ptr))buf; \
	buf += (len); \
	used += len; } while (0)

int
daos_csummer_alloc_iods_csums(struct daos_csummer *obj,
			      daos_iod_t *iods, uint32_t nr,
			      struct dcs_iod_csums **p_iods_csums)
{
	int			 i, j, rc = 0;
	uint32_t		 chunksize;
	uint16_t		 csum_size;
	uint16_t		 csum_type;
	struct dcs_iod_csums	*iods_csums = NULL;
	uint8_t			*buf;
	uint64_t		 buf_len;
	uint64_t		 used = 0;

	if (!(daos_csummer_initialized(obj)) || iods == NULL || nr == 0)
		goto done;

	rc = nr;

	chunksize = daos_csummer_get_chunksize(obj);
	csum_size = daos_csummer_get_csum_len(obj);
	csum_type = daos_csummer_get_type(obj);

	/**
	 * allocate enough memory for all iod checksums at once, then update
	 * pointers appropriately
	 */
	buf_len = daos_csummer_allocation_size(obj, iods, nr);
	D_ALLOC(buf, buf_len);
	if (buf == NULL)
		return -DER_NOMEM;

	setptr(iods_csums, buf, sizeof(*iods_csums) * nr, used, buf_len);

	for (i = 0; i < nr; i++) {
		daos_iod_t		*iod = &iods[i];
		struct dcs_iod_csums	*iod_csum = &iods_csums[i];

		if (!csum_iod_is_supported(chunksize, iod))
			continue;

		setptr(iod_csum->ic_data, buf,
		       sizeof(*iod_csum->ic_data) * iod->iod_nr,
		       used, buf_len);

		for (j = 0; j < iod->iod_nr; j++) {
			daos_recx_t		*recx;
			struct dcs_csum_info	*recx_csum;
			uint32_t		 csum_count;

			recx = &iod->iod_recxs[j];
			recx_csum = &iod_csum->ic_data[j];
			csum_count = daos_recx_calc_chunks(*recx,
							   iod->iod_size,
							   chunksize);

			/**
			 * set buffer to null first, then set it using
			 * the setptr macro so that amount of memory
			 * used from allocated buffer is tracked.
			 */
			ci_set(recx_csum, NULL, csum_count * csum_size,
			       csum_size, csum_count, chunksize, csum_type);
			setptr(recx_csum->cs_csum, buf, csum_count * csum_size,
			       used, buf_len);
		}
	}

done:
	if (p_iods_csums != NULL)
		*p_iods_csums = iods_csums;

	return rc;
}


static int
calc_csum(struct daos_csummer *obj, d_sg_list_t *sgl,
	  size_t rec_len, daos_recx_t *recxs, size_t nr,
	  struct dcs_csum_info *csums)
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
		C_TRACE("csum_nr: %lu\n", csum_nr);
		bytes = recxs[i].rx_nr * rec_len;

		for (j = 0; j < csum_nr; j++) {
			struct daos_csum_range chunk = csum_recx_chunkidx2range(
				&recxs[i], rec_len, chunk_size, j);

			buf = ci_idx2csum(&csums[i], j);
			daos_csummer_set_buffer(obj, buf, csums->cs_len);
			daos_csummer_reset(obj);

			bytes_for_csum = chunk.dcr_nr * rec_len;
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
daos_csummer_calc_iods(struct daos_csummer *obj, d_sg_list_t *sgls,
		  daos_iod_t *iods, uint32_t nr,
		  struct dcs_iod_csums **p_iods_csums)
{
	int			 rc = 0;
	int			 i;
	struct dcs_iod_csums	*iods_csums = NULL;
	uint32_t		 iods_csums_nr;
	uint32_t		 chunksize = daos_csummer_get_chunksize(obj);

	if (!daos_csummer_initialized(obj))
		return 0;

	*p_iods_csums = NULL;

	rc = daos_csummer_alloc_iods_csums(obj, iods, nr, &iods_csums);

	if (rc < 0)
		return rc;

	iods_csums_nr = (uint32_t) rc;

	for (i = 0; i < iods_csums_nr; i++) {
		daos_iod_t		*iod = &iods[i];
		struct dcs_iod_csums	*csums = &iods_csums[i];

		if (!csum_iod_is_supported(chunksize, iod))
			continue;

		rc = calc_csum(obj, &sgls[i], iod->iod_size,
			       iod->iod_recxs, iod->iod_nr,
			       csums->ic_data);
		csums->ic_nr = iod->iod_nr;

		if (rc != 0) {
			daos_csummer_free_ic(obj, &iods_csums);
			return rc;
		}
	}
	*p_iods_csums = iods_csums;

	return 0;

}

void
daos_csummer_free_ic(struct daos_csummer *obj,
		     struct dcs_iod_csums **p_cds)
{
	if (!(daos_csummer_initialized(obj) && *p_cds))
		return;
	D_FREE((*p_cds));
}

int
daos_csummer_verify(struct daos_csummer *obj, daos_iod_t *iod, d_sg_list_t *sgl,
		    struct dcs_iod_csums *iod_csums)
{
	struct dcs_iod_csums	*new_iod_csums;
	int			 i;
	int			 rc;
	bool			 match;

	rc = daos_csummer_calc_iods(obj, sgl, iod, 1, &new_iod_csums);
	if (rc != 0)
		return rc;

	for (i = 0; i < iod->iod_nr; i++) {
		match = daos_csummer_compare_csum_info(obj,
				&new_iod_csums->ic_data[i],
				&iod_csums->ic_data[i]);
		if (!match) {
			D_ERROR("Data corruption found\n");
			D_GOTO(done, rc = -DER_CSUM);
		}
	}

done:
	daos_csummer_free_ic(obj, &new_iod_csums);

	return rc;
}

/**
 * -----------------------------------------------------------------------------
 * struct dcs_iod_csums Functions
 * -----------------------------------------------------------------------------
 */

uint8_t *
ic_idx2csum(struct dcs_iod_csums *iod_csum, uint32_t iod_idx,
	    uint32_t csum_idx)
{
	struct dcs_csum_info *csum_info = &iod_csum->ic_data[iod_idx];
	uint32_t offset = csum_info->cs_len * csum_idx;

	/** Make sure the buffer is big enough for the indexed checksum */
	if (csum_info->cs_buf_len < offset + csum_info->cs_len)
		return NULL;
	return csum_info->cs_csum + offset;
}


/**
 * struct daos_csum_info functions
 */
void
ci_set(struct dcs_csum_info *csum_buf, void *buf, uint32_t csum_buf_size,
	uint16_t csum_size, uint32_t csum_count, uint32_t chunksize,
	uint16_t type)
{
	csum_buf->cs_csum = buf;
	csum_buf->cs_len = csum_size;
	csum_buf->cs_buf_len = csum_buf_size;
	csum_buf->cs_nr = csum_count;
	csum_buf->cs_chunksize = chunksize;
	csum_buf->cs_type = type;
}

void
ci_set_null(struct dcs_csum_info *csum_buf)
{
	ci_set(csum_buf, NULL, 0, 0, 0, 0, 0);
}

bool
ci_is_valid(const struct dcs_csum_info *csum)
{
	return csum != NULL &&
	       csum->cs_len > 0 &&
	       csum->cs_buf_len > 0 &&
	       csum->cs_csum != NULL &&
	       csum->cs_chunksize > 0 &&
	       csum->cs_nr > 0;
}

void
ci_insert(struct dcs_csum_info *dcb, int idx, uint8_t *csum_buf, size_t len)
{
	uint8_t *to_update;

	D_ASSERT(idx < dcb->cs_nr);

	to_update = dcb->cs_csum + idx * dcb->cs_len;
	memcpy(to_update, csum_buf, len);
}

uint32_t
ci_off2idx(struct dcs_csum_info *csum_buf, uint32_t offset_bytes)
{
	if (csum_buf->cs_chunksize == 0)
		return 0;
	return offset_bytes / csum_buf->cs_chunksize;
}

uint8_t *
ci_idx2csum(struct dcs_csum_info *csum_buf, uint32_t idx)
{
	uint32_t offset = csum_buf->cs_len * idx;

	/** Make sure the buffer is big enough for the indexed checksum */
	if (csum_buf->cs_buf_len < offset + csum_buf->cs_len)
		return NULL;
	return csum_buf->cs_csum + offset;
}

uint8_t *
ci_off2csum(struct dcs_csum_info *csum_buf, uint32_t offset)
{
	return ci_idx2csum(csum_buf,
			   ci_off2idx(csum_buf, offset));
}

/** Other Functions */
uint32_t
daos_recx_calc_chunks(daos_recx_t extent, uint32_t record_size,
		      uint32_t chunk_size)
{
	if (extent.rx_nr == 0)
		return 0;

	return csum_chunk_count(chunk_size,
				extent.rx_idx,
				extent.rx_idx + extent.rx_nr - 1,
				record_size);
}

daos_off_t
csum_chunk_align_floor(daos_off_t off, size_t chunksize)
{
	D_ASSERT(chunksize != 0);
	return off - (off % chunksize);
}
daos_off_t
csum_chunk_align_ceiling(daos_off_t off, size_t chunksize)
{
	daos_off_t lo = csum_chunk_align_floor(off, chunksize);
	daos_off_t hi = (lo + chunksize) - 1;

	if (hi < lo) /** overflow */
		hi = UINT64_MAX;
	return hi;
}

struct daos_csum_range
csum_recidx2range(size_t chunksize, daos_off_t record_idx, size_t lo_boundary,
		  daos_off_t hi_boundary, size_t rec_size)
{
	struct daos_csum_range	result = {0};
	daos_off_t		lo;
	daos_off_t		hi;

	if (record_idx > hi_boundary)
		return result;

	chunksize /= rec_size;
	lo = csum_chunk_align_floor(record_idx, chunksize);
	hi = csum_chunk_align_ceiling(record_idx, chunksize);

	result.dcr_lo = max(lo, lo_boundary);
	result.dcr_hi = min(hi, hi_boundary);

	result.dcr_nr = result.dcr_hi - result.dcr_lo + 1;

	return result;
}

struct daos_csum_range
csum_chunkidx2range(uint64_t rec_size, uint64_t chunksize, uint64_t chunk_idx,
		    uint64_t lo, uint64_t hi)
{
	uint64_t record_idx = csum_chunk_align_floor(lo, chunksize / rec_size)
			      + chunk_idx * (chunksize / rec_size);

	return csum_recidx2range(chunksize, record_idx, lo, hi, rec_size);
}

struct daos_csum_range
csum_chunkrange(uint64_t chunksize, uint64_t idx)
{
	struct daos_csum_range	result;

	dcr_set_idx_nr(&result, idx * chunksize, chunksize);

	return result;
}

struct daos_csum_range
csum_align_boundaries(daos_off_t lo, daos_off_t hi,
		      daos_off_t lo_boundary, daos_off_t hi_boundary,
		      daos_off_t record_size, size_t chunksize)

{
	struct daos_csum_range	result = {0};
	size_t			chunksize_records;
	daos_off_t		lo_aligned;
	daos_off_t		hi_aligned;
	uint64_t		overflow_gaurd;

	if (lo < lo_boundary || hi > hi_boundary)
		return result;

	/** Calculate alignment based on records ... otherwise if ex_hi is
	 * UINT64_MAX the calculations would wrap and be incorrect
	 */
	chunksize_records = chunksize / record_size;
	lo_aligned = csum_chunk_align_floor(lo, chunksize_records);
	hi_aligned = csum_chunk_align_ceiling(hi, chunksize_records);

	result.dcr_lo = max(lo_boundary, lo_aligned);
	result.dcr_hi = min(hi_boundary, hi_aligned);
	overflow_gaurd = result.dcr_hi + 1;
	if (overflow_gaurd < result.dcr_hi)
		overflow_gaurd = UINT64_MAX;
	result.dcr_nr = overflow_gaurd - result.dcr_lo;

	return result;
}

struct daos_csum_range
csum_recx_chunkidx2range(daos_recx_t *recx, uint32_t rec_size,
			 uint32_t chunksize, uint64_t chunk_idx)
{
	return csum_chunkidx2range(rec_size, chunksize, chunk_idx, recx->rx_idx,
				   recx->rx_idx + recx->rx_nr - 1);
}

uint32_t
csum_chunk_count(uint32_t chunk_size, uint64_t lo_idx, uint64_t hi_idx,
		 uint64_t rec_size)
{
	struct daos_csum_range chunk;

	if (rec_size == 0)
		return 0;
	chunk = csum_align_boundaries(lo_idx, hi_idx, 0, UINT64_MAX,
				      rec_size, chunk_size);
	daos_size_t result = chunk.dcr_nr / (chunk_size / rec_size);
	return result;
}

static int
checksum_sgl_cb(uint8_t *buf, size_t len, void *args)
{
	struct daos_csummer *obj = args;

	return daos_csummer_update(obj, buf, len);
}
