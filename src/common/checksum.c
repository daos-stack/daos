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

#include <gurt/types.h>
#include <daos/common.h>
#include <daos/checksum.h>
#include <daos/cont_props.h>

#define C_TRACE(...) D_DEBUG(DB_CSUM, __VA_ARGS__)
#define C_TRACE_ENABLED() D_LOG_ENABLED(DB_TRACE)

/** File function signatures */
static int
checksum_sgl_cb(uint8_t *buf, size_t len, void *args);

static bool
is_array(const daos_iod_t *iod)
{
	return iod->iod_type == DAOS_IOD_ARRAY;
}

/** ------------------------------------------------------------- */
static char *csum_unknown_name = "unknown checksum type";

/**
 * struct daos_csummer functions
 */

int
daos_csummer_init(struct daos_csummer **obj, struct hash_ft *ft,
		  size_t chunk_bytes, bool srv_verify)
{
	struct daos_csummer	*result;
	int			 rc = 0;

	if (!ft) {
		D_ERROR("No function table");
		return -DER_INVAL;
	}

	D_ALLOC(result, sizeof(*result));
	if (result == NULL)
		return -DER_NOMEM;

	result->dcs_algo = ft;
	result->dcs_chunk_size = chunk_bytes;
	result->dcs_srv_verify = srv_verify;

	if (result->dcs_algo->cf_init)
		rc = result->dcs_algo->cf_init(&result->dcs_ctx);

	if (rc == 0)
		*obj = result;

	return rc;
}

int
daos_csummer_init_with_type(struct daos_csummer **obj, enum DAOS_HASH_TYPE type,
			    size_t chunk_bytes, bool srv_verify)
{
	return daos_csummer_init(obj, daos_mhash_type2algo(type), chunk_bytes,
				 srv_verify);
}

int
daos_csummer_init_with_props(struct daos_csummer **obj, daos_prop_t *props)
{
	uint32_t csum_prop = daos_cont_prop2csum(props);

	if (!daos_cont_csum_prop_is_enabled(csum_prop)) {
		*obj = NULL;
		return 0;
	}

	return daos_csummer_init_with_type(obj,
					   daos_contprop2hashtype(csum_prop),
					   daos_cont_prop2chunksize(props),
					   daos_cont_prop2serververify(props));
}

struct daos_csummer *
daos_csummer_copy(const struct daos_csummer *obj)
{
	struct daos_csummer *result = NULL;

	daos_csummer_init(&result, obj->dcs_algo,
			  obj->dcs_chunk_size, obj->dcs_srv_verify);

	return result;
}

void daos_csummer_destroy(struct daos_csummer **obj)
{
	struct daos_csummer *csummer = *obj;

	if (!*obj)
		return;

	if (csummer->dcs_algo->cf_destroy)
		csummer->dcs_algo->cf_destroy(csummer->dcs_ctx);
	D_FREE(csummer);
	*obj = NULL;
}

uint16_t
daos_csummer_get_csum_len(struct daos_csummer *obj)
{
	if (!daos_csummer_initialized(obj))
		return 0;
	if (obj->dcs_algo->cf_get_size)
		return obj->dcs_algo->cf_get_size(obj->dcs_ctx);
	return obj->dcs_algo->cf_hash_len;
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

bool
daos_csummer_get_srv_verify(struct daos_csummer *obj)
{
	if (daos_csummer_initialized(obj))
		return obj->dcs_srv_verify;
	return false;
}

uint32_t
daos_csummer_get_rec_chunksize(struct daos_csummer *obj, uint64_t rec_size)
{
	if (daos_csummer_initialized(obj))
		return csum_record_chunksize(obj->dcs_chunk_size, rec_size);
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

int
daos_csummer_reset(struct daos_csummer *obj)
{
	if (obj->dcs_algo->cf_reset)
		return obj->dcs_algo->cf_reset(obj->dcs_ctx);
	return 0;
}

int
daos_csummer_update(struct daos_csummer *obj, uint8_t *buf, size_t buf_len)
{
	int rc = 0;

	if (obj->dcs_csum_buf && obj->dcs_csum_buf_size > 0)
		rc = obj->dcs_algo->cf_update(obj->dcs_ctx, buf, buf_len);

	if (C_TRACE_ENABLED()) {
		d_iov_t tmp;

		d_iov_set(&tmp, buf, buf_len);
		C_TRACE("Updated csum(type=%s) for'"DF_KEY"'\n",
			daos_csummer_get_name(obj), DP_KEY(&tmp));
	}

	return rc;
}

int
daos_csummer_finish(struct daos_csummer *obj)
{
	int rc = 0;

	if (obj->dcs_algo->cf_finish)
		rc = obj->dcs_algo->cf_finish(obj->dcs_ctx, obj->dcs_csum_buf,
					      obj->dcs_csum_buf_size);

	if (C_TRACE_ENABLED()) {
		C_TRACE("Finished - Checksum (type=%s) is: "DF_CI_BUF"\n",
			daos_csummer_get_name(obj),

			DP_CI_BUF(obj->dcs_csum_buf, obj->dcs_csum_buf_size));
	}

	return rc;
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
		C_TRACE("Comparing: "DF_CI_BUF" with "DF_CI_BUF"\n",
			DP_CI_BUF(a, csum_len),
			DP_CI_BUF(b, csum_len));
	}

	if (obj->dcs_algo->cf_compare)
		return obj->dcs_algo->cf_compare(obj->dcs_ctx, a, b, csum_len);

	return memcmp(a, b, csum_len) == 0;
}

static uint32_t
daos_singv_calc_chunks(struct dcs_layout *singv_los, int idx)
{
	if (singv_los == NULL || singv_los[idx].cs_even_dist == 0)
		return 1;

	D_ASSERT(singv_los[idx].cs_nr > 1);
	return singv_los[idx].cs_nr;
}

uint64_t
daos_csummer_allocation_size(struct daos_csummer *obj, daos_iod_t *iods,
			     uint32_t nr, bool akey_only,
			     struct dcs_layout *singv_los)
{
	int		i, j;
	uint64_t	result = 0;
	uint16_t	csum_size = daos_csummer_get_csum_len(obj);

	for (i = 0; i < nr; i++) {
		daos_iod_t *iod = &iods[i];

		result += sizeof(struct dcs_iod_csums);

		if (!obj->dcs_skip_key_calc)
			result += csum_size; /** akey csum */

		if (akey_only || !csum_iod_is_supported(iod))
			continue;

		/** calc needed memory for the recx csums */
		for (j = 0; j < iod->iod_nr; j++) {
			daos_recx_t	*recx = &iod->iod_recxs[j];
			uint32_t	 csum_count;
			uint64_t	 rec_chunksize;

			rec_chunksize = daos_csummer_get_rec_chunksize(obj,
							       iod->iod_size);

			csum_count = is_array(iod) ?
				     daos_recx_calc_chunks(*recx, iod->iod_size,
							   rec_chunksize) :
				     daos_singv_calc_chunks(singv_los, i);

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
daos_csummer_alloc_iods_csums(struct daos_csummer *obj, daos_iod_t *iods,
			      uint32_t nr, bool akey_only,
			      struct dcs_layout *singv_los,
			      struct dcs_iod_csums **p_iods_csums)
{
	int			 i, j, rc = 0;
	uint16_t		 csum_size;
	uint16_t		 csum_type;
	struct dcs_iod_csums	*iods_csums = NULL;
	uint8_t			*buf;
	uint64_t		 buf_len;
	uint64_t		 used = 0;

	if (!(daos_csummer_initialized(obj)) || iods == NULL || nr == 0)
		goto done;

	rc = nr;

	csum_size = daos_csummer_get_csum_len(obj);
	csum_type = daos_csummer_get_type(obj);

	/**
	 * allocate enough memory for all iod checksums at once, then update
	 * pointers appropriately
	 */
	buf_len = daos_csummer_allocation_size(obj, iods, nr, akey_only,
					       singv_los);
	D_ALLOC(buf, buf_len);
	if (buf == NULL)
		return -DER_NOMEM;

	setptr(iods_csums, buf, sizeof(*iods_csums) * nr, used, buf_len);

	for (i = 0; i < nr; i++) {
		daos_iod_t		*iod = &iods[i];
		struct dcs_iod_csums	*iod_csum = &iods_csums[i];
		uint64_t		 rec_chunksize;



		/** setup akey csum  */
		if (!obj->dcs_skip_key_calc) {
			ci_set(&iod_csum->ic_akey, NULL, csum_size, csum_size,
			       1, CSUM_NO_CHUNK, csum_type);
			setptr(iod_csum->ic_akey.cs_csum, buf, csum_size, used,
			       buf_len);
		}

		if (akey_only || !csum_iod_is_supported(iod))
			continue;

		rec_chunksize = daos_csummer_get_rec_chunksize(obj,
							       iod->iod_size);

		/** setup data csum infos */
		setptr(iod_csum->ic_data, buf,
		       sizeof(*iod_csum->ic_data) * iod->iod_nr,
		       used, buf_len);

		for (j = 0; j < iod->iod_nr; j++) {
			struct dcs_csum_info	*csum_info;
			uint32_t		 csum_count;

			csum_info = &iod_csum->ic_data[j];
			if (is_array(iod)) {

				daos_recx_t *recx;

				recx = &iod->iod_recxs[j];
				csum_count = daos_recx_calc_chunks(*recx,
						iod->iod_size, rec_chunksize);
				ci_set(csum_info, NULL, csum_count * csum_size,
				       csum_size, csum_count,
				       rec_chunksize, csum_type);
			} else { /** single value */
				csum_count = daos_singv_calc_chunks(singv_los,
								    i);
				ci_set(csum_info, NULL, csum_count * csum_size,
				       csum_size, csum_count,
				       CSUM_NO_CHUNK, csum_type);
			}

			/**
			 * buffer set to null first by ci_set, now set it using
			 * the setptr macro so that amount of memory
			 * used from allocated buffer is tracked.
			 */
			setptr(csum_info->cs_csum, buf, csum_count * csum_size,
			       used, buf_len);
		}
		iod_csum->ic_nr = iod->iod_nr;
	}

done:
	if (p_iods_csums != NULL)
		*p_iods_csums = iods_csums;

	return rc;
}

static int
calc_csum_recx_with_no_map(struct daos_csummer *obj, size_t csum_nr,
			   daos_recx_t *recx,
			   struct dcs_csum_info *csum_info,
			   size_t rec_len, d_sg_list_t *sgl,
			   uint32_t rec_chunksize,
			   struct daos_sgl_idx *idx)
{
	struct daos_csum_range	 chunk;
	daos_size_t		 bytes_for_csum;
	uint8_t			*buf;
	uint32_t		 i;
	int			 rc;

	for (i = 0; i < csum_nr; i++) {
		buf = ci_idx2csum(csum_info, i);
		daos_csummer_set_buffer(obj, buf, csum_info->cs_len);
		daos_csummer_reset(obj);

		chunk = csum_recx_chunkidx2range(recx, rec_len,
						 rec_chunksize, i);

		bytes_for_csum = chunk.dcr_nr * rec_len;
		rc = daos_sgl_processor(sgl, false, idx, bytes_for_csum,
					checksum_sgl_cb, obj);
		if (rc != 0) {
			D_ERROR("daos_sgl_processor error: %d\n", rc);
			return rc;
		}
		daos_csummer_finish(obj);
	}

	return 0;
}

static bool
range_overlaps(struct daos_csum_range *a, struct daos_csum_range *b)
{
	return (a->dcr_lo >= b->dcr_lo && a->dcr_lo <= b->dcr_hi) ||
	       (b->dcr_lo >= a->dcr_lo && b->dcr_lo <= a->dcr_hi);
}

struct daos_csum_range
get_maps_idx_nr_for_range(struct daos_csum_range *req_range, daos_iom_t *map)
{
	struct daos_csum_range	mapped_range;
	struct daos_csum_range	result = {0};
	daos_recx_t		mapped_recx;
	int			i;

	for (i = 0; i < map->iom_nr; i++) {
		mapped_recx = map->iom_recxs[i];
		/** turn mapped recx into a range */
		dcr_set_idx_nr(&mapped_range, mapped_recx.rx_idx,
			       mapped_recx.rx_nr);
		if (range_overlaps(&mapped_range, req_range))
			result.dcr_nr++;
		else if (req_range->dcr_lo > mapped_range.dcr_hi)
			result.dcr_lo++;
	}

	if (result.dcr_nr == 0) /** none found, reset idx */
		result.dcr_lo = 0;
	else
		dcr_set_idx_nr(&result, result.dcr_lo, result.dcr_nr);

	return result;
}

static int
recx_hi(daos_recx_t *r)
{
	return r->rx_idx + r->rx_nr - 1;
}

static int
sgl_process_nop_cb(uint8_t *buf, size_t len, void *args)
{
	return 0;
}

static int
calc_csum_recx_with_map(struct daos_csummer *obj, size_t csum_nr,
			daos_recx_t *recx,
			struct dcs_csum_info *csum_info,
			daos_iom_t *map, size_t rec_len,
			d_sg_list_t *sgl, uint32_t rec_chunksize,
			struct daos_sgl_idx *idx)
{
	uint8_t			*buf;
	struct daos_csum_range	 req_chunk;
	struct daos_csum_range	 mapped_chunk;
	int			 rc;
	uint32_t		 i, j;
	daos_size_t		 bytes_for_csum;
	daos_size_t		 bytes_to_skip;
	uint64_t		 prev_idx = recx->rx_idx;
	struct daos_csum_range	 maps_in_chunk;
	daos_size_t		 consumed_bytes = 0;

	for (i = 0; i < csum_nr; i++) {
		buf = ci_idx2csum(csum_info, i);
		daos_csummer_set_buffer(obj, buf, csum_info->cs_len);
		daos_csummer_reset(obj);
		req_chunk = csum_recx_chunkidx2range(recx,
						 rec_len, rec_chunksize,
						 i);
		maps_in_chunk = get_maps_idx_nr_for_range(&req_chunk, map);
		for (j = maps_in_chunk.dcr_lo;
		     j < maps_in_chunk.dcr_lo + maps_in_chunk.dcr_nr; j++) {
			dcr_set_idxs(&mapped_chunk,
				     max(req_chunk.dcr_lo,
					 map->iom_recxs[j].rx_idx),
				     min(req_chunk.dcr_hi,
					 recx_hi(&map->iom_recxs[j])));

			if (mapped_chunk.dcr_lo > prev_idx) {
				bytes_to_skip = (mapped_chunk.dcr_lo - prev_idx)
						* rec_len;
				daos_sgl_processor(sgl, false, idx,
						   bytes_to_skip,
						   sgl_process_nop_cb, NULL);
				consumed_bytes += bytes_to_skip;
			}
			bytes_for_csum = mapped_chunk.dcr_nr * rec_len;
			rc = daos_sgl_processor(sgl, false, idx, bytes_for_csum,
						checksum_sgl_cb, obj);
			consumed_bytes += bytes_for_csum;
			if (rc != 0) {
				D_ERROR("daos_sgl_processor error: %d\n", rc);
				return rc;
			}
			prev_idx = mapped_chunk.dcr_hi + 1;
		}

		daos_csummer_finish(obj);
	}

	if (consumed_bytes < recx->rx_nr * rec_len) {
		/** Nothing mapped for recx or tail unmapped */
		bytes_to_skip = (recx->rx_nr * rec_len) - consumed_bytes;
		daos_sgl_processor(sgl, false, idx, bytes_to_skip,
				   sgl_process_nop_cb, NULL);
		consumed_bytes += bytes_to_skip;
	}

	D_ASSERTF(consumed_bytes == recx->rx_nr * rec_len,
		"consumed_bytes(%lu) == recx->rx_nr * rec_len(%lu)",
		  consumed_bytes, recx->rx_nr * rec_len);
	return 0;
}

static int
calc_csum_recx(struct daos_csummer *obj, d_sg_list_t *sgl, size_t rec_len,
	       daos_recx_t *recxs, size_t nr, struct dcs_csum_info *csums,
	       daos_iom_t *map)
{
	size_t			 csum_nr;
	uint32_t		 rec_chunksize;
	uint32_t		 i;
	uint32_t		 rc;
	struct daos_sgl_idx	 idx = {0};

	if (!(daos_csummer_initialized(obj) && recxs && sgl))
		return 0;

	rec_chunksize = daos_csummer_get_rec_chunksize(obj, rec_len);
	for (i = 0; i < nr; i++) { /** for each extent/checksum info */
		csum_nr = daos_recx_calc_chunks(recxs[i], rec_len,
						rec_chunksize);

		if (map != NULL)
			rc = calc_csum_recx_with_map(obj, csum_nr, &recxs[i],
						     &csums[i], map, rec_len,
						     sgl, rec_chunksize, &idx);
		else
			rc = calc_csum_recx_with_no_map(obj, csum_nr, &recxs[i],
							&csums[i], rec_len, sgl,
							rec_chunksize, &idx);
		if (rc != 0)
			return rc;

		C_TRACE("Calculating %zu checksum(s) for Array Value "
				DF_RECX", data_len: %lu -> "DF_CI"\n",
			csum_nr, DP_RECX(recxs[i]), sgl->sg_iovs[0].iov_len,
			DP_CI(csums[i]));
	}

	return 0;
}

static int
calc_csum_sv(struct daos_csummer *obj, d_sg_list_t *sgl, size_t rec_len,
	     struct dcs_layout *singv_lo, int singv_idx,
	     struct dcs_csum_info *csums)
{
	size_t			 bytes_for_csum, csum_buf_len;
	size_t			 skip_size, last_size = -1;
	struct daos_sgl_idx	 sgl_idx = {0};
	uint32_t		 idx, last_idx = -1;
	uint8_t			*csum_buf;
	int			 rc;

	if (!(daos_csummer_initialized(obj)))
		return 0;

	C_TRACE("Calculating checksum for Single Value (len=%lu)\n", rec_len);
	if (singv_lo != NULL && singv_lo->cs_even_dist == 1) {
		D_ASSERT(singv_lo->cs_bytes > 0 &&
			 singv_lo->cs_bytes < rec_len);
		D_ASSERT(singv_lo->cs_nr > 1);
		last_idx = rec_len / singv_lo->cs_bytes;
		D_ASSERT(last_idx >= 1);
		if (singv_idx == -1) {
			D_ASSERT(csums->cs_nr == singv_lo->cs_nr);
			last_size = rec_len - last_idx * singv_lo->cs_bytes;
		} else {
			D_ASSERT(csums->cs_nr == 1);
			/* skip to the sgl location of singv_idx, the last
			 * data cell possibly with less valid bytes, and
			 * followed by parity cells.
			 */
			if (singv_idx <= last_idx)
				skip_size = singv_idx * singv_lo->cs_bytes;
			else
				skip_size = rec_len + (singv_idx - last_idx) *
						      singv_lo->cs_bytes;
			rc = daos_sgl_processor(sgl, false, &sgl_idx, skip_size,
						NULL, NULL);
			if (rc)
				return rc;
		}
		bytes_for_csum = singv_lo->cs_bytes;
	} else {
		D_ASSERT(csums->cs_nr == 1);
		bytes_for_csum = rec_len;
	}

	csum_buf_len = bytes_for_csum;
	for (idx = 0; idx < csums->cs_nr; idx++) {
		if (idx == last_idx)
			csum_buf_len = last_size;
		csum_buf = ci_idx2csum(&csums[0], idx);
		daos_csummer_set_buffer(obj, csum_buf, csums->cs_len);
		daos_csummer_reset(obj);

		rc = daos_sgl_processor(sgl, false, &sgl_idx, csum_buf_len,
					checksum_sgl_cb, obj);
		if (rc)
			return rc;

		daos_csummer_finish(obj);
	}

	C_TRACE("Calculating checksum for Single Value (len=%lu) -> "
		DF_CI"\n", rec_len, DP_CI(csums[0]));

	return 0;
}

/** Using the data from the iov, calculate the checksum */
static int
calc_for_iov(struct daos_csummer *csummer, daos_key_t *iov,
	     uint8_t *csum_buf, uint16_t csum_buf_len)
{
	int rc;

	memset(csum_buf, 0, csum_buf_len);

	daos_csummer_set_buffer(csummer, csum_buf, csum_buf_len);

	rc = daos_csummer_reset(csummer);
	if (rc != 0) {
		D_ERROR("daos_csummer_reset error: %d\n", rc);
		return rc;
	}

	rc = daos_csummer_update(csummer, iov->iov_buf, iov->iov_len);
	if (rc != 0) {
		D_ERROR("daos_csummer_update error: %d\n", rc);
		return rc;
	}

	rc = daos_csummer_finish(csummer);
	if (rc != 0) {
		D_ERROR("daos_csummer_finish error: %d\n", rc);
		return rc;
	}

	return 0;
}

int
daos_csummer_calc_one(struct daos_csummer *obj, d_sg_list_t *sgl,
		       struct dcs_csum_info *csums, size_t rec_len, size_t nr,
		       size_t idx)
{
	daos_recx_t recx = { 0 };

	recx.rx_idx = idx;
	recx.rx_nr = nr;
	return calc_csum_recx(obj, sgl, rec_len, &recx, 1, csums, NULL);
}

int
daos_csummer_calc_iods(struct daos_csummer *obj, d_sg_list_t *sgls,
		       daos_iod_t *iods, daos_iom_t *maps, uint32_t nr,
		       bool akey_only, struct dcs_layout *singv_los,
		       int singv_idx, struct dcs_iod_csums **p_iods_csums)
{
	int			 rc = 0;
	int			 i;
	struct dcs_iod_csums	*iods_csums = NULL;
	struct dcs_layout	*singv_lo, *los;
	uint32_t		 iods_csums_nr;
	uint16_t		 csum_len = daos_csummer_get_csum_len(obj);

	if (!daos_csummer_initialized(obj) || nr == 0)
		return 0;

	*p_iods_csums = NULL;

	if (singv_los == NULL || singv_idx != -1)
		los = NULL;
	else
		los = singv_los;
	rc = daos_csummer_alloc_iods_csums(obj, iods, nr, akey_only,
					   los, &iods_csums);
	if (rc < 0) {
		D_ERROR("daos_csummer_alloc_iods_csums error: %d\n", rc);
		return rc;
	}

	iods_csums_nr = (uint32_t) rc;

	for (i = 0; i < iods_csums_nr; i++) {
		daos_iod_t		*iod = &iods[i];
		struct dcs_iod_csums	*csums = &iods_csums[i];

		/** akey */
		if (!obj->dcs_skip_key_calc) {
			rc = calc_for_iov(obj, &iod->iod_name,
					  csums->ic_akey.cs_csum, csum_len);
			if (rc != 0) {
				D_ERROR("calc_for_iov error: %d\n", rc);
				goto error;
			}
		}

		if (akey_only || !csum_iod_is_supported(iod))
			continue;

		/** data */
		singv_lo = (singv_los == NULL) ? NULL : &singv_los[i];
		daos_iom_t *map = maps == NULL ? NULL : &maps[i];

		rc = is_array(iod) ?
		     calc_csum_recx(obj, &sgls[i], iod->iod_size,
				    iod->iod_recxs, iod->iod_nr,
				    csums->ic_data, map) :
		     calc_csum_sv(obj, &sgls[i], iod->iod_size, singv_lo,
				  singv_idx, csums->ic_data);
		csums->ic_nr = iod->iod_nr;

		if (rc != 0) {
			D_ERROR("calc_csum error: %d\n", rc);
			goto error;
		}
	}

	*p_iods_csums = iods_csums;

	return 0;

error:
	daos_csummer_free_ic(obj, &iods_csums);
	return rc;
}

int
daos_csummer_calc_key(struct daos_csummer *csummer, daos_key_t *key,
		      struct dcs_csum_info **p_csum)
{
	struct dcs_csum_info	*csum_info;
	uint8_t			*dkey_csum_buf;
	uint16_t		 size = daos_csummer_get_csum_len(csummer);
	uint16_t		 type = daos_csummer_get_type(csummer);
	int			 rc;

	if (!daos_csummer_initialized(csummer) || csummer->dcs_skip_key_calc)
		return 0;

	C_TRACE("Creating checksum for key: "DF_KEY"\n", DP_KEY(key));
	D_ALLOC(csum_info, sizeof(*csum_info) + size);
	if (csum_info == NULL)
		return -DER_NOMEM;

	dkey_csum_buf = (uint8_t *) &csum_info[1];

	ci_set(csum_info, dkey_csum_buf, size, size, 1, CSUM_NO_CHUNK, type);

	rc = calc_for_iov(csummer, key, dkey_csum_buf, size);
	if (rc == 0) {
		*p_csum = csum_info;
		C_TRACE("Checksum created for key: "DF_KEY"->"DF_CI"\n",
			DP_KEY(key), DP_CI(*csum_info));
	} else {
		D_ERROR("calc_for_iov error: %d\n", rc);
		*p_csum = NULL;
		D_FREE(csum_info);
	}

	return rc;
}


void
daos_csummer_free_ic(struct daos_csummer *obj, struct dcs_iod_csums **p_cds)
{
	if (!(daos_csummer_initialized(obj) && *p_cds))
		return;
	D_FREE((*p_cds));
}


void
daos_csummer_free_ci(struct daos_csummer *obj, struct dcs_csum_info **p_cis)
{
	if (!(daos_csummer_initialized(obj) && *p_cis))
		return;
	D_FREE((*p_cis));
}

int
daos_csummer_verify_iod(struct daos_csummer *obj, daos_iod_t *iod,
			d_sg_list_t *sgl, struct dcs_iod_csums *iod_csum,
			struct dcs_layout *singv_lo, int singv_idx,
			daos_iom_t *map)
{
	struct dcs_iod_csums	*new_iod_csums;
	int			 i;
	int			 rc;
	bool			 match;

	if (!daos_csummer_initialized(obj) || obj->dcs_skip_data_verify)
		return 0;

	if (iod == NULL || sgl == NULL || iod_csum == NULL) {
		D_ERROR("Invalid params\n");
		return -DER_INVAL;
	}

	rc = daos_csummer_calc_iods(obj, sgl, iod, map, 1, 0, singv_lo,
				    singv_idx,
				    &new_iod_csums);
	if (rc != 0) {
		D_ERROR("daos_csummer_calc_iods error: %d\n", rc);
		return rc;
	}

	for (i = 0; i < iod->iod_nr; i++) {
		match = daos_csummer_compare_csum_info(obj,
				&new_iod_csums->ic_data[i],
				&iod_csum->ic_data[i]);
		if (!match) {
			D_ERROR("Data corruption found. "
				"Calculated "DF_CI" != "
				"received "DF_CI"\n",
				DP_CI(new_iod_csums->ic_data[i]),
				DP_CI(iod_csum->ic_data[i]));
			D_GOTO(done, rc = -DER_CSUM);
		}
	}

done:
	daos_csummer_free_ic(obj, &new_iod_csums);

	return rc;
}

int
daos_csummer_verify_key(struct daos_csummer *obj, daos_key_t *key,
			struct dcs_csum_info *csum)
{
	struct dcs_csum_info	*csum_info_verify;
	bool			 match;
	int			rc;

	if (!daos_csummer_initialized(obj))
		return 0;

	if (!ci_is_valid(csum)) {
		D_ERROR("checksums is enabled, but dcs_csum_info is invalid\n");
		return -DER_CSUM;
	}

	rc = daos_csummer_calc_key(obj, key, &csum_info_verify);
	if (rc != 0) {
		D_ERROR("daos_csummer_calc error: %d\n", rc);
		return rc;
	}

	match = daos_csummer_compare_csum_info(obj, csum, csum_info_verify);
	daos_csummer_free_ci(obj, &csum_info_verify);
	if (!match) {
		D_ERROR("Key checksums don't match\n");
		return -DER_CSUM;
	}

	return 0;
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
 * -----------------------------------------------------------------------------
 * struct daos_csum_info functions
 * -----------------------------------------------------------------------------
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

	D_ASSERTF(idx < dcb->cs_nr, "idx(%d) < dcb->cs_nr(%d)",
		  idx, dcb->cs_nr);
	D_ASSERT(len <= dcb->cs_buf_len - idx * dcb->cs_len);

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

uint64_t
ci_buf2uint64(const uint8_t *buf, uint16_t len)
{
	if (len >= 8)
		return *(uint64_t *) buf;
	if (len >= 4)
		return *(uint32_t *) buf;
	if (len >= 2)
		return *(uint16_t *) buf;
	if (len == 1)
		return *(uint16_t *) buf;

	return 0;
}

/** helper for printing csum as a 64bit value */
uint64_t
ci2csum(struct dcs_csum_info ci)
{
	return ci_buf2uint64(ci.cs_csum, ci.cs_len);
}

int
ci_serialize(struct dcs_csum_info *obj, d_iov_t *iov)
{
	if (ci_size(*obj) > daos_iov_remaining(*iov))
		return -DER_REC2BIG;
	daos_iov_append(iov, obj, sizeof(*obj));
	daos_iov_append(iov, obj->cs_csum, obj->cs_buf_len);

	return 0;
}

void
ci_cast(struct dcs_csum_info **obj, const d_iov_t *iov)
{
	void			*buf;
	struct dcs_csum_info	*tmp;

	D_ASSERT(iov != NULL);
	D_ASSERT(obj != NULL);
	*obj = NULL;

	buf = iov->iov_buf;
	tmp = (struct dcs_csum_info *)buf;

	if (ci_size(*tmp) > iov->iov_len)
		return;

	tmp->cs_csum = buf + sizeof(struct dcs_csum_info);
	*obj = tmp;
}

void
ci_move_next_iov(struct dcs_csum_info *csum_info, d_iov_t *csum_iov)
{
	if (csum_info == NULL || csum_iov == NULL)
		return;
	D_ASSERT(csum_iov->iov_buf_len >= ci_size(*csum_info));
	D_ASSERT(csum_iov->iov_len >= ci_size(*csum_info));

	csum_iov->iov_buf += ci_size(*csum_info);
	csum_iov->iov_buf_len -= ci_size(*csum_info);
	csum_iov->iov_len -= ci_size(*csum_info);
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

daos_off_t
csum_record_chunksize(daos_off_t default_chunksize, daos_off_t rec_size)
{
	D_ASSERT(rec_size > 0 && default_chunksize > 0);

	if (rec_size > default_chunksize)
		return rec_size;
	return (default_chunksize / rec_size) * rec_size;
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
	if (lo_idx == hi_idx) /** if lo == hi, always just 1 chunk */
		return 1;
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

void
dcf_corrupt(d_sg_list_t *sgls, uint32_t nr)
{
	int i;

	for (i = 0; i < nr; i++) {
		d_sg_list_t *sgl = &sgls[i];
		((char *)sgl->sg_iovs->iov_buf)[0] += 2;
	}
}
