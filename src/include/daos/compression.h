/**
 * (C) Copyright 2020 Intel Corporation.
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

#ifndef __DAOS_COMPRESSION_H
#define __DAOS_COMPRESSION_H

#include <daos_prop.h>

/**
 * -----------------------------------------------------------
 * Container Property Knowledge
 * -----------------------------------------------------------
 */

/** Convert a string into a property value for compression property */
int
daos_str2compresscontprop(const char *value);

/**
 * -----------------------------------------------------------
 * DAOS Compressor
 * -----------------------------------------------------------
 */
/**
 * Type of compression algorithm supported by DAOS.
 * Primarily used for looking up the appropriate algorithm functions to be used
 * for the compressor.
 */
enum DAOS_COMPRESS_TYPE {
	COMPRESS_TYPE_UNKNOWN = 0,

	COMPRESS_TYPE_LZ4	= 1,
	COMPRESS_TYPE_DEFLATE	= 2,
	COMPRESS_TYPE_DEFLATE1	= 3,
	COMPRESS_TYPE_DEFLATE2	= 4,
	COMPRESS_TYPE_DEFLATE3	= 5,
	COMPRESS_TYPE_DEFLATE4	= 6,

	COMPRESS_TYPE_END	= 7,
};

/** Lookup the appropriate COMPRESS_TYPE given daos container property */
enum DAOS_COMPRESS_TYPE daos_contprop2compresstype(int contprop_compress_val);

struct compress_ft;
struct daos_compressor {
	/** Pointer to the function table to be used for compression */
	struct compress_ft *dc_algo;
	/** Pointer to function table specific contexts */
	void *dc_ctx;
};

struct compress_ft {
	int		(*cf_init)(void **daos_dc_ctx, uint16_t level);
	int		(*cf_compress)(void *daos_dc_ctx,
				       uint8_t *src, size_t src_len,
				       uint8_t *dst, size_t dst_len);
	int		(*cf_decompress)(void *daos_mhash_ctx,
					 uint8_t *src, size_t src_len,
					 uint8_t *dst, size_t dst_len);
	void	(*cf_destroy)(void *daos_dc_ctx);
	uint16_t	cf_level;
	char		*cf_name;
	enum DAOS_COMPRESS_TYPE	cf_type;
};

struct compress_ft *
daos_compress_type2algo(enum DAOS_COMPRESS_TYPE type);

/**
 * Initialize compressor with the specified compress function table.
 *
 * \param[in]	obj	compressor.
 * \param[in]	ft	function table.
 */
int
daos_compressor_init(struct daos_compressor **obj,
		     struct compress_ft *ft);

/**
 * Initialize compressor with the specified compress type.
 *
 * \param[in]	obj	compressor.
 * \param[in]	type	compression type (algorithm).
 */
int
daos_compressor_init_with_type(struct daos_compressor **obj,
			       enum DAOS_COMPRESS_TYPE type);

/**
 * Compression function.
 *
 * \param[in]	obj	compressor.
 * \param[in]	src_buf	pointer to the buffer to be compressed.
 * \param[in]	src_len	length of the buffer to be compressed.
 * \param[in]	dst_buf	pointer to the pre-allocated output buffer
 *		for the compression result.
 * \param[in]	dst_len	length of the output buffer.
 *
 * \return	length of the compressed data
 *		0 indicates compression failed or data is incompressable
 */
int
daos_compressor_compress(struct daos_compressor *obj,
			 uint8_t *src_buf, size_t src_len,
			 uint8_t *dst_buf, size_t dst_len);

/**
 * DeCompression function.
 *
 * \param[in]	obj	compressor.
 * \param[in]	src_buf	pointer to the buffer to be de-compressed.
 * \param[in]	src_len	length of the buffer to be de-compressed.
 * \param[in]	dst_buf	pointer to the pre-allocated output buffer
 *		for the decompression result.
 * \param[in]	dst_len	length of the output buffer.
 *
 * \return	length of the decompressed data
 *		0 indicates decompression failed
 */
int
daos_compressor_decompress(struct daos_compressor *obj,
			   uint8_t *src_buf, size_t src_len,
			   uint8_t *dst_buf, size_t dst_len);

/**
 * Destroy and release the compressor.
 *
 * \param[in]	obj	compressor.
 */
void
daos_compressor_destroy(struct daos_compressor **obj);

#endif /** __DAOS_COMPRESSION_H */
