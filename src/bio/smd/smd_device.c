/**
 * (C) Copyright 2018-2023 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(bio)

#include <daos/common.h>
#include <daos/dtx.h>
#include "smd_internal.h"

struct smd_device {
	enum smd_dev_state	sd_state;
	uint32_t		sd_tgt_cnt;
	uint32_t		sd_tgts[SMD_MAX_TGT_CNT];
};

int
smd_dev_add_tgt(uuid_t dev_id, uint32_t tgt_id, enum smd_dev_type st)
{
	struct smd_device	dev;
	struct d_uuid		id_org;
	struct d_uuid		id;
	int			rc;

	uuid_copy(id.uuid, dev_id);
	smd_db_lock();

	/* Check if the target is already bound to a device */
	rc = smd_db_fetch(TABLE_TGTS[st], &tgt_id, sizeof(tgt_id),
			  &id_org, sizeof(id_org));
	if (rc == 0) {
		D_ERROR("Target %d is already bound to dev "DF_UUID"\n",
			tgt_id, DP_UUID(&id_org.uuid));
		rc = -DER_EXIST;
		goto out;
	} else if (rc != -DER_NONEXIST) {
		D_ERROR("Get target %d failed. "DF_RC"\n", tgt_id, DP_RC(rc));
		goto out;
	}

	/* Fetch device if it's already existing */
	rc = smd_db_fetch(TABLE_DEV, &id, sizeof(id), &dev, sizeof(dev));
	if (rc == 0) {
		if (dev.sd_tgt_cnt >= SMD_MAX_TGT_CNT) {
			D_ERROR("Dev "DF_UUID" is assigned to too many "
				"targets (%d)\n", DP_UUID(&id.uuid),
				dev.sd_tgt_cnt);
			rc = -DER_OVERFLOW;
			goto out;
		}
		dev.sd_tgts[dev.sd_tgt_cnt] = tgt_id;
		dev.sd_tgt_cnt += 1;
	} else if (rc == -DER_NONEXIST) {
		dev.sd_state	= SMD_DEV_NORMAL;
		dev.sd_tgt_cnt	= 1;
		dev.sd_tgts[0]	= tgt_id;
	} else {
		D_ERROR("Fetch dev "DF_UUID" failed. "DF_RC"\n",
			DP_UUID(&id.uuid), DP_RC(rc));
		goto out;
	}

	/* Update device and target tables in same transaction */
	rc = smd_db_tx_begin();
	if (rc)
		goto out;

	rc = smd_db_upsert(TABLE_DEV, &id, sizeof(id), &dev, sizeof(dev));
	if (rc) {
		D_ERROR("Fetch dev "DF_UUID" failed. "DF_RC"\n",
			DP_UUID(&id.uuid), DP_RC(rc));
		goto out_tx;
	}

	rc = smd_db_upsert(TABLE_TGTS[st], &tgt_id, sizeof(tgt_id), &id, sizeof(id));
	if (rc) {
		D_ERROR("Update target %d failed: "DF_RC"\n",
			tgt_id, DP_RC(rc));
		goto out_tx;
	}
out_tx:
	rc = smd_db_tx_end(rc);
out:
	smd_db_unlock();
	return rc;
}

int
smd_dev_del_tgt(uuid_t dev_id, uint32_t tgt_id, enum smd_dev_type st)
{
	return -DER_NOSYS;
}

char *
smd_dev_stat2str(enum smd_dev_state state)
{
	switch (state) {
	default:
		return "UNKNOWN";
	case SMD_DEV_NORMAL:
		return "NORMAL";
	case SMD_DEV_FAULTY:
		return "FAULTY";
	}
}

int
smd_dev_set_state(uuid_t dev_id, enum smd_dev_state state)
{
	struct smd_device	dev;
	struct d_uuid		id;
	int			rc;

	D_ASSERT(state == SMD_DEV_NORMAL || state == SMD_DEV_FAULTY);
	uuid_copy(id.uuid, dev_id);

	smd_db_lock();
	rc = smd_db_fetch(TABLE_DEV, &id, sizeof(id), &dev, sizeof(dev));
	if (rc) {
		D_ERROR("Fetch dev "DF_UUID" failed. "DF_RC"\n",
			DP_UUID(&id.uuid), DP_RC(rc));
		goto out;
	}

	dev.sd_state = state;
	rc = smd_db_upsert(TABLE_DEV, &id, sizeof(id), &dev, sizeof(dev));
	if (rc) {
		D_ERROR("SMD dev "DF_UUID" state set failed. "DF_RC"\n",
			DP_UUID(&id.uuid), DP_RC(rc));
		goto out;
	}
	D_DEBUG(DB_MGMT, "SMD dev "DF_UUID" state set to %s\n",
		DP_UUID(&id.uuid), smd_dev_stat2str(state));
out:
	smd_db_unlock();
	return rc;
}

static struct smd_dev_info *
smd_dev_alloc_info(struct d_uuid *id, struct smd_device *dev)
{
	struct smd_dev_info	*info;
	int			 i;

	D_ALLOC_PTR(info);
	if (info == NULL)
		return NULL;

	D_ALLOC_ARRAY(info->sdi_tgts, SMD_MAX_TGT_CNT);
	if (info->sdi_tgts == NULL) {
		smd_dev_free_info(info);
		return NULL;
	}

	D_INIT_LIST_HEAD(&info->sdi_link);
	uuid_copy(info->sdi_id, id->uuid);
	info->sdi_state	= dev->sd_state;
	info->sdi_tgt_cnt = dev->sd_tgt_cnt;
	for (i = 0; i < info->sdi_tgt_cnt; i++)
		info->sdi_tgts[i] = dev->sd_tgts[i];

	return info;
}

static int
smd_dev_get_info(struct d_uuid *id, struct smd_dev_info **dev_info)
{
	struct smd_dev_info	*info;
	struct smd_device	 dev;
	int			 rc;

	rc = smd_db_fetch(TABLE_DEV, id, sizeof(*id), &dev, sizeof(dev));
	if (rc) {
		DL_CDEBUG(rc != -DER_NONEXIST, DLOG_ERR, DB_MGMT, rc,
			  "Fetch dev " DF_UUID " failed", DP_UUID(&id->uuid));
		return rc;
	}

	info = smd_dev_alloc_info(id, &dev);
	if (info == NULL)
		return -DER_NOMEM;

	*dev_info = info;
	return 0;
}

int
smd_dev_get_by_id(uuid_t dev_id, struct smd_dev_info **dev_info)
{
	struct d_uuid	id;
	int		rc;

	uuid_copy(id.uuid, dev_id);
	smd_db_lock();
	rc = smd_dev_get_info(&id, dev_info);
	smd_db_unlock();
	return rc;
}

int
smd_dev_get_by_tgt(uint32_t tgt_id, enum smd_dev_type st, struct smd_dev_info **dev_info)
{
	struct d_uuid	id;
	int		rc;

	smd_db_lock();
	rc = smd_db_fetch(TABLE_TGTS[st], &tgt_id, sizeof(tgt_id), &id, sizeof(id));
	if (rc) {
		DL_CDEBUG(rc != -DER_NONEXIST, DLOG_ERR, DB_MGMT, rc, "Fetch target %d failed",
			  tgt_id);
		goto out;
	}
	rc = smd_dev_get_info(&id, dev_info);
out:
	smd_db_unlock();
	return rc;
}

static int
smd_dev_list_cb(struct sys_db *db, char *table, d_iov_t *key, void *args)
{
	struct smd_trav_data    *td = args;
	struct smd_dev_info     *info;
	struct smd_device        dev;
	struct d_uuid            id;
	int                      rc;

	D_ASSERT(key->iov_len == sizeof(id));
	id = *(struct d_uuid *)key->iov_buf;
	rc = smd_db_fetch(TABLE_DEV, &id, sizeof(id), &dev, sizeof(dev));
	if (rc)
		return rc;

	info = smd_dev_alloc_info(&id, &dev);
	if (!info)
		return -DER_NOMEM;

	d_list_add_tail(&info->sdi_link, &td->td_list);
	td->td_count++;
	return 0;
}

int
smd_dev_list(d_list_t *dev_list, int *devs)
{
	struct smd_trav_data td;
	int		     rc;

	/* There is no NVMe, smd will not be initialized */
	if (!smd_db_ready())
		return 0;

	td.td_count = 0;
	D_INIT_LIST_HEAD(&td.td_list);

	smd_db_lock();
	rc = smd_db_traverse(TABLE_DEV, smd_dev_list_cb, &td);
	smd_db_unlock();

	if (rc == 0) { /* success */
		*devs = td.td_count;
		d_list_splice_init(&td.td_list, dev_list);
	}

	while (!d_list_empty(&td.td_list)) { /* failure handling */
		struct smd_dev_info	*info;

		info = d_list_entry(td.td_list.next,
				    struct smd_dev_info, sdi_link);
		d_list_del(&info->sdi_link);
		smd_dev_free_info(info);
	}
	return rc;
}

static int
tgt_need_replace(uuid_t old_id, uint32_t tgt_id, unsigned int old_roles, enum smd_dev_type st)
{
	struct d_uuid assigned;
	int           rc;

	if (old_roles)
		return (old_roles & smd_dev_type2role(st));

	rc = smd_db_fetch(TABLE_TGTS[st], &tgt_id, sizeof(tgt_id), &assigned, sizeof(assigned));
	if (rc == -DER_NONEXIST) {
		return 0;
	} else if (rc) {
		DL_ERROR(rc, "Failed to fetch target table");
		return rc;
	}

	return uuid_compare(old_id, assigned.uuid) == 0 ? 1 : 0;
}

/*
 * When the 'old_roles' isn't specified, it must be called from ddb device replacing.
 *
 * The purpose of 'ddb dev_replace' is to replace an old device which can't be discovered
 * by SPDK anymore (that will fail engine start), in such case, the replaced device
 * should be marked as FAULTY, when engine started later, user will be able to use the
 * 'dmg storage replace' command with same old and new device ID to revive the device.
 */
int
smd_dev_replace(uuid_t old_id, uuid_t new_id, unsigned int old_roles)
{
	struct smd_device	 dev;
	struct d_uuid		 id;
	enum smd_dev_type	 st;
	int			 i, rc;

	D_ASSERT(uuid_compare(old_id, new_id) != 0);

	smd_db_lock();
	/* Fetch new device */
	uuid_copy(id.uuid, new_id);
	rc = smd_db_fetch(TABLE_DEV, &id, sizeof(id), &dev, sizeof(dev));
	if (rc == 0) {
		D_ERROR("New dev "DF_UUID" is inuse\n", DP_UUID(&id.uuid));
		rc = -DER_INVAL;
		goto out;
	} else if (rc != -DER_NONEXIST) {
		D_ERROR("Fetch new dev "DF_UUID" failed. "DF_RC"\n",
			DP_UUID(id.uuid), DP_RC(rc));
		goto out;
	}

	/* Fetch old device */
	uuid_copy(id.uuid, old_id);
	rc = smd_db_fetch(TABLE_DEV, &id, sizeof(id), &dev, sizeof(dev));
	if (rc) {
		D_ERROR("Fetch dev "DF_UUID" failed. "DF_RC"\n",
			DP_UUID(&id.uuid), DP_RC(rc));
		goto out;
	}

	/*
	 * Sanity check to old device, don't have to check the old dev state when it's
	 * from 'ddb dev_replace'
	 */
	if (old_roles && dev.sd_state != SMD_DEV_FAULTY) {
		D_ERROR("Dev "DF_UUID" isn't in faulty\n", DP_UUID(&id.uuid));
		rc = -DER_INVAL;
		goto out;
	}

	if (dev.sd_tgt_cnt >= SMD_MAX_TGT_CNT || dev.sd_tgt_cnt == 0) {
		D_ERROR("Invalid targets (%d) for dev "DF_UUID"\n",
			dev.sd_tgt_cnt, DP_UUID(&id.uuid));
		rc = -DER_INVAL;
		goto out;
	}

	/* Update device, target and pool tables in one transaction */
	rc = smd_db_tx_begin();
	if (rc)
		goto out;

	/* Delete old device in device table */
	rc = smd_db_delete(TABLE_DEV, &id, sizeof(id));
	if (rc) {
		D_ERROR("Failed to delete old dev "DF_UUID". "DF_RC"\n",
			DP_UUID(&id.uuid), DP_RC(rc));
		goto tx_end;
	}

	/* Insert new device in device table */
	uuid_copy(id.uuid, new_id);
	/* The replaced device from 'ddb dev_replace' should start with FAULTY state */
	dev.sd_state = old_roles ? SMD_DEV_NORMAL : SMD_DEV_FAULTY;
	rc = smd_db_upsert(TABLE_DEV, &id, sizeof(id), &dev, sizeof(dev));
	if (rc) {
		D_ERROR("Failed to insert new dev "DF_UUID". "DF_RC"\n",
			DP_UUID(&id.uuid), DP_RC(rc));
		goto tx_end;
	}

	/* Replace old device ID with new device ID in target table */
	for (i = 0; i < dev.sd_tgt_cnt; i++) {
		uint32_t tgt_id = dev.sd_tgts[i];

		for (st = SMD_DEV_TYPE_DATA; st < SMD_DEV_TYPE_MAX; st++) {
			rc = tgt_need_replace(old_id, tgt_id, old_roles, st);
			if (rc < 0)
				goto tx_end;
			else if (rc == 0)
				continue;

			rc = smd_db_upsert(TABLE_TGTS[st], &tgt_id, sizeof(tgt_id),
					   &id, sizeof(id));
			if (rc) {
				DL_ERROR(rc, "Update target:%u type:%u failed.", tgt_id, st);
				goto tx_end;
			}
		}
	}
tx_end:
	rc = smd_db_tx_end(rc);
out:
	smd_db_unlock();
	return rc;
}
