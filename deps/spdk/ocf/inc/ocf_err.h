/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __OCF_ERR_H__
#define __OCF_ERR_H__

/**
 * @file
 * @brief OCF error codes definitions
 */

/**
 * @brief OCF error enumerator
 */
typedef enum {
	OCF_ERR_MIN = 1000000,

	/** Invalid input parameter value */
	OCF_ERR_INVAL = OCF_ERR_MIN,

	/** Try again */
	OCF_ERR_AGAIN,

	/** Operation interrupted */
	OCF_ERR_INTR,

	/** Operation not supported */
	OCF_ERR_NOT_SUPP,

	/** Out of memory */
	OCF_ERR_NO_MEM,

	/** Lock not acquired */
	OCF_ERR_NO_LOCK,

	/** Metadata version mismatch */
	OCF_ERR_METADATA_VER,

	/** No metadata found on device */
	OCF_ERR_NO_METADATA,

	/** Cache metadata found on device */
	OCF_ERR_METADATA_FOUND,

	/** Invalid volume type */
	OCF_ERR_INVAL_VOLUME_TYPE,

	/** Unknown error occurred */
	OCF_ERR_UNKNOWN,

	/** To many caches */
	OCF_ERR_TOO_MANY_CACHES,

	/** Not enough RAM to start cache */
	OCF_ERR_NO_FREE_RAM,

	/** Start cache failure */
	OCF_ERR_START_CACHE_FAIL,

	/** Cache ID/name does not exist */
	OCF_ERR_CACHE_NOT_EXIST,

	/** Core ID/name does not exist */
	OCF_ERR_CORE_NOT_EXIST,

	/** Cache ID/name already exists */
	OCF_ERR_CACHE_EXIST,

	/** Core ID/name already exists */
	OCF_ERR_CORE_EXIST,

	/** Too many core devices in cache */
	OCF_ERR_TOO_MANY_CORES,

	/** Core device not available */
	OCF_ERR_CORE_NOT_AVAIL,

	/** Cannot open device exclusively*/
	OCF_ERR_NOT_OPEN_EXC,

	/** Cache device not available */
	OCF_ERR_CACHE_NOT_AVAIL,

	/** IO Class does not exist */
	OCF_ERR_IO_CLASS_NOT_EXIST,

	/** IO Error */
	OCF_ERR_IO,

	/** Error while writing to cache device */
	OCF_ERR_WRITE_CACHE,

	/** Error while writing to core device */
	OCF_ERR_WRITE_CORE,

	/*!< Dirty shutdown */
	OCF_ERR_DIRTY_SHUTDOWN,

	/** Cache contains dirty data */
	OCF_ERR_DIRTY_EXISTS,

	/** Flushing of core interrupted */
	OCF_ERR_FLUSHING_INTERRUPTED,

	/** Another flushing operation in progress */
	OCF_ERR_FLUSH_IN_PROGRESS,

	/** Adding core to core pool failed */
	OCF_ERR_CANNOT_ADD_CORE_TO_POOL,

	/** Cache is in incomplete state */
	OCF_ERR_CACHE_IN_INCOMPLETE_STATE,

	/** Core device is in inactive state */
	OCF_ERR_CORE_IN_INACTIVE_STATE,

	/** Invalid cache mode */
	OCF_ERR_INVALID_CACHE_MODE,

	/** Invalid cache line size */
	OCF_ERR_INVALID_CACHE_LINE_SIZE,

	/** Invalid cache name loaded */
	OCF_ERR_CACHE_NAME_MISMATCH,

	/** Device does not meet requirements */
	OCF_ERR_INVAL_CACHE_DEV,

	/** Core with the uuid already exists */
	OCF_ERR_CORE_UUID_EXISTS,

	OCF_ERR_MAX = OCF_ERR_CORE_UUID_EXISTS,
} ocf_error_t;

#endif /* __OCF_ERR_H__ */
