/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __OCF_VOLUME_H__
#define __OCF_VOLUME_H__

/**
 * @file
 * @brief OCF volume API
 */

#include "ocf_types.h"
#include "ocf_env_headers.h"
#include "ocf/ocf_err.h"

struct ocf_io;

/**
 * @brief OCF volume UUID maximum allowed size
 */
#define OCF_VOLUME_UUID_MAX_SIZE	(4096UL - sizeof(uint32_t))

/**
 * @brief OCF volume UUID
 */
struct ocf_volume_uuid {
	size_t size;
		/*!< UUID data size */

	void *data;
		/*!< UUID data content */
};

/**
 * @brief This structure describes volume capabilities
 */
struct ocf_volume_caps {
	uint32_t atomic_writes : 1;
		/*!< Volume supports atomic writes */
};

/**
 * @brief OCF volume interface declaration
 */
struct ocf_volume_ops {
	/**
	 * @brief Submit IO on this volume
	 *
	 * @param[in] io IO to be submitted
	 */
	void (*submit_io)(struct ocf_io *io);

	/**
	 * @brief Submit IO with flush command
	 *
	 * @param[in] io IO to be submitted
	 */
	void (*submit_flush)(struct ocf_io *io);

	/**
	 * @brief Submit IO with metadata
	 *
	 * @param[in] io IO to be submitted
	 */
	void (*submit_metadata)(struct ocf_io *io);

	/**
	 * @brief Submit IO with discard command
	 *
	 * @param[in] io IO to be submitted
	 */
	void (*submit_discard)(struct ocf_io *io);

	/**
	 * @brief Submit operation to write zeroes to target address (including
	 *        metadata extended LBAs in atomic mode)
	 *
	 * @param[in] io IO description (addr, size)
	 */
	void (*submit_write_zeroes)(struct ocf_io *io);

	/**
	 * @brief Open volume
	 *
	 * @note This function performs volume initialization and should
	 *	 be called before any other operation on volume
	 *
	 * @param[in] volume Volume
	 * @param[in] volume_params optional volume parameters, opaque to OCF
	 *
	 * @return Zero on success, otherwise error code
	 */
	int (*open)(ocf_volume_t volume, void *volume_params);

	/**
	 * @brief Close volume
	 *
	 * @param[in] volume Volume
	 */
	void (*close)(ocf_volume_t volume);

	/**
	 * @brief Get volume length
	 *
	 * @param[in] volume Volume
	 *
	 * @return Volume length in bytes
	 */
	uint64_t (*get_length)(ocf_volume_t volume);

	/**
	 * @brief Get maximum io size
	 *
	 * @param[in] volume Volume
	 *
	 * @return Maximum io size in bytes
	 */
	unsigned int (*get_max_io_size)(ocf_volume_t volume);
};

/**
 * @brief This structure describes volume properties
 */
struct ocf_volume_properties {
	const char *name;
		/*!< The name of volume operations */

	uint32_t io_priv_size;
		/*!< Size of io private context structure */

	uint32_t volume_priv_size;
		/*!< Size of volume private context structure */

	struct ocf_volume_caps caps;
		/*!< Volume capabilities */

	struct ocf_io_ops io_ops;
		/*!< IO operations */

	void (*deinit)(void);
		/*!< Deinitialize volume type */

	struct ocf_volume_ops ops;
		/*!< Volume operations */
};

/**
 * @brief Initialize UUID from string
 *
 * @param[in] uuid UUID to be initialized
 * @param[in] str NULL-terminated string
 *
 * @return Zero when success, othewise error
 */
int ocf_uuid_set_str(ocf_uuid_t uuid, char *str);

/**
 * @brief Obtain string from UUID
 * @param[in] uuid pointer to UUID
 * @return String contained within UUID
 */
static inline const char *ocf_uuid_to_str(const struct ocf_volume_uuid *uuid)
{
	return (const char *)uuid->data;
}

/**
 * @brief Initialize volume
 *
 * @param[in] volume volume handle
 * @param[in] type cache/core volume type
 * @param[in] uuid OCF volume UUID
 * @param[in] uuid_copy crate copy of uuid data
 *
 * @return Zero when success, othewise error
 */
int ocf_volume_init(ocf_volume_t volume, ocf_volume_type_t type,
		struct ocf_volume_uuid *uuid, bool uuid_copy);

/**
 * @brief Deinitialize volume
 *
 * @param[in] volume volume handle
 */
void ocf_volume_deinit(ocf_volume_t volume);

/**
 * @brief Allocate and initialize volume
 *
 * @param[out] volume pointer to volume handle
 * @param[in] type cache/core volume type
 * @param[in] uuid OCF volume UUID
 *
 * @return Zero when success, othewise en error
 */
int ocf_volume_create(ocf_volume_t *volume, ocf_volume_type_t type,
		struct ocf_volume_uuid *uuid);

/**
 * @brief Deinitialize and free volume
 *
 * @param[in] volume volume handle
 */
void ocf_volume_destroy(ocf_volume_t volume);

/**
 * @brief Get volume type
 *
 * @param[in] volume Volume
 *
 * @return Volume type
 */
ocf_volume_type_t ocf_volume_get_type(ocf_volume_t volume);

/**
 * @brief Get volume UUID
 *
 * @param[in] volume Volume
 *
 * @return UUID of volume
 */
const struct ocf_volume_uuid *ocf_volume_get_uuid(ocf_volume_t volume);

/**
 * @brief Get private context of volume
 *
 * @param[in] volume Volume
 *
 * @return Volume private context
 */
void *ocf_volume_get_priv(ocf_volume_t volume);

/**
 * @brief Get cache handle for given volume
 *
 * @param volume volume handle
 *
 * @return Handle to cache for which volume belongs to
 */
ocf_cache_t ocf_volume_get_cache(ocf_volume_t volume);

/**
 * @brief Check if volume supports atomic mode
 *
 * @param[in] volume Volume
 *
 * @return Non-zero value if volume is atomic, otherwise zero
 */
int ocf_volume_is_atomic(ocf_volume_t volume);

/**
 * @brief Allocate new io
 *
 * @param[in] volume Volume
 * @param[in] queue IO queue handle
 * @param[in] addr OCF IO destination address
 * @param[in] bytes OCF IO size in bytes
 * @param[in] dir OCF IO direction
 * @param[in] io_class OCF IO destination class
 * @param[in] flags OCF IO flags
 *
 * @return ocf_io on success atomic, otherwise NULL
 */
struct ocf_io *ocf_volume_new_io(ocf_volume_t volume, ocf_queue_t queue,
		uint64_t addr, uint32_t bytes, uint32_t dir,
		uint32_t io_class, uint64_t flags);


/**
 * @brief Submit io to volume
 *
 * @param[in] io IO
 */
void ocf_volume_submit_io(struct ocf_io *io);

/**
 * @brief Submit flush to volume
 *
 * @param[in] io IO
 */
void ocf_volume_submit_flush(struct ocf_io *io);

/**
 * @brief Submit discard to volume
 *
 * @param[in] io IO
 */
void ocf_volume_submit_discard(struct ocf_io *io);

/**
 * @brief Open volume
 *
 * @param[in] volume Volume
 * @param[in] volume_params Opaque volume params
 *
 * @return Zero when success, othewise en error
 */
int ocf_volume_open(ocf_volume_t volume, void *volume_params);

/**
 * @brief Get volume max io size
 *
 * @param[in] volume Volume
 */
void ocf_volume_close(ocf_volume_t volume);

/**
 * @brief Get volume max io size
 *
 * @param[in] volume Volume
 *
 * @return Volume max io size in bytes
 */
unsigned int ocf_volume_get_max_io_size(ocf_volume_t volume);

/**
 * @brief Get volume length
 *
 * @param[in] volume Volume
 *
 * @return Length of volume in bytes
 */
uint64_t ocf_volume_get_length(ocf_volume_t volume);

#endif /* __OCF_VOLUME_H__ */
