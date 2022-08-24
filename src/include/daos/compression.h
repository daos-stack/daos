/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_COMPRESSION_H
#define __DAOS_COMPRESSION_H

#include <daos_prop.h>

#define DC_STATUS_OK			0
#define DC_STATUS_ERR			-DER_MISC
#define DC_STATUS_INVALID_LEVEL		-DER_INVAL
#define DC_STATUS_OVERFLOW		-DER_TRUNC
#define DC_STATUS_NOMEM			-DER_NOSPACE

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

typedef void (*dc_callback_fn)(void *cb_data, int produced, int status);

struct compress_ft {
	int		(*cf_init)(
				void **daos_dc_ctx,
				uint16_t level,
				uint32_t max_buf_size);
	int		(*cf_compress)(
				void *daos_dc_ctx,
				uint8_t *src, size_t src_len,
				uint8_t *dst, size_t dst_len,
				size_t *produced);
	int		(*cf_decompress)(
				void *daos_mhash_ctx,
				uint8_t *src, size_t src_len,
				uint8_t *dst, size_t dst_len,
				size_t *produced);
	int		(*cf_compress_async)(
				void *daos_dc_ctx,
				uint8_t *src, size_t src_len,
				uint8_t *dst, size_t dst_len,
				dc_callback_fn cb_fn,
				void *cb_data);
	int		(*cf_decompress_async)(
				void *daos_dc_ctx,
				uint8_t *src, size_t src_len,
				uint8_t *dst, size_t dst_len,
				dc_callback_fn cb_fn,
				void *cb_data);
	int		(*cf_poll_response)(void *daos_dc_ctx);
	void		(*cf_destroy)(void *daos_dc_ctx);
	int		(*cf_available)();
	uint16_t	cf_level;
	char		*cf_name;
	enum DAOS_COMPRESS_TYPE	cf_type;
};

struct compress_ft *
daos_compress_type2algo(enum DAOS_COMPRESS_TYPE type, bool qat_preferred);

/**
 * Initialize compressor with the specified compress function table.
 *
 * \param[in]	obj		compressor.
 * \param[in]	ft		function table.
 * \param[in]	max_buf_size	maximum input size in bytes for each call,
 *		it is used by qat only for intermediate buffer allocation,
 *		in qat, if the size is set to 0, 64KB is used by default.
 */
int
daos_compressor_init(
		struct daos_compressor **obj,
		struct compress_ft *ft,
		uint32_t max_buf_size);

/**
 * Initialize compressor with the specified compress type.
 *
 * \param[in]	obj		compressor.
 * \param[in]	type		compression type (algorithm).
 * \param[in]	qat_preferred	indicates if qat is preferred.
 * \param[in]	max_buf_size	maximum input size in bytes for each call,
 *		it is used by qat only for intermediate buffer allocation,
 *		in qat, if the size is set to 0, 64KB is used by default.
 */
int
daos_compressor_init_with_type(
		struct daos_compressor **obj,
		enum DAOS_COMPRESS_TYPE type,
		bool qat_preferred,
		uint32_t max_buf_size);

/**
 * Compression function.
 *
 * \param[in]	obj		compressor.
 * \param[in]	src_buf		pointer to the buffer to be compressed.
 * \param[in]	src_len		length of the buffer to be compressed.
 * \param[in]	dst_buf		pointer to the pre-allocated output buffer
 *				for the compression result.
 * \param[in]	dst_len		length of the output buffer.
 * \param[out]	produced	length of compress result.
 */
int
daos_compressor_compress(
		struct daos_compressor *obj,
		uint8_t *src_buf, size_t src_len,
		uint8_t *dst_buf, size_t dst_len,
		size_t *produced);

/**
 * Compression asynchronous function.
 *
 * \param[in]	obj		compressor.
 * \param[in]	src_buf		pointer to the buffer to be compressed.
 * \param[in]	src_len		length of the buffer to be compressed.
 * \param[in]	dst_buf		pointer to the pre-allocated output buffer
 *				for the compression result.
 * \param[in]	dst_len		length of the output buffer.
 * \param[out]	cb_fn		callback function when async call complete.
 * \param[out]	cb_data		transparent data sent back to callback func.
 */
int
daos_compressor_compress_async(
		struct daos_compressor *obj,
		uint8_t *src_buf, size_t src_len,
		uint8_t *dst_buf, size_t dst_len,
		dc_callback_fn cb_fn, void *cb_data);

/**
 * DeCompression function.
 *
 * \param[in]	obj		compressor.
 * \param[in]	src_buf		pointer to the buffer to be de-compressed.
 * \param[in]	src_len		length of the buffer to be de-compressed.
 * \param[in]	dst_buf		pointer to the pre-allocated output buffer
 *				for the decompression result.
 * \param[in]	dst_len		length of the output buffer.
 * \param[out]	produced	length of decompress result.
 */
int
daos_compressor_decompress(
		struct daos_compressor *obj,
		uint8_t *src_buf, size_t src_len,
		uint8_t *dst_buf, size_t dst_len,
		size_t *produced);

/**
 * DeCompression asynchronous function.
 *
 * \param[in]	obj		compressor.
 * \param[in]	src_buf		pointer to the buffer to be de-compressed.
 * \param[in]	src_len		length of the buffer to be de-compressed.
 * \param[in]	dst_buf		pointer to the pre-allocated output buffer
 *				for the decompression result.
 * \param[in]	dst_len		length of the output buffer.
 * \param[out]	cb_fn		callback function when async call complete.
 * \param[out]	cb_data		transparent data sent back to callback func.
 */
int
daos_compressor_decompress_async(
		struct daos_compressor *obj,
		uint8_t *src_buf, size_t src_len,
		uint8_t *dst_buf, size_t dst_len,
		dc_callback_fn cb_fn, void *cb_data);

/**
 * Poll response on asynchronous mode.
 *
 * \param[in]	obj		compressor.
 */
int
daos_compressor_poll_response(struct daos_compressor *obj);

/**
 * Destroy and release the compressor.
 *
 * \param[in]	obj		compressor.
 */
void
daos_compressor_destroy(struct daos_compressor **obj);

/** ISA-L compression function table implemented in compression_isal.c */
extern struct compress_ft *isal_compress_algo_table[];

/** QAT compression function table implemented in compression_qat.c */
extern struct compress_ft *qat_compress_algo_table[];

#endif /** __DAOS_COMPRESSION_H */
