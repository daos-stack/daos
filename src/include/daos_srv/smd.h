/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * Per-server metadata
 */

#ifndef __SMD_H__
#define __SMD_H__

#include <uuid/uuid.h>
#include <gurt/common.h>
#include <gurt/list.h>

enum smd_dev_state {
	SMD_DEV_NORMAL	= 0,
	SMD_DEV_FAULTY,
};

struct smd_dev_info {
	d_list_t		 sdi_link;
	uuid_t			 sdi_id;
	enum smd_dev_state	 sdi_state;
	uint32_t		 sdi_tgt_cnt;
	int			*sdi_tgts;
};

struct smd_pool_info {
	d_list_t	 spi_link;
	uuid_t		 spi_id;
	uint64_t	 spi_blob_sz;
	uint32_t	 spi_tgt_cnt;
	int		*spi_tgts;
	uint64_t	*spi_blobs;
};

static inline void
smd_free_dev_info(struct smd_dev_info *dev_info)
{
	if (dev_info->sdi_tgts != NULL)
		D_FREE(dev_info->sdi_tgts);
	D_FREE(dev_info);
}

static inline void
smd_free_pool_info(struct smd_pool_info *pool_info)
{
	if (pool_info->spi_tgts != NULL)
		D_FREE(pool_info->spi_tgts);
	if (pool_info->spi_blobs != NULL)
		D_FREE(pool_info->spi_blobs);
	D_FREE(pool_info);
}

/**
 * Initialize SMD store, create store if it's not existing
 *
 * \param [IN]	path	Path of SMD store
 *
 * \return		Zero on success, negative value on error
 */
int smd_init(const char *path);

/**
 * Finalize SMD store
 */
void smd_fini(void);

/**
 * Destroy SMD store
 *
 * \param [IN]	path	Path of SMD store
 */
int smd_store_destroy(const char *path);

/**
 * Assign a NVMe device to a target (VOS xstream)
 *
 * \param [IN]	dev_id	NVMe device ID
 * \param [IN]	tgt_id	Target ID
 *
 * \return		Zero on success, negative value on error
 */
int smd_dev_assign(uuid_t dev_id, int tgt_id);

/**
 * Unassign a NVMe device from a target (VOS xstream)
 *
 * \param [IN]	dev_id	NVMe device ID
 * \param [IN]	tgt_id	Target ID
 *
 * \return		Zero on success, negative value on error
 */
int smd_dev_unassign(uuid_t dev_id, int tgt_id);

/**
 * Set a NVMe device state
 *
 * \param [IN]	dev_id	NVMe device ID
 * \param [IN]	state	Device state
 *
 * \return		Zero on success, negative value on error
 */
int smd_dev_set_state(uuid_t dev_id, enum smd_dev_state state);

/**
 * Get NVMe device info, caller is responsible to free @dev_info
 *
 * \param [IN]	dev_id		NVMe device ID
 * \param [OUT]	dev_info	Device info
 *
 * \return			Zero on success, negative value on error
 */
int smd_dev_get_by_id(uuid_t dev_id, struct smd_dev_info **dev_info);

/**
 * Get NVMe device info by target ID, caller is responsible to free @dev_info
 *
 * \param [IN]	tgt_id		Target ID
 * \param [OUT]	dev_info	Device info
 *
 * \return			Zero on success, negative value on error
 */
int smd_dev_get_by_tgt(int tgt_id, struct smd_dev_info **dev_info);

/**
 * List all NVMe devices, caller is responsible to free list items
 *
 * \param [OUT]	dev_list	Device list
 * \param [OUT] dev_cnt		Number of devices in list
 *
 * \return			Zero on success, negative value on error
 */
int smd_dev_list(d_list_t *dev_list, int *dev_cnt);

/**
 * Replace an old device ID with new device ID, change it's state from
 * FAULTY to NORMAL, update pool info according to @pool_list.
 *
 * \param [IN] old_id		Old device ID
 * \param [IN] new_id		New device ID
 * \param [IN] pool_list	List of pools to be updated
 *
 * \return			Zero on success, negative value on error
 */
int smd_dev_replace(uuid_t old_id, uuid_t new_id, d_list_t *pool_list);

/**
 * Assign a blob to a VOS pool target
 *
 * \param [IN]	pool_id		Pool UUID
 * \param [IN]	tgt_id		Target ID
 * \param [IN]	blob_id		Blob ID
 * \param [IN]	blob_sz		Blob size
 *
 * \return			Zero on success, negative value on error
 */
int smd_pool_assign(uuid_t pool_id, int tgt_id, uint64_t blob_id,
		    uint64_t blob_sz);

/**
 * Unassign a VOS pool target
 *
 * \param [IN]	pool_id		Pool UUID
 * \param [IN]	tgt_id		Target ID
 *
 * \return			Zero on success, negative value on error
 */
int smd_pool_unassign(uuid_t pool_id, int tgt_id);

/**
 * Get pool info, caller is responsible to free @pool_info
 *
 * \param [IN]	pool_id		Pool UUID
 * \param [OUT]	pool_info	Pool info
 *
 * \return			Zero on success, negative value on error
 */
int smd_pool_get(uuid_t pool_id, struct smd_pool_info **pool_info);

/**
 * Get blob ID mapped to pool:target
 *
 * \param [IN]	pool_id		Pool UUID
 * \param [IN]	tgt_id		Target ID
 * \param [OUT]	blob_id		Blob ID
 *
 * \return			Zero on success, negative value on error
 */
int smd_pool_get_blob(uuid_t pool_id, int tgt_id, uint64_t *blob_id);

/**
 * Get pool info, caller is responsible to free list items
 *
 * \param [OUT]	pool_list	Pool list
 * \param [OUT]	pool_cnt	Number of pools in list
 *
 * \return			Zero on success, negative value on error
 */
int smd_pool_list(d_list_t *pool_list, int *pool_cnt);

/**
 * Convert device state to human-readable string
 *
 * \param [IN]	state		Device state
 *
 * \return			Static string representing enum value
 */
char *smd_state_enum_to_str(enum smd_dev_state state);

#endif /* __SMD_H__ */
