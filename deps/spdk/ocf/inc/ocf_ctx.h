/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __OCF_CTX_H__
#define __OCF_CTX_H__

/**
 * @file
 * @brief OCF library context API
 */

#include "ocf_volume.h"
#include "ocf_logger.h"

/**
 * @brief Seeking start position in environment data buffer
 */
typedef enum {
	ctx_data_seek_begin,
		/*!< Seeking from the beginning of environment data buffer */
	ctx_data_seek_current,
		/*!< Seeking from current position in environment data buffer */
} ctx_data_seek_t;

/**
 * @brief Context data representation ops
 */
struct ocf_data_ops {
	/**
	 * @brief Allocate contest data buffer
	 *
	 * @param[in] pages The size of data buffer in pages
	 *
	 * @return Context data buffer
	 */
	ctx_data_t *(*alloc)(uint32_t pages);

	/**
	 * @brief Free context data buffer
	 *
	 * @param[in] data Contex data buffer which shall be freed
	 */
	void (*free)(ctx_data_t *data);

	/**
	 * @brief Lock context data buffer to disable swap-out
	 *
	 * @param[in] data Contex data buffer which shall be locked
	 *
	 * @retval 0 Memory locked successfully
	 * @retval Non-zero Memory locking failure
	 */
	int (*mlock)(ctx_data_t *data);

	/**
	 * @brief Unlock context data buffer
	 *
	 * @param[in] data Contex data buffer which shall be unlocked
	 */
	void (*munlock)(ctx_data_t *data);

	/**
	 * @brief Read from environment data buffer into raw data buffer
	 *
	 * @param[in,out] dst Destination raw memory buffer
	 * @param[in] src Source context data buffer
	 * @param[in] size Number of bytes to be read
	 *
	 * @return Number of read bytes
	 */
	uint32_t (*read)(void *dst, ctx_data_t *src, uint32_t size);

	/**
	 * @brief Write raw data buffer into context data buffer
	 *
	 * @param[in,out] dst Destination context data buffer
	 * @param[in] src Source raw memory buffer
	 * @param[in] size Number of bytes to be written
	 *
	 * @return Number of written bytes
	 */
	uint32_t (*write)(ctx_data_t *dst, const void *src, uint32_t size);

	/**
	 * @brief Zero context data buffer
	 *
	 * @param[in,out] dst Destination context data buffer to be zeroed
	 * @param[in] size Number of bytes to be zeroed
	 *
	 * @return Number of zeroed bytes
	 */
	uint32_t (*zero)(ctx_data_t *dst, uint32_t size);

	/**
	 * @brief Seek read/write head in context data buffer for specified
	 * offset
	 *
	 * @param[in,out] dst Destination context data buffer to be seek
	 * @param[in] seek Seek beginning offset
	 * @param[in] size Number of bytes to be seek
	 *
	 * @return Number of seek bytes
	 */
	uint32_t (*seek)(ctx_data_t *dst, ctx_data_seek_t seek, uint32_t size);

	/**
	 * @brief Copy context data buffer content
	 *
	 * @param[in,out] dst Destination context data buffer
	 * @param[in] src Source context data buffer
	 * @param[in] to Starting offset in destination buffer
	 * @param[in] from Starting offset in source buffer
	 * @param[in] bytes Number of bytes to be copied
	 *
	 * @return Number of bytes copied
	 */
	uint64_t (*copy)(ctx_data_t *dst, ctx_data_t *src,
			uint64_t to, uint64_t from, uint64_t bytes);

	/**
	 * @brief Erase content of data buffer
	 *
	 * @param[in] dst Contex data buffer which shall be erased
	 */
	void (*secure_erase)(ctx_data_t *dst);
};

/**
 * @brief Cleaner operations
 */
struct ocf_cleaner_ops {
	/**
	 * @brief Initialize cleaner.
	 *
	 * This function should create worker, thread, timer or any other
	 * mechanism responsible for calling cleaner routine.
	 *
	 * @param[in] c Descriptor of cleaner to be initialized
	 *
	 * @retval 0 Cleaner has been initializaed successfully
	 * @retval Non-zero Cleaner initialization failure
	 */
	int (*init)(ocf_cleaner_t c);

	/**
	 * @brief Kick cleaner thread.
	 *
	 * @param[in] c Descriptor of cleaner to be kicked.
	 */
	void (*kick)(ocf_cleaner_t c);

	/**
	 * @brief Stop cleaner
	 *
	 * @param[in] c Descriptor of cleaner beeing stopped
	 */
	void (*stop)(ocf_cleaner_t c);
};

/**
 * @brief OCF context specific operation
 */
struct ocf_ctx_ops {
	/* Context data operations */
	struct ocf_data_ops data;

	/* Cleaner operations */
	struct ocf_cleaner_ops cleaner;

	/* Logger operations */
	struct ocf_logger_ops logger;
};

struct ocf_ctx_config {
	/* Context name */
	const char *name;

	/* Context operations */
	const struct ocf_ctx_ops ops;

	/* Context logger priv */
	void *logger_priv;
};

/**
 * @brief Register volume interface
 *
 * @note Type of volume operations is unique and cannot be repeated.
 *
 * @param[in] ctx OCF context
 * @param[in] properties Reference to volume properties
 * @param[in] type_id Type id of volume operations
 *
 * @retval 0 Volume operations registered successfully
 * @retval Non-zero Volume registration failure
 */
int ocf_ctx_register_volume_type(ocf_ctx_t ctx, uint8_t type_id,
		const struct ocf_volume_properties *properties);

/**
 * @brief Unregister volume interface
 *
 * @param[in] ctx OCF context
 * @param[in] type_id Type id of volume operations
 */
void ocf_ctx_unregister_volume_type(ocf_ctx_t ctx, uint8_t type_id);

/**
 * @brief Get volume type operations by type id
 *
 * @param[in] ctx OCF context
 * @param[in] type_id Type id of volume operations which were registered
 *
 * @return Volume type
 * @retval NULL When volume operations were not registered
 * for requested type
 */
ocf_volume_type_t ocf_ctx_get_volume_type(ocf_ctx_t ctx, uint8_t type_id);

/**
 * @brief Get volume type id by type
 *
 * @param[in] ctx OCF context
 * @param[in] type Type of volume operations which were registered
 *
 * @return Volume type id
 * @retval -1 When volume operations were not registered
 * for requested type
 */
int ocf_ctx_get_volume_type_id(ocf_ctx_t ctx, ocf_volume_type_t type);

/**
 * @brief Create volume of given type
 *
 * @param[in] ctx handle to object designating ocf context
 * @param[out] volume volume handle
 * @param[in] uuid OCF volume UUID
 * @param[in] type_id cache/core volume type id
 *
 * @return Zero when success, othewise en error
 */

int ocf_ctx_volume_create(ocf_ctx_t ctx, ocf_volume_t *volume,
		struct ocf_volume_uuid *uuid, uint8_t type_id);

/**
 * @brief Create and initialize OCF context
 *
 * @param[out] ctx OCF context
 * @param[in] ops OCF context operations
 *
 * @return Zero when success, otherwise an error
 */
int ocf_ctx_create(ocf_ctx_t *ctx, const struct ocf_ctx_config *cfg);

/**
 * @brief Increase reference counter of ctx
 *
 * @param[in] ctx OCF context
 */
void ocf_ctx_get(ocf_ctx_t ctx);

/**
 * @brief Decrease reference counter of ctx
 *
 * @param[in] ctx OCF context
 */
void ocf_ctx_put(ocf_ctx_t ctx);

#endif /* __OCF_CTX_H__ */
