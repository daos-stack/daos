/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */


#ifndef __OCF_IO_H__
#define __OCF_IO_H__

#include "ocf_types.h"

/**
 * @file
 * @brief OCF IO definitions
 */

struct ocf_io;

/**
 * @brief OCF IO start
 *
 * @note OCF IO start notification callback
 *
 * @param[in] io OCF IO being started
 */
typedef void (*ocf_start_io_t)(struct ocf_io *io);

/**
 * @brief OCF IO handle
 *
 * @note OCF IO handle callback
 *
 * @param[in] io OCF IO to handle
 */
typedef void (*ocf_handle_io_t)(struct ocf_io *io, void *opaque);

/**
 * @brief OCF IO completion
 *
 * @note Completion function for OCF IO
 *
 * @param[in] io OCF IO being completed
 * @param[in] error Completion status code
 */
typedef void (*ocf_end_io_t)(struct ocf_io *io, int error);

/**
 * @brief OCF IO main structure
 */
struct ocf_io {
	/**
	 * @brief OCF IO destination address
	 */
	uint64_t addr;

	/**
	 * @brief OCF IO flags
	 */
	uint64_t flags;

	/**
	 * @brief OCF IO size in bytes
	 */
	uint32_t bytes;

	/**
	 * @brief OCF IO destination class
	 */
	uint32_t io_class;

	/**
	 * @brief OCF IO direction
	 */
	uint32_t dir;

	/**
	 * @brief Queue handle
	 */
	ocf_queue_t io_queue;

	/**
	 * @brief OCF IO start function
	 */
	ocf_start_io_t start;

	/**
	 * @brief OCF IO private 1
	 */
	void *priv1;

	/**
	 * @brief OCF IO private 2
	 */
	void *priv2;

	/**
	 * @brief OCF IO handle function
	 */
	ocf_handle_io_t handle;

	/**
	 * @brief OCF IO completion function
	 */
	ocf_end_io_t end;
};

/**
 * @brief OCF IO operations set structure
 */
struct ocf_io_ops {
	/**
	 * @brief Set up data vector in OCF IO
	 *
	 * @param[in] io OCF IO to set up
	 * @param[in] data Source context data
	 * @param[in] offset Data offset in source context data
	 *
	 * @retval 0 Data set up successfully
	 * @retval Non-zero Data set up failure
	 */
	int (*set_data)(struct ocf_io *io, ctx_data_t *data,
			uint32_t offset);

	/**
	 * @brief Get context data from OCF IO
	 *
	 * @param[in] io OCF IO to get data
	 *
	 * @return Data vector from IO
	 */
	ctx_data_t *(*get_data)(struct ocf_io *io);
};

/**
 * @brief Get IO private context structure
 *
 * @param[in] io OCF IO
 *
 * @return IO private context structure
 */
void *ocf_io_get_priv(struct ocf_io *io);

/**
 * @brief Increase reference counter in OCF IO
 *
 * @note Wrapper for get IO operation
 *
 * @param[in] io OCF IO
 */
void ocf_io_get(struct ocf_io *io);

/**
 * @brief Decrease reference counter in OCF IO
 *
 * @note If IO don't have any reference - deallocate it
 *
 * @param[in] io OCF IO
 */
void ocf_io_put(struct ocf_io *io);

/**
 * @brief Set OCF IO completion function
 *
 * @param[in] io OCF IO
 * @param[in] context Context for completion function
 * @param[in] fn Completion function
 */
static inline void ocf_io_set_cmpl(struct ocf_io *io, void *context,
		void *context2, ocf_end_io_t fn)
{
	io->priv1 = context;
	io->priv2 = context2;
	io->end = fn;
}

/**
 * @brief Set OCF IO start function
 *
 * @param[in] io OCF IO
 * @param[in] fn Start callback function
 */
static inline void ocf_io_set_start(struct ocf_io *io, ocf_start_io_t fn)
{
	io->start = fn;
}

/**
 * @brief Set OCF IO handle function
 *
 * @param[in] io OCF IO
 * @param[in] fn Handle callback function
 */
static inline void ocf_io_set_handle(struct ocf_io *io, ocf_handle_io_t fn)
{
	io->handle = fn;
}

/**
 * @brief Set up data vector in OCF IO
 *
 * @note Wrapper for set up data vector function
 *
 * @param[in] io OCF IO to set up
 * @param[in] data Source data vector
 * @param[in] offset Data offset in source data vector
 *
 * @retval 0 Data set up successfully
 * @retval Non-zero Data set up failure
 */
int ocf_io_set_data(struct ocf_io *io, ctx_data_t *data, uint32_t offset);

/**
 * @brief Get data vector from OCF IO
 *
 * @note Wrapper for get data vector function
 *
 * @param[in] io OCF IO to get data
 *
 * @return Data vector from IO
 */
ctx_data_t *ocf_io_get_data(struct ocf_io *io);

/**
 * @brief Handle IO in cache engine
 *
 * @param[in] io OCF IO to be handled
 * @param[in] opaque OCF opaque
 */
void ocf_io_handle(struct ocf_io *io, void *opaque);

/**
 * @brief Get volume associated with io
 *
 * @param[in] io OCF IO to be handled
 */
ocf_volume_t ocf_io_get_volume(struct ocf_io *io);

#endif /* __OCF_IO_H__ */
