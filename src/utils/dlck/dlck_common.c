/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <daos/mem.h>
#include <daos/btree_class.h>
#include <daos_srv/vos.h>
#include <daos_srv/daos_mgmt_srv.h>
#include <daos_version.h>

#include "dlck_common.h"

int
xxx_vos_preallocate(const char *path, uuid_t uuid, daos_size_t scm_size);

int
dlck_pool_mkdir(const char *storage_path, uuid_t po_uuid)
{
	char  po_uuid_str[UUID_STR_LEN];
	char *path;
	int   rc;

	uuid_unparse(po_uuid, po_uuid_str);

	rc = asprintf(&path, "%s/%s/", storage_path, po_uuid_str);
	if (rc < 0) {
		return rc;
	}

	rc = mkdir(path, 0777);
	D_FREE(path);
	if (rc != 0 && errno != EEXIST) {
		return errno;
	} else {
		return 0;
	}
}

static int
dlck_recreate(const char *path, uuid_t uuid)
{
	struct smd_pool_info *pool_info = NULL;
	int                   rc;

	rc = smd_pool_get_info(uuid, &pool_info);
	if (rc != 0) {
		return rc;
	}

	rc = xxx_vos_preallocate(path, uuid, pool_info->spi_scm_sz);
	if (rc != 0) {
		goto out;
	}

out:
	smd_pool_free_info(pool_info);

	return rc;
}

int
dlck_pool_open(const char *storage_path, struct dlck_file *file, int tgt_id, daos_handle_t *poh)
{
	char              *path;
	char               po_uuid[UUID_STR_LEN];
	const unsigned int flags = VOS_POF_EXCL | VOS_POF_EXTERNAL_FLUSH | VOS_POF_FOR_FEATURE_FLAG;
	int                rc;

	uuid_unparse(file->po_uuid, po_uuid);

	rc = asprintf(&path, "%s/%s/" VOS_FILE "%d", storage_path, po_uuid, tgt_id);
	if (rc < 0) {
		goto fail;
	}

	if (bio_nvme_configured(SMD_DEV_TYPE_META)) {
		rc = dlck_recreate(path, file->po_uuid);
		if (rc != 0) {
			goto fail;
		}
	}

	rc = vos_pool_open(path, file->po_uuid, flags, poh);
	if (rc != 0) {
		goto fail;
	}

fail:
	D_FREE(path);

	return rc;
}

/**
 * Just add the container's UUID to the provided list.
 */
static int
cont_list_append(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
		 vos_iter_param_t *param, void *cb_arg, unsigned int *acts)
{
	d_list_t                 *co_uuids = cb_arg;
	struct co_uuid_list_elem *elm;

	D_ALLOC_PTR(elm);
	if (elm == NULL) {
		return ENOMEM;
	}

	uuid_copy(elm->uuid, entry->ie_couuid);
	d_list_add(&elm->link, co_uuids);

	return 0;
}

int
dlck_pool_cont_list(daos_handle_t poh, d_list_t *co_uuids)
{
	/** loop over containers */
	vos_iter_param_t        param   = {0};
	struct vos_iter_anchors anchors = {0};
	int                     rc;

	param.ip_hdl        = poh;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	param.ip_flags      = VOS_IT_FOR_CHECK;

	rc = vos_iterate(&param, VOS_ITER_COUUID, false, &anchors, cont_list_append, NULL, co_uuids,
			 NULL);

	if (rc != 0) {
		return rc;
	}

	return rc;
}