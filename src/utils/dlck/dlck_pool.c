/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <gurt/common.h>
#include <daos/common.h>
#include <daos_srv/daos_mgmt_srv.h>
#include <daos_srv/smd.h>
#include <daos_srv/vos.h>

#include "dlck_pool.h"

int
mgmt_file_preallocate(const char *path, uuid_t uuid, daos_size_t scm_size);

int
dlck_pool_mkdir(const char *storage_path, uuid_t po_uuid)
{
	char  po_uuid_str[UUID_STR_LEN];
	char *path;
	int   rc;

	uuid_unparse(po_uuid, po_uuid_str);

	D_ASPRINTF(path, "%s/%s/", storage_path, po_uuid_str);
	if (path == NULL) {
		return -DER_NOMEM;
	}

	rc = mkdir(path, 0777);
	D_FREE(path);
	if (rc != 0 && errno != EEXIST) {
		return daos_errno2der(errno);
	} else {
		return DER_SUCCESS;
	}
}

static int
dlck_file_preallocate(const char *path, uuid_t uuid)
{
	struct smd_pool_info *pool_info = NULL;
	int                   rc;

	rc = smd_pool_get_info(uuid, &pool_info);
	if (rc != 0) {
		return rc;
	}

	rc = mgmt_file_preallocate(path, uuid, pool_info->spi_scm_sz);

	smd_pool_free_info(pool_info);

	return rc;
}

int
dlck_pool_open(const char *storage_path, uuid_t po_uuid, int tgt_id, daos_handle_t *poh)
{
	char              *path;
	char               po_uuid_str[UUID_STR_LEN];
	const unsigned int flags = VOS_POF_EXCL | VOS_POF_EXTERNAL_FLUSH | VOS_POF_FOR_FEATURE_FLAG;
	int                rc;

	uuid_unparse(po_uuid, po_uuid_str);

	D_ASPRINTF(path, "%s/%s/" VOS_FILE "%d", storage_path, po_uuid_str, tgt_id);
	if (path == NULL) {
		return -DER_NOMEM;
	}

	/** no MD-on-SSD mode means no file preallocation is necessary */
	if (bio_nvme_configured(SMD_DEV_TYPE_META)) {
		rc = dlck_file_preallocate(path, po_uuid);
		if (rc != 0) {
			goto fail;
		}
	}

	rc = vos_pool_open(path, po_uuid, flags, poh);

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
		return -DER_NOMEM;
	}

	uuid_copy(elm->uuid, entry->ie_couuid);
	d_list_add(&elm->link, co_uuids);

	return DER_SUCCESS;
}

int
dlck_pool_cont_list(daos_handle_t poh, d_list_t *co_uuids)
{
	/** loop over containers */
	vos_iter_param_t        param   = {0};
	struct vos_iter_anchors anchors = {0};

	param.ip_hdl        = poh;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	param.ip_flags      = VOS_IT_FOR_CHECK;

	return vos_iterate(&param, VOS_ITER_COUUID, false, &anchors, cont_list_append, NULL,
			   co_uuids, NULL);
}
