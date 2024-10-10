/**
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(csum)

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <gurt/types.h>
#include <daos/common.h>
#include <daos/compression.h>
#include <daos/cont_props.h>

/** Container Property knowledge */
enum DAOS_COMPRESS_TYPE
daos_contprop2compresstype(int contprop_compress_val)
{
	switch (contprop_compress_val) {
	case DAOS_PROP_CO_COMPRESS_LZ4:
		return COMPRESS_TYPE_LZ4;
	case DAOS_PROP_CO_COMPRESS_DEFLATE:
		return COMPRESS_TYPE_DEFLATE;
	case DAOS_PROP_CO_COMPRESS_DEFLATE1:
		return COMPRESS_TYPE_DEFLATE1;
	case DAOS_PROP_CO_COMPRESS_DEFLATE2:
		return COMPRESS_TYPE_DEFLATE2;
	case DAOS_PROP_CO_COMPRESS_DEFLATE3:
		return COMPRESS_TYPE_DEFLATE3;
	case DAOS_PROP_CO_COMPRESS_DEFLATE4:
		return COMPRESS_TYPE_DEFLATE4;
	default:
		return COMPRESS_TYPE_UNKNOWN;
	}
}

uint32_t
daos_compresstype2contprop(enum DAOS_COMPRESS_TYPE daos_compress_type)
{
	switch (daos_compress_type) {
	case COMPRESS_TYPE_LZ4:
		return DAOS_PROP_CO_COMPRESS_LZ4;
	case COMPRESS_TYPE_DEFLATE:
		return DAOS_PROP_CO_COMPRESS_DEFLATE;
	case COMPRESS_TYPE_DEFLATE1:
		return DAOS_PROP_CO_COMPRESS_DEFLATE1;
	case COMPRESS_TYPE_DEFLATE2:
		return DAOS_PROP_CO_COMPRESS_DEFLATE2;
	case COMPRESS_TYPE_DEFLATE3:
		return DAOS_PROP_CO_COMPRESS_DEFLATE3;
	case COMPRESS_TYPE_DEFLATE4:
		return DAOS_PROP_CO_COMPRESS_DEFLATE4;
	default:
		return DAOS_PROP_CO_COMPRESS_OFF;
	}
}

/** ------------------------------------------------------------- */

/**
 * Use ISA-L table by default, and choose QAT if it is preferred & available.
 */
static struct compress_ft **isal_algo_table = isal_compress_algo_table;
static struct compress_ft **qat_algo_table = qat_compress_algo_table;

struct compress_ft *
daos_compress_type2algo(enum DAOS_COMPRESS_TYPE type, bool qat_preferred)
{
	struct compress_ft *result = NULL;

	if (type > COMPRESS_TYPE_UNKNOWN && type < COMPRESS_TYPE_END) {
		if (qat_preferred &&
		    qat_algo_table[type - 1] &&
		    qat_algo_table[type - 1]->cf_available())
			result = qat_algo_table[type - 1];
		else
			result = isal_algo_table[type - 1];
	}
	if (result && result->cf_type == COMPRESS_TYPE_UNKNOWN)
		result->cf_type = type;
	return result;
}

int
daos_str2compresscontprop(const char *value)
{
	int t;

	for (t = COMPRESS_TYPE_UNKNOWN + 1; t < COMPRESS_TYPE_END; t++) {
		char *name = isal_algo_table[t - 1]->cf_name;

		if (!strncmp(name, value,
			     min(strlen(name), strlen(value)) + 1)) {
			return daos_compresstype2contprop(t);
		}
	}

	if (!strncmp(value, "off", min(strlen("off"), strlen(value)) + 1))
		return DAOS_PROP_CO_COMPRESS_OFF;

	return -DER_INVAL;
}

/**
 * struct daos_compressor functions
 */

int
daos_compressor_init(struct daos_compressor **obj,
		     struct compress_ft *ft,
		     uint32_t max_buf_size)
{
	struct daos_compressor	*result;
	int			rc = DC_STATUS_ERR;

	if (!ft) {
		D_ERROR("No function table\n");
		return DC_STATUS_ERR;
	}

	D_ALLOC_PTR(result);
	if (result == NULL)
		return DC_STATUS_NOMEM;

	result->dc_algo = ft;

	if (result->dc_algo->cf_init)
		rc = result->dc_algo->cf_init(&result->dc_ctx,
					      ft->cf_level,
					      max_buf_size);

	if (rc == DC_STATUS_OK)
		*obj = result;
	else
		D_FREE(result);

	return rc;
}

int
daos_compressor_init_with_type(struct daos_compressor **obj,
			       enum DAOS_COMPRESS_TYPE type,
			       bool qat_preferred,
			       uint32_t max_buf_size)
{
	return daos_compressor_init(
			obj,
			daos_compress_type2algo(type, qat_preferred),
			max_buf_size);
}

int
daos_compressor_compress(struct daos_compressor *obj,
			 uint8_t *src_buf, size_t src_len,
			 uint8_t *dst_buf, size_t dst_len,
			 size_t *produced)
{
	if (obj->dc_algo->cf_compress)
		return obj->dc_algo->cf_compress(
			obj->dc_ctx,
			src_buf, src_len,
			dst_buf, dst_len,
			produced);

	return DC_STATUS_ERR;
}

int
daos_compressor_decompress(struct daos_compressor *obj,
			   uint8_t *src_buf, size_t src_len,
			   uint8_t *dst_buf, size_t dst_len,
			   size_t *produced)
{
	if (obj->dc_algo->cf_decompress)
		return obj->dc_algo->cf_decompress(
			obj->dc_ctx,
			src_buf, src_len,
			dst_buf, dst_len,
			produced);

	return DC_STATUS_ERR;
}


int
daos_compressor_compress_async(struct daos_compressor *obj,
			       uint8_t *src_buf, size_t src_len,
			       uint8_t *dst_buf, size_t dst_len,
			       dc_callback_fn cb_fn, void *cb_data)
{
	if (obj->dc_algo->cf_compress_async)
		return obj->dc_algo->cf_compress_async(
				obj->dc_ctx,
				src_buf, src_len,
				dst_buf, dst_len,
				cb_fn, cb_data);

	return DC_STATUS_ERR;
}

int
daos_compressor_decompress_async(struct daos_compressor *obj,
				 uint8_t *src_buf, size_t src_len,
				 uint8_t *dst_buf, size_t dst_len,
				 dc_callback_fn cb_fn, void *cb_data)
{
	if (obj->dc_algo->cf_decompress_async)
		return obj->dc_algo->cf_decompress_async(obj->dc_ctx,
			src_buf, src_len, dst_buf, dst_len, cb_fn, cb_data);

	return DC_STATUS_ERR;
}

int
daos_compressor_poll_response(struct daos_compressor *obj)
{
	if (obj->dc_algo->cf_poll_response)
		return obj->dc_algo->cf_poll_response(obj->dc_ctx);

	return DC_STATUS_ERR;
}

void
daos_compressor_destroy(struct daos_compressor **obj)
{
	struct daos_compressor *compressor = *obj;

	if (!*obj)
		return;

	if (compressor->dc_algo->cf_destroy)
		compressor->dc_algo->cf_destroy(compressor->dc_ctx);

	D_FREE(compressor);
	*obj = NULL;
}
