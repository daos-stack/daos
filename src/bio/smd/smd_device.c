/**
 * (C) Copyright 2018-2019 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(bio)

#include <daos/common.h>
#include <daos/dtx.h>
#include "smd_internal.h"

struct smd_dev_entry {
	enum smd_dev_state	sde_state;
	uint32_t		sde_tgt_cnt;
	int			sde_tgts[SMD_MAX_TGT_CNT];
};

int
smd_dev_assign(uuid_t dev_id, int tgt_id)
{
	struct smd_dev_entry	entry = { 0 };
	d_iov_t			key, val;
	struct d_uuid		key_dev, bond_dev;
	int			rc;

	D_ASSERT(!daos_handle_is_inval(smd_store.ss_dev_hdl));
	D_ASSERT(!daos_handle_is_inval(smd_store.ss_tgt_hdl));

	uuid_copy(key_dev.uuid, dev_id);
	D_RWLOCK_WRLOCK(&smd_store.ss_rwlock);

	/* Check if the target is already bound to a device */
	d_iov_set(&key, &tgt_id, sizeof(tgt_id));
	d_iov_set(&val, &bond_dev, sizeof(bond_dev));
	rc = dbtree_fetch(smd_store.ss_tgt_hdl, BTR_PROBE_EQ,
			  DAOS_INTENT_DEFAULT, &key, NULL, &val);
	if (rc == 0) {
		D_ERROR("Target %d is already bound to dev "DF_UUID"\n",
			tgt_id, DP_UUID(&bond_dev.uuid));
		rc = -DER_EXIST;
		goto out;
	} else if (rc != -DER_NONEXIST) {
		D_ERROR("Get target %d failed. %d\n", tgt_id, rc);
		goto out;
	}

	/* Fetch device if it's already existing */
	d_iov_set(&key, &key_dev, sizeof(key_dev));
	d_iov_set(&val, &entry, sizeof(entry));
	rc = dbtree_fetch(smd_store.ss_dev_hdl, BTR_PROBE_EQ,
			  DAOS_INTENT_DEFAULT, &key, NULL, &val);
	if (rc == 0) {
		if (entry.sde_tgt_cnt >= SMD_MAX_TGT_CNT) {
			D_ERROR("Dev "DF_UUID" is assigned to too many "
				"targets (%d)\n", DP_UUID(&key_dev.uuid),
				entry.sde_tgt_cnt);
			rc = -DER_OVERFLOW;
			goto out;
		}
		entry.sde_tgts[entry.sde_tgt_cnt] = tgt_id;
		entry.sde_tgt_cnt += 1;
	} else if (rc == -DER_NONEXIST) {
		entry.sde_state = SMD_DEV_NORMAL;
		entry.sde_tgt_cnt = 1;
		entry.sde_tgts[0] = tgt_id;
	} else {
		D_ERROR("Fetch dev "DF_UUID" failed. %d\n",
			DP_UUID(&key_dev.uuid), rc);
		goto out;
	}

	/* Update device and target tables in same transaction */
	rc = smd_tx_begin(&smd_store);
	if (rc)
		goto out;

	rc = dbtree_update(smd_store.ss_dev_hdl, &key, &val);
	if (rc) {
		D_ERROR("Update dev "DF_UUID" failed. %d\n",
			DP_UUID(&key_dev.uuid), rc);
		goto tx_end;
	}

	d_iov_set(&key, &tgt_id, sizeof(tgt_id));
	d_iov_set(&val, &key_dev, sizeof(key_dev));
	rc = dbtree_update(smd_store.ss_tgt_hdl, &key, &val);
	if (rc) {
		D_ERROR("Update target %d failed. %d\n", tgt_id, rc);
		goto tx_end;
	}
tx_end:
	rc = smd_tx_end(&smd_store, rc);
out:
	D_RWLOCK_UNLOCK(&smd_store.ss_rwlock);
	return rc;
}

int
smd_dev_unassign(uuid_t dev_id, int tgt_id)
{
	return -DER_NOSYS;
}

int
smd_dev_set_state(uuid_t dev_id, enum smd_dev_state state)
{
	struct smd_dev_entry	entry = { 0 };
	d_iov_t			key, val;
	struct d_uuid		key_dev;
	int			rc;

	D_ASSERT(state == SMD_DEV_NORMAL || state == SMD_DEV_FAULTY);
	D_ASSERT(!daos_handle_is_inval(smd_store.ss_dev_hdl));

	uuid_copy(key_dev.uuid, dev_id);
	D_RWLOCK_WRLOCK(&smd_store.ss_rwlock);

	d_iov_set(&key, &key_dev, sizeof(key_dev));
	d_iov_set(&val, &entry, sizeof(entry));
	rc = dbtree_fetch(smd_store.ss_dev_hdl, BTR_PROBE_EQ,
			  DAOS_INTENT_DEFAULT, &key, NULL, &val);
	if (rc) {
		D_ERROR("Fetch dev "DF_UUID" failed. %d\n",
			DP_UUID(&key_dev.uuid), rc);
		goto out;
	}

	entry.sde_state = state;
	rc = dbtree_update(smd_store.ss_dev_hdl, &key, &val);
	if (rc) {
		D_ERROR("Update dev "DF_UUID" failed. %d\n",
			DP_UUID(&key_dev.uuid), rc);
		goto out;
	}
out:
	D_RWLOCK_UNLOCK(&smd_store.ss_rwlock);
	return rc;
}

static struct smd_dev_info *
create_dev_info(uuid_t dev_id, struct smd_dev_entry *entry)
{
	struct smd_dev_info	*info;
	int			 i;

	D_ALLOC_PTR(info);
	if (info == NULL)
		return NULL;

	D_ALLOC_ARRAY(info->sdi_tgts, SMD_MAX_TGT_CNT);
	if (info->sdi_tgts == NULL) {
		D_FREE(info);
		return NULL;
	}

	D_INIT_LIST_HEAD(&info->sdi_link);
	uuid_copy(info->sdi_id, dev_id);
	info->sdi_state = entry->sde_state;
	info->sdi_tgt_cnt = entry->sde_tgt_cnt;
	for (i = 0; i < info->sdi_tgt_cnt; i++)
		info->sdi_tgts[i] = entry->sde_tgts[i];

	return info;
}

static int
fetch_dev_info(uuid_t dev_id, struct smd_dev_info **dev_info)
{
	struct smd_dev_info	*info;
	struct smd_dev_entry	 entry = { 0 };
	struct d_uuid		 key_dev;
	d_iov_t			 key, val;
	int			 rc;

	D_ASSERT(!daos_handle_is_inval(smd_store.ss_dev_hdl));

	uuid_copy(key_dev.uuid, dev_id);

	d_iov_set(&key, &key_dev, sizeof(key_dev));
	d_iov_set(&val, &entry, sizeof(entry));
	rc = dbtree_fetch(smd_store.ss_dev_hdl, BTR_PROBE_EQ,
			  DAOS_INTENT_DEFAULT, &key, NULL, &val);
	if (rc) {
		D_ERROR("Fetch dev "DF_UUID" failed. %d\n",
			DP_UUID(&key_dev.uuid), rc);
		return rc;
	}

	info = create_dev_info(dev_id, &entry);
	if (info == NULL)
		return -DER_NOMEM;

	*dev_info = info;
	return 0;
}

int
smd_dev_get_by_id(uuid_t dev_id, struct smd_dev_info **dev_info)
{
	int	rc;

	D_RWLOCK_RDLOCK(&smd_store.ss_rwlock);
	rc = fetch_dev_info(dev_id, dev_info);
	D_RWLOCK_UNLOCK(&smd_store.ss_rwlock);
	return rc;
}

int
smd_dev_get_by_tgt(int tgt_id, struct smd_dev_info **dev_info)
{
	struct d_uuid	bond_dev;
	d_iov_t		key, val;
	int		rc;

	D_ASSERT(!daos_handle_is_inval(smd_store.ss_tgt_hdl));
	D_RWLOCK_RDLOCK(&smd_store.ss_rwlock);

	d_iov_set(&key, &tgt_id, sizeof(tgt_id));
	d_iov_set(&val, &bond_dev, sizeof(bond_dev));
	rc = dbtree_fetch(smd_store.ss_tgt_hdl, BTR_PROBE_EQ,
			  DAOS_INTENT_DEFAULT, &key, NULL, &val);
	if (rc) {
		D_ERROR("Fetch target %d failed. %d\n", tgt_id, rc);
		goto out;
	}

	rc = fetch_dev_info(bond_dev.uuid, dev_info);
out:
	D_RWLOCK_UNLOCK(&smd_store.ss_rwlock);
	return rc;
}

int
smd_dev_list(d_list_t *dev_list)
{
	struct smd_dev_info	*info;
	struct smd_dev_entry	 entry = { 0 };
	daos_handle_t		 iter_hdl;
	struct d_uuid		 key_dev;
	d_iov_t			 key, val;
	int			 rc;

	D_ASSERT(dev_list && d_list_empty(dev_list));
	D_ASSERT(!daos_handle_is_inval(smd_store.ss_dev_hdl));

	D_RWLOCK_RDLOCK(&smd_store.ss_rwlock);

	rc = dbtree_iter_prepare(smd_store.ss_dev_hdl, 0, &iter_hdl);
	if (rc) {
		D_ERROR("Prepare device iterator failed. %d\n", rc);
		goto out;
	}

	rc = dbtree_iter_probe(iter_hdl, BTR_PROBE_FIRST, DAOS_INTENT_DEFAULT,
			       NULL, NULL);
	if (rc) {
		if (rc != -DER_NONEXIST)
			D_ERROR("Probe first device failed. %d\n", rc);
		else
			rc = 0;
		goto done;
	}

	d_iov_set(&key, &key_dev, sizeof(key_dev));
	d_iov_set(&val, &entry, sizeof(entry));

	while (1) {
		rc = dbtree_iter_fetch(iter_hdl, &key, &val, NULL);
		if (rc != 0) {
			D_ERROR("Iterate fetch failed. %d\n", rc);
			break;
		}

		info = create_dev_info(key_dev.uuid, &entry);
		if (info == NULL) {
			rc = -DER_NOMEM;
			D_ERROR("Create device info failed. %d\n", rc);
			break;
		}
		d_list_add_tail(&info->sdi_link, dev_list);

		rc = dbtree_iter_next(iter_hdl);
		if (rc) {
			if (rc != -DER_NONEXIST)
				D_ERROR("Iterate next failed. %d\n", rc);
			else
				rc = 0;
			break;
		}
	}
done:
	dbtree_iter_finish(iter_hdl);
out:
	D_RWLOCK_UNLOCK(&smd_store.ss_rwlock);
	return rc;
}
