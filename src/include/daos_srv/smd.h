/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Per-server metadata
 */

#ifndef __SMD_H__
#define __SMD_H__

#include <uuid/uuid.h>
#include <gurt/common.h>
#include <gurt/list.h>
#include <daos/common.h>
#include <daos_types.h>
#include <daos_srv/daos_engine.h>

enum smd_dev_state {
	SMD_DEV_NORMAL = 0,
	SMD_DEV_FAULTY,
};

struct smd_dev_info {
	d_list_t           sdi_link;
	uuid_t             sdi_id;
	enum smd_dev_state sdi_state;
	uint32_t           sdi_tgt_cnt;
	int               *sdi_tgts;
};

struct smd_pool_info {
	d_list_t  spi_link;
	uuid_t    spi_id;
	uint64_t  spi_blob_sz;
	uint32_t  spi_tgt_cnt;
	int      *spi_tgts;
	uint64_t *spi_blobs;
};

/**
 * Initialize SMD store, create store if it's not existing
 *
 * \param [IN]	db	system database
 *
 * \return		Zero on success, negative value on error
 */
int
smd_init(struct sys_db *db);

/**
 * Finalize SMD store
 */
void
smd_fini(void);

/**
 * Assign a NVMe device to a target (VOS xstream)
 *
 * \param [IN]	dev_id	NVMe device ID
 * \param [IN]	tgt_id	Target ID
 *
 * \return		Zero on success, negative value on error
 */
int
smd_dev_add_tgt(uuid_t dev_id, uint32_t tgt_id);

/**
 * Unassign a NVMe device from a target (VOS xstream)
 *
 * \param [IN]	dev_id	NVMe device ID
 * \param [IN]	tgt_id	Target ID
 *
 * \return		Zero on success, negative value on error
 */
int
smd_dev_del_tgt(uuid_t dev_id, uint32_t tgt_id);

/**
 * Set a NVMe device state
 *
 * \param [IN]	dev_id	NVMe device ID
 * \param [IN]	state	Device state
 *
 * \return		Zero on success, negative value on error
 */
int
smd_dev_set_state(uuid_t dev_id, enum smd_dev_state state);

/**
 * Get NVMe device info, caller is responsible to free @dev_info
 *
 * \param [IN]	dev_id		NVMe device ID
 * \param [OUT]	dev_info	Device info
 *
 * \return			Zero on success, negative value on error
 */
int
smd_dev_get_by_id(uuid_t dev_id, struct smd_dev_info **dev_info);

/**
 * Get NVMe device info by target ID, caller is responsible to free @dev_info
 *
 * \param [IN]	tgt_id		Target ID
 * \param [OUT]	dev_info	Device info
 *
 * \return			Zero on success, negative value on error
 */
int
smd_dev_get_by_tgt(uint32_t tgt_id, struct smd_dev_info **dev_info);

/**
 * List all NVMe devices, caller is responsible to free list items
 *
 * \param [OUT]	dev_list	Device list
 * \param [OUT] dev_cnt		Number of devices in list
 *
 * \return			Zero on success, negative value on error
 */
int
smd_dev_list(d_list_t *dev_list, int *dev_cnt);

static inline void
smd_dev_free_info(struct smd_dev_info *dev_info)
{
	if (dev_info->sdi_tgts != NULL)
		D_FREE(dev_info->sdi_tgts);
	D_FREE(dev_info);
}

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
int
smd_dev_replace(uuid_t old_id, uuid_t new_id, d_list_t *pool_list);

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
int
smd_pool_add_tgt(uuid_t pool_id, uint32_t tgt_id, uint64_t blob_id, uint64_t blob_sz);

/**
 * Unassign a VOS pool target
 *
 * \param [IN]	pool_id		Pool UUID
 * \param [IN]	tgt_id		Target ID
 *
 * \return			Zero on success, negative value on error
 */
int
smd_pool_del_tgt(uuid_t pool_id, uint32_t tgt_id);

/**
 * Get pool info, caller is responsible to free @pool_info
 *
 * \param [IN]	pool_id		Pool UUID
 * \param [OUT]	pool_info	Pool info
 *
 * \return			Zero on success, negative value on error
 */
int
smd_pool_get_info(uuid_t pool_id, struct smd_pool_info **pool_info);

/**
 * Get blob ID mapped to pool:target
 *
 * \param [IN]	pool_id		Pool UUID
 * \param [IN]	tgt_id		Target ID
 * \param [OUT]	blob_id		Blob ID
 *
 * \return			Zero on success, negative value on error
 */
int
smd_pool_get_blob(uuid_t pool_id, uint32_t tgt_id, uint64_t *blob_id);

/**
 * Get pool info, caller is responsible to free list items
 *
 * \param [OUT]	pool_list	Pool list
 * \param [OUT]	pool_cnt	Number of pools in list
 *
 * \return			Zero on success, negative value on error
 */
int
smd_pool_list(d_list_t *pool_list, int *pool_cnt);

/**
 * Convert device state to human-readable string
 *
 * \param [IN]	state		Device state
 *
 * \return			Static string representing enum value
 */
char *
smd_dev_stat2str(enum smd_dev_state state);

static inline void
smd_pool_free_info(struct smd_pool_info *pool_info)
{
	if (pool_info->spi_blobs != NULL)
		D_FREE(pool_info->spi_blobs);
	if (pool_info->spi_tgts != NULL)
		D_FREE(pool_info->spi_tgts);
	D_FREE(pool_info);
}

#endif /* __SMD_H__ */
