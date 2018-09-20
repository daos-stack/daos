/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
/**
 * Server-side metadata API offering the following functionalities:
 * - per-server persistent metadata for NVMe SSD devices.
 */

#ifndef __SMD_H__
#define __SMD_H__

#include <daos_types.h>

#define	DEV_MAX_STREAMS 64

/** NVMe metadata table type */
enum smd_device_status {
	SMD_NVME_UNKNOWN = 5000,
	/** NVMe device normal */
	SMD_NVME_NORMAL,
	/** NVMe device in restore mode */
	SMD_NVME_RESTORE,
	/** NMVe device faulty */
	SMD_NVME_FAULT,
};

struct smd_nvme_stream_bond {
	/** stream ID */
	int	nsm_stream_id;
	/** Device ID of the NVMe SSD device */
	uuid_t	nsm_dev_id;
};

struct smd_nvme_device_info {
	/** Device ID of the NVMe SSD device */
	uuid_t			ndi_dev_id;
	/** Status of this device */
	enum smd_device_status	ndi_status;
	/** Number of streams bound to this device */
	int			ndi_xs_cnt;
	/** Stream ID(s) bound to this device */
	int			ndi_xstreams[DEV_MAX_STREAMS];
};

struct smd_nvme_pool_info {
	/** Pool UUID */
	uuid_t		npi_pool_uuid;
	/** Stream ID mapped to this pool */
	int		npi_stream_id;
	/** NVMe Blob ID */
	uint64_t	npi_blob_id;
};

static inline void
smd_nvme_set_stream_bond(int stream_id, uuid_t uid,
			 struct smd_nvme_stream_bond *mapping)
{
	mapping->nsm_stream_id = stream_id;
	uuid_copy(mapping->nsm_dev_id, uid);
}

static inline void
smd_nvme_set_device_info(uuid_t uid, int status,
			 struct smd_nvme_device_info *info)
{
	uuid_copy(info->ndi_dev_id, uid);
	info->ndi_status = status;
}

static inline void
smd_nvme_set_pool_info(uuid_t pid, int stream_id, uint64_t blob_id,
		       struct smd_nvme_pool_info *pinfo)
{
	uuid_copy(pinfo->npi_pool_uuid, pid);
	pinfo->npi_stream_id = stream_id;
	pinfo->npi_blob_id = blob_id;
}

/**
 * Initialize SMD library
 * Creates SMD pool file if it doesn't exist.
 *
 * \param [IN]	 path	Path of directory for creating
 *				SMD pool file
 * \param [IN]	 fname	Optional pool file-name (d: nvme-meta)
 * \param [IN]	 size	Optional pool file-size (d: 256M)
 *
 * \return		 Zero on success, negative value on error
 */
int smd_create_initialize(const char *path, const char *fname,
			  daos_size_t size);

/**
 * Global finalization for Server Metadata (SMD) library.
 *
 * \return		 Zero on success, negative value on error
 */
void smd_fini(void);

/**
 * Remove Server Metadata Library completely
 */
void smd_remove(const char *path, const char *file);

/**
 * Server NMVe add Stream to Device mapping SMD stream table
 * Check for the device entry in device table and
 * Adds a device entry to the SMD device table if device
 * is not found.
 *
 * \param	[IN]	stream_bond	SMD NVMe device/stream
 *					mapping
 *
 * \returns				Zero on success,
 *					negative value on error
 */
int smd_nvme_add_stream_bond(struct smd_nvme_stream_bond *mapping);

/**
 * Server NVMe get device corresponding to a stream
 *
 * \param	[IN]	stream_id	SMD NVMe stream ID
 * \param	[OUT]	mapping		SMD mapping information
 *
 * \returns				Zero on success,
 *					negative value on error
 */
int smd_nvme_get_stream_bond(int stream_id,
			     struct smd_nvme_stream_bond *mapping);
/**
 * Server NVMe set device status will update the status of the NVMe device
 * in the SMD device table, if the device is not found it adds a new entry
 *
 * \param	[IN]	device_id	UUID of device
 * \param	[IN]	status		Status of device
 *
 * \returns				Zero on success,
 *					negative value on error
 */
int smd_nvme_set_device_status(uuid_t device_id, enum smd_device_status status);

/**
 * Server NVMe get device info using device ID
 *
 * \param	[IN]	 device_id	NVMe device UUID
 * \param	[OUT]	 info		SMD store NVMe device info
 *
 * \returns				Zero on success, negative
 *					value on error
 */
int smd_nvme_get_device(uuid_t device_id,
			struct smd_nvme_device_info *info);

/**
 * Server NMVe add pool metadata to SMD store
 *
 * \param	[IN]	pool	SMD store NVMe pool table info
 *
 * \returns			Zero on success, negative value
 *				on error
 */
int smd_nvme_add_pool(struct smd_nvme_pool_info *pool);

/**
 * Server NMVe get pool metadata to SMD store
 *
 * \param	[IN]	 pool_id	Pool UUID to search
 * \param	[IN]	 stream_id	Xstream ID to search
 * \param	[IN/OUT] info		SMD store NVMe pool table info
 *
 * \returns				Zero on success, negative value
 *					on error
 */
int smd_nvme_get_pool(uuid_t pool_id, int stream_id,
		      struct smd_nvme_pool_info *info);

/** TODO: Add iterator API to list devices and pools */
#endif /* __SMD_H__ */
