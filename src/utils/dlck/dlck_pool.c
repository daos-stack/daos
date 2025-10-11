/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <gurt/common.h>
#include <daos/common.h>
#include <daos_srv/mgmt_tgt_common.h>
#include <daos_srv/smd.h>
#include <daos_srv/vos.h>

#include "dlck_pool.h"

int
dlck_pool_mkdir(const char *storage_path, uuid_t po_uuid, struct dlck_print *dp)
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
	if ((rc != 0 && errno != EEXIST) || DAOS_FAIL_CHECK(DLCK_FAULT_CREATE_POOL_DIR)) {
		if (d_fault_inject_is_enabled()) {
			errno = daos_fail_value_get();
		}
		rc = daos_errno2der(errno);
		DLCK_PRINTF_ERRL(dp, "Cannot create a pool directory: %s: " DF_RC "\n", path,
				 DP_RC(rc));
	} else {
		rc = DER_SUCCESS;
	}

	D_FREE(path);
	return rc;
}

int
dlck_pool_mkdir_all(const char *storage_path, d_list_t *files, struct dlck_print *dp)
{
	struct dlck_file *file;
	int               rc;

	if (d_list_empty(files)) {
		return DER_SUCCESS;
	}

	d_list_for_each_entry(file, files, link) {
		rc = dlck_pool_mkdir(storage_path, file->po_uuid, dp);
		if (rc != DER_SUCCESS) {
			return rc;
		}
	}

	return DER_SUCCESS;
}

static int
dlck_file_preallocate(const char *storage_path, uuid_t po_uuid, int tgt_id)
{
	struct smd_pool_info *pool_info = NULL;
	int                   rc;

	rc = smd_pool_get_info(po_uuid, &pool_info);
	if (rc != 0) {
		return rc;
	}

	rc = ds_mgmt_tgt_preallocate(po_uuid, pool_info->spi_scm_sz, tgt_id, storage_path);

	smd_pool_free_info(pool_info);

	return rc;
}

int
dlck_pool_open(const char *storage_path, uuid_t po_uuid, int tgt_id, daos_handle_t *poh)
{
	char              *path;
	int                rc;

	rc = ds_mgmt_file(storage_path, po_uuid, VOS_FILE, &tgt_id, &path);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	/** no MD-on-SSD mode means no file preallocation is necessary */
	if (bio_nvme_configured(SMD_DEV_TYPE_META)) {
		rc = dlck_file_preallocate(storage_path, po_uuid, tgt_id);
		if (rc != 0) {
			goto fail;
		}
	}

	rc = vos_pool_open(path, po_uuid, DLCK_POOL_OPEN_FLAGS, poh);

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
